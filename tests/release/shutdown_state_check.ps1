param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp'),
    [string] $ShutdownHeaderPath = (Join-Path $PSScriptRoot '..\..\src\core\shutdown.h'),
    [string] $ShutdownSourcePath = (Join-Path $PSScriptRoot '..\..\src\core\shutdown.cpp')
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw
$shutdownHeader = Get-Content -LiteralPath $ShutdownHeaderPath -Raw
$shutdownSource = Get-Content -LiteralPath $ShutdownSourcePath -Raw

function Assert-Source([string] $Pattern, [string] $Name) {
    if ($source -notmatch $Pattern) {
        throw "shutdown state invariant failed: $Name"
    }
}

function Assert-Shutdown([string] $Pattern, [string] $Name) {
    if (($shutdownHeader + $shutdownSource) -notmatch $Pattern) {
        throw "shutdown state invariant failed: $Name"
    }
}

Assert-Shutdown 'bool\s+shutdown_save_attempted\{\};' 'final save attempt flag exists'
Assert-Shutdown 'bool\s+shutdown_clean_marker_armed\{\};' 'clean marker arm flag exists'
Assert-Shutdown 'bool\s+main_window_destroyed\{\};' 'main window destruction flag exists'
Assert-Shutdown 'bool\s+end_session_pending\{\};' 'confirmed session-end flag exists'
Assert-Source 'bool&\s+shutdown_save_attempted\s*=\s*shutdown_sequence\.state\(\)\.shutdown_save_attempted;' `
    'app shell forwards the final save attempt flag'
Assert-Source 'bool&\s+shutdown_clean_marker_armed\s*=\s*shutdown_sequence\.state\(\)\.shutdown_clean_marker_armed;' `
    'app shell forwards the clean marker flag'
Assert-Source 'bool&\s+main_window_destroyed\s*=\s*shutdown_sequence\.state\(\)\.main_window_destroyed;' `
    'app shell forwards the window destruction flag'
Assert-Source 'bool&\s+end_session_pending\s*=\s*shutdown_sequence\.state\(\)\.end_session_pending;' `
    'app shell forwards the confirmed session-end flag'
Assert-Source 'session_dirty\s*&&\s*!state->shutdown_save_attempted' `
    'WM_DESTROY only saves before a final attempt'
Assert-Source 'const bool save_succeeded = save_now\(state, false, true\);[\s\S]*?ShutdownEvent::save_succeeded' `
    'shutdown save result is routed through the reducer'
Assert-Source 'ShutdownEvent::window_destroyed[\s\S]*?ShutdownEvent::save_started[\s\S]*?save_now\(\*state, false, true\)' `
    'unexpected destroy fallback keeps marker false'
Assert-Shutdown 'case ShutdownEvent::save_prompt_finished:[\s\S]*?state_\.end_session_pending\s*\?\s*ShutdownAction::destroy_views' `
    'nested confirmed shutdown is remembered'
Assert-Source 'void\s+set_main_window_title\(HWND window, bool diagnostic_mode,\s*bool closing\)[\s\S]*?L"PaneDock.*Closing\.\.\."' `
    'closing caption has a dedicated update path'
Assert-Source 'set_main_window_title\(window, state\.diagnostic_mode, true\);[\s\S]*?destroy_explorers\(state\);' `
    'closing state is visible before Shell teardown'
Assert-Shutdown 'state_\.shutdown_deferred\s*=\s*true;' `
    'reducer records deferred shutdown'
Assert-Source 'set_main_window_title\(window, state\.diagnostic_mode, true\);[\s\S]*?PostMessageW\(window, kDeferredShutdownMessage' `
    'closing state yields to the message loop before teardown'
Assert-Source 'if \(answer != IDNO\)\s*\{[\s\S]*?ShutdownEvent::save_keep_open[\s\S]*?return;' `
    'only explicit No closes after an interactive failure'
Assert-Shutdown 'case ShutdownEvent::save_keep_open:[\s\S]*?state_\.shutdown_save_attempted = false;' `
    'keeping the window open resets the failed save attempt'
Assert-Source 'case WM_ENDSESSION:[\s\S]*?ShutdownEvent::end_session' `
    'WM_ENDSESSION records confirmation'

$lastOleUninitialize = $source.LastIndexOf('OleUninitialize();')
$finalCleanMarker = $source.LastIndexOf(
    'state.session_document.clean_shutdown = true;')
if ($lastOleUninitialize -lt 0 -or $finalCleanMarker -lt $lastOleUninitialize) {
    throw 'shutdown state invariant failed: clean marker is not after OleUninitialize'
}
if ($source -match 'save_now\(\s*(?:state|\*state),\s*true') {
    throw 'shutdown state invariant failed: save_now writes true before teardown'
}

function Get-FunctionSource([string] $Start, [string] $Name) {
    $startIndex = $source.IndexOf($Start)
    if ($startIndex -lt 0) {
        throw "session save debounce check failed: $Name is missing"
    }
    $bodyStart = $source.IndexOf('{', $startIndex)
    if ($bodyStart -lt 0) {
        throw "session save debounce check failed: $Name body is missing"
    }
    $end = [regex]::Match($source.Substring($bodyStart), '\r?\n}\r?\n\r?\n')
    if (-not $end.Success) {
        throw "session save debounce check failed: $Name body end is missing"
    }
    return $source.Substring($startIndex, $bodyStart + $end.Index - $startIndex)
}

function Assert-DebouncedFunction([string] $Start, [string] $Name) {
    $body = Get-FunctionSource $Start $Name
    if ($body -notmatch 'schedule_session_save\(' -or
        $body -match 'save_now\(') {
        throw "session save debounce check failed: $Name writes synchronously"
    }
}

Assert-DebouncedFunction 'void switch_active_tab(HWND, AppState& state' `
    'switch_active_tab'
Assert-DebouncedFunction 'void add_tab_to_pane(' 'add_tab_to_pane'
Assert-DebouncedFunction 'void close_tab_in_pane(' 'close_tab_in_pane'
Assert-DebouncedFunction 'void delete_group(' 'delete_group'
Assert-DebouncedFunction 'void move_group(' 'move_group'
Assert-DebouncedFunction 'void set_active_pane(' 'set_active_pane'
Assert-DebouncedFunction 'void set_pane_view_mode(' 'set_pane_view_mode'
Assert-DebouncedFunction 'void add_current_folder(' 'add_current_folder'
Assert-DebouncedFunction 'void set_layout(' 'set_layout'
Assert-DebouncedFunction 'void finish_tab_drag(' 'finish_tab_drag'
Assert-DebouncedFunction 'void finish_group_drag(' 'finish_group_drag'

$directSaveCalls = [regex]::Matches($source, 'save_now\((?:state|\*state)')
if ($directSaveCalls.Count -ne 5) {
    throw "session save debounce check failed: expected 5 synchronous save sites, found $($directSaveCalls.Count)"
}

$timerStart = $source.IndexOf('if (timer == kSessionSaveTimerId)')
$timerEnd = $source.IndexOf(
    'if (timer == kDragHoverSidebarTimerId', $timerStart)
if ($timerStart -lt 0 -or $timerEnd -lt 0) {
    throw 'session save debounce check failed: session save timer branch is missing'
}
$timerBody = $source.Substring($timerStart, $timerEnd - $timerStart)
$timerWrites = [regex]::Matches($timerBody, 'save_now\(').Count
if ($timerWrites -ne 1 -or $timerBody -notmatch 'KillTimer\(window, kSessionSaveTimerId\)') {
    throw 'session save debounce check failed: timer does not perform one post-debounce write'
}

$simulatedBurstSize = 5
if ($timerWrites -ge $simulatedBurstSize) {
    throw 'session save debounce check failed: a five-operation burst is not coalesced'
}

Write-Output 'PASSED: session_save_debounce_check'
Write-Output 'PASSED: shutdown_state_check'
