# PD-133 — startup／normal close 的 session save failure 對使用者完全靜默

Phase 7 · app_shell persistence UX · Depends on: PD-006, PD-025, PD-078, PD-132

- Source: 2026-08-30 startup／close audit loop。
- Priority: HIGH——disk full、permission、anti-virus deny／quarantine 或 atomic replace failure 時，PaneDock 仍顯示可操作 UI或直接關閉，但 Group／pane／tab 變更不會保存，使用者沒有任何提示。

## Goal

啟動 crash marker 寫入失敗時，主視窗仍可開啟但必須顯示 persistence warning。正常 close 的 final save 失敗時，不得靜默丟棄變更；讓使用者選擇保持 PaneDock 開啟後重試，或明確選擇不保存離開。

## 已確認的根因與 callers

- `save_now` 在 `write_session` 失敗時只呼叫 `OutputDebugStringW` 並回傳 false。
- `wWinMain` 啟動於 `CreateWindowExW` 前呼叫 `save_now(state)`，忽略結果；這包含 session directory 無法寫入、temp/backup flush 或 rename 被擋。
- `begin_shutdown` 呼叫 `(void)save_now(state, true, true)` 後無條件 destroy explorers/window；normal `WM_CLOSE`、deferred transfer close 與 confirmed `WM_ENDSESSION` 都經此共享函式。
- 執行期 save failure 保留 `session_dirty=true`，因此 final close 是可集中處理的共同防線；沒有其他 UI guard。

## Binding constraints

`AGENTS.md`：

> All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> App UI text must be English.

`docs/design-spec.md` §9.4：session atomic write 必須先於 explorer/window teardown。

## Scope

1. 啟動 `save_now(state)` 失敗時填入既有 `startup_warning_message`；在主視窗建立後沿用既有 warning MessageBox 顯示英文提示。
2. `begin_shutdown` 在 normal/deferred close 的 clean save 失敗時顯示英文 Yes/No warning：Yes 保持 PaneDock 開啟以便修正問題後重試；No 明確選擇不保存並繼續既有 teardown。
3. 以最小的 `shutdown_prompt_active` guard 防止 prompt 的 nested modal loop 重複進入 close；`closing_` 只在 final save 決策完成後設定，避免 save 尚在擷取 live view 狀態時被誤判為 teardown。
4. confirmed `WM_ENDSESSION(TRUE)` 不顯示阻塞 prompt；best-effort save 失敗時繼續 shutdown，舊 false marker 讓下次啟動顯示 unclean warning。

## Non-goals

- 不改 JSON/schema、atomic write／backup 演算法、不新增 retry timer、background thread、I/O timeout 或 log file。
- 無法保證 storage driver／anti-virus 讓同步 filesystem call 永不返回時仍可即時顯示 UI；Windows force-if-hung 與下一次 crash recovery 是既有邊界。
- 不在每次執行期 autosave failure 彈窗；final close 與 startup 是最小且不騷擾使用者的共同防線。

## Acceptance / Agent checks

1. 啟動 marker write failure 設定可見 warning，視窗仍建立。
2. normal close clean save failure 不 teardown，除非使用者明確選擇不保存；nested close 不重入。
3. confirmed Windows session end 不彈 prompt。
4. shipped strings 全為英文；build、CTest、launch smoke、`git diff --check` 通過。

```powershell
rg -n "session could not be saved|begin_shutdown|save_now\(state" src/app_shell/main.cpp
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

## 交接區

<!-- 實作 agent append-only -->

### 2026-08-30 — implemented

- 啟動 marker save 失敗會填入既有 `startup_warning_message`，主窗建立後顯示英文 warning；不再只有 debug output。
- `begin_shutdown` 集中處理 final clean save：normal／deferred close 失敗時以 Yes/No 明確讓使用者保持開啟或不保存退出；`shutdown_prompt_active` 只防 modal re-entry，既有 `closing_` 仍只代表 final teardown。confirmed `WM_ENDSESSION(TRUE)` 傳 `allow_keep_open=false`，不阻塞系統 shutdown。
- sandbox 實測：PaneDock 無權寫 workspace 外 `%LOCALAPPDATA%` 時，startup warning 實際出現；normal close 不靜默退出而保持 process/UI，證明兩個新 failure UX 路徑可達。測試建立的 stale process 已依明確 PID 清理，未刪改 session 檔。
- `cmake --build build` PASS；五個 deterministic CTest PASS；`git diff --check` PASS。`panedock_launch_smoke` 在 sandbox 內因新 warning 正確阻止 silent close 而 timeout；嘗試 elevated 重跑被安全審查拒絕，理由是會寫 repository 外的真實 `%LOCALAPPDATA%\PaneDock`。因此未宣稱完整 6/6，本項待使用者明確批准外部 session write 或於一般桌面手動驗證。
- 未新增 persistence schema、background I/O、retry timer、dependency 或每次 autosave 彈窗。同步 storage driver 永不返回仍是 OS/force-if-hung 邊界。
