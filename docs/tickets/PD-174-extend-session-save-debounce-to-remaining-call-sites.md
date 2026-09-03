# PD-174 — 把 session save 的 500ms debounce 擴大到 tab/Group/layout/splitter 等其餘同步 `save_now` 呼叫點

Phase 7 · app_shell · Depends on: PD-091

## 來源

2026-09-03 三方稽核（Codex 與 OpenCode 各自獨立指出，OpenCode 列出更完整的呼叫點清單）。經 fork 對照現有原始碼與 `PD-091` 交接區核對，判定為 CONFIRMED-NEW：`PD-091` 建立了 debounce 機制但明確只套用在 `handle_navigation_complete` 一個呼叫點，其餘互動路徑仍是同步寫入。

## 背景與現況

`PD-091`（已完成）引入了 `session_dirty` + 500ms 一次性 timer 的 debounce 機制：

- `schedule_session_save`（`src/app_shell/main.cpp:2393-2401`）：設 dirty 並重設 `kSessionSaveTimerId`（`:89`，`kSessionSaveDelayMilliseconds = 500`，`:88`）。
- `WM_TIMER` 到期時才真正 `save_now`（`main.cpp:5992-5994`）。

但目前全專案只有**兩個**呼叫點用了它（`main.cpp:2709` 的 `handle_navigation_complete` 與 `:3199`），其餘互動路徑仍直接呼叫同步的 `save_now`：

| 呼叫點 | 行號 | 觸發時機 |
|---|---|---|
| `delete_group` | `:3287` | 刪除 Group |
| `move_group` | `:3299` | Group 拖曳排序 |
| `set_active_pane` | `:3318` | 切換 active pane |
| `switch_active_tab` | `:3363` | **切換 tab** |
| `add_tab` | `:3447` | 新增 tab |
| `close_tab_in_pane` | `:3470` | 關閉 tab |
| 其他 tab/檢視相關 | `:3529`, `:3592`, `:3896`, `:3926` | tab 與檢視模式操作 |
| layout 切換 | `:3711` | 版型切換 |
| splitter 拖曳結束/雙擊 | `:5955`, `:5963`, `:5984` | 分隔線操作 |

`save_now` 最終呼叫 `panedock::core::write_session`（`src/core/session.cpp:580` 一帶），每次成本依 `PD-091` 已記錄的分析包含：重新序列化整份文件、寫暫存檔 + `FlushFileBuffers`、讀回舊檔驗證、複製 `.bak` + 再一次 `FlushFileBuffers`、atomic rename。

`PD-091` 的交接區已確認其實作範圍：

> `handle_navigation_complete` 現在只標記 dirty 並以 `SetTimer` 排程保存，不再直接呼叫 `write_session`。

也就是其餘呼叫點從未被改動，不是本票重複既有工作。

## 為什麼這是真的問題

`docs/design-spec.md NFR-003` 要求「Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI」——雖然這條的原文針對 Shell location，但這幾條路徑上的同步磁碟寫入（含兩次 `FlushFileBuffers` 強制落盤）同樣會在磁碟繁忙、防毒即時掃描介入或使用者資料在慢速磁碟時造成可感的停頓：**使用者只是點一下 tab，就要等一次完整的 atomic-replace 落盤**。`PD-091` 已經為這個問題建立了正確的機制，本票只是把它套用完整。

## Fix 方向

把上表列出的呼叫點從 `save_now(state)` 改為 `schedule_session_save(state)`，並保留 `PD-091` 已建立的兩項既有保證不變：

1. **關閉路徑仍必須同步落盤**：`WM_CLOSE`、`WM_QUERYENDSESSION`、dirty 狀態下的 `WM_DESTROY`（`main.cpp:6073-6077` 一帶）仍走同步 `save_now`，不能讓 debounce 造成「使用者最後一個操作沒存到」。
2. **`SetTimer` 失敗時退回同步 `save_now`**（`schedule_session_save` 內 `:2396-2399` 已有此 fallback）。

逐一檢查每個呼叫點是否有「必須立刻落盤」的理由（例如某些路徑後面緊接著會讀回 session、或某些路徑本來就處在關閉序列中）；若有，保留同步寫入並在交接區說明理由，不要為了統一而破壞正確性。

## 綁定限制（引用）

- `AGENTS.md`：「All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.」——本票不改變寫入機制本身，只改變觸發頻率。
- `AGENTS.md`：「Event-driven idle path only. No busy loops, no polling timers.」——沿用 `PD-091` 既有的「因變更啟動、到期即停」一次性 timer，不新增常駐輪詢。
- `AGENTS.md`：「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」——直接重用既有 `schedule_session_save`，不新增機制。
- `PD-078`（crash-safe session write）建立的保證不得被破壞：debounce 期間累積的未落盤變更，在正常關閉路徑上仍必須寫入。

## 檔案與範圍

- `src/app_shell/main.cpp`：`save_now`、`schedule_session_save`（:2393-2401）、上表所有呼叫點行號（以實作時 `rg -n "save_now\(state|save_now\(\*state" src/app_shell/main.cpp` 的實際結果為準）、`WM_TIMER` 的 `kSessionSaveTimerId` 處理（:5992-5994）、關閉路徑的強制同步落盤點（:6073-6077 一帶、`begin_shutdown`、`:4951`）。
- `src/core/session.cpp`：`write_session`（:580 一帶）——**不需修改**，僅供理解成本。
- `docs/tickets/PD-091-session-save-synchronous-on-every-navigation.md`（機制來源與其明確的範圍邊界）、`PD-078-crash-safe-session-write.md`。

## Scope

1. 把互動路徑的同步 `save_now` 改為 `schedule_session_save`；逐點確認並在交接區列出哪些呼叫點刻意保留同步及理由。
2. 確認關閉／`WM_QUERYENDSESSION`／dirty 的 `WM_DESTROY` 三條路徑仍會強制同步落盤（不新增行為，只驗證不回歸）。
3. 新增或擴充一個聚焦 self-check：驗證「連續多次 tab 切換只產生一次實際 `write_session`」。若無法在無 UI 環境下驗證，比照 `PD-091` 的做法用 source-level 檢查 + 計數器說明，並把實機驗證留給使用者，在交接區寫清楚。

## Non-goals

- 不改變 `session.json`/`.bak` 的格式、schema 版本或 atomic-replace 流程。
- 不調整 `kSessionSaveDelayMilliseconds`（500ms 是 `PD-091` 已記錄理由的既有決策；若實作中認為需要改，須在交接區提出理由並視為覆寫該決策）。
- 不做 `PD-091` 當初列為選用、未採納的 `write_session` 讀回驗證優化。
- 不處理 `PD-168`（Shell 解析同步阻塞）——那是同一條路徑上的另一個獨立成本來源。

## Acceptance Criteria

1. 連續快速切換 5 個 tab（或連續 5 次上表任一操作），實際 `write_session` 呼叫次數明顯少於操作次數。
2. 在有未落盤變更的狀態下正常關閉，重開後 session 反映關閉前最新狀態，沒有遺失最後一次操作。
3. 既有涵蓋 `PD-078` crash-safe 寫入行為的測試不受影響。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "save_now\(state|save_now\(\*state|schedule_session_save" src/app_shell/main.cpp
```

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-09-03 實作交接

- `src/app_shell/main.cpp` 的 18 個互動同步存檔點已改用既有 `schedule_session_save`：pinned-location Apply/OK、Group 建立/刪除/排序、active pane、tab 切換/新增/關閉、view mode、Add Current Folder、layout、tab/Group 拖曳排序、Group rename，以及 sidebar/splitter 拖曳結束與 splitter 雙擊。
- 保留的 5 個直接 `save_now` 點均為必要同步路徑：`schedule_session_save` 的 `SetTimer` 失敗 fallback、shutdown reducer 的關閉存檔、`WM_TIMER` 到期實際寫入、dirty `WM_DESTROY` 補寫，以及啟動時寫入 `clean_shutdown=false` marker。`WM_CLOSE`/`WM_ENDSESSION` 仍經 shutdown save，未改 session 格式、atomic replace 或 500ms 常數。
- 擴充 `tests/release/shutdown_state_check.ps1` 為 focused source self-check：逐一確認 tab/Group/layout/view/drag mutation function 使用 debounce、直接同步呼叫只剩上述 5 點、session-save timer 先 `KillTimer` 且只含 1 個實際 `save_now`。無 UI CTest seam，因此以五次操作對一個 timer write 的 source-level counter 取代真實 UI 計數；連續 tab 操作的實機計數留給使用者驗證。

### Agent Checks

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：PASS。
- `cmake --build build`：PASS。
- `ctest --test-dir build --output-on-failure`：17/18 通過；`panedock_launch_smoke` 在本 sandbox 因 `%LOCALAPPDATA%\PaneDock` 實際寫入被拒絕而開啟既有 save-failure `MessageBoxW`，程序未能完成 smoke close。ACL 與 `session.json`/`.bak` 存在性已確認；未將此環境限制誤判為 PD-174 shutdown 回歸。
- `panedock_shutdown_state` 內的 `session_save_debounce_check`：PASS；`rg -n "save_now\(state|save_now\(\*state|schedule_session_save" src/app_shell/main.cpp`：PASS，直接存檔僅保留 5 個必要點。
