# Q7: hidden in-process WebView2 on a fake clean PC.
# Run 1 opens the local fixture page and expects the card text and ticking timers.
# Run 2 fakes a missing runtime (WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = empty folder) and
# expects exit code 0 with "ok": false, not a crash.
# During both runs every visible top-level window of the spike and of the fake PC's
# msedgewebview2 processes is recorded; the expected count is 0.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'fake-pc.ps1')

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$exeSrc = Join-Path $repo 'build\qt-spike\xgs-qt-spike.exe'
$fixture = Join-Path $repo 'shell\tools\browser-check\fixture.mjs'
$outFile = Join-Path $repo 'build\qt-spike\webview.json'
if (-not (Test-Path -LiteralPath $exeSrc)) { throw "exe가 없다: $exeSrc" }
$node = (Get-Command node -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $node) { throw 'node를 PATH에서 찾지 못했다' }

Add-Type -Namespace XgsSpike -Name Win -MemberDefinition @'
public delegate bool EnumProc(System.IntPtr hwnd, System.IntPtr lp);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr lp);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr hwnd);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr hwnd, out uint pid);
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] public static extern int GetClassName(System.IntPtr hwnd, System.Text.StringBuilder sb, int max);
public static System.Collections.Generic.List<string> Visible(uint[] pids) {
    var found = new System.Collections.Generic.List<string>();
    var set = new System.Collections.Generic.HashSet<uint>(pids);
    EnumWindows((h, l) => {
        uint pid; GetWindowThreadProcessId(h, out pid);
        if (set.Contains(pid) && IsWindowVisible(h)) {
            var sb = new System.Text.StringBuilder(256); GetClassName(h, sb, 256);
            found.Add(pid + ":" + sb.ToString());
        }
        return true;
    }, System.IntPtr.Zero);
    return found;
}
'@

# msedgewebview2 processes that belong to this fake PC (their command line names its folder).
function Get-FakePcWebViews($pc) {
    @(Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.Contains($pc.Root) })
}

# One launch, watched for visible windows until it exits; reads the report only after exit.
function Invoke-Check($pc, $exe, $report, [hashtable] $extraEnv) {
    Remove-Item -LiteralPath $report -Force -ErrorAction SilentlyContinue
    $p = Start-InFakePc -Pc $pc -Exe $exe -Arguments @('--report', $report, '--webview-check', $url) -ExtraEnv $extraEnv
    $visible = [System.Collections.Generic.HashSet[string]]::new()
    $edgePids = @()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $n = 0
    while (-not $p.HasExited -and $sw.Elapsed.TotalSeconds -lt 45) {
        if ($n % 5 -eq 0) { $edgePids = @(Get-FakePcWebViews $pc | ForEach-Object { [uint32]$_.ProcessId }) }
        foreach ($w in [XgsSpike.Win]::Visible([uint32[]](@([uint32]$p.Id) + $edgePids))) { [void]$visible.Add($w) }
        Start-Sleep -Milliseconds 100
        $n++
    }
    $timedOut = -not $p.HasExited
    if ($timedOut) { try { $p.Kill($true) } catch { } }
    [void]$p.WaitForExit(5000)
    $json = $null
    if (Test-Path -LiteralPath $report) {
        try { $json = Get-Content -LiteralPath $report -Raw -Encoding utf8 | ConvertFrom-Json } catch { }
    }
    @{
        TimedOut = $timedOut
        ExitCode = $(if ($timedOut) { $null } else { $p.ExitCode })
        Webview  = $(if ($json) { $json.webview } else { $null })
        Visible  = @($visible)
    }
}

# Free port on 127.0.0.1.
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$listener.Start(); $port = $listener.LocalEndpoint.Port; $listener.Stop()
$url = "http://127.0.0.1:$port/first"

$fixtureProc = $null
$pc = New-FakePc
$results = $null
try {
    # The fixture runs from this script (outside the fake PC), with no console window.
    $psi = [System.Diagnostics.ProcessStartInfo]::new($node)
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.ArgumentList.Add($fixture)
    $psi.ArgumentList.Add([string]$port)
    $psi.ArgumentList.Add((Join-Path $pc.Root 'fixture.log'))
    $fixtureProc = [System.Diagnostics.Process]::Start($psi)
    $line = $fixtureProc.StandardOutput.ReadLineAsync()
    if (-not $line.Wait(10000) -or $line.Result -notmatch 'listening') { throw 'fixture가 시작되지 않았다' }

    $exe = Join-Path $pc.AppDir 'xgs-qt-spike.exe'
    Copy-Item -LiteralPath $exeSrc -Destination $exe

    $a = Invoke-Check $pc $exe (Join-Path $pc.Root 'report-ok.json') @{}
    $emptyRuntime = Join-Path $pc.Root '빈 런타임'
    New-Item -ItemType Directory -Force -Path $emptyRuntime | Out-Null
    $b = Invoke-Check $pc $exe (Join-Path $pc.Root 'report-missing.json') @{ WEBVIEW2_BROWSER_EXECUTABLE_FOLDER = $emptyRuntime }
    $requests = @(Get-Content -LiteralPath (Join-Path $pc.Root 'fixture.log') -ErrorAction SilentlyContinue)
    $results = @{ A = $a; B = $b; Requests = $requests }
}
finally {
    if ($fixtureProc -and -not $fixtureProc.HasExited) {
        try { $fixtureProc.Kill() } catch { }
        [void]$fixtureProc.WaitForExit(5000)
    }
    # WebView2 child processes may hold the fake PC folder for a moment: wait for them,
    # and end only processes whose command line names this fake PC.
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ((Get-FakePcWebViews $pc).Count -gt 0 -and $sw.Elapsed.TotalSeconds -lt 15) { Start-Sleep -Milliseconds 300 }
    $leftEdge = Get-FakePcWebViews $pc
    $endedEdge = $leftEdge.Count
    foreach ($e in $leftEdge) { try { Stop-Process -Id $e.ProcessId -Force -ErrorAction Stop } catch { } }
    Remove-FakePc $pc
}

$a = $results.A; $b = $results.B
Write-Host '실행 1 (fixture /first):'
Write-Host ('  종료 코드 {0}, 시간 초과 {1}' -f $a.ExitCode, $a.TimedOut)
Write-Host ('  webview: {0}' -f ($a.Webview | ConvertTo-Json -Compress))
Write-Host ('  보인 창: {0}' -f $(if ($a.Visible.Count) { $a.Visible -join ', ' } else { '없음' }))
Write-Host '실행 2 (런타임 없음 흉내):'
Write-Host ('  종료 코드 {0}, 시간 초과 {1}' -f $b.ExitCode, $b.TimedOut)
Write-Host ('  webview: {0}' -f ($b.Webview | ConvertTo-Json -Compress))
Write-Host ('  보인 창: {0}' -f $(if ($b.Visible.Count) { $b.Visible -join ', ' } else { '없음' }))
Write-Host ('fixture가 받은 요청: {0}' -f ($results.Requests -join ', '))

$okPass = (-not $a.TimedOut) -and $a.ExitCode -eq 0 -and $a.Webview -and $a.Webview.ok -eq $true -and
    $a.Webview.text -eq 'card of first' -and [int]$a.Webview.ticks -gt 0
$missingPass = (-not $b.TimedOut) -and $b.ExitCode -eq 0 -and $b.Webview -and $b.Webview.ok -eq $false -and
    [string]$b.Webview.error
$hiddenPass = ($a.Visible.Count -eq 0) -and ($b.Visible.Count -eq 0)

Write-Host ("실행 1 ok·'card of first'·ticks > 0: {0}" -f $(if ($okPass) { 'PASS' } else { 'FAIL' }))
Write-Host ("실행 2 종료 코드 0·ok:false: {0}" -f $(if ($missingPass) { 'PASS' } else { 'FAIL' }))
Write-Host ("보인 창 0개: {0}" -f $(if ($hiddenPass) { 'PASS' } else { 'FAIL' }))

$leftFolders = @(Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'xgs-qt-spike-*' -ErrorAction SilentlyContinue).Count
$leftProcs = @(Get-Process -Name 'xgs-qt-spike' -ErrorAction SilentlyContinue).Count
$leftNode = $(if ($fixtureProc -and -not $fixtureProc.HasExited) { 1 } else { 0 })
Write-Host "남은 xgs-qt-spike-* 폴더: $leftFolders, 남은 xgs-qt-spike 프로세스: $leftProcs, 남은 fixture: $leftNode, 정리 때 끝낸 가짜 PC msedgewebview2: $endedEdge"

[ordered]@{
    ok              = [ordered]@{ exitCode = $a.ExitCode; timedOut = $a.TimedOut; webview = $a.Webview; visibleWindows = $a.Visible }
    runtimeMissing  = [ordered]@{ exitCode = $b.ExitCode; timedOut = $b.TimedOut; webview = $b.Webview; visibleWindows = $b.Visible }
    fixtureRequests = $results.Requests
    okPass          = $okPass
    missingPass     = $missingPass
    hiddenPass      = $hiddenPass
    endedWebViews   = $endedEdge
    leftoverFolders = $leftFolders
    leftoverProcs   = $leftProcs
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $outFile -Encoding utf8

if (-not ($okPass -and $missingPass -and $hiddenPass)) { exit 1 }
