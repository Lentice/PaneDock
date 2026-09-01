# PD-166 — startup recoverable warning 改為非同步 `OK` 通知，fatal startup error 維持同步退出

Phase 7 · app_shell · Depends on: PD-130, PD-135, PD-141, PD-144

## Goal

把 startup 的使用者提示按「是否仍可使用主要功能」分流：

- **Recoverable**：主視窗與 pane 已建立，功能仍可用，只是 session recovery、
  clean marker、storage、單一 pane Shell view 或 tab drag-and-drop 有問題。
  使用不阻塞主視窗的 modeless notification 顯示英文訊息，只有 `OK` 按鈕；
  使用者可先操作 pane，按 `OK` 後通知關閉。
- **Fatal**：COM、data folder、window class、主視窗或必要 UI 建立失敗，主要
  功能無法使用。不得顯示主視窗／pane；以同步 ownerless error dialog 告知，
  使用者按下按鈕後清理並離開 process。

這張票覆寫 PD-144 的「保留 recoverable startup warning 為 modal
`MessageBoxW`、不建立 notification framework」決策。新證據是 restricted
`%LOCALAPPDATA%` save failure 會讓 `panedock_launch_smoke` 卡在預期中的
startup `MessageBoxW`，即使主視窗與 Shell view 已可用；這不是應由 30 秒測試
截止或強制終止掩蓋的狀況。

## Binding constraints

`AGENTS.md`:

> App UI text must be English. No Chinese strings ship in the binary.

> Keep `src/core` free of HWND, COM and `windows.h`.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

> Event-driven idle path only. No busy loops, no polling timers.

`docs/design-spec.md §9.2`:

> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`docs/design-spec.md §9.4` 的 close 順序不可因通知視窗改變：先保存、destroy
所有 live `IExplorerBrowser`、destroy pane HWND、destroy root、退出 message
loop、最後 `CoUninitialize`。

## Files to read and trace

- `src/app_shell/main.cpp`: `AppState` startup warning 欄位、
  `register_window_class`、`window_proc` 的 `kDeferredRealizeMessage`、
  `finish_shutdown`、`wWinMain` 的 fatal branch 與 post-window startup flow。
- `tests/release/startup_frame_order_check.ps1`: startup realization 與 warning
  source-level invariants。
- `tests/release/shutdown_state_check.ps1`: close guard 與 teardown ordering。
- `tests/release/launch_smoke.ps1`: 30 秒 startup/close deadline；不可放寬或
  改成 force-kill 作為產品修正。
- `docs/design-spec.md §9.2–§9.4, §11`。
- `docs/tickets/PD-130-startup-recoverable-failure-never-blocks-window.md`、
  `PD-135-startup-dialog-close-destroyed-hwnd.md`、
  `PD-141-startup-frame-before-shell-realize.md`、
  `PD-144-startup-warning-aggregation.md`。

## Scope

1. 在 `app_shell` 內以原生 Win32 modeless notification 呈現 recoverable
   startup warnings；只保留 `OK`，不使用 worker thread、async runtime、toast
   或 tray service。
2. 同一時間最多一個通知；既有通知尚未按 `OK` 時，後續 deferred Shell warning
   追加到同一通知內容，不建立阻塞式對話框。
3. 主視窗關閉或 `WM_ENDSESSION` 時先銷毀 notification，再依既有 Shell/
   window teardown 順序關閉；notification 不得 disable 主視窗或吞掉 `WM_CLOSE`。
4. post-window recoverable paths（session recovery、unclean shutdown、startup
   save warning、partial Shell realization、drag/drop registration）不得再以
   `MessageBoxW` 阻塞。
5. pre-window / fatal path 保持同步錯誤提示；`CreateWindowExW` 失敗時合併
   收集到的訊息為單一 prompt，按下後清理 COM／mutex 並離開。
6. 更新 design spec 與既有 focused source checks；保留 launch smoke 的 30 秒
   deadline 與 `CloseMainWindow()` 行為。

## Non-goals

- 不把所有 app 內的 user confirmation（刪除 Group、關閉中的 file operation、
  shutdown save failure）改成非同步通知；它們仍是需要使用者決策的同步對話框。
- 不把不可用的 pane 自動切換到其他 location，也不改 Shell realization、session
  schema、clean marker 或 `IExplorerBrowser::Destroy` 語意。
- 不新增 notification manager、timer、thread、third-party UI framework 或
  產品常駐服務。
- 不修改 30 秒 timeout、`WaitForExit` 或測試的 force-kill fallback。

## Acceptance criteria

1. 正常或 recoverable-warning startup 會先進入主 message loop；warning 可見但
   不 disable 主視窗／pane，且只有英文 `OK` 按鈕。
2. deferred Shell warning 在既有 notification 存在時更新同一視窗；不會再進入
   synchronous `MessageBoxW`，也不會讓 `panedock_launch_smoke` 因未處理 prompt
   等滿 30 秒。
3. 使用者在 notification 顯示時送出 `WM_CLOSE`，通知會被安全銷毀，所有 live
   ExplorerBrowser 仍先於 parent/root HWND 銷毀；不得 double teardown。
4. fatal startup path 不顯示主視窗或 pane；單一同步 error prompt 的按鈕返回後
   process 結束，且不遺留 COM／single-instance mutex。
5. shutdown save-failure 的既有同步 prompt 遇到 re-entrant `WM_CLOSE`／
   `WM_ENDSESSION` 仍遵守既有 close guard。
6. Agent checks：focused source checks、LLVM-MinGW Release build、CTest、
   `git diff --check` 通過；launch smoke 維持 30 秒設定。

## Agent checks

```powershell
cmake --build build
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/startup_frame_order_check.ps1 -SourcePath src/app_shell/main.cpp
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/shutdown_state_check.ps1 -SourcePath src/app_shell/main.cpp
ctest --test-dir build --output-on-failure
git diff --check
```

## Handoff requirements

- 記錄 fatal／recoverable 分流的每個 caller 與實際英文提示。
- 記錄 modeless notification 如何處理後續 deferred warning、主視窗 `WM_CLOSE`
  與 `WM_ENDSESSION`。
- 分開記錄 deterministic source checks 與真實 restricted/writable session 的
  launch smoke 結果；不得以測試 timeout 被 force-kill 當成正常通過。

## 交接區

### 2026-09-01 — modeless recoverable notification 與 root WM_CLOSE

- `src/app_shell/main.cpp` 新增 root-owned modeless `PaneDockStartupNotification`
  child；它只有英文 `OK`，不 disable 主視窗、不攔截 root `WM_CLOSE`。後續
  deferred Shell failure 追加到既有 warning string 並更新同一通知；shutdown
  先 destroy notification，再進既有 Shell/window teardown 順序。
- `wWinMain` 的 session recovery、unclean marker、startup storage、partial
  Shell 與 drag/drop warning 不再呼叫 post-window `MessageBoxW`。`CreateWindowExW`
  失敗時保留單一同步 ownerless error prompt，合併已收集訊息後清理並退出。
- smoke 不再依賴 PowerShell `MainWindowHandle` 啟發式；依 PID 找
  `PaneDockMainWindow` 後以 bounded `SendMessage(WM_CLOSE)` 關閉，30 秒等待與
  force-kill fallback 未改。
- `cmake --build build` PASS；startup/shutdown focused checks PASS；
  `ctest --test-dir build --output-on-failure` 在可寫 session storage 13/13 PASS，
  `panedock_launch_smoke` 2.02 秒；`git diff --check` PASS。
- restricted storage 的 launch smoke 仍會在**關閉階段**觸發既有 session-save
  failure `YES/NO` prompt，這是保護資料的同步使用者決策，不是 startup
  notification hang；該環境不應被拿來宣稱正常 close 已通過。
