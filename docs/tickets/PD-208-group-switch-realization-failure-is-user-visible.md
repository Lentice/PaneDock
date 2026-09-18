# PD-208 — Group 切換時 realize 失敗必須有使用者可見提示

Phase 7 · switching path robustness · Depends on: PD-093

- Source: 2026-09-18 tab／Group 切換路徑稽核（Claude finding 3）。
- Priority: MEDIUM——後果是使用者按下 Group 後看到空白 pane，沒有任何線索，
  而 model 已經切換並排程存檔。違反 `docs/design-spec.md` FR-012
  「Shell 錯誤在 tab 內可復原且可見」的精神。

## 根本原因

`apply_layout` 的 realize 失敗分支（`src/app_shell/main.cpp:1755-1759`）只
記錄 `first_failure` 然後 `continue`：

```cpp
if (FAILED(hr)) {
    if (!first_failure.has_value())
        first_failure = LayoutFailure{hr, index};
    continue;
}
```

`perform_group_transition` 收到失敗後（`:1958-1962`）只呼叫
`report_shell_failure`，而那是 **debugger-only** 的 `OutputDebugStringW`
（`:1460-1480`）。

對比：啟動路徑的 `kDeferredRealizeMessage` 分支（`:3548-3562`）同樣失敗時會
走 `report_shell_failure` → `append_startup_warning`（`:1445`）→
`show_startup_notification`（`:2698`），使用者會看到非模態提示。
`main.cpp:4131` 附近的啟動路徑也是。**只有 Group 切換路徑沒接上。**

導覽失敗另有 `pane_error_overlay`（`explorer_host.cpp:1040`）覆蓋，但
**初始化失敗沒有** overlay——view 根本沒建起來。

結果：`activate_group`（`:1996`）已寫入 `active_group_id` 並在 transition 的
`save_session` 步驟排程存檔，畫面卻是空的且無任何 UI 線索。

## 要讀與追的檔案

- `src/app_shell/main.cpp`：`LayoutFailure`（`:408`）、
  `AppState::last_layout_failure`（`:437`）、`append_startup_warning`
  （`:1445`）、`last_failed_pane`（`:1451`）、`report_shell_failure`
  （`:1460`）、`apply_layout` 的失敗分支（`:1755`）與結尾
  `state.last_layout_failure = first_failure;`（`:1873`）、
  `show_startup_notification`（`:2698`）、`perform_group_transition` 的
  `Step::apply_layout`（`:1958-1962`）、`kDeferredRealizeMessage` 分支
  （`:3548-3562`，作為要沿用的範本）。
- `src/app_shell/startup_notification.{h,cpp}`：`append_warning` 與非模態
  提示的生命週期。
- `src/app_shell/window_helpers.h`：`format_shell_failure_detail`。

## 範圍

`src/app_shell/main.cpp`，`perform_group_transition` 的 `Step::apply_layout`
分支（`:1958-1962`）：失敗時在既有 `report_shell_failure` 之後，沿用啟動路徑
那條既有機制：

```cpp
case Step::apply_layout:
    if (const HRESULT hr = apply_layout(window, state); FAILED(hr)) {
        report_shell_failure(state, L"group_transition", hr);
        if (hr != E_ABORT) {
            append_startup_warning(
                state,
                L"PaneDock could not open the Shell view for one or more "
                L"panes. Some panes may be empty. " +
                    panedock::app_shell::format_shell_failure_detail(
                        hr, last_failed_pane(state)));
            show_startup_notification(window, state);
        }
    }
    break;
```

`E_ABORT` 必須排除：`apply_layout` 在 `is_shutting_down()` 時回傳 `E_ABORT`
（`:1597`），那不是失敗而是正常的關閉中止，不該彈提示。

文案沿用啟動路徑既有字串（App UI 文字必須是英文，見 `AGENTS.md`），
**不新增機制、不新增字串常數**。

## 非目標

- 不新增 realize 重試。
- 不新增初始化失敗的 pane overlay（那要動 `explorer_host`，且
  `docs/testing.md` 明訂該模組無自動化測試網）。
- 不改 `apply_layout` 的回傳約定，不改 `LayoutFailure`。
- 不回滾 `active_group_id`：使用者確實切到了那個 Group，只是其中一個 pane
  沒開起來；回滾會讓 sidebar 選取與 model 打架。
- 不改 `startup_notification` 的命名（雖然它現在也服務非啟動路徑）：改名是
  純機械 rename，不屬於本票的成果。

## 驗收條件

1. Group 切換時任一 pane 的 `realize()` 失敗，使用者會看到非模態提示，並
   指出是哪個 pane 與簡短的失敗原因。
2. 關閉流程造成的 `E_ABORT` 不會產生提示。
3. 啟動路徑的既有提示行為不變（同一個 `startup_notification` 不會被重複
   堆疊出多份視窗）。
4. 既有測試全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

此票為 UI 提示接線，無法自動化驗證（UI 自動化是已否決方向）。以既有測試
不退化 ＋ 交接區記錄人工複驗方式作為證據。

## 交接區

2026-09-18 實作完成。

- `perform_group_transition` 的 `Step::apply_layout` 失敗分支按 ticket 接上
  `append_startup_warning` + `show_startup_notification`，並排除 `E_ABORT`。
- 需要在 `perform_group_transition`（`main.cpp:2009` 附近）之前為
  `show_startup_notification`（定義在 `:2793` 附近）補一行前置宣告。
- 無新增字串常數、無新增機制，文案沿用啟動路徑既有英文字串。
- `ctest`：33/33 通過。人工複驗方式：把某個 Group 的 pane 指向一個會讓
  `IExplorerBrowser::Initialize` 失敗的位置後切到該 Group，應出現非模態提示。
  尚未實機執行（需要能穩定讓 realize 失敗的位置）。
