# Build-wfview-IC7610-EXT-v51.ps1
#
# v51: CW decoder for hand-sent CW, plus a clip recorder.
#
#   1. New "Decoder:" choice in the CW Decoder window:
#        Machine (keyer) - ggmorse, as before.
#        Hand key        - new engine (include\ic7610cwhand.h). Learns each
#                          sender's own dit, dah and gap lengths, keeps
#                          adapting, and splits each word into the most
#                          likely valid letters. Prints a word at a time,
#                          about a second behind.
#   2. "Record 60 s" button: saves exactly what the decoder hears as a mono
#      16-bit WAV in D:\wfbuild testing\cw-clips (falls back to
#      Documents\wfview-cw-clips if that folder does not exist), plus an
#      "Open clips folder" button.
#
# Offline test (synthetic 18 WPM QSO text, character accuracy):
#      fist                        Machine   Hand key
#      keyer / good (10% jitter)    91-98%     100%
#      average (20%, odd ratios)    38-44%    91-99%
#      sloppy (25%, speed drift)    23-35%    51-62%
#      keyer at +3 dB SNR / 500 Hz    58%        85%
#
# Updated files come from D:\wfbuild testing\v51-payload.
# Expected input: the source tree after Build-wfview-IC7610-EXT-v50.ps1.
# Patches are idempotent; -NoBuild patches only.
# Keep this file ASCII-only (Windows PowerShell 5.1 reads BOM-less files as ANSI).

param(
    # All build trees and outputs live under D:\wfbuild testing.
    [string]$Root = "D:\wfbuild testing\wfview-agc-build",
    [string]$Payload = "D:\wfbuild testing\v51-payload",
    [switch]$NoBuild
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

function Read-Norm([string]$Path) {
    [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
}

function Write-Norm([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText(
        $Path,
        $Text.Replace("`r`n", "`n"),
        (New-Object System.Text.UTF8Encoding($false)))
}

function Replace-Once([string]$Text, [string]$Old, [string]$New, [string]$Label) {
    $Old = $Old.Replace("`r`n", "`n")
    $New = $New.Replace("`r`n", "`n")
    $first = $Text.IndexOf($Old, [System.StringComparison]::Ordinal)
    if ($first -lt 0) { Fail "Anchor not found: $Label" }
    $second = $Text.IndexOf($Old, $first + 1, [System.StringComparison]::Ordinal)
    if ($second -ge 0) { Fail "Anchor not unique: $Label" }
    Write-Host "  Patched: $Label" -ForegroundColor Green
    return $Text.Substring(0, $first) + $New + $Text.Substring($first + $Old.Length)
}

function Invoke-Exe {
    param(
        [Parameter(Mandatory=$true)][string]$File,
        [Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments
    )
    Write-Host ">> $File $($Arguments -join ' ')" -ForegroundColor DarkGray

    # qmake/nmake/windeployqt write ordinary warnings to stderr. With
    # ErrorActionPreference=Stop, Windows PowerShell turns those into a
    # terminating NativeCommandError. Judge success by exit code only.
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

    $tmp = Join-Path $env:TEMP ("wfview-v51-vcvars-" + [guid]::NewGuid().ToString("N") + ".cmd")
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
$ProFile   = Join-Path $WfviewDir "wfview.pro"
$RigFile   = Join-Path $WfviewDir "rigs\IC-7610.rig"
$QtBin     = Join-Path $Root "Qt\6.8.1\msvc2022_64\bin"

$PayloadFiles = @(
    "include\ic7610cwhand.h",
    "include\ic7610cwcore.h",
    "include\ic7610cwdecoder.h",
    "src\ic7610cwdecoder.cpp"
)

# ----------------------------------------------------------------------
Step "Checking v50 source state and v51 payload"
# ----------------------------------------------------------------------

foreach ($p in @($CppFile, $ProFile, $RigFile, (Join-Path $QtBin "qmake.exe"))) {
    if (-not (Test-Path $p)) { Fail "Missing required file: $p" }
}

$cpp = Read-Norm $CppFile
$pro = Read-Norm $ProFile

foreach ($marker in @("IC7610_V50_CW_BTN_BOTTOM_ROW", "IC7610_V49_CW_DECODER_PITCH")) {
    if (-not $cpp.Contains($marker)) { Fail "Expected v50 source; missing marker: $marker (run Build-wfview-IC7610-EXT-v50.ps1 first)" }
    Write-Host "  OK: $marker" -ForegroundColor Green
}
$decNow = Read-Norm (Join-Path $WfviewDir "src\ic7610cwdecoder.cpp")
if (-not $decNow.Contains("IC7610_V50_CW_LAYOUT")) { Fail "Expected v50 decoder files (IC7610_V50_CW_LAYOUT); run v50 first" }
Write-Host "  OK: IC7610_V50_CW_LAYOUT" -ForegroundColor Green

foreach ($rel in $PayloadFiles) {
    $src = Join-Path $Payload $rel
    if (-not (Test-Path $src)) { Fail "Missing payload file: $src" }
}
Write-Host "  OK: payload ($($PayloadFiles.Count) files) in $Payload" -ForegroundColor Green

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$BackupDir = Join-Path $Root "CHECKPOINTS\pre-v51-$stamp"
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
Copy-Item $ProFile (Join-Path $BackupDir "wfview.pro") -Force
foreach ($rel in $PayloadFiles) {
    $cur = Join-Path $WfviewDir $rel
    if (Test-Path $cur) { Copy-Item $cur (Join-Path $BackupDir (Split-Path $rel -Leaf)) -Force }
}
Write-Host "Backup: $BackupDir" -ForegroundColor DarkGray

# ----------------------------------------------------------------------
Step "Installing v51 CW decoder files"
# ----------------------------------------------------------------------

foreach ($rel in $PayloadFiles) {
    Copy-Item (Join-Path $Payload $rel) (Join-Path $WfviewDir $rel) -Force
    Write-Host "  Installed: $rel" -ForegroundColor Green
}

# ----------------------------------------------------------------------
Step "wfview.pro: add ic7610cwhand.h"
# ----------------------------------------------------------------------

if ($pro.Contains("include/ic7610cwhand.h")) {
    Write-Host "  wfview.pro already lists ic7610cwhand.h; skipping." -ForegroundColor Yellow
}
else {
    $pro = Replace-Once $pro @'
    include/ic7610cwcore.h \
'@ @'
    include/ic7610cwcore.h \
    include/ic7610cwhand.h \
'@ "pro: HEADERS ic7610cwhand.h"
}

# ----------------------------------------------------------------------
Step "v51 sanity checks"
# ----------------------------------------------------------------------

$handH  = Read-Norm (Join-Path $WfviewDir "include\ic7610cwhand.h")
$coreH  = Read-Norm (Join-Path $WfviewDir "include\ic7610cwcore.h")
$decCpp = Read-Norm (Join-Path $WfviewDir "src\ic7610cwdecoder.cpp")
foreach ($pair in @(
    @($handH,  "class Ic7610CwHandDecoder"),
    @($coreH,  '#include "ic7610cwhand.h"'),
    @($coreH,  "enum class Engine { Machine, Hand };"),
    @($decCpp, "IC7610_V51_CW_HANDKEY"),
    @($decCpp, "IC7610_V50_CW_LAYOUT"),
    @($decCpp, "void Ic7610CwDecoderWorker::startRecording("),
    @($pro,    "include/ic7610cwhand.h")
)) {
    if (-not $pair[0].Contains($pair[1])) { Fail "Sanity check failed: $($pair[1])" }
    Write-Host "  OK: $($pair[1])" -ForegroundColor Green
}

Write-Norm $ProFile $pro
Write-Host "Source written." -ForegroundColor Green

New-Item -ItemType Directory -Force -Path "D:\wfbuild testing\cw-clips" | Out-Null

if ($NoBuild) {
    Write-Host "-NoBuild given; stopping before compile." -ForegroundColor Yellow
    return
}

# ----------------------------------------------------------------------
Step "Loading build environment"
# ----------------------------------------------------------------------

Import-VcVars64
if (($env:PATH -split ';') -notcontains $QtBin) { $env:PATH = "$QtBin;$env:PATH" }

$qmake       = Join-Path $QtBin "qmake.exe"
$windeployqt = Join-Path $QtBin "windeployqt.exe"

Step "Compiling wfview IC-7610 EXT v51"

Push-Location $WfviewDir
try {
    # wfview.pro changed (new header), so qmake regenerates the Makefiles.
    Invoke-Exe $qmake "wfview.pro" "CONFIG+=release" 'VERSION=2.24'
    Invoke-Exe nmake.exe
}
finally {
    Pop-Location
}

$BuiltDir = Join-Path $WfviewDir "wfview-release"
$BuiltExe = Join-Path $BuiltDir "wfview.exe"
if (-not (Test-Path $BuiltExe)) { Fail "Build completed without producing: $BuiltExe" }

Step "Deploying Qt runtime"
Invoke-Exe $windeployqt "--release" "--printsupport" "--no-system-d3d-compiler" $BuiltExe

# ----------------------------------------------------------------------
Step "Creating v51 READY folder"
# ----------------------------------------------------------------------

$ReadyRoot = Join-Path $Root "READY"
$Ready     = Join-Path $ReadyRoot "wfview-IC7610-EXT-v51"
New-Item -ItemType Directory -Force -Path $ReadyRoot | Out-Null

if (Test-Path $Ready) {
    try { Remove-Item $Ready -Recurse -Force }
    catch {
        $Ready = Join-Path $ReadyRoot ("wfview-IC7610-EXT-v51-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
        Write-Host "Existing v51 READY folder locked; using $Ready" -ForegroundColor Yellow
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
$Checkpoint     = Join-Path $CheckpointRoot "wfview-IC7610-EXT-v51-candidate"
New-Item -ItemType Directory -Force -Path $CheckpointRoot | Out-Null
if (Test-Path $Checkpoint) { Remove-Item $Checkpoint -Recurse -Force }
Copy-Item $WfviewDir $Checkpoint -Recurse -Force

@"
wfview IC-7610 EXT v51 candidate - hand-key CW decoding + clip recorder
=======================================================================

  1. CW Decoder window: new "Decoder:" box (Machine (keyer) / Hand key),
     fully visible, nothing clipped. "Record 60 s" and "Open clips folder"
     buttons under the text.
  2. Hand key on a straight-key / bug station: text appears a word at a
     time about 1 s behind; Speed shows the sender's speed after ~6 marks.
  3. Same station in Machine mode for comparison.
  4. Record 60 s while Decode is on: label counts up, then shows
     "Saved 60 s: D:\wfbuild testing\cw-clips\cw-....wav". "Stop recording"
     ends early. File plays in any audio player.
  5. Machine mode on keyer CW still copies as in v50 (regression).
"@ | Set-Content -Path (Join-Path $Ready "IC7610-EXT-v51-CANDIDATE.txt") -Encoding UTF8

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "V51 BUILD COMPLETE" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host "Run:" -ForegroundColor Cyan
Write-Host "  $(Join-Path $Ready 'wfview.exe')" -ForegroundColor White
