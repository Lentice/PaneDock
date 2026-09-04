# PD-184 — `core` 保證 `PaneState` 的位址穩定性，讓 UI 層可長期持有 `PaneState*`

Phase 7 · architecture · Depends on: PD-004, PD-006

- Source: 2026-09-04 使用者重構討論（延續 PD-178～PD-183 的同一輪需求）。使用者要求「pane 可以拿到 state 的實體位址，直接改，不用複製來複製去」。本票是那個要求的**前置條件**：在讓 `app_shell::Pane` 持有 `core::PaneState*`（PD-185）之前，必須先讓那個指標**在產品的所有既有操作下都不會失效**。
- Priority: HIGH——PD-185／PD-186 都站在本票的不變式上。本票不做，PD-185 就是一張製造 dangling pointer 的票。

## Outcome

`core` 明確保證並機械化驗證一條不變式：

> 一個 `PaneState` 物件一旦被建立進某個 `GroupState::panes`，它的**記憶體位址在該 Group 存活期間永不改變**。

行為零變更、序列化格式零變更、公開型別零變更。本票只加入 `reserve`、`static_assert`、`assert` 與一條會真的失敗的單元測試。

## 為什麼不改成 `std::vector<std::unique_ptr<PaneState>>`（覆寫 2026-09-04 討論中先選定的方向）

2026-09-04 討論中原本選定的手段是把 `ApplicationState::groups` 與 `GroupState::panes` 改成 `std::vector<std::unique_ptr<T>>`。**本票明確覆寫該選擇**，改用本票的手段，新證據來自撰票前的原始碼調查：

1. `GroupState::panes` **只會成長、永不縮減**（`src/core/model.cpp:245-247` 的註解與 `:248-253` 的成長迴圈：「Panes are stable identities and are never discarded or merged on a shrink: the template only changes which of them are visible.」），且上限是 `kMaxPaneCount = 4`（`src/core/model.h:11`）。因此只要建構時 `reserve(kMaxPaneCount)`，`push_back` 永遠不會重新配置。
2. `ApplicationState::groups` 的 `push_back`／`erase`／`insert` 會搬移 `GroupState` 元素，但搬移方式是 **move**；`std::vector<PaneState>` 的 move 是接管既有 heap buffer，不重新配置元素。因此 `groups` 的任何重排都**不會**改變 `PaneState` 的位址。（此保證的前提是 `GroupState` 的 move 為 `noexcept`，否則 vector 會退回 copy——本票用 `static_assert` 把這個前提鎖死。）
3. `unique_ptr` 版本的代價經清點為：`core` 三檔加 `main.cpp` 約 103 處機械解參照，且 `GroupState`／`ApplicationState`／`PaneState` 的 `operator==`（`src/core/model.h:41,49,60,81`）目前是 `= default`，一旦成員變成 `vector<unique_ptr<T>>`，預設比較會**靜默**退化成比較指標位址而非內容——`tests/unit/core_model_test.cpp:126,149,245,247` 與 `tests/unit/core_session_test.cpp:88,122,178,203,277,284,297,304` 共 12 條既有斷言會在**編譯通過的情況下於執行期**改變語意。這是可以避免的高風險改動。

本票的手段取得完全相同的性質（`PaneState*` 長期有效），diff 小一個數量級，且不動 `operator==`。

## 已確認的現況（2026-09-04 工作樹）

- `src/core/model.h:11`：`inline constexpr std::size_t kMaxPaneCount = 4;`
- `src/core/model.h:44-50`：`PaneState{ std::string id; std::vector<TabState> tabs; std::string active_tab_id; }`
- `src/core/model.h:52-61`：`GroupState{ ...; std::vector<PaneState> panes; ... }`，`operator==` 為 `= default`（`:60`）
- `GroupState::panes` 的**唯一**成長點：`switch_layout`（`src/core/model.cpp:229-271`），成長迴圈在 `:248-253`。**沒有任何縮減點**（`is_valid` 於 `model.cpp:103` 允許 `panes.size()` 大於目前版型的可見 pane 數）。
- `GroupState` 的建構／複製點：
  - `add_group`（`model.cpp:147-157`，`push_back` 於 `:152`）
  - `duplicate_group`（`model.cpp:169-182`，`push_back` 於 `:180`，**複製**來源 Group）
  - `delete_group`（`model.cpp:184-198`，`erase` 於 `:190`）
  - `reorder_group`（`model.cpp:200-212`，`erase`＋`insert` 於 `:207-208`）
  - `decode`（`src/core/session.cpp:455-545`，`GroupState group{...}` 於 `:509`，`panes.push_back` 於 `:540`，`groups.push_back` 於 `:542`）
  - `default_application_state`（`src/app_shell/main.cpp:676-697`，`group.panes.push_back` 於 `:682`，`groups.push_back` 於 `:691`）
- `PaneState::tabs` 則**會**被 `push_back`／`erase`／`insert`：`add_tab`（`model.cpp:277`）、`reorder_tab`（`:288-291`）、`move_tab`（`:311,318-320`）、`close_tab`（`:338`）。因此 `TabState` 的位址**不穩定**，本票不為它提供任何保證。
- 全樹沒有任何 `std::rotate`／`std::swap` 作用在 `groups` 或 `panes` 上（唯二的 `std::swap` 在 `pinned_locations_dialog.cpp:289,298`，與本票無關）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> **Keep `src/core` free of HWND, COM and `windows.h`.** It is the only automated test seam in this project (`docs/testing.md`); every type that leaks into it costs that seam.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`AGENTS.md`：

> **Every persisted config/setting file must be designed for forward extensibility.** …A schema change is additive…

（本票**不**改任何持久化欄位或 schema version，故該規則以「不觸發」的方式滿足。）

`docs/design-spec.md` §9.1：

> | `core` | Group／pane／tab 資料模型、版型矩形計算、session 序列化與遷移 | **任何 HWND、COM 或 `windows.h`** |

`docs/design-spec.md` §12.2：

> 資料模型不變式、版型矩形計算、session 序列化往返、schema 遷移、損壞文件處理、Group 變更操作。

`docs/development.md`：

> Do not add a dependency, background loop, framework, or abstraction without a measured need.

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/core/model.h`：`kMaxPaneCount`、`PaneState`、`GroupState`、`ApplicationState` 與四個 `= default` 的 `operator==`。
- `src/core/model.cpp:103-145`：`is_valid(GroupState)` 與 `is_valid(ApplicationState)`。
- `src/core/model.cpp:147-212`：`add_group`／`rename_group`／`duplicate_group`／`delete_group`／`reorder_group`。
- `src/core/model.cpp:229-271`：`switch_layout`（唯一成長點，含 `:245-247` 的「永不縮減」註解）。
- `src/core/session.cpp:455-545`：`decode`。
- `src/app_shell/main.cpp:676-697`：`default_application_state`。
- `tests/unit/core_model_test.cpp`、`tests/unit/core_session_test.cpp`：既有斷言，本票**不得**修改它們的語意。
- `tests/CMakeLists.txt:10-24`：表格式測試註冊。

## Scope

1. 在 `src/core/model.h` 加入一個小型自由函式（放在 `GroupState` 定義之後）：

   ```cpp
   // app_shell::Pane holds a raw core::PaneState* for the lifetime of the
   // group it displays (PD-185). Reserving the fixed maximum up front is what
   // makes that pointer safe: switch_layout only ever push_backs (model.cpp
   // "Panes are stable identities and are never discarded ... on a shrink"),
   // so with capacity kMaxPaneCount the vector never reallocates and no
   // PaneState is ever relocated.
   inline void reserve_panes(GroupState& group) {
       group.panes.reserve(kMaxPaneCount);
   }
   ```

2. 在**每一個** `GroupState` 進入 `ApplicationState::groups` 之前的建構點呼叫 `reserve_panes`：
   - `core::add_group`（`model.cpp:147-157`）
   - `core::duplicate_group`（`model.cpp:169-182`）——**複製出來的那份**也要 reserve（複製建構出的 `panes` 容量不保證是 4）
   - `core::decode`（`session.cpp`，`:540` 的 `panes.push_back` 之前）
   - `app_shell::default_application_state`（`main.cpp:682` 之前）

3. 在 `src/core/model.h` 加入把前提鎖死的 `static_assert`，附一行說明為何：

   ```cpp
   // std::vector relocates elements with move-if-noexcept. If GroupState ever
   // gains a member whose move can throw, the groups vector silently falls
   // back to copying, which relocates every PaneState and dangles every
   // pointer app_shell::Pane holds. Keep this assertion true.
   static_assert(std::is_nothrow_move_constructible_v<GroupState>);
   ```

4. 在 `switch_layout` 的成長迴圈（`model.cpp:248-253`）前後加入 debug 期防線：迴圈前記下 `candidate.panes.data()`，迴圈後 `assert` 它未改變，並 `assert(new_count <= kMaxPaneCount)`。

5. 新增單元測試 `tests/unit/core_pane_address_stability_test.cpp`，註冊進 `tests/CMakeLists.txt:10-24` 的表格（`"core_pane_address_stability|unit/core_pane_address_stability_test.cpp|panedock_core"`）。測試內容見下方 Acceptance Criteria 第 4 點。

6. 在 `docs/design-spec.md` §9.1 或 §10 **不**新增條文；改為在 `src/core/model.h` 的 `GroupState` 定義正上方寫一段註解，說明這條位址穩定性不變式屬於 `core` 的對外契約，以及 `TabState` **不**在保證範圍內。

## Non-goals

- **不**把 `ApplicationState::groups` 或 `GroupState::panes` 改成 `std::vector<std::unique_ptr<T>>`（理由見上方「為什麼不改」一節）。
- **不**改任何 `operator==`。
- **不**為 `TabState` 提供位址穩定性保證——`PaneState::tabs` 本來就會 `push_back`／`erase`／`insert`，任何持有 `TabState*` 的設計都是錯的，PD-185／PD-186 必須遵守這點。
- **不**改 session schema、schema version、JSON 欄位或序列化順序。
- **不**改 `is_valid` 的判定、`switch_layout` 的語意或任何既有 `core` 函式的簽章。
- **不**在本票讓 `app_shell::Pane` 持有任何 `PaneState*`（那是 PD-185）。
- **不**改 `src/app_shell` 的任何行為；`main.cpp` 只允許 `default_application_state` 一處加上 `reserve_panes` 呼叫。
- **不**讓 `core` 沾到 HWND／COM／`windows.h`。

## Acceptance Criteria

1. `GroupState::panes` 在所有建構路徑上容量皆為 `kMaxPaneCount`；`switch_layout` 的成長迴圈前後 `panes.data()` 不變（由 debug assert 與測試驗證）。
2. `static_assert(std::is_nothrow_move_constructible_v<GroupState>)` 存在且成立。
3. 既有 12 條 whole-struct `operator==` 斷言（`core_model_test.cpp:126,149,245,247`、`core_session_test.cpp:88,122,178,203,277,284,297,304`）語意未改變且全綠。
4. 新測試 `core_pane_address_stability` 涵蓋下列每一項：先取得 `PaneState* p = &group.panes[0]`，執行操作後 `EXPECT(p == &group.panes[0])` **且** `EXPECT(p->id == expected_id)`：
   - `add_group` 連續 8 次（跨越 vector 成長門檻）
   - `duplicate_group`
   - `delete_group`（刪除**另一個** Group，被觀察的 Group 位於被刪者之後）
   - `reorder_group`（把被觀察的 Group 從尾端移到頭端）
   - `switch_layout` 由 1 pane 成長到 4 pane（成長後仍須驗證 `&group.panes[0]` 不變）
   - `switch_layout` 由 4 pane 切回 1 pane（驗證 `panes.size()` 未縮減、位址不變）
   - `encode` → `decode` 往返後，**新**文件內的 `panes` 容量仍為 `kMaxPaneCount`
5. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。
6. `src/core` 仍不含 HWND／COM／`windows.h`（`panedock_shell_core_boundary` 仍綠）。
7. 產品行為、視覺與 `session.json` 內容零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 新測試已註冊並執行
ctest --test-dir build -N | Select-String 'core_pane_address_stability'
```

```powershell
# core 不得出現 unique_ptr 版方向的殘留，且不得改動 operator==
Select-String -Path src/core/model.h -Pattern 'unique_ptr'
Select-String -Path src/core/model.h -Pattern 'operator==\(const (GroupState|PaneState|TabState|ApplicationState)&\) const \{'
```

兩條都必須無結果（`operator==` 必須維持 `= default` 形式）。

```powershell
# 四個建構點都有 reserve
Select-String -Path src/core/model.cpp,src/core/session.cpp,src/app_shell/main.cpp -Pattern 'reserve_panes'
```

必須至少 4 個結果。

```powershell
# session.json 內容零變更：以既有 session 啟動再乾淨關閉，比對前後檔案
$s = "$env:LOCALAPPDATA\PaneDock\session.json"
$before = Get-FileHash $s
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 4
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(8000)) { throw 'process survived graceful close' }
Get-FileHash $s   # 與 $before 比對；欄位內容應等價（時間戳等既有可變欄位除外，需在交接區說明）
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

本票理論上零行為變更，實機檢查用來確認沒有意外副作用：

1. 啟動、切換 3 個 Group、關閉，重新啟動確認狀態完整還原。
2. 新增 Group、複製 Group、刪除 Group、拖曳排序 Group 各一次，關閉後重啟確認結果正確。
3. 在同一個 Group 內把版型從 1 pane 切到 4 pane 再切回 1 pane，確認切回去時原本的 pane 內容仍在（這正是「panes 永不縮減」的既有行為，本票不得改變它）。
4. 檢查工作管理員無殘留 `PaneDock.exe`。

## Handoff requirements

在 `## 交接區` 記錄：`reserve_panes` 的實際呼叫點清單與行號、`static_assert` 的位置、`switch_layout` debug assert 的寫法、新測試涵蓋的七個情境與執行結果、`ctest` 全量結果、以及 session.json 前後比對的結論（含任何既有可變欄位的說明）。

## 交接區

- `reserve_panes` 呼叫點：`src/core/model.cpp:153`（`add_group`）、`src/core/model.cpp:182`（`duplicate_group` 複製品）、`src/core/session.cpp:510`（`decode` 建立 Group 後、加入 panes 前）、`src/app_shell/main.cpp:680`（`default_application_state` 加入 panes 前）。另在 `src/core/model.cpp:247` 為 `switch_layout` 的 local candidate reserve，確保成長迴圈的 debug 防線成立；這不是進入 `ApplicationState::groups` 的建構點。
- `static_assert(std::is_nothrow_move_constructible_v<GroupState>)` 位於 `src/core/model.h:81`，`GroupState` 定義後、`ApplicationState` 定義前。
- `switch_layout` debug 防線位於 `src/core/model.cpp:253` 與 `:262-263`：`#ifndef NDEBUG` 下記錄 `candidate.panes.data()`，成長迴圈後 assert backing address 未變，並 assert `new_count <= kMaxPaneCount`。候選驗證成功後以 `src/core/model.cpp:277-286` 就地更新既有 pane、只 append 新 pane，保留外部 `PaneState*`；既有 layout 語意不變。
- 新測試 `tests/unit/core_pane_address_stability_test.cpp`（`panedock_core_pane_address_stability`）涵蓋七個情境：`add_group` 連續 8 次跨過 groups vector 成長、`duplicate_group`、刪除觀察 Group 之前的另一個 Group、尾端 Group `reorder_group` 到頭端、`switch_layout` 由 1 pane 成長到 4 pane、由 4 pane 切回 1 pane 且 size 不縮減、以及 `serialize_session` → `deserialize_session` 後容量仍為 `kMaxPaneCount`；各操作均驗證 `PaneState*` 位址與 `id`。
- Agent checks：configure 通過；build 通過；`ctest --test-dir build --output-on-failure` **22/22 通過**（`panedock_diagnostic_flag`、`panedock_tab_overflow`、`panedock_window_placement`、`panedock_core_model`、`panedock_core_layout`、`panedock_core_realization_plan`、`panedock_core_session`、`panedock_core_pane_address_stability`、`panedock_core_navigation`、`panedock_core_shutdown`、`panedock_pane`、`panedock_pane_control_id`、`panedock_shell_core_view_mode`、`panedock_file_operation_effect`、`panedock_shutdown_state`、`panedock_single_instance_relay`、`panedock_shell_reentry_gate`、`panedock_address_bar_failure`、`panedock_shell_core_boundary`、`panedock_startup_frame_order`、`panedock_launch_smoke`、`panedock_explorer_host_lifetime`）。
- `ctest --test-dir build -N | Select-String 'core_pane_address_stability'` 找到 Test #8；`unique_ptr` 與非 default `operator==` 兩條檢查皆無結果；四個正式建構點的 `reserve_panes` 檢查有 5 筆（含 candidate reserve）；`git diff --check` 通過。
- session.json 檢查以既有檔案正常啟動並 `CloseMainWindow` 關閉：before/after SHA-256 均為 `BDF8CFD18C4BA5063E724D0B8A866045BB00213B6D4B223CCF1261583A7283FC`，`hash_equal=True`，exit code 0。檔案沒有因本票產生任何差異，因此沒有需要排除的時間戳或其他既有可變欄位。第一次受限 sandbox 的 smoke 會因無法寫入 session 而停在預期的存檔失敗提示；改以可寫 session storage 重跑後 smoke 與全量 ctest 均通過。
