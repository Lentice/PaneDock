# PaneDock Agent Instructions

## Project intent

PaneDock is a Windows desktop file manager built around one abstraction: a **Group** is a named, saved working context that restores an entire pane arrangement in one click. The right side hosts 1–4 file panes. Each pane shows a PaneDock-managed list that keeps Shell namespace identity, native file operations and the native context-menu experience. PD-241's gate was waived and PD-242 removed the legacy `IExplorerBrowser` host from the code; `docs/tickets.md` tracks what PD-242 still owes.

Platform baseline is Windows 10 22H2 / Windows 11 x64, C++20, native Win32, Shell COM via `Microsoft::WRL::ComPtr`. The design specification in `docs/design-spec.md` is the product source of truth. Do not add features that are listed as out of scope there.

## Language rules

- Conversation with the user is in Traditional Chinese.
- Documents may be written in Traditional Chinese.
- **App UI text must be English.** No Chinese strings ship in the binary.
- Code, identifiers, test names and diagnostic event names are English.

## Engineering rules

- Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.
- Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.
- Reach for the standard library and Win32 before adding a dependency.
- **Keep `src/core` free of HWND, COM and `windows.h`.** It is the primary pure-model test seam; other public units that can be driven without a live Shell are tested too (`docs/testing.md`).
- Never destroy a parent HWND while a child list is alive; on shutdown tear down every pane before the message loop exits.
- Group switching keeps the live pane HWNDs and re-navigates them, rather than destroying and recreating them — recreation is both a crash surface and a CPU/disk spike from simultaneous folder enumerations.
- Only the visible pane's active tab enumerates; inactive tabs remain data (plus a bounded snapshot) and are realized on activation. Bound retained rows, queued batches, metadata and image caches across panes and tabs; the budgets are in `docs/performance-baseline.md`. Legacy `IExplorerBrowser` memory measurements are not managed-list evidence.
- **A `pane_index` parameter is a signal that the function belongs to that pane.** Before adding one, ask whether the function can simply be a member of `app_shell::Pane`. It can when all three hold: it touches exactly one pane; it needs no coordinator service (`ShellCallScope`, session save scheduling, shutdown gates, cross-pane layout); and it is not shared state that is a singleton by nature. When a coordinator service is genuinely needed, keep it a free function but narrow the signature to `Pane&` rather than the whole `AppState&`. The coordinator owns what spans panes — cross-pane tab drag, splitter drag, active-pane selection, layout rects, whole-window painting; `Pane` owns what is its own.
- Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.
- File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.
- Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side sequencing and shutdown must be reentrancy-safe; do not add locks to the STA.
- **The UI/Shell host hazard is reentrancy, never concurrency.** The application UI, `app_shell`, `explorer_host`, existing Shell COM objects and their callbacks remain on the single STA: no locks, atomics, `volatile`, or worker access to HWNDs, `PaneState`, `AppState`, or UI-owned COM pointers. The only planned worker exception is the exact future file `src/managed_list/list_worker.cpp`, for bounded managed-list enumeration and property work. It exchanges owned value data with the UI, tags results with location identity and generation so stale work can be discarded, and owns any COM apartment and COM pointers it creates itself. It never sends Shell COM objects or PIDLs to the UI. Bound workers, pending requests and result batches; cancellation drops interest but need not interrupt an OS call already in progress. Shutdown must not wait indefinitely for a stuck provider; if bounded safe shutdown cannot be demonstrated, the managed-list gate fails. On the STA, a call which pumps our loop (a Shell call, `TrackPopupMenu`, `MessageBoxW`) can dispatch the next message nested inside the current one: re-verify identity (`active()`, `pane_state()`, `is_shutting_down()`) after every pump point, and never hold a reference or pointer across one. `ShellCallScope` / `shell_call_depth` defers re-entrant work; a lock does not. The other exceptions are cross-process coordination for single-instance relay and the reference count of a COM object handed to `shell32` (`src/com_ref_counted.h`), which third-party code releases at times we do not control. `tests/release/single_threaded_check.ps1` must keep banning concurrency machinery everywhere except that exact future worker file and the COM reference counter.
- Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.
- All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.
- **Never persist a PIDL or a COM pointer.** Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.
- **Every persisted config/setting file must be designed for forward extensibility.** It carries an explicit schema version from its first version. A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back. A schema change is additive (new optional fields, new migration step) rather than a destructive reinterpretation of an existing field's meaning. This applies to anything meant to survive a restart; it does not apply to a file a ticket has explicitly designed as disposable (e.g. a prototype-only persistence format that a later ticket is known to replace).
- No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.
- New non-trivial logic needs one focused runnable test or self-check.

## Ticket authoring rules

Tickets live in `docs/tickets/` and are tracked in `docs/tickets.md`.

- Split work agile-style: one ticket delivers one outcome, sized half a day to two days. If it grows past that, split it before writing it.
- Write each ticket to be self-contained, so a low-capability agent can pick it up and finish it without prior context or further questions.
- Quote the binding constraints into the ticket itself: the relevant `docs/design-spec.md` clauses, the `docs/development.md` rules, and the applicable rules from this file. Do not rely on the agent finding them.
- List the exact files to read and trace, the concrete scope (signatures, constants, call sites), the non-goals, the acceptance criteria, and the runnable Agent checks.
- When a ticket overrides an earlier decision, state the override inside the new ticket. Never edit a completed ticket's document — that rule protects its scope, decisions and 交接區, which are the historical record. It does not protect tracker metadata that has since become false. The rule is why the narrowing below applies only to new tickets: the existing 交接區 sections stay exactly as written, mixed content and all.
- **Status and dependencies live only in the Ticket 總覽 table of `docs/tickets.md`.** A ticket document must not declare its own status. The same status stored in two places will diverge.
- Before writing a new ticket, read the 「已否決的方向」 section of `docs/tickets.md`. Reopening a rejected direction is allowed, but the new ticket must state the override and present new evidence.
- Anything a later session needs must live in the repository, not in a scratchpad handoff. Candidate tickets and rejected directions go in `docs/tickets.md`; measured numbers go in `docs/performance-baseline.md` or the ticket's 交接區.
- A 交接區 holds exactly two things: **unresolved risks** (what is still unmeasured, unverified, or assumed) and **conclusions a later session must not re-derive** (a constant verified the hard way, a cause already ruled out). Nothing else. It is not a retrospective, and it is the last resort — prefer the destination that the reader will actually be standing in:
  - a constant or invariant that must not be "corrected" from memory → a comment at that line in the code
  - a measured number → `docs/performance-baseline.md`
  - a direction that was tried and rejected → the 「已否決的方向」 section of `docs/tickets.md`
  - a superseded decision → the Overrides section of the ticket that supersedes it
- Do not put these in a 交接區, in any ticket: a list of the files or `file.cpp:123` lines that changed (git holds it, and the line numbers rot the next time anyone edits above them), a restatement of the diff or of the ticket's own scope, process notes and comparison scratch work whose purpose ended when the ticket landed, or an unfilled "（實作者填寫：…）" template. In this repository all 213 tickets grew a 交接區 and most of what accumulated there was one of those four.
- Do not reserve ticket numbers in advance. Take the highest number in the Ticket 總覽 table and add one at the moment you write the file, and confirm `docs/tickets/` has no file with that number: another agent may be authoring tickets in this repository at the same time.

## Current baseline

The application and its automated test suite exist; the managed list is the only pane view. `docs/roadmap.md` is the authoritative phase status. The Phase 8 migration started with the PD-222 product contract; PD-241's gate was waived (2026-10-21) and PD-242 removed the legacy view. Historical NFR-001 evidence and legacy-view measurements remain in `docs/release-evidence.md` and `docs/performance-baseline.md`; none is evidence that the managed list passed. Managed-list targets are explicitly marked unmeasured until tested on the required desktop, Shell namespace and OneDrive environments.

## Validation

The project uses LLVM-MinGW Clang/LLD targeting `x86_64-w64-windows-gnu`. **MSVC is not used** — do not look for `cl.exe`, and never report a missing MSVC as a blocker. PD-012 records the compatibility evidence; `docs/development.md` records the rationale.

Validation needs `clang++`, `llvm-rc`, `cmake` and `ninja` on `PATH`. On the current development machine they live at `E:\Dev\LLVM-MinGW\bin` and `E:\Dev\Ninja`.

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Windows GUI verification (Used for Codex only)

- Read the `computer-use` skill and bundled guidance/API/confirmation docs before UI automation. Initialize `@oai/sky` in `node_repl`; launch with `{app: "<returned app id or explicit .exe path>"}`, then select exactly one target from fresh `list_apps()`/`list_windows()` results.
- Treat every `get_window_state()` as a new snapshot: perform one action, refresh immediately, and never reuse stale coordinates, indexes, or screenshot IDs. On `failed to activate captured window`, fresh-list/reselect, call `activate_window`, then capture fresh state and retry once; if activation still fails, stop GUI verification for that task.
- Use the `sky` API names exactly: `element_index` for accessibility clicks, `press_key` for chords, and `drag` with `from_x/from_y/to_x/to_y` plus the current `screenshotId`; do not invent `element`, `keypress`, or `start/end` arguments.
- Native Shell menus may exist only in the screenshot; verify that screenshot and right-click visibly empty list space. For verbs that open another app, re-list windows and close only the newly opened test window. Do not automate terminal apps or use verb tests for file changes.
- For a stdout-captured PaneDock test, start `build\PaneDock.exe` exactly once with `Start-Process` and redirected output, and keep its PID. Then use fresh `sky.list_windows()` / `sky.list_apps()` results to select that launched window. `sky.launch_app` starts an executable; it is not a foreground-activation command. Never call it for the same executable after a stdout-captured launch: if the first instance is busy, the second launch can remain as another `PaneDock.exe` and show “already running but not responding.”
- If the launched window is absent from a fresh window list, inspect that same PID's responsiveness and captured output, then do one bounded wait and re-list. Use `sky.activate_window` only with a currently listed window object. If the window remains unavailable or a single-instance warning appears, stop GUI verification and report the process IDs and evidence; do not launch another copy to recover.
- Close only the known test instance through its normal window close path or `CloseMainWindow()`, then wait for it to exit. If it does not respond, stop and report; never use `Stop-Process`, `taskkill`, or another forced termination path.
- If `node_repl` reports `failed to start Node runtime` or `os error 3`, reset and retry once; after a second failure, stop UI automation and use non-UI checks.
- `panedock_launch_smoke` tests the real `build\PaneDock.exe`. A restricted `%LOCALAPPDATA%\PaneDock` save failure can leave its intentional `MessageBoxW` open; verify with writable session storage before treating a timeout as a shutdown bug, and let the normal shutdown path finish rather than force-killing it.

## Safety boundaries

- Do not push branches, publish releases, or modify anything outside this repository without explicit approval.
- Do not bundle schema migrations or destructive cleanup into an unrelated change.
- Keep changes scoped to the ticket, and update the affected documents when behavior changes.
