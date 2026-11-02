# Build-wfview-IC7610-EXT-v55.ps1
#
# v55: v54 rebuilt on current upstream wfview (GitHub wf-group/wfview master,
#      v2.25 = 0985685). No source patches: the tree is already on branch
#      ic7610-ext (v51+v53+v54 rebased onto upstream). This script only checks
#      the tree, does a CLEAN build stamped 2.25, and makes READY\...-v55.
#
#   Why clean: VERSION and the git hash are compiler defines; nmake does not
#   recompile objects when only defines change, so an incremental build would
#   keep showing 2.24 / the old hash. Use -NoClean to skip (faster, not stamped).
#
#   READY\wfview-IC7610-EXT-v54 (the dcd2c49 build) is left untouched.
#
# Usage:
#   .\Build-wfview-IC7610-EXT-v55.ps1            (check + clean build)
#   .\Build-wfview-IC7610-EXT-v55.ps1 -NoBuild   (checks only)

param(
    # All build trees and outputs live under D:\wfbuild testing.
    [string]$Root = "D:\wfbuild testing\wfview-agc-build",
    [string]$Branch = "ic7610-ext",
    [string]$Version = "2.25",
    [switch]$NoBuild,
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
Set-StrictMode -Version 2.0

function Step([string]$Text) {
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host $Text -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
}
function Fail([string]$Text) { throw $Text }

function Invoke-Exe {
    param(
        [Parameter(Mandatory=$true)][string]$File,
        [Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments
    )
    Write-Host ">> $File $($Arguments -join ' ')" -ForegroundColor DarkGray
    # qmake/nmake/windeployqt write ordinary warnings to stderr; judge by exit code.
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $File @Arguments 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                Write-Host $_.Exception.Message -ForegroundColor DarkYellow
            }
            else {
                Write-Host $_
            }
        }
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $prevEap
    }
    if ($code -ne 0) {
        Fail "Command failed with exit code $code`: $File $($Arguments -join ' ')"
    }
}

# Quiet git query; returns trimmed stdout and sets $script:GitCode.
function Get-GitOut {
    param([Parameter(ValueFromRemainingArguments=$true)][string[]]$GitArgs)
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $out = & $script:GitExe -C $WfviewDir @GitArgs 2>&1
    $script:GitCode = $LASTEXITCODE
    $ErrorActionPreference = $prevEap
    return (($out | ForEach-Object { "$_" }) -join "`n").Trim()
}

function Import-VcVars64 {
    $candidates = @(
        "D:\Program Files\VisualStudio\VC\Auxiliary\Build\vcvars64.bat",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )
    $vcvars = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $vcvars) { Fail "Could not locate vcvars64.bat." }
    $tmp = Join-Path $env:TEMP ("wfview-v55-vcvars-" + [guid]::NewGuid().ToString("N") + ".cmd")
    try {
        @"
@echo off
call "$vcvars" >nul
set
"@ | Set-Content -Path $tmp -Encoding ASCII
        $lines = & cmd.exe /d /c $tmp
        if ($LASTEXITCODE -ne 0) { Fail "vcvars64.bat failed." }
        foreach ($line in $lines) {
            if ($line -match '^([^=]+)=(.*)$') {
                [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
            }
        }
    }
    finally {
        Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    }
    Write-Host "MSVC x64 build environment loaded." -ForegroundColor Green
}

$WfviewDir = Join-Path $Root "wfview"
$CppFile   = Join-Path $WfviewDir "src\wfmain.cpp"
$AboutFile = Join-Path $WfviewDir "src\aboutbox.cpp"
$ProFile   = Join-Path $WfviewDir "wfview.pro"
$RigFile   = Join-Path $WfviewDir "rigs\IC-7610.rig"
$QtBin     = Join-Path $Root "Qt\6.8.1\msvc2022_64\bin"

# ----------------------------------------------------------------------
Step "Checking tree: branch $Branch on upstream v$Version"
# ----------------------------------------------------------------------

foreach ($p in @($CppFile, $AboutFile, $ProFile, $RigFile, (Join-Path $QtBin "qmake.exe"))) {
    if (-not (Test-Path $p)) { Fail "Missing required file: $p" }
}

$script:GitExe = (Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $script:GitExe) { Fail "git not found on PATH" }

$cur = Get-GitOut rev-parse --abbrev-ref HEAD
if ($cur -ne $Branch) { Fail "Tree is on '$cur', expected '$Branch'. Run: git -C `"$WfviewDir`" switch $Branch" }
$head = Get-GitOut rev-parse --short HEAD
Write-Host "  Branch $Branch at $head" -ForegroundColor Green

$null = Get-GitOut merge-base --is-ancestor upstream/master HEAD
if ($script:GitCode -ne 0) { Fail "HEAD does not contain upstream/master; the rebase did not complete." }
Write-Host "  Contains upstream/master ($(Get-GitOut rev-parse --short upstream/master))" -ForegroundColor Green

# Real edits only (line-ending-only differences are normal on Windows).
$real = Get-GitOut diff --ignore-all-space --ignore-cr-at-eol --stat
if ($real) { Write-Host "  WARNING: uncommitted source edits (will be built):`n$real" -ForegroundColor Yellow }
else { Write-Host "  No uncommitted source edits." -ForegroundColor Green }

$cpp = [System.IO.File]::ReadAllText($CppFile)
foreach ($marker in @("IC7610_V49_CW_DECODER_BTN", "IC7610_V50_CW_BTN_BOTTOM_ROW", "IC7610_V54_CW_MERGE", "IC7610_V54_CW_MERGE_BTN", "IC7610_V54_CW_PITCH_ALL")) {
    if (-not $cpp.Contains($marker)) { Fail "Expected marker missing from wfmain.cpp: $marker" }
}
Write-Host "  v54 markers present in wfmain.cpp" -ForegroundColor Green
if (-not ([System.IO.File]::ReadAllText($ProFile)).Contains("ic7610cwmulti.h")) { Fail "wfview.pro does not list ic7610cwmulti.h" }
if (-not ([System.IO.File]::ReadAllText($AboutFile)).Contains("github.com/wf-group/wfview")) {
    Fail "aboutbox.cpp is not the v2.25 version (no GitHub link); tree is not on the new upstream."
}
Write-Host "  Upstream v2.25 files present (aboutbox.cpp GitHub links)" -ForegroundColor Green

if ($NoBuild) {
    Write-Host ""
    Write-Host "-NoBuild given; checks passed, stopping before compile." -ForegroundColor Yellow
    return
}

# ----------------------------------------------------------------------
Step "Loading build environment"
# ----------------------------------------------------------------------

Import-VcVars64
if (($env:PATH -split ';') -notcontains $QtBin) { $env:PATH = "$QtBin;$env:PATH" }

$qmake       = Join-Path $QtBin "qmake.exe"
$windeployqt = Join-Path $QtBin "windeployqt.exe"

Step "Compiling wfview IC-7610 EXT v55 (wfview $Version, $head)"

Push-Location $WfviewDir
try {
    Invoke-Exe $qmake "wfview.pro" "CONFIG+=release" "VERSION=$Version"
    if (-not $NoClean) {
        Write-Host "Clean build (version/hash are compiler defines)." -ForegroundColor Cyan
        Invoke-Exe nmake.exe clean
    }
    Invoke-Exe nmake.exe
}
finally {
    Pop-Location
}

$BuiltDir = Join-Path $WfviewDir "wfview-release"
$BuiltExe = Join-Path $BuiltDir "wfview.exe"
if (-not (Test-Path $BuiltExe)) { Fail "Build completed without producing: $BuiltExe" }
$age = (Get-Date) - (Get-Item $BuiltExe).LastWriteTime
if (-not $NoClean -and $age.TotalMinutes -gt 30) { Fail "wfview.exe was not rebuilt (it is $([int]$age.TotalMinutes) minutes old)." }

Step "Deploying Qt runtime"
Invoke-Exe $windeployqt "--release" "--printsupport" "--no-system-d3d-compiler" $BuiltExe

# ----------------------------------------------------------------------
Step "Creating v55 READY folder"
# ----------------------------------------------------------------------

$ReadyRoot = Join-Path $Root "READY"
$Ready     = Join-Path $ReadyRoot "wfview-IC7610-EXT-v55"
New-Item -ItemType Directory -Force -Path $ReadyRoot | Out-Null

if (Test-Path $Ready) {
    try { Remove-Item $Ready -Recurse -Force }
    catch {
        $Ready = Join-Path $ReadyRoot ("wfview-IC7610-EXT-v55-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
        Write-Host "Existing v55 READY folder locked; using $Ready" -ForegroundColor Yellow
    }
}

New-Item -ItemType Directory -Force -Path $Ready | Out-Null
Copy-Item (Join-Path $BuiltDir "*") $Ready -Recurse -Force
$ReadyRigs = Join-Path $Ready "rigs"
New-Item -ItemType Directory -Force -Path $ReadyRigs | Out-Null
Copy-Item $RigFile (Join-Path $ReadyRigs "IC-7610.rig") -Force
# MIT license notice must travel with the binary.
Copy-Item (Join-Path $WfviewDir "src\ggmorse\LICENSE") (Join-Path $Ready "LICENSE-ggmorse.txt") -Force

$CheckpointRoot = Join-Path $Root "CHECKPOINTS"
$Checkpoint     = Join-Path $CheckpointRoot "wfview-IC7610-EXT-v55-candidate"
New-Item -ItemType Directory -Force -Path $CheckpointRoot | Out-Null
if (Test-Path $Checkpoint) { Remove-Item $Checkpoint -Recurse -Force }
Copy-Item $WfviewDir $Checkpoint -Recurse -Force

@"
wfview IC-7610 EXT v55 candidate - v54 on upstream wfview $Version
===================================================================

Built from branch $Branch at $head (GitHub Rippey5742/wfview).
Same IC-7610 features as v54; base moved from wfview dcd2c49 to $Version.

  1. Help > About: shows wfview version $Version and build $head.
  2. Connects to the IC-7610 as before (LAN).
  3. CW Sender / Decoder window: decode, [TX] lines, Multi-signal list.
  4. Notch box, CI-V tab, button bottom row look as in v54.
  5. Record 60 s still writes to the clips folder.
"@ | Set-Content -Path (Join-Path $Ready "IC7610-EXT-v55-CANDIDATE.txt") -Encoding UTF8

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "V55 BUILD COMPLETE (wfview $Version, $head)" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host "Run:" -ForegroundColor Cyan
Write-Host "  $(Join-Path $Ready 'wfview.exe')" -ForegroundColor White
