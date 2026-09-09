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
Assert-Source 'child_message_blocked_while_closing\(\s*state->is_shutting_down\(\),\s*message\)' `
    'child procs share one teardown allowlist'
Assert-Source 'case kDeferredShutdownMessage:' `
    'deferred teardown is resumed by the message loop'
Assert-Source 'constexpr UINT kDeferredCommandMessage' `
    'Shell re-entry commands have a deferred message'
Assert-Source 'case kDeferredCommandMessage:' `
    'deferred commands return through the message loop'
Assert-Source 'message == WM_COMMAND[\s\S]*kDeferredCommandMessage' `
    'model-changing commands are deferred during Shell re-entry'
# Tab activation and creation are the pane's own: its strip acts on itself,
# and the mouse message that triggers it is what carries the re-entry gate.
if ($paneSource -notmatch 'void Pane::activate_tab_at\(std::size_t item\)' -or
    $paneSource -notmatch 'void Pane::add_default_tab\(\)' -or
    $paneSource -notmatch 'void Pane::close_tab_at_screen\(POINT screen\)') {
    throw 'Shell re-entry invariant failed: tab activation/creation/close must live in Pane'
}
$stripSource = Get-Content -LiteralPath (
    Join-Path $PSScriptRoot '..\..\src\app_shell\pane_tab_strip.cpp') -Raw
if ($stripSource -match 'SendMessageW\(GetParent\(GetParent\(tab_strip_\)\)' -or
    $stripSource -notmatch 'owner_->add_default_tab\(\)') {
    throw 'Shell re-entry invariant failed: the tab strip must act on its own pane, not the coordinator'
}
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
Assert-Source 'file_operation_setup_aborted[\s\S]*state\.is_shutting_down\(\)' `
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
# Both Group transitions now reach the helper through the single
# perform_group_transition script, so there is exactly one call site left and
# it looks the Group up fresh rather than holding a reference across re-entry.
if ([regex]::Matches(
        $navigationCallSiteSource,
        'navigate_realized_panes\(state, active_group\(state\)\)').Count -ne 1 -or
    [regex]::Matches($navigationCallSiteSource,
        'navigate_realized_panes\(').Count -ne 1) {
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
    $statusBody -notmatch 'item_counts\(counts\);\s*}\s*if \(!active\(\)\) return;') {
    throw 'Shell re-entry invariant failed: Pane status counts lost their Shell gate or shutdown check'
}

# PD-198: popup selection still returns through main WM_COMMAND; only the
# selected pane's option execution moves behind PaneHost's existing gate.
$commandStart = $paneSource.IndexOf('bool Pane::handle_command(')
$commandEnd = $paneSource.IndexOf('bool Pane::draw_control(', [Math]::Max(0, $commandStart))
if ($commandStart -lt 0 -or $commandEnd -lt $commandStart) {
    throw 'Shell re-entry invariant failed: Pane command body missing'
}
$commandBody = $paneSource.Substring($commandStart, $commandEnd - $commandStart)
if ([regex]::Matches($commandBody, 'ShellCall shell_call\(pane_host\(\)\);\s*\(void\)navigate_to\(').Count -ne 2 -or
    $commandBody -notmatch '!active\(\)' -or
    $commandBody -match 'show_pinned_locations_manager') {
    throw 'Shell re-entry invariant failed: Pane pinned options lost their gate or own the app dialog'
}
Assert-Source 'state\.panes\[command\.pane\]\.handle_command\(id\)' 'popup commands dispatch to the decoded pane'
# The tab strip menu is the pane's own feature: the pane hit-tests, shows it
# and closes the tabs itself. The coordinator only supplies the re-entry gate,
# so WM_CONTEXTMENU must be on the deferral list and reach the pane through it.
Assert-Source 'message != WM_LBUTTONUP && message != WM_CONTEXTMENU' `
    'pane context menus are deferred during Shell re-entry'
if ($paneSource -notmatch 'case WM_CONTEXTMENU: \{[\s\S]*?pane->tab_strip\(\)[\s\S]*?handle_pane_control_message\([\s\S]*?pane->handle_tab_context_menu\(screen\);') {
    throw 'Shell re-entry invariant failed: the pane must run its own tab popup behind the host gate'
}
if ($paneSource -notmatch 'void Pane::handle_tab_context_menu\(POINT screen\)[\s\S]*?tab_at_screen\(screen\)[\s\S]*?TrackPopupMenu\([\s\S]*?close_tab\(tab_id\);[\s\S]*?close_tabs\(tab_id, command\);') {
    throw 'Shell re-entry invariant failed: Pane must own tab popup hit-test, display and close'
}

# main.cpp has no standalone behavioral seam. This checks the empty-state
# teardown wiring; the live lifetime check separately exercises Destroy.
$emptyLayoutStart = $source.IndexOf('if (!has_active_group(state)) {',
    $source.IndexOf('HRESULT apply_layout('))
$emptyLayoutEnd = $source.IndexOf('ShowWindow(state.empty_message, SW_HIDE)',
    $emptyLayoutStart)
if ($emptyLayoutStart -lt 0 -or $emptyLayoutEnd -lt $emptyLayoutStart -or
    $source.Substring($emptyLayoutStart, $emptyLayoutEnd - $emptyLayoutStart) -notmatch
        'ShellCallScope shell_call\(state\);\s*state\.panes\[index\]\.derealize\(\)') {
    throw 'Empty Group state must release every realized view before reuse'
}

# A pane's buttons are its children, so only their BN_CLICKED WM_COMMAND
# reaches us -- at the pane window, which is not the main window that gates
# WM_COMMAND on shell_call_depth. Without this the nav/tab buttons mutate the
# model re-entrantly inside a pumping Shell call.
$paneControlStart = $source.IndexOf(
    'std::optional<LRESULT> AppState::handle_pane_control_message(')
if ($paneControlStart -lt 0) {
    throw 'Shell re-entry invariant failed: pane control message handler missing'
}
$paneControlSwitch = $source.IndexOf('switch (message)', $paneControlStart)
$paneControlGate = $source.IndexOf(
    'if (state->shell_call_depth != 0 && message == WM_COMMAND)',
    $paneControlStart)
if ($paneControlSwitch -lt 0 -or $paneControlGate -lt 0 -or
    $paneControlGate -gt $paneControlSwitch -or
    $source.Substring($paneControlGate, $paneControlSwitch - $paneControlGate) -notmatch
        'defer_shell_reentry_message\(pane_window, message, wparam, lparam\)') {
    throw 'Shell re-entry invariant failed: pane child WM_COMMAND must be deferred during Shell re-entry'
}

# WM_TIMER is not on the deferral list, so the session-save timer can fire
# inside a pumping Shell call and capture a pane mid-navigation. It must leave
# the timer armed rather than cancel-and-save.
$timerStart = $source.IndexOf('if (timer == kSessionSaveTimerId) {')
if ($timerStart -lt 0) {
    throw 'Shell re-entry invariant failed: session save timer branch missing'
}
$timerBody = $source.Substring($timerStart, 900)
if ($timerBody -notmatch 'if \(state->shell_call_depth != 0\) return 0;[\s\S]*state->session\.cancel_timer\(window\)') {
    throw 'Shell re-entry invariant failed: session save must not run inside a Shell call'
}

# MessageBoxW pumps with shell_call_depth at 0, so queued deferred commands
# replay inside the box and can delete or add Groups. delete_group therefore
# must carry the Group id across the box, never the selected index.
$deleteStart = $source.IndexOf('void delete_group(HWND window, AppState& state)')
$deleteEnd = $source.IndexOf('void move_group(', $deleteStart)
if ($deleteStart -lt 0 -or $deleteEnd -lt $deleteStart) {
    throw 'Shell re-entry invariant failed: delete_group body missing'
}
$deleteBody = $source.Substring($deleteStart, $deleteEnd - $deleteStart)
if ($deleteBody -notmatch 'const std::string id = state\.application\.groups\[\*selected\]\.id;[\s\S]*MessageBoxW') {
    throw 'Shell re-entry invariant failed: delete_group must resolve the Group id before the modal box'
}
if ($deleteBody -match 'MessageBoxW\(window,[\s\S]*groups\[\*selected\]') {
    throw 'Shell re-entry invariant failed: delete_group must not index by selection after the modal box'
}
if ($deleteBody -notmatch 'MessageBoxW\(window,[\s\S]*std::none_of\([\s\S]*group\.id == id[\s\S]*for \(auto& pane : state\.panes\) pane\.unbind\(\)') {
    throw 'Shell re-entry invariant failed: delete_group must re-check the Group still exists before unbinding'
}

# apply_layout is the only place holding a GroupState& across pumping Shell
# calls; the assert is what reports a regression in the WM_COMMAND deferral
# that makes it sound.
$applyStart = $source.IndexOf('HRESULT apply_layout(')
if ($applyStart -lt 0 -or
    $source.Substring($applyStart) -notmatch
        'group_count_on_entry =\s*state\.application\.groups\.size\(\);[\s\S]*assert\(state\.application\.groups\.size\(\) == group_count_on_entry\);') {
    throw 'Shell re-entry invariant failed: apply_layout must assert the groups vector did not move under its GroupState&'
}

Write-Output 'PASSED: shell_reentry_gate_check'
