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

$chromeStart = $paneSource.IndexOf('void Pane::refresh_navigation_chrome(')
$chromeEnd = $paneSource.IndexOf('void Pane::refresh_status_bar(', $chromeStart)
if ($chromeStart -lt 0 -or $chromeEnd -lt 0) {
    throw 'Address-bar failure check failed: navigation chrome helper missing'
}
$chromeBody = $paneSource.Substring($chromeStart, $chromeEnd - $chromeStart)
if ($chromeBody -notmatch 'refresh_navigation_buttons' -or
    $chromeBody -notmatch 'SetWindowTextW') {
    throw 'Address-bar failure check failed: normal chrome refresh lost address synchronization'
}

$editStart = $paneSource.IndexOf('LRESULT CALLBACK address_edit_proc(')
$editEnd = $paneSource.IndexOf('void set_font(', [Math]::Max(0, $editStart))
if ($editStart -lt 0 -or $editEnd -lt $editStart) {
    throw 'Address-bar failure check failed: pane EDIT subclass missing'
}
$editBody = $paneSource.Substring($editStart, $editEnd - $editStart)
if ($editBody -match 'AppState|state->panes' -or
    $editBody -notmatch 'reinterpret_cast<Pane\*>\(reference_data\)' -or
    $editBody -notmatch 'WM_LBUTTONDOWN && GetFocus\(\) != window[\s\S]*SetFocus\(window\);\s*SendMessageW\(window, EM_SETSEL, 0, -1\)' -or
    $editBody -notmatch 'WM_KEYDOWN[\s\S]*pane->submit_address\(\)' -or
    $editBody -notmatch 'WM_CHAR && wparam == VK_RETURN\) return 0' -or
    $editBody -notmatch 'WM_NCDESTROY[\s\S]*RemoveWindowSubclass\(window, address_edit_proc, subclass_id\)') {
    throw 'Address-bar failure check failed: EDIT ownership or input behavior changed'
}
if ($source -match 'address_edit_proc' -or
    $paneSource -notmatch '!SetWindowSubclass\(address_bar_, address_edit_proc, index_,\s*reinterpret_cast<DWORD_PTR>\(this\)\)\)\s*\{\s*destroy\(\);\s*return false;') {
    throw 'Address-bar failure check failed: Pane create must own subclass wiring and report failure'
}

Write-Output 'PASSED: address_bar_failure_check'
