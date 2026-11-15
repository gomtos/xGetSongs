# Q5 (stop gate): are the child's descendants gone within 5 s after cancel / after the spike is killed?
# Four scenarios, each in its own fake clean PC:
#   A cancel        --spawn-tree 120 --cancel-after 1500          the exe cancels by itself
#   B force kill    --spawn-tree 120                              Stop-Process -Force on the spike once PING.EXE is seen
#   C yt-dlp cancel --spawn-cmd "yt-dlp -J <url>" --cancel-after 700
#   D yt-dlp kill   --spawn-cmd "yt-dlp -J <url>"                 Stop-Process -Force on the spike once yt-dlp has a child
# Pass: the alive count of the collected tree reaches 0 within 5 s, and is still 0 two seconds later.
# The tree (the reported child pid and everything below it, by ParentProcessId) is collected BEFORE the
# action; each entry keeps its creation time, so a reused pid is never counted as alive.
# Only the spike exe this script started and the processes in that tree are ever stopped.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'fake-pc.ps1')

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$exeSrc = Join-Path $repo 'build\qt-spike\xgs-qt-spike.exe'
if (-not (Test-Path -LiteralPath $exeSrc)) { throw "exe가 없다: $exeSrc" }
$videoUrl = 'https://www.youtube.com/watch?v=jNQXAC9IVRw'
$passMs = 5000
$stillMs = 2000
$observeMs = 15000   # keep counting past 5 s on a failure, to report the real number

function Read-Report($path) {
    for ($i = 0; $i -lt 5; $i++) {
        if (-not (Test-Path -LiteralPath $path)) { return $null }
        try { return (Get-Content -LiteralPath $path -Raw -Encoding utf8 -ErrorAction Stop | ConvertFrom-Json) }
        catch { Start-Sleep -Milliseconds 20 }   # QSaveFile rename in progress
    }
    $null
}

# One CIM snapshot; returns the tree under $rootPid (root included) as entries @{ Pid; Name; Created }.
# A child counts only if it was created after its parent (guards against a reused parent pid).
function Get-Tree([int]$rootPid, [datetime]$rootCreated) {
    $all = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, CreationDate, Name)
    $byParent = @{}
    $root = $null
    foreach ($p in $all) {
        $ppid = [int]$p.ParentProcessId
        if (-not $byParent.ContainsKey($ppid)) { $byParent[$ppid] = [System.Collections.Generic.List[object]]::new() }
        $byParent[$ppid].Add($p)
        if ([int]$p.ProcessId -eq $rootPid) { $root = $p }
    }
    if ($null -eq $root) { return @() }
    if ([math]::Abs(($root.CreationDate - $rootCreated).TotalMilliseconds) -gt 5) { return @() }
    $out = @()
    $queue = [System.Collections.Generic.Queue[object]]::new()
    $queue.Enqueue($root)
    while ($queue.Count -gt 0) {
        $p = $queue.Dequeue()
        $out += @{ Pid = [int]$p.ProcessId; Name = [string]$p.Name; Created = $p.CreationDate }
        if ($byParent.ContainsKey([int]$p.ProcessId)) {
            foreach ($c in $byParent[[int]$p.ProcessId]) {
                if ([int]$c.ProcessId -ne [int]$p.ProcessId -and $c.CreationDate -ge $p.CreationDate) { $queue.Enqueue($c) }
            }
        }
    }
    $out
}

function Add-ToUnion($union, $entries) {
    foreach ($e in $entries) { $union["$($e.Pid)|$($e.Created.Ticks)"] = $e }
}

# Alive = same pid, same creation time, not exited. If it cannot be verified, it counts as alive.
function Test-Alive($e) {
    try { $p = [System.Diagnostics.Process]::GetProcessById($e.Pid) } catch { return $false }
    try {
        if ($p.HasExited) { return $false }
        return ([math]::Abs(($p.StartTime - $e.Created).TotalMilliseconds) -le 5)
    }
    catch { return $true }
    finally { $p.Dispose() }
}

function Get-AliveCount($union) { @($union.Values | Where-Object { Test-Alive $_ }).Count }

function Format-Names($union) {
    ($union.Values | Group-Object { $_.Name } | Sort-Object Name | ForEach-Object {
        if ($_.Count -gt 1) { "$($_.Name)×$($_.Count)" } else { $_.Name }
    }) -join ', '
}

# Runs one attempt. $Action: 'self' (the exe cancels) or 'kill' (Stop-Process -Force on the spike).
# $Ready: scriptblock over the union. 'kill': the kill happens once it returns $true.
#   Both: it must be true for the tree collected before the action, or the attempt is an error.
# Returns @{ Result = 'pass'|'fail'|'tooFast'|'error'; Names; ZeroMs; StillZero; Alive; Detail }.
function Invoke-Attempt([string[]]$SpikeArgs, [string]$Action, [scriptblock]$Ready, [hashtable]$ExtraEnv) {
    $pc = New-FakePc
    $proc = $null
    $union = @{}
    try {
        $exe = Join-Path $pc.AppDir 'xgs-qt-spike.exe'
        Copy-Item -LiteralPath $exeSrc -Destination $exe
        $report = Join-Path $pc.Root 'report.json'
        if ($ExtraEnv.ContainsKey('__copyYtDlp')) {
            $ytDir = Join-Path $pc.Root 'yt-dlp'
            New-Item -ItemType Directory -Force -Path $ytDir | Out-Null
            Copy-Item -LiteralPath $ExtraEnv['__copyYtDlp'] -Destination $ytDir   # read-only use of the source
            $ExtraEnv = @{ PATH = "$ytDir;$($ExtraEnv['PATH'])" }
        }
        $proc = Start-InFakePc -Pc $pc -Exe $exe -Arguments (@('--report', $report) + $SpikeArgs) -ExtraEnv $ExtraEnv

        # 1. Wait for the reported child pid.
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $childPid = $null
        while ($sw.ElapsedMilliseconds -lt 15000) {
            $j = Read-Report $report
            if ($j -and $j.spawned) {
                if ($null -ne $j.spawned.pid) { $childPid = [int]$j.spawned.pid; break }
                return @{ Result = 'error'; Detail = "자식 시작 실패: $($j.spawned.error)" }
            }
            if ($proc.HasExited) { return @{ Result = 'error'; Detail = "스파이크가 먼저 끝남 (코드 $($proc.ExitCode))" } }
            Start-Sleep -Milliseconds 20
        }
        if ($null -eq $childPid) { return @{ Result = 'error'; Detail = '15초 안에 spawned 보고 없음' } }
        try {
            $cp = [System.Diagnostics.Process]::GetProcessById($childPid)
            $childCreated = $cp.StartTime; $cp.Dispose()
        }
        catch { return @{ Result = 'tooFast'; Detail = '자식이 보기 전에 끝남' } }

        # 2. Collect the tree until the action.
        $actionEpochMs = $null
        $sw.Restart()
        while ($true) {
            Add-ToUnion $union (Get-Tree $childPid $childCreated)
            $j = Read-Report $report
            if ($Action -eq 'self') {
                if ($j -and $j.cancelled) { $actionEpochMs = [int64]$j.cancelled.atEpochMs; break }
                if ($j -and $j.finished) { return @{ Result = 'tooFast'; Names = (Format-Names $union); Count = $union.Count; Detail = '취소 전에 자식이 끝남' } }
            }
            else {
                if ($j -and $j.finished) { return @{ Result = 'tooFast'; Names = (Format-Names $union); Count = $union.Count; Detail = '강제 종료 전에 자식이 끝남' } }
                if (& $Ready $union) {
                    Add-ToUnion $union (Get-Tree $childPid $childCreated)   # last look right before the kill
                    $actionEpochMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
                    Stop-Process -Id $proc.Id -Force
                    break
                }
            }
            if ($proc.HasExited -and $Action -ne 'self') { return @{ Result = 'error'; Detail = '스파이크가 먼저 끝남' } }
            if ($sw.ElapsedMilliseconds -gt 20000) { return @{ Result = 'error'; Names = (Format-Names $union); Count = $union.Count; Detail = '20초 안에 동작 시점이 오지 않음' } }
        }

        # A tree without the expected processes would make the count meaningless (0 alive from the start).
        if ($union.Count -eq 0) { return @{ Result = 'error'; Detail = '동작 전에 자손 트리를 모으지 못함' } }
        if ($Ready -and -not (& $Ready $union)) {
            return @{ Result = 'error'; Names = (Format-Names $union); Count = $union.Count; Detail = '동작 전 트리에 기대한 프로세스가 없음' }
        }

        # 3. Count the alive entries every 100 ms from the action time.
        $zeroMs = $null
        $alive = $null
        while ($true) {
            $alive = Get-AliveCount $union
            $elapsed = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - $actionEpochMs
            if ($alive -eq 0) { $zeroMs = $elapsed; break }
            if ($elapsed -gt $observeMs) { break }
            Start-Sleep -Milliseconds 100
        }
        $stillZero = $false
        $aliveLater = $null
        if ($null -ne $zeroMs) {
            Start-Sleep -Milliseconds $stillMs
            $aliveLater = Get-AliveCount $union
            $stillZero = ($aliveLater -eq 0)
        }
        $ok = ($null -ne $zeroMs) -and ($zeroMs -le $passMs) -and $stillZero
        @{
            Result    = $(if ($ok) { 'pass' } else { 'fail' })
            Names     = (Format-Names $union)
            Count     = $union.Count
            ZeroMs    = $zeroMs
            StillZero = $stillZero
            Alive     = $(if ($null -eq $zeroMs) { $alive } else { $aliveLater })
            Detail    = ''
        }
    }
    finally {
        if ($proc -and -not $proc.HasExited) {
            try { $proc.Kill() } catch { }   # this one spike process only
            [void]$proc.WaitForExit(5000)
        }
        foreach ($e in $union.Values) {
            if (Test-Alive $e) { try { Stop-Process -Id $e.Pid -Force } catch { } }
        }
        Remove-FakePc $pc
    }
}

function Write-Result([string]$label, $r, [string]$extra = '') {
    $word = switch ($r.Result) { 'pass' { '통과' } 'fail' { '실패' } 'skip' { '건너뜀' } 'tooFast' { '너무 빨리 끝남' } default { '오류' } }
    $parts = @("${label}: $word")
    if ($r.Names) { $parts += "찾은 프로세스($($r.Count)): $($r.Names)" }
    if ($r.Result -in @('pass', 'fail')) {
        $parts += $(if ($null -ne $r.ZeroMs) { "0까지 $($r.ZeroMs) ms" } else { "$observeMs ms 뒤에도 $($r.Alive)개 살아 있음" })
        if ($null -ne $r.ZeroMs) { $parts += $(if ($r.StillZero) { '2초 뒤에도 0' } else { "2초 뒤 $($r.Alive)개" }) }
    }
    if ($r.Detail) { $parts += $r.Detail }
    if ($extra) { $parts += $extra }
    Write-Host ($parts -join ' | ')
}

# yt-dlp for C/D: PATH first; otherwise the app's own copy under %APPDATA%\xGetSongs, read only
# (it is copied into the fake PC, so the user's file is never held open while the app might update it).
$ytEnv = $null
$ytWhere = $null
$node = Get-Command node -ErrorAction SilentlyContinue
$nodeDir = if ($node) { Split-Path -Parent $node.Source } else { $null }
$basePath = 'C:\Windows\System32;C:\Windows'
if ($nodeDir) { $basePath = "$nodeDir;$basePath" }
$ytCmd = Get-Command yt-dlp -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($ytCmd) {
    $ytEnv = @{ PATH = "$(Split-Path -Parent $ytCmd.Source);$basePath" }
    $ytWhere = $ytCmd.Source
}
else {
    $appYt = Join-Path $env:APPDATA 'xGetSongs\bin\yt-dlp.exe'
    if (-not (Test-Path -LiteralPath $appYt)) {
        $appYt = Get-ChildItem -LiteralPath (Join-Path $env:APPDATA 'xGetSongs') -Recurse -Filter 'yt-dlp.exe' -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
    }
    if ($appYt -and (Test-Path -LiteralPath $appYt)) {
        $ytEnv = @{ PATH = $basePath; __copyYtDlp = $appYt }
        $ytWhere = "$appYt (가짜 PC로 복사해 씀)"
    }
}
Write-Host "yt-dlp: $(if ($ytWhere) { $ytWhere } else { '찾지 못함' }) / node: $(if ($nodeDir) { $nodeDir } else { '찾지 못함' })"
Write-Host ''

$results = [ordered]@{}

$readyPing = { param($u) @($u.Values | Where-Object { $_.Name -ieq 'PING.EXE' }).Count -gt 0 }
$results.A = Invoke-Attempt @('--spawn-tree', '120', '--cancel-after', '1500') 'self' $readyPing @{}
Write-Result 'A 취소' $results.A

$results.B = Invoke-Attempt @('--spawn-tree', '120') 'kill' $readyPing @{}
Write-Result 'B 강제 종료' $results.B

# yt-dlp.exe (PyInstaller onefile) is the child itself; D waits until a real process (not the
# console host) runs below it, e.g. the onefile's own yt-dlp.exe child.
$seenYt = { param($u) @($u.Values | Where-Object { $_.Name -ieq 'yt-dlp.exe' }).Count -gt 0 }
$readyYt = { param($u)
    (@($u.Values | Where-Object { $_.Name -ieq 'yt-dlp.exe' }).Count -gt 0) -and
    (@($u.Values | Where-Object { $_.Name -ine 'conhost.exe' }).Count -ge 2)
}
foreach ($s in @(
        @{ Key = 'C'; Label = 'C yt-dlp 취소'; Args = @('--spawn-cmd', "yt-dlp -J $videoUrl", '--cancel-after', '700'); Action = 'self'; Ready = $seenYt },
        @{ Key = 'D'; Label = 'D yt-dlp 강제 종료'; Args = @('--spawn-cmd', "yt-dlp -J $videoUrl"); Action = 'kill'; Ready = $readyYt })) {
    if (-not $ytEnv) {
        $results[$s.Key] = @{ Result = 'skip'; Detail = 'yt-dlp를 찾지 못함' }
        Write-Result $s.Label $results[$s.Key]
        continue
    }
    for ($try = 1; $try -le 3; $try++) {
        $r = Invoke-Attempt $s.Args $s.Action $s.Ready $ytEnv.Clone()
        Write-Result $s.Label $r "회차 $try"
        $results[$s.Key] = $r
        if ($r.Result -ne 'tooFast') { break }
    }
}

Write-Host ''
$failed = @($results.Keys | Where-Object { $results[$_].Result -notin @('pass', 'skip') })
foreach ($k in $results.Keys) {
    $word = switch ($results[$k].Result) { 'pass' { 'PASS' } 'skip' { 'SKIP' } default { 'FAIL' } }
    Write-Host "${k}: $word"
}
$leftFolders = @(Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'xgs-qt-spike-*' -ErrorAction SilentlyContinue).Count
$leftProcs = @(Get-Process -Name 'xgs-qt-spike' -ErrorAction SilentlyContinue).Count
Write-Host "남은 xgs-qt-spike-* 폴더: $leftFolders, 남은 xgs-qt-spike 프로세스: $leftProcs"
if ($failed.Count -gt 0) { exit 1 }
