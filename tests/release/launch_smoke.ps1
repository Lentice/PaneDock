param(
    [Parameter(Mandatory = $true)]
    [string] $AppPath,
    [int] $StartupTimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'
$resolvedAppPath = (Resolve-Path -LiteralPath $AppPath).Path
$appName = [IO.Path]::GetFileNameWithoutExtension($resolvedAppPath)
$existing = @(Get-Process -Name $appName -ErrorAction SilentlyContinue)
if ($existing.Count -ne 0) {
    throw "Cannot run launch smoke test while $appName is already running."
}

$process = Start-Process -FilePath $resolvedAppPath -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) {
            throw "$appName exited during startup with code $($process.ExitCode)."
        }
    } while ($process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

    $process.Refresh()
    if ($process.MainWindowHandle -eq 0) {
        throw "$appName did not create a main window within $StartupTimeoutSeconds seconds."
    }

    if (-not $process.CloseMainWindow()) {
        throw "$appName main window could not be closed gracefully."
    }
    if (-not $process.WaitForExit(30000)) {
        throw "$appName did not exit after its main window was closed."
    }
    if ($process.ExitCode -ne 0) {
        throw "$appName exited after smoke test with code $($process.ExitCode)."
    }
    Write-Host "$appName launch smoke test passed."
}
finally {
    $process.Refresh()
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}
