param([switch]$VerdictSelfTest)
# Q4: Defender scan and run of a Mark-of-the-Web copy of the spike exe on a fake clean PC.
# The folder is deliberately NOT removed: the user opens the exe from it by hand next.
# -VerdictSelfTest runs only the verdict table below; it launches nothing and creates no folder.

# Returns PASS, FAIL or UNJUDGEABLE.
# FAIL: evidence that Defender (or the exe) did something bad: a threat, a block/delete, a changed or missing file, a detection.
# UNJUDGEABLE: no such evidence, but Defender did not really look (scan failed, real-time protection off, odd scan code,
# detection query failed).
function Get-Q4Verdict {
    param(
        $ScanCode, [bool]$ScanFailed, [bool]$ThreatLine, [bool]$RtpOn,
        $RunExit, $RunError, [bool]$Intact, [int]$DetCount, [bool]$DetQueryFailed = $false
    )
    if ($ThreatLine) { return 'FAIL' }
    if ($ScanCode -eq 2 -and -not $ScanFailed) { return 'FAIL' }
    if ($RunError) { return 'FAIL' }
    if (-not $Intact) { return 'FAIL' }
    if ($DetCount -gt 0) { return 'FAIL' }
    if ($null -ne $RunExit -and $RunExit -ne 0) { return 'FAIL' }
    if ($null -eq $RunExit) { return 'FAIL' }
    if ($DetQueryFailed) { return 'UNJUDGEABLE' }
    if ($ScanFailed -or -not $RtpOn -or $ScanCode -ne 0) { return 'UNJUDGEABLE' }
    return 'PASS'
}

if ($VerdictSelfTest) {
    $cases = @(
        @{ N = 'all good, RTP on';                    A = @{ ScanCode = 0; ScanFailed = $false; ThreatLine = $false; RtpOn = $true;  RunExit = 0; RunError = $null; Intact = $true;  DetCount = 0 }; E = 'PASS' }
        @{ N = 'RTP off + scan failed (this PC)';     A = @{ ScanCode = 2; ScanFailed = $true;  ThreatLine = $false; RtpOn = $false; RunExit = 0; RunError = $null; Intact = $true;  DetCount = 0 }; E = 'UNJUDGEABLE' }
        @{ N = 'scan code 2, no failure text';        A = @{ ScanCode = 2; ScanFailed = $false; ThreatLine = $false; RtpOn = $true;  RunExit = 0; RunError = $null; Intact = $true;  DetCount = 0 }; E = 'FAIL' }
        @{ N = 'file missing after run';              A = @{ ScanCode = 0; ScanFailed = $false; ThreatLine = $false; RtpOn = $true;  RunExit = 0; RunError = $null; Intact = $false; DetCount = 0 }; E = 'FAIL' }
        @{ N = 'run threw (blocked)';                 A = @{ ScanCode = 0; ScanFailed = $false; ThreatLine = $false; RtpOn = $true;  RunExit = $null; RunError = 'blocked'; Intact = $true; DetCount = 0 }; E = 'FAIL' }
        @{ N = 'detections > 0';                      A = @{ ScanCode = 0; ScanFailed = $false; ThreatLine = $false; RtpOn = $true;  RunExit = 0; RunError = $null; Intact = $true;  DetCount = 1 }; E = 'FAIL' }
        @{ N = 'RTP on, scan failed, rest fine';      A = @{ ScanCode = 2; ScanFailed = $true;  ThreatLine = $false; RtpOn = $true;  RunExit = 0; RunError = $null; Intact = $true;  DetCount = 0 }; E = 'UNJUDGEABLE' }
        @{ N = 'threat line despite failure line';    A = @{ ScanCode = 2; ScanFailed = $true;  ThreatLine = $true;  RtpOn = $false; RunExit = 0; RunError = $null; Intact = $true;  DetCount = 0 }; E = 'FAIL' }
        @{ N = 'RTP off, file missing';               A = @{ ScanCode = 2; ScanFailed = $true;  ThreatLine = $false; RtpOn = $false; RunExit = 0; RunError = $null; Intact = $false; DetCount = 0 }; E = 'FAIL' }
        @{ N = 'all good but detection query failed'; A = @{ ScanCode = 0; ScanFailed = $false; ThreatLine = $false; RtpOn = $true;  RunExit = 0; RunError = $null; Intact = $true;  DetCount = 0; DetQueryFailed = $true }; E = 'UNJUDGEABLE' }
        @{ N = 'detection query failed, file missing'; A = @{ ScanCode = 0; ScanFailed = $false; ThreatLine = $false; RtpOn = $true;  RunExit = 0; RunError = $null; Intact = $false; DetCount = 0; DetQueryFailed = $true }; E = 'FAIL' }
    )
    $bad = 0
    foreach ($c in $cases) {
        $a = $c.A
        $got = Get-Q4Verdict @a
        $ok = ($got -eq $c.E)
        if (-not $ok) { $bad++ }
        Write-Host ("{0,-36} scan={1} failed={2} threat={3} rtp={4} run={5} err={6} intact={7} det={8} detfail={12} -> {9} (기대 {10}) {11}" -f `
            $c.N, $a.ScanCode, $a.ScanFailed, $a.ThreatLine, $a.RtpOn, $a.RunExit, $a.RunError, $a.Intact, $a.DetCount, $got, $c.E, $(if ($ok) { 'ok' } else { 'MISMATCH' }), [bool]$a.DetQueryFailed)
    }
    Write-Host "자체 시험: $($cases.Count - $bad)/$($cases.Count) 일치"
    if ($bad) { exit 1 }
    exit 0
}

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
$srcHash = (Get-FileHash -LiteralPath $exeSrc -Algorithm SHA256).Hash
$srcLen = (Get-Item -LiteralPath $exeSrc).Length

# A copy/mark failure here is most likely Defender (or another AV) blocking or deleting the exe: record it, keep going.
$prepError = $null
try {
    Copy-Item -LiteralPath $exeSrc -Destination $exe
    Add-Mark $exe
} catch {
    $prepError = "복사·표식 단계에서 오류: $($_.Exception.Message)"
    Write-Host "FAIL: Defender가 막았거나 지웠을 수 있다. $prepError"
}

# Side-by-side copy of the current app for the user's SmartScreen comparison; copied only, never run or scanned.
try {
    if (Test-Path -LiteralPath $cur) {
        $curCopy = Join-Path $dir 'xGetSongs-현행.exe'
        Copy-Item -LiteralPath $cur -Destination $curCopy
        Add-Mark $curCopy
        Write-Host "현행 exe 복사본: $curCopy ($([math]::Round((Get-Item -LiteralPath $curCopy).Length / 1MB, 1)) MB, 실행·검사하지 않음)"
    } else { Write-Host 'dist\xGetSongs.exe 없음: 현행 복사본 생략' }
} catch { Write-Host "현행 exe 복사본 만들기 실패(비교용이라 판정에는 쓰지 않음): $($_.Exception.Message)" }

$rtpOn = $false
try {
    $st = Get-MpComputerStatus
    $rtpOn = [bool]$st.RealTimeProtectionEnabled
    Write-Host ("RealTimeProtectionEnabled: {0}" -f $st.RealTimeProtectionEnabled)
    Write-Host ("AntivirusSignatureLastUpdated: {0}" -f $st.AntivirusSignatureLastUpdated)
} catch { Write-Host "Get-MpComputerStatus 실패: $($_.Exception.Message) (실시간 보호 꺼짐으로 취급)" }
if (-not $rtpOn) { Write-Host '*** 경고: 실시간 보호가 꺼져 있다. 아래 "실행" 결과는 Defender 점검으로서 아무것도 증명하지 않는다. ***' }

$scanCode = $null; $scanFailed = $false; $threatLine = $false
if (Test-Path -LiteralPath $exe) {
    Write-Host "검사: $exe"
    try {
        $scanOut = @(& $mpCmd -Scan -ScanType 3 -File $exe -DisableRemediation)
        $scanCode = $LASTEXITCODE
        $scanOut | ForEach-Object { Write-Host "  $_" }
        # MpCmdRun also returns 2 when the scan itself failed (e.g. Defender disabled), so look at the output too.
        $scanFailed = [bool]($scanOut | Where-Object { $_ -match 'Failed with hr|Product/Feature disabled' })
        $threatLine = [bool]($scanOut | Where-Object { $_ -match 'found\s+[1-9]\d*\s+threats?' })
    } catch { $scanFailed = $true; Write-Host "검사 실행 실패: $($_.Exception.Message)" }
} else { $scanFailed = $true; Write-Host '검사 생략: 복사본이 없다' }
Write-Host "검사 종료 코드: $scanCode (실패 표시: $scanFailed, 위협 줄: $threatLine)"

$report = Join-Path $pc.Root 'report.json'
$runExit = $null; $runError = $null
if (Test-Path -LiteralPath $exe) {
    try {
        $r = Start-InFakePc -Pc $pc -Exe $exe -Arguments @('--report', $report, '--exit-after-show') -Wait -TimeoutSec 30
        if ($r.TimedOut) { $runError = '30초 안에 끝나지 않음'; Write-Host "실행: $runError" }
        else { $runExit = $r.ExitCode; Write-Host "실행 종료 코드: $runExit" }
    } catch {
        $runError = "실행 오류(차단됐을 수 있음): $($_.Exception.Message)"
        Write-Host "FAIL: Defender가 막았거나 지웠을 수 있다. $runError"
    }
    if ((-not $runError) -and (Test-Path -LiteralPath $report)) { Write-Host ("보고 파일: " + (Get-Content -LiteralPath $report -Raw -Encoding utf8).Trim()) }
} else { $runError = '복사본이 없어 실행하지 못함' ; Write-Host "실행: $runError" }
if ($prepError -and -not $runError) { $runError = $prepError }

# Defender may quarantine silently, so re-check the file afterwards.
$exists = Test-Path -LiteralPath $exe
$intact = $false
if ($exists) {
    try { $intact = ((Get-Item -LiteralPath $exe).Length -eq $srcLen) -and ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -eq $srcHash) }
    catch { $intact = $false; Write-Host "복사본 읽기 실패: $($_.Exception.Message)" }
}
Write-Host "복사본 존재: $exists, 길이·SHA-256 원본과 동일: $intact"
Start-Sleep -Seconds 2
$folderName = Split-Path -Leaf $pc.Root
$det = @(); $detQueryFailed = $false
try { $det = @(Get-MpThreatDetection -ErrorAction Stop | Where-Object { (($_.Resources) -join ' ') -like "*$folderName*" }) }
catch { $detQueryFailed = $true; Write-Host "Get-MpThreatDetection 실패: $($_.Exception.Message) (탐지 여부를 알 수 없음: 판정 불가로 취급)" }
Write-Host "이 폴더에 대한 Defender 탐지 수: $($det.Count)"
foreach ($d in $det) { Write-Host "  탐지: $(($d.Resources) -join ', ')" }

$leftProcs = @(Get-Process -Name 'xgs-qt-spike' -ErrorAction SilentlyContinue).Count
Write-Host "남은 xgs-qt-spike 프로세스: $leftProcs"
$verdict = Get-Q4Verdict -ScanCode $scanCode -ScanFailed $scanFailed -ThreatLine $threatLine -RtpOn $rtpOn `
    -RunExit $runExit -RunError $runError -Intact $intact -DetCount $det.Count -DetQueryFailed $detQueryFailed
Write-Host "Q4 결과: $verdict"
if ($verdict -eq 'UNJUDGEABLE') { Write-Host '  (판정 불가: Defender가 실제로 검사하지 못했다. 통과도 실패도 아니다. 멈추고 사용자에게 알릴 것)' }
if ($verdict -eq 'FAIL') { Write-Host '  (실패: 위협·차단·삭제·변조·탐지 중 하나가 있다. 멈추고 사용자에게 알릴 것)' }
Write-Host "FOLDER: $dir"
if ($verdict -ne 'PASS') { exit 1 }
