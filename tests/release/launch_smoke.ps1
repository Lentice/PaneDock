param(
    [Parameter(Mandatory = $true)]
    [string] $AppPath,
    [int] $StartupTimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class PaneDockSmokeWindow {
    public delegate bool EnumProc(IntPtr window, IntPtr lparam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumProc callback, IntPtr lparam);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(
        IntPtr window, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(
        IntPtr window, StringBuilder className, int classNameLength);

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessageTimeout(
        IntPtr window, uint message, IntPtr wparam, IntPtr lparam,
        uint flags, uint timeout, out IntPtr result);

    public static IntPtr FindMainWindow(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, unused) => {
            uint ownerProcessId;
            GetWindowThreadProcessId(window, out ownerProcessId);
            if (ownerProcessId == processId) {
                var className = new StringBuilder(128);
                GetClassName(window, className, className.Capacity);
                if (className.ToString() == "PaneDockMainWindow") {
                    found = window;
                    return false;
                }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static bool SendClose(IntPtr window, uint timeout) {
        IntPtr result;
        return SendMessageTimeout(
            window, 0x0010, IntPtr.Zero, IntPtr.Zero,
            0x0003, timeout, out result) != IntPtr.Zero;
    }
}
'@
$resolvedAppPath = (Resolve-Path -LiteralPath $AppPath).Path
$appName = [IO.Path]::GetFileNameWithoutExtension($resolvedAppPath)
$existing = @(Get-Process -Name $appName -ErrorAction SilentlyContinue)
if ($existing.Count -ne 0) {
    throw "Cannot run launch smoke test while $appName is already running."
}

# --diagnostic makes the app emit "panedock.live_view_count=N". The teardown
# path emits a final sample after destroy_panes, which is the only automated
# check that every initialized IExplorerBrowser was destroyed: the in-source
# assert() is compiled out by -DNDEBUG in the Release build this test runs.
$stdoutPath = Join-Path $env:TEMP "panedock-smoke-$PID.stdout.txt"
$stderrPath = Join-Path $env:TEMP "panedock-smoke-$PID.stderr.txt"
$process = Start-Process -FilePath $resolvedAppPath -ArgumentList '--diagnostic' `
    -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
# Touch the handle while the process is alive. Without this the object started
# by Start-Process can report a null ExitCode after the process has gone.
$null = $process.Handle
try {
    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    $mainWindow = [IntPtr]::Zero
    do {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) {
            throw "$appName exited during startup with code $($process.ExitCode)."
        }
        $mainWindow = [PaneDockSmokeWindow]::FindMainWindow(
            [uint32] $process.Id)
    } while ($mainWindow -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $deadline)

    $process.Refresh()
    if ($mainWindow -eq [IntPtr]::Zero) {
        throw "$appName did not create a main window within $StartupTimeoutSeconds seconds."
    }

    if (-not [PaneDockSmokeWindow]::SendClose($mainWindow, 3000)) {
        throw "$appName main window could not be closed gracefully."
    }
    if (-not $process.WaitForExit(30000)) {
        throw "$appName did not exit after its main window was closed."
    }
    # The bounded overload does not wait for the redirected output readers to
    # drain, and leaves ExitCode unpopulated. The unbounded call does both, and
    # cannot block here because the process has already exited.
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "$appName exited after smoke test with code $($process.ExitCode)."
    }

    $counts = @()
    if (Test-Path $stdoutPath) {
        foreach ($line in @(Get-Content -LiteralPath $stdoutPath)) {
            if ($line -notmatch '^panedock\.live_view_count=(\d+)$') { continue }
            $counts += [int] $Matches[1]
        }
    }
    if ($counts.Count -eq 0) {
        throw "$appName emitted no live view count; the teardown sample is missing."
    }
    if ($counts[-1] -ne 0) {
        throw ("$appName leaked Shell views: final live_view_count=" +
               "$($counts[-1]) (samples: $($counts -join ','))")
    }
    Write-Host "$appName launch smoke test passed (live_view_count reached 0)."
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
}
