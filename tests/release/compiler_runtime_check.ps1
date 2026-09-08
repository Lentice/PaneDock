param(
    [Parameter(Mandatory = $true)] [string] $AppPath,
    [Parameter(Mandatory = $true)] [string] $ObjdumpPath
)

$imports = & $ObjdumpPath -p $AppPath
if ($LASTEXITCODE -ne 0) {
    throw "llvm-objdump failed with exit code $LASTEXITCODE"
}

$forbidden = $imports | Select-String -Pattern 'DLL Name: (libc\+\+|libunwind)\.dll' -CaseSensitive:$false
if ($forbidden) {
    throw "PaneDock imports an LLVM runtime DLL: $($forbidden.Line.Trim())"
}

Write-Output 'PaneDock has no dynamic libc++ or libunwind dependency.'
