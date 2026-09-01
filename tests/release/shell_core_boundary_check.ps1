param(
    [string] $AppSourcePath = (Join-Path $PSScriptRoot '..\..\src\app_shell\main.cpp'),
    [string] $ShellCoreHeaderPath = (Join-Path $PSScriptRoot '..\..\src\shell_core\shell_core.h'),
    [string] $ShellCoreSourcePath = (Join-Path $PSScriptRoot '..\..\src\shell_core\shell_core.cpp'),
    [string] $CorePath = (Join-Path $PSScriptRoot '..\..\src\core')
)

$ErrorActionPreference = 'Stop'
$appSource = Get-Content -LiteralPath $AppSourcePath -Raw
$shellCoreHeader = Get-Content -LiteralPath $ShellCoreHeaderPath -Raw
$shellCoreSource = Get-Content -LiteralPath $ShellCoreSourcePath -Raw

if ($appSource -match 'SHGetKnownFolderPath|GetDisplayName') {
    throw 'shell_core boundary failed: app_shell owns a location/value Shell API'
}
if ($appSource -match 'SHCreateItemFromParsingName') {
    throw 'shell_core boundary failed: app_shell owns Shell item parsing'
}
if ($shellCoreHeader -match 'IUnknown|IShellItem|ITEMIDLIST|PIDL|ComPtr|windows\.h') {
    throw 'shell_core boundary failed: public API exposes Shell ownership'
}
foreach ($required in @('SHGetKnownFolderPath', 'SHCreateItemFromParsingName', 'GetDisplayName')) {
    if ($shellCoreSource -notmatch $required) {
        throw "shell_core boundary failed: missing implementation $required"
    }
}
foreach ($required in @('FindFolderFromIDList', 'GetShellItem',
                         'SIGDN_FILESYSPATH', 'resolve_location')) {
    if ($shellCoreSource -notmatch $required) {
        throw "shell_core boundary failed: missing location identity operation $required"
    }
}
if ($appSource -match 'FindFolderFromIDList|SIGDN_FILESYSPATH|CLSIDFromString') {
    throw 'shell_core boundary failed: app_shell reimplements Shell identity logic'
}
$coreLeak = Get-ChildItem -LiteralPath $CorePath -File -Recurse |
    Select-String -Pattern 'windows\.h|IUnknown|IShellItem|ITEMIDLIST|PIDL|ComPtr'
if ($coreLeak) {
    throw 'shell_core boundary failed: src/core contains Windows or COM types'
}

Write-Output 'PASSED: shell_core_boundary_check'
