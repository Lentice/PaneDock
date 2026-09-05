param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp'),
    [string] $PaneSourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\pane.cpp')
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw
$paneSource = Get-Content -LiteralPath $PaneSourcePath -Raw

$failedStart = $paneSource.IndexOf('void Pane::navigation_failed(')
$failedEnd = $paneSource.IndexOf('void Pane::navigate_history(', $failedStart)
if ($failedStart -lt 0 -or $failedEnd -lt 0) {
    throw 'Address-bar failure check failed: navigation failure handler missing'
}

$failedBody = $paneSource.Substring($failedStart, $failedEnd - $failedStart)
if ($failedBody -notmatch 'set_suppress_history\(false\)') {
    throw 'Address-bar failure check failed: history suppression is not released'
}
if ($failedBody -notmatch
    'refresh_navigation_buttons\(\)') {
    throw 'Address-bar failure check failed: button refresh is missing'
}
if ($failedBody -match 'refresh_navigation_chrome|SetWindowTextW') {
    throw 'Address-bar failure check failed: failure path rewrites the address bar'
}

$chromeStart = $source.IndexOf('void refresh_navigation_chrome(')
$chromeEnd = $source.IndexOf('void capture_pane_view_mode(', $chromeStart)
if ($chromeStart -lt 0 -or $chromeEnd -lt 0) {
    throw 'Address-bar failure check failed: navigation chrome helper missing'
}
$chromeBody = $source.Substring($chromeStart, $chromeEnd - $chromeStart)
if ($chromeBody -notmatch 'refresh_navigation_buttons' -or
    $chromeBody -notmatch 'SetWindowTextW') {
    throw 'Address-bar failure check failed: normal chrome refresh lost address synchronization'
}

Write-Output 'PASSED: address_bar_failure_check'
