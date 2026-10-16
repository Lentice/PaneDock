# Performance Baseline

Rows that still read **Not measured** are estimates or unobserved planning context; measured rows cite the ticket and conditions that produced their values. No unmeasured row may be treated as evidence.

| Metric | Target | Blocking threshold | Result | Environment / notes |
|---|---|---|---|---|
| Idle CPU, 10 min sample | 0% | avg ≥ 0.1% fails | **PASS** — 0.004948% avg | PD-003 2026-08-29: 600.02 s window, 4 panes on local folders (`D:\Documents\Desktop\screenGif`, `D:\downloads`), untouched. `Process.TotalProcessorTime` delta / elapsed / logical processors. See `docs/release-evidence.md`. |
| Idle disk I/O, 10 min sample | zero bytes | any I/O fails | **FAIL** — 307294 bytes (PD-003 historic reading) | PD-176 2026-09-03 controlled repeats, same `GetProcessIoCounters` transfer-byte delta: normal Single/Three/Four = 0/48/0 bytes; `--diagnostic` Single/Three/Four = 0/0/0 bytes. The intermittent normal-mode-only signal, together with the Microsoft-signed-only control, attributes the activity to third-party Shell extension activity; no PaneDock idle loop was found. The PD-003 307294-byte release-gate failure remains recorded. |
| Resident memory, 1 pane, local folder | — | — | 62,881,792 bytes (~59.97 MiB), 678 handles | PD-003 2026-08-29: Single layout, `D:\Documents\Desktop\screenGif`, 3 s settle. |
| Resident memory, 4 panes, local folders | — | — | 72,822,784 bytes (~69.45 MiB), 971 handles | PD-003 2026-08-29: Four Panes, `D:\Documents\Desktop\screenGif` / `D:\downloads` (x2 each), 3 s settle. Supersedes the PD-011 2026-08-24 single reading (54.3 MB) as the formal baseline. |
| Resident memory, 4 panes, thumbnails + OneDrive + network | — | — | 72,933,376 bytes (~69.56 MiB), 971 handles | PD-003 2026-08-29: panes on `D:\Documents\Desktop\screenGif` (thumbnails), `D:\OneDrive - via.com.tw\附件`, `\\vianextfs06\Tmp\Lentice\test`, `D:\downloads`; 8 s settle. |
| Handle count, idle after 20 layout switches | flat | monotonic growth fails | **PASS** — 971 → 894 across 21 samples, not monotonic | PD-003 2026-08-29: 20 automated Single/Four-Panes layout switches via direct `BM_CLICK` to the layout buttons (not the human `Ctrl+Shift+L` action the original script design assumed). Oscillates between two plateaus (~735 / ~890) with a small +6 rise in the first 3 switches then flat; no unbounded growth. |
| Live view count after 20 layout switches | returns to baseline | any residual view fails | Not measured | This build does not emit `panedock.live_view_count=` diagnostic stdout, so PD-003's automated run captured nothing to parse. |
| Group switch latency, all locations local | — | — | Not measured | Measuring this requires product timing instrumentation, outside PD-026; no blocking threshold is defined. |
| Group switch latency, one unreachable network path | UI never blocks | any UI block fails | Not exercised | PD-003 2026-08-29 automated run only did direct address-bar navigation to reachable paths; no unreachable-path Group restore was tested. No timing instrumentation exists (PD-026 scope). |
| Tab realize latency on activation | — | — | Not measured | Measuring this requires product timing instrumentation, outside PD-026; no blocking threshold is defined. |
| Cold start to first painted pane | — | — | Not measured | Requires visible-paint instrumentation, outside PD-026. |
| Thumbnail pipeline memory contribution | — | — | 0 bytes (not a reliable reading) | PD-003 2026-08-29: WorkingSet64(thumbnail folder) − WorkingSet64(text-only folder), but both folders had already been visited earlier in the same run, so thumbnails were pre-cached before this comparison. Not usable as evidence for a thumbnail-cache ticket; a fresh-process comparison is needed. |

## Managed-list acceptance targets (PD-222; all not measured)

These are release acceptance budgets for the managed-list path, not observations. None has been measured because the managed-list implementation is not present. Do not overwrite the legacy measurements above or use them as managed-list evidence. The legacy path remains the normal default until PD-241 records a complete passing gate; PD-242 removes it separately.

| Metric | Acceptance target | Measurement | Result at PD-222 |
|---|---|---|---|
| Pane / tab / Group command acknowledgement | p95 ≤ 100 ms, including while another pane is loading a slow source | QPC from command dispatch to UI acknowledgement; at least 30 samples per scenario, report all samples, p50 and p95 | Not measured |
| Local first operable row, cold process | p95 ≤ 250 ms | Fresh process and first visit to a mixed 1,000-item local directory; QPC request-to-first-operable-row; ≥30 runs | Not measured |
| Local first operable row, warm | p95 ≤ 100 ms | Repeat after one completed enumeration under the same process; QPC; ≥30 runs | Not measured |
| Slow / disconnected location UI response | Other-pane command acknowledgement p95 ≤ 100 ms; show loading or recoverable error. First row has no fixed SLA. | Four Panes with one slow/disconnected network source; ≥30 other-pane actions while pending; record source state and all timings | Not measured |
| Process private bytes | ≤ 256 MiB after 2 minutes settled and peak during enumeration, with Four Panes: one 100,000-item local location and three 1,000-item locations, mixed file types and images enabled | `Get-Process` `PrivateMemorySize64` samples once per second; record settled and peak, new process for each run | Not measured |
| Managed row/property snapshots | ≤ 128 MiB across visible and inactive tabs; inactive tabs count toward the limit | Managed-list owned-byte counters or equivalent allocation accounting, recorded beside process private bytes | Not measured |
| Icon/thumbnail cache | ≤ 96 MiB | Cache-owned byte counter while exercising the media/icon-heavy sample | Not measured |
| Queued result batches | ≤ 16 MiB retained | Queue-owned byte counter while the large-folder and slow-source samples are active | Not measured |
| Worker capacity | ≤ 4 active operations and ≤ 16 pending requests | Record high-water marks during four-pane navigation and rapid tab/Group changes | Not measured |
| Idle CPU / disk I/O | Existing NFR-001 unchanged: average CPU < 0.1% over 10 minutes and 0 process I/O bytes | Managed-list-aware adaptation of `tests/release/release_evidence.ps1`; no debugger, no interaction, managed-list opt-in enabled | Not measured |
| Idle power | No polling or continuously scheduled worker activity; zero idle CPU/disk I/O remains the blocking power proxy | Supporting Windows Performance Recorder energy trace where available; report method and trace, but do not call it a pass/fail watt measurement | Not measured |

The test corpus, environments, required sample counts, future command and PASS/FAIL/UNVERIFIED rules are defined in `docs/testing.md` under “Managed-list preservation and release gate”. The future PD-241 harness must emit a complete result and fail closed on a missing sample or environment. A direct energy number is supplemental only; this repository has no validated per-process watt measurement method yet.

## Local tab-switch Shell probe (2026-09-29)

`panedock_tab_switch_probe` reuses the product's `ExplorerHost` in a separate
visible window. It navigates between the reported E: and D: local folders,
then leaves the E: view idle before switching to D: again. E: had 25 visible
items; D: had one visible file and three hidden files. `complete_ms` ends at
the Shell navigation callback; `all_items_ms` ends when `IFolderView2::ItemCount`
reports every expected visible item. Both times start before `navigate()`.
The probe waits on window messages while idle and makes no periodic folder
queries during that interval.

| Idle period | E→D phase | `navigate()` call | Navigation complete | All visible items |
|---|---|---:|---:|---:|
| 120 s | warm | 0.56 ms | 57.49 ms | 133.02 ms |
| 120 s | after idle | 1.22 ms | 42.99 ms | 107.15 ms |
| 600 s | warm | 0.51 ms | 52.21 ms | 123.84 ms |
| 600 s | after idle | 4.57 ms | 46.84 ms | 114.66 ms |

These runs did not reproduce the reported long delay. This is a Shell-host
measurement, not a PaneDock tab-click or pixel-paint measurement, so the
"Tab realize latency on activation" row remains **Not measured**. Run the
probe with `cmake --build build --target panedock_tab_switch_probe`, then
`build\panedock_tab_switch_probe.exe <from> <to> <visible-from-count> <visible-to-count> <idle-seconds>`.

## Release evidence contract

`tests/release/release_evidence.ps1` regenerates `docs/release-evidence.md`. The contract is fail-closed:

- An unmeasured blocking metric produces **INCOMPLETE** and exit code 2. Absence of evidence is never treated as absence of a problem.
- A measured threshold failure, a build failure, or a test failure produces exit code 1.
- A skipped test is not evidence. Skipped `ctest` results are reported as INCOMPLETE, not as a pass.
- Process totals and estimates are context only and never satisfy a gate.

The current `docs/release-evidence.md` contains measured blocking metrics: idle CPU passed, while the historic PD-003 idle disk I/O sample failed and PD-176 attributed its intermittent normal-mode-only signal to third-party Shell extension activity. The fail-closed contract still applies to any future run whose blocking metric is unmeasured.

## Why the language runtime is not on this table

An earlier selection review reasoned about memory by comparing language runtimes, and reached a wrong conclusion (recorded in `docs/tickets.md` §計畫決策紀錄, 2026-08-20). The correction, and the reason no runtime row appears above:

In the four-pane configuration the native host process is roughly 5–20 MB while the four Shell views plus thumbnails plus in-process third-party extensions are 150–500 MB. The runtime is about 5–8% of the total. Choosing a language to save runtime memory buys a rounding error and pays for it in integration risk.

For the legacy implementation, the decisions that bound memory are architectural and are stated in `docs/design-spec.md` §NFR-002:

1. Only the visible pane's active tab holds a live `IExplorerBrowser`.
2. The thumbnail pipeline has explicit cache and size caps.
3. Third-party shell extensions load lazily.

For the managed-list implementation, use the explicit acceptance budgets above and measure them on the defined corpus. Reasoning from deployment size, runtime baselines or another project's figures is not evidence here.
