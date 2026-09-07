param(
    [Parameter(Mandatory = $true)]
    [string] $SourcePath
)

$ErrorActionPreference = 'Stop'
$source = ($SourcePath -split ',' | ForEach-Object {
    Get-Content -LiteralPath $_ -Raw
}) -join "`n"

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
Assert-Source 'void\s+show_startup_notification\(HWND owner,\s*AppState& state\)' 'recoverable startup warnings have a modeless notification path'
Assert-Source 'L"BUTTON", L"OK"' 'startup notification has an explicit OK action'
Assert-Source 'constexpr wchar_t kWindowClassName\[\]\s*=\s*L"PaneDockStartupNotification"[\s\S]*?kWindowClassName,\s*nullptr,\s*WS_CHILD\s*\|\s*WS_VISIBLE' 'startup notification remains a root child'
Assert-Source 'RealizationMode::startup_frame' 'WM_CREATE blocks Shell realization through the core plan'
Assert-Source 'plan_realization\(\s*group,\s*group\.layout_template,\s*realized_flags\(state\),\s*realization_mode\s*\)' 'layout consumes the shared realization plan'
Assert-Source 'if\s*\(plan_contains\(realization_plan\.realize,\s*index\)\)' 'layout executes planned realization only'
Assert-Source 'ShowWindow\(window,[\s\S]*?UpdateWindow\(window\);\s*state\.startup_frame_only\s*=\s*false;' 'frame is shown before Shell gate opens'
Assert-Source 'const HRESULT active_result = apply_layout\(window, state\);' 'startup pass realizes active layout first'
Assert-Source 'state\.startup_realize_pending\s*=\s*false;\s*const HRESULT remaining_result = apply_layout\(window, state, true\);' 'remaining panes follow active pane'
Assert-Source 'case kDeferredRealizeMessage:[\s\S]*?realize_startup_panes\(window, \*state\)' 'deferred message owns startup realization'
Assert-Source 'void\s+refresh_startup_chrome\(AppState& state\)' 'startup chrome has one deferred helper'
Assert-Source 'refresh_startup_chrome\(state\);\s*if \(state\.is_shutting_down\(\)\) return E_ABORT;' 'startup chrome is gated before layout'

$startupRealizeQueue = $source.IndexOf(
    'PostMessageW(window, kDeferredRealizeMessage', $windowUpdated)
$uncleanWarning = $source.IndexOf(
    'L"PaneDock did not shut down cleanly last time.', $windowUpdated)
if ($startupRealizeQueue -lt 0 -or $uncleanWarning -lt 0 -or
    $startupRealizeQueue -gt $uncleanWarning) {
    throw 'startup frame order check failed: deferred realization must be queued before the unclean-shutdown warning'
}

$chromeStart = $source.IndexOf('void refresh_startup_chrome(AppState& state)')
$chromeEnd = $source.IndexOf('HRESULT realize_startup_panes(', $chromeStart)
# End realize_startup_panes at its own closing brace, not at whatever
# function happens to follow it: anchoring on an unrelated neighbour's name
# made this check fail whenever that neighbour moved.
$realizeEnd = $source.IndexOf("`n}`n", $chromeEnd)
if ($chromeStart -lt 0 -or $chromeEnd -lt 0 -or $realizeEnd -lt 0) {
    throw 'startup frame order check failed: startup chrome ownership missing'
}
$realizeEnd += 3
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
if ($deferredCase -notmatch 'FAILED\(hr\)[\s\S]*append_startup_warning[\s\S]*show_startup_notification' -or
    $deferredCase -match 'MessageBoxW') {
    throw 'startup frame order check failed: deferred Shell failure blocks the usable window'
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

$postWindowStartup = $source.Substring($windowUpdated, $messageLoopStart - $windowUpdated)
if ($postWindowStartup -match 'MessageBoxW' -or
    $postWindowStartup -notmatch 'show_startup_notification\(window, state\)') {
    throw 'startup frame order check failed: recoverable warning path is modal'
}

$shutdownStart = $source.IndexOf('void finish_shutdown(HWND window, AppState& state)')
$explorerDestroy = $source.IndexOf('destroy_panes(state);', $shutdownStart)
$notificationDestroy = $source.IndexOf('state.startup_notification.destroy();', $shutdownStart)
if ($shutdownStart -lt 0 -or $notificationDestroy -lt 0 -or
    $explorerDestroy -lt 0 -or $notificationDestroy -gt $explorerDestroy) {
    throw 'startup frame order check failed: shutdown does not destroy notification before Shell views'
}

Write-Output 'startup frame order check passed'
