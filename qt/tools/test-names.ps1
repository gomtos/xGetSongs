# Compares the names of the Kotlin @Test functions of the ported test classes with the names of the Qt Test functions of
# their test executables (`build/qt/<exe>.exe -functions`, without the trailing "()"). The classes are listed in
# qt/tools/test-classes.json: an array of { "kotlin": <the Kotlin test file, relative to the repository root>,
# "exe": <the Qt test executable's name>, "added": [<names the port adds on purpose, not in the Kotlin class>] }.
#
#   pwsh qt/tools/test-names.ps1
#
# Per class it prints `Kotlin N, Qt M, missing 0, added K (names)`. The exit code is 1 when any Kotlin name has no Qt
# function, when a Qt function is neither a Kotlin name nor one of its class's `added` names, when an `added` name is not
# a Qt function of its class, or when a Kotlin test file yields no name at all.
# Qt's own initTestCase/cleanupTestCase are not test names and are ignored.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$listFile = Join-Path $PSScriptRoot 'test-classes.json'

$classes = @(Get-Content -Raw -LiteralPath $listFile | ConvertFrom-Json)
if ($classes.Count -eq 0) { Write-Output "no classes in $listFile"; Write-Output 'FAILED'; exit 1 }

$failed = $false
$addedTotal = 0
foreach ($spec in $classes) {
    if (-not $spec.kotlin -or -not $spec.exe -or $null -eq $spec.added) {
        Write-Output "an entry of $listFile lacks kotlin, exe or added: $($spec | ConvertTo-Json -Compress)"; $failed = $true; continue
    }
    $class = [System.IO.Path]::GetFileNameWithoutExtension($spec.kotlin)
    $allowed = @($spec.added)
    $source = Join-Path $root $spec.kotlin
    $exe = Join-Path $root "build/qt/$($spec.exe).exe"
    if (-not (Test-Path $source)) { Write-Output "${class}: no Kotlin file $source"; $failed = $true; continue }
    if (-not (Test-Path $exe)) { Write-Output "${class}: no executable $exe (build first, see qt/README.md)"; $failed = $true; continue }

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

    $listed = @(& $exe -functions)
    if ($LASTEXITCODE -ne 0) { Write-Output "${class}: $($spec.exe) -functions exited with $LASTEXITCODE"; $failed = $true; continue }
    $qt = @($listed | ForEach-Object { $_.Trim() -replace '\(\)$', '' } | Where-Object { $_ -and $_ -cne 'initTestCase' -and $_ -cne 'cleanupTestCase' })

    $kotlinSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$kotlin, [StringComparer]::Ordinal)
    $qtSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$qt, [StringComparer]::Ordinal)
    if ($kotlinSet.Count -ne $kotlin.Count) { Write-Output "${class}: duplicate Kotlin test names"; $failed = $true }
    if ($qtSet.Count -ne $qt.Count) { Write-Output "${class}: duplicate Qt test names"; $failed = $true }
    $missing = @($kotlin | Where-Object { -not $qtSet.Contains($_) })
    $added = @($qt | Where-Object { -not $kotlinSet.Contains($_) })
    $unexpected = @($added | Where-Object { $allowed -cnotcontains $_ })
    $absent = @($allowed | Where-Object { $added -cnotcontains $_ })
    $addedTotal += $added.Count - $unexpected.Count

    Write-Output ('{0}: Kotlin {1}, Qt {2}, missing {3}{4}, added {5}{6}' -f $class, $kotlin.Count, $qt.Count, $missing.Count,
                  $(if ($missing.Count) { " ($($missing -join ', '))" } else { '' }), $added.Count,
                  $(if ($added.Count) { " ($($added -join ', '))" } else { '' }))
    if ($missing.Count -gt 0) { $failed = $true }
    if ($unexpected.Count -gt 0) { Write-Output "${class}: added names that are not allowed: $($unexpected -join ', ')"; $failed = $true }
    if ($absent.Count -gt 0) { Write-Output "${class}: the intended added tests are not Qt functions of this class: $($absent -join ', ')"; $failed = $true }
}
if ($failed) { Write-Output 'FAILED'; exit 1 }
Write-Output "PASSED: every Kotlin test name is a Qt test name in $($classes.Count) classes; only the $addedTotal intended names were added"
exit 0
