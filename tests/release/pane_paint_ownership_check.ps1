param(
    [Parameter(Mandatory = $true)]
    [string] $SourcePath,
    [Parameter(Mandatory = $true)]
    [string] $PaneSourcePath
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw
$paneSource = Get-Content -LiteralPath $PaneSourcePath -Raw
$start = $source.IndexOf('void paint_client_background(')
# End at paint_client_background's own closing brace. Anchoring on the next
# function's name (cancel_session_save_timer, which has nothing to do with
# painting) meant moving that unrelated function broke this check.
$end = if ($start -lt 0) { -1 } else { $source.IndexOf("`n}`n", $start) }
if ($start -lt 0 -or $end -lt 0) {
    throw 'pane paint ownership check failed: paint_client_background bounds missing'
}
$body = $source.Substring($start, $end - $start)
if ($body -match 'draw_pane_card|layout_rects') {
    throw 'pane paint ownership check failed: main window paints pane content'
}

$colorStart = $paneSource.IndexOf('std::optional<LRESULT> Pane::color_address_bar(')
$colorEnd = $paneSource.IndexOf('void Pane::refresh_navigation_chrome(', [Math]::Max(0, $colorStart))
$regionStart = $paneSource.IndexOf('void Pane::apply_container_region(')
$regionEnd = $paneSource.IndexOf('bool Pane::set_rect(', [Math]::Max(0, $regionStart))
if ($colorStart -lt 0 -or $colorEnd -lt $colorStart -or
    $regionStart -lt 0 -or $regionEnd -lt $regionStart) {
    throw 'pane paint ownership check failed: pane color/region bodies missing'
}
$colorBody = $paneSource.Substring($colorStart, $colorEnd - $colorStart)
$regionBody = $paneSource.Substring($regionStart, $regionEnd - $regionStart)
if ($colorBody -notmatch 'control != address_bar\(\)' -or
    $colorBody -notmatch 'SetBkMode\(dc, OPAQUE\)' -or
    $colorBody -notmatch 'SetBkColor\(dc, RGB\(251, 252, 253\)\)' -or
    $colorBody -notmatch 'SetTextColor\(dc, RGB\(76, 89, 107\)\)' -or
    $colorBody -notmatch 'address_bar_background_brush\(\)' -or
    $paneSource -notmatch 'static HBRUSH brush = CreateSolidBrush\(RGB\(251, 252, 253\)\)' -or
    $source -notmatch 'case WM_CTLCOLOREDIT:\s*return chrome.color_address_bar\(' -or
    [regex]::Matches($source, 'Pane::release_address_bar_background_brush\(\);').Count -ne 1) {
    throw 'pane paint ownership check failed: address colors/shared brush wiring changed'
}
if ($regionBody -notmatch 'const HWND container = explorer_container_;' -or
    $regionBody -notmatch 'CreateRoundRectRgn\(0, 0, width, height, radius, radius\)' -or
    $regionBody -notmatch 'CreateRectRgn\(0, 0, width, radius\)' -or
    $regionBody -notmatch 'CombineRgn\(region, rounded, top_strip, RGN_OR\)' -or
    $regionBody -notmatch 'SetWindowRgn\(container, region, FALSE\)' -or
    $regionBody -match 'SetWindowRgn\([^;]*TRUE' -or
    $source -match 'void apply_container_region\(|HBRUSH address_bar_background_brush\(') {
    throw 'pane paint ownership check failed: container clipping must stay in Pane without immediate repaint'
}

Write-Output 'pane paint ownership check passed'
