# PD-143 — clean marker 延後到完整 Shell/COM teardown 後，避免 force-kill false-clean

Phase 7 · app_shell shutdown correctness · Depends on: PD-025, PD-032, PD-132, PD-136

- Source: 2026-08-30 close/startup audit loop；受控 elevated desktop teardown
  race repro。
- Priority: HIGH——`begin_shutdown` 在 `IExplorerBrowser::Destroy` 前呼叫
  `save_now(state, true, true)`。Shell/extension teardown 若因系統太慢、disk/AV
  或 re-entry 拖住，使用者可在 marker 已變成 `true`、程序仍活著時 force kill；
  下一次啟動會漏報本次未完成 teardown。這直接削弱 FR-013 的 crash recovery
  與使用者要求的 force-kill audit。

## Goal

讓 durable `clean_shutdown=true` 只在以下條件都成立後寫入：

1. final session snapshot 已成功寫成 `clean_shutdown=false`；
2. 所有 live `IExplorerBrowser` 已 `Destroy`、主視窗已銷毀、message loop 已
   結束，且 `OleUninitialize` 已返回。

若 process 在 save、Shell/COM teardown 或最後 marker write 期間被 force kill、
crash、卡死或遇到 disk/AV failure，磁碟上的 marker 必須保持 `false`，下一次
啟動沿既有英文 unclean-shutdown MessageBox 告知使用者。Windows 的強制終止／
斷電沒有可攔截的 close message，不承諾攔截它們。

## Confirmed root cause and evidence

- `src/app_shell/main.cpp:4721` 的 `begin_shutdown` 目前在
  `src/app_shell/main.cpp:4695` 的 `destroy_explorers` 前寫入 `true`。
- `WM_DESTROY` 的 fallback `save_now(..., true, true)` 也可能把 unexpected
  destroy 標成 clean。
- `wWinMain` 在 message loop 結束後仍會再次呼叫 `destroy_explorers`，接著
  `OleUninitialize`；這是可放置最後 clean write 的單一離開點，且此時不再有
  live Shell view 或主視窗。
- 實際 elevated repro（先備份並於 finally 還原 `%LOCALAPPDATA%\PaneDock`）：
  啟動本次 build、送 `CloseMainWindow()`，觀察到
  `CLEAN_SEEN_WHILE_ALIVE=True`、`STILL_ALIVE_BEFORE_KILL=True`，立即只對該
  PID `Stop-Process -Force` 後 `AFTER_FORCE_CLEAN=True`。相對地，程序正常執行
  中直接 force kill 留下 `AFTER_FORCE_FALSE=True`，證明缺口位於 teardown window。

## Override of PD-025

PD-025 歷史決策 2 將「啟動後寫 false、正常關閉前最後一次寫 true」定義為
marker 時機。該文件不修改；本票以新的使用者需求（涵蓋 force kill、Windows
shutdown、slow disk/AV、race）與上面的真實 teardown repro 覆寫「true 必須在
teardown 前」這一部分。啟動 false marker、session schema、backup 保護與
既有 recovery UI 不變。

## Binding constraints

`docs/design-spec.md §9.4`:

> 1. 擷取現行狀態並原子寫入 session document

> 2. destroy 全部 live `IExplorerBrowser`

> 3. destroy pane HWND

> 4. destroy 主視窗

> 5. 退出訊息迴圈

> 6. `CoUninitialize`

`AGENTS.md`:

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

> Do not add a worker thread, busy loop, polling timer, or forced process termination for this lifecycle problem.

> App UI text must be English. New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace

- `src/app_shell/main.cpp`: `AppState`, `save_now`, `finish_shutdown`,
  `begin_shutdown`, `WM_DESTROY`, `WM_QUERYENDSESSION`, `WM_ENDSESSION`, the
  outer message loop and all `save_now` callers.
- `src/core/session.cpp/.h`: `write_session` atomic replacement and
  `SessionDocument::clean_shutdown`; no schema change.
- `src/explorer_host/explorer_host.cpp/.h`: `Destroy` contract and live-view
  count; do not change the host lifetime implementation in this ticket.
- `docs/design-spec.md §9.2, §9.4, §11` and `docs/development.md` shutdown/re-entry
  rules。
- `docs/tickets/PD-025-crash-recovery-path.md`, `PD-032-endsession-clean-shutdown-handling.md`,
  `PD-132-session-query-marks-cancelled-shutdown-clean.md`,
  `PD-136-shutdown-save-decision-reentrancy.md`, and `PD-140-shell-call-reentry-shutdown-gate.md`。
- `tests/release/shutdown_state_check.ps1` and `tests/CMakeLists.txt` for the
  existing focused shutdown state self-check。

## Scope

1. Add the minimum state needed to distinguish a successful pre-teardown false
   marker save from an unexpected/failed close, and to know that `WM_DESTROY`
   actually occurred.
2. Change the normal and confirmed-session final pre-teardown save to
   `clean_shutdown=false`; arm the final clean write only when that save succeeds.
3. Change the unexpected `WM_DESTROY` fallback save to `false`, never `true`.
4. After the outer message loop has exited and `OleUninitialize` has returned,
   write the existing `SessionDocument` once with `clean_shutdown=true` only when
   the successful false save and actual main-window destruction were recorded.
   Reuse `panedock::core::write_session` and `flush_session_file`; do not call
   `save_now` after Shell/COM teardown because it is a state-capture path.
5. Extend `shutdown_state_check.ps1` to assert the false-before-teardown / true-
   after-`OleUninitialize` ordering and the unexpected-destroy false fallback.

## Non-goals

- Do not change session schema, atomic replacement, backup behavior, startup
  recovery messages, `WM_QUERYENDSESSION` semantics, or `IExplorerBrowser` host code.
- Do not add a timeout, retry, worker, async runtime, polling loop, progress UI, or
  `TerminateProcess`.
- Do not promise a prompt while the final marker write or `OleUninitialize` is
  blocked; the safe observable result is a retained false marker and a warning on
  the next startup.
- Do not rewrite completed PD-025/PD-032/PD-132/PD-136 documents.

## Acceptance criteria

1. A normal `WM_CLOSE` and confirmed `WM_ENDSESSION(TRUE)` write durable
   `clean_shutdown=false` before any live-view or parent-window teardown.
2. `WM_DESTROY` fallback never writes `true`; an unexpected destroy leaves the
   marker false when the write succeeds.
3. The final `true` write occurs only after the message loop has ended and
   `OleUninitialize` has returned, and only after a successful pre-teardown false
   save plus observed main-window destruction.
4. If the final true write fails or the process is force-killed before it, the
   existing durable marker remains false and the next startup warning remains
   available; no second teardown or Shell call is introduced.
5. Existing re-entry, file-operation close, cancelled/confirmed Windows shutdown,
   and view-destroy ordering remain intact.
6. Focused self-check, build, CTest, `git diff --check`, and an elevated ordinary
   launch smoke pass. The controlled teardown force-kill result is recorded
   separately from deterministic source checks.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/shutdown_state_check.ps1 -SourcePath src/app_shell/main.cpp
ctest --test-dir build -E panedock_launch_smoke --output-on-failure
git diff --check
```

```powershell
ctest --test-dir build -R "^panedock_launch_smoke$" --output-on-failure
```

The second command needs a real writable `%LOCALAPPDATA%\PaneDock`; it must only
target the process started by the test and must not force-kill an unrelated PID.

## Handoff requirements

- Record the new flags and the exact false-save → Shell/COM teardown → true-save
  sequence.
- Record why the final write is after `OleUninitialize`, and what happens if it
  blocks or fails.
- Record elevated normal smoke and controlled teardown force-kill evidence with
  the session files restored; do not call source matching a runtime proof.
- Record remaining startup warning aggregation and pre-window session read/marker
  save boundaries for the next audit round.

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- `AppState` 新增 `shutdown_clean_marker_armed` 與
  `main_window_destroyed`。`begin_shutdown` 現在先以
  `save_now(state, false, true)` 保存最新狀態並只在成功時 arm；
  `WM_DESTROY` 的 unexpected fallback 也只寫 `false`。
- 正常 close／confirmed `WM_ENDSESSION(TRUE)` 經既有 teardown 後，outer
  message loop 結束、`destroy_explorers` 確認 live count 為 0、且
  `OleUninitialize` 返回，才以既有 `write_session` 原子寫入
  `clean_shutdown=true`。若此最後 write 卡住或失敗，沒有可用主視窗可提示，
  但 durable false marker 保留，下一次 startup 會顯示既有 unclean warning。
- `shutdown_state_check.ps1` 現在驗證 flags、false-before-teardown、
  unexpected-destroy fallback，以及 true marker 位於最後一次
  `OleUninitialize` 之後；禁止任何 `save_now(..., true)` 的 pre-teardown
  路徑。
- deterministic 結果：focused `shutdown_state_check` PASS；LLVM-MinGW
  Release build PASS；非 launch CTest 10/10 PASS；`git diff --check` PASS。
- elevated real desktop 結果（每次均只操作本次啟動的 PID，並在 finally
  還原 `%LOCALAPPDATA%\PaneDock\session.json(.bak)`）：
  `CLEAN_CLOSE_AUDIT ... EXIT_CODE=0 CLEAN_AFTER_EXIT=True`；修正後受控
  teardown force-kill 為 `CLEAN_BEFORE_FORCE=False WAS_ALIVE=True
  AFTER_FORCE_CLEAN=False EXITED=True`；elevated `panedock_launch_smoke`
  亦 1/1 PASS。
- 先前未修正版本的相同 teardown repro 為
  `CLEAN_SEEN_WHILE_ALIVE=True STILL_ALIVE_BEFORE_KILL=True
  AFTER_FORCE_CLEAN=True`，作為 PD-025 override 的實證；一般執行中直接
  force kill 則維持 `AFTER_FORCE_FALSE=True`。
- 尚未真正登出／關機或注入 offline/slow-disk/anti-virus provider；
  `WM_QUERYENDSESSION`／`WM_ENDSESSION` 的 deterministic path 由既有
  `shutdown_state_check` 驗證。Windows 強制終止、斷電與永不返回的 OS call
  無法由 app 攔截；本票保證的是 false marker 不會被過早改成 true。
- 後續 startup audit：`startup_warning_message` 仍可能在不同 failure path
  覆蓋，且 deferred Shell failure 在已有 warning 時可能靜默；pre-window
  `read_session`／startup false-marker save 的同步阻塞仍是 §9.2 邊界。
