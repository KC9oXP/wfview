# Build-wfview-IC7610-EXT-v54.ps1
#
# v54: CW decoder merged into wfview's CW Sender window + multi-signal list.
#
#   1. IC7610_V54_CW_MERGE: the decoder is now a panel (Ic7610CwDecoderPanel)
#      inside the CW Sender window ("CW" button), above the send transcript,
#      so keying and decoding happen in one window. What you send is shown
#      inline in the decode text as an orange "[TX] ..." line. The window is
#      titled "CW Sender / Decoder" and opens larger. The "CW Decode" button
#      now opens that window; the old stand-alone decoder window is only used
#      if the CW window does not exist yet (not connected).
#   2. IC7610_V54_CW_MULTI: "Multi-signal" check box. Every CW signal in the
#      passband (200-2000 Hz; widen the radio filter to see more) gets its
#      own hand-key decoder and a line in a list: pitch, WPM, decoded text.
#      Double-click a line to make that station the main decode.
#      New include\ic7610cwmulti.h. Offline: 3 overlapping simulated stations
#      at 450/600/820 Hz all copied; noise alone opens no channels; on the
#      27 real clips only real stations show (local noise-floor test).
#   3. Hand-key fix: a noise blip inside a word no longer knocks marks and
#      gaps out of step (clip 224048).
#
# Files from D:\wfbuild testing\v54-payload. Expected input: tree after v53.
# Patches are idempotent; -NoBuild patches only.
# Keep this file ASCII-only (Windows PowerShell 5.1 reads BOM-less files as ANSI).

param(
    # All build trees and outputs live under D:\wfbuild testing.
    [string]$Root = "D:\wfbuild testing\wfview-agc-build",
    [string]$Payload = "D:\wfbuild testing\v54-payload",
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

    $tmp = Join-Path $env:TEMP ("wfview-v54-vcvars-" + [guid]::NewGuid().ToString("N") + ".cmd")
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
    "include\ic7610cwmulti.h",
    "include\ic7610cwdecoder.h",
    "src\ic7610cwdecoder.cpp"
)

# ----------------------------------------------------------------------
Step "Checking v53 source state and v54 payload"
# ----------------------------------------------------------------------

foreach ($p in @($CppFile, $ProFile, $RigFile, (Join-Path $QtBin "qmake.exe"))) {
    if (-not (Test-Path $p)) { Fail "Missing required file: $p" }
}
$cpp = Read-Norm $CppFile
$pro = Read-Norm $ProFile
$coreNow = Read-Norm (Join-Path $WfviewDir "include\ic7610cwcore.h")
if (-not $coreNow.Contains("kLockSnrDb     = 7.0f;")) { Fail "Expected v53 source (ic7610cwcore.h lock 7 dB); run v53 first" }
foreach ($marker in @("IC7610_V49_CW_DECODER_BTN", "IC7610_V49_CW_DECODER_PITCH", "IC7610_V50_CW_BTN_BOTTOM_ROW")) {
    if (-not $cpp.Contains($marker)) { Fail "Expected marker missing from wfmain.cpp: $marker" }
}
Write-Host "  OK: v53 source" -ForegroundColor Green

foreach ($rel in $PayloadFiles) {
    $src = Join-Path $Payload $rel
    if (-not (Test-Path $src)) { Fail "Missing payload file: $src" }
}
Write-Host "  OK: payload ($($PayloadFiles.Count) files) in $Payload" -ForegroundColor Green

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$BackupDir = Join-Path $Root "CHECKPOINTS\pre-v54-$stamp"
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
Copy-Item $CppFile (Join-Path $BackupDir "wfmain.cpp") -Force
Copy-Item $ProFile (Join-Path $BackupDir "wfview.pro") -Force
foreach ($rel in $PayloadFiles) {
    $cur = Join-Path $WfviewDir $rel
    if (Test-Path $cur) { Copy-Item $cur (Join-Path $BackupDir (Split-Path $rel -Leaf)) -Force }
}
Write-Host "Backup: $BackupDir" -ForegroundColor DarkGray

# ----------------------------------------------------------------------
Step "Installing v54 decoder files"
# ----------------------------------------------------------------------

foreach ($rel in $PayloadFiles) {
    Copy-Item (Join-Path $Payload $rel) (Join-Path $WfviewDir $rel) -Force
    Write-Host "  Installed: $rel" -ForegroundColor Green
}

# ----------------------------------------------------------------------
Step "wfmain: decoder inside the CW Sender window"
# ----------------------------------------------------------------------

if ($cpp.Contains("IC7610_V54_CW_MERGE")) {
    Write-Host "  wfmain.cpp already patched; skipping." -ForegroundColor Yellow
}
else {
    # 1. Embed the panel when the CW Sender window is created.
    $cpp = Replace-Once $cpp @'
            cw->setMacroText(prefs.cwMacroList);
        }
'@ @'
            cw->setMacroText(prefs.cwMacroList);

            // IC7610_V54_CW_MERGE: CW decoder panel inside the CW Sender
            // window, above its send transcript.
            {
                auto *cwDecPanel = new Ic7610CwDecoderPanel(rxProc, cw);
                cwDecPanel->setObjectName(QStringLiteral("ic7610CwDecoderPanel"));
                QVBoxLayout *cwRxLayout = cw->findChild<QVBoxLayout*>(QStringLiteral("verticalLayout_7"));
                if (cwRxLayout != Q_NULLPTR) {
                    cwRxLayout->insertWidget(0, cwDecPanel, 3);
                } else {
                    cwDecPanel->hide();
                    qWarning() << "IC7610 v54: CW Sender layout not found; decoder panel hidden";
                }
                connect(cwDecPanel, &Ic7610CwDecoderPanel::radioPitchRequested, this, [this]() {
                    if (queue != Q_NULLPTR && connStatus == connConnected)
                        queue->add(priorityImmediate, funcCwPitch, false, false);
                });
                connect(cw, &cwSender::sendCW, cwDecPanel, &Ic7610CwDecoderPanel::appendSent);
                cw->setWindowTitle(tr("CW Sender / Decoder"));
                cw->resize(cw->size().expandedTo(QSize(980, 860)));
            }
        }
'@ "wfmain: embed decoder panel in CW Sender"

    # 2. "CW Decode" button opens the merged window when it exists.
    $cpp = Replace-Once $cpp @'
        connect(cwDecodeBtn, &QPushButton::clicked, this, [this]() {
            auto *win = findChild<Ic7610CwDecoderWindow*>(
'@ @'
        connect(cwDecodeBtn, &QPushButton::clicked, this, [this]() {
            // IC7610_V54_CW_MERGE_BTN: the decoder lives in the CW window.
            if (cw != Q_NULLPTR) {
                on_cwButton_clicked();
                return;
            }
            auto *win = findChild<Ic7610CwDecoderWindow*>(
'@ "wfmain: CW Decode button opens CW window"

    # 3. Radio CW pitch goes to every decoder panel (CW window and stand-alone).
    $cpp = Replace-Once $cpp @'
        // IC7610_V49_CW_DECODER_PITCH
        if (auto *cwDec = findChild<Ic7610CwDecoderWindow*>(
                    QStringLiteral("ic7610CwDecoderWin"), Qt::FindDirectChildrenOnly)) {
            cwDec->setRadioPitch(static_cast<int>(val.value.value<quint16>()));
        }
'@ @'
        // IC7610_V49_CW_DECODER_PITCH (v54: all panels, IC7610_V54_CW_PITCH_ALL)
        for (Ic7610CwDecoderPanel *cwDecP : findChildren<Ic7610CwDecoderPanel*>()) {
            cwDecP->setRadioPitch(static_cast<int>(val.value.value<quint16>()));
        }
'@ "wfmain: forward CW pitch to all panels"
}

# ----------------------------------------------------------------------
Step "wfview.pro: add ic7610cwmulti.h"
# ----------------------------------------------------------------------

if ($pro.Contains("include/ic7610cwmulti.h")) {
    Write-Host "  wfview.pro already lists ic7610cwmulti.h; skipping." -ForegroundColor Yellow
}
else {
    $pro = Replace-Once $pro @'
    include/ic7610cwwords.h \
'@ @'
    include/ic7610cwwords.h \
    include/ic7610cwmulti.h \
'@ "pro: HEADERS ic7610cwmulti.h"
}

# ----------------------------------------------------------------------
Step "v54 sanity checks"
# ----------------------------------------------------------------------

$decH   = Read-Norm (Join-Path $WfviewDir "include\ic7610cwdecoder.h")
$decCpp = Read-Norm (Join-Path $WfviewDir "src\ic7610cwdecoder.cpp")
$multiH = Read-Norm (Join-Path $WfviewDir "include\ic7610cwmulti.h")
$handH  = Read-Norm (Join-Path $WfviewDir "include\ic7610cwhand.h")
foreach ($pair in @(
    @($cpp,    "IC7610_V54_CW_MERGE"),
    @($cpp,    "IC7610_V54_CW_MERGE_BTN"),
    @($cpp,    "IC7610_V54_CW_PITCH_ALL"),
    @($decH,   "class Ic7610CwDecoderPanel : public QWidget"),
    @($decH,   "class Ic7610CwDecoderWindow : public QDialog"),
    @($decCpp, "IC7610_V54_CW_MULTI"),
    @($multiH, "class Ic7610CwMulti"),
    @($handH,  "m_gapCarry"),
    @($pro,    "include/ic7610cwmulti.h")
)) {
    if (-not $pair[0].Contains($pair[1])) { Fail "Sanity check failed: $($pair[1])" }
    Write-Host "  OK: $($pair[1])" -ForegroundColor Green
}
foreach ($h in @($handH, $multiH)) {
    if ($h -match '\b(emit|signals|slots|foreach)\s*\(') { Fail "Qt keyword used as an identifier in a decoder header" }
}
Write-Host "  OK: no Qt keywords used as identifiers" -ForegroundColor Green

Write-Norm $CppFile $cpp
Write-Norm $ProFile $pro
Write-Host "Source written." -ForegroundColor Green

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

Step "Compiling wfview IC-7610 EXT v54"

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
Step "Creating v54 READY folder"
# ----------------------------------------------------------------------

$ReadyRoot = Join-Path $Root "READY"
$Ready     = Join-Path $ReadyRoot "wfview-IC7610-EXT-v54"
New-Item -ItemType Directory -Force -Path $ReadyRoot | Out-Null

if (Test-Path $Ready) {
    try { Remove-Item $Ready -Recurse -Force }
    catch {
        $Ready = Join-Path $ReadyRoot ("wfview-IC7610-EXT-v54-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
        Write-Host "Existing v54 READY folder locked; using $Ready" -ForegroundColor Yellow
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
$Checkpoint     = Join-Path $CheckpointRoot "wfview-IC7610-EXT-v54-candidate"
New-Item -ItemType Directory -Force -Path $CheckpointRoot | Out-Null
if (Test-Path $Checkpoint) { Remove-Item $Checkpoint -Recurse -Force }
Copy-Item $WfviewDir $Checkpoint -Recurse -Force

@"
wfview IC-7610 EXT v54 candidate - CW Sender/Decoder in one window + multi-signal
==================================================================================

  1. "CW" button (and "CW Decode") opens "CW Sender / Decoder": decoder
     controls and text on top, send box / macros / transcript below.
     Nothing clipped.
  2. Tick Decode, send something: it appears in the decode text as an
     orange "[TX] ..." line; received text continues after it.
  3. Multi-signal: widen the filter (e.g. 1.2-2.4 kHz) on a busy band.
     One line per station (pitch, WPM, text). Double-click a line: the main
     decode switches to that station ("[now decoding NNN Hz]").
  4. Close the CW window: decoding stops. Minimise: keeps decoding.
  5. Regression: Hand key / Machine copy as in v53; Record 60 s works.
"@ | Set-Content -Path (Join-Path $Ready "IC7610-EXT-v54-CANDIDATE.txt") -Encoding UTF8

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "V54 BUILD COMPLETE" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host "Run:" -ForegroundColor Cyan
Write-Host "  $(Join-Path $Ready 'wfview.exe')" -ForegroundColor White
