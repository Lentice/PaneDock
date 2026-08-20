# Testing

## Automated checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The live `ctest` count is the single source of truth for how many tests exist. It is deliberately not written down here, because a hardcoded count drifts and then lies.

## Single seam: `core`

**All automated tests target `core`, and `core` alone.** This is a deliberate one-seam design, chosen over two alternatives that were considered and rejected (see below).

A good test here exercises externally observable behavior through a public boundary and says nothing about how that behavior is implemented. It states an input and an expected output. It does not assert on internal call sequences, private structure, or the identity of collaborating objects.

`core` is testable that way because it is pure computation over data:

- **Model invariants** — pane count agreeing with the layout template, exactly one active pane, exactly one active tab per pane, referential integrity after a delete or reorder.
- **Layout rectangle computation** — each of the five templates, split ratios applied, behavior at degenerate window sizes (FR-004a).
- **Session serialization** — round-trip fidelity, schema migration from an older version, graceful handling of a truncated or corrupt document.
- **Group mutations** — create, rename, duplicate, delete, reorder, asserted through resulting model state.

These tests are fast, deterministic, and run in CI without a desktop session, a Shell, or installed extensions.

## What is deliberately not automated

`explorer_host`, `shell_core` and `file_operations` have no automated tests.

Their behavior is defined by `shell32`, by whichever shell extensions are installed on the machine, and by undocumented view internals. A test double for `IExplorerBrowser` would assert our assumptions about the COM host contract rather than the contract itself — it would pass while the real integration is broken, which is worse than no test at all.

Two rejected alternatives, recorded here so they are not reopened without new evidence:

- **Wrapping `IExplorerBrowser` in an abstraction to enable faking.** Rejected: it adds an interface with exactly one real implementation and a permanent maintenance burden, buying coverage that does not correspond to real risk.
- **End-to-end UI automation (WinAppDriver / UIAutomation).** Rejected: against live Shell views it is severely flaky. The maintenance cost exceeds the signal. Reopening requires a demonstration of a stable run across at least 20 consecutive executions on two machines.

## Prototype acceptance protocol (Phase 0)

This replaces automated coverage for the COM layers. Run manually, on a real desktop, in Release. Every step records its observed result in the commissioning ticket's 交接區.

1. **Four independent panes.** Open the four-pane layout. Navigate each pane to a different local folder. Expected: all four list contents, native icons and thumbnails render.
2. **Native context menu.** Right-click a file in each pane. Expected: the standard Windows menu appears, including entries contributed by installed shell extensions.
3. **Cross-pane drag and drop.** Drag a file from pane 1 to pane 4. Expected: standard Windows move/copy behavior with the usual modifier semantics.
4. **External drag and drop.** Drag a file out to another application and in from one. Expected: both directions work.
5. **Layout churn.** Switch between two-pane and four-pane layouts 20 times. Expected: no growth in view count, focus still lands on the active pane, handle count returns to its prior level.
6. **Restore.** Close and reopen. Expected: layout and every tab's location are restored exactly.
7. **Unreachable path.** Save a Group containing a disconnected network path, then restart. Expected: UI responsive throughout, that tab shows a recoverable error, its configuration intact.
8. **Selection restoration feasibility.** Attempt to read and restore the selection in a pane. Expected: a written verdict on whether it is achievable at acceptable risk. A negative verdict is a valid result and cuts the feature.
9. **Idle resources.** Leave the app open and untouched for 10 minutes, then sample. Expected: CPU below the NFR-001 threshold, zero disk I/O, memory and handle count flat.

## Required test environments

- A clean Windows 11 x64 machine with no third-party shell extensions.
- A machine with third-party shell extensions installed (a cloud sync client and an archiver at minimum) — this is where extension-induced failures appear.
- A multi-monitor setup with two different DPI scalings.
- A machine with a mapped network drive that can be disconnected on demand.

## MVP acceptance checklist

- [ ] AC-001 four-pane stability, no unhandled COM exceptions
- [ ] AC-002 cross-pane drag and drop
- [ ] AC-002b external drag and drop
- [ ] AC-003 layout churn without view leak or focus breakage
- [ ] AC-004 layout and locations restored after restart
- [ ] AC-005 UI responsive with an unreachable path in restored state
- [ ] AC-006 idle resources meet NFR-001
- [ ] FR-001 Group create / rename / duplicate / delete / reorder
- [ ] FR-003 all five layout templates
- [ ] FR-005 tab add / close / switch in every layout
- [ ] FR-007 copy / move / delete / rename via `IFileOperation`
- [ ] FR-009 network drive, USB volume, OneDrive placeholder reachable
- [ ] FR-013 recovery from a corrupt session document
- [ ] NFR-004 correct scaling across mixed-DPI monitors
