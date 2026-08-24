# Release Evidence

Generated: 2026-08-24T19:01:45.4769205+08:00

## Environment

- OS: Windows 10 Pro, build 26200.9168, version 10.0.26200.0
- CPU: Intel64 Family 6 Model 151 Stepping 2, GenuineIntel
- Logical processors: 20
- Evidence-script debugger attached: False
- PaneDock debugger attached during measurement: Not measured
- Git commit: 7d84277f1edd8acb8a9f029516105c0a4f7e7dac
- Live CTest registrations: 4

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
| Idle CPU, 10 min | average < 0.1% | Process.TotalProcessorTime delta / elapsed / logical processors | False | Not measured | INCOMPLETE |
| Idle disk I/O, 10 min | zero bytes | GetProcessIoCounters transfer-byte delta | False | Not measured | INCOMPLETE |

## CTest gate

| Registered | Executed | Skipped markers | Verdict |
|---:|---:|---:|---|
| 4 | 4 | 0 | PASS |

## Non-blocking context

| Metric | Value | Notes |
|---|---|---|
| Resident memory, one pane, local folder | Not measured | Requires a real interactive desktop to select the Single layout, navigate to a local folder, and wait for Shell enumeration to settle. |
| Resident memory, 4 panes, local folders | Not measured | Requires a real interactive desktop. |
| Resident memory, 4 panes, thumbnails + OneDrive + network | Not measured | Requires the named resources on a real interactive desktop. |
| Handle count after 20 manual layout switches | Not measured | Script waits for each human Ctrl+Shift+L action; it never synthesizes input. |
| Thumbnail pipeline memory contribution | Not measured | WorkingSet64(thumbnail folders) - WorkingSet64(text-only folders). |
| Live view count after 20 switches | Not measured | No live-view stdout was collected; requires -CollectMeasurements and a normal interactive close. |
| Group switch latency | Not measured | Measuring this requires product timing instrumentation, outside this ticket; no blocking threshold is defined. |
| Tab realize latency | Not measured | Measuring this requires product timing instrumentation, outside this ticket; no blocking threshold is defined. |
| Group switch latency, one unreachable network path | Not measured | Requires an operator to restore a Group containing the unreachable path and answer the AC-005 responsiveness prompt; no automated timing is attempted. |
| AC-005 restored unreachable-path responsiveness | Not measured | Operator response captured by Read-Host; this is non-blocking context, not a timing measurement. |
| Cold start to first painted pane | Not measured | Requires visible-paint instrumentation, outside this ticket. |

## Step logs

### configure

Exit code: 0

```text
-- Configuring done (0.2s)
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
  Test #2: panedock_core_model
  Test #3: panedock_core_layout
  Test #4: panedock_core_session

Total Tests: 4
```

### complete ctest

Exit code: 0

```text
Test project E:/GitHub/PaneDock/build
    Start 1: panedock_diagnostic_flag
1/4 Test #1: panedock_diagnostic_flag .........   Passed    0.01 sec
    Start 2: panedock_core_model
2/4 Test #2: panedock_core_model ..............   Passed    0.02 sec
    Start 3: panedock_core_layout
3/4 Test #3: panedock_core_layout .............   Passed    0.02 sec
    Start 4: panedock_core_session
4/4 Test #4: panedock_core_session ............   Passed    0.03 sec

100% tests passed out of 4

Total Test time (real) =   0.08 sec
```

### process launch: measurement

Exit code: not run

```text
Not run: requires -CollectMeasurements on a real interactive desktop.
```

### idle sample

Exit code: not run

```text
Not run: requires -CollectMeasurements on a real interactive desktop.
```

### process launch: soak-1

Exit code: not run

```text
Not run: requires -CollectMeasurements on a real interactive desktop.
```

### process launch: soak-2

Exit code: not run

```text
Not run: requires -CollectMeasurements on a real interactive desktop.
```

### process launch: soak-3

Exit code: not run

```text
Not run: requires -CollectMeasurements on a real interactive desktop.
```

## Result

**INCOMPLETE** — one or more blocking metrics lack a valid ten-minute measurement, or CTest evidence is stale.
