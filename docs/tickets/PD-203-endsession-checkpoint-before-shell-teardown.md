# PD-203 — OS session end 先寫 durable checkpoint，Shell teardown 改 best-effort

Phase 7 · app_shell shutdown correctness · Depends on: PD-025, PD-032, PD-132, PD-143

- Source: 2026-09-14 使用者回報 Windows Update 自動重開機後，PaneDock 啟動仍
  顯示「PaneDock did not shut down cleanly last time」。以暫時性 breadcrumb log
  在兩次真實重開機上追到精確阻塞點。
- Priority: HIGH——每一次系統關機／重開機都會誤報一次不乾淨關閉，且該次
  session 的最終 snapshot 也沒有寫入。這正是 PD-032 要修但沒修完的同一個症狀。

## 覆寫聲明

本票覆寫 **PD-143** 在 `WM_ENDSESSION` 這一條路徑上的排序決策。

PD-143 要求 durable `clean_shutdown=true` 只在「所有 `IExplorerBrowser` 已
`Destroy`、主視窗已銷毀、message loop 已結束、`OleUninitialize` 已返回」之後
才寫入，目的是讓 force-kill 與 teardown 卡死不會留下 false-clean。

新證據顯示這個排序在 OS session end 上**無法達成**：Windows 在 `WM_ENDSESSION`
返回後即終止行程，而完成 teardown 所需的時間超過系統給的 kill timeout（實測
見下方「量測」）。結果不是「保守地保持 false」，而是「每次正常關機都誤報」——
PD-143 想防的 false-clean 換成了必然發生的 false-unclean。

覆寫範圍**僅限 `run_end_session_shutdown`**。正常 `WM_CLOSE` 關閉路徑與
`wWinMain` 尾端維持 PD-143 的排序不變，force-kill audit 能力不受影響：使用者
force kill 一個正在執行的 PaneDock，marker 仍然停在 `false`。

在這條路徑上，marker 的語意讀作「已建立可恢復的 checkpoint」，而非「teardown
已完成」。理由：`WM_ENDSESSION(wParam=TRUE)` 是 OS 發起 session end 的可靠訊號，
而那不是 FR-013 的 crash recovery 警告要告訴使用者的事件。

## 量測（真實重開機，非模擬）

暫時性 breadcrumb log 寫在 `%LOCALAPPDATA%\PaneDock\endsession.log`，
`FILE_APPEND_DATA` + 每筆 `FlushFileBuffers`，所以行程被硬砍也留得住。

**第一次（修正前的 defer 版本）** — 2026-09-14 09:49:26，Event 1074 使用者發起
重新啟動：

```
09:49:26.282 queryendsession   deferred=0 shell_depth=0 drag=0 closing=0 fileop=0
09:49:26.361 endsession-enter  deferred=0 shell_depth=0 drag=0 closing=0 fileop=0
<沒有後續>
```

`WM_ENDSESSION` 確實送達、所有 gate 乾淨、handler 確實進入，行程被砍在裡面。
同樣的訊息序列用 `SendMessageTimeout` 從 PowerShell 驅動只需 **59ms** 就完成
並寫入 `clean_shutdown=true`——所以失敗條件是「真實 session end」，不是這兩則
訊息本身。

**第二次（本票修正後）** — 2026-09-14 10:04:30：

```
10:04:30.865 endsession-enter
10:04:30.865 saved-clean          ← checkpoint 已落地，<1ms
10:04:30.865 teardown-enter
10:04:30.865 title-skipped
10:04:30.865 drag-targets-revoked
10:04:30.865 pane0-destroy-enter
10:04:30.881 pane0-destroy-left   ← 16ms
10:04:30.881 pane1-destroy-enter
<沒有後續>
```

**精確阻塞點：第二個 pane 的 `IExplorerBrowser::Destroy()`。** 真實 session end
時 Explorer 也在拆，`Destroy()` 與 `capture_location()` 的每-pane Shell 讀取都是
打進一個垂死 COM server 的跨行程呼叫，會超過 kill timeout。

`session.json.bak` 的 mtime 10:04:30.865 證明該次 atomic replace 確實發生；
10:07:05 重新啟動後未再出現警告，使用者確認。

## 範圍

`src/app_shell/main.cpp`：

1. 新增 `run_end_session_shutdown(HWND, AppState&)`，`WM_ENDSESSION(TRUE)` 改為
   呼叫它，取代原本的 `run_shutdown_action(step(end_session))`。
   原路徑一律回傳 `ShutdownAction::defer` 並 `PostMessageW(kDeferredShutdownMessage)`，
   而該則訊息在 `WM_ENDSESSION` 返回後永遠不會被 pump——save、teardown、marker
   三者全部落空。
2. 函式開頭**無條件**先做 durable checkpoint，順序不可改動：
   `capture_window_placement`（純 Win32）→ `step(save_started)` →
   `state.session.write(state.application, /*clean_shutdown=*/true, window)`。
   **刻意不呼叫 `capture_locations`**：它會讀每個 live `IExplorerBrowser`，正是
   會卡住的那類呼叫。model 已持有每個 pane 上次導航的位置。
   **任何 gate 都不得排在這次寫入之前**，包含 `state.closing_`：一個已經卡在
   Shell teardown 的正常關閉，若在此時收到真正的 `WM_ENDSESSION`，提早 return
   會讓 marker 停在 false，就是本票要修的那個誤報。
3. checkpoint 之後才判斷是否嘗試 teardown。`state.closing_`、
   `action != defer`、`shell_call_depth != 0`、`drag_in_progress` 任一成立即
   return——在巢狀 Shell pump 或 live OLE drag 下 teardown views 是
   `docs/design-spec.md §9.4` 警告的當機面。teardown 本身為 best-effort。
4. `finish_shutdown` 在 OS session end 時跳過 `set_main_window_title(..., true)`。
   `teardown_started` 會清掉 `end_session_pending`，所以要在 step 之前先讀。
   `RedrawWindow(RDW_UPDATENOW | RDW_FRAME)` 是同步非客戶區重繪，而關機時 DWM
   也在收攤；caption 是給「正在看的人」的進度面，OS session end 沒有這種人。
5. 新增 `finalize_process(AppState&)`：`OleUninitialize()` → `write_clean_marker`，
   以 `AppState::ole_finalized` once-flag 保護。`wWinMain` 尾端與
   `run_end_session_shutdown` 共用，避免重複 `OleUninitialize` 與重複寫入。

`tests/release/shutdown_state_check.ps1`：

6. 原本以「檔案中最後一次出現位置」比較 `OleUninitialize();` 與
   `write_clean_marker(...)` 的排序檢查失效（兩者現在都在 `finalize_process`
   內，而檔案後段另有 startup 早退路徑的 `OleUninitialize`）。改為斷言
   `finalize_process` **函式體內**的順序。
7. 新增不變式：`run_end_session_shutdown` 從函式開頭到
   `session.write(state.application, true, window)` 之間不得出現任何 `return;`。
   用 `(?:(?!return;)[\s\S])*?` 表達。此條專門防止未來有人把 checkpoint 移到
   gate 後面。

## 非目標

- 不為 `IExplorerBrowser::Destroy` 加逾時或背景執行緒。`docs/design-spec.md §9.4`
  明文規定 teardown 順序不可調換、不得改用背景執行緒；為了一個下一秒就被 OS
  回收的行程開執行緒，風險大於問題本身。teardown 卡住時 view 洩漏是可接受的。
- 不改 `WM_QUERYENDSESSION` 回傳 `TRUE` 的行為，也不加
  `ShutdownBlockReasonCreate`：block reason 只在回傳 `FALSE` 時才有意義，回傳
  `TRUE` 時它不會延長任何時限。
- 不改正常 `WM_CLOSE` 路徑的 defer 行為與 PD-143 排序。
- 不新增 crash detector。`WM_ENDSESSION(TRUE)` 已是足夠可靠的 OS-session-end 訊號。
- breadcrumb log 是暫時性診斷，已在本票結束前完整移除，不進版本庫。

## 驗收條件

1. 開著 PaneDock 執行系統重新啟動，開機後啟動 PaneDock 不再出現
   「did not shut down cleanly last time」。
2. force kill（`Stop-Process -Force`）一個執行中的 PaneDock，下次啟動**仍然**
   出現該警告——PD-143 的 force-kill audit 能力未被削弱。
3. 正常 `WM_CLOSE` 關閉後 `session.json` 的 `clean_shutdown` 為 `true`，且
   caption 在 teardown 期間仍顯示「Closing...」。
4. `ctest --test-dir build --output-on-failure` 全數通過，含
   `panedock_shutdown_state` 與 `panedock_launch_smoke`。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

第 7 項不變式需反向驗證：在 `run_end_session_shutdown` 開頭插入一行
`if (state.closing_) return;`，`panedock_shutdown_state` 必須失敗；移除後必須通過。

## 交接區

- 精確阻塞點是 `IExplorerBrowser::Destroy()`，而且不是第一個 pane——pane0 在
  16ms 內完成，pane1 進去就沒再出來。若日後要處理 teardown 完整性，起點是
  「為什麼第二個之後的 Destroy 會慢一個數量級」，不是「Destroy 整體很慢」。
- `SendMessageTimeout` 模擬 `WM_QUERYENDSESSION`/`WM_ENDSESSION` 完全無法重現
  這個問題（59ms 全程走完）。任何針對 OS session end 的驗證都必須用真實重開機。
- 驗證期間 `session.json` 請先備份再還原：probe 會關閉使用者正在使用的實例。
