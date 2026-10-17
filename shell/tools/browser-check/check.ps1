# Checks the hidden browser window of the Google lyrics lookup (src-tauri/src/browser_window.rs) in a real shell, with no Google: the check
# build of the shell runs it against the fixed pages of fixture.mjs and reports one line per thing it must do. Then the page closes the
# window of the app with the hidden window still open, and the app must end all the same.
#
#   pwsh -File check.ps1 -ShellExe <check build of shell.exe>
#
# The check build is `npm run tauri build -- --no-bundle --config check.conf.json` (its identifier is not the one of the app the user runs).
# The sidecar image must be built (`gradlew :server:sidecarImage`). Nothing of the user's is touched: the app data folder is a temp folder.
# Windows may flash on screen for a moment (the main window of the check build); the browser window must not be seen.
param(
    [Parameter(Mandatory)] [string] $ShellExe,
    [string] $Image = (Join-Path $PSScriptRoot "..\..\..\server\build\sidecar-image"),
    [int] $Port = 18765,
    [int] $EndLimitSeconds = 5
)
$ErrorActionPreference = "Stop"
$expected = "browserBuiltFast", "timersRunHidden", "windowIsReused", "cookieDoesNotSurviveTheWindow", "popupAndDownloadAreRefused", "hungPageIsReplaced", "closeRequestOnTheHiddenWindowIsHarmless", "windowLeftOpen"
$problems = [System.Collections.Generic.List[string]]::new()

if (-not (Test-Path $ShellExe)) { throw "no shell at $ShellExe" }
$Image = (Resolve-Path $Image).Path
$base = Join-Path $env:TEMP ("xgs-browser-check-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory $base | Out-Null
$record = "$base\check-record.txt"
$requests = "$base\requests.txt"
$download = Join-Path $env:USERPROFILE "Downloads\xgs-check-download.bin"
Remove-Item $download -ErrorAction SilentlyContinue

$fixture = Start-Process node -ArgumentList "`"$PSScriptRoot\fixture.mjs`"", $Port, "`"$requests`"" -PassThru -WindowStyle Hidden
$names = "XGS_SIDECAR_DIR", "XGS_SIDECAR_APPDATA", "XGS_SIDECAR_LOGDIR", "XGS_BRIDGE_CHECK_FILE", "XGS_CHECK_BROWSER_URL", "XGS_CHECK_BROWSER_KEEP"
try {
    Start-Sleep -Seconds 1
    $env:XGS_SIDECAR_DIR = $Image
    $env:XGS_SIDECAR_APPDATA = "$base\data"
    $env:XGS_SIDECAR_LOGDIR = "$base\log"
    $env:XGS_BRIDGE_CHECK_FILE = $record
    $env:XGS_CHECK_BROWSER_URL = "http://127.0.0.1:$Port"
    $env:XGS_CHECK_BROWSER_KEEP = "1"
    $shell = Start-Process -FilePath $ShellExe -PassThru

    $lines = @{}
    $deadline = (Get-Date).AddSeconds(120)
    do {
        Start-Sleep -Milliseconds 200
        if (Test-Path $record) {
            foreach ($line in Get-Content $record -Encoding UTF8) { $key, $value = $line -split "=", 2; $lines[$key] = $value }
        }
    } while ((Get-Date) -lt $deadline -and -not ($lines.Contains("done") -or $lines.Contains("error")))
    if ($lines.Contains("error")) { $problems.Add("the check page failed: $($lines['error'])") }
    if (-not $lines.Contains("done")) { $problems.Add("the check page never finished") }

    # The page has closed the window of the app, with the browser window open: the app must end, and the sidecar with it.
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $ended = $shell.WaitForExit($EndLimitSeconds * 1000)
    $endMs = [int]$watch.ElapsedMilliseconds
    if (-not $ended) { $problems.Add("browserWindowDoesNotKeepTheAppAlive: the app was still running $EndLimitSeconds s after its window was closed") }
    else { "browserWindowDoesNotKeepTheAppAlive=ok the app ended $endMs ms after the page closed its window" }

    foreach ($name in $expected) {
        $value = $lines["browser.$name"]
        if ($null -eq $value) { $problems.Add("${name}: no line") }
        elseif ($value -notlike "ok*") { $problems.Add("${name}: $value") }
        else { "$name=$value" }
    }

    # The two things the window refuses: the page tried (the fixture saw the request) and nothing arrived on disk.
    $asked = (Test-Path $requests) -and ((Get-Content $requests) -contains "/download.bin")
    if (-not $asked) { $problems.Add("the popup page never asked for the download: the check did not test it") }
    # A refused popup never loads its page: the fixture would have seen the request. (A window the app does not know of is not in its list.)
    if ((Test-Path $requests) -and ((Get-Content $requests) -contains "/popup-child")) { $problems.Add("popupAndDownloadAreRefused: the page opened another window (the fixture saw /popup-child)") }
    if (Test-Path $download) { $problems.Add("a download was saved: $download"); Remove-Item $download -ErrorAction SilentlyContinue }

    $left = Get-CimInstance Win32_Process -Filter "Name='java.exe'" | Where-Object { $_.CommandLine -like "*SidecarMainKt*" -and $_.CommandLine -like "*xgs-browser-check-*" }
    if ($left) { $problems.Add("the sidecar of the check is still running") }
}
finally {
    if ($shell -and -not $shell.HasExited) { $shell.Kill() }
    Stop-Process -Id $fixture.Id -ErrorAction SilentlyContinue
    foreach ($name in $names) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500
    Remove-Item $base -Recurse -Force -ErrorAction SilentlyContinue
}

if ($problems.Count -eq 0) { "ALL OK" } else { $problems | ForEach-Object { "FAIL $_" }; exit 1 }
