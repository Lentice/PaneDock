# PD-190 — 六支 pane chrome 刷新函式與 `pending_navigation` 收進 `Pane`

Phase 7 · architecture · Depends on: PD-186

- Source: 2026-09-04 使用者提問「還有哪些 pane 的狀態／操作／行為沒有放到 pane 之中」，並要求依盤點結果開票。本票是那次全檔盤點（`main.cpp` 5988 行）篩選後**真正值得搬**的剩餘部分。
- Priority: LOW-MEDIUM——零行為變更的收尾票。做完之後 `AppState` 不再有任何 pane-parallel 陣列。

## Outcome

1. `AppState::pending_navigation`（最後一個 `std::array<..., kExplorerCount>` 資料欄位）搬進 `Pane`。
2. 六支只服務單一 pane 的 chrome 刷新函式，簽章從 `(AppState&, std::size_t pane_index)` 收窄——能成為 `Pane` 成員的成為成員，需要協調層服務的則改吃 `Pane&`。
3. `active_tab(core::PaneState&)` 這個純存取器從 `main.cpp` 的自由函式變成 `Pane` 的查詢。

行為與視覺零變更。

## 盤點結論：本票**不做**哪些，以及為什麼（避免下一個 agent 重新提案）

撰票前的全檔盤點推翻了三項原本的候選，理由記在這裡：

| 原候選 | 不做的理由 |
|---|---|
| `PaneErrorOverlay` 從 `ExplorerHost` 搬到 `Pane` | **已經在 `Pane` 裡了。** `ExplorerHost explorer_host_` 是 `Pane` 的 private 成員（`pane.h:202`），`PaneErrorOverlay error_overlay_` 是 `ExplorerHost` 的 private 成員（`explorer_host.h:129`），`main.cpp` 對 `error_overlay`／`PaneErrorOverlay` **零引用**。PD-181＋PD-183 已完成封裝。唯一剩下的是檔案放在 `src/explorer_host/` 而它零 COM，屬純外觀問題，不值得動。 |
| 位址列＋自動完成獨立一張票 | **沒有自訂下拉可搬。** PD-044 的實作是 `main.cpp:2735` 呼叫一次原生 `SHAutoComplete`，下拉 UI 由 Windows Shell 負責。整群只有 31 行（`submit_address` 3414-3427、`address_edit_proc` 3429-3445），併進本票即可。 |
| `tab_context_menu_pane`／`tab_context_menu_tab_id` 搬進 `Pane` | **會把模型改壞。** 這兩個欄位是 singleton（全 app 同時只會有一個右鍵選單開著）。搬成 per-`Pane` 欄位會變成 4 個槽位而永遠只有 1 個非空，語意變差。維持在協調層。 |
| tab strip 那 ~830 行 | 維持 `docs/tickets.md` §候選，觸發條件已改為「PD-187～189 完成後重新評估」。其中 `cancel_tab_drag`／`finish_tab_drag`／`update_tab_drag`／`register_tab_drag_hover_targets` 依契約 (3) 本來就跨 pane，不可能整群變成 `Pane` 成員。 |

## 已確認的現況（2026-09-04 工作樹）

- `AppState::pending_navigation`（`main.cpp:563-564`）是 `std::array<core::NavigationRequest, kExplorerCount>`。**全檔只有三處**觸碰它：宣告（`:564`）、`begin_navigation`（`:752`）、`navigation_request_is_current`（`:778`）。PD-183 之後它是 `AppState` 唯一剩下的 pane-parallel 資料陣列（`panes` 本身是 `Pane` 陣列本體，不算）。
- `core::NavigationRequest` 定義於 `src/core/navigation.h:9`，含 generation／`group_id`／`tab_id`（PD-170）。
- 六支單一 pane 的 chrome 刷新函式：
  - `refresh_navigation_buttons(AppState&, std::size_t)` — `:1573-1595`
  - `refresh_navigation_chrome(AppState&, std::size_t)` — `:1596-1609`
  - `refresh_status_bar(AppState&, std::size_t) noexcept` — `:1684-1730`
  - `update_tab_strip_tooltips(AppState&, std::size_t) noexcept` — `:1731-1766`
  - `apply_tab_item_size(AppState&, std::size_t, bool = false)` — `:1768-1873`（105 行，本組最大）
  - `refresh_tab_strip(AppState&, std::size_t)` — `:1874-1896`
- `refresh_tab_strips(AppState&)`（`:1897-1900`）是對四個 pane 的迴圈，**留在協調層**。
- `active_tab(core::PaneState& pane)` — `:732-748`，純存取器，目前是 `main.cpp` 的自由函式。
- 位址列兩支：`submit_address(AppState&, std::size_t)` `:3414-3427`、`address_edit_proc(...)` `:3429-3445`。
- PD-184 的界線：`PaneState` 位址穩定，**`TabState` 不穩定**（`tabs` 有 `push_back`／`erase`／`insert`）。因此 `active_tab` 的回傳值只能當場使用，不得存成成員。
- PD-186 之後 pane 局部狀態一律由 `state.panes[i].pane_state()` 取得。

## Binding constraints — quoted, do not go looking for them

`docs/tickets.md` 2026-09-03 條目，模組契約（**本票不覆寫**）：

> (1) **單向依賴**——協調層 → `Pane`，`Pane` 不得持有 `AppState*`、回呼介面或 `std::function` 成員。
> (3) **跨 pane 的拖曳狀態留在協調層**，因為它天生跨越來源與目標兩個 pane。

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/design-spec.md` NFR-003 反應性：

> Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。

`docs/design-spec.md` §9.1：

> | `core` | Group／pane／tab 資料模型、版型矩形計算、session 序列化與遷移 | **任何 HWND、COM 或 `windows.h`** |

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/app_shell/pane.h`／`pane.cpp`：PD-185 的 `pane_state()`、PD-183 的 `host()`。
- `src/app_shell/main.cpp:563-564`、`:751-790`：`pending_navigation` 與它的兩個使用點。
- `src/core/navigation.h`：`NavigationRequest`、`navigation_request_matches`。
- `src/app_shell/main.cpp:732-748`：`active_tab`。
- `src/app_shell/main.cpp:1573-1900`：六支刷新函式與 `refresh_tab_strips`。
- `src/app_shell/main.cpp:3414-3445`：位址列兩支。
- `src/app_shell/tab_overflow.h:176-336`：`layout_tab_strip`（純函式，已經可直接被 `Pane` 呼叫，不需搬）。
- `docs/tickets/PD-184`（位址穩定性界線）、`PD-185`（綁定與 pending navigation 的關係）、`PD-186`（呼叫點收窄）。

## Scope

1. **`pending_navigation` 搬進 `Pane`**：改為 `Pane` 的成員（型別仍是 `core::NavigationRequest`），提供最小存取器。三個觸碰點（`:564`／`:752`／`:778`）相應改寫。**PD-170 的三重比對語意（generation ＋ `group_id` ＋ `tab_id`）一字不改**；本票只換儲存位置，不換判定邏輯。改完後 `AppState` 不得再有任何 `std::array<..., kExplorerCount>` 的資料欄位（`panes` 本體除外）。

2. **六支刷新函式逐支分類後收窄**。實作者必須先逐支判斷它是否需要協調層服務（`ShellCallScope`、`schedule_session_save`、`closing_`／`shutdown_deferred` 閘門、跨 pane 資料），然後：
   - **不需要** → 成為 `Pane` 的成員函式。
   - **需要** → 保留為 `main.cpp` 的自由函式，但簽章從 `(AppState&, std::size_t)` 收窄成吃 `Pane&`（必要時加上它真正需要的那一項協調層參數），**不得**繼續吃整個 `AppState&`。
   - **`refresh_status_bar` 特別注意**：它讀 Shell view 的 item count，那是 Shell 呼叫，必須包在協調層的 `ShellCallScope` 內（契約 (1) 不允許 `Pane` 自己開 scope）。預期它屬於第二類。
   - 交接區必須附上六支的分類表與理由。

3. **`active_tab` 成為 `Pane` 的查詢**：`Pane::active_tab()` 回傳 `core::TabState*`（綁定為空時回 `nullptr`）。在宣告上方註解寫明：**回傳值只能當場使用，絕不能存成成員**——`PaneState::tabs` 會 `push_back`／`erase`／`insert`，PD-184 明確不保證 `TabState` 的位址穩定。`main.cpp` 既有的 `active_tab(...)` 呼叫點改用它。

4. **位址列兩支併入**：`submit_address` 與 `address_edit_proc` 依第 2 點的同一規則分類收窄。`SHAutoComplete`（`:2735`）的呼叫位置不動。

5. `refresh_tab_strips`（四 pane 迴圈）、`tab_context_menu_pane`／`tab_context_menu_tab_id`、全部拖曳狀態**留在協調層**，一行不動。

6. 回報 `AppState&` 函式計數（PD-183 完成時 118 → PD-186 值 → 本票值）。

## Non-goals

- **不**搬 `PaneErrorOverlay`（已封裝，見上方盤點結論）。
- **不**搬 tab strip 群（維持候選，觸發條件見 `docs/tickets.md` §候選）。
- **不**搬 `tab_context_menu_pane`／`tab_context_menu_tab_id`（singleton，搬了模型變差）。
- **不**搬任何跨 pane 的東西：tab 拖曳、splitter 拖曳、active pane 切換、版型矩形、`apply_layout`。
- **不**讓 `Pane` 自己開 `ShellCallScope`、排程 session 存檔或判斷關閉時機（契約 (1)，PD-187 已定案）。
- **不**改 PD-170 的導覽身分比對邏輯，只換 `NavigationRequest` 的儲存位置。
- **不**讓 `Pane` 持有 `TabState*`（PD-184 明確不保證其位址）。
- **不**改 `core` 的任何東西；`core::model.h` 的 pane 域函式（`add_tab`／`close_tab`／`set_active_tab`／`reorder_tab`／`move_tab`／歷史）**刻意**留在 `core`——那是唯一的測試接縫，`Pane` 只呼叫它們，不吸收它們。
- **不**改視覺或任何使用者可見行為。

## Acceptance Criteria

1. `AppState` 不再有任何 `std::array<..., kExplorerCount>` 的資料欄位（`panes` 本體除外）；`pending_navigation` 已在 `Pane` 內。
2. PD-170 的三重比對語意未改變；`navigation_request_is_current` 的判定結果與改動前逐條等價（交接區列出對照）。
3. 六支刷新函式加位址列兩支，全部不再吃 `AppState&`；交接區附分類表。
4. `Pane::active_tab()` 存在，註解寫明「不得存成成員」的理由；`main.cpp` 無任何長期持有的 `TabState*`。
5. `AppState&` 計數較 PD-186 完成時下降，數字記於交接區。
6. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。
7. `Pane` 仍不反向依賴協調層。
8. 行為與視覺零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# AppState 不得再有 pane-parallel 資料陣列（命中的應只有 panes 本體與 apply_layout 的局部暫存，
# 逐一人工核對後記進交接區——PD-183 交接區已說明這條 regex 無法字面清零的原因）
Select-String -Path src/app_shell/main.cpp -Pattern 'std::array<.*kExplorerCount>'
# pending_navigation 不得再出現在 AppState
Select-String -Path src/app_shell/main.cpp -Pattern 'state\.pending_navigation|state->pending_navigation'
```

第二條必須無結果。

```powershell
# 六支刷新函式不得再吃 AppState&
Select-String -Path src/app_shell/main.cpp -Pattern '(refresh_navigation_buttons|refresh_navigation_chrome|refresh_status_bar|update_tab_strip_tooltips|apply_tab_item_size|refresh_tab_strip|submit_address)\(AppState&'
# Pane 不得持有 TabState 指標成員，也不得反向依賴協調層
Select-String -Path src/app_shell/pane.h -Pattern 'TabState\s*\*\s*\w+_\s*(=|;)|AppState|std::function|callback'
# Pane 不得自己開 ShellCallScope
Select-String -Path src/app_shell/pane.cpp -Pattern 'ShellCallScope|schedule_session_save'
```

三條都必須無結果。

```powershell
(Select-String -Path src/app_shell/main.cpp -Pattern 'AppState&\s+state').Count
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

1. 每個 pane 各做一次：上一頁、下一頁、上一層、Refresh，確認導覽按鈕的啟用／停用狀態正確。
2. 每個 pane 各確認一次 footer：項目數、選取數、選取大小、分隔線。
3. 每個 pane 的 tab hover 一次，確認 tooltip 出現且內容正確。
4. 開到 10 個以上 tab 使其溢出，確認 tab 寬度縮放與捲動按鈕正常（`apply_tab_item_size`）。
5. 位址列輸入路徑 Enter、輸入時確認自動完成下拉出現、按 Esc 取消，各在兩個 pane 做一次。
6. **在一個仍在載入的 pane 上立刻切 Group／切 tab**，確認舊的導覽結果不會寫進新的分頁（PD-170 的保護，本票搬了它的儲存位置）。
7. 把一個 pane 指向已拔除的隨身碟，確認錯誤面板出現、Retry 可用、UI 不凍結。
8. 關閉再重啟，確認狀態完整還原；工作管理員無殘留 `PaneDock.exe`。

## Handoff requirements

在 `## 交接區` 記錄：`pending_navigation` 搬移後的存取方式與三個觸碰點的前後對照、PD-170 三重比對的等價性說明、六支刷新函式加位址列兩支的分類表（成為 `Pane` 成員 vs 收窄成吃 `Pane&`，含各自理由）、`Pane::active_tab()` 的註解措辭、`AppState&` 計數前後值、`std::array<..., kExplorerCount>` regex 命中項的人工核對結果、`ctest` 全量結果、以及使用者實機檢查 8 項的逐項回報。

## 交接區

（實作者填寫）
