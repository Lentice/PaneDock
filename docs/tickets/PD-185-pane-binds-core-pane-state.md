# PD-185 — `Pane` 綁定 `core::PaneState*`，Group 切換改為重新綁定而非複製

Phase 7 · architecture · Depends on: PD-182, PD-183, PD-184

- Source: 2026-09-04 使用者重構討論（延續 PD-178～PD-183 的同一輪需求）。使用者原話：「pane 應該要負責自己的狀態」「pane 可以拿到 state 的實體位址，直接改，不用複製來複製去」「切走前回存到上層的 store 中，下次需要的時候再拿出來 load 到 pane」。
- Priority: MEDIUM——本票不新增功能，但它決定 PD-186 之後每個 pane 操作的資料路徑。

## Outcome

`app_shell::Pane` 多一個成員：指向它目前顯示中的那個 `core::PaneState` 的裸指標。Group 切換不再是「把資料複製進 pane」或「從 pane 複製回去」，而是**重新指向**——因為 `Pane` 指的就是 `ApplicationState` 裡那個物件本身，權威資料在任何時刻都只有一份。

行為與視覺零變更。本票**只建立並維護綁定**，不搬移任何既有的 tab 操作邏輯（那是 PD-186）。

## 覆寫聲明：本票覆寫 `docs/tickets.md` 2026-09-03 條目的模組契約 (2)

該條目原文：

> (2) **真相來源硬切**——`core::PaneState` 擁有 tabs/location/view mode/sort/history/active tab id(會被存檔、有 unit test)，`Pane` 只擁有 HWND/幾何/hover/捲動/drag placeholder(永不存檔)，且 **`Pane` 不得持有 tab 清單複本**，避免切 Group 後才浮現的不同步缺陷。

**本票覆寫的部分**：解除「`Pane` 不得觸及 tab 資料」這一條，改為「`Pane` 持有一個指向 `core::PaneState` 的**指標**」。

**本票沒有覆寫、而且刻意強化的部分**：那條契約真正要防的是「**複本**造成的不同步」。本票的手段恰好是消滅複本——`Pane` 持有的不是複製品而是同一個物件的位址，因此「切 Group 後才浮現的不同步缺陷」在結構上不可能發生（沒有第二份資料可以走樣）。原契約以「禁止持有」達成的目標，本票以「持有的就是本體」達成。

**新證據**：使用者 2026-09-04 明確要求 pane 管理自己的狀態、且明確拒絕複製來回；同時 2026-09-04 的原始碼調查確認 `PaneState` 的位址可以被保證穩定（PD-184），使「持有指標」從當時不成立的選項變成成立的選項。當時之所以選擇硬切，是因為沒人確認過位址穩定性。

**同時放棄的中途方案**：討論過程中一度規劃的「切走前 `snapshot_pane_state()` 複製回 store、切入時 `load` 複製進 pane」的 pull-model **不採用**——一旦 `Pane` 指的就是本體，那兩個複製步驟都是多餘的，而且每一個複製步驟都是一個「忘記呼叫就資料遺失」的新失敗點（PD-086 正是這個形狀）。

## 已確認的現況（2026-09-04 工作樹）

- `src/app_shell/pane.h:50-205`：`Pane` 目前持有 11 個 HWND、tab strip 的視覺狀態、drag target 與 `ExplorerHost`；**沒有**任何 `core::PaneState` 成員。`pane.h:20` 已經 include `core/model.h`（為了 `ShellLocation`），因此本票不新增任何模組相依。
- `AppState::panes` 是 `std::array<panedock::app_shell::Pane, kExplorerCount>`（`main.cpp:557`），4 個固定插槽，在 `WM_CREATE` 建立、程式結束才 destroy。版型只改變它們的可見性與大小，**不重建**。
- 協調層取得 pane 資料的既有寫法一律是 `active_group(state).panes[pane_index]`（`main.cpp` 內約 85 處）。`active_group` 定義於 `main.cpp:699-717`，以 `active_group_id` 做 `find_if`。
- Group 生命週期與 active Group 變動點：
  - `activate_group`（`main.cpp:2927-2955`）——`:2938` 改寫 `active_group_id`
  - `add_group`（`:2983-3004`）——`:3003` 尾端呼叫 `activate_group`
  - `duplicate_group`（`:3006-3015`）——`:3014` 尾端呼叫 `activate_group`
  - `delete_group`（`:3017-3046`）——`:3028` 呼叫 `core::delete_group`（該 Group 的 `PaneState` 在此**被解構**）
  - `move_group`（`:3048-3058`）、`finish_group_drag`（`:4168-4184`）——只重排 `groups`，**不**改變任何 `PaneState` 的位址（PD-184 已保證）
  - `set_layout`（`:3447-3478`）——`:3458` 呼叫 `core::switch_layout`，可能**成長** `group.panes`
- `capture_locations`（`main.cpp:1921` 起）是既有的「把 Shell view 當下的 location/view mode/sort 寫回 `core`」步驟，資料來源是 `ExplorerHost` 而非 `Pane` 的自有資料。**本票不動它**，它與綁定無關。
- PD-184 保證：`PaneState` 在其 Group 存活期間位址不變；`TabState` **不在**保證範圍內（`PaneState::tabs` 會 `push_back`／`erase`／`insert`）。

## Binding constraints — quoted, do not go looking for them

`docs/tickets.md` 2026-09-03 條目，模組契約 (1) 與 (3)（**本票不覆寫這兩條**）：

> (1) **單向依賴**——協調層 → `Pane`，`Pane` 不得持有 `AppState*`、回呼介面或 `std::function` 成員；一個只有單一實作的介面在本專案是已否決模式。
> (3) **跨 pane 的拖曳狀態留在協調層**，因為它天生跨越來源與目標兩個 pane。

`AGENTS.md`：

> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups — that is both the crash surface and a CPU/disk spike from simultaneous folder enumerations.

`AGENTS.md`：

> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`：

> Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller.

`docs/design-spec.md` §9.1：

> | `app_shell` | WinMain、STA 初始化、訊息迴圈、主視窗、命令路由 | 資料模型計算、Shell 呼叫 |
> | `core` | Group／pane／tab 資料模型、版型矩形計算、session 序列化與遷移 | **任何 HWND、COM 或 `windows.h`** |

**本票不違反這條**：資料模型本身仍然完全住在 `core`，`Pane` 只是持有它的位址；`core` 不會因此看見任何 HWND。

`docs/design-spec.md` §4.2：

> 以保活既有 view 並重新導覽達成,不重建 HWND。

`docs/design-spec.md` NFR-003 反應性：

> Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。

**本票如何遵守**：`rebind_panes` 是純指標賦值，不做任何 Shell 呼叫、不做 location 解析、不觸發同步導覽。PD-168／PD-169 把 Shell 解析與顯示名稱查詢移出 UI 執行緒同步路徑的成果，本票一行都不得回退。

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/app_shell/pane.h`、`src/app_shell/pane.cpp`（全檔，兩檔共約 400 行）。
- `src/app_shell/main.cpp:699-729`：`active_group`、`has_active_group`、`active_pane_index`。
- `src/app_shell/main.cpp:2927-2955`：`activate_group`。
- `src/app_shell/main.cpp:2983-3058`：`add_group`／`duplicate_group`／`delete_group`／`move_group`。
- `src/app_shell/main.cpp:3447-3478`：`set_layout`。
- `src/app_shell/main.cpp:4168-4184`：`finish_group_drag`。
- `src/app_shell/main.cpp:1921` 起：`capture_locations`（本票不改，但要理解它與綁定的分工）。
- `src/app_shell/main.cpp` 的 `WM_CREATE` 路徑與 `apply_layout`：`Pane::create` 的呼叫點與版型套用點。
- `src/core/model.cpp:184-212`、`:229-271`：`delete_group`、`reorder_group`、`switch_layout`。
- `docs/tickets/PD-184-panestate-address-stability-invariant.md`：位址穩定性不變式的範圍與界線。
- `docs/tickets/PD-182-pane-type-owns-ui-state.md`、`PD-183-pane-owns-explorer-host-lifetime.md`：`Pane` 既有的所有權界線與 `destroy()` 順序。

## Scope

1. `Pane` 新增成員與存取器：

   ```cpp
   // The PaneState this slot currently displays. Not owned: it lives in
   // core::ApplicationState and outlives every rebind (PD-184 guarantees its
   // address is stable for the group's lifetime). Null when this slot is not
   // showing anything (no active Group, or a layout with fewer panes).
   void bind(panedock::core::PaneState* state) noexcept;
   void unbind() noexcept;
   panedock::core::PaneState* pane_state() const noexcept;
   ```

   `Pane::destroy()`（`pane.cpp:81`）結尾必須 `unbind()`。

2. **綁定只在一個地方維護**：新增協調層自由函式

   ```cpp
   void rebind_panes(AppState& state) noexcept;
   ```

   語意：對 `i < kExplorerCount`，若有 active Group 且 `i < active_group(state).panes.size()`，則 `state.panes[i].bind(&active_group(state).panes[i])`，否則 `state.panes[i].unbind()`。

3. 在下列（且僅下列）位置呼叫 `rebind_panes`，每處都必須在對應的 `core` 變動**之後**：
   - 啟動時 session 載入並建立 4 個 `Pane` 之後（`WM_CREATE` 路徑）
   - `activate_group`：`:2938` 改寫 `active_group_id` 之後、`refresh_tab_strips` 之前
   - `delete_group`：`:3028` 的 `core::delete_group` 成功之後
   - `set_layout`：`:3458` 的 `core::switch_layout` 成功之後（pane 數量成長時）
   - 若 `add_group`／`duplicate_group` 已透過尾端的 `activate_group` 涵蓋，**不要**重複呼叫；在交接區寫明確認結果。

4. **`delete_group` 的解除綁定順序**：`core::delete_group` 會解構被刪 Group 的 `PaneState`。因此必須在 `:3028` 呼叫**之前**先對 4 個 `Pane` 全部 `unbind()`，成功後再 `rebind_panes`。在該處寫一段註解說明為何順序不可調換。

5. `move_group`／`finish_group_drag`／`reorder_group` **不需要**重新綁定（PD-184 保證重排 `groups` 不改變 `PaneState` 位址）。在 `move_group` 上方寫一行註解記錄這個結論與其依據，避免未來有人「保險起見」加上多餘的 rebind。

6. 加上 debug 期防線：`Pane` 內任何會用到 `pane_state()` 的路徑，在 debug build 下 `assert(bound_state_ != nullptr)`；`rebind_panes` 之後 `assert` 可見 pane 數量與 `panes.size()` 一致。

7. **重新綁定不得破壞飛行中導覽的身分驗證（NFR-003 相關）**：非同步導覽（PD-168）完成時會回呼 `navigation_request_is_current`（`main.cpp:775-789`），它以 generation ＋ `group_id` ＋ `tab_id` 三者比對（PD-170）。`rebind_panes` 只改 `Pane` 的指標，**不得**觸碰 `state.pending_navigation`、不得重設 generation、不得以「指標已換」當成新的判定依據。切 Group 時若某個 pane 有導覽在飛行中，舊結果必須照既有機制被 PD-170 的身分比對擋掉，而不是靠綁定狀態。實作後必須在交接區寫出「切 Group 時有 pending navigation」這個情境的追蹤結論（哪一段程式碼擋住了舊結果）。

8. **不改任何既有呼叫點的寫法**。`main.cpp` 現有約 85 處 `active_group(state).panes[pane_index]` 一行都不動——本票只是讓 `Pane` **也**能拿到同一個物件，收窄呼叫點是 PD-186 的工作。

9. 順手把本票碰到的函式簽章從 `AppState&` 收窄成實際需要的型別（PD-178～PD-183 的共同必辦項）。

## Non-goals

- **不**搬移任何 tab 操作（新增／關閉／切換／排序／跨 pane 搬移）到 `Pane`——那是 PD-186。
- **不**新增 `snapshot_pane_state()` / `load_pane_state()` 這類複製式 API（見上方覆寫聲明）。
- **不**讓 `Pane` 持有 `TabState*`（PD-184 明確不保證 `TabState` 的位址穩定）。
- **不**讓 `Pane` 持有 `AppState*`、`GroupState*`、回呼介面或 `std::function`（契約 (1)）。
- **不**動跨 pane 拖曳狀態（契約 (3)）。
- **不**改 `capture_locations`、`refresh_tab_strips`、`navigate_realized_panes`、`apply_layout` 的既有語意。
- **不**為每個 pane 註冊獨立的 window class 或 `WNDPROC`（該方向是 `docs/tickets.md` §候選 的既有登記項，觸發條件未變）。
- **不**改變 realize/derealize 政策、`core::plan_realization`、shutdown reducer 或任何 Shell 呼叫閘門。
- **不**改 session schema 或持久化行為。

## Acceptance Criteria

1. `Pane` 有 `bind`／`unbind`／`pane_state()`，且 `Pane::destroy()` 會 `unbind()`。
2. `rebind_panes` 是**唯一**設定該指標的函式；全樹沒有第二處呼叫 `Pane::bind`。
3. `delete_group` 在 `core::delete_group` 之前解除全部綁定，之後才重新綁定，且有註解說明順序理由。
4. `move_group`／`finish_group_drag` 沒有多餘的 rebind，且有一行註解記錄依據。
5. `main.cpp` 既有的 `active_group(state).panes[...]` 呼叫點語意與數量未改變（本票不做收窄）。
6. `tests/unit/pane_test.cpp` 新增涵蓋：
   - `bind` 後 `pane_state()` 回傳同一位址；`unbind` 後為 `nullptr`
   - `destroy()` 之後 `pane_state()` 為 `nullptr`
   - 綁定一個 `PaneState` 後，透過 `pane_state()` 修改 `active_tab_id`，原始 `PaneState` 物件同步可見（證明沒有複製）
7. **NFR-003 不退步**：`rebind_panes` 內沒有任何 Shell 呼叫、location 解析或同步導覽；`state.pending_navigation` 未被綁定路徑觸碰；「切 Group 時有導覽在飛行中」的追蹤結論已寫進交接區。
8. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。
9. `Pane` 仍不反向依賴協調層（見 Agent Checks 的 grep）。
10. 行為與視覺零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# Pane 不得反向依賴協調層（PD-182/PD-183 的既有檢查，本票必須維持）
Select-String -Path src/app_shell/pane.h -Pattern 'AppState|std::function|callback'
# Pane 不得持有 TabState 指標或 GroupState
Select-String -Path src/app_shell/pane.h -Pattern 'TabState\s*\*|GroupState'
# 不得出現複製式 API
Select-String -Path src/app_shell/pane.h -Pattern 'snapshot_pane_state|load_pane_state'
```

三條都必須無結果。

```powershell
# bind 只能有一個呼叫點（rebind_panes 內），加上 pane.cpp/測試中的定義與使用
Select-String -Path src/app_shell/main.cpp -Pattern '\.bind\('
```

必須只出現在 `rebind_panes` 內。

```powershell
# rebind_panes 的呼叫點清單（供交接區逐一核對）
Select-String -Path src/app_shell/main.cpp -Pattern 'rebind_panes\('
```

```powershell
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 3
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(5000)) { throw 'process survived graceful close' }
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

本票的風險集中在 Group 生命週期與版型變動時的綁定正確性，請完整執行：

1. 建立 3 個 Group（分別是 1／2／4 pane 版型），在每個 Group 內開 2～3 個 tab 並導覽到不同資料夾。
2. 三個 Group 之間來回切換 **10 次**，確認每次切回去 tab 清單、active tab 與路徑都正確。
3. 刪除**目前正在使用**的 Group，確認不崩潰、自動切到另一個 Group 且內容正確。
4. 刪除一個**非** active 的 Group，確認目前 Group 的內容完全不受影響。
5. 用拖曳把 Group 排序來回移動 3 次，確認目前 Group 的 pane 內容不受影響。
6. 在同一個 Group 內把版型 1 → 4 → 1 切換 5 次，確認 pane 內容正確、沒有錯位到別的 pane。
7. **緩慢／無法連線的 location（NFR-003）**：把某個 Group 的一個 tab 指向已拔除的隨身碟或離線的網路磁碟，然後在那個 Group 與其他 Group 之間來回切 5 次，確認：UI 全程不凍結、錯誤面板正常出現、Retry 可用、切走再切回狀態正確、其他 pane 不受影響。
8. 在一個仍在載入大型資料夾的 pane 上立刻切 Group，確認舊的載入結果不會寫進新 Group 的分頁（PD-170 的既有保護）。
9. 關閉程式再重新啟動，確認全部 Group 的狀態完整還原。
10. 每次測試後檢查工作管理員無殘留 `PaneDock.exe`。

## Handoff requirements

在 `## 交接區` 記錄：`rebind_panes` 的完整呼叫點清單與行號、`add_group`／`duplicate_group` 是否確實由 `activate_group` 涵蓋的確認結果、`delete_group` 的解除綁定順序、`move_group` 註解的最終措辭、新增的 `pane_test` 案例與結果、本票收窄掉的 `AppState&` 簽章數量（PD-178 系列的共同指標，PD-183 完成時的基準是 118 → 目前值）、`ctest` 全量結果、以及使用者實機檢查 8 項的逐項回報。

## 交接區

（實作者填寫）
