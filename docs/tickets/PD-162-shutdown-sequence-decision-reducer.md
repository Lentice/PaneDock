# PD-162 — 把關閉／重入決策抽成 `core` 的 shutdown sequence reducer

Phase 7 · architecture · Depends on: PD-123, PD-140, PD-148, PD-155, PD-159

- Source: 2026-09-01 `improve-codebase-architecture` 架構審查，結論由背景 Codex 唯讀核對（判定 PARTLY WRONG，方向成立、數字修正見下）。延續 2026-08-27 三方審查留下的「`AppState` 拆分」候選。
- Priority: HIGH——近三十個 commit 中約十個是 shutdown／re-entrancy hardening（`Guard ExplorerHost callback re-entry`、`Harden Shell re-entry shutdown handling`、`Close single-instance reacquire race`、`Keep crash marker dirty until shutdown is confirmed`），每次修正都再往同一個 struct 加一個布林旗標，而這段邏輯目前沒有任何 runnable 測試可以先寫出紅燈。

## Outcome

`src/core` 新增一個不含 HWND／COM 的 shutdown 決策 reducer：事件進、動作出。`app_shell` 保留所有實際動作（存檔、destroy views、destroy window、寫 clean marker、彈 transfer dialog），但不再自行判斷「現在該不該做」。既有可觀察行為完全不變，本票是純重構加測試。

## 已確認的現況（2026-09-01 工作樹，經 Codex 唯讀核對）

- `AppState` 位於 `src/app_shell/main.cpp:459`。
- 關閉／重入相關的**布林**旗標實際是 **13 個**，不是 14 個：`closing_`、`quit_requested`、`shutdown_prompt_active`、`shutdown_save_attempted`、`shutdown_clean_marker_armed`、`shutdown_deferred`、`shutdown_message_queued`、`end_session_pending`、`main_window_destroyed`、`file_operation_call_active`、`file_operation_in_progress`、`close_after_file_operation`、`cancel_file_operation`。
- `shell_call_depth`（`main.cpp:498`）是 `unsigned`，不是布林；`session_dirty`（`main.cpp:474`）是 persistence 旗標，只有在「關閉時要不要 force save」這一點上與本票相關。
- 這些名稱在 `main.cpp` 共 274 次 identifier 出現、180 個不同 source line；扣掉欄位宣告後是 **166 個不同的操作行**。
- 目前**沒有** `ShutdownState` enum 或任何 state-machine 型別。轉移規則散在 `begin_shutdown`／`finish_shutdown`／`complete_deferred_close`（`main.cpp:4684` 一帶）、`ShellCallScope`、`file_operation_*` callbacks、`transfer_close_dialog_proc` 與 `window_proc`。
- **但「完全沒有寫下來」不成立**：`docs/design-spec.md §9.4` 已明列關閉順序。本票要做的是讓那份順序在程式碼裡有一個可執行、可測試的對應物，不是發明新規則。
- `src/core` 目前對 `HWND`／`windows.h`／COM／`HRESULT` 是 0 matches，符合既有 boundary。
- `tests/release/shutdown_state_check.ps1` 與 `tests/release/shell_reentry_gate_check.ps1` 是以 regex 讀原始碼字串的 source-level 檢查（`shutdown_state_check.ps1:6`、`shell_reentry_gate_check.ps1:9`），雖已註冊為 CTest（`tests/CMakeLists.txt:37`），但不啟動程式、不驗證實際 message-loop 行為。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.4：

> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**(每個曾 `Initialize` 的都必須 `Destroy`)
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`
>
> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`docs/design-spec.md` §9.1：

> | `core` | Group／pane／tab 資料模型、版型矩形計算、session 序列化與遷移 | **任何 HWND、COM 或 `windows.h`** |

`docs/design-spec.md` §9.1：

> `core` 刻意不含 COM——它是本專案唯一的自動測試 seam。

`AGENTS.md`：

> Keep `src/core` free of HWND, COM and `windows.h`. It is the only automated test seam in this project (`docs/testing.md`); every type that leaks into it costs that seam.

`AGENTS.md`：

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`docs/tickets.md` Agent 交付規則：

> 必須保持既有 build／CTest 可用;不得用關閉測試來取得綠燈。

## Files to read and trace first

- `src/app_shell/main.cpp:459-600`（`AppState` 全部旗標與各自的既有註解——那些註解就是目前唯一的轉移規格，必須逐條保留其語意）。
- `src/app_shell/main.cpp:603-660`（`finish_shell_call`、`ShellCallScope`、`app_shell_call_state_changed`）。
- `src/app_shell/main.cpp:4684` 一帶（`begin_shutdown`、`finish_shutdown`、`complete_deferred_close`、`show_transfer_close_dialog`、`transfer_close_dialog_proc`、`file_operation_*` callbacks）。
- `src/app_shell/main.cpp` 的 `window_proc`：`WM_CLOSE`、`WM_DESTROY`、`WM_ENDSESSION`、`kDeferredShutdownMessage`、`kFileOperationFinishedMessage`。
- `src/core/session.h/.cpp`：`save_now` 呼叫的 persistence 與 clean marker 語意。
- `tests/release/shutdown_state_check.ps1`、`tests/release/shell_reentry_gate_check.ps1`：目前的 source-level 斷言內容，決定哪些字串不能消失。
- `docs/tickets/PD-123-close-during-shell-copy-validation.md`、`PD-140-shell-call-reentry-shutdown-gate.md`、`PD-148`（前景化失敗沿 relay 回傳）。
- `docs/design-spec.md` §9.4。

## Scope

1. 新增 `src/core/shutdown.h/.cpp`（沿用既有 `panedock_core` target，不新增 CMake target）：
   - `enum class ShutdownEvent`：至少涵蓋 `close_requested`、`end_session`、`shell_call_entered`、`shell_call_left`、`file_operation_started`、`file_operation_finished`、`transfer_keep_open`、`transfer_close_after_transfer`、`transfer_cancel_and_close`、`save_succeeded`、`save_failed`、`views_destroyed`、`window_destroyed`。
   - `enum class ShutdownAction`：至少涵蓋 `none`、`defer`、`prompt_transfer`、`save_session`、`destroy_views`、`destroy_window`、`write_clean_marker`、`request_quit`。
   - `class ShutdownSequence`：持有目前狀態，`ShutdownAction step(ShutdownEvent)`（或回傳小型 action 集合，視既有邏輯需要而定），加上唯讀查詢供 `window_proc` 判斷是否已在關閉中。
   - 型別只用 `std::`；不得 include `windows.h`，不得出現 `HWND`、`HRESULT`、COM。
2. 逐條把 `AppState` 上述 13 個布林與 `shell_call_depth` 的**判斷**語意搬進 reducer。`AppState` 保留必要欄位時，必須降級為 reducer 狀態的轉發，不得同時存在兩份權威。
3. `app_shell` 只負責兩件事：把 Win32／Shell 事件翻成 `ShutdownEvent`，以及執行回傳的 `ShutdownAction`。§9.4 的順序由 reducer 決定，`app_shell` 依序執行。
4. `docs/design-spec.md §9.4` 的六個步驟必須在 reducer 有可指認的對應（測試中以斷言證明順序）。
5. 為 `src/core/shutdown` 新增 `tests/unit/core_shutdown_test.cpp` 並註冊到 `tests/CMakeLists.txt`，至少涵蓋：
   - 一般關閉：`close_requested` → save → destroy views → destroy window → clean marker，順序正確。
   - 關閉發生在 shell call 之中（`shell_call_entered` 後 `close_requested`）→ 回傳 `defer`；`shell_call_left` 後才推進。
   - 巢狀 shell call（depth 2）→ 只有最外層離開才推進。
   - 檔案操作進行中關閉 → `prompt_transfer`；三個 transfer 選項各自的後續路徑。
   - `WM_ENDSESSION` 在 save-failure 或 file operation 進行中抵達。
   - 重入的第二次 `close_requested` 不得重跑 teardown。
   - `save_failed` 時**不得**寫 clean marker。
6. `tests/release/shutdown_state_check.ps1` 與 `shell_reentry_gate_check.ps1` 若因搬移而找不到原字串，更新其 pattern 指向新位置；不得為了讓它們通過而保留死程式碼，也不得直接刪除這兩個檢查。

## Non-goals

- 不改變任何使用者可觀察的關閉行為、對話框文字、時序或 §9.4 順序。
- 不改 session schema、不改 clean/crash marker 的檔案格式。
- 不動 `ExplorerHost::destroy()` 內部、不動 single-instance relay、不動啟動序列。
- 不引入 threading、async、timer 或 polling。
- 不把 `AppState` 其餘（pane chrome、drag、layout）欄位一併重整——那是 PD-163 的範圍。
- 不新增對 `IExplorerBrowser` 的抽象層（`docs/tickets.md` 已否決的方向）。

## Acceptance Criteria

1. `src/core/shutdown.h/.cpp` 存在，且 `grep -E "HWND|windows\.h|HRESULT|ComPtr|IUnknown" src/core/shutdown.*` 無結果。
2. 上述 13 個布林旗標在 `main.cpp` 中不再有獨立的判斷邏輯；每個保留下來的欄位都可指出它只是 reducer 狀態的轉發。
3. `tests/unit/core_shutdown_test.cpp` 涵蓋 Scope 第 5 點全部案例並通過。
4. `ctest --test-dir build --output-on-failure` 全綠，包含既有的 `shutdown_state_check`、`shell_reentry_gate_check` 與 `panedock_launch_smoke`。
5. 實機以一般模式啟動 `build\PaneDock.exe` 後正常關閉，程序不殘留（比照 PD-106 的驗收方式），且 `session.json` 的 `clean_shutdown` 正確變為 `true`。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# core 邊界
Select-String -Path src/core/shutdown.* -Pattern 'HWND|windows\.h|HRESULT|ComPtr|IUnknown'   # 必須無輸出
# 舊旗標不得殘留獨立判斷
Select-String -Path src/app_shell/main.cpp -Pattern 'shutdown_deferred|close_after_file_operation|shutdown_clean_marker_armed'
```

```powershell
# 關閉不殘留（PD-106 方式）
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 3
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(5000)) { throw 'process survived graceful close' }
```

## Handoff requirements

在本檔 `## 交接區` 記錄：reducer 的完整事件／動作對照表、13 個舊旗標各自對應到 reducer 的哪個狀態或事件、兩支 PowerShell 檢查的 pattern 如何更新、以及實機關閉驗證的命令與結果。若某個舊旗標**無法**搬進 reducer，寫出具體的 Win32／COM 依賴理由。

## 交接區
