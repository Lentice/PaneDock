# PD-249 — 有實體路徑的 Shell namespace 自動更新（桌面）

Phase 8 · 目標 1–2 天

## 來源與交付

來源：使用者 2026-10-22 回報桌面列表「沒有自動 refresh」，狀態列顯示 `Automatic refresh unavailable. Use Refresh to update.`。

交付一個結果：當 managed list 開啟的是有實體資料夾支撐的 Shell namespace location（目前實際案例是桌面，`namespace_location == true` 且 `fallback_path` 為本機磁碟路徑），資料夾內容變動時列表自動更新，不需手動 Refresh。沒有實體路徑的虛擬位置（例如「本機」）維持手動 Refresh。

## Overrides

PD-232 與 PD-246 刻意只保留虛擬 namespace 的通知接點、顯示 `Automatic refresh unavailable`。本票只對「有本機實體路徑」的 namespace 取消這個限制；其餘不變。

## 綁定限制（引用）

- AGENTS：「Event-driven idle path only. No busy loops, no polling timers.」用 `ReadDirectoryChangesW` 的事件等待，不輪詢、不用計時器掃描。
- AGENTS：「The UI/Shell host hazard is reentrancy, never concurrency.」監看只在 `src/managed_list/list_worker.cpp` 的 worker；worker 以 owned value 與 generation 回傳，不傳 COM／PIDL；取消不等待 OS 呼叫；shutdown 不得無限等待。
- PD-232 沿用：不訂閱 UNC／對應網路磁碟機；本機磁碟路徑需解析後確認不是 UNC junction；連續兩次無細節的復原掃描後停止並回報 `watch_error`。
- 桌面是 user Desktop 與 Public Desktop 的合併檢視：兩個實體資料夾都要監看，任何一個變動就觸發重掃。
- AGENTS：「Never persist a PIDL or a COM pointer.」不新增持久化。
- 既有上限不變：四個 enumeration slots、每 slot 1 pending／2 batches、128 rows/batch、100,000 rows／24 MiB。

## 必讀檔案與 caller

- `src/managed_list/list_worker.cpp`：`enumerate_and_watch`（目前對 `!is_filesystem_location` 直接結束並要求手動 Refresh）、`enumerate_shell_namespace`、`DirectoryWatch`、`decode_changes`、復原掃描迴圈、`enumerate(..., recovery)`（注意 namespace 路徑目前忽略 `recovery`，rescan 的 `Change::rescan` 標記需補）。
- `src/managed_list/list_worker.h`：`Change`、`NotificationCapability`、`Batch`。
- `src/app_shell/pane_managed_list.cpp`：狀態列字串（約 `Automatic refresh unavailable`）、`process_content_results` 對 `watch_error`／`rescan_begin` 的處理、`list_.watch`。
- `tests/unit/managed_list_namespace_worker_test.cpp`、`managed_list_changes_test.cpp`。

## 具體範圍

1. 對有本機實體路徑的 namespace request，worker 開啟這些路徑（桌面：user 與 public）的目錄監看；事件到來時以現有「重掃」流程重列舉（namespace 路徑也要發 `rescan_begin`／`Change::rescan`，避免舊列殘留或重複）。
2. 通知能力標示為可自動更新，狀態列不再顯示 `Automatic refresh unavailable` 這句；無法訂閱時仍退回現行字串。
3. 同一個 request 結束、切換 tab／Group、shutdown 時關閉所有目錄 handle；舊 serial 的事件一律丟棄。
4. 重掃必須保留選取、焦點與可見 anchor（沿用現有 interaction 擷取／還原）。

## Non-goals

不做 `SHChangeNotifyRegister` 的全域 Shell 通知、不做虛擬位置（本機、回收筒、網路）的自動更新、不做細粒度 delta（桌面重掃即可）、不新增計時器。

## 驗收標準

- [ ] 在桌面建立、刪除、重新命名檔案後，列表在不按 Refresh 的情況下更新；public desktop 的變動也會更新。
- [ ] 更新後選取與捲動位置保持；沒有重複列或殘留列。
- [ ] 離開桌面、切換 Group、關閉程式後所有目錄 handle 都已關閉；shutdown 不卡住。
- [ ] 「本機」等虛擬位置仍顯示手動 Refresh 的狀態列字串。
- [ ] 桌面閒置 10 分鐘 CPU 與磁碟 I/O 仍為 0。

## Agent checks

在 `tests/unit/managed_list_namespace_worker_test.cpp` 新增真實 Shell 測試：對暫存資料夾（`namespace_location = true`）列舉後，新增檔案，期望在限定時間內收到包含該檔的重掃批次；刪除後該列消失。

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

桌面實機（含 public desktop）的觀察交給使用者。

## 交接區

- 未驗證：真實桌面（user 與 public desktop 同時存在）的建立／刪除／重新命名，以及閒置 10 分鐘 CPU／磁碟 I/O；自動測試只覆蓋暫存資料夾（`namespace_location`＋本機路徑）。
- 結論：namespace 的監看不解碼變更，任何事件都整批重掃（`Change::rescan`），因此沒有沿用檔案系統監看的「連續兩次無細節復原就停止」限制；重掃只讀不寫，不會自我觸發。
- 結論：桌面以 `FOLDERID_Desktop`＋`FOLDERID_PublicDesktop` 兩個路徑監看；命名空間根（`ILIsEmpty`）視為桌面。以前「namespace_location＋實體路徑」會走檔案系統 delta 監看，現在統一走 namespace 重掃。
