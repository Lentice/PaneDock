# PD-177 — Shutdown 恢復條件在三個呼叫點各自重複檢查一部分，而非集中在 reducer

Phase 7 · core / app_shell · 修正 PD-171～PD-175 疊加後的整合缺陷

## 來源

2026-09-03，使用者回報一個測試用 `PaneDock.exe`（PID 37396）卡在「關閉中/無回應」狀態，`build\PaneDock.exe` 的 single-instance relay 顯示「PaneDock is already running but not responding, or it is shutting down.」。PD-168～PD-175 這 8 張票由不同 Codex session 疊加實作、彼此之間沒有整合測試，被要求根因分析。

## 根因

`src/core/shutdown.cpp` 的 `ShutdownSequence::step()` 中，「一個延後的 shutdown 何時可以恢復」這個判斷被拆成三份，各自只檢查其中一個 gate：

- `shell_call_left`（PD-172 引入）恢復條件只檢查 `shell_call_depth == 0 && shutdown_deferred && !shutdown_message_queued`，**沒有檢查 `drag_in_progress`**。
- `drag_finished`（PD-173 引入）恢復條件只檢查 `!drag_in_progress && !closing_ && shutdown_deferred && !shutdown_message_queued`，**沒有檢查 `shell_call_depth`**。
- `deferred_shutdown_ready`（PD-172 引入、PD-173 疊加）則同時檢查兩者，各自獨立處理並清除 `shutdown_message_queued`。

`AGENTS.md` 的規則是「一個共用函式裡的 guard，比每個呼叫點各自加 guard的diff更小；只修票據指名的路徑，會讓其餘 sibling caller 繼續壞。」這裡的三個 case 互為 sibling caller，卻各自實作了部分正確的恢復條件——目前找到的呼叫序列都會經由 `deferred_shutdown_ready` 的完整檢查自我修正（多繞一次 `PostMessageW` 往返），沒有實測到永久卡死；但這是巧合自癒，不是設計保證：`shell_call_left`／`drag_finished` 任何一個在未來新增第三個 gate（例如檔案操作、prompt）時，只要漏了其中一個檢查，就會在該 gate 仍卡著的狀態下錯誤回傳 `ShutdownAction::defer`，讓上層 `finish_shell_call`／`drag_state_changed`（`src/app_shell/main.cpp`）跑出一次無意義甚至有害的 `deferred_shutdown_queued` 訊息序列。

PID 37396 本身的卡死更可能是 `SHCreateItemFromParsingName`／`IExplorerBrowser::BrowseToObject` 對一個緩慢或離線的 Shell provider 做同步呼叫時真正卡在系統呼叫裡（PD-168/169 的 `IBindCtx` deadline 只能限制 `SHCreateItemFromParsingName`，`BrowseToObject` 本身仍是全同步、且 Windows 沒有能在同一 STA 安全中止的 API，PD-168/169 的交接區已記載此邊界）——這屬於已知、尚未解決的平台限制，不在本票範圍內，改善方向留給後續量測後另開票。

## Fix

把「shutdown 是否可以恢復」的判斷收斂成 `shutdown.cpp` 內的一個共用 predicate `shutdown_ready_to_resume(state)`，同時檢查 `shutdown_deferred && !shutdown_message_queued && !closing_ && shell_call_depth == 0 && drag_target_count == 0`，讓 `shell_call_left`、`drag_finished`、`deferred_shutdown_ready` 都呼叫同一份判斷，不再各自維護部分條件。同時移除 `close_requested` 內一段在 PD-173 疊加時留下的死碼（`drag_in_progress` 分支與其後的無條件分支邏輯完全相同）。

`end_session` 內同樣有一段功能上重複的 `drag_in_progress` 分支（PD-173），評估後保留不動：拿掉它會改變 `shutdown_prompt_active`/`shutdown_save_attempted` 同時為真時的判斷順序，屬於行為變更而非本票範圍的清理。

## 檔案與範圍

- `src/core/shutdown.cpp`：新增檔案內部（匿名 namespace）的 `shutdown_ready_to_resume`；`shell_call_left`、`drag_finished`、`deferred_shutdown_ready`、`close_requested` 改用/清理。
- 未變更 `src/core/shutdown.h`（無新增 state 欄位、無 API 變更）。
- 未變更 `src/app_shell/main.cpp`：`finish_shell_call`、`run_shutdown_action`、`drag_state_changed` 對 `ShutdownAction` 的處理方式不變，因為 reducer 回傳的 action 語意沒有變，只是判斷更準確。

## Non-goals

- 不解決 `BrowseToObject`／`SHCreateItemFromParsingName` 對慢速 Shell provider 的同步阻塞本身（PD-168/169 已知邊界，需另外量測與設計非阻塞路徑才能開票）。
- 不新增 state 欄位或改變 `ShutdownEvent`/`ShutdownAction` 的公開介面。
- 不處理 `end_session` 內的重複分支（見上）。

## Acceptance Criteria

1. `tests/unit/core_shutdown_test.cpp` 既有測試（含 PD-172/PD-173 新增的巢狀 Shell call、drag 案例）全數通過，不修改既有測試的期望值。
2. `shell_call_left` 在 `drag_in_progress` 為真時，即使 `shell_call_depth` 歸零，也不得回傳 `ShutdownAction::defer`。
3. `drag_finished` 在 `shell_call_depth != 0` 時，即使拖曳全部結束，也不得回傳 `ShutdownAction::defer`。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過（19/19，含 `panedock_shutdown_state`、`panedock_shell_reentry_gate`、`panedock_launch_smoke`）。
5. 手動驗證：`build\PaneDock.exe` 一般啟動後關閉、剛啟動立即關閉，兩者視窗與程序都確實結束，`tasklist` 查不到殘留 PID。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 實作交接（2026-09-03）

- 根因：`shutdown.cpp` 的 `shell_call_left`／`drag_finished` 各自只檢查一部分恢復條件（缺 `drag_in_progress`／`shell_call_depth`），只靠 `deferred_shutdown_ready` 的完整檢查自我修正，屬於巧合自癒而非設計保證。用窮舉所有 `close_requested`/`shell_call_entered`/`shell_call_left`/`drag_started`/`drag_finished`/`deferred_shutdown_ready` 事件交錯序列（深度 9）的 Python 模型驗證：修正前後皆未發現永久卡死狀態——現有呼叫序列下這不是一個會實際重現的死鎖，而是一個脆弱、未來容易被打破的設計。
- 沒有找到 PID 37396 卡死的可重現路徑：本地重建 HEAD 後，一般關閉與啟動後立即關閉都乾淨結束、`tasklist` 無殘留。最可能的解釋是 `BrowseToObject`／`SHCreateItemFromParsingName` 對慢速/離線 Shell provider 的同步系統呼叫真正卡住（PD-168/169 已記載的已知邊界，非本票範圍）。
- Fix：抽出共用 predicate `shutdown_ready_to_resume`，三個恢復路徑改用同一份判斷；順帶移除 `close_requested` 內的死碼分支。未新增 state 欄位、未改公開介面。
- Agent checks：configure PASS；`cmake --build build` PASS；`ctest --test-dir build --output-on-failure` PASS（19/19）；手動啟動/一般關閉/剛啟動立即關閉三種情境下 `tasklist` 均無殘留 PID。
