param(
    [Parameter(Mandatory = $true)]
    [string] $SourcePath
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw

function Assert-Source([string] $Pattern, [string] $Name) {
    if ($source -notmatch $Pattern) {
        throw "startup frame order check failed: $Name"
    }
}

Assert-Source 'bool\s+startup_frame_only\{\};' 'frame-only state exists'
Assert-Source 'if\s*\(!state\.realized\[index\]\s*&&\s*!state\.startup_frame_only' 'WM_CREATE blocks Shell realization'
Assert-Source 'ShowWindow\(window,[\s\S]*?UpdateWindow\(window\);\s*state\.startup_frame_only\s*=\s*false;' 'frame is shown before Shell gate opens'
Assert-Source 'const HRESULT active_result = apply_layout\(window, state\);' 'startup pass realizes active layout first'
Assert-Source 'state\.startup_realize_pending\s*=\s*false;\s*const HRESULT remaining_result = apply_layout\(window, state, true\);' 'remaining panes follow active pane'
Assert-Source 'case kDeferredRealizeMessage:[\s\S]*?realize_startup_panes\(window, \*state\)' 'deferred message owns startup realization'

Write-Output 'startup frame order check passed'
