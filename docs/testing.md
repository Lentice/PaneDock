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

## Shell file operations acceptance protocol (Phase 4)

Run manually on a real Windows desktop in a Release build. Use the four-pane layout, disposable test files, and at least two different volumes so both same-volume move and cross-volume copy semantics are exercised. For every numbered item, record `PASS`, `FAIL`, or `未驗證,需真實桌面` in PD-023's 交接區; a `FAIL` must include exact reproduction steps and its follow-up ticket.

### A. File operations (FR-007)

1. In one pane, use the context menu to copy a file and paste it into another pane. Expected: native progress UI appears and the resulting copy is correct.
2. Repeat with Cut/Paste between folders on the same volume. Expected: native move behavior and the correct final source/destination state.
3. Copy a large file or a large batch. Expected: native progress UI appears and can cancel the operation without damaging the source.
4. Paste into a folder containing a file with the same name. Exercise Replace, Skip, and Keep both. Expected: native conflict UI appears and each choice produces the corresponding result.
5. Delete a disposable file to the Recycle Bin, then permanently delete another with `Shift+Delete`. Expected: native confirmations and Windows File Explorer-equivalent behavior.
6. Rename a file in place with `F2`; cancel once with `Esc`, then attempt a duplicate name. Expected: inline rename works, cancellation preserves the old name, and the duplicate produces native conflict feedback.
7. While a file operation is running, switch Group, switch tab, and drag a splitter. Expected: PaneDock remains responsive and does not crash during Shell re-entry.

### B. Clipboard (FR-007)

8. Exercise `Ctrl+C`, `Ctrl+X`, and `Ctrl+V` inside PaneDock panes. Expected: all reach the active Shell view and PD-021 shortcuts do not intercept them.
9. Copy from PaneDock and paste into Windows File Explorer, then copy from File Explorer and paste into PaneDock. Expected: both directions work.
10. Copy a file from PaneDock and paste it into an application that accepts files, such as an email or chat compose window. Expected: the receiving application obtains the Shell file data object.

### C. Drag and drop (FR-008)

11. Drag from pane A to pane B on the same volume. Expected: the default operation is move.
12. Drag from pane A to pane B across different volumes. Expected: the default operation is copy.
13. Drag from PaneDock to an external File Explorer window, then from File Explorer into PaneDock. Expected: both directions work.
14. Drop onto a non-active pane. Record whether it accepts the drop without first becoming active and verify the destination is the pane under the pointer; either focus behavior is acceptable, but the observed behavior must be recorded.
15. During a drag, move the pointer outside PaneDock and back, then repeat and cancel with `Esc`. Expected: no stuck drag state remains.

### D. Namespace coverage sample (FR-009)

16. Copy once inside a OneDrive placeholder folder. Expected: placeholder semantics remain intact without an unexpected forced local download, or record the exact observed hydration behavior.
17. On both a mapped network drive and a USB volume, perform one copy and one delete. Expected: native Shell behavior completes correctly for each location type.

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
