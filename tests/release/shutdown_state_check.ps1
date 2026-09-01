param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp')
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw

function Assert-Source([string] $Pattern, [string] $Name) {
    if ($source -notmatch $Pattern) {
        throw "shutdown state invariant failed: $Name"
    }
}

Assert-Source 'bool\s+shutdown_save_attempted\{\};' 'final save attempt flag exists'
Assert-Source 'bool\s+shutdown_clean_marker_armed\{\};' 'clean marker arm flag exists'
Assert-Source 'bool\s+main_window_destroyed\{\};' 'main window destruction flag exists'
Assert-Source 'bool\s+end_session_pending\{\};' 'confirmed session-end flag exists'
Assert-Source 'session_dirty\s*&&\s*!state->shutdown_save_attempted' `
    'WM_DESTROY only saves before a final attempt'
Assert-Source 'const bool save_succeeded = save_now\(state, false, true\);[\s\S]*?state\.shutdown_clean_marker_armed = save_succeeded;' `
    'shutdown arms clean marker only after false save succeeds'
Assert-Source 'state->main_window_destroyed = true;[\s\S]*?save_now\(\*state, false, true\)' `
    'unexpected destroy fallback keeps marker false'
Assert-Source 'if \(state\.shutdown_prompt_active\)\s*\{[\s\S]*?if \(!allow_keep_open\) state\.end_session_pending = true;' `
    'nested confirmed shutdown is remembered'
Assert-Source 'void\s+set_main_window_title\(HWND window, bool diagnostic_mode,\s*bool closing\)[\s\S]*?L"PaneDock.*Closing\.\.\."' `
    'closing caption has a dedicated update path'
Assert-Source 'set_main_window_title\(window, state\.diagnostic_mode, true\);[\s\S]*?destroy_explorers\(state\);' `
    'closing state is visible before Shell teardown'
Assert-Source 'state\.shutdown_deferred\s*=\s*true;[\s\S]*?set_main_window_title\(window, state\.diagnostic_mode, true\);[\s\S]*?PostMessageW\(window, kDeferredShutdownMessage' `
    'closing state yields to the message loop before teardown'
Assert-Source 'if \(answer != IDNO\)\s*\{[\s\S]*?state\.shutdown_save_attempted = false;[\s\S]*?return;' `
    'only explicit No closes after an interactive failure'
Assert-Source 'begin_shutdown\(window, state, !state\.end_session_pending\);' `
    'deferred confirmed shutdown does not allow a prompt'
Assert-Source 'case WM_ENDSESSION:[\s\S]*?state->end_session_pending = true;' `
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

Write-Output 'PASSED: shutdown_state_check'
