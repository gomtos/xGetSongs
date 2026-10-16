# Builds dist\xGetSongs.exe: the shell (Tauri) with the sidecar image (the Kotlin server and the Java runtime it runs on) appended, so that
# one file is all there is to hand out. Needs what building the parts needs: JDK 21 for Gradle, Node.js, Rust. Run it with package-exe.bat.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$shellExe = Join-Path $root 'shell\src-tauri\target\release\shell.exe'
$pack = Join-Path $root 'sidecar-host\target\release\xgs-pack.exe'
$image = Join-Path $root 'server\build\sidecar-image'
$output = Join-Path $root 'dist\xGetSongs.exe'

# Runs a native command and stops at the first one that fails: PowerShell does not do that by itself.
function Step([string]$Title, [scriptblock]$Run) {
    Write-Host "== $Title" -ForegroundColor Cyan
    & $Run
    if ($LASTEXITCODE -ne 0) { throw "$Title failed (exit code $LASTEXITCODE)" }
}

Step 'sidecar image (Gradle)' { & (Join-Path $root 'gradlew.bat') --project-dir $root :server:sidecarImage --no-daemon }
if (-not (Test-Path (Join-Path $root 'shell\node_modules'))) {
    Step 'shell: npm install' { npm --prefix (Join-Path $root 'shell') ci }
}
# The shell is built as the product (not the check build), so a check build left in the same place is replaced.
Step 'shell (Tauri)' { npm --prefix (Join-Path $root 'shell') run tauri -- build --no-bundle }
Step 'xgs-pack (Cargo)' { cargo build --release --bin xgs-pack --manifest-path (Join-Path $root 'sidecar-host\Cargo.toml') }
Step 'pack' { & $pack $shellExe $image $output }

"{0}: {1:N1} MB" -f $output, ((Get-Item $output).Length / 1MB)
