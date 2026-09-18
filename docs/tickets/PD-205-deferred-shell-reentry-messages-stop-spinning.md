# PD-205 — Shell 呼叫期間的延遲訊息改為 pending flush，消除 PostMessage 自旋

Phase 7 · switching path robustness · Depends on: PD-171, PD-204

- Source: 2026-09-18 使用者要求稽核 tab／Group 切換路徑的速度與健壯性；
  Claude 與 Codex 兩份唯讀稽核並行，本項由 Claude 提出，作者已在原始碼層面複驗。
- Priority: HIGH——直接違反 `AGENTS.md`「Event-driven idle path only. No busy
  loops, no polling timers.」與 `docs/performance-baseline.md` 的 idle CPU 門檻。

## 根本原因

`defer_shell_reentry_message`（`src/app_shell/main.cpp:576`）在
`shell_call_depth != 0` 時，把訊息**立刻** `PostMessageW` 回同一個視窗：

```cpp
void defer_shell_reentry_message(HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam) noexcept {
    if (PostMessageW(window, message, wparam, lparam)) return;
    ...
}
```

`main_window_proc`（`:3498-3512`）對 `WM_COMMAND`、`kDragHoverMessage`、
`WM_PARENTNOTIFY`、`WM_LBUTTONDOWN/DBLCLK/UP` 走這條路；
`kDeferredCommandMessage` 的處理器（`:3525-3533`）更是明確地「仍在 shell call
就再 post 一次」。

Shell 呼叫（拖放、`IFileOperation` 進度、context menu、慢速導覽、
`display_text_for_parsing_name`）會抽我們的訊息迴圈。被 post 回去的訊息在該
迴圈裡**立刻**重新派送 → 再次 defer → 再 post，形成無界迴圈：整段 Shell 呼叫
期間 UI 執行緒 100% CPU、訊息佇列持續攪動。

同一份程式碼在 `WM_TIMER` 的 session-save 分支（`:3738-3744`）留了註解說
「a 500ms retry cannot become a PostMessage spin」——作者已知這個風險，但
command／hover／mouse 路徑正是那個 spin。

附帶後果：一連串點擊會全部被重貼，並在 Shell 呼叫解開後一次全部執行，等於
連續跑 N 次完整 Group transition（`delete_group` 在 `:2059` 的註解已描述這個
replay 行為）。

## 要讀與追的檔案

- `src/app_shell/main.cpp`：`finish_shell_call`（`:539`）、`ShellCallScope`
  （`:558`）、`defer_shell_reentry_message`（`:576`）、
  `defer_shell_reentry_mouse_message`（`:583`）、
  `app_shell_call_state_changed`（`:594`）、`main_window_proc` 的延遲分支
  （`:3498-3512`）與 `kDeferredCommandMessage` 分支（`:3525-3533`）、
  `pane_window_proc` 的 `WM_COMMAND` 延遲（`:4315`）。
- `src/core/shutdown.h` / `shutdown.cpp`：`shell_call_depth` 的唯一擁有者
  （`shutdown.cpp:73,77-78`）。`finish_shell_call` 是 depth 遞減的唯一出口。

## 範圍

`src/app_shell/main.cpp`：

1. 在 `AppState` 上新增延遲訊息佇列：
   `std::vector<DeferredMessage> deferred_shell_messages;`，其中
   `struct DeferredMessage final { UINT message; WPARAM wparam; LPARAM lparam; };`
   宣告在 `AppState` 之前。
2. `defer_shell_reentry_message` 改為 **push 進佇列**而非 `PostMessageW`。
   保留原本的失敗診斷路徑（改為 `catch (...)` 時的 `OutputDebugStringW`，
   因為 `push_back` 可能擲出；函式為 `noexcept`，必須自行吞掉）。
3. `finish_shell_call`（`:539`）在 `shell_call_depth == 0` 時 flush：先 move
   出整個佇列再逐一 `PostMessageW`，避免 flush 期間有人再 push 造成
   iterator 失效。flush 必須在既有的 shutdown deferral 判斷**之前或之後
   都不重入**：`is_shutting_down()` 為真時直接清空佇列而不 post。
4. `kDeferredCommandMessage` 分支（`:3527`）的 `shell_call_depth != 0` 自我
   重貼改為 push 進佇列（呼叫同一個 `defer_shell_reentry_message`），這樣它
   也不再自旋。
5. 去重：同一 `(message, wparam, lparam)` 已在佇列中就不重複 push。這同時
   收斂 `:2059` 描述的「一連串點擊在 MessageBoxW 裡全部 replay」。

## 非目標

- 不改 `shell_call_depth` 的語意，不改 `ShutdownSequence`。
- 不改哪些訊息要被延遲（清單保持 `:3498-3512` 現狀）。
- 不動 `pane_window_proc:4315`（它 `return 0` 丟棄而非重貼，不會自旋）。
- 不改 `kDeferredShutdownMessage`／`kDeferredLayoutMessage`／
  `kDeferredRealizeMessage`：三者都有自己的 queued 旗標或 generation，不自旋。

## 驗收條件

1. Shell 呼叫期間被延遲的訊息不再產生 `PostMessageW` 迴圈：同一則訊息在
   `shell_call_depth` 歸零前最多被 post 一次。
2. 延遲的訊息在 `shell_call_depth` 歸零後仍會被送出並執行（行為不退化）。
3. `is_shutting_down()` 為真時佇列被丟棄，不會在 teardown 後派送。
4. 既有測試全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

新增一個 `tests/unit/` 的聚焦自檢：把佇列邏輯（push 去重 + flush 一次性
取出 + shutdown 丟棄）抽成一個不依賴 HWND 的小純函式或直接測 `std::vector`
操作序列。若無法在不引入 HWND 的前提下測到，則在 ticket 交接區寫明為何，
並以 `panedock_launch_smoke` 作為不退化證據。

## 交接區

2026-09-18 實作完成。

- 佇列型別抽到新檔 `src/app_shell/deferred_messages.h`
  （`DeferredMessage`／`DeferredMessages`／`hold_deferred_message`／
  `take_deferred_messages`），因為留在 `main.cpp` 的 anonymous namespace 就
  沒有測試接縫。這是純資料操作，只依賴 `windows.h` 的 `HWND`／`UINT`，不碰 COM。
- **對原範圍的修正（重要）**：ticket 假設所有延遲訊息都屬於主視窗。實際上
  `defer_shell_reentry_mouse_message` 有三種呼叫者——tab strip subclass
  （`main.cpp` 的 `tab_strip_subclass_proc`）、sidebar subclass、
  `AppState::handle_pane_control_message`——訊息各屬不同 HWND。若 flush 一律
  post 回 `state.main_window` 會靜默錯誤路由。因此 `DeferredMessage` 多帶一個
  `target` HWND，flush 時以 `IsWindow(target)` 檢查後 post 回原視窗。
  `target` 同時參與去重判斷。
- `defer_shell_reentry_message` 與 `defer_shell_reentry_mouse_message` 的簽名
  改為 `(HWND window, AppState& state, ...)`；六個呼叫點同步更新。
- flush 掛在 `finish_shell_call`（depth 遞減之後、shutdown deferral 判斷之前），
  `state.shell_call_depth == 0` 才執行。`is_shutting_down()` 為真時佇列被取出
  後直接丟棄，不 post。
- 測試：新增 `tests/unit/deferred_messages_test.cpp`（三個 case：重複只留一份、
  target 參與身分、取出後清空且可再收）。`tests/CMakeLists.txt` 新增一列
  `deferred_messages|unit/deferred_messages_test.cpp|user32`。
- `tests/release/shell_reentry_gate_check.ps1:331` 的
  `defer_shell_reentry_message(pane_window, message, wparam, lparam)` 字面比對
  隨簽名更新。
- `ctest`：33/33 通過。
