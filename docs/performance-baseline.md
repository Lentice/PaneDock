# Performance Baseline

Every row below is an **estimate for planning**, not a measurement. Estimates are replaced by measured values as tickets produce them; the ticket that produced a number is cited in the notes column. A row that still reads "Not measured" has never been observed on real hardware, and no claim may be made about it.

| Metric | Target | Blocking threshold | Result | Environment / notes |
|---|---|---|---|---|
| Idle CPU, 10 min sample | 0% | avg ≥ 0.1% fails | Not measured (single raw reading below) | NFR-001 blocking. PD-003 added the formal idle-window delta measurement, but it was not run without a real interactive desktop. PD-011 2026-08-24 single sample: process cumulative CPU time 1.16 s over an 11.5 min elapsed window (launch to sample, includes startup/navigation work, not an idle-only delta) — informational only, not a pass/fail measurement against the threshold. |
| Idle disk I/O, 10 min sample | zero bytes | any I/O fails | Not measured | NFR-001 blocking. PD-003 added `GetProcessIoCounters` transfer-byte delta measurement; the ten-minute interactive sample remains pending. |
| Resident memory, 1 pane, local folder | — | — | Not measured | PD-003: the Phase 0 prototype exposes only two- and four-pane layouts and keeps hidden views live, so it cannot honestly produce a one-live-pane reading. |
| Resident memory, 4 panes, local folders | — | — | 54.3 MB WorkingSet64 (single reading) | PD-011 2026-08-24: PaneDock idle after 11.5 min, 4 panes on default local-folder paths, HandleCount 553. Raw process-level reading, not PD-003's formal baseline. |
| Resident memory, 4 panes, thumbnails + OneDrive + network | — | — | Not measured | PD-003 measurement requires the named resources and Shell interaction on a real desktop; no reading was fabricated. |
| Handle count, idle after 20 layout switches | flat | monotonic growth fails | Not measured | PD-003 script records 21 handle samples around 20 human `Ctrl+Shift+L` actions; execution remains pending because this session did not automate keyboard input. |
| Live view count after 20 layout switches | returns to baseline | any residual view fails | Not measured | PD-003: the prototype has no runtime diagnostic surface for live-view count; adding product diagnostics was outside this measurement-only ticket. |
| Group switch latency, all locations local | — | — | Not measured | Perceived-instant is the goal; no threshold set until measured. |
| Group switch latency, one unreachable network path | UI never blocks | any UI block fails | Not measured | NFR-003 / AC-005 blocking. PD-001 step 7. |
| Tab realize latency on activation | — | — | Not measured | Governs whether realize-on-activation is perceptible; see the rejected direction on live-per-tab views. |
| Cold start to first painted pane | — | — | Not measured | — |
| Thumbnail pipeline memory contribution | — | — | Not measured | PD-003 script computes the WorkingSet64 difference between operator-prepared thumbnail and text-only folders; real-desktop execution remains pending. |

## Release evidence contract

`tests/release/release_evidence.ps1` regenerates `docs/release-evidence.md`. The contract is fail-closed:

- An unmeasured blocking metric produces **INCOMPLETE** and exit code 2. Absence of evidence is never treated as absence of a problem.
- A measured threshold failure, a build failure, or a test failure produces exit code 1.
- A skipped test is not evidence. Skipped `ctest` results are reported as INCOMPLETE, not as a pass.
- Process totals and estimates are context only and never satisfy a gate.

INCOMPLETE is therefore the correct and expected result today: no blocking metric has been measured. `docs/release-evidence.md` does not exist until the script has been run.

## Why the language runtime is not on this table

An earlier selection review reasoned about memory by comparing language runtimes, and reached a wrong conclusion (recorded in `docs/tickets.md` §計畫決策紀錄, 2026-08-20). The correction, and the reason no runtime row appears above:

In the four-pane configuration the native host process is roughly 5–20 MB while the four Shell views plus thumbnails plus in-process third-party extensions are 150–500 MB. The runtime is about 5–8% of the total. Choosing a language to save runtime memory buys a rounding error and pays for it in integration risk.

The three decisions that actually bound memory are architectural and are stated in `docs/design-spec.md` §NFR-002:

1. Only the visible pane's active tab holds a live `IExplorerBrowser`.
2. The thumbnail pipeline has explicit cache and size caps.
3. Third-party shell extensions load lazily.

Any future optimization proposal must show a measured number from this table before it is written as a ticket. Reasoning from deployment size, from runtime baselines, or from another project's figures is not evidence here.
