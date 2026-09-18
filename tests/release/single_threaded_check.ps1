param(
    [string] $SourcePath = (Join-Path $PSScriptRoot '..\..\src'),
    [string] $ComRefCountedPath = (Join-Path $PSScriptRoot '..\..\src\com_ref_counted.h')
)

# PD-213: the application is single-threaded STA, so the hazard is reentrancy
# and never concurrency (see AGENTS.md). That premise is what lets every ticket
# skip a whole class of cases -- so it needs a gate, or it gets violated a few
# tickets from now and nobody notices.
$ErrorActionPreference = 'Stop'

# src/com_ref_counted.h is the one deliberate exception: those objects are
# handed to shell32, which releases them at times we do not control.
$exempt = (Resolve-Path -LiteralPath $ComRefCountedPath).Path

$banned = @(
    'std::thread',
    'std::jthread',
    'std::mutex',
    'std::recursive_mutex',
    'std::shared_mutex',
    'std::lock_guard',
    'std::unique_lock',
    'std::scoped_lock',
    'std::condition_variable',
    'std::atomic',
    'CRITICAL_SECTION',
    'SRWLOCK',
    'InitializeCriticalSection',
    'CreateThread',
    '_beginthread',
    'QueueUserWorkItem',
    'TrySubmitThreadpoolCallback',
    'CreateThreadpoolWork'
)

$hits = @()
foreach ($file in Get-ChildItem -LiteralPath $SourcePath -File -Recurse -Include *.h, *.cpp) {
    if ($file.FullName -eq $exempt) { continue }
    foreach ($pattern in $banned) {
        $found = Select-String -LiteralPath $file.FullName -SimpleMatch -Pattern $pattern
        if ($found) {
            $hits += ("{0}:{1}: {2}" -f $file.FullName, $found[0].LineNumber, $pattern)
        }
    }
}

# `volatile` is checked separately: it is a substring of nothing useful here,
# but it must not match a comment that merely mentions the word.
foreach ($file in Get-ChildItem -LiteralPath $SourcePath -File -Recurse -Include *.h, *.cpp) {
    $found = Select-String -LiteralPath $file.FullName -Pattern '(^|[^\w])volatile\s+\w'
    if ($found) {
        $hits += ("{0}:{1}: volatile" -f $file.FullName, $found[0].LineNumber)
    }
}

if ($hits.Count -ne 0) {
    throw ("single-threaded invariant failed: concurrency machinery in src/" +
           [Environment]::NewLine + ($hits -join [Environment]::NewLine) +
           [Environment]::NewLine +
           'The hazard in this application is reentrancy, not concurrency. ' +
           'Use ShellCallScope / shell_call_depth, not a lock. ' +
           'If a new trust boundary genuinely needs an atomic, exempt it here ' +
           'and say why in the ticket.')
}

if ((Get-Content -LiteralPath $ComRefCountedPath -Raw) -notmatch 'std::atomic') {
    throw ('single-threaded invariant failed: com_ref_counted.h no longer uses ' +
           'an atomic. Its reference count is released by shell32, not by us; ' +
           'that is a trust boundary and must stay atomic.')
}

Write-Output 'PASSED: single_threaded_check'
