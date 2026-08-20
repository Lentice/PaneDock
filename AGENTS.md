# PaneDock Agent Instructions

## Project intent

PaneDock is a Windows desktop file manager built around one abstraction: a **Group** is a named, saved working context that restores an entire pane arrangement in one click. The right side hosts 1–4 file panes; each realized pane hosts the real Windows Shell folder view through `IExplorerBrowser`, so native icons, thumbnails, context menus, installed shell extensions, drag and drop, and OneDrive placeholders come from Windows itself. The file list is never reimplemented.

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
- **Keep `src/core` free of HWND, COM and `windows.h`.** It is the only automated test seam in this project (`docs/testing.md`); every type that leaks into it costs that seam.
- Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.
- Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups — that is both the crash surface and a CPU/disk spike from simultaneous folder enumerations.
- Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation. This is what keeps memory bounded; see `docs/performance-baseline.md`.
- Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.
- File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.
- Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.
- Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.
- All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.
- **Never persist a PIDL or a COM pointer.** Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.
- No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.
- New non-trivial logic needs one focused runnable test or self-check.

## Ticket authoring rules

Tickets live in `docs/tickets/` and are tracked in `docs/tickets.md`.

- Split work agile-style: one ticket delivers one outcome, sized half a day to two days. If it grows past that, split it before writing it.
- Write each ticket to be self-contained, so a low-capability agent can pick it up and finish it without prior context or further questions.
- Quote the binding constraints into the ticket itself: the relevant `docs/design-spec.md` clauses, the `docs/development.md` rules, and the applicable rules from this file. Do not rely on the agent finding them.
- List the exact files to read and trace, the concrete scope (signatures, constants, call sites), the non-goals, the acceptance criteria, and the runnable Agent checks.
- When a ticket overrides an earlier decision, state the override inside the new ticket. Never edit a completed ticket's document — that rule protects its scope, decisions and 交接區, which are the historical record. It does not protect tracker metadata that has since become false.
- **Status and dependencies live only in the Ticket 總覽 table of `docs/tickets.md`.** A ticket document must not declare its own status. The same status stored in two places will diverge.
- Before writing a new ticket, read the 「已否決的方向」 section of `docs/tickets.md`. Reopening a rejected direction is allowed, but the new ticket must state the override and present new evidence.
- Anything a later session needs must live in the repository, not in a scratchpad handoff. Candidate tickets and rejected directions go in `docs/tickets.md`; measured numbers go in `docs/performance-baseline.md` or the ticket's 交接區.
- Do not reserve ticket numbers in advance. Take the highest number in the Ticket 總覽 table and add one at the moment you write the file, and confirm `docs/tickets/` has no file with that number: another agent may be authoring tickets in this repository at the same time.

## Current baseline

Greenfield. No source code exists yet. `docs/roadmap.md` is the authoritative phase status.

- Phase 0 (feasibility prototype) is the current phase and the Go/No-Go gate for the whole product.
- The decisive technical risk is stable multi-instance `IExplorerBrowser` integration. Two panes cannot expose it; the prototype is four panes for that reason.
- Selection restoration has no public Shell API and is expected to require undocumented `LVM_*` messages. It is best-effort and is the first feature cut if the prototype shows it unstable.
- Memory figures in `docs/performance-baseline.md` are planning estimates, not measurements. Every one of them is marked "Not measured" until the prototype produces a number.
- Pre-prototype: no NFR gate has been measured, so `docs/release-evidence.md` does not exist yet and the release gate fails closed by definition.

## Validation

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Safety boundaries

- Do not push branches, publish releases, or modify anything outside this repository without explicit approval.
- Do not bundle schema migrations or destructive cleanup into an unrelated change.
- Keep changes scoped to the ticket, and update the affected documents when behavior changes.
