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
Assert-Source 'bool\s+end_session_pending\{\};' 'confirmed session-end flag exists'
Assert-Source 'session_dirty\s*&&\s*!state->shutdown_save_attempted' `
    'WM_DESTROY only saves before a final attempt'
Assert-Source 'if \(state\.shutdown_prompt_active\)\s*\{[\s\S]*?if \(!allow_keep_open\) state\.end_session_pending = true;' `
    'nested confirmed shutdown is remembered'
Assert-Source 'if \(answer != IDNO\)\s*\{[\s\S]*?state\.shutdown_save_attempted = false;\s*return;' `
    'only explicit No closes after an interactive failure'
Assert-Source 'begin_shutdown\(window, state, !state\.end_session_pending\);' `
    'deferred confirmed shutdown does not allow a prompt'
Assert-Source 'case WM_ENDSESSION:[\s\S]*?state->end_session_pending = true;' `
    'WM_ENDSESSION records confirmation'

Write-Output 'PASSED: shutdown_state_check'
