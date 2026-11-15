# Compares the names of the Kotlin @Test functions of the five ported test classes with the names of the Qt Test functions
# of the same-named test executables (`build/qt/tst_*.exe -functions`, without the trailing "()").
#
#   pwsh qt/tools/test-names.ps1
#
# Per class it prints `Kotlin N, Qt M, missing 0, added K (names)`. The exit code is 1 when any Kotlin name has no Qt
# function, when a Qt function is neither a Kotlin name nor one of the three names this port added on purpose ($Allowed), or
# when a Kotlin test file yields no name at all.
# Qt's own initTestCase/cleanupTestCase are not test names and are ignored.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$kotlinDir = Join-Path $root 'shared/src/commonTest/kotlin/com/xgetsongs/shared'

# class -> the Kotlin test file (under $kotlinDir) and the Qt test executable
$Classes = [ordered]@{
    InputClassifierTest   = @{ File = 'input/InputClassifierTest.kt';      Exe = 'tst_input_classifier' }
    FilenameFormatterTest = @{ File = 'filename/FilenameFormatterTest.kt'; Exe = 'tst_filename_formatter' }
    TitleParserTest       = @{ File = 'title/TitleParserTest.kt';          Exe = 'tst_title_parser' }
    LyricsExtractorTest   = @{ File = 'lyrics/LyricsExtractorTest.kt';     Exe = 'tst_lyrics_extractor' }
    LogRedactionTest      = @{ File = 'log/LogRedactionTest.kt';           Exe = 'tst_log_redaction' }
}
# The names the port adds on purpose (a performance test each in Tasks 6 and 7, and the characterization of a Kotlin
# behaviour in Task 8); nothing else may be added.
$Allowed = @('extractOnOneMegabyteDescriptionFinishesQuickly', 'redactPathsWithFiveThousandPartsFinishesQuickly',
             'leadingNonBmpLettersAreDroppedLikeEmoji')

$failed = $false
$allowedSeen = [System.Collections.Generic.List[string]]::new()
foreach ($class in $Classes.Keys) {
    $spec = $Classes[$class]
    $source = Join-Path $kotlinDir $spec.File
    $exe = Join-Path $root "build/qt/$($spec.Exe).exe"
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
    if ($LASTEXITCODE -ne 0) { Write-Output "${class}: $($spec.Exe) -functions exited with $LASTEXITCODE"; $failed = $true; continue }
    $qt = @($listed | ForEach-Object { $_.Trim() -replace '\(\)$', '' } | Where-Object { $_ -and $_ -cne 'initTestCase' -and $_ -cne 'cleanupTestCase' })

    $kotlinSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$kotlin, [StringComparer]::Ordinal)
    $qtSet = [System.Collections.Generic.HashSet[string]]::new([string[]]$qt, [StringComparer]::Ordinal)
    if ($kotlinSet.Count -ne $kotlin.Count) { Write-Output "${class}: duplicate Kotlin test names"; $failed = $true }
    if ($qtSet.Count -ne $qt.Count) { Write-Output "${class}: duplicate Qt test names"; $failed = $true }
    $missing = @($kotlin | Where-Object { -not $qtSet.Contains($_) })
    $added = @($qt | Where-Object { -not $kotlinSet.Contains($_) })
    $unexpected = @($added | Where-Object { $Allowed -cnotcontains $_ })
    foreach ($name in $added) { if ($Allowed -ccontains $name) { $allowedSeen.Add($name) } }

    Write-Output ('{0}: Kotlin {1}, Qt {2}, missing {3}{4}, added {5}{6}' -f $class, $kotlin.Count, $qt.Count, $missing.Count,
                  $(if ($missing.Count) { " ($($missing -join ', '))" } else { '' }), $added.Count,
                  $(if ($added.Count) { " ($($added -join ', '))" } else { '' }))
    if ($missing.Count -gt 0) { $failed = $true }
    if ($unexpected.Count -gt 0) { Write-Output "${class}: added names that are not allowed: $($unexpected -join ', ')"; $failed = $true }
}
foreach ($name in $Allowed) {
    if ($allowedSeen -cnotcontains $name) { Write-Output "the intended added test $name is not in any class"; $failed = $true }
}
if ($failed) { Write-Output 'FAILED'; exit 1 }
Write-Output 'PASSED: every Kotlin test name is a Qt test name; only the three intended names were added'
exit 0
