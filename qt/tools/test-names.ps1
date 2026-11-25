# Compares the names of the Kotlin @Test functions of the ported test classes with the names of the Qt Test functions of
# their test executables (`build/qt/<exe>.exe -functions`, without the trailing "()"). The classes are listed in
# qt/tools/test-classes.json: an array of { "kotlin": <the Kotlin test file, relative to the repository root>,
# "exe": <the Qt test executable's name, or an array of names when the port of one Kotlin class is split over several
# executables; their functions are then taken together>, "added": [<names the port adds on purpose, not in the Kotlin class>],
# and optionally "omitted": [{ "name": <a Kotlin test name the port leaves out on purpose>, "reason": <why> }] }.
#
#   pwsh qt/tools/test-names.ps1
#
# Per class it prints `Kotlin N, Qt M, missing 0, added K (names)`, and `, omitted O (names)` when the class omits any. The
# exit code is 1 when any Kotlin name that is not omitted has no Qt function, when a Qt function is neither a Kotlin name nor
# one of its class's `added` names, when an `added` name is not a Qt function of its class, when an `omitted` entry lacks its
# name or reason, names no Kotlin test of its class, or names one that is a Qt function after all, or when a Kotlin test file
# yields no name at all.
# Qt's own initTestCase/cleanupTestCase are not test names and are ignored.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$listFile = Join-Path $PSScriptRoot 'test-classes.json'

$classes = @(Get-Content -Raw -LiteralPath $listFile | ConvertFrom-Json)
if ($classes.Count -eq 0) { Write-Output "no classes in $listFile"; Write-Output 'FAILED'; exit 1 }

$failed = $false
$addedTotal = 0
$omittedTotal = 0
foreach ($spec in $classes) {
    if (-not $spec.kotlin -or -not $spec.exe -or $null -eq $spec.added) {
        Write-Output "an entry of $listFile lacks kotlin, exe or added: $($spec | ConvertTo-Json -Compress)"; $failed = $true; continue
    }
    $class = [System.IO.Path]::GetFileNameWithoutExtension($spec.kotlin)
    $allowed = @($spec.added)
    $omittedSpecs = @(if ($null -ne $spec.omitted) { $spec.omitted })
    $badOmitted = @($omittedSpecs | Where-Object { -not $_.name -or -not $_.reason })
    if ($badOmitted.Count -gt 0) {
        Write-Output "an omitted entry of $listFile lacks name or reason: $($badOmitted | ConvertTo-Json -Compress)"; $failed = $true; continue
    }
    $omitted = @($omittedSpecs | ForEach-Object { [string]$_.name })
    $source = Join-Path $root $spec.kotlin
    $exeNames = @($spec.exe)
    if (-not (Test-Path $source)) { Write-Output "${class}: no Kotlin file $source"; $failed = $true; continue }
    $exes = @($exeNames | ForEach-Object { Join-Path $root "build/qt/$_.exe" })
    $absentExe = @($exes | Where-Object { -not (Test-Path $_) })
    if ($absentExe.Count -gt 0) { Write-Output "${class}: no executable $($absentExe -join ', ') (build first, see qt/README.md)"; $failed = $true; continue }

    # `fun <name>(` on the line right after `@Test` (a name may be written on the @Test line itself too)
    $lines = [System.IO.File]::ReadAllLines($source)
    $kotlin = [System.Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch '^\s*@Test\b') { continue }
        $text = $lines[$i] + ' ' + $(if ($i + 1 -lt $lines.Count) { $lines[$i + 1] } else { '' })
        if ($text -match '\bfun\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(') { $kotlin.Add($Matches[1]) }
        else { Write-Output "${class}: @Test at line $($i + 1) is not followed by fun <name>("; $failed = $true }
    }

    if ($kotlin.Count -eq 0) { Write-Output "${class}: no @Test names found in $source (the file moved or its form changed)"; $failed = $true; continue }

    $listed = [System.Collections.Generic.List[string]]::new()
    $listFailed = $false
    for ($e = 0; $e -lt $exes.Count; $e++) {
        $output = @(& $exes[$e] -functions)
        if ($LASTEXITCODE -ne 0) { Write-Output "${class}: $($exeNames[$e]) -functions exited with $LASTEXITCODE"; $listFailed = $true; break }
        foreach ($line in $output) { $listed.Add($line) }
    }
    if ($listFailed) { $failed = $true; continue }
    $qt = @($listed | ForEach-Object { $_.Trim() -replace '\(\)$', '' } | Where-Object { $_ -and $_ -cne 'initTestCase' -and $_ -cne 'cleanupTestCase' })

    $kotlinSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$kotlin, [StringComparer]::Ordinal)
    $qtSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$qt, [StringComparer]::Ordinal)
    if ($kotlinSet.Count -ne $kotlin.Count) { Write-Output "${class}: duplicate Kotlin test names"; $failed = $true }
    if ($qtSet.Count -ne $qt.Count) { Write-Output "${class}: duplicate Qt test names"; $failed = $true }
    $missing = @($kotlin | Where-Object { -not $qtSet.Contains($_) -and $omitted -cnotcontains $_ })
    $added = @($qt | Where-Object { -not $kotlinSet.Contains($_) })
    $omittedUnknown = @($omitted | Where-Object { -not $kotlinSet.Contains($_) })
    $omittedPresent = @($omitted | Where-Object { $qtSet.Contains($_) })
    $omittedTotal += $omitted.Count
    $unexpected = @($added | Where-Object { $allowed -cnotcontains $_ })
    $absent = @($allowed | Where-Object { $added -cnotcontains $_ })
    $addedTotal += $added.Count - $unexpected.Count

    Write-Output ('{0}: Kotlin {1}, Qt {2}, missing {3}{4}, added {5}{6}{7}' -f $class, $kotlin.Count, $qt.Count, $missing.Count,
                  $(if ($missing.Count) { " ($($missing -join ', '))" } else { '' }), $added.Count,
                  $(if ($added.Count) { " ($($added -join ', '))" } else { '' }),
                  $(if ($omitted.Count) { ", omitted $($omitted.Count) ($($omitted -join ', '))" } else { '' }))
    if ($missing.Count -gt 0) { $failed = $true }
    if ($unexpected.Count -gt 0) { Write-Output "${class}: added names that are not allowed: $($unexpected -join ', ')"; $failed = $true }
    if ($absent.Count -gt 0) { Write-Output "${class}: the intended added tests are not Qt functions of this class: $($absent -join ', ')"; $failed = $true }
    if ($omittedUnknown.Count -gt 0) { Write-Output "${class}: omitted names that are not Kotlin tests of this class: $($omittedUnknown -join ', ')"; $failed = $true }
    if ($omittedPresent.Count -gt 0) { Write-Output "${class}: omitted names that are Qt functions after all: $($omittedPresent -join ', ')"; $failed = $true }
}
if ($failed) { Write-Output 'FAILED'; exit 1 }
Write-Output "PASSED: every Kotlin test name that is not omitted is a Qt test name in $($classes.Count) classes; $omittedTotal names were omitted on purpose; only the $addedTotal intended names were added"
exit 0
