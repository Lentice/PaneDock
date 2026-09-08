# Development Rules

## Product boundary

PaneDock is a file manager shell around Windows' own folder views. It is not a file manager implementation. Every behavior a user sees inside a pane belongs to Windows; every behavior outside a pane belongs to us.

Priority order when two goals conflict:

1. Idle resource use — it sits open all day
2. Stability of the Shell host — a crash costs the user their arrangement
3. Restore correctness for required state
4. Keyboard and mouse efficiency
5. Visual polish

## Architecture rules

| Module | Owns | Must not own |
|---|---|---|
| `app_shell` | WinMain, STA init, message loop, main window, command routing to the active pane | Model computation, Shell calls, persistence format |
| `core` | Group/pane/tab model, layout rectangle computation, session serialization and migration | **Any HWND, COM type, or `windows.h` include** |
| `sidebar` | Group list rendering, selection input | Authoritative Group state (that is `core`) |
| `explorer_host` | `IExplorerBrowser` instances, site objects, view lifetime, browser events | Product decisions, persistence, layout math |
| `shell_core` | `IShellItem`, PIDL, Shell location identity, change notification | Handing raw COM pointers to callers above it |
| `file_operations` | `IFileOperation`, clipboard, OLE drag and drop | Direct filesystem calls, path string assembly |

The `core` boundary is load-bearing, not stylistic. It is the primary automated test seam (`docs/testing.md`); a COM type leaking into it destroys that seam.

## COM lifetime rules

- Every `IExplorerBrowser` that was `Initialize`d must be `Destroy`ed. There is no exception and no cleanup path that covers a missed `Destroy`.
- Use `Microsoft::WRL::ComPtr` for every interface pointer. Raw `AddRef`/`Release` pairs are not acceptable in new code.
- Never destroy a parent HWND while a hosted view is alive.
- Shell APIs re-enter our message loop. Any host-side lock, and any "close the view then wait for its event" sequence, must be reentrancy-safe or it will deadlock.

## UI language

All shipped strings are English. Examples of the expected register:

- `New Group`, `Duplicate Group`, `Delete Group`
- `Single`, `Left / Right`, `Top / Bottom`, `Three Panes`, `Four Panes`
- `This location is not available. Reconnect the drive and retry.`

No Chinese text in the binary. Documents and conversation are Traditional Chinese; the product is not.

## Build configuration

LLVM-MinGW Clang/LLD targeting `x86_64-w64-windows-gnu`, C++20, `-Wall -Wextra -Wpedantic`, warnings treated seriously. Release for every gate measurement. Ninja generator. Configure with `cmake/llvm-mingw.cmake`.

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
