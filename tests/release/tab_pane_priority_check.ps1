param(
    [string] $SourcePath = "$PSScriptRoot/../../src/app_shell/main.cpp"
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw
# main.cpp has no isolated message-loop seam. This checks routing order only;
# actual Shell focus movement still requires a desktop check.
$tab = [regex]::Match($source,
    'if \(key_down && !control && !alt && message.wParam == VK_TAB &&\s*!address_bar_has_focus\(state.panes\)\) \{[^}]+\}')
$shell = $source.IndexOf('state.panes[active].host().translate_accelerator(&message)')
if (!$tab.Success -or $shell -lt 0 -or $tab.Index -gt $shell -or
    $tab.Value -notmatch 'set_active_pane\(window, state, next\);\s*continue;') {
    throw 'Tab must switch panes and consume the key before Shell accelerator routing.'
}
Write-Output 'Tab pane priority check passed (source order only).'
