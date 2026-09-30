# PD-220 — launch smoke 使用隔離的 session 儲存位置

Phase 7 · test reliability · Depends on: PD-218

## 來源與約束

2026-09-30 的 `panedock_launch_smoke` 在受限執行環境連續兩次於送出 `WM_CLOSE` 後逾時 30 秒。啟動用的 session 路徑由 `SHGetKnownFolderPath(FOLDERID_LocalAppData)` 決定，忽略行程的 `LOCALAPPDATA` 覆寫；測試直接讀寫互動使用者的 session，受限時寫入失敗，關閉序列的 `MessageBoxW` 會等待使用者回答。測試先設定隔離 `LOCALAPPDATA` 仍逾時，證明單改 harness 不足；讓程式使用該行程變數後，同一測試約 2 秒通過。

- `docs/design-spec.md` §9.4：「擷取現行狀態並原子寫入 session document」→ destroy 全部 live `IExplorerBrowser` → destroy pane HWND → destroy 主視窗 → 退出訊息迴圈；不得為測試跳過 view teardown。
- `docs/design-spec.md` §10：「位置：`%LOCALAPPDATA%\PaneDock`」；正常寫入仍須保留原子替換與備份。
- `docs/development.md`：「Make the smallest change that satisfies the acceptance criteria」；不新增抽象、服務或第三方 runtime。
- `AGENTS.md`：「Never destroy a parent HWND while a view is alive」；「`panedock_launch_smoke` tests the real `build\PaneDock.exe`」；受限 session save 失敗可能留下 `MessageBoxW`，不得強制終止或略過 `IExplorerBrowser::Destroy`。

## Overrides

PD-218 交接區把 smoke test 的逾時列為未驗證風險。本票以「覆寫環境變數仍紅 → 修正資料目錄解析後綠」的同一個 smoke test 確認原因；該風險已由本票關閉。既有完成 ticket 文件保持不動。

## 範圍

讀並追 `src/shell_core/shell_core.h/.cpp` 的 `session_directory()` 及其唯一產品呼叫者 `src/app_shell/main.cpp`，`src/app_shell/session_writer.cpp` 的關閉存檔，`src/core/shutdown_coordinator.cpp` 的 save-failure prompt，`tests/release/launch_smoke.ps1` 和 `tests/CMakeLists.txt`。`session_directory()` 優先採用非空、絕對路徑的行程 `LOCALAPPDATA`，不可用時仍用 Known Folder。Smoke harness 在 build 目錄內設定獨立資料根並確認 session 確實寫入其中；失敗時保留仍在執行的 app 供正常關閉，不強制終止。

非目標：改變正式使用者的預設 session 路徑、修改 session schema、繞過關閉存檔提示或 Shell teardown。

## 驗收與 Agent checks

1. 修正前相同環境中 smoke test 穩定逾時；修正後通過且隔離資料根有 `PaneDock\session.json`。
2. 完整 CTest 全綠；smoke 結束後沒有遺留 PaneDock 行程。
3. 測試不讀寫互動使用者的 session，失敗時不強制殺掉仍持有 Shell view 的行程。

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

## 交接區

- 未驗證：真實 Windows 登出／重開機取消事件的 GUI 行為仍屬 PD-218 記載的獨立風險；本票只驗證一般 `WM_CLOSE` 的真實執行檔流程。
