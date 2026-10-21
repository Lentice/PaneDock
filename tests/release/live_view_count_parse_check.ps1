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

Assert-Equal (Get-LiveListCounts @(
        'noise',
        'panedock.live_list_count=4',
        'other=1',
        'panedock.live_list_count=2',
        'panedock.live_list_count=0')) @(4, 2, 0) 'normal samples'
Assert-Equal (Get-LiveListCounts @('noise only', 'still noise')) @() 'zero samples'
Assert-Equal (Get-LiveListCounts @('panedock.live_list_count=1', 'noise')) @(1) 'noise filtering'

$invalidFailed = $false
try {
    Get-LiveListCounts @('panedock.live_list_count=not-a-number') | Out-Null
} catch {
    $invalidFailed = $true
}
if (-not $invalidFailed) { throw 'illegal live-view input did not fail' }

Write-Output 'PASSED: live_list_count_parse_check'
exit 0
