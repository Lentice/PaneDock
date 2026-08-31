param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp'),
    [string] $ShellCoreSourcePath = (Join-Path $PSScriptRoot '..\..\src\shell_core\shell_core.cpp'),
    [string] $ExplorerHostSourcePath = (Join-Path $PSScriptRoot '..\..\src\explorer_host\explorer_host.cpp'),
    [string] $ExplorerHostHeaderPath = (Join-Path $PSScriptRoot '..\..\src\explorer_host\explorer_host.h')
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw
$shellCoreSource = Get-Content -LiteralPath $ShellCoreSourcePath -Raw
$explorerHostSource = Get-Content -LiteralPath $ExplorerHostSourcePath -Raw
$explorerHostHeader = Get-Content -LiteralPath $ExplorerHostHeaderPath -Raw

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
Assert-Source 'PostMessageW\(state\.main_window, kDeferredShutdownMessage' `
    'Shell scope queues deferred shutdown after re-entry'
Assert-Source 'state->closing_ \|\| state->shutdown_deferred' `
    'main-window work is blocked during deferred teardown'
Assert-Source 'case kDeferredShutdownMessage:' `
    'deferred teardown is resumed by the message loop'
Assert-Source 'ShellCallScope shell_call\(state\)' `
    'ExplorerHost callers use the shared Shell-call gate'
Assert-Source 'set_shell_call_callback\(\s*&state,\s*app_shell_call_state_changed\)' `
    'ExplorerHost receives the app Shell-call gate before initialization'
Assert-Source 'void finish_shell_call\(AppState& state\)' `
    'app Shell-call leave logic is shared with callback entry'
Assert-Source 'finish_shell_call\(state_\)' `
    'RAII ShellCallScope uses the shared leave logic'
Assert-Source 'finish_shell_call\(state\)' `
    'ExplorerHost callback leave uses the shared leave logic'
Assert-Source 'bool\s+navigate_realized_panes\(\s*AppState& state,\s*const panedock::core::GroupState& group\)\s*noexcept' `
    'Group transitions share realized-pane navigation'
Assert-Source 'state\.file_operation_in_progress\s*=\s*false;[\s\S]*paste_from_clipboard' `
    'clipboard setup is not reported as an active transfer'
Assert-Source 'file_operation_setup_aborted[\s\S]*state\.shutdown_deferred\s*\|\|' `
    'clipboard setup observes deferred shutdown'
Assert-Source 'case WM_CLOSE:[\s\S]*if \(state->file_operation_in_progress\)[\s\S]*begin_shutdown' `
    'close defers Shell setup but prompts only for an active transfer'

$navigationHelperStart = $source.IndexOf('bool navigate_realized_panes(')
$navigationHelperEnd = $source.IndexOf(
    'RECT to_win32_rect(const panedock::core::PaneRect& rect)', $navigationHelperStart)
if ($navigationHelperStart -lt 0 -or $navigationHelperEnd -lt 0) {
    throw 'Shell re-entry invariant failed: realized-pane navigation helper body missing'
}
$navigationHelperBody = $source.Substring(
    $navigationHelperStart, $navigationHelperEnd - $navigationHelperStart)
if ($navigationHelperBody -notmatch 'state\.suppress_location_capture\s*=\s*true' -or
    $navigationHelperBody -notmatch 'ShellCallScope shell_call\(state\)' -or
    $navigationHelperBody -notmatch 'state\.explorers\[pane\]\.navigate') {
    throw 'Shell re-entry invariant failed: realized-pane navigation helper is incomplete'
}
$navigationCallSiteSource = $source.Remove(
    $navigationHelperStart, $navigationHelperEnd - $navigationHelperStart)
if ([regex]::Matches(
        $navigationCallSiteSource,
        'navigate_realized_panes\(\s*state,\s*group\s*\)').Count -ne 2) {
    throw 'Shell re-entry invariant failed: both Group transitions must use the shared helper'
}

function Assert-ExplorerHostSource([string] $Pattern, [string] $Name) {
    if ($explorerHostSource -notmatch $Pattern) {
        throw "Shell re-entry invariant failed: $Name"
    }
}

if ($explorerHostHeader -notmatch 'using\s+ShellCallCallback\s*=\s*void\s*\(\*\)\(void\*\s+context,\s*bool\s+entering\)\s*noexcept') {
    throw 'Shell re-entry invariant failed: ExplorerHost exposes a non-owning Shell-call callback'
}
Assert-ExplorerHostSource 'ExplorerHost::ShellCallScope::ShellCallScope' `
    'ExplorerHost exposes a non-owning Shell-call callback'
Assert-ExplorerHostSource 'set_shell_call_callback\(\s*void\*\s+context,\s*ShellCallCallback\s+callback\)\s*noexcept' `
    'ExplorerHost stores the app gate without a new COM abstraction'

$navigationStart = $explorerHostSource.IndexOf(
    'void ExplorerHost::navigation_complete(PCIDLIST_ABSOLUTE pidl) noexcept')
$navigationEnd = $explorerHostSource.IndexOf(
    'void ExplorerHost::navigation_failed() noexcept', $navigationStart)
if ($navigationStart -lt 0 -or $navigationEnd -lt 0) {
    throw 'Shell re-entry invariant failed: navigation callback bodies missing'
}
$navigationBody = $explorerHostSource.Substring(
    $navigationStart, $navigationEnd - $navigationStart)
if ($navigationBody -notmatch 'ShellCallScope shell_call\(\*this\);[\s\S]*view_window') {
    throw 'Shell re-entry invariant failed: navigation_complete is unguarded'
}
$failedStart = $navigationEnd
$failedEnd = $explorerHostSource.IndexOf(
    'void ExplorerHost::destroy() noexcept', $failedStart)
if ($failedEnd -lt 0) {
    throw 'Shell re-entry invariant failed: navigation_failed body missing'
}
$failedBody = $explorerHostSource.Substring($failedStart, $failedEnd - $failedStart)
if ($failedBody -notmatch 'ShellCallScope shell_call\(\*this\);') {
    throw 'Shell re-entry invariant failed: navigation_failed is unguarded'
}

$viewCallbackStart = $explorerHostSource.IndexOf(
    'HRESULT STDMETHODCALLTYPE MessageSFVCB')
$viewCallbackEnd = $explorerHostSource.IndexOf(
    'private:', $viewCallbackStart)
if ($viewCallbackStart -lt 0 -or $viewCallbackEnd -lt 0) {
    throw 'Shell re-entry invariant failed: ViewCallback body missing'
}
$viewCallbackBody = $explorerHostSource.Substring(
    $viewCallbackStart, $viewCallbackEnd - $viewCallbackStart)
if ($viewCallbackBody -notmatch 'ShellCallScope shell_call\(\*host_\);[\s\S]*previous_->MessageSFVCB') {
    throw 'Shell re-entry invariant failed: ViewCallback callback chain is unguarded'
}

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
    $helperBody -notmatch 'ShellCallScope shell_call\(state\);[\s\S]*panedock::shell_core::display_text_for_parsing_name') {
    throw 'Shell re-entry invariant failed: display-name Shell calls are unguarded'
}
if ($shellCoreSource -notmatch 'SHCreateItemFromParsingName[\s\S]*GetDisplayName') {
    throw 'Shell re-entry invariant failed: shell_core display lookup is incomplete'
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
