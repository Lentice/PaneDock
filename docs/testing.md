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
- **Layout rectangle computation** — each of the six templates, split ratios applied, behavior at degenerate window sizes (FR-004a).
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

## Crash recovery acceptance protocol (Phase 5, FR-013)

Run manually on a real Windows desktop in a Release build. Before changing
anything, copy `%LOCALAPPDATA%\PaneDock\session.json` and
`session.json.bak` to a safe temporary directory so the original user state
can be restored after the check. Record each warning as `PASS`, `FAIL`, or
`未驗證,需真實桌面` in PD-025's 交接區; do not infer a MessageBox result from
the source code.

1. Start PaneDock, wait for the normal window, then close it normally. Confirm
   `session.json` contains `"clean_shutdown":true`. Start it again, wait five
   seconds, and confirm the running file contains `"clean_shutdown":false`.
   Stop that process with `Stop-Process -Force`, start PaneDock again, and
   confirm the warning says it did not shut down cleanly. Dismiss the warning
   and verify all Groups, panes, tabs, and locations remain present.
2. With PaneDock closed, replace `session.json` with `not json at all` while
   leaving a known-good `session.json.bak`. Start the app and confirm the
   warning says it restored the previous good version. Dismiss it and verify
   the restored Groups and locations come from the backup.
3. While the recovered app is running, perform an operation that calls
   `save_now` (for example, add a tab), then close normally. Parse
   `session.json.bak` as JSON and confirm it is not the garbage primary and
   still contains a valid session document.
4. Replace both `session.json` and `session.json.bak` with invalid text, start
   PaneDock, and confirm the warning says it started with a default Group.
   Dismiss it and verify the default Group is usable and the process remains
   stable. Restore the saved files from the safe temporary directory.

## MVP acceptance run (Phase 5)

Run this protocol manually on a Release build. The four environment labels
may describe the same physical machine when it satisfies multiple conditions,
but the handoff must record the machine, Windows build, monitor/DPI setup,
installed shell extensions, and drive letters for every result. For each row,
record exactly `PASS`, `FAIL`, or `未驗證,需真實桌面` in PD-027's 交接區. A
`FAIL` requires a new follow-up ticket and exact reproduction steps. PD-023's
all-unverified A–D record is not historical PASS evidence; execute the
referenced steps again.

Environment labels:

- **E1 clean** — Windows 11 x64 with no third-party shell extensions.
- **E2 extensions** — a Windows machine with at least one cloud-sync client
  and one archive tool installed as shell extensions.
- **E3 mixed-DPI** — a multi-monitor machine with two different active DPI
  scales.
- **E4 network** — a machine with a mapped network drive that can be
  disconnected on demand; record the mapped drive letter, USB volume, and
  OneDrive placeholder used for namespace checks.

| Checklist item | Required environment | Operation and expected result | Record |
|---|---|---|---|
| AC-001 four-pane stability | E1 and E2 | Open Four Panes, navigate all panes to different local folders, exercise native icons/thumbnails and normal navigation. Expected: all panes remain usable with no unhandled COM exception, hang, or crash. | Machine/build, folders, extension list on E2, and observed stability. |
| AC-002 cross-pane drag and drop | E1 | In Four Panes, drag disposable files between panes on the same and different volumes. Expected: native Shell move/copy behavior and correct source/destination state. | Source/destination drive letters, operation result, and any native progress UI. |
| AC-002b external drag and drop | E1 | Drag a file from PaneDock to File Explorer/an accepting application, then drag a file back into PaneDock. Expected: both directions work through native Shell/OLE behavior. | External application, file identity, and both directions' result. |
| AC-003 layout churn without view leak or focus breakage | E1 and E2 | Repeatedly use `Ctrl+Shift+L` to switch layouts 20 times, recording 21 handle samples, live-view stdout samples, and active-pane focus after each switch. Expected: no monotonic handle/view growth, final live-view count 0 after close, and focus remains on the active pane. | Full handle series, before/after/closed live-view values, focus observations, machine/build. |
| AC-004 layout and locations restored after restart | E1 | Create multiple Groups and multiple tabs with distinct locations and layout templates, close normally, reopen, and compare every Group, pane, tab, active tab, layout, and parsing name. Expected: exact required-state restoration. | Group/tab/location matrix before and after, plus machine/build. |
| AC-005 unreachable restored path stays responsive | E4 | Save a Group containing a mapped network path, disconnect the drive, restart into that Group, and interact with another pane while the unavailable pane resolves. Expected: UI remains responsive, the unavailable tab shows its recoverable error, and its saved identity remains intact. | Drive letter/path, observed error text, responsiveness result, and any measured operator timing. |
| AC-006 idle resources meet NFR-001 | E1 | With no debugger attached, run `.\tests\release\release_evidence.ps1 -CollectMeasurements` interactively. Follow every prompt, including the 10-minute idle window and three soak runs. Expected: evidence `## Result` is PASS, idle CPU is below 0.1% average, and idle disk I/O is zero. | `docs/release-evidence.md` result/exit code, CPU/disk values, debugger state, and environment. Do not substitute visual observation. |
| FR-001 Group mutations | E1 | Use the sidebar to create, rename, duplicate, delete, move up, and move down Groups; switch among them after each mutation. Expected: names/order/active Group and persisted state remain correct. | Each mutation's observed result and final Group order. |
| FR-003 six layout templates | E1 | Exercise Single, Left / Right, Top / Bottom, Three Panes, Two over One, and Four Panes layouts; resize the window and return to each template. Expected: correct pane count/placement and no view destruction or crash. | Template-by-template pane count, visible arrangement, and machine/build. |
| FR-005 tabs in every layout | E1 | In each of the six layouts, add, switch, and close multiple tabs in each visible pane; activate an inactive tab and navigate it. Expected: tab state persists, active tab is correct, and only the visible active tab owns a live view. | Per-layout tab operations, active tab/location, and any realization issue. |
| FR-007 Shell file operations | E1 | Execute every item in the existing Shell file operations protocol A1–A7 and clipboard protocol B8–B10 (copy/move/delete/rename, conflict choices, cancel, clipboard round-trips, and re-entry during an operation). Expected: Explorer-equivalent progress/conflict behavior and no crash. | Each A/B item individually; include files/volumes and native dialog observations. |
| FR-009 network, USB, OneDrive namespace coverage | E4 | Perform the D16 OneDrive placeholder copy and D17 mapped-network/USB copy and delete from the existing protocol. Expected: native namespace semantics remain intact; record any placeholder hydration behavior. | OneDrive state, mapped drive/USB letters, operation results, and exact hydration behavior. |
| FR-013 corrupt session recovery | E1 | Follow the Crash recovery acceptance protocol above: force-stop recovery, corrupt-primary backup recovery, save-after-recovery backup protection, and both-files-corrupt default recovery. Expected: each exact warning appears, state is usable, and backup is never replaced by garbage. | Each recovery case, warning text/appearance, restored state, and session file contents. |
| NFR-004 mixed-DPI scaling | E3 | Move the window across both monitors and exercise all six layouts, sidebar, tabs, navigation bars, splitters, and dialogs at each DPI. Expected: no clipping, overlap, wrong hit target, or unscaled logical constant. | Monitor DPI values, Windows build, per-layout visual result, and any defect reproduction. |
| NFR-006 diagnostic-mode comparison | E2 | On the same file and same Shell view, run normal `.\build\PaneDock.exe` and then `.\build\PaneDock.exe --diagnostic`; right-click the same file in both runs. Expected: native menu remains, while third-party extension entries disappear or are reduced only in diagnostic mode. | Extension list, both menu item lists, title/mode, and any navigation regression. |

The release gate is closed only when the interactive AC-006 measurement run
produces `PASS`; the non-measurement script health check is not a gate result.
Do not automate keyboard or mouse input, and do not treat a skipped prompt,
headless smoke test, or source-code inspection as a PASS for any row above.

## MVP acceptance checklist

- [ ] AC-001 four-pane stability, no unhandled COM exceptions
- [ ] AC-002 cross-pane drag and drop
- [ ] AC-002b external drag and drop
- [ ] AC-003 layout churn without view leak or focus breakage
- [ ] AC-004 layout and locations restored after restart
- [ ] AC-005 UI responsive with an unreachable path in restored state
- [ ] AC-006 idle resources meet NFR-001
- [ ] FR-001 Group create / rename / duplicate / delete / reorder
- [ ] FR-003 all six layout templates
- [ ] FR-005 tab add / close / switch in every layout
- [ ] FR-007 copy / move / delete / rename via `IFileOperation`
- [ ] FR-009 network drive, USB volume, OneDrive placeholder reachable
- [ ] FR-013 recovery from a corrupt session document
- [ ] NFR-004 correct scaling across mixed-DPI monitors
