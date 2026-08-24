$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'release_evidence.ps1'
. $scriptPath

function Assert-Equal($Actual, $Expected, [string]$Name) {
    $actualArray = if ($null -eq $Actual) { @() } else { @($Actual) }
    $expectedArray = if ($null -eq $Expected) { @() } else { @($Expected) }
    if ($actualArray.Count -ne $expectedArray.Count -or
        ($actualArray -join ',') -ne ($expectedArray -join ',')) {
        throw "${Name}: expected '$($expectedArray -join ',')', got '$($actualArray -join ',')'"
    }
}

Assert-Equal (Get-LiveViewCounts @(
        'noise',
        'panedock.live_view_count=4',
        'other=1',
        'panedock.live_view_count=2',
        'panedock.live_view_count=0')) @(4, 2, 0) 'normal samples'
Assert-Equal (Get-LiveViewCounts @('noise only', 'still noise')) @() 'zero samples'
Assert-Equal (Get-LiveViewCounts @('panedock.live_view_count=1', 'noise')) @(1) 'noise filtering'

$invalidFailed = $false
try {
    Get-LiveViewCounts @('panedock.live_view_count=not-a-number') | Out-Null
} catch {
    $invalidFailed = $true
}
if (-not $invalidFailed) { throw 'illegal live-view input did not fail' }

Write-Output 'PASSED: live_view_count_parse_check'
exit 0
