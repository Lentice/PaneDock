param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp'),
    [string] $PaneSourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\pane.cpp'),
    [string] $ShellCoreSourcePath = (Join-Path $PSScriptRoot '..\..\src\shell_core\shell_core.cpp'),
    [string] $ExplorerHostSourcePath = (Join-Path $PSScriptRoot '..\..\src\explorer_host\explorer_host.cpp'),
    [string] $ExplorerHostHeaderPath = (Join-Path $PSScriptRoot '..\..\src\explorer_host\explorer_host.h')
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw
$paneSource = Get-Content -LiteralPath $PaneSourcePath -Raw
$shellCoreSource = Get-Content -LiteralPath $ShellCoreSourcePath -Raw
$explorerHostSource = Get-Content -LiteralPath $ExplorerHostSourcePath -Raw
$explorerHostHeader = Get-Content -LiteralPath $ExplorerHostHeaderPath -Raw

function Assert-Source([string] $Pattern, [string] $Name) {
    if ($source -notmatch $Pattern) {
        throw "Shell re-entry invariant failed: $Name"
    }
}

Assert-Source 'unsigned&\s+shell_call_depth\s*=\s*shutdown_sequence\.state\(\)\.shell_call_depth;' `
    'Shell call depth is owned by the reducer'
Assert-Source 'constexpr UINT kDeferredShutdownMessage' `
    'shutdown has a posted continuation message'
Assert-Source 'state\.shell_call_depth != 0' `
    'close defers while a Shell call is active'
Assert-Source 'PostMessageW\(state\.main_window, kDeferredShutdownMessage' `
    'Shell scope queues deferred shutdown after re-entry'
Assert-Source 'state->closing_ \|\| state->shutdown_deferred' `
    'main-window work is blocked during deferred teardown'
Assert-Source 'case kDeferredShutdownMessage:' `
    'deferred teardown is resumed by the message loop'
Assert-Source 'constexpr UINT kDeferredCommandMessage' `
    'Shell re-entry commands have a deferred message'
Assert-Source 'case kDeferredCommandMessage:' `
    'deferred commands return through the message loop'
Assert-Source 'message == WM_COMMAND[\s\S]*kDeferredCommandMessage' `
    'model-changing commands are deferred during Shell re-entry'
Assert-Source 'message == kTabStripSelectionMessage[\s\S]*kDeferredTabSelectionMessage' `
    'tab mutations are deferred during Shell re-entry'
Assert-Source 'defer_shell_reentry_mouse_message' `
    'Group/tab drag completion is deferred during Shell re-entry'

$windowProcStart = $source.IndexOf('LRESULT CALLBACK window_proc(')
$windowSwitch = $source.IndexOf('switch (message)', $windowProcStart)
$windowReentryGate = $source.IndexOf(
    'if (state != nullptr && state->shell_call_depth != 0)', $windowProcStart)
if ($windowProcStart -lt 0 -or $windowSwitch -lt 0 -or
    $windowReentryGate -lt 0 -or $windowReentryGate -gt $windowSwitch) {
    throw 'Shell re-entry invariant failed: interaction gate must precede main dispatch'
}
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
Assert-Source 'ShutdownEvent::file_operation_call_started[\s\S]*paste_from_clipboard' `
    'clipboard setup is not reported as an active transfer'
Assert-Source 'file_operation_setup_aborted[\s\S]*state\.shutdown_deferred\s*\|\|' `
    'clipboard setup observes deferred shutdown'
Assert-Source 'case WM_CLOSE:\s*if \(state != nullptr\)\s*run_shutdown_action\([\s\S]*?ShutdownEvent::close_requested' `
    'close decisions are routed through the shutdown reducer'

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
        $navigationHelperBody -notmatch 'state\.panes\[pane\]\.navigate_to\(') {
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

$navigateStart = $explorerHostSource.IndexOf(
    'HRESULT ExplorerHost::navigate(const core::ShellLocation& location)')
$navigateEnd = $explorerHostSource.IndexOf(
    'HRESULT ExplorerHost::refresh()', $navigateStart)
if ($navigateStart -lt 0 -or $navigateEnd -lt 0) {
    throw 'Shell re-entry invariant failed: value navigation body missing'
}
$navigateBody = $explorerHostSource.Substring(
    $navigateStart, $navigateEnd - $navigateStart)
if ($navigateBody -notmatch 'ShellCallScope shell_call\(\*this\);[\s\S]*shell_core::resolve_location') {
    throw 'Shell re-entry invariant failed: location resolution is unguarded'
}

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
$displayStart = $shellCoreSource.IndexOf(
    'std::wstring display_text_for_parsing_name(')
$displayEnd = $shellCoreSource.IndexOf(
    'std::optional<std::filesystem::path> session_directory()', $displayStart)
if ($displayStart -lt 0 -or $displayEnd -lt 0) {
    throw 'Shell re-entry invariant failed: shell_core display helper missing'
}
$displayBody = $shellCoreSource.Substring(
    $displayStart, $displayEnd - $displayStart)
if ($displayBody -notmatch 'CreateBindCtx\(0,\s*&bind_context\)' -or
    $displayBody -notmatch 'dwTickCountDeadline\s*=\s*GetTickCount\(\)\s*\+\s*kDisplayNameLookupTimeoutMs' -or
    $displayBody -notmatch 'bind_context->SetBindOptions' -or
    $displayBody -notmatch 'SHCreateItemFromParsingName\([\s\S]*bind_context\.Get\(\)') {
    throw 'Shell display lookup invariant failed: binding deadline is missing'
}
if ($displayBody -notmatch 'if \(!parsing_name\.starts_with\(L"::"\)\)\s*return\s+std::wstring\(parsing_name\)' -or
    ([regex]::Matches($displayBody, 'return\s+parsing_text;').Count -lt 3)) {
    throw 'Shell display lookup invariant failed: fallback behavior is missing'
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
    'void refresh_tab_strips(', $tabHelperStart)
if ($tabHelperStart -lt 0 -or $tabHelperEnd -lt 0) {
    throw 'Shell re-entry invariant failed: tab display helper missing'
}
$tabCallSiteSource = $source.Remove($tabHelperStart,
    $tabHelperEnd - $tabHelperStart)
# Exclude only the PaneHost bridge's declaration/signature, never its body:
# its free-function call must still pass the coordinator's Shell-call state.
$tabCallSiteSource = $tabCallSiteSource.Replace(
    'std::wstring tab_display_text(std::wstring_view parsing_name) override;', '')
$tabCallSiteSource = $tabCallSiteSource.Replace(
    'std::wstring AppState::tab_display_text(std::wstring_view parsing_name)', '')
Assert-Source 'std::wstring AppState::tab_display_text\(std::wstring_view parsing_name\)\s*\{\s*AppState &state = \*this;\s*return ::tab_display_text\(state, parsing_name\);\s*\}' `
    'PaneHost tab display bridge retains the Shell-call state'
if ([regex]::Matches($tabCallSiteSource, 'tab_display_text\(').Count -ne
    [regex]::Matches($tabCallSiteSource,
        'tab_display_text\(\s*state\s*,').Count) {
    throw 'Shell re-entry invariant failed: state-less tab display caller exists'
}

# PD-197 moved address display lookup and status counts into Pane. Keep their
# real Shell calls covered after removing the coordinator bridge.
$chromeStart = $paneSource.IndexOf('void Pane::refresh_navigation_chrome(')
$statusStart = $paneSource.IndexOf('void Pane::refresh_status_bar(')
$statusEnd = $paneSource.IndexOf('bool Pane::register_window_class(', $statusStart)
if ($chromeStart -lt 0 -or $statusStart -lt $chromeStart -or $statusEnd -lt $statusStart) {
    throw 'Shell re-entry invariant failed: Pane chrome/status bodies missing'
}
$chromeBody = $paneSource.Substring($chromeStart, $statusStart - $chromeStart)
$statusBody = $paneSource.Substring($statusStart, $statusEnd - $statusStart)
if ($chromeBody -notmatch 'ShellCall shell_call\(pane_host\(\)\);[\s\S]*panedock::shell_core::display_text_for_parsing_name' -or
    $chromeBody -notmatch 'current != bound') {
    throw 'Shell re-entry invariant failed: Pane address display lost its Shell gate or binding check'
}
if ($statusBody -notmatch 'ShellCall shell_call\(pane_host\(\)\);[\s\S]*item_counts\(counts\)' -or
    $statusBody -notmatch 'item_counts\(counts\);\s*}\s*if \(pane_host\(\)->is_shutting_down\(\)\) return;') {
    throw 'Shell re-entry invariant failed: Pane status counts lost their Shell gate or shutdown check'
}

Write-Output 'PASSED: shell_reentry_gate_check'
