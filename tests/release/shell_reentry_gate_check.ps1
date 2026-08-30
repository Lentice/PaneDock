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

$helperStart = $source.IndexOf(
    'std::wstring display_text_for_parsing_name(')
$helperEnd = $source.IndexOf(
    'panedock::core::ApplicationState default_application_state()',
    $helperStart)
if ($helperStart -lt 0 -or $helperEnd -lt 0) {
    throw 'Shell re-entry invariant failed: display-name helper missing'
}
$helperBody = $source.Substring($helperStart, $helperEnd - $helperStart)
if ($helperBody -notmatch 'AppState& state' -or
    $helperBody -notmatch 'ShellCallScope shell_call\(state\);[\s\S]*SHCreateItemFromParsingName' -or
    $helperBody -notmatch 'SHCreateItemFromParsingName[\s\S]*GetDisplayName') {
    throw 'Shell re-entry invariant failed: display-name Shell calls are unguarded'
}
$callSiteSource = $source.Remove($helperStart, $helperEnd - $helperStart)
if ([regex]::Matches($callSiteSource, 'display_text_for_parsing_name\(').Count -ne
    [regex]::Matches($callSiteSource,
        'display_text_for_parsing_name\(\s*state\s*,').Count) {
    throw 'Shell re-entry invariant failed: state-less display-name caller exists'
}
Assert-Source 'std::wstring tab_display_text\(AppState& state' `
    'tab display text carries the Shell-call state'
$tabHelperStart = $source.IndexOf(
    'std::wstring tab_display_text(AppState& state')
$tabHelperEnd = $source.IndexOf(
    'void update_tab_strip_tooltips(', $tabHelperStart)
if ($tabHelperStart -lt 0 -or $tabHelperEnd -lt 0) {
    throw 'Shell re-entry invariant failed: tab display helper missing'
}
$tabCallSiteSource = $source.Remove($tabHelperStart,
    $tabHelperEnd - $tabHelperStart)
if ([regex]::Matches($tabCallSiteSource, 'tab_display_text\(').Count -ne
    [regex]::Matches($tabCallSiteSource,
        'tab_display_text\(\s*state\s*,').Count) {
    throw 'Shell re-entry invariant failed: state-less tab display caller exists'
}

Write-Output 'PASSED: shell_reentry_gate_check'
