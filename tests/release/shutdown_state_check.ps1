param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp'),
    [string] $PaneSourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\pane.cpp'),
    [string] $ShutdownHeaderPath = (Join-Path $PSScriptRoot '..\..\src\core\shutdown.h'),
    [string] $ShutdownSourcePath = (Join-Path $PSScriptRoot '..\..\src\core\shutdown.cpp')
)

$ErrorActionPreference = 'Stop'
$source = ($SourcePath -split ',' | ForEach-Object {
    Get-Content -LiteralPath $_ -Raw
}) -join "`n"
$source += "`n" + (Get-Content -LiteralPath $PaneSourcePath -Raw)
$shutdownHeader = Get-Content -LiteralPath $ShutdownHeaderPath -Raw
$shutdownSource = Get-Content -LiteralPath $ShutdownSourcePath -Raw

# The SessionWriter dirty-flag rule used to be checked here by scanning
# session_writer.cpp for the order of two assignments. panedock_session_writer
# now drives the real object against a real directory instead, so the source
# scan is gone rather than kept as a second, weaker copy of the same rule.

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
Assert-Source 'const bool&\s+shutdown_save_attempted\s*=\s*shutdown_coordinator\.state\(\)\.shutdown_save_attempted;' `
    'app shell forwards the final save attempt flag'
Assert-Source 'const bool&\s+shutdown_clean_marker_armed\s*=\s*shutdown_coordinator\.state\(\)\.shutdown_clean_marker_armed;' `
    'app shell forwards the clean marker flag'
Assert-Source 'const bool&\s+main_window_destroyed\s*=\s*shutdown_coordinator\.state\(\)\.main_window_destroyed;' `
    'app shell forwards the window destruction flag'
Assert-Source 'const bool&\s+end_session_pending\s*=\s*shutdown_coordinator\.state\(\)\.end_session_pending;' `
    'app shell forwards the confirmed session-end flag'
Assert-Source 'session\.dirty\(\)\s*&&\s*!state->shutdown_save_attempted' `
    'WM_DESTROY only saves before a final attempt'

# PD-216: the order of the shutdown effects -- caption before the posted
# continuation, save before views, views before the window, keep-open only on
# an explicit No, a session end inside the prompt winning -- is executed by
# core_shutdown_coordinator_test against ShutdownCoordinator. What is left here
# is what the Win32 adapter itself must do inside each effect.
Assert-Source 'bool AppShutdownEffects::save_session\(\) noexcept \{[\s\S]*?capture_window_placement\(state_\.main_window, state_\);[\s\S]*?return save_now\(state_, false, true\);' `
    'the shutdown save captures placement and overrides capture suppression'
Assert-Source 'ShutdownEvent::window_destroyed[\s\S]*?ShutdownEvent::save_started[\s\S]*?save_now\(\*state, false, true\)' `
    'unexpected destroy fallback keeps marker false'
Assert-Shutdown 'case ShutdownEvent::save_prompt_finished:[\s\S]*?state_\.end_session_pending\s*\?\s*ShutdownAction::destroy_views' `
    'nested confirmed shutdown is remembered'
Assert-Source 'void\s+set_main_window_title\(HWND window, bool diagnostic_mode,\s*bool closing\)[\s\S]*?L"PaneDock.*Closing\.\.\."' `
    'closing caption has a dedicated update path'
# The six teardown steps of finish_shutdown are the concrete shape of the rule
# "never destroy the parent HWND while a view is alive". A single
# title-then-destroy_panes regex only pinned two of them: moving DestroyWindow
# ahead of destroy_panes, or dropping the drag-target revoke, still passed.
# Assert the whole order instead.
$finishStart = $source.IndexOf('void AppShutdownEffects::destroy_views(bool session_ending) noexcept {')
if ($finishStart -lt 0) {
    throw 'shutdown state invariant failed: AppShutdownEffects::destroy_views is missing'
}
$finishEnd = $source.IndexOf(
    'void AppShutdownEffects::destroy_window() noexcept {', $finishStart)
if ($finishEnd -lt 0) {
    throw 'shutdown state invariant failed: destroy_views body end is missing'
}
$finishBody = $source.Substring($finishStart, $finishEnd - $finishStart)
$finishOrder = @(
    @{ Pattern = 'set_main_window_title\(window, state\.diagnostic_mode, true\);'
       Name = 'closing caption before teardown' },
    @{ Pattern = 'transfer_close_dialog\.destroy\(\);'
       Name = 'dialogs destroyed before panes' },
    @{ Pattern = 'revoke_drag_hover_targets\(state\);'
       Name = 'drag targets revoked before panes' },
    @{ Pattern = 'cancel_session_save_timer\(state\);'
       Name = 'save timer cancelled before panes' },
    @{ Pattern = 'destroy_panes\(state\);'
       Name = 'panes destroyed' },
    @{ Pattern = 'write_live_view_count\(state\.diagnostic_mode\);'
       Name = 'live view count emitted after teardown' }
)
$previousIndex = -1
$previousName = 'start of destroy_views'
foreach ($step in $finishOrder) {
    $match = [regex]::Match($finishBody, $step.Pattern)
    if (-not $match.Success) {
        throw "shutdown state invariant failed: destroy_views is missing '$($step.Name)'"
    }
    if ($match.Index -lt $previousIndex) {
        throw ("shutdown state invariant failed: '$($step.Name)' must come " +
               "after '$previousName' in destroy_views")
    }
    $previousIndex = $match.Index
    $previousName = $step.Name
}
Assert-Shutdown 'state_\.shutdown_deferred\s*=\s*true;' `
    'reducer records deferred shutdown'
Assert-Source 'bool AppShutdownEffects::post_deferred_shutdown\(\) noexcept \{[\s\S]*?PostMessageW\(state_\.main_window, kDeferredShutdownMessage' `
    'closing state yields to the message loop before teardown'
Assert-Source 'state_\.transfer_close_dialog\.show\(state_\.main_window\)' `
    'transfer prompt is owned by its dialog module'
Assert-Source 'void\s+handle_transfer_close_dialog_result\(AppState& state\)[\s\S]*?take_result\(\)' `
    'transfer choice is reduced by the app shell coordinator'
Assert-Source 'return answer != IDNO;' `
    'only explicit No closes after an interactive failure'
Assert-Shutdown 'case ShutdownEvent::save_keep_open:[\s\S]*?state_\.shutdown_save_attempted = false;' `
    'keeping the window open resets the failed save attempt'
Assert-Source 'case WM_ENDSESSION:[\s\S]*?run_end_session_shutdown\(\*state\)' `
    'WM_ENDSESSION records confirmation'

# The marker-after-COM ordering now lives inside finalize_process, the single
# tail both exits run: wWinMain's, and the WM_ENDSESSION handler, which never
# returns to the message loop because Windows terminates the process.
Assert-Source 'void finalize_process\(AppState& state\) noexcept \{[\s\S]*?OleUninitialize\(\);[\s\S]*?write_clean_marker\(state\.application\)' `
    'clean marker is written after OleUninitialize'
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

# PD-203: at an OS session end the durable checkpoint must be written before
# anything that can block or bail, because Windows kills the process partway
# through the Shell teardown that follows. That the checkpoint comes first and
# the teardown after it stays gated is executed by core_shutdown_coordinator_
# test (PD-216); what is left is that the checkpoint itself reaches no Shell.
$checkpoint = Get-FunctionSource `
    'bool AppShutdownEffects::write_end_session_checkpoint() noexcept {' `
    'write_end_session_checkpoint'
$checkpointCode = [regex]::Replace($checkpoint, '//[^
]*', '')
foreach ($escape in @('destroy', 'capture_locations', 'save_now',
                      'finalize_process')) {
    if ($checkpointCode -match ('(?<![A-Za-z_])' + [regex]::Escape($escape) + '(?![A-Za-z_])')) {
        throw ("shutdown state invariant failed: '$escape' is reached by " +
               'the end-session checkpoint')
    }
}
if ($checkpointCode -notmatch 'capture_window_placement\(state_\.main_window, state_\);[\s\S]*session\.write\(state_\.application, true, state_\.main_window\)') {
    throw ('shutdown state invariant failed: the end-session checkpoint must ' +
           'capture placement and write a clean checkpoint')
}

# finalize_process is the single COM-then-marker tail, run once.
$finalize = Get-FunctionSource `
    'void finalize_process(AppState& state) noexcept {' 'finalize_process'
if ($finalize -notmatch 'if \(state\.ole_finalized\) return;[\s\S]*?state\.ole_finalized = true;[\s\S]*?OleUninitialize\(\);[\s\S]*?write_clean_marker\(state\.application\)') {
    throw ('shutdown state invariant failed: finalize_process must guard on ' +
           'ole_finalized and write the marker after OleUninitialize')
}
$finalizeCalls = ([regex]::Matches($source, '(?<![A-Za-z_])finalize_process\(state\);')).Count
if ($finalizeCalls -ne 2) {
    throw ("shutdown state invariant failed: expected 2 finalize_process call " +
           "sites (wWinMain tail and end-session path), found $finalizeCalls")
}

# The "Closing..." caption is for a user who is watching; an OS session end
# has none, and its synchronous non-client repaint is spent against the one
# path with a kill timeout. The coordinator passes session_ending (tested).
$destroyViews = Get-FunctionSource `
    'void AppShutdownEffects::destroy_views(bool session_ending) noexcept {' 'destroy_views'
if ($destroyViews -notmatch 'if \(!session_ending\) set_main_window_title\(window, state\.diagnostic_mode, true\);') {
    throw ('shutdown state invariant failed: destroy_views must skip the ' +
           'closing caption only at an OS session end')
}

function Assert-DebouncedFunction([string] $Start, [string] $Name) {
    $body = Get-FunctionSource $Start $Name
    if ($body -notmatch 'schedule_session_save\(' -or
        $body -match 'save_now\(') {
        throw "session save debounce check failed: $Name writes synchronously"
    }
}

Assert-DebouncedFunction 'void Pane::finish_tab_change(' 'Pane::finish_tab_change'
foreach ($command in @('switch_active_tab', 'add_tab', 'close_tab')) {
    $body = Get-FunctionSource "void Pane::$command(" "Pane::$command"
    if ($body -notmatch 'finish_tab_change\(' -or $body -match 'save_now\(') {
        throw "session save debounce check failed: Pane::$command must use finish_tab_change"
    }
}
$closeTabsBody = Get-FunctionSource 'void Pane::close_tabs(' 'Pane::close_tabs'
if ($closeTabsBody -notmatch 'std::vector<std::string> tab_ids;' -or
    $closeTabsBody -notmatch 'for \(const auto& id_to_close : tab_ids\)\s*close_tab\(id_to_close\)' -or
    $closeTabsBody -match 'save_now\(|core::close_tab\(') {
    throw 'session save debounce check failed: batch close must snapshot IDs and reuse Pane::close_tab'
}
# delete_group and set_layout no longer save directly: every Group mutation
# runs the one shared script, so the debounce is asserted where it now lives.
Assert-DebouncedFunction 'void perform_group_transition(' 'perform_group_transition'
Assert-DebouncedFunction 'void move_group(' 'move_group'
Assert-DebouncedFunction 'void set_active_pane(' 'set_active_pane'
Assert-DebouncedFunction 'void Pane::set_view_mode(' 'Pane::set_view_mode'
$pinBody = Get-FunctionSource 'void Pane::pin_current_folder(' 'Pane::pin_current_folder'
if ($pinBody.IndexOf('pane_host()->pin_location') -lt 0 -or
    $pinBody.IndexOf('save_now(') -ge 0) {
    throw 'session save debounce check failed: Pane::pin_current_folder does not delegate persistence'
}
Assert-DebouncedFunction 'void finish_tab_drag(' 'finish_tab_drag'
# The group reorder gesture lives in Sidebar, but committing it stays
# coordinator work: Sidebar reports the finished drag, group_list_proc applies
# it to the model and debounces the save like every other mutation.
$groupReorderBody = Get-FunctionSource 'LRESULT CALLBACK group_list_proc(' 'group_list_proc'
if ($groupReorderBody -notmatch 'sidebar\.take_reorder_request\(\)' -or
    $groupReorderBody -notmatch 'core::reorder_group\(' -or
    $groupReorderBody -notmatch 'schedule_session_save\(' -or
    $groupReorderBody -match 'save_now\(') {
    throw 'session save debounce check failed: group reorder must be applied by the coordinator and debounce its save'
}

$directSaveCalls = [regex]::Matches($source, 'save_now\((?:state|\*state)')
# PD-215: the timer-failure fallback moved into SessionWriter::schedule.
if ($directSaveCalls.Count -ne 4) {
    throw "session save debounce check failed: expected 4 synchronous save sites, found $($directSaveCalls.Count)"
}

$timerStart = $source.IndexOf('if (timer == kSessionSaveTimerId)')
$timerEnd = $source.IndexOf(
    'if (timer == kDragHoverSidebarTimerId', $timerStart)
if ($timerStart -lt 0 -or $timerEnd -lt 0) {
    throw 'session save debounce check failed: session save timer branch is missing'
}
$timerBody = $source.Substring($timerStart, $timerEnd - $timerStart)
$timerWrites = [regex]::Matches($timerBody, 'save_now\(').Count
if ($timerWrites -ne 1 -or $timerBody -notmatch 'session\.cancel_timer\(window\)') {

    throw 'session save debounce check failed: timer does not perform one post-debounce write'
}

$simulatedBurstSize = 5
if ($timerWrites -ge $simulatedBurstSize) {
    throw 'session save debounce check failed: a five-operation burst is not coalesced'
}

Write-Output 'PASSED: session_save_debounce_check'
Write-Output 'PASSED: shutdown_state_check'
