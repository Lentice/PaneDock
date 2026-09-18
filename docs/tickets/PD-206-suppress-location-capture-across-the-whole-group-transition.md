# PD-206 — Group transition 全程抑制 location capture，避免半切換狀態污染新 Group

Phase 7 · switching path robustness · Depends on: PD-203, PD-204

- Source: 2026-09-18 使用者要求稽核 tab／Group 切換路徑；本項由 Codex 提出，
  Claude 的同一輪稽核把這一區判為「已查核正確」——作者複驗後判定 **Codex 正確**，
  Claude 只看到 `navigate_realized_panes` 內部的抑制，漏掉它之前的視窗期。
- Priority: HIGH——後果是 `session.json` 存下使用者從未在該 Group 看過的位置，
  違反 Group 的核心承諾「一鍵還原整組工作情境」。與 PD-204 是同一類缺陷
  （model 與 live view 不一致被寫進 session），只是觸發點不同。

## 根本原因

`activate_group`（`src/app_shell/main.cpp:1985`）的順序是：

```cpp
capture_locations(state);                        // 擷取舊 Group，正確
state.application.active_group_id = target_id;   // :1996 model 已切換
perform_group_transition(window, state, activate, true);
```

`perform_group_transition`（`:1939`）依 `plan_group_transition` 的步驟跑：
`rebind_panes` → `refresh_tab_strips` → `navigate_realized_panes` → …

而 `suppress_location_capture` 只在 `navigate_realized_panes`（`:732-751`）
內部被設為 `true`：

```cpp
const bool previous_suppression = state.suppress_location_capture;
state.suppress_location_capture = true;
```

於是從 `active_group_id` 被改寫、pane 已 `rebind` 到新 Group 的
`PaneState`，到抑制被開啟之前，存在一段視窗期。`refresh_tab_strips`
（`:1113`）正落在這段裡，而它會對每個 pane 的每個 tab 呼叫
`tab_display_text` → `display_text_for_parsing_name`（`:619`）；parsing name
以 `::` 開頭（`::{GUID}` 形式的虛擬資料夾）時會進入 `ShellCallScope`，
**抽我們的訊息迴圈**。

失敗情境：切換到含虛擬位置 tab 的 Group → display-name Shell 呼叫期間收到
`WM_CLOSE` → transition 在下一輪 `if (state.is_shutting_down()) return;`
（`:1945`）中止 → pane 已綁新 Group 的 `PaneState`，但 live
`IExplorerBrowser` 仍顯示舊 Group 的資料夾 → 關閉流程的
`capture_locations` 把舊 view 的 location／view mode／sort 寫進**新 Group**
的 tab。

`ExplorerHost::navigate` 在 `BrowseToObject` 前先寫 `location_`
（`explorer_host.cpp:612-614`）也救不了：這段視窗期裡 `navigate` 還沒被呼叫。

## 要讀與追的檔案

- `src/app_shell/main.cpp`：`AppState::suppress_location_capture`（`:530`）、
  `location_capture_suppressed()` 的 override、`capture_locations`（`:206` 區段，
  見 `pane_tab_strip.cpp` 上方的自由函式群）、`navigate_realized_panes`
  （`:732`）、`refresh_tab_strips`（`:1113`）、
  `display_text_for_parsing_name`（`:619`）、`perform_group_transition`
  （`:1939`）、`activate_group`（`:1985`）。
- `src/core/group_transition.{h,cpp}`：`plan_group_transition` 的步驟順序。
- `src/app_shell/pane.cpp`：`Pane::capture_location()`。
- `src/app_shell/pane_tab_strip.cpp:204`：`PaneTabStrip::refresh()` 的
  display-name 迴圈。

## 範圍

`src/app_shell/main.cpp`：

1. 新增一個 RAII guard（檔案內 anonymous namespace，比照 `ShellCallScope`
   的寫法）：

   ```cpp
   class LocationCaptureSuppression final {
   public:
       explicit LocationCaptureSuppression(AppState& state) noexcept
           : state_(state), previous_(state.suppress_location_capture) {
           state_.suppress_location_capture = true;
       }
       ~LocationCaptureSuppression() noexcept {
           state_.suppress_location_capture = previous_;
       }
       // copy/move 全部 delete
   };
   ```

2. `perform_group_transition`（`:1939`）在進入 step 迴圈**之前**建立這個
   guard，涵蓋全部步驟與所有提早 `return` 路徑。
3. `navigate_realized_panes`（`:732-751`）移除手寫的
   `previous_suppression` 存還原，改用同一個 guard——它仍可被
   `perform_group_transition` 之外的呼叫者使用（若有），所以不能直接刪掉抑制。
4. 保留 `activate_group` 在改 `active_group_id` **之前**的
   `capture_locations(state)`：那一次是正確且必要的（擷取舊 Group）。guard
   必須在它之後才建立。

## 非目標

- 不改 `plan_group_transition` 的步驟順序（`src/core` 不動）。
- 不移除 `refresh_tab_strips` 的 Shell 呼叫——那是 PD-207 的範圍。
- 不改 `Pane::capture_location()` 本身。
- 不把抑制擴到 `apply_layout` 的其他呼叫者（例如視窗 resize）：那些路徑沒有
  model 與 view 不一致的問題，擴大範圍會遮蔽正常的位置擷取。

## 驗收條件

1. `active_group_id` 改寫之後、transition 完成或中止之前的任何時點，
   `capture_locations` 都是 no-op。
2. transition 正常完成後 `suppress_location_capture` 回到進入前的值
   （巢狀呼叫不會把抑制永久打開）。
3. transition 因 `is_shutting_down()` 中止時抑制同樣被還原。
4. 既有測試全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

新增一個聚焦自檢：`plan_group_transition` 的步驟序列中，`rebind_panes`
出現在 `navigate_realized_panes` 之前（這是本缺陷的結構前提，若未來順序改變
本票的修法需重新評估）。該斷言放在
`tests/unit/core_group_transition_test.cpp`。

## 交接區

2026-09-18 實作完成。

- 新增 `LocationCaptureSuppression` RAII guard（`main.cpp`，緊接在
  `ShellCallScope` 之後），`navigate_realized_panes` 的手寫
  `previous_suppression` 存還原改用它。
- **對原範圍的修正（重要）**：ticket 原本要求 guard 涵蓋整個 step 迴圈。實作時
  發現 `save_now`（`main.cpp:1552`）在 `suppress_location_capture` 為真且
  `force_during_transition` 為假時**拒絕寫檔**。transition 的最後一步
  `save_session` 會呼叫 `schedule_session_save`，而後者在 `arm_timer` 失敗時
  fallback 到 `save_now(state)`——若 guard 仍持有，那次 fallback 會被靜默吞掉。
  因此 guard 改為 `std::optional`，在 `case Step::save_session` 開頭 `reset()`。
  `plan_group_transition` 保證 `save_session` 是最後一步，所以沒有任何後續步驟
  失去保護；該保證已成為 `core_group_transition_test.cpp` 的斷言。
- 副作用（刻意接受）：`relayout` transition 原本完全沒有抑制（它沒有
  `navigate_realized_panes` 步驟），現在 rebind→apply_layout→focus→refresh_sidebar
  期間也被抑制。這是同一類保護：版型變更時 pane 正在 realize／derealize，
  擷取 live view 的位置沒有意義。
- 測試：`tests/unit/core_group_transition_test.cpp` 新增
  `test_rebind_precedes_navigation_so_the_capture_guard_covers_the_gap()`，
  斷言 rebind 早於 navigate、且 `save_session` 是最後一步。
- `tests/release/shell_reentry_gate_check.ps1` 的
  「realized-pane navigation helper is incomplete」比對從
  `state.suppress_location_capture = true` 改為
  `LocationCaptureSuppression suppression(state)`，並**新增**兩條對
  `perform_group_transition` 本體的斷言（`suppression.emplace(state)` 存在、
  `case Step::save_session:` 後緊接 `suppression.reset();`）——這才是本票真正
  的不變量，之前的 gate 只看得到 helper 內部。
- `ctest`：33/33 通過。
