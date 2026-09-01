# PaneDock `CloseMainWindow()` shutdown timeout investigation

日期：2026-09-01  
範圍：只研究，不修改產品程式。

## 結論

`Process.CloseMainWindow()` 成功，不等於 PaneDock 已退出；它只表示 close message 已送到主視窗。Microsoft 明確說明這個方法不會強制程序結束，應用程式仍可提示使用者、拒絕關閉，或在同步清理期間繼續存活。[`Process.CloseMainWindow`](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.process.closemainwindow?view=net-10.0)

就目前程式碼與既有測試紀錄，優先級如下：

1. **最可能：關閉時的同步儲存失敗對話框。** `WM_CLOSE` 已收到後，`begin_shutdown` 會同步呼叫 `save_now`；失敗且允許保持開啟時，立即呼叫擁有主視窗的 `MessageBoxW`。測試不會按 Yes/No，因此程序可合法地維持 30 秒。這與 Microsoft 對 `WM_CLOSE`「應用程式可先提示、只有確認後才 `DestroyWindow`」的描述一致。[`WM_CLOSE`](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-close) [`Using Dialog Boxes`](https://learn.microsoft.com/en-us/windows/win32/dlgbox/using-dialog-boxes)
2. **若主視窗已消失：同步 Shell teardown 或關閉後寫檔。** `finish_shutdown` 逐一執行 `IExplorerBrowser::Destroy`，離開 message loop 後還會執行 `OleUninitialize` 與最後一次 clean-marker `write_session`。這些是仍可使 PID 存活的同步工作；目前沒有官方契約證明 `IExplorerBrowser::Destroy` 必定快速返回或必定不泵訊息。
3. **較低可能：`WM_QUIT` 被巢狀 loop 取走。** Win32 定義 `WM_QUIT` 是 thread-queue 訊息，由 `GetMessage`/`PeekMessage` 取走，不會進入 WindowProc；`PostQuitMessage` 也只是把它放入 queue。這個推論成立時，外層 loop 可能看不到同一顆訊息，但目前 PaneDock 已有 sticky `quit_requested`，並在進入 `GetMessageW` 前及 dispatch 後檢查，因此不能僅憑症狀再宣稱這是現行版本根因。[`WM_QUIT`](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-quit) [`PostQuitMessage`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postquitmessage) [`GetMessage`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmessage)

既有 repo 紀錄曾在 sandbox 重現 `%LOCALAPPDATA%\\PaneDock` 寫入拒絕，並在可寫入的 elevated context 讓 launch smoke 通過；這是「儲存失敗提示」假說的環境證據，但不是目前 30 秒案例的 debugger proof。[PD-159 交接區](../tickets/PD-159-background-shell-verb-current-folder.md)

## 本次實證

- `cmake --build build`：PASS（Ninja 顯示無需重建）。
- 非提升權限：完整 CTest `12/13 PASS`；`panedock_launch_smoke` 失敗，訊息是主視窗關閉後 30 秒仍未退出。孤立重跑兩次皆同樣失敗。
- 非提升權限的直接探針：`CloseMainWindow()` 回傳 `True`，程序在 12 秒後仍存活且 `Responding=True`；`MainWindowHandle` 在 close 後改變，符合 close-time owned modal 出現的形狀，但本次沒有取得 HWND class/title，故仍標為推論。
- 提升權限、同一 `build\\PaneDock.exe`、同一 session：`ctest --test-dir build --output-on-failure -R panedock_launch_smoke` 在 1.17 秒 PASS。
- `panedock_explorer_host_lifetime_check.exe`：PASS；`ctest -E panedock_launch_smoke`：12/12 PASS；`git diff --check`：PASS。
- 後續在可寫入環境重跑完整 Release CTest：`13/13 PASS`，其中 `panedock_launch_smoke` 0.76 秒通過；`panedock_explorer_host_lifetime_check.exe` 再次 PASS。

這個「同一 binary／同一 session、只有權限條件改變就由 30 秒 timeout 變成 1.17 秒通過」是目前最強的區分證據。它支持 sandbox 無法完成 session atomic replace／flush，導致 `begin_shutdown` 的 save-failure `MessageBoxW` 阻塞；它不支持現在就修改 Shell teardown 或 message loop。要把 modal 直接證實，仍需在非提升 timeout 當下擷取 top-level HWND 與 UI thread stack。

## 現行呼叫鏈

- [`launch_smoke.ps1`](../../tests/release/launch_smoke.ps1#L28-L37) 看到 `MainWindowHandle` 後呼叫 `CloseMainWindow()`，不尋找或關閉任何 modal dialog，接著只等待 process exit。
- [`window_proc`](../../src/app_shell/main.cpp#L5684-L5713) 收到 `WM_CLOSE` 後進入 `begin_shutdown`；成功完成時才會走 `finish_shutdown`，後者依序清理 UI/Shell、`DestroyWindow`、`PostQuitMessage(0)`。[`begin_shutdown`](../../src/app_shell/main.cpp#L4642-L4684)
- [`begin_shutdown`](../../src/app_shell/main.cpp#L4660-L4679) 的 `save_now` 是同步呼叫；失敗時的 `MessageBoxW(window, ..., MB_YESNO)` 會阻塞此 close decision。Microsoft 也說明，若主視窗 disabled（例如正在顯示 modal dialog），`CloseMainWindow()` 可回傳 `false`；因此「測試已通過 CloseMainWindow、之後才 timeout」較符合 close-time save failure，而不是已經存在的 startup modal。[`CloseMainWindow` return value](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.process.closemainwindow?view=net-10.0)
- [`destroy_explorers`](../../src/app_shell/main.cpp#L2690-L2698) 以 `ShellCallScope` 包住每個 `ExplorerHost::destroy()`；[`ExplorerHost::destroy`](../../src/explorer_host/explorer_host.cpp#L1006-L1065) 呼叫 `IExplorerBrowser::Destroy`。Microsoft 的契約只要求 initialized browser 必須呼叫 `Destroy` 以釋放 windowed resources，未提供時間上限或不重入保證。[`IExplorerBrowser`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexplorerbrowser) [`IExplorerBrowser::Destroy`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-iexplorerbrowser-destroy)
- 外層 loop 在 [`wWinMain`](../../src/app_shell/main.cpp#L6092-L6101) 前先檢查 `quit_requested`，dispatch 後再檢查一次；離開 loop 後仍有 [`destroy_explorers`、`OleUninitialize`、最後寫檔](../../src/app_shell/main.cpp#L6183-L6203)。`PostQuitMessage` 只保證 thread 的 message loop 在取到 `WM_QUIT` 時結束，不直接等同於 `wWinMain` 已返回或 process 已終止。[`PostQuitMessage`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postquitmessage)

## 候選原因與區分證據

| 候選 | 支持的來源行為 | 能確認／排除它的證據 | 修正方向比較 |
|---|---|---|---|
| Save failure → modal prompt | `WM_CLOSE` 可提示後不摧毀；`MessageBox` 是 modal；`CloseMainWindow` 不強制退出。 | timeout 當下仍有 PaneDock 主 HWND，另有 `MessageBox` HWND；主視窗 disabled；debugger stack 在 `MessageBox`/User32；`session.json.tmp` 或 `write_session` 失敗。 | 先修測試環境的可寫入 profile 或取得對話框證據。不要用 `Kill` 掩蓋資料遺失；只有證明產品 UX 不應阻塞 automation 才討論改成非同步/非 modal UI。 |
| `IExplorerBrowser::Destroy`／Shell callback teardown | `IExplorerBrowser` 會建立/銷毀 Shell view；COM STA 對 message-driven re-entry 有明文警告。 | 主 HWND 已消失；主執行緒 stack 停在 `IExplorerBrowser::Destroy`、`shell32` 或第三方 Shell extension；`--diagnostic` 對照改變結果。 | 保留「Destroy 在 parent HWND 前」；只在 stack/trace 指向自家 callback 或錯誤生命週期時修。官方文件沒有支持「無條件延後 Destroy」或「立即 PostQuit」的依據。 |
| `WM_QUIT` 被 nested loop 取走 | `WM_QUIT` 是 queue message，`GetMessage` 取到後回 0；modal/COM 路徑會處理訊息。COM 文件明確允許同一 STA 被 re-enter。 | 主 HWND 已消失，stack 在外層 `GetMessageW`；事件順序顯示 `quit_requested` 未設、或現行 binary 沒有 pre-`GetMessage` guard。 | 目前 source 已有 sticky flag 與兩個檢查點；先確認測試是否執行這個 binary，再決定是否還有漏點。不要重複加入 `PostQuitMessage`。 |
| loop 已退出，但 process 卡在 post-loop cleanup | `PostQuitMessage` 只結束 message loop；`OleUninitialize` 會釋放 apartment 持有的 COM/OLE objects、servers；PaneDock 還有 synchronous atomic session write。 | 主 HWND 不存在、`GetMessage` 已返回、stack 在 `write_session`、`FlushFileBuffers`、`OleUninitialize` 或 Shell unload。 | 針對實際阻塞點量測；不要先加 thread、timer 或強制終止。 |

## Shell re-entry 的官方邊界

Microsoft 的 STA 文件說明：STA 必須有 message loop；若 interface method 取出並 dispatch messages，物件可以在同一 thread 被 re-enter，OLE 不會阻止這件事。[`Single-Threaded Apartments`](https://learn.microsoft.com/en-us/windows/win32/com/single-threaded-apartments) `IExplorerBrowser` 文件則說明瀏覽器會建立新的 Shell view、隱藏並銷毀舊 view，且 host 必須以 `IServiceProvider::QueryService` 回應服務查詢。[`IExplorerBrowser`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexplorerbrowser)

這足以支持「Shell 呼叫期間必須防重入」的設計約束，但**不足以證明**本案的 `IExplorerBrowser::Destroy` 實際泵了哪個訊息、哪個 callback 導致 timeout。那必須由 timeout 當下的 stack、HWND/message trace 或 diagnostic-vs-normal 對照確認。

## 最小取證步驟

1. 在 30 秒 timeout 發生時先記錄所有 top-level HWND 的 class、title、owner、visible/enabled 狀態；特別記錄是否有 PaneDock `MessageBox`。
2. 不要立即讓 harness 的 `finally` 強制終止；先 attach debugger，記錄 UI thread 與其他 thread stacks。分類依序看：`MessageBoxW`、`IExplorerBrowser::Destroy`/`shell32`、外層 `GetMessageW`、`write_session`/`FlushFileBuffers`、`OleUninitialize`。
3. 以同一 build、同一 session，在可寫入的 `%LOCALAPPDATA%` 重跑；再用 `--diagnostic` 重跑。若只有不可寫 profile 失敗，優先處理 test isolation；若只有 normal mode 卡住且 stack 在 Shell/extension，才進入 Shell re-entry investigation。

在取得上述證據前，不能把 `PostQuitMessage`、提前 `DestroyWindow`、跳過 `IExplorerBrowser::Destroy` 或 `Kill` 稱為解法；前兩者可能破壞既定 teardown 順序，後兩者分別可能留下 Shell resources 或遺失資料。
