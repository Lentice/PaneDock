# Audit: PaneDock

## Summary

PaneDock is a native Win32 file manager (C++20, LLVM-MinGW) whose right-hand panes host real `IExplorerBrowser` Shell views; the audited coverage centered on session persistence, ExplorerHost lifetime, shutdown sequencing, and tab/pane model mutations. All 30 existing tests pass and the traced flows show deliberate reentrancy and lifetime discipline (detach-before-Destroy, re-fetch-after-ShellCall, group-wide id allocation at the single caller).

- Biggest architectural concern supported by the audited coverage: None found within coverage
- Biggest correctness/stability concern supported by the audited coverage: None found within coverage

## Findings

None. No candidate satisfied all four evidence-contract items; the suspicious candidates each have an existing guard or lack a reachable failure path (see Observations and Ruled out).

## Observations

- `main.cpp:4151` dereferences `active_tab()->id` for `KeyAction::close_tab` without a null check; reachable null requires a broken model invariant (session decode enforces `is_valid`, all mutations go through `core`), and the message loop already gates on `has_active_group` (`main.cpp:4084`).
- `explorer_host.cpp:979` returns early on null `pidl` after consuming the generation without updating `completed_navigation_generation_`, so view-mode/sort calls stay `E_PENDING`; whether the Shell ever passes null here is unverified.
- `core::move_tab` (`model.cpp:394`) checks `retained_tab_id` only against the target pane, trusting the caller for group-wide uniqueness; the sole caller uses `make_unique_tab_id` (`main.cpp:2400`), which scans the whole group, so this holds today but constrains future callers.
- `session.cpp:380` rejects any `schema_version != 1` wholesale and unknown `layout_template` strings fail the whole decode to backup/default; unknown *fields* are preserved on write-back, but there is no migration step yet for a future version or template value.
- `SessionWriter::write` is `noexcept` while `core::write_session` and `serialize_session` can throw (`bad_alloc`); the only realistic trigger is allocation failure, which would call `std::terminate`.
- Shutdown deferral (`shutdown.cpp:86`) resumes on shell-call/drag gates only and does not check file-operation state; safe today because new pastes are blocked once `is_shutting_down()` (`main.cpp:2849`) and an in-progress operation diverts `close_requested` to `prompt_transfer` instead of `defer`.

## Recommended order

None.

## Coverage

- **Read**: 15 plan files + 5 reserve files + 0 evidence-overrun files
- **Flows traced**: session persist/restore (`core/session.cpp`, `app_shell/session_writer.cpp`); ExplorerHost init/navigate/destroy lifecycle (`explorer_host/explorer_host.cpp`); deferred shutdown with Shell reentrancy gates (`core/shutdown.cpp`, `main.cpp` shutdown handlers); tab move/add/close with id allocation (`core/model.cpp`, `main.cpp:2400`, `pane.cpp` tab paths); clipboard paste via `IFileOperation` (`file_operations/file_operations.cpp`)
- **Boundary states checked**: empty/missing session directory, corrupt primary with valid backup, first run with no persisted state, single-tab source pane on cross-pane move, null/unresolvable Shell locations, shutdown while paste in progress vs in setup, null `active_tab()` on close-tab key path
- **Evidence overrun**: none
- **Ruled out**: shutdown-during-paste teardown race (paste setup aborts via `file_operation_setup_aborted`, new pastes blocked by `is_shutting_down`, deferred message re-armed by `shell_call_left`); `PaneState*` instability across `groups` reallocation (vector move transfers buffers, `static_assert` on nothrow move upholds it); `TabState*` use-after-ShellCall in `pane.cpp` (pointers re-fetched after every `ShellCall`, verified at `pane.cpp:957,1140,1173,1189`)
- **Read, no finding**: `core/session.h`, `core/session.cpp`, `app_shell/session_writer.h`, `app_shell/session_writer.cpp`, `core/model.h`, `core/model.cpp`, `core/group_transition.h`, `core/group_transition.cpp`, `core/shutdown.h`, `core/shutdown.cpp`, `explorer_host/explorer_host.h`, `explorer_host/explorer_host.cpp`, `shell_core/shell_core.h`, `shell_core/shell_core.cpp`, `file_operations/file_operations.h`, `file_operations/file_operations.cpp`, `explorer_host/live_view_count.h`, `app_shell/pane.h`, `app_shell/pane.cpp` (partial), `app_shell/main.cpp` (partial)
- **Unreached**: full `main.cpp` (4258 lines, read only in the sections needed for evidence chains); `sidebar`, `pane_tab_strip`, `tab_overflow`, dialog modules (not part of selected flows); budget remaining: 3 reserve files unused
- **Verification performed**: `cmake --build build` (no work to do) and `ctest --test-dir build --output-on-failure`: 100% tests passed, 30/30
