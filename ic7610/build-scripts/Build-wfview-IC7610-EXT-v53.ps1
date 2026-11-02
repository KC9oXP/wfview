# Build-wfview-IC7610-EXT-v53.ps1
#
# v53: hand-key engine tuned on the second batch of real clips
# (6 clips, 2026-11-01 22:06-22:12; all 13 clips re-checked).
#
#   1. QSB: the signal-level reference now follows each mark (~30 ms)
#      instead of holding the strongest recent peak for seconds. During a
#      fade the threshold used to sit above the faded dits, which came out
#      short or vanished (clip 221232). E.g. "VIBR WPLEX DUG KEY H RE" ->
#      "VIBROPLEX BUG KEY HERE", "VO DUCI FEE THE CHAT" -> "VY MUCH FER THE
#      CHAT TONITE".
#   2. Run-together words: long letter gaps inside a timed "word" are now
#      offered to the word joiner as split points ("7373DEN9HAL" ->
#      "73 73 ..."). Short vocabulary words (R, I, K, ES) count for less,
#      single digits are not evidence, callsign prefixes max 2 characters.
#   3. Weak signals: tone lock threshold 9 -> 7 dB (clip 220759 now copies
#      "... <SK> W4EDN GN ..." instead of nothing; noise-only test still
#      prints nothing).
#
# Files from D:\wfbuild testing\v53-payload (ic7610cwcore.h, ic7610cwhand.h,
# ic7610cwwords.h). Expected input: tree after v52. No wfview.pro change.
# Patches are idempotent; -NoBuild patches only.
# Keep this file ASCII-only (Windows PowerShell 5.1 reads BOM-less files as ANSI).

param(
    # All build trees and outputs live under D:\wfbuild testing.
    [string]$Root = "D:\wfbuild testing\wfview-agc-build",
    [string]$Payload = "D:\wfbuild testing\v53-payload",
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

    $tmp = Join-Path $env:TEMP ("wfview-v53-vcvars-" + [guid]::NewGuid().ToString("N") + ".cmd")
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
$ProFile   = Join-Path $WfviewDir "wfview.pro"
$RigFile   = Join-Path $WfviewDir "rigs\IC-7610.rig"
$QtBin     = Join-Path $Root "Qt\6.8.1\msvc2022_64\bin"

$PayloadFiles = @(
    "include\ic7610cwcore.h",
    "include\ic7610cwhand.h",
    "include\ic7610cwwords.h"
)

# ----------------------------------------------------------------------
Step "Checking v52 source state and v53 payload"
# ----------------------------------------------------------------------

foreach ($p in @($ProFile, $RigFile, (Join-Path $QtBin "qmake.exe"))) {
    if (-not (Test-Path $p)) { Fail "Missing required file: $p" }
}
$pro = Read-Norm $ProFile
if (-not $pro.Contains("include/ic7610cwwords.h")) { Fail "Expected v52 source (wfview.pro lists ic7610cwwords.h); run v52 first" }
$wordsNow = Read-Norm (Join-Path $WfviewDir "include\ic7610cwwords.h")
if (-not $wordsNow.Contains("void settle(int count")) { Fail "Expected the fixed v52 ic7610cwwords.h; run v52 first" }
Write-Host "  OK: v52 source" -ForegroundColor Green

foreach ($rel in $PayloadFiles) {
    $src = Join-Path $Payload $rel
    if (-not (Test-Path $src)) { Fail "Missing payload file: $src" }
}
Write-Host "  OK: payload ($($PayloadFiles.Count) files) in $Payload" -ForegroundColor Green

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$BackupDir = Join-Path $Root "CHECKPOINTS\pre-v53-$stamp"
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
foreach ($rel in $PayloadFiles) {
    $cur = Join-Path $WfviewDir $rel
    if (Test-Path $cur) { Copy-Item $cur (Join-Path $BackupDir (Split-Path $rel -Leaf)) -Force }
}
Write-Host "Backup: $BackupDir" -ForegroundColor DarkGray

# ----------------------------------------------------------------------
Step "Installing v53 hand-key engine files"
# ----------------------------------------------------------------------

foreach ($rel in $PayloadFiles) {
    Copy-Item (Join-Path $Payload $rel) (Join-Path $WfviewDir $rel) -Force
    Write-Host "  Installed: $rel" -ForegroundColor Green
}

# ----------------------------------------------------------------------
Step "v53 sanity checks"
# ----------------------------------------------------------------------

$handH  = Read-Norm (Join-Path $WfviewDir "include\ic7610cwhand.h")
$wordsH = Read-Norm (Join-Path $WfviewDir "include\ic7610cwwords.h")
$coreH  = Read-Norm (Join-Path $WfviewDir "include\ic7610cwcore.h")
foreach ($pair in @(
    @($handH,  "m_hi += m_markA * (mag - m_hi);"),
    @($handH,  "void pushDecoded("),
    @($wordsH, "bool internal = false"),
    @($wordsH, "void settle(int count"),
    @($coreH,  "kLockSnrDb     = 7.0f;")
)) {
    if (-not $pair[0].Contains($pair[1])) { Fail "Sanity check failed: $($pair[1])" }
    Write-Host "  OK: $($pair[1])" -ForegroundColor Green
}
# Qt #defines emit / signals / slots / foreach: never use them as names.
foreach ($h in @($handH, $wordsH, $coreH)) {
    if ($h -match '\b(emit|signals|slots|foreach)\s*\(') { Fail "Qt keyword used as an identifier in a decoder header" }
}
Write-Host "  OK: no Qt keywords used as identifiers" -ForegroundColor Green

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

Step "Compiling wfview IC-7610 EXT v53"

Push-Location $WfviewDir
try {
    # Headers only changed; qmake/nmake dependency tracking rebuilds ic7610cwdecoder.cpp.
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
Step "Creating v53 READY folder"
# ----------------------------------------------------------------------

$ReadyRoot = Join-Path $Root "READY"
$Ready     = Join-Path $ReadyRoot "wfview-IC7610-EXT-v53"
New-Item -ItemType Directory -Force -Path $ReadyRoot | Out-Null

if (Test-Path $Ready) {
    try { Remove-Item $Ready -Recurse -Force }
    catch {
        $Ready = Join-Path $ReadyRoot ("wfview-IC7610-EXT-v53-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
        Write-Host "Existing v53 READY folder locked; using $Ready" -ForegroundColor Yellow
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
$Checkpoint     = Join-Path $CheckpointRoot "wfview-IC7610-EXT-v53-candidate"
New-Item -ItemType Directory -Force -Path $CheckpointRoot | Out-Null
if (Test-Path $Checkpoint) { Remove-Item $Checkpoint -Recurse -Force }
Copy-Item $WfviewDir $Checkpoint -Recurse -Force

@"
wfview IC-7610 EXT v53 candidate - hand-key engine, second tuning pass
======================================================================

  1. Hand key through QSB: dits should no longer drop out during fades.
  2. Fewer run-together words; callsigns / RST / numbers split by long
     gaps should mostly join ("579", "KG9HV").
  3. Weak stations (5-7 dB on the status line) now decode.
  4. Machine mode unchanged.
  5. The first word or two of a recording may be the tail of an earlier
     word - that is expected.
"@ | Set-Content -Path (Join-Path $Ready "IC7610-EXT-v53-CANDIDATE.txt") -Encoding UTF8

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "V53 BUILD COMPLETE" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host "Run:" -ForegroundColor Cyan
Write-Host "  $(Join-Path $Ready 'wfview.exe')" -ForegroundColor White
