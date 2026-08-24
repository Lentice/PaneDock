# Release Evidence

Generated: 2026-08-24T13:01:44.4763246+08:00

## Environment

- OS: Microsoft Windows 11 專業版, build 26200, version 10.0.26200
- CPU: 12th Gen Intel(R) Core(TM) i7-12700K
- Logical processors: 20
- Evidence-script debugger attached: False
- PaneDock debugger attached during measurement: Not measured
- Git commit: d36ae74e51acbf7ad01773b1149970fc6623855f
- Live CTest registrations: 1

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
| 1 | 1 | 0 | PASS |

## Non-blocking context

| Metric | Value | Notes |
|---|---|---|
| Resident memory, 1 pane, local folder | Not measured | Prototype has only two- and four-pane layouts; hidden panes remain live. |
| Resident memory, 4 panes, local folders | Not measured | Requires a real interactive desktop. |
| Resident memory, 4 panes, thumbnails + OneDrive + network | Not measured | Requires the named resources on a real interactive desktop. |
| Handle count after 20 manual layout switches | Not measured | Script waits for each human Ctrl+Shift+L action; it never synthesizes input. |
| Thumbnail pipeline memory contribution | Not measured | WorkingSet64(thumbnail folders) - WorkingSet64(text-only folders). |
| Live view count after 20 switches | Not measured | No runtime diagnostic surface exists in the prototype. |
| Group switch latency | Not measured | Prototype has no Group. |
| Tab realize latency | Not measured | Prototype has no tab. |
| Cold start to first painted pane | Not measured | Requires visible-paint instrumentation outside PD-003 scope. |

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
  Test #1: panedock_placeholder

Total Tests: 1
```

### complete ctest

Exit code: 0

```text
Test project E:/GitHub/PaneDock/build
    Start 1: panedock_placeholder
1/1 Test #1: panedock_placeholder .............   Passed    0.02 sec

100% tests passed out of 1

Total Test time (real) =   0.03 sec
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
