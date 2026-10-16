# Development Rules

## Product boundary

PaneDock is a file manager built around Groups and stable panes. The current pane content is the Windows Shell folder view; the approved Phase 8 direction is a PaneDock-managed list over Shell namespace items. PaneDock owns list presentation, selection and keyboard interaction. Windows Shell remains responsible for namespace identity and behavior, native context menus, and file operations. The managed list remains opt-in until PD-241's measured gate passes; PD-242 separately removes the legacy host.

Priority order when two goals conflict:

1. User experience: fast interaction, clear progressive results, correct selection and keyboard behavior
2. CPU, memory and power use, including the existing event-driven idle requirement
3. Architecture and maintainability

Compatibility and stability remain release gates. The priority order does not waive an existing NFR or turn an unmeasured result into a pass.

## Architecture rules

| Module | Owns | Must not own |
|---|---|---|
| `app_shell` | WinMain, STA init, message loop, main window, cross-pane coordination (active pane, layout rects, splitter and cross-pane tab drag), the shutdown and Shell re-entry gates | Model computation, Shell calls, persistence format, **anything that affects one pane only** |
| `app_shell::Pane` (and `PaneTabStrip`) | Everything scoped to one pane: its tab strip hit-testing, tab activation/creation/close, its tab context menu, its navigation chrome, address bar and status bar | Cross-pane state (which pane is active, cross-pane tab drag, whole-window layout) |
| `core` | Group/pane/tab model, layout rectangle computation, session serialization and migration | **Any HWND, COM type, or `windows.h` include** |
| `sidebar` | Group list rendering, selection input | Authoritative Group state (that is `core`) |
| `explorer_host` | `IExplorerBrowser` instances, site objects, view lifetime, browser events | Product decisions, persistence, layout math |
| `managed_list` (planned) | Bounded list enumeration and property work, using worker-owned COM apartments as needed; owned value batches tagged with location identity and generation | HWNDs, `PaneState` / `AppState`, UI-owned COM pointers, persistence, file operations, native-menu redesign |
| `shell_core` | `IShellItem`, PIDL, Shell location identity, change notification | Handing raw COM pointers to callers above it |
| `file_operations` | `IFileOperation`, clipboard, OLE drag and drop | Direct filesystem calls, path string assembly |

A pane-only feature is implemented in `Pane` or one of its sub-modules, not in the
coordinator. The coordinator may still route to it -- resolving *which* pane a click
landed in spans panes -- and it still supplies the shutdown and Shell re-entry gates
every pane child message passes through, but the behaviour itself belongs to the pane.
Cross-pane tab drag is the deliberate exception: the coordinator owns the drag because
the move spans two panes.

For a child window, `DefWindowProc` forwards an unhandled `WM_CONTEXTMENU` to its
parent; forwarding can continue through child ancestors (see the
[Win32 contract](https://learn.microsoft.com/en-us/windows/win32/menurc/wm-contextmenu)).
The pane window proc handles its tab strip menu (`pane.cpp`, `case WM_CONTEXTMENU`)
because the feature belongs to the pane, not because Win32 prevents forwarding it
to the main window.

The `core` boundary is load-bearing, not stylistic. It is the primary automated test seam (`docs/testing.md`); a COM type leaking into it destroys that seam.

The UI, `app_shell`, and `explorer_host` remain single-threaded STA code. The only planned concurrency boundary is the exact future source file `src/managed_list/list_worker.cpp`, for list work only. A worker owns its COM initialization and interfaces, returns copied value data, and cannot call into UI state. Its worker count, pending requests, result batches, retained rows and image caches have explicit caps in `docs/performance-baseline.md`. Cancellation invalidates the result's interest/generation; it does not promise that a provider's in-flight OS call can be interrupted. Shutdown may not block indefinitely on a provider. `tests/release/single_threaded_check.ps1` keeps the concurrency scan active everywhere else.

## COM lifetime rules

- While the legacy host is present, every `IExplorerBrowser` that was `Initialize`d must be `Destroy`ed. There is no exception and no cleanup path that covers a missed `Destroy`.
- Use `Microsoft::WRL::ComPtr` for every interface pointer. Raw `AddRef`/`Release` pairs are not acceptable in new code.
- Never destroy a parent HWND while a hosted view is alive.
- Shell APIs re-enter our message loop. Any "close the view then wait for its event" sequence must be reentrancy-safe or it will deadlock. Do not add locks to the STA to address this; use the existing Shell-call deferral gates. Worker synchronization is confined to the managed-list worker boundary and must not be used to expose partially updated UI state.

## UI language

All shipped strings are English. Examples of the expected register:

- `New Group`, `Duplicate Group`, `Delete Group`
- `Single`, `Left / Right`, `Top / Bottom`, `Three Panes`, `Four Panes`
- `This location is not available. Reconnect the drive and retry.`

No Chinese text in the binary. Documents and conversation are Traditional Chinese; the product is not.

## Build configuration

LLVM-MinGW Clang/LLD targeting `x86_64-w64-windows-gnu`, C++20, `-Wall -Wextra -Wpedantic`, warnings treated seriously. Release for every gate measurement. Ninja generator. Configure with `cmake/llvm-mingw.cmake`.

`PaneDock.exe` links the LLVM-MinGW compiler runtime statically. A deployed
copy must not require `libc++.dll` or `libunwind.dll`; the
`panedock_no_compiler_runtime_dlls` check enforces this from the PE import
table.

The project uses LLVM-MinGW because the development machine has no usable C++20 MSVC toolset. The recorded PD-012 probe confirms that mingw-w64 provides `wrl/client.h` and the Shell COM declarations, and that `IExplorerBrowser` creation, `Initialize`, `BrowseToObject`, and `Destroy` all complete successfully. `Microsoft::WRL::ComPtr` remains the only interface-pointer ownership type.

For Shell-extension troubleshooting, start the same `PaneDock.exe` process with `--diagnostic` (or `/diagnostic`), for example `.\build\PaneDock.exe --diagnostic`. This applies the per-process Microsoft-signed-only DLL policy before OLE initialization and labels the window `PaneDock — Diagnostic Mode` when the policy succeeds; it does not persist a setting or change machine-wide policy.

## Change workflow

1. Read the ticket, then the `docs/design-spec.md` clauses it quotes, then this file.
2. Read and trace the files the ticket lists. Grep every caller of any shared function you intend to change.
3. Make the smallest change that satisfies the acceptance criteria. Reuse before adding.
4. Add one focused runnable test or self-check for new non-trivial logic. If the logic is not in `core`, say in the ticket's 交接區 why it could not be, and what manual check replaces it.
5. Run the Agent checks the ticket specifies, plus `git diff --check`. Record the commands and their results.
6. Fill in the ticket's 交接區 and update the status in the Ticket 總覽 table of `docs/tickets.md`. Update any document whose described behavior changed.

Do not add a dependency, background loop, framework, or abstraction without a measured need.
