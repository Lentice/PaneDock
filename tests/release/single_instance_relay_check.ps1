param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp')
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw

function Assert-Source([string] $Pattern, [string] $Name) {
    if ($source -notmatch $Pattern) {
        throw "single-instance relay invariant failed: $Name"
    }
}

Assert-Source 'SendMessageTimeoutW' 'activation uses a bounded synchronous handshake'
Assert-Source 'SMTO_ABORTIFHUNG\s*\|\s*SMTO_BLOCK' 'hung receiver has a timeout'
Assert-Source 'bool\s+activate_main_window_on_own_thread\(HWND window\)\s+noexcept' `
    'activation reports its Win32 result'
Assert-Source 'return\s+activate_main_window_on_own_thread\(window\)\s*\?\s*0\s*:\s*1' `
    'activation handler forwards foreground failure'
Assert-Source 'const BOOL activated = SetForegroundWindow\(window\);[\s\S]*?return activated != FALSE;' `
    'foreground result is returned instead of logged only'
Assert-Source 'activation_result\s*==\s*0\)\s*return true' `
    'only a usable acknowledgement relays successfully'
Assert-Source 'state->closing_\s*\|\|\s*state->quit_requested' `
    'closing receiver rejects activation'
Assert-Source 'const DWORD probe_error = GetLastError\(\)' `
    'mutex probe error is captured'
Assert-Source 'probe_error != ERROR_FILE_NOT_FOUND' `
    'non-absence probe errors are visible'
Assert-Source 'GetLastError\(\) == ERROR_ALREADY_EXISTS' `
    'reacquire race remains guarded'

Write-Output 'PASSED: single_instance_relay_check'
