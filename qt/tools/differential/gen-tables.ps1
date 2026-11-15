# Writes qt/core/text/kt_tables.inc: Unicode Character Database facts as of the Unicode version that JVM 21 implements,
# obtained by asking the JVM through its public API (Oracle.java chartable and casetable), where xgs::kt cannot take Qt's
# own Unicode data: the units whose letter/case properties differ between the two Unicode versions, the special casing
# (SpecialCasing.txt) of single units, and the case mappings above U+FFFF. Run from anywhere, after building xgs-diff:
#
#   pwsh qt/tools/differential/gen-tables.ps1            (then rebuild; the X kind of run.ps1 must show 0 mismatches)
#   pwsh qt/tools/differential/gen-tables.ps1 -NoQt      (bootstrap: no xgs-diff yet, so no unit exceptions)
param([switch]$NoQt)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$image = Join-Path $root 'server/build/sidecar-image'
$java = Join-Path $image 'runtime/bin/java.exe'
$oracle = Join-Path $PSScriptRoot 'Oracle.java'
$tool = Join-Path $root 'build/qt/xgs-diff.exe'
$output = Join-Path $root 'qt/core/text/kt_tables.inc'
if (-not (Test-Path $java)) { throw "no sidecar image at $image (.\gradlew.bat --no-daemon :server:sidecarImage)" }

function Invoke-Lines([string]$exe, [string[]]$arguments) {
    $lines = & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe $($arguments -join ' ') failed ($LASTEXITCODE)" }
    return , @($lines)
}

$classpath = @('-cp', (Join-Path $image 'lib/*'))
$jvmVersion = ((& $java -version 2>&1) | ForEach-Object { "$_" } | Select-Object -First 3) -join ' / '
$jvmChars = Invoke-Lines $java ($classpath + @($oracle, 'chartable'))
$jvmCases = Invoke-Lines $java ($classpath + @($oracle, 'casetable'))
if ($jvmChars.Count -ne 65536) { throw "the oracle wrote $($jvmChars.Count) chartable lines, not 65536" }

$qtVersion = '(none: -NoQt)'
$exceptions = [System.Collections.Generic.List[string]]::new()
$counts = [ordered]@{ isWhitespace = 0; isLetter = 0; isLetterOrDigit = 0; uppercaseChar = 0; lowercaseChar = 0 }
$examples = [ordered]@{ isWhitespace = @(); isLetter = @(); isLetterOrDigit = @(); uppercaseChar = @(); lowercaseChar = @() }
if (-not $NoQt) {
    $qtVersion = (Invoke-Lines $tool @('qt-version'))[0]
    $qtChars = Invoke-Lines $tool @('qt-chartable')
    if ($qtChars.Count -ne 65536) { throw "xgs-diff wrote $($qtChars.Count) qt-chartable lines, not 65536" }
    $names = @($counts.Keys)
    for ($unit = 0; $unit -le 0xFFFF; $unit++) {
        if ($jvmChars[$unit] -ceq $qtChars[$unit]) { continue }
        $j = $jvmChars[$unit].Split(' ')
        $q = $qtChars[$unit].Split(' ')
        for ($k = 0; $k -lt 5; $k++) {
            if ($j[$k] -cne $q[$k]) {
                $counts[$names[$k]]++
                if ($examples[$names[$k]].Count -lt 6) { $examples[$names[$k]] += ('U+{0:X4} jvm {1} qt {2}' -f $unit, $j[$k], $q[$k]) }
            }
        }
        $bool = { param($v) if ($v -eq '1') { 'true' } else { 'false' } }
        $exceptions.Add(('    {{0x{0:x4}, {1}, {2}, {3}, 0x{4}, 0x{5}}},' -f $unit, (& $bool $j[0]), (& $bool $j[1]), (& $bool $j[2]), $j[3], $j[4]))
    }
}

function Get-UnitList([string]$hex) {
    $units = @()
    for ($i = 0; $i -lt $hex.Length; $i += 4) { $units += '0x' + $hex.Substring($i, 4) }
    return , $units
}
function Get-CodePointHex([int]$codePoint) {
    return ([char]::ConvertFromUtf32($codePoint).ToCharArray() | ForEach-Object { '{0:x4}' -f [int]$_ }) -join ''
}

$upper = [System.Collections.Generic.List[string]]::new()
$lower = [System.Collections.Generic.List[string]]::new()
$supplementary = [System.Collections.Generic.List[string]]::new()
$jvmUpper = @{}
$jvmLower = @{}
foreach ($line in $jvmCases) {
    $part = $line.Split(' ')
    switch ($part[0]) {
        'U' { $u = Get-UnitList $part[2]; if ($u.Count -gt 3) { throw "longer than 3 units: $line" }
              $jvmUpper[[Convert]::ToInt32($part[1], 16)] = $part[2]
              $upper.Add(('    {{0x{0}, {1}, {{{2}}}}},' -f $part[1], $u.Count, ($u -join ', '))) }
        'L' { $u = Get-UnitList $part[2]; if ($u.Count -gt 3) { throw "longer than 3 units: $line" }
              $jvmLower[[Convert]::ToInt32($part[1], 16)] = $part[2]
              $lower.Add(('    {{0x{0}, {1}, {{{2}}}}},' -f $part[1], $u.Count, ($u -join ', '))) }
        'P' { $cp = [Convert]::ToInt32($part[1], 16); $up = [Convert]::ToInt32($part[2], 16); $lo = [Convert]::ToInt32($part[3], 16)
              # Above U+FFFF the full mapping must be the simple one: kt::uppercase/lowercase use the simple one there.
              if ($part[4] -cne (Get-CodePointHex $up) -or $part[5] -cne (Get-CodePointHex $lo)) { throw "special casing above U+FFFF: $line" }
              $supplementary.Add(('    {{0x{0}, 0x{1}, 0x{2}}},' -f $part[1], $part[2], $part[3])) }
        default { throw "unexpected casetable line: $line" }
    }
}
# kt::lowercase handles U+03A3 itself (final sigma) and expects no other conditional lowercase.
foreach ($unit in $jvmLower.Keys) { if ($unit -ne 0x0130) { throw ('unexpected lowercase special U+{0:X4}' -f $unit) } }

$exceptionSummary = ($counts.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }) -join ', '
$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine('// Generated by qt/tools/differential/gen-tables.ps1 (pwsh qt/tools/differential/gen-tables.ps1); do not edit by hand.')
[void]$sb.AppendLine('// Unicode Character Database facts as of the Unicode version of JVM 21, obtained by asking the JVM through its public')
[void]$sb.AppendLine('// API (Oracle.java chartable / casetable) on the runtime of server/build/sidecar-image:')
[void]$sb.AppendLine("//   $jvmVersion")
[void]$sb.AppendLine("// Compared with the Unicode tables of Qt $qtVersion (xgs-diff qt-chartable).")
[void]$sb.AppendLine('// Included by kt_text.cpp, which defines the record types.')
[void]$sb.AppendLine('')
[void]$sb.AppendLine("// The UTF-16 units where Qt's answer differs from the JVM's in at least one of Char.isWhitespace, isLetter,")
[void]$sb.AppendLine("// isLetterOrDigit, uppercaseChar, lowercaseChar; the row is the JVM's answer. Differing units per function:")
[void]$sb.AppendLine("// $exceptionSummary.")
[void]$sb.AppendLine("inline constexpr std::array<CharException, $($exceptions.Count)> kCharExceptions{{")
foreach ($row in $exceptions) { [void]$sb.AppendLine($row) }
[void]$sb.AppendLine('}};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine("// String.uppercase() of one unit where it is not uppercaseChar() (SpecialCasing.txt), and String.lowercase() likewise.")
[void]$sb.AppendLine("inline constexpr std::array<SpecialCase, $($upper.Count)> kUpperSpecials{{")
foreach ($row in $upper) { [void]$sb.AppendLine($row) }
[void]$sb.AppendLine('}};')
[void]$sb.AppendLine("inline constexpr std::array<SpecialCase, $($lower.Count)> kLowerSpecials{{")
foreach ($row in $lower) { [void]$sb.AppendLine($row) }
[void]$sb.AppendLine('}};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('// Character.toUpperCase(int) and toLowerCase(int) of every code point above U+FFFF that has a case mapping.')
[void]$sb.AppendLine("inline constexpr std::array<SupplementaryCase, $($supplementary.Count)> kSupplementaryCases{{")
foreach ($row in $supplementary) { [void]$sb.AppendLine($row) }
[void]$sb.AppendLine('}};')
[void]$sb.AppendLine('')
[System.IO.File]::WriteAllText($output, $sb.ToString().Replace("`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

Write-Output "wrote $output"
Write-Output "JVM: $jvmVersion; Qt $qtVersion"
Write-Output "unit exceptions: $($exceptions.Count) ($exceptionSummary)"
foreach ($name in $examples.Keys) { foreach ($e in $examples[$name]) { Write-Output "  $name $e" } }
Write-Output "upper specials $($upper.Count), lower specials $($lower.Count), supplementary case mappings $($supplementary.Count)"

# For the record only: where Qt's QString::toUpper/toLower of a single unit differs from Kotlin's String.uppercase/lowercase
# (kt_text.cpp does not use them; final sigma, which needs context, is not counted here).
if (-not $NoQt) {
    $qtCases = Invoke-Lines $tool @('qt-casetable')
    $upperDiff = @(); $lowerDiff = @()
    for ($unit = 0; $unit -le 0xFFFF; $unit++) {
        $j = $jvmChars[$unit].Split(' ')
        $kotlinUpper = if ($jvmUpper.ContainsKey($unit)) { $jvmUpper[$unit] } else { $j[3] }
        $kotlinLower = if ($jvmLower.ContainsKey($unit)) { $jvmLower[$unit] } else { $j[4] }
        $q = $qtCases[$unit].Split(' ')
        if ($q[0] -cne $kotlinUpper) { $upperDiff += ('U+{0:X4} kotlin {1} qt {2}' -f $unit, $kotlinUpper, $q[0]) }
        if ($q[1] -cne $kotlinLower) { $lowerDiff += ('U+{0:X4} kotlin {1} qt {2}' -f $unit, $kotlinLower, $q[1]) }
    }
    Write-Output "QString::toUpper differs from String.uppercase() on $($upperDiff.Count) single units; QString::toLower from String.lowercase() on $($lowerDiff.Count)"
    $upperDiff | Select-Object -First 12 | ForEach-Object { Write-Output "  toUpper $_" }
    $lowerDiff | Select-Object -First 12 | ForEach-Object { Write-Output "  toLower $_" }
}
