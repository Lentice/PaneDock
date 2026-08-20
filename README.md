# PaneDock

A Windows file manager built around one idea: a **Group** is a named working context that restores an entire pane arrangement in one click.

The left side is a persistent Group sidebar. The right side is 1–4 file panes, each hosting the real Windows Shell folder view through `IExplorerBrowser` — so native icons, thumbnails, context menus, installed shell extensions, drag and drop, network drives and OneDrive placeholders all behave exactly as they do in Explorer. The file list is not reimplemented.

It combines Q-Dir's multi-pane browsing with saved, switchable working contexts, and is meant to sit open all day without consuming CPU or disk while idle.

## Status

**Phase 0 — feasibility prototype.** No application code exists yet.

The Go/No-Go gate for the whole product is PD-001: four independent `IExplorerBrowser` instances coexisting stably in one process. `docs/roadmap.md` is the authoritative phase status.

## Build

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

MSVC, C++20. MSVC rather than a MinGW toolchain because WRL and the Shell COM headers are MSVC-oriented — see `docs/development.md`.

## Layout

```
AGENTS.md      agent charter and engineering rules (CLAUDE.md imports this)
CONTEXT.md     glossary — the vocabulary used in spec, UI and tickets
docs/
  design-spec.md            product source of truth (FR / NFR / AC ids)
  tickets.md                tracker: status, dependencies, rejected directions
  tickets/PD-*.md           one self-contained ticket per outcome
  development.md            module boundaries and change workflow
  testing.md                test seam, and the manual prototype protocol
  roadmap.md                Phase 0..5
  performance-baseline.md   measured numbers and blocking thresholds
  adr/                      architecture decision records
src/
  core/                     model, layout maths, persistence — no COM, no windows.h
  app_shell/                WinMain, message loop, main window
  explorer_host/            IExplorerBrowser instances and their lifetime
  shell_core/               IShellItem, PIDL, Shell location identity
  file_operations/          IFileOperation, clipboard, drag and drop
  sidebar/                  Group list
tests/
  unit/                     one executable per test file, no framework
  release/                  release evidence gate (fail-closed, not a ctest)
```

## How work is organized

Read `AGENTS.md` first. Work is tracked as tickets in `docs/tickets.md`; each ticket in `docs/tickets/` is self-contained and quotes the constraints that bind it, so it can be picked up without prior context.

Two conventions worth knowing before editing anything:

- **Ticket status lives only in the Ticket 總覽 table** of `docs/tickets.md`. A ticket document never declares its own status.
- **`docs/tickets.md` has a 已否決的方向 section** listing rejected directions and what evidence would justify reopening each one. Read it before proposing an approach.

## Documented non-goals

Recursive pane splitting, nested Groups, a custom file-operation engine, content preview, search indexing, FTP/SFTP, folder sync, a plugin system, a built-in terminal, and cross-platform support. See `docs/design-spec.md` §3.2 for the full list and the reasoning.
