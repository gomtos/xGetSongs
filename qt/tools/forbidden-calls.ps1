# Looks for calls that the ported code must not make (Global Constraints of the Q1 plan), and for names of JDK internals.
#
#   pwsh qt/tools/forbidden-calls.ps1
#
# 1. Under qt/core, except qt/core/text/kt_text.* (the xgs::kt layer, which implements them): Qt character and string
#    functions whose meaning differs from Kotlin's or that depend on the locale or on Qt's Unicode version (case, category,
#    folding, locale-aware comparison), unless the match is `kt::<name>(` (the Q1 list, $forbidden).
# 2. Under all of qt/core, the xgs::kt layer and qt/core/json included (the Q2a list, $forbiddenQ2a): Qt's JSON classes,
#    number parsing (toDouble/toInt/toLongLong), QString::number with a floating-point format ('e', 'f', 'g'),
#    normalization (normalized), and path clean-up (cleanPath, absoluteFilePath, absolutePath, canonicalPath,
#    canonicalFilePath). The same work is done by xgs::kt and xgs::json. One exception, $allowedQ2a: `normalized(` in
#    qt/core/text/kt_text.cpp, which implements kt::nfkc with it. (A QString::number of a double without a format argument
#    cannot be told from one of an integer here; reviews look for it.)
# 3. Every text file under qt/, whatever its extension, except qt/spike (and this script, which names them; a file with a
#    NUL byte is taken as binary and skipped, and reported): names of JDK internals. The project is MIT
#    licensed and code derived from the OpenJDK must not enter it (a ruling of the Q1 stage).
# Exit code 1 on any hit, with the hits printed; 0 and the number of files looked at otherwise.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$qt = Join-Path $root 'qt'
$spike = Join-Path $qt 'spike'
$self = $MyInvocation.MyCommand.Path

$forbidden = [regex]::new('isSpace\(|isLetterOrNumber\(|isLetter\(|isDigit\(|toLower\(|toUpper\(|trimmed\(|simplified\(|CaseInsensitive|QRegularExpression|isUpper\(|isLower\(|isNumber\(|isPunct\(|toCaseFolded\(|localeAwareCompare|QLocale', 'Compiled')
$forbiddenQ2a = [regex]::new("QJsonDocument|QJsonObject|QJsonValue|QJsonArray|toDouble\(|toInt\(|toLongLong\(|QString::number\([^;]*?,\s*'[eEfFgG]'|normalized\(|cleanPath|absoluteFilePath|absolutePath|canonicalPath|canonicalFilePath", 'Compiled')
# file (relative to qt/core, with /) -> the $forbiddenQ2a matches it may contain
$allowedQ2a = @{ 'text/kt_text.cpp' = @('normalized(') }
$internals = [regex]::new('compareToCI|StringUTF16|regionMatchesCI|BreakIterator|sun\.text|jdk\.internal', 'Compiled')

function Get-Files([string]$dir) {
    Get-ChildItem -LiteralPath $dir -Recurse -File | Where-Object {
        $_.FullName -notlike "$spike\*" -and $_.FullName -ne $self
    }
}
$hits = [System.Collections.Generic.List[string]]::new()

# every file under qt/core (not only .cpp/.h) except the kt layer
$coreFiles = @(Get-Files (Join-Path $qt 'core') | Where-Object { $_.BaseName -ne 'kt_text' -and $_.BaseName -ne 'kt_tables' })
foreach ($file in $coreFiles) {
    $lines = [System.IO.File]::ReadAllLines($file.FullName)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($m in $forbidden.Matches($lines[$i])) {
            $before = $lines[$i].Substring(0, $m.Index)
            if ($before.EndsWith('kt::', [StringComparison]::Ordinal)) { continue }
            $hits.Add("$($file.FullName.Substring($root.Length + 1)):$($i + 1): forbidden call '$($m.Value)': $($lines[$i].Trim())")
        }
    }
}

# the Q2a list: every file under qt/core, with only the exceptions of $allowedQ2a
$coreDir = Join-Path $qt 'core'
$allCoreFiles = @(Get-Files $coreDir)
foreach ($file in $allCoreFiles) {
    $relative = $file.FullName.Substring($coreDir.Length + 1).Replace('\', '/')
    $allowed = if ($allowedQ2a.ContainsKey($relative)) { $allowedQ2a[$relative] } else { @() }
    $lines = [System.IO.File]::ReadAllLines($file.FullName)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($m in $forbiddenQ2a.Matches($lines[$i])) {
            if ($allowed -ccontains $m.Value) { continue }
            $hits.Add("$($file.FullName.Substring($root.Length + 1)):$($i + 1): forbidden call '$($m.Value)': $($lines[$i].Trim())")
        }
    }
}

# JDK internal names: every text file under qt/ except qt/spike and this script, whatever the extension
$allFiles = [System.Collections.Generic.List[object]]::new()
$binaryFiles = [System.Collections.Generic.List[string]]::new()
foreach ($file in @(Get-Files $qt)) {
    if ([System.Array]::IndexOf([System.IO.File]::ReadAllBytes($file.FullName), [byte]0) -ge 0) { $binaryFiles.Add($file.FullName.Substring($root.Length + 1)); continue }
    $allFiles.Add($file)
}
foreach ($file in $allFiles) {
    $lines = [System.IO.File]::ReadAllLines($file.FullName)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($m in $internals.Matches($lines[$i])) {
            $hits.Add("$($file.FullName.Substring($root.Length + 1)):$($i + 1): JDK internal name '$($m.Value)': $($lines[$i].Trim())")
        }
    }
}

Write-Output "looked at $($coreFiles.Count) files under qt/core for the Q1 list (kt_text.* and kt_tables.inc excluded), $($allCoreFiles.Count) for the Q2a list, and $($allFiles.Count) text files under qt/ (qt/spike excluded)"
if ($binaryFiles.Count -gt 0) { Write-Output "skipped as binary (a NUL byte): $($binaryFiles -join ', ')" }
if ($hits.Count -gt 0) {
    $hits | ForEach-Object { Write-Output $_ }
    Write-Output "FAILED: $($hits.Count) hits"
    exit 1
}
Write-Output 'PASSED: 0 hits'
exit 0
