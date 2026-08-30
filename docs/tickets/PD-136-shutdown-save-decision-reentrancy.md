# PD-136 — close save failure 在 `WM_DESTROY` 重試且吞掉進行中的 Windows shutdown

Phase 7 · app_shell shutdown state · Depends on: PD-125, PD-133

- Source: 2026-08-30 close/startup lifecycle audit loop。
- Priority: HIGH——disk full、權限拒絕、anti-virus lock 或慢速 storage 讓 final save 失敗時，現行流程會再同步重試一次並可能拖住關閉；同時 Windows 在 save-failure dialog 的 nested modal loop 送來 confirmed `WM_ENDSESSION` 時會被 `shutdown_prompt_active` 直接丟掉，讓已確認的關機無法完成。

## Goal

Make the final-save decision single-shot and re-entrancy safe:

1. After `begin_shutdown` has attempted the final save, `WM_DESTROY` must not silently attempt the same blocking write again.
2. A confirmed `WM_ENDSESSION(TRUE)` received during the save-failure prompt, or while a Shell file operation is being cancelled, must be remembered and finish shutdown without showing a blocking keep-open prompt.
3. A failed/indeterminate warning-dialog result must keep the app open; only an explicit `No` means “close without saving”.

## Confirmed root cause and callers

- `begin_shutdown` in `src/app_shell/main.cpp` calls `save_now(state, true, true)`. On failure it leaves `session_dirty` true; when normal close chooses `No`, or confirmed session end uses `allow_keep_open=false`, teardown proceeds.
- `WM_DESTROY` then tests only `session_dirty` and calls `save_now` again. A disk/AV failure therefore becomes two synchronous close attempts, and `No` does not actually mean “close without another save attempt”.
- `begin_shutdown` returns immediately when `shutdown_prompt_active` is true. A nested `MessageBoxW` loop can dispatch `WM_ENDSESSION(TRUE)`, so the confirmed shutdown request disappears when the prompt returns.
- `WM_ENDSESSION(TRUE)` during an active file operation sets `close_after_file_operation`, and `complete_deferred_close` currently calls the default `begin_shutdown(..., true)`, which can show the normal keep-open prompt during system shutdown.

## Binding constraints

`AGENTS.md`:

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> All user data lives under `%LOCALAPPDATA%\\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

> App UI text must be English. New non-trivial logic needs one focused runnable test or self-check.

`docs/design-spec.md §9.4`:

> 1. capture current state and atomically write the session document; 2. destroy all live `IExplorerBrowser`; 3. destroy pane HWND; 4. destroy the main window; 5. exit the message loop; 6. `CoUninitialize`.

The save attempt remains before Shell/window teardown. This ticket changes only whether that already-attempted save is repeated and how a nested confirmed shutdown is carried forward.

## Files to read and trace

- `src/app_shell/main.cpp`: `AppState`, `begin_shutdown`, `complete_deferred_close`, `WM_CLOSE`, `WM_DESTROY`, `WM_ENDSESSION`, and all `begin_shutdown` callers.
- `src/core/session.cpp`: `write_session` result contract; do not change its atomic-write algorithm in this ticket.
- `docs/tickets/PD-125-close-sequence-reentrancy-and-endsession-exit.md` and `docs/tickets/PD-133-session-save-failure-is-silent.md`: existing close guards and save-failure UX decisions.
- `tests/CMakeLists.txt`: register the focused self-check if a new script is added.

## Scope

1. Add the smallest AppState state needed to remember that final-save has already been attempted and that confirmed system shutdown arrived during a prompt/file-operation barrier.
2. Factor or reuse the existing teardown body so the pending-shutdown path does not call `save_now` a second time.
3. Make `WM_DESTROY` retain its fallback save only for an unexpected destroy for which no final-save attempt was made.
4. Route deferred close after confirmed `WM_ENDSESSION(TRUE)` through `allow_keep_open=false`.
5. Preserve the existing English prompt and make only explicit `IDNO` proceed after an interactive save failure; `IDYES` and an indeterminate dialog result keep the window open unless confirmed system shutdown is pending.
6. Add one focused runnable source/self-check covering the state-machine invariants that do not require a live Shell view; document the live nested-modal/system-shutdown limitation in the handoff.

## Non-goals

- Do not change session JSON schema, clean-marker policy, `write_session`, or backup durability in this ticket.
- Do not add a worker thread, polling timer, storage timeout, retry loop, or forced process termination.
- Do not make `WM_QUERYENDSESSION` block or veto shutdown.
- Do not reorder `IExplorerBrowser::Destroy`, parent-window destruction, `PostQuitMessage`, or `CoUninitialize`.
- Do not modify Shell view lifetime or add a fake COM abstraction.

## Acceptance criteria

1. A final `save_now` failure is attempted once per close decision; `WM_DESTROY` does not repeat it after `begin_shutdown` has made the decision.
2. Confirmed `WM_ENDSESSION(TRUE)` received inside the save-failure dialog is remembered and completes teardown after the dialog returns, without a second prompt.
3. Confirmed `WM_ENDSESSION(TRUE)` during file-operation cancellation reaches `begin_shutdown(..., false)` after the operation barrier completes.
4. Normal save failure: explicit `Yes` keeps the app open for retry; explicit `No` closes without another save; an indeterminate dialog result keeps it open.
5. Existing clean-save and non-failure close paths still destroy all live views before the parent and exit the message loop.
6. Focused self-check, build, CTest, and `git diff --check` pass. Launch smoke may require an idle PaneDock instance to be closed first.

## Agent checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/shutdown_state_check.ps1
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "shutdown_save_attempted|end_session_pending|WM_DESTROY|WM_ENDSESSION|allow_keep_open" src/app_shell/main.cpp
```

## Handoff requirements

- Record the exact state flags and the single final-save/teardown path.
- Record deterministic self-check, build, CTest, and any live desktop close/shutdown evidence separately.
- State whether launch smoke was blocked by an existing PID; do not force-kill an unrelated/user-owned process.

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- `AppState` 新增 `shutdown_save_attempted` 與 `end_session_pending`。`begin_shutdown` 先記錄 final-save attempt，再進入共用 `finish_shutdown`；`WM_DESTROY` 僅在 unexpected destroy 尚未嘗試 final save 時保留 fallback，避免 disk/AV failure 造成第二次同步阻塞。
- `WM_ENDSESSION(TRUE)` 在 save-failure MessageBox 或 `IFileOperation` barrier 期間會留下 pending flag；dialog 返回後或 operation 完成後不再顯示 keep-open prompt，直接沿既有 §9.4 teardown。互動 prompt 只有明確 `IDNO` 才 close，其他結果保留 UI 供重試。
- 新增 `tests/release/shutdown_state_check.ps1` 並註冊 `panedock_shutdown_state` CTest。self-check、`cmake --build build` 與前 7 個 deterministic CTest 通過。
- 完整 CTest 的 `panedock_launch_smoke` 仍受測試環境 `%LOCALAPPDATA%\\PaneDock` 寫入權限／既有 PD-133 save-failure prompt 影響，30 秒後失敗；測試 finally 已清理由該測試建立的 PID。未強殺既有使用者程序，也未宣稱 live shutdown 驗收通過。
- 未修改 session schema、`write_session`、Shell lifetime、thread、timer、shutdown 順序或強制終止行為。
