param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp')
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw

function Assert-Source([string] $Pattern, [string] $Name) {
    if ($source -notmatch $Pattern) {
        throw "Shell re-entry invariant failed: $Name"
    }
}

Assert-Source 'unsigned\s+shell_call_depth\{\};' 'Shell call depth exists'
Assert-Source 'constexpr UINT kDeferredShutdownMessage' `
    'shutdown has a posted continuation message'
Assert-Source 'if \(state\.shell_call_depth != 0\)\s*\{' `
    'close defers while a Shell call is active'
Assert-Source 'PostMessageW\(state_\.main_window, kDeferredShutdownMessage' `
    'Shell scope queues deferred shutdown after re-entry'
Assert-Source 'state->closing_ \|\| state->shutdown_deferred' `
    'main-window work is blocked during deferred teardown'
Assert-Source 'case kDeferredShutdownMessage:' `
    'deferred teardown is resumed by the message loop'
Assert-Source 'ShellCallScope shell_call\(state\)' `
    'ExplorerHost callers use the shared Shell-call gate'

Write-Output 'PASSED: shell_reentry_gate_check'
