param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp')
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw

$failedStart = $source.IndexOf('void handle_navigation_failed(')
$failedEnd = $source.IndexOf('void destroy_panes(', $failedStart)
if ($failedStart -lt 0 -or $failedEnd -lt 0) {
    throw 'Address-bar failure check failed: navigation failure handler missing'
}

$failedBody = $source.Substring($failedStart, $failedEnd - $failedStart)
if ($failedBody -notmatch
    'state\.panes\[pane_index\]\.set_suppress_history\(false\)') {
    throw 'Address-bar failure check failed: history suppression is not released'
}
if ($failedBody -notmatch
    'refresh_navigation_buttons\(state,\s*pane_index\)') {
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
