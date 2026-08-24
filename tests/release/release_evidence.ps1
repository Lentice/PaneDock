[CmdletBinding()]
param(
    [switch]$CollectMeasurements,
    [ValidateRange(1, 86400)]
    [int]$IdleSeconds = 600
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $repo 'build'
$evidencePath = Join-Path $repo 'docs\release-evidence.md'
$appPath = Join-Path $build 'PaneDock.exe'
$logs = [System.Collections.Generic.List[object]]::new()
$failed = $false
$incomplete = $false

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class PaneDockProcessMetrics {
    [StructLayout(LayoutKind.Sequential)]
    public struct IoCounters {
        public ulong ReadOperationCount, WriteOperationCount, OtherOperationCount;
        public ulong ReadTransferCount, WriteTransferCount, OtherTransferCount;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetProcessIoCounters(IntPtr process, out IoCounters counters);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CheckRemoteDebuggerPresent(IntPtr process, out bool attached);
}
'@

function Invoke-LoggedStep([string]$Name, [scriptblock]$Command) {
    $output = @(& $Command 2>&1 | ForEach-Object { $_.ToString() })
    $code = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
    $script:logs.Add([pscustomobject]@{ Name = $Name; Output = $output; ExitCode = $code })
    if ($code -ne 0) { $script:failed = $true }
    return $code
}

function Get-IoBytes([System.Diagnostics.Process]$Process) {
    $counters = [PaneDockProcessMetrics+IoCounters]::new()
    if (-not [PaneDockProcessMetrics]::GetProcessIoCounters($Process.Handle, [ref]$counters)) {
        throw "GetProcessIoCounters failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    return $counters.ReadTransferCount + $counters.WriteTransferCount + $counters.OtherTransferCount
}

function Get-DebuggerAttached([System.Diagnostics.Process]$Process) {
    $attached = $false
    if (-not [PaneDockProcessMetrics]::CheckRemoteDebuggerPresent($Process.Handle, [ref]$attached)) {
        throw "CheckRemoteDebuggerPresent failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    return $attached
}

function Start-PaneDock([string]$Name) {
    $stdout = Join-Path $env:TEMP "panedock-$PID-$Name.stdout.txt"
    $stderr = Join-Path $env:TEMP "panedock-$PID-$Name.stderr.txt"
    $process = Start-Process -FilePath $appPath -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $script:logs.Add([pscustomobject]@{
        Name = "process launch: $Name"
        Output = @("PID=$($process.Id)", "stdout=$stdout", "stderr=$stderr")
        ExitCode = 'pending until process closes'
    })
    return [pscustomobject]@{ Process = $process; Stdout = $stdout; Stderr = $stderr; LogIndex = $logs.Count - 1 }
}

function Complete-PaneDock($Run) {
    $Run.Process.WaitForExit()
    $output = @()
    if (Test-Path $Run.Stdout) { $output += Get-Content $Run.Stdout }
    if (Test-Path $Run.Stderr) { $output += Get-Content $Run.Stderr }
    $script:logs[$Run.LogIndex].Output += $output
    $script:logs[$Run.LogIndex].ExitCode = $Run.Process.ExitCode
    if ($Run.Process.ExitCode -ne 0) { $script:failed = $true }
    Remove-Item $Run.Stdout, $Run.Stderr -Force -ErrorAction SilentlyContinue
    return $output
}

function Get-Snapshot([System.Diagnostics.Process]$Process) {
    $Process.Refresh()
    return [pscustomobject]@{
        WorkingSetBytes = [uint64]$Process.WorkingSet64
        Handles = $Process.HandleCount
        IoBytes = Get-IoBytes $Process
        CpuSeconds = $Process.TotalProcessorTime.TotalSeconds
    }
}

function Get-LiveViewCounts([string[]]$Lines) {
    $counts = [System.Collections.Generic.List[uint32]]::new()
    foreach ($line in @($Lines)) {
        if ($line -notmatch '^panedock\.live_view_count=(.*)$') { continue }
        $payload = $Matches[1]
        if ($payload -notmatch '^\d+$') {
            throw "Invalid live view count: $line"
        }
        [uint32]$value = 0
        if (-not [uint32]::TryParse($payload, [ref]$value)) {
            throw "Invalid live view count: $line"
        }
        $counts.Add($value)
    }
    return $counts.ToArray()
}

if ($MyInvocation.InvocationName -ne '.') {
Push-Location $repo
try {
    Invoke-LoggedStep 'configure' {
        & cmake -S . -B build -G Ninja '-DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake' '-DCMAKE_BUILD_TYPE=Release'
    } | Out-Null
    if (-not $failed) {
        Invoke-LoggedStep 'build' { & cmake --build build } | Out-Null
    } else {
        $logs.Add([pscustomobject]@{ Name = 'build'; Output = @('Not run: configure failed.'); ExitCode = 'not run' })
    }

    $registeredOutput = @(& ctest --test-dir build -N 2>&1 | ForEach-Object { $_.ToString() })
    $registeredCode = $LASTEXITCODE
    $logs.Add([pscustomobject]@{ Name = 'ctest discovery'; Output = $registeredOutput; ExitCode = $registeredCode })
    $registered = @($registeredOutput | Select-String '^\s*Test\s+#\d+:').Count
    if ($registeredCode -ne 0) { $failed = $true }

    $testOutput = @()
    $testCode = -1
    if (-not $failed) {
        $testOutput = @(& ctest --test-dir build --output-on-failure 2>&1 | ForEach-Object { $_.ToString() })
        $testCode = $LASTEXITCODE
        $logs.Add([pscustomobject]@{ Name = 'complete ctest'; Output = $testOutput; ExitCode = $testCode })
        if ($testCode -ne 0) { $failed = $true }
    } else {
        $logs.Add([pscustomobject]@{ Name = 'complete ctest'; Output = @('Not run: an earlier build/discovery step failed.'); ExitCode = 'not run' })
    }
    $executed = @($testOutput | Select-String '^\s*Start\s+\d+:').Count
    $skipped = @($testOutput | Select-String '(?i)skipped|not run').Count
    $ctestVerdict = if ($failed) { 'FAIL' } elseif ($skipped -gt 0 -or $registered -ne $executed) { 'STALE' } else { 'PASS' }
    if ($ctestVerdict -eq 'STALE') { $incomplete = $true }

    $idle = $null
    $memory = [ordered]@{}
    $handleSamples = @()
    $thumbnailDelta = $null
    $appDebuggerAttached = 'Not measured'
    $measurementOutput = @()
    $liveViewCounts = @()
    $liveViewParseError = $null
    $ac005Response = 'Not measured'

    if ($CollectMeasurements -and -not $failed) {
        $run = Start-PaneDock 'measurement'
        Read-Host 'Arrange four panes on local folders, wait for navigation to settle, then press Enter to start the untouched idle sample' | Out-Null
        $appDebuggerAttached = Get-DebuggerAttached $run.Process
        $start = Get-Snapshot $run.Process
        $watch = [Diagnostics.Stopwatch]::StartNew()
        Start-Sleep -Seconds $IdleSeconds
        $watch.Stop()
        $end = Get-Snapshot $run.Process
        $cpuPercent = (($end.CpuSeconds - $start.CpuSeconds) / $watch.Elapsed.TotalSeconds / [Environment]::ProcessorCount) * 100
        $idle = [pscustomobject]@{
            Seconds = $watch.Elapsed.TotalSeconds
            CpuPercent = $cpuPercent
            IoBytes = $end.IoBytes - $start.IoBytes
            WorkingSetBytes = $end.WorkingSetBytes
            Handles = $end.Handles
        }
        $logs.Add([pscustomobject]@{
            Name = 'idle sample'
            Output = @("elapsed_seconds=$($idle.Seconds)", "average_cpu_percent=$($idle.CpuPercent)", "io_bytes=$($idle.IoBytes)", "working_set_bytes=$($idle.WorkingSetBytes)", "handles=$($idle.Handles)")
            ExitCode = 0
        })

        Read-Host 'Switch to the Single layout, navigate to one local folder, wait for it to settle, then press Enter to record the one-pane memory configuration' | Out-Null
        $memory['One pane, local folder'] = Get-Snapshot $run.Process
        Read-Host 'Switch to the Four Panes layout on local folders; press Enter to record the local-folder memory configuration' | Out-Null
        $memory['Four panes, local folders'] = Get-Snapshot $run.Process
        Read-Host 'Arrange four panes to include thumbnails, OneDrive, and a network path; press Enter after settling' | Out-Null
        $memory['Four panes, thumbnails + OneDrive + network'] = Get-Snapshot $run.Process
        Read-Host 'Arrange four panes on text-only folders; press Enter after settling' | Out-Null
        $textOnly = Get-Snapshot $run.Process
        Read-Host 'Arrange the same four panes on thumbnail folders; press Enter after settling' | Out-Null
        $thumbnail = Get-Snapshot $run.Process
        $thumbnailDelta = [int64]$thumbnail.WorkingSetBytes - [int64]$textOnly.WorkingSetBytes

        $handleSamples += (Get-Snapshot $run.Process).Handles
        for ($switch = 1; $switch -le 20; $switch++) {
            Read-Host "Manually press Ctrl+Shift+L once ($switch/20), then press Enter" | Out-Null
            $handleSamples += (Get-Snapshot $run.Process).Handles
        }
        $ac005Response = Read-Host 'In the restored state with an unreachable network path, confirm whether the UI stayed responsive throughout (include any note in your answer)'
        Read-Host 'Close PaneDock normally, then press Enter' | Out-Null
        $measurementOutput = @(Complete-PaneDock $run)
        try {
            $liveViewCounts = @(Get-LiveViewCounts $measurementOutput)
        } catch {
            $liveViewParseError = $_.Exception.Message
        }

        for ($soak = 1; $soak -le 3; $soak++) {
            $soakRun = Start-PaneDock "soak-$soak"
            Read-Host "Soak $soak/3: exercise the app, then close it normally and press Enter" | Out-Null
            Complete-PaneDock $soakRun
        }
    } else {
        foreach ($name in 'process launch: measurement', 'idle sample', 'process launch: soak-1', 'process launch: soak-2', 'process launch: soak-3') {
            $logs.Add([pscustomobject]@{ Name = $name; Output = @('Not run: requires -CollectMeasurements on a real interactive desktop.'); ExitCode = 'not run' })
        }
    }

    $idleCpuMeasured = $null -ne $idle -and $idle.Seconds -ge 600
    $idleDiskMeasured = $idleCpuMeasured
    if (-not $idleCpuMeasured -or -not $idleDiskMeasured) { $incomplete = $true }
    $cpuVerdict = if (-not $idleCpuMeasured) { 'INCOMPLETE' } elseif ($idle.CpuPercent -ge 0.1) { 'FAIL' } else { 'PASS' }
    $diskVerdict = if (-not $idleDiskMeasured) { 'INCOMPLETE' } elseif ($idle.IoBytes -gt 0) { 'FAIL' } else { 'PASS' }
    if ($cpuVerdict -eq 'FAIL' -or $diskVerdict -eq 'FAIL') { $failed = $true }

    try {
        $osInfo = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
        $osDescription = "$($osInfo.Caption), build $($osInfo.BuildNumber), version $($osInfo.Version)"
    } catch {
        $windows = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
        $osDescription = "$($windows.ProductName), build $($windows.CurrentBuildNumber).$($windows.UBR), version $([Environment]::OSVersion.Version)"
    }
    try {
        $cpuName = (Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1).Name
    } catch {
        $cpuName = $env:PROCESSOR_IDENTIFIER
    }
    $commit = (& git rev-parse HEAD 2>$null)
    $currentDebugger = [Diagnostics.Debugger]::IsAttached
    $toolVersions = foreach ($tool in 'cmake', 'ninja', 'clang++', 'llvm-rc', 'ctest') {
        if ($tool -eq 'llvm-rc') {
            $command = Get-Command $tool
            $version = $command.FileVersionInfo.ProductVersion
            if ([string]::IsNullOrWhiteSpace($version)) {
                $clangVersion = @(& clang++ --version 2>&1 | Select-Object -First 1) -join ' '
                $version = "bundled LLVM-MinGW tool; $clangVersion"
            }
        } else {
            $version = @(& $tool --version 2>&1 | Select-Object -First 1) -join ' '
        }
        "| $tool | $version |"
    }

    $result = if ($failed) { 'FAIL' } elseif ($incomplete) { 'INCOMPLETE' } else { 'PASS' }
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('# Release Evidence')
    $lines.Add('')
    $lines.Add("Generated: $(Get-Date -Format o)")
    $lines.Add('')
    $lines.Add('## Environment')
    $lines.Add('')
    $lines.Add("- OS: $osDescription")
    $lines.Add("- CPU: $cpuName")
    $lines.Add("- Logical processors: $([Environment]::ProcessorCount)")
    $lines.Add("- Evidence-script debugger attached: $currentDebugger")
    $lines.Add("- PaneDock debugger attached during measurement: $appDebuggerAttached")
    $lines.Add("- Git commit: $commit")
    $lines.Add("- Live CTest registrations: $registered")
    $lines.Add('')
    $lines.Add('| Tool | Version |')
    $lines.Add('|---|---|')
    $lines.AddRange([string[]]$toolVersions)
    $lines.Add('')
    $lines.Add('## Blocking-threshold gate')
    $lines.Add('')
    $lines.Add('| Metric | Blocking threshold | Measurement source | Measured | Value | Verdict |')
    $lines.Add('|---|---|---|---|---|---|')
    $cpuValue = if ($idleCpuMeasured) { '{0:F6}%' -f $idle.CpuPercent } else { 'Not measured' }
    $diskValue = if ($idleDiskMeasured) { "$($idle.IoBytes) bytes" } else { 'Not measured' }
    $lines.Add("| Idle CPU, 10 min | average < 0.1% | Process.TotalProcessorTime delta / elapsed / logical processors | $idleCpuMeasured | $cpuValue | $cpuVerdict |")
    $lines.Add("| Idle disk I/O, 10 min | zero bytes | GetProcessIoCounters transfer-byte delta | $idleDiskMeasured | $diskValue | $diskVerdict |")
    $lines.Add('')
    $lines.Add('## CTest gate')
    $lines.Add('')
    $lines.Add('| Registered | Executed | Skipped markers | Verdict |')
    $lines.Add('|---:|---:|---:|---|')
    $lines.Add("| $registered | $executed | $skipped | $ctestVerdict |")
    $lines.Add('')
    $lines.Add('## Non-blocking context')
    $lines.Add('')
    $lines.Add('| Metric | Value | Notes |')
    $lines.Add('|---|---|---|')
    foreach ($entry in $memory.GetEnumerator()) {
        $lines.Add("| Resident memory, $($entry.Key.ToLowerInvariant()) | $($entry.Value.WorkingSetBytes) bytes | Process WorkingSet64 snapshot after operator-confirmed settling. |")
    }
    if ($memory.Count -eq 0) {
        $lines.Add('| Resident memory, one pane, local folder | Not measured | Requires a real interactive desktop to select the Single layout, navigate to a local folder, and wait for Shell enumeration to settle. |')
        $lines.Add('| Resident memory, 4 panes, local folders | Not measured | Requires a real interactive desktop. |')
        $lines.Add('| Resident memory, 4 panes, thumbnails + OneDrive + network | Not measured | Requires the named resources on a real interactive desktop. |')
    }
    if ($handleSamples.Count -eq 21) {
        $monotonic = $true
        for ($i = 1; $i -lt $handleSamples.Count; $i++) { if ($handleSamples[$i] -le $handleSamples[$i - 1]) { $monotonic = $false } }
        $lines.Add("| Handle count after 20 manual layout switches | $($handleSamples[0]) → $($handleSamples[-1]) | 21 samples; strictly monotonic growth=$monotonic. |")
    } else {
        $lines.Add('| Handle count after 20 manual layout switches | Not measured | Script waits for each human Ctrl+Shift+L action; it never synthesizes input. |')
    }
    $thumbValue = if ($null -eq $thumbnailDelta) { 'Not measured' } else { "$thumbnailDelta bytes" }
    $lines.Add("| Thumbnail pipeline memory contribution | $thumbValue | WorkingSet64(thumbnail folders) - WorkingSet64(text-only folders). |")
    if ($liveViewParseError) {
        $lines.Add("| Live view count after 20 switches | Not measured | Could not parse PaneDock stdout: $liveViewParseError |")
    } elseif ($liveViewCounts.Count -ge 2) {
        $beforeSwitches = $liveViewCounts[0]
        $afterSwitches = if ($liveViewCounts.Count -ge 3) {
            $liveViewCounts[$liveViewCounts.Count - 2]
        } else {
            $liveViewCounts[-1]
        }
        $closedValue = $liveViewCounts[-1]
        $lines.Add("| Live view count after 20 switches | before=$beforeSwitches; after=$afterSwitches; closed=$closedValue | Parsed $($liveViewCounts.Count) stdout samples; final sample is emitted after destroy. |")
    } else {
        $lines.Add('| Live view count after 20 switches | Not measured | No live-view stdout was collected; requires -CollectMeasurements and a normal interactive close. |')
    }
    $lines.Add('| Group switch latency | Not measured | Measuring this requires product timing instrumentation, outside this ticket; no blocking threshold is defined. |')
    $lines.Add('| Tab realize latency | Not measured | Measuring this requires product timing instrumentation, outside this ticket; no blocking threshold is defined. |')
    $lines.Add('| Group switch latency, one unreachable network path | Not measured | Requires an operator to restore a Group containing the unreachable path and answer the AC-005 responsiveness prompt; no automated timing is attempted. |')
    $lines.Add("| AC-005 restored unreachable-path responsiveness | $ac005Response | Operator response captured by Read-Host; this is non-blocking context, not a timing measurement. |")
    $lines.Add('| Cold start to first painted pane | Not measured | Requires visible-paint instrumentation, outside this ticket. |')
    $lines.Add('')
    $lines.Add('## Step logs')
    foreach ($log in $logs) {
        $lines.Add('')
        $lines.Add("### $($log.Name)")
        $lines.Add('')
        $lines.Add("Exit code: $($log.ExitCode)")
        $lines.Add('')
        $lines.Add('```text')
        if ($log.Output.Count -eq 0) { $lines.Add('(no stdout/stderr)') } else { $lines.AddRange([string[]]$log.Output) }
        $lines.Add('```')
    }
    $lines.Add('')
    $lines.Add('## Result')
    $lines.Add('')
    $lines.Add("**$result** — " + $(if ($result -eq 'INCOMPLETE') { 'one or more blocking metrics lack a valid ten-minute measurement, or CTest evidence is stale.' } elseif ($result -eq 'FAIL') { 'a blocking threshold, build, or test failed.' } else { 'all blocking gates passed.' }))
    [IO.File]::WriteAllText(
        $evidencePath, (($lines -join "`n") + "`n"),
        [Text.UTF8Encoding]::new($false))

    Write-Output $result
    Write-Output "Evidence: $evidencePath"
    if ($failed) { exit 1 }
    if ($incomplete) { exit 2 }
    exit 0
}
finally {
    Pop-Location
}
}
