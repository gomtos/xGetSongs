# Q4: Defender scan and run of a Mark-of-the-Web copy of the spike exe on a fake clean PC.
# The folder is deliberately NOT removed: the user opens the exe from it by hand next.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'fake-pc.ps1')

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$exeSrc = Join-Path $repo 'build\qt-spike\xgs-qt-spike.exe'
$cur = Join-Path $repo 'dist\xGetSongs.exe'
if (-not (Test-Path -LiteralPath $exeSrc)) { throw "exe가 없다: $exeSrc" }
$mpCmd = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
if (-not (Test-Path -LiteralPath $mpCmd)) { throw "MpCmdRun.exe가 없다: $mpCmd" }

function Add-Mark([string]$path) {
    Set-Content -LiteralPath $path -Stream Zone.Identifier -Value "[ZoneTransfer]`r`nZoneId=3"
    $z = Get-Content -LiteralPath $path -Stream Zone.Identifier -Raw
    if ($z -notmatch 'ZoneId=3') { throw "Zone.Identifier 확인 실패: $path" }
    Write-Host "Zone.Identifier ($([IO.Path]::GetFileName($path))):"
    ($z -split "`r?`n") | Where-Object { $_ } | ForEach-Object { Write-Host "  $_" }
}

$pc = New-FakePc
$dir = Join-Path $pc.Root '내려받은 것'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$exe = Join-Path $dir 'xgs-qt-spike.exe'
Copy-Item -LiteralPath $exeSrc -Destination $exe
Add-Mark $exe
$srcHash = (Get-FileHash -LiteralPath $exeSrc -Algorithm SHA256).Hash
$srcLen = (Get-Item -LiteralPath $exeSrc).Length

# Side-by-side copy of the current app for the user's SmartScreen comparison; copied only, never run or scanned.
if (Test-Path -LiteralPath $cur) {
    $curCopy = Join-Path $dir 'xGetSongs-현행.exe'
    Copy-Item -LiteralPath $cur -Destination $curCopy
    Add-Mark $curCopy
    Write-Host "현행 exe 복사본: $curCopy ($([math]::Round((Get-Item -LiteralPath $curCopy).Length / 1MB, 1)) MB, 실행·검사하지 않음)"
} else { Write-Host 'dist\xGetSongs.exe 없음: 현행 복사본 생략' }

$st = Get-MpComputerStatus
Write-Host ("RealTimeProtectionEnabled: {0}" -f $st.RealTimeProtectionEnabled)
Write-Host ("AntivirusSignatureLastUpdated: {0}" -f $st.AntivirusSignatureLastUpdated)
if (-not $st.RealTimeProtectionEnabled) { Write-Host '*** 경고: 실시간 보호가 꺼져 있다. 아래 "실행" 결과는 Defender 점검으로서 아무것도 증명하지 않는다. ***' }

Write-Host "검사: $exe"
$scanOut = @(& $mpCmd -Scan -ScanType 3 -File $exe -DisableRemediation)
$scanCode = $LASTEXITCODE
$scanOut | ForEach-Object { Write-Host "  $_" }
# MpCmdRun also returns 2 when the scan itself failed (e.g. Defender disabled), so look at the output too.
$scanFailed = [bool]($scanOut | Where-Object { $_ -match 'Failed with hr|Product/Feature disabled' })
$scanVerdict = if ($scanFailed) { "검사하지 못함(코드 $scanCode, 출력에 실패 표시): 통과도 실패도 아님" }
    else { switch ($scanCode) { 0 { '위협 없음' } 2 { '위협 발견: FAIL' } default { "검사하지 못함(코드 $scanCode): 통과도 실패도 아님" } } }
Write-Host "검사 종료 코드: $scanCode ($scanVerdict)"

$report = Join-Path $pc.Root 'report.json'
$r = Start-InFakePc -Pc $pc -Exe $exe -Arguments @('--report', $report, '--exit-after-show') -Wait -TimeoutSec 30
if ($r.TimedOut) { Write-Host '실행: 30초 안에 끝나지 않음' } else { Write-Host "실행 종료 코드: $($r.ExitCode)" }
if ((-not $r.TimedOut) -and (Test-Path -LiteralPath $report)) { Write-Host ("보고 파일: " + (Get-Content -LiteralPath $report -Raw -Encoding utf8).Trim()) }

# Defender may quarantine silently, so re-check the file afterwards.
$exists = Test-Path -LiteralPath $exe
$intact = $false
if ($exists) {
    $intact = ((Get-Item -LiteralPath $exe).Length -eq $srcLen) -and ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -eq $srcHash)
}
Write-Host "복사본 존재: $exists, 길이·SHA-256 원본과 동일: $intact"
Start-Sleep -Seconds 2
$det = @(Get-MpThreatDetection -ErrorAction SilentlyContinue | Where-Object { (($_.Resources) -join ' ') -like "*$($pc.Root)*" })
Write-Host "이 폴더에 대한 Defender 탐지 수: $($det.Count)"
foreach ($d in $det) { Write-Host "  탐지: $(($d.Resources) -join ', ')" }

$leftProcs = @(Get-Process -Name 'xgs-qt-spike' -ErrorAction SilentlyContinue).Count
Write-Host "남은 xgs-qt-spike 프로세스: $leftProcs"
$pass = ($scanCode -eq 0) -and (-not $scanFailed) -and (-not $r.TimedOut) -and ($r.ExitCode -eq 0) -and $intact -and ($det.Count -eq 0) -and $st.RealTimeProtectionEnabled
Write-Host ("Q4 (검사 0, 실행 0, 파일 그대로, 탐지 0, 실시간 보호 켜짐): {0}" -f $(if ($pass) { 'PASS' } else { 'FAIL 또는 판정 불가: 멈추고 사용자에게 알릴 것' }))
Write-Host "FOLDER: $dir"
if (-not $pass) { exit 1 }
