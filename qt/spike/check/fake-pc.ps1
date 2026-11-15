# Fake clean PC helpers (dot-sourced by the check scripts).
# The fake PC is a folder under %TEMP% with empty APPDATA/LOCALAPPDATA/TEMP and a minimal PATH.
# Children get this environment through ProcessStartInfo.Environment; the parent's environment is never changed.

function New-FakePc {
    $id = [guid]::NewGuid().ToString('N').Substring(0, 8)
    $root = Join-Path $env:TEMP "xgs-qt-spike-$id"
    $roaming = Join-Path $root '로밍 폴더'
    $local = Join-Path $root '로컬 폴더'
    $temp = Join-Path $root 'temp'
    $appDir = Join-Path $root '내 앱 폴더'
    foreach ($d in @($root, $roaming, $local, $temp, $appDir)) {
        New-Item -ItemType Directory -Force -Path $d | Out-Null
    }
    @{
        Root   = $root
        AppDir = $appDir
        Env    = @{
            APPDATA      = $roaming
            LOCALAPPDATA = $local
            TEMP         = $temp
            TMP          = $temp
            PATH         = 'C:\Windows\System32;C:\Windows'
        }
    }
}

# Starts $Exe in the fake PC's environment.
# -ExtraEnv entries override or add to the fake environment for this launch; a $null value removes the variable.
# Without -Wait: returns the started Process.
# With -Wait: waits up to -TimeoutSec, kills that one process on timeout, returns @{ ExitCode; TimedOut; Process }.
function Start-InFakePc {
    param(
        [Parameter(Mandatory)] $Pc,
        [Parameter(Mandatory)] [string] $Exe,
        [string[]] $Arguments = @(),
        [switch] $Wait,
        [int] $TimeoutSec = 30,
        [hashtable] $ExtraEnv = @{}
    )
    $psi = [System.Diagnostics.ProcessStartInfo]::new($Exe)
    $psi.UseShellExecute = $false
    $psi.WorkingDirectory = Split-Path -Parent $Exe
    foreach ($a in $Arguments) { $psi.ArgumentList.Add($a) }
    foreach ($k in $Pc.Env.Keys) { $psi.Environment[$k] = $Pc.Env[$k] }
    foreach ($k in $ExtraEnv.Keys) {
        if ($null -eq $ExtraEnv[$k]) { [void]$psi.Environment.Remove($k) }
        else { $psi.Environment[$k] = [string]$ExtraEnv[$k] }
    }
    $proc = [System.Diagnostics.Process]::Start($psi)
    if (-not $Wait) { return $proc }
    $timedOut = $false
    if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
        $timedOut = $true
        try { $proc.Kill($true) } catch { }
        [void]$proc.WaitForExit(5000)
    }
    @{ ExitCode = $(if ($timedOut) { $null } else { $proc.ExitCode }); TimedOut = $timedOut; Process = $proc }
}

function Remove-FakePc {
    param([Parameter(Mandatory)] $Pc)
    # Retry: antivirus or a just-exited process may still hold a file for a moment.
    for ($i = 0; $i -lt 10; $i++) {
        if (-not (Test-Path -LiteralPath $Pc.Root)) { return }
        try { Remove-Item -LiteralPath $Pc.Root -Recurse -Force -ErrorAction Stop; return }
        catch { Start-Sleep -Milliseconds 300 }
    }
    if (Test-Path -LiteralPath $Pc.Root) { Write-Warning "가짜 PC 폴더를 지우지 못했다: $($Pc.Root)" }
}
