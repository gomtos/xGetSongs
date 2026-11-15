# Q8: one HTTPS GET through Qt's TLS backend (Schannel, no OpenSSL) on a fake clean PC.
# The only outbound request: https://lrclib.net/api/search?track_name=test&artist_name=test
# If lrclib.net does not answer (time-out, 5xx) the HTTP part is "건너뜀" and only the backend is checked.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'fake-pc.ps1')

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$exeSrc = Join-Path $repo 'build\qt-spike\xgs-qt-spike.exe'
$outFile = Join-Path $repo 'build\qt-spike\tls.json'
if (-not (Test-Path -LiteralPath $exeSrc)) { throw "exe가 없다: $exeSrc" }

$pc = New-FakePc
try {
    $exe = Join-Path $pc.AppDir 'xgs-qt-spike.exe'
    Copy-Item -LiteralPath $exeSrc -Destination $exe
    $report = Join-Path $pc.Root 'report.json'
    $r = Start-InFakePc -Pc $pc -Exe $exe -Arguments @('--report', $report, '--tls-check') -Wait -TimeoutSec 30
    # Read only after the exe has exited.
    $tls = $null
    if (Test-Path -LiteralPath $report) {
        try { $tls = (Get-Content -LiteralPath $report -Raw -Encoding utf8 | ConvertFrom-Json).tls } catch { }
    }
}
finally { Remove-FakePc $pc }

Write-Host ('종료 코드 {0}, 시간 초과 {1}' -f $r.ExitCode, $r.TimedOut)
Write-Host ('tls: {0}' -f ($tls | ConvertTo-Json -Compress))

$ran = (-not $r.TimedOut) -and $r.ExitCode -eq 0 -and $null -ne $tls
$backendPass = $ran -and $tls.backend -eq 'schannel'
$status = $(if ($ran) { [int]$tls.status } else { 0 })
# No answer: Qt's transfer time-out ends the request as "Operation canceled"; 5xx is the server.
$noAnswer = $ran -and ($status -ge 500 -or ($status -eq 0 -and [string]$tls.error -match 'cancel|timed? ?out'))
$httpResult = $(if (-not $ran) { 'FAIL' } elseif ($noAnswer) { '건너뜀' }
    elseif ($tls.ok -eq $true -and $status -eq 200) { 'PASS' } else { 'FAIL' })

Write-Host ("backend가 schannel: {0}" -f $(if ($backendPass) { 'PASS' } else { 'FAIL' }))
Write-Host ("ok·HTTP 200: {0}" -f $httpResult)

$leftFolders = @(Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'xgs-qt-spike-*' -ErrorAction SilentlyContinue).Count
$leftProcs = @(Get-Process -Name 'xgs-qt-spike' -ErrorAction SilentlyContinue).Count
Write-Host "남은 xgs-qt-spike-* 폴더: $leftFolders, 남은 xgs-qt-spike 프로세스: $leftProcs"

[ordered]@{
    exitCode        = $r.ExitCode
    timedOut        = $r.TimedOut
    tls             = $tls
    backendPass     = $backendPass
    http            = $httpResult
    leftoverFolders = $leftFolders
    leftoverProcs   = $leftProcs
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $outFile -Encoding utf8

if (-not $backendPass -or $httpResult -eq 'FAIL') { exit 1 }
