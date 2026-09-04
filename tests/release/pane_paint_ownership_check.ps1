param(
    [Parameter(Mandatory = $true)]
    [string] $SourcePath
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw
$start = $source.IndexOf('void paint_client_background(')
$end = $source.IndexOf('void cancel_session_save_timer(', $start)
if ($start -lt 0 -or $end -lt 0) {
    throw 'pane paint ownership check failed: paint_client_background bounds missing'
}
$body = $source.Substring($start, $end - $start)
if ($body -match 'draw_pane_card|layout_rects') {
    throw 'pane paint ownership check failed: main window paints pane content'
}

Write-Output 'pane paint ownership check passed'
