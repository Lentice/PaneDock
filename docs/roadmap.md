# Roadmap

`docs/roadmap.md` is the authoritative phase status. Ticket status lives in `docs/tickets.md`.

## Phase 0 — Feasibility prototype (current)

The Go/No-Go gate for the entire product. Four panes, not two: the risk being tested is multi-instance `IExplorerBrowser` behavior, which two panes cannot expose.

- Host one `IExplorerBrowser` per pane in a four-pane layout
- Independent navigation, native thumbnails, native context menus
- Cross-pane drag and drop
- Layout churn without view leaks or focus breakage
- Location persistence and restore across restart
- Responsiveness with an unreachable network path
- A written verdict on selection-restoration feasibility
- Measured idle CPU, memory and handle count

Done means every step of the prototype acceptance protocol in `docs/testing.md` has a recorded result, and the Go/No-Go decision is written into the commissioning ticket's 交接區. A No-Go outcome is a legitimate result and redirects to the `IShellFolder` fallback recorded in `docs/design-spec.md` §9.1.

## Phase 1 — Core model and persistence

- `core` data model with its invariants
- Layout rectangle computation for all five templates
- Versioned session document with atomic write, backup and migration
- The `core` test suite that establishes the testing pattern

Done means a session document survives a round trip, a schema migration, and a corrupt-file fallback, all under test, with no COM dependency in `core`.

## Phase 2 — Application shell

- Main window, STA setup, message loop, Per-Monitor-V2 DPI
- Group sidebar with create / rename / duplicate / delete / reorder
- Layout templates applied to real panes, draggable splitters
- Active-pane indication and focus routing

Done means a user can create Groups, switch between them, and see the correct pane arrangement restored each time.

## Phase 3 — Tabs and navigation

- Tabs per pane, with realize-on-activation
- Per-tab address field, back, forward, parent
- Per-tab navigation history
- Keyboard shortcuts routed to the active pane

Done means a Group with multiple tabs per pane restores its full working set, and only the visible active tab holds a live view.

## Phase 4 — Shell operations

- `IFileOperation` wiring for copy, move, delete, rename
- Clipboard via Shell `IDataObject`
- Cross-pane and external drag and drop
- Unresolvable-location error state and retry

Done means every file operation in the MVP checklist behaves as it does in Explorer, including progress and conflict dialogs.

## Phase 5 — Release gate

- Diagnostic mode suppressing third-party shell extensions (NFR-006)
- Crash recovery path
- Full MVP acceptance checklist executed on every required environment
- `docs/performance-baseline.md` estimates replaced by measurements
- `tests/release/release_evidence.ps1` producing a PASS

Done means the release evidence document reports PASS rather than INCOMPLETE. Until every blocking NFR-001 metric is measured, the gate fails closed by design.
