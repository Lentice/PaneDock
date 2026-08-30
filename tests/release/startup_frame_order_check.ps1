param(
    [Parameter(Mandatory = $true)]
    [string] $SourcePath
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw

function Assert-Source([string] $Pattern, [string] $Name) {
    if ($source -notmatch $Pattern) {
        throw "startup frame order check failed: $Name"
    }
}

$windowCreation = $source.IndexOf('HWND window = CreateWindowExW(')
if ($windowCreation -lt 0) {
    throw 'startup frame order check failed: top-level window creation missing'
}
$windowShown = $source.IndexOf('ShowWindow(window', $windowCreation)
$windowUpdated = $source.IndexOf('UpdateWindow(window);', $windowShown)
if ($windowShown -lt 0 -or $windowUpdated -lt 0) {
    throw 'startup frame order check failed: frame is not shown'
}

Assert-Source 'bool\s+startup_frame_only\{\};' 'frame-only state exists'
Assert-Source 'void\s+append_startup_warning\(AppState& state,\s*std::wstring_view warning\)' 'startup warnings have an append path'
Assert-Source 'if\s*\(!state\.realized\[index\]\s*&&\s*!state\.startup_frame_only' 'WM_CREATE blocks Shell realization'
Assert-Source 'ShowWindow\(window,[\s\S]*?UpdateWindow\(window\);\s*state\.startup_frame_only\s*=\s*false;' 'frame is shown before Shell gate opens'
Assert-Source 'const HRESULT active_result = apply_layout\(window, state\);' 'startup pass realizes active layout first'
Assert-Source 'state\.startup_realize_pending\s*=\s*false;\s*const HRESULT remaining_result = apply_layout\(window, state, true\);' 'remaining panes follow active pane'
Assert-Source 'case kDeferredRealizeMessage:[\s\S]*?realize_startup_panes\(window, \*state\)' 'deferred message owns startup realization'
Assert-Source 'void\s+refresh_startup_chrome\(AppState& state\)' 'startup chrome has one deferred helper'
Assert-Source 'refresh_startup_chrome\(state\);\s*if \(state\.shutdown_deferred \|\| state\.closing_\) return E_ABORT;' 'startup chrome is gated before layout'

$chromeStart = $source.IndexOf('void refresh_startup_chrome(AppState& state)')
$chromeEnd = $source.IndexOf('HRESULT realize_startup_panes(', $chromeStart)
$realizeEnd = $source.IndexOf('std::string unique_group_id', $chromeEnd)
if ($chromeStart -lt 0 -or $chromeEnd -lt 0 -or $realizeEnd -lt 0) {
    throw 'startup frame order check failed: startup chrome ownership missing'
}
$chromeBody = $source.Substring($chromeStart, $chromeEnd - $chromeStart)
$realizeBody = $source.Substring($chromeEnd, $realizeEnd - $chromeEnd)
if ($chromeBody -notmatch 'state\.pinned_fixed_labels\[index\]\s*=\s*display_text_for_parsing_name' -or
    $chromeBody -notmatch 'refresh_tab_strips\(state\)' -or
    $realizeBody -notmatch 'refresh_startup_chrome\(state\)') {
    throw 'startup frame order check failed: deferred helper does not own startup chrome'
}

$mainStart = $source.IndexOf('int WINAPI wWinMain(')
$messageLoopStart = $source.IndexOf('MSG message{}', $mainStart)
if ($mainStart -lt 0 -or $messageLoopStart -lt 0) {
    throw 'startup frame order check failed: startup message loop missing'
}
$startupPrologue = $source.Substring($mainStart, $messageLoopStart - $mainStart)
if ($startupPrologue -match 'refresh_startup_chrome\(state\)|state\.pinned_fixed_labels\[index\]\s*=\s*display_text_for_parsing_name|refresh_tab_strips\(state\)') {
    throw 'startup frame order check failed: normal prologue performs startup chrome lookup'
}

$deferredStart = $source.IndexOf('case kDeferredRealizeMessage:')
$deferredEnd = $source.IndexOf('case kTabStripSelectionMessage:', $deferredStart)
if ($deferredStart -lt 0 -or $deferredEnd -lt 0) {
    throw 'startup frame order check failed: deferred realization case missing'
}
$deferredCase = $source.Substring($deferredStart, $deferredEnd - $deferredStart)
if ($deferredCase -notmatch 'FAILED\(hr\)[\s\S]*MessageBoxW' -or
    $deferredCase -match 'startup_warning_message\.empty') {
    throw 'startup frame order check failed: deferred Shell failure is silent'
}

$fatalStart = $source.IndexOf('if (window == nullptr) {')
$fatalEnd = $source.IndexOf('ShowWindow(window', $fatalStart)
if ($fatalStart -lt 0 -or $fatalEnd -lt 0) {
    throw 'startup frame order check failed: fatal startup branch missing'
}
$fatalBranch = $source.Substring($fatalStart, $fatalEnd - $fatalStart)
if ($fatalBranch -notmatch 'startup_warning_message\.empty\(\)[\s\S]*MessageBoxW') {
    throw 'startup frame order check failed: fatal branch drops warnings'
}

Write-Output 'startup frame order check passed'
