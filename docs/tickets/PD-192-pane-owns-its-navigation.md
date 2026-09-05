# PD-192 — pane 的導覽群收進 `Pane`

Phase 7 · architecture · Depends on: PD-191

- Source: 2026-09-05 使用者要求「main 中不要有屬於 pane 的獨立功能」。本票是 PD-191 之後的第一群搬移，刻意選最小、最沒有 UI 面積的一群當試金石。
- Priority: MEDIUM。

## Outcome

`main.cpp` 中八支只服務單一 pane 的導覽函式全部成為 `Pane` 的成員，`main.cpp` 不再有任何 `pane_index` 參數的導覽函式。行為與視覺零變更。

## 要搬的函式（2026-09-05 工作樹行號）

| 目前 | 行數 | 搬成 |
|---|---|---|
| `begin_navigation(AppState&, std::size_t)` `:767-775` | 9 | `NavigationGeneration Pane::begin_navigation()` |
| `navigate_pane(AppState&, std::size_t, const ShellLocation&)` `:777-781` | 5 | `HRESULT Pane::navigate_to(const ShellLocation&)` |
| `navigate_up_pane(AppState&, std::size_t)` `:783-786` | 4 | `HRESULT Pane::navigate_up_one_level()` |
| `navigation_request_is_current(AppState&, std::size_t, NavigationGeneration)` `:788-805` | 18 | `bool Pane::navigation_request_is_current(NavigationGeneration)` |
| `handle_navigation_complete(AppState&, std::size_t, NavigationGeneration, const ShellLocation&)` `:2332-2355` | 24 | `void Pane::navigation_complete(NavigationGeneration, const ShellLocation&)` |
| `handle_navigation_failed(AppState&, std::size_t, NavigationGeneration)` `:2357-2364` | 8 | `void Pane::navigation_failed(NavigationGeneration)` |
| `navigate_tab_history(AppState&, std::size_t, bool back)` `:3172-3190` | 19 | `void Pane::navigate_history(bool back)` |
| `navigate_up(AppState&, std::size_t)` `:3192-3199` | 8 | `void Pane::navigate_up()` |
| `refresh_pane(AppState&, std::size_t)` `:3201-3209` | 9 | `void Pane::refresh_view()` |

命名刻意避開既有的 `Pane::navigate(const ShellLocation&)`（PD-183 的薄轉呼叫，`pane.h`）。**該薄方法在本票結束時應被 `navigate_to` 取代並刪除**——它不 `begin_navigation`，是一個容易誤用的陷阱；清點所有呼叫點後改用 `navigate_to`，若有呼叫點刻意不要 generation，在交接區列出並說明。

## 依賴的 `PaneHost` 服務（PD-191 已提供，不要新增成員）

- `begin_navigation` / `navigation_request_is_current` → `pane_host()->active_group_id()`
- `navigate_tab_history` / `navigate_up` / `refresh_view` / `navigation_complete` → `is_shutting_down()`、`ShellCall`、`schedule_session_save()`

## Scope

1. 依上表逐支搬進 `pane.h`／`pane.cpp`，函式體原封搬移，只做機械替換：
   - `state.panes[pane_index]` → `*this`（或直接去掉）
   - `state.closing_ || state.shutdown_deferred` → `pane_host()->is_shutting_down()`
   - `ShellCallScope shell_call(state);` → `ShellCall shell_call(pane_host());`
   - `active_group(state).id` → `pane_host()->active_group_id()`
   - `schedule_session_save(state);` → `pane_host()->schedule_session_save();`
   - `active_tab(*pane_state)` → 既有的 `active_tab()` 成員（PD-190）
2. **每支開頭加 `if (pane_host() == nullptr) return;`**（`refresh_view` 等回傳值的則回傳既有失敗值）。`Pane` 可在未接線時存在（PD-191 驗收條件 4）。
3. `handle_navigation_complete` 呼叫 `apply_pane_view_mode`／`apply_pane_sort`／`refresh_tab_strip`。這三支在本票時**尚未搬**（分別是 PD-193、PD-196）。本票的處理：`Pane::navigation_complete` 暫時留下這三個呼叫點，做法是把它們保留為 `main.cpp` 的自由函式並由 `Pane` 呼叫——**不可行**（`Pane` 看不到 `AppState`）。因此本票**必須連同 PD-193 一起排**，或改為：把 `navigation_complete` 拆成 `Pane::record_navigation_result(...)`（純資料，本票搬）＋ 協調層仍持有的 `handle_navigation_complete`（呼叫 `pane.record_navigation_result()` 後再做那三個刷新）。**採用後者**：本票只搬純資料那半，`handle_navigation_complete` 縮到約 8 行留在 `main.cpp`，等 PD-193／PD-196 完成後再由 PD-197 收尾成完整的 `Pane::navigation_complete`。這一點寫進 PD-197 的 scope。
4. 更新全部呼叫點。`grep -n 'navigate_pane\|navigate_up_pane\|begin_navigation\|navigation_request_is_current\|refresh_pane\|navigate_tab_history' src/app_shell/main.cpp` 是完整清單，逐一改為 `state.panes[i].xxx()`。
5. `realized_flags`（`:810-815`）與 `navigate_realized_panes`（`:817-838`）是**跨 pane 迴圈，留在協調層**，只需改呼叫新成員。

## Non-goals

- 不動 `core::record_navigation`／導覽歷史純函式（`AGENTS.md` 的測試接縫，`Pane` 只呼叫不吸收）。
- 不改 `NavigationRequest` 的欄位或 PD-170 的比對語意。
- 不搬 `apply_pane_view_mode`／`apply_pane_sort`／`refresh_tab_strip`（PD-193／PD-196）。
- 不動 `ExplorerHost` 的回呼註冊方式（協調層透過 `Pane::host()` 註冊，捕捉 `&state`）。回呼本體改為呼叫 `pane.navigation_failed(...)` 等新成員即可。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> **A `pane_index` parameter is a signal that the function belongs to that pane.**

> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`docs/tickets.md` 2026-09-04 界線：

> `PaneState` 的位址由 `core` 保證穩定（PD-184）；`TabState` 的位址**不保證**，因此任何持有 `TabState*` 的設計都是錯的。

（`active_tab()` 的回傳值只能當場用，不得存成成員。）

## Acceptance criteria

1. `main.cpp` 內 `grep -c 'std::size_t pane_index'` 對導覽群為 0。
2. 上表九支全部是 `Pane` 成員（`handle_navigation_complete` 除外，見 Scope 3）。
3. `Pane::navigate` 這個 PD-183 薄方法已刪除或已在交接區說明保留理由。
4. 每支成員都有 `pane_host() == nullptr` 的防護。
5. 零行為、零視覺變更；PD-170 的過期導覽丟棄行為不變。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/unit/pane_test.cpp` 新增：

- `test_navigation_request_identity_survives_group_switch` — 以假的 `PaneHost`（回傳可控的 `active_group_id`）驗證 `navigation_request_is_current` 在 group id 改變後回傳 `false`，generation 相同時回傳 `true`。這是 PD-170 語意第一次獲得單元測試涵蓋。
- `test_navigation_calls_are_no_ops_without_host` — 未 `set_host` 時各成員不崩潰。

## 使用者實機檢查

1. 上一頁／下一頁按鈕在四個 pane 各自正確。
2. 上一層按鈕在磁碟根目錄時停用。
3. F5／重新整理按鈕不改變歷史。
4. 在 pane A 導覽中途切換 Group → 導覽完成後不會把新 Group 的 tab 位置改掉（PD-170）。
5. 導覽到一個不存在的路徑 → 錯誤覆蓋層出現，上一頁按鈕未被卡住停用（`navigation_failed` 的 `set_suppress_history(false)`）。

## 交接區

（實作者填寫：`Pane::navigate` 的處置、`handle_navigation_complete` 拆分後留在 `main.cpp` 的實際行數、假 `PaneHost` 測試替身放在哪個檔案。）
