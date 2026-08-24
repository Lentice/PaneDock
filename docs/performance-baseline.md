# Performance Baseline

Rows that still read **Not measured** are estimates or unobserved planning context; measured rows cite the ticket and conditions that produced their values. No unmeasured row may be treated as evidence.

| Metric | Target | Blocking threshold | Result | Environment / notes |
|---|---|---|---|---|
| Idle CPU, 10 min sample | 0% | avg ≥ 0.1% fails | Not measured (single raw reading below) | NFR-001 blocking. PD-003 added the formal idle-window delta measurement, but it was not run without a real interactive desktop. PD-011 2026-08-24 single sample: process cumulative CPU time 1.16 s over an 11.5 min elapsed window (launch to sample, includes startup/navigation work, not an idle-only delta) — informational only, not a pass/fail measurement against the threshold. |
| Idle disk I/O, 10 min sample | zero bytes | any I/O fails | Not measured | NFR-001 blocking. PD-003 added `GetProcessIoCounters` transfer-byte delta measurement; the ten-minute interactive sample remains pending. |
| Resident memory, 1 pane, local folder | — | — | Not measured | Single layout and realize-on-activation now exist; a valid value still requires a real interactive desktop and operator-confirmed local-folder settling, which is outside this tooling ticket. |
| Resident memory, 4 panes, local folders | — | — | 54.3 MB WorkingSet64 (single reading) | PD-011 2026-08-24: PaneDock idle after 11.5 min, 4 panes on default local-folder paths, HandleCount 553. Raw process-level reading, not PD-003's formal baseline. |
| Resident memory, 4 panes, thumbnails + OneDrive + network | — | — | Not measured | PD-003 measurement requires the named resources and Shell interaction on a real desktop; no reading was fabricated. |
| Handle count, idle after 20 layout switches | flat | monotonic growth fails | Not measured | PD-003 script records 21 handle samples around 20 human `Ctrl+Shift+L` actions; execution remains pending because this session did not automate keyboard input. |
| Live view count after 20 layout switches | returns to baseline | any residual view fails | Not measured | PD-026 adds stdout samples after layout and destroy; a 20-switch run still requires a real interactive desktop and remains part of `-CollectMeasurements`. |
| Group switch latency, all locations local | — | — | Not measured | Measuring this requires product timing instrumentation, outside PD-026; no blocking threshold is defined. |
| Group switch latency, one unreachable network path | UI never blocks | any UI block fails | Not measured | AC-005 is recorded as an operator responsiveness answer by the release script; no timing instrumentation is added in PD-026. |
| Tab realize latency on activation | — | — | Not measured | Measuring this requires product timing instrumentation, outside PD-026; no blocking threshold is defined. |
| Cold start to first painted pane | — | — | Not measured | Requires visible-paint instrumentation, outside PD-026. |
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
