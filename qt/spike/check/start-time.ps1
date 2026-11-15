# Q1/Q2/Q3: start time and size on a fake clean PC.
# 5 rounds; each round freshly copies the exe (first run), then reuses the same file (second run).
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'fake-pc.ps1')

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$exeSrc = Join-Path $repo 'build\qt-spike\xgs-qt-spike.exe'
$outFile = Join-Path $repo 'build\qt-spike\start-time.json'
if (-not (Test-Path -LiteralPath $exeSrc)) { throw "exe가 없다: $exeSrc" }
$rounds = 5

# One launch; returns @{ Ok; Detail; Ms } and treats a missing/unparsable report as a failed run.
function Invoke-Run($pc, $exe) {
    $report = Join-Path $pc.Root 'report.json'
    Remove-Item -LiteralPath $report -Force -ErrorAction SilentlyContinue
    $r = Start-InFakePc -Pc $pc -Exe $exe -Arguments @('--report', $report, '--exit-after-show') -Wait -TimeoutSec 30
    if ($r.TimedOut) { return @{ Ok = $false; Detail = '30초 안에 끝나지 않음'; Ms = $null } }
    if ($r.ExitCode -ne 0) { return @{ Ok = $false; Detail = "종료 코드 $($r.ExitCode)"; Ms = $null } }
    if (-not (Test-Path -LiteralPath $report)) { return @{ Ok = $false; Detail = '보고 파일 없음'; Ms = $null } }
    try { $j = Get-Content -LiteralPath $report -Raw -Encoding utf8 | ConvertFrom-Json }
    catch { return @{ Ok = $false; Detail = '보고 파일 파싱 실패'; Ms = $null } }
    if ($null -eq $j -or $null -eq $j.start -or $null -eq $j.start.processStartToIdleMs) {
        return @{ Ok = $false; Detail = '보고 파일에 start 없음'; Ms = $null }
    }
    @{ Ok = $true; Detail = 'ok'; Ms = [int]$j.start.processStartToIdleMs; ShownMs = [int]$j.start.processStartToShownMs }
}

function Get-Median($values) {
    $s = @($values | Sort-Object)
    if ($s.Count -eq 0) { return $null }
    $m = [int][math]::Floor($s.Count / 2)
    if ($s.Count % 2 -eq 1) { $s[$m] } else { ($s[$m - 1] + $s[$m]) / 2 }
}

$first = @(); $second = @(); $failures = @()
for ($i = 1; $i -le $rounds; $i++) {
    $pc = New-FakePc
    try {
        $exe = Join-Path $pc.AppDir 'xgs-qt-spike.exe'
        Copy-Item -LiteralPath $exeSrc -Destination $exe
        $a = Invoke-Run $pc $exe
        $b = Invoke-Run $pc $exe
        if ($a.Ok) { $first += $a.Ms } else { $failures += "라운드 $i 첫 실행: $($a.Detail)" }
        if ($b.Ok) { $second += $b.Ms } else { $failures += "라운드 $i 두 번째 실행: $($b.Detail)" }
        Write-Host ("라운드 {0}: 첫 실행 {1} / 두 번째 {2}" -f $i, $(if ($a.Ok) { "$($a.Ms) ms" } else { $a.Detail }), $(if ($b.Ok) { "$($b.Ms) ms" } else { $b.Detail }))
    }
    finally { Remove-FakePc $pc }
}

$size = (Get-Item -LiteralPath $exeSrc).Length
$firstMed = Get-Median $first
$secondMed = Get-Median $second
$allOk = ($failures.Count -eq 0)
$firstPass = $allOk -and $firstMed -le 3700
$secondPass = $allOk -and $secondMed -le 1200

Write-Host ''
Write-Host '| 라운드 | 첫 실행 (ms) | 두 번째 실행 (ms) |'
Write-Host '|---|---|---|'
for ($i = 0; $i -lt $rounds; $i++) {
    Write-Host ("| {0} | {1} | {2} |" -f ($i + 1), $(if ($i -lt $first.Count) { $first[$i] } else { '-' }), $(if ($i -lt $second.Count) { $second[$i] } else { '-' }))
}
Write-Host ("| 중앙값 | {0} | {1} |" -f $firstMed, $secondMed)
Write-Host ''
Write-Host ("exe 크기: {0:N0} 바이트 ({1:N1} MB) (기록만)" -f $size, ($size / 1MB))
foreach ($f in $failures) { Write-Host "실패: $f" }
Write-Host ("10번 모두 종료 코드 0·보고 정상: {0}" -f $(if ($allOk) { 'PASS' } else { 'FAIL' }))
Write-Host ("첫 실행 중앙값 <= 3700 ms: {0}" -f $(if ($firstPass) { 'PASS' } else { 'FAIL' }))
Write-Host ("두 번째 중앙값 <= 1200 ms: {0}" -f $(if ($secondPass) { 'PASS' } else { 'FAIL' }))

$leftFolders = @(Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'xgs-qt-spike-*' -ErrorAction SilentlyContinue).Count
$leftProcs = @(Get-Process -Name 'xgs-qt-spike' -ErrorAction SilentlyContinue).Count
Write-Host "남은 xgs-qt-spike-* 폴더: $leftFolders, 남은 xgs-qt-spike 프로세스: $leftProcs"

[ordered]@{
    exeBytes        = $size
    exeMB           = [math]::Round($size / 1MB, 2)
    firstIdleMs     = $first
    secondIdleMs    = $second
    firstMedianMs   = $firstMed
    secondMedianMs  = $secondMed
    failures        = $failures
    allExitZero     = $allOk
    firstPass       = $firstPass
    secondPass      = $secondPass
    leftoverFolders = $leftFolders
    leftoverProcs   = $leftProcs
} | ConvertTo-Json | Set-Content -LiteralPath $outFile -Encoding utf8

if (-not ($firstPass -and $secondPass)) { exit 1 }
