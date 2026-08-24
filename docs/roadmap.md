# Roadmap

`docs/roadmap.md` is the authoritative phase status. Ticket status lives in `docs/tickets.md`.

## Phase 0 — Feasibility prototype (Go, 2026-08-24)

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

**Verdict: Go.** Recorded in PD-011's 交接區 (`docs/tickets/PD-011-prototype-acceptance-and-go-no-go.md`). The decisive risk — stable multi-instance `IExplorerBrowser` behavior across independent navigation, native context menus with third-party shell extensions, keep-alive layout switching, and bidirectional cross-pane/external drag and drop — held up on the real interactive desktop. Selection-restoration feasibility got a preliminary written verdict (feasible, likely via public `IFolderView2`/`IShellView` APIs without undocumented `LVM_*` messages); the deep multi-scenario, multi-Windows-build validation is PD-002's job. Layout-churn handle-count verification (protocol step 5) was not executed — the user paused keyboard/mouse automation testing mid-session — and is an open item to close before Phase 1 finishes, not a Phase 0 blocker. Idle CPU/memory/handle-count got one raw reading (PD-011); the formal idle baseline and thresholds are PD-003's job.

## Phase 1 — Core model and persistence (done, 2026-08-24)

- `core` data model with its invariants
- Layout rectangle computation for all five templates
- Versioned session document with atomic write, backup and migration
- The `core` test suite that establishes the testing pattern

Done means a session document survives a round trip, a schema migration, and a corrupt-file fallback, all under test, with no COM dependency in `core`.

Delivered by PD-004 (data model), PD-005 (layout rects), and PD-006 (session persistence). All three have automated `core` test coverage (`panedock_core_model`, `panedock_core_layout`, `panedock_core_session`) plus independent re-verification recorded in each ticket's 交接區. PD-002 (selection-restoration feasibility) and PD-003 (idle resource baseline) remain open alongside Phase 1/2 — PD-002 is done (feasible but unacceptable risk outside plain filesystem folders), PD-003 is `blocked` pending a human running `tests/release/release_evidence.ps1 -CollectMeasurements` on a real interactive desktop.

## Phase 2 — Application shell (done, 2026-08-24)

- Main window, STA setup, message loop, Per-Monitor-V2 DPI
- Group sidebar with create / rename / duplicate / delete / reorder
- Layout templates applied to real panes, draggable splitters
- Active-pane indication and focus routing

Done means a user can create Groups, switch between them, and see the correct pane arrangement restored each time.

Delivered by PD-015 (app_shell wired to `core::ApplicationState` and session persistence, replacing the Phase 0 prototype's ad hoc state), PD-016 (all five layout templates, draggable splitters, DPI-scaled layout constants, F6/Shift+F6 pane-focus cycling), and PD-017 (Group sidebar: create/rename/duplicate/delete/reorder/switch, keep-alive Group switching via a new `ExplorerHost::navigate()`). All three built and passed automated checks (build, existing `core` CTest suite, boundary greps, `git diff --check`) with independent re-verification and a minimal real-desktop launch/close smoke test recorded in each ticket's 交接區. The deeper interactive acceptance items (mouse-drag splitter feel, actual DPI-switch visuals, F6 focus cycling, sidebar button clicks and owner-draw rendering) still need a human to confirm on the real desktop — this session paused keyboard/mouse automation mid-project after a privacy-adjacent stale-window screenshot incident (see PD-015's 交接區), so those are documented as pending rather than claimed.

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
