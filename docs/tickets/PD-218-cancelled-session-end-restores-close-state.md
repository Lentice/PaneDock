# PD-218 — 取消系統結束工作階段後恢復關閉狀態

Phase 7 · shutdown correctness · Depends on: PD-203, PD-216

## 來源與約束

2026-09-30 稽核發現：`WM_ENDSESSION(TRUE)` 在 Shell 呼叫或 drag 中只寫 checkpoint、跳過 teardown，卻留下 `shutdown_deferred` 與 `shutdown_save_attempted`。其後 `WM_ENDSESSION(FALSE)` 只清 `end_session_pending`，使用者再關閉視窗會被 reducer 拒絕。

- `docs/design-spec.md` §9.4：「擷取現行狀態並原子寫入 session document」後須先 destroy 全部 live `IExplorerBrowser`，再 destroy pane HWND、主視窗與退出訊息迴圈；「view 存活期間 destroy parent HWND 是已知的崩潰面」。
- `docs/development.md`：「Make the smallest change that satisfies the acceptance criteria」；`core` 不可含 HWND、COM 或 `windows.h`。
- `AGENTS.md`：「The hazard is reentrancy, never concurrency」；Shell 呼叫與 drag 期間不可拆除仍存活的 view；新非平凡邏輯須有 focused runnable test。

## Overrides

PD-203 要求 OS session end 的 durable checkpoint 必須無條件先寫，此順序保留。其 `save_started` 早於 teardown gate 的排序改為只在確定開始 teardown 時執行；checkpoint 在 gate 前且不受影響。取消 OS session end 時，僅撤銷由該次事件建立的關閉意圖；先前使用者發起的關閉仍繼續。

## 範圍

先讀並追 `src/core/shutdown.h/.cpp` 所有 `end_session`、`end_session_cancelled`、`close_requested`、`deferred_shutdown_ready` 呼叫，`src/core/shutdown_coordinator.h/.cpp` 的 checkpoint 與 effect 順序，`src/app_shell/main.cpp` 的 `WM_ENDSESSION`、`WM_CLOSE`、`ShellReentryGuard`，以及 `tests/unit/core_shutdown_test.cpp`、`tests/unit/core_shutdown_coordinator_test.cpp`。讓取消事件清除該次 session end 的 defer、排隊、save-attempt 與傳輸取消意圖，並恢復正常 caption；保留先前正常關閉或傳輸決策。

非目標：改變正常 `WM_CLOSE` 的 Shell teardown 順序、加入鎖或計時器、改變 checkpoint 的 durable write。

## 驗收與 Agent checks

1. Shell 呼叫中收到 session end、再取消後，正常關閉仍可完成；拖曳與傳輸的取消路徑也不遺留關閉意圖。
2. session end 前已有使用者關閉時，取消 session end 不取消該次關閉。
3. checkpoint 仍在任何 teardown gate 前執行，且不在 Shell 呼叫中 destroy view。

```powershell
cmake --build build
ctest --test-dir build -R '^panedock_core_shutdown(_coordinator)?$|^panedock_shutdown_state$' --output-on-failure
git diff --check
```

## 交接區

- 未驗證：真實 Windows 登出／重開機取消事件的 UI 行為；自動測試涵蓋 reducer 和 coordinator 的等價事件序列。
- 未驗證：`panedock_launch_smoke` 在受限環境兩次等待正常關閉逾 30 秒。`session_directory()` 使用 `SHGetKnownFolderPath(FOLDERID_LocalAppData)`，覆寫 `LOCALAPPDATA` 環境變數沒有轉移寫入位置；其餘 35 項 CTest 通過。需在具可寫使用者儲存位置的桌面環境重跑此項，才能區分 save 失敗提示與 shutdown 故障。
