# PD-140 — Shell API re-entry 可在 outer call 未返回前 destroy parent 或繼續碰死 COM

Phase 7 · app_shell / explorer_host lifecycle · Depends on: PD-125, PD-127, PD-136

- Source: 2026-08-30 close/startup audit loop。
- Priority: CRITICAL——`IExplorerBrowser::Initialize`, `BrowseToObject`, `Destroy`, view queries and file-operation/Shell calls can re-enter the STA message loop. A close dispatched inside one of those calls can destroy the parent or reset an `ExplorerHost` while the outer call still resumes. That creates a concrete dead-parent/UAF/null-COM crash path, not a theoretical thread race.

## Goal

Make close requests during any app-owned `ExplorerHost` call defer until the outermost Shell call returns, and ignore queued interaction work while that close is pending. The existing §9.4 teardown order remains the only teardown path.

## Confirmed root cause and callers

- `src/explorer_host/explorer_host.cpp::ExplorerHost::initialize` sets `initialized_` only after `IExplorerBrowser::Initialize` returns, then calls `Advise` and `navigate`. If a re-entered `WM_CLOSE` runs during `Initialize`, current `destroy()` sees `!initialized_` and returns; the outer function can then mark the host live and use a parent HWND already destroyed by shutdown.
- If re-entry occurs during `initialize`'s `navigate`, shutdown can reset `browser_` while `BrowseToObject`'s caller resumes, allowing a null COM call or stale callback installation.
- `src/app_shell/main.cpp::apply_layout` and the navigation/view-mode/accelerator helpers call `ExplorerHost` methods directly. Several entry guards cover `closing_` only at function entry, not close requests dispatched from inside a Shell call or continuation after that call.
- `IExplorerBrowser::Destroy` can itself pump messages. The current `destroying_` guard prevents only a same-host nested `destroy`; it does not prevent other app commands, layout, navigation, or parent destruction from re-entering.

## Binding constraints

`AGENTS.md`:

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

> Keep `src/core` free of HWND, COM and `windows.h`.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/design-spec.md §9.2` requires one STA UI thread and no custom worker pool. `docs/design-spec.md §9.4` fixes save → destroy all live views → destroy parent → exit loop. Do not solve this by moving Shell work to another thread or by skipping `Destroy`.

## Files to read and trace

- `src/app_shell/main.cpp`: `AppState`, `apply_layout`, `destroy_explorers`, `begin_shutdown`, all `ExplorerHost` call sites, `window_proc`, child subclass procedures, and the outer message loop.
- `src/explorer_host/explorer_host.cpp/.h`: `initialize`, `navigate`, `set_rect`, `set_visible`, view queries, `navigation_complete`, and `destroy`.
- `docs/design-spec.md §9.2, §9.4, §11, §14` and `docs/development.md` Shell re-entry guidance.
- `docs/tickets/PD-125-close-sequence-reentrancy-and-endsession-exit.md`, `PD-127-teardown-reentrancy-guard-and-deferred-startup-error.md`, `PD-136-shutdown-save-decision-reentrancy.md`.
- `tests/CMakeLists.txt` and existing source/lifetime checks.

## Scope

1. Add one small app-shell Shell-call depth guard/RAII scope around every direct `ExplorerHost` method call that can re-enter the STA.
2. When `WM_CLOSE` or confirmed `WM_ENDSESSION(TRUE)` arrives at nonzero depth, record deferred shutdown (and session-end intent when applicable) instead of saving, destroying, or destroying the parent immediately.
3. Post one deferred-shutdown message after the outermost Shell call returns; if posting fails, use a direct post-call fallback without touching the parent from inside the Shell call.
4. Gate the outer `window_proc` and relevant child interaction procedures while shutdown is pending/closing; allow only lifecycle, close/session-end, and deferred-shutdown messages through.
5. After each wrapped Shell call, stop the current continuation when deferred/closing so it cannot install callbacks, mark a dead host realized, navigate another pane, or persist half-mutated state.
6. Add one focused runnable source/lifetime self-check for guard coverage and deferred message handling. Do not add a fake COM abstraction.

## Non-goals

- Do not alter `ExplorerHost`'s COM contract, callback ownership, or `Destroy` order except for a directly evidenced continuation bug.
- Do not add a worker thread, async runtime, polling timer, IPC, process isolation, or timeout around Shell APIs.
- Do not skip a required `IExplorerBrowser::Destroy`, destroy a parent before all initialized views, or use `TerminateProcess`.
- Do not redesign Group/tab/layout behavior or change session schema/atomic persistence.
- Do not treat a Shell call that never returns as fixable by this ticket; record that OS/extension hang boundary separately.

## Acceptance criteria

1. A close/session-end dispatched while any wrapped `ExplorerHost` call is active cannot call `DestroyWindow` or `ExplorerHost::destroy` until that call and its outermost scope have returned.
2. After the deferred request is observed, queued tab/group/layout/navigation/drag messages cannot mutate state or call a dead/closing host.
3. Startup `Initialize` re-entry cannot mark a host realized or install callbacks after shutdown was requested.
4. Normal close still destroys every initialized browser before parent destruction and exits the message loop.
5. Focused self-check, build, CTest, and `git diff --check` pass; live Shell-extension hang/crash stress is recorded separately if unavailable.

## Agent checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/shell_reentry_gate_check.ps1
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "shell_call_depth|deferred_shutdown|kDeferredShutdownMessage|ExplorerHost|WM_CLOSE|WM_ENDSESSION" src/app_shell/main.cpp
```

## Handoff requirements

- List every wrapped `ExplorerHost` caller and the post-call abort points.
- Record the deferred message/fallback behavior and which messages remain allowed during shutdown.
- State that no fake COM seam or background Shell work was introduced.
- Separate deterministic checks from real desktop re-entry evidence; do not call source matching a runtime proof.

## 交接區

<!-- 實作 agent 填寫, append-only -->

2026-08-30 implementation handoff:

- Added `ShellCallScope` in `src/app_shell/main.cpp`. It counts nested app-owned
  Shell/ExplorerHost calls, records `WM_CLOSE` and confirmed
  `WM_ENDSESSION(TRUE)` during a call as deferred shutdown, and posts
  `kDeferredShutdownMessage` only when the outermost call returns. A failed
  `PostMessageW` uses the post-call `begin_shutdown` fallback after checking
  that the HWND still exists.
- Wrapped callers: view-mode capture/apply, status-bar item counts,
  `destroy_explorers`, all `apply_layout` visibility/initialize/rect/destroy
  operations, Group activation/add/delete, active-pane and tab navigation,
  tab realization/drag-drop registration, history/up/refresh/view-mode/address
  navigation, pinned-folder navigation, clipboard/FileOperation setup Shell
  calls, and the message-loop accelerator translation. Every wrapped call has
  a post-scope `shutdown_deferred`/`closing_` abort before continuing.
- The main window, address bars, tab/group strips, and pinned-locations manager
  reject ordinary interaction while deferred/closing; close system-command and
  non-client close-button messages remain allowed. The outer loop also skips
  accelerator/paste preprocessing while teardown is pending. `WM_DESTROY` and
  all ExplorerHost destruction remain ordered through the existing
  save -> destroy views -> destroy parent path.
- Added `tests/release/shell_reentry_gate_check.ps1`, registered as
  `panedock_shell_reentry_gate`. It is a source/lifetime invariant check, not a
  runtime proof against a real Shell extension hang. No fake COM seam,
  background Shell work, timeout, polling loop, or process termination was
  introduced.
- Checks run: `cmake --build build` passed; the focused PowerShell check passed;
  `ctest --test-dir build -E panedock_launch_smoke --output-on-failure` passed
  9/9; `git diff --check` passed. The launch smoke remains excluded because the
  sandbox cannot write `%LOCALAPPDATA%\PaneDock` and its expected save-failure
  prompt cannot be interacted with here.
