# Release Evidence

Generated: 2026-08-29T12:45:00+08:00

## Environment

- OS: Microsoft Windows 11 專業版, build 26200, version 10.0.26200
- CPU: 12th Gen Intel(R) Core(TM) i7-12700K
- Logical processors: 20
- Evidence-script debugger attached: False
- PaneDock debugger attached during measurement: False
- Git commit: 36640fea260e7e38b44a9590f4a335915c8247d9
- Live CTest registrations: 6

| Tool | Version |
|---|---|
| cmake | cmake version 4.4.2 |
| ninja | 1.13.2 |
| clang++ | clang version 22.1.8 (https://github.com/llvm/llvm-project.git ca7933e47d3a3451d81e72ac174dcb5aa28b59d1) |
| llvm-rc | bundled LLVM-MinGW tool; clang version 22.1.8 (https://github.com/llvm/llvm-project.git ca7933e47d3a3451d81e72ac174dcb5aa28b59d1) |
| ctest | ctest version 4.4.2 |

## Blocking-threshold gate

| Metric | Blocking threshold | Measurement source | Measured | Value | Verdict |
|---|---|---|---|---|---|
| Idle CPU, 10 min | average < 0.1% | Process.TotalProcessorTime delta / elapsed / logical processors | True | 0.004948% | PASS |
| Idle disk I/O, 10 min | zero bytes | GetProcessIoCounters transfer-byte delta | True | 307294 bytes | FAIL |

## CTest gate

| Registered | Executed | Skipped markers | Verdict |
|---:|---:|---:|---|
| 6 | 6 | 0 | PASS |

## Non-blocking context

| Metric | Value | Notes |
|---|---|---|
| Resident memory, one pane, local folder | 62881792 bytes (~59.97 MiB) | Process WorkingSet64 snapshot, Single layout, `D:\Documents\Desktop\screenGif`, 3 s settle after navigation. |
| Resident memory, 4 panes, local folders | 72822784 bytes (~69.45 MiB) | Process WorkingSet64 snapshot, Four Panes layout, panes on `D:\Documents\Desktop\screenGif` / `D:\downloads` (x2 each), 3 s settle. |
| Resident memory, 4 panes, thumbnails + OneDrive + network | 72933376 bytes (~69.56 MiB) | Panes: `D:\Documents\Desktop\screenGif` (thumbnails), `D:\OneDrive - via.com.tw\附件` (OneDrive), `\\vianextfs06\Tmp\Lentice\test` (network), `D:\downloads`; 8 s settle. |
| Handle count after 20 automated layout switches | 971 → 894 (21 samples) | Alternated Single/Four Panes 20 times via direct `BM_CLICK` to the layout buttons. Not strictly monotonic: oscillates between two plateaus (~735 on Single, ~890 on Four Panes) with a small +6 handle rise in the first 3 switches then flat. No unbounded growth observed. |
| Thumbnail pipeline memory contribution | 0 bytes | WorkingSet64(thumbnail folder) - WorkingSet64(text-only folder), both re-visits of folders already navigated earlier in the same run — thumbnails were already cached from the prior four-pane step, so this delta measures re-navigation, not first-time thumbnail generation. Not a reliable read on thumbnail cost; treat as not measured for planning purposes. |
| Live view count after 20 switches | Not measured | Build does not emit `panedock.live_view_count=` diagnostic stdout in this configuration. |
| Group switch latency | Not measured | Measuring this requires product timing instrumentation, outside this ticket; no blocking threshold is defined. |
| Tab realize latency | Not measured | Measuring this requires product timing instrumentation, outside this ticket; no blocking threshold is defined. |
| Group switch latency, one unreachable network path | Not measured | Not exercised this run; no automated timing is attempted. |
| AC-005 restored unreachable-path responsiveness | Not exercised: this run only did direct address-bar navigation to reachable local/OneDrive/network paths; no unreachable-path Group restore was tested. | Non-blocking context, not a timing measurement. |
| Cold start to first painted pane | Not measured | Requires visible-paint instrumentation, outside this ticket. |

## Step logs

### configure

Exit code: 0

```text
-- Configuring done (0.6s)
-- Generating done (0.0s)
-- Build files have been written to: E:/GitHub/PaneDock/build
```

### build

Exit code: 0

```text
ninja: no work to do.
```

### ctest discovery

Exit code: 0

```text
Test project E:/GitHub/PaneDock/build
  Test #1: panedock_diagnostic_flag
  Test #2: panedock_tab_overflow
  Test #3: panedock_core_model
  Test #4: panedock_core_layout
  Test #5: panedock_core_session
  Test #6: panedock_launch_smoke

Total Tests: 6
```

### complete ctest

Exit code: 0

```text
Test project E:/GitHub/PaneDock/build
    Start 1: panedock_diagnostic_flag
1/6 Test #1: panedock_diagnostic_flag .........   Passed    0.02 sec
    Start 2: panedock_tab_overflow
2/6 Test #2: panedock_tab_overflow ............   Passed    0.02 sec
    Start 3: panedock_core_model
3/6 Test #3: panedock_core_model ..............   Passed    0.02 sec
    Start 4: panedock_core_layout
4/6 Test #4: panedock_core_layout .............   Passed    0.02 sec
    Start 5: panedock_core_session
5/6 Test #5: panedock_core_session ............   Passed    0.04 sec
    Start 6: panedock_launch_smoke
6/6 Test #6: panedock_launch_smoke ............   Passed    0.76 sec

100% tests passed out of 6

Total Test time (real) =   0.88 sec
```

Note: an earlier run of `panedock_launch_smoke` in this same session exited with code -1073740791 (0xC0000409, STATUS_STACK_BUFFER_OVERRUN) during shutdown of a launch against the real, large `%LOCALAPPDATA%\PaneDock\session.json` (4 groups, one with 42 tabs, accumulated from prior manual use — not created by this measurement run, which never opens new tabs). Immediately re-running the same test in isolation, and then the full suite, passed cleanly both times: not reproducible on demand. Recorded here rather than discarded silently; candidate for a follow-up investigation ticket if it recurs, out of scope for this measurement-only ticket.

### process launch: measurement

Exit code: 0 (closed via WM_CLOSE after the full measurement sequence)

```text
Automated run (see tests/release/release_evidence.ps1 design; this pass used a temporary driver script
that reuses its helper functions and drives the same measurement points without human Read-Host input,
via direct Win32 messages to PaneDock's window controls: WM_KEYDOWN/VK_RETURN to the address-bar edit
controls, BM_CLICK to the layout-template buttons). No physical mouse/keyboard input was used.
No panedock.live_view_count stdout lines were captured.
```

### idle sample

Exit code: 0

```text
elapsed_seconds=600.0236721
average_cpu_percent=0.0049477214617379755
io_bytes=307294
working_set_bytes=64147456
handles=663
Idle window: four panes, all on local folders (D:\Documents\Desktop\screenGif, D:\downloads),
untouched for the full 600 s after a 5 s settle post-navigation.
```

### process launch: soak-1

Exit code: 0 (closed via WM_CLOSE after a Four Panes layout switch and one navigation)

```text
Automated soak run: launched, switched to Four Panes, navigated pane 0 to D:\Documents\Desktop\screenGif,
2 s settle, closed via WM_CLOSE.
```

### process launch: soak-2

Exit code: 0 (closed via WM_CLOSE after a Four Panes layout switch and one navigation)

```text
Automated soak run: same sequence as soak-1.
```

### process launch: soak-3

Exit code: 0 (closed via WM_CLOSE after a Four Panes layout switch and one navigation)

```text
Automated soak run: same sequence as soak-1.
```

## Result

**FAIL** — the idle disk I/O blocking threshold failed (307294 bytes observed against a zero-byte gate). Idle CPU passed (0.004948% against < 0.1%). CTest evidence is current (6 registered, 6 executed, 0 skipped).
