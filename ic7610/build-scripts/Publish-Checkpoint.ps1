# Publish-Checkpoint.ps1
#
# Publishes one confirmed-working IC-7610 EXT version as a "major checkpoint"
# to GitLab (kc9oxp-group/wfview-ic7610-build):
#
#   1. Commits the live source tree (wfview-agc-build\wfview, an upstream
#      wfview git checkout) on branch ic7610-dev.
#   2. Stores the matching build script, the CW test tools (no WAV clips)
#      and a one-line log entry under wfview\ic7610\.
#   3. Creates annotated tag ic7610-vNN.
#   4. Pushes branch + tag to the GitLab remote ("gitlab"; upstream stays
#      "origin"). main on GitLab is left untouched.
#   5. -Release: zips READY\wfview-IC7610-EXT-vNN, uploads it to the
#      project's package registry and creates a GitLab Release for the tag.
#      Needs a Personal Access Token (api scope) in $env:GITLAB_TOKEN.
#
# Run it AFTER a version is built and you are happy with it, and BEFORE the
# next Build-...-vNN.ps1 patches the tree.
#
# Examples:
#   .\Publish-Checkpoint.ps1 -Version 51 -DryRun
#   .\Publish-Checkpoint.ps1 -Version 51 -Message "CW hand-key engine + 60 s recorder"
#   .\Publish-Checkpoint.ps1 -Version 51 -Release        (tag already pushed: adds the Release)
#
# Keep this file ASCII-only (Windows PowerShell 5.1 reads BOM-less files as ANSI).

param(
    [Parameter(Mandatory = $true)][int]$Version,
    [string]$Message = "",
    # All build trees and outputs live under D:\wfbuild testing.
    [string]$Root = "D:\wfbuild testing\wfview-agc-build",
    [string]$Scripts = "D:\wfbuild testing",
    [string]$Branch = "ic7610-dev",
    [string]$RemoteName = "gitlab",
    [string]$RemoteUrl = "https://gitlab.com/kc9oxp-group/wfview-ic7610-build.git",
    [string]$ProjectPath = "kc9oxp-group/wfview-ic7610-build",
    [switch]$DryRun,
    [switch]$NoPush,
    [switch]$Release,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
Set-StrictMode -Version 2.0

function Step([string]$Text) { Write-Host ""; Write-Host "== $Text" -ForegroundColor Cyan }
function Info([string]$Text) { Write-Host "   $Text" }
function Warn([string]$Text) { Write-Host "   WARN: $Text" -ForegroundColor Yellow }
function Fail([string]$Text) { Write-Host ""; Write-Host "ERROR: $Text" -ForegroundColor Red; exit 1 }

# git writes progress and hints to stderr; keep EAP=Stop from turning that into a throw.
function Run-Git {
    $old = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $out = & git.exe -C $script:Repo @args 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    $text = (@($out) | ForEach-Object { "$_" }) -join "`n"
    return @{ Code = $code; Text = $text }
}
function Git-Ok {
    $r = Run-Git @args
    if ($r.Code -ne 0) {
        if ($r.Text) { Write-Host $r.Text }
        Fail ("git " + ($args -join " ") + " failed (exit " + $r.Code + ")")
    }
    return $r.Text
}

$script:Repo = Join-Path $Root "wfview"
$Tag         = "ic7610-v$Version"
$ReadyDir    = Join-Path $Root ("READY\wfview-IC7610-EXT-v" + $Version)
$BuildScript = Join-Path $Scripts ("Build-wfview-IC7610-EXT-v" + $Version + ".ps1")
$TestsDir    = Join-Path $Scripts "cw-decoder-tests"
$ExtraDir    = Join-Path $script:Repo "ic7610"
$Stamp       = Get-Date -Format "yyyy-MM-dd HH:mm"
if ($DryRun) { Write-Host "DRY RUN - nothing will be changed or pushed." -ForegroundColor Yellow }

# ---------------------------------------------------------------- checks
Step "Checks"
if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) { Fail "git.exe is not on PATH." }
if (-not (Test-Path (Join-Path $script:Repo ".git"))) { Fail "$($script:Repo) is not a git checkout." }
Info "Repo:   $($script:Repo)"

if (Test-Path $ReadyDir) { Info "Build:  $ReadyDir" }
elseif ($Force) { Warn "No READY build at $ReadyDir (continuing because of -Force)." }
else { Fail "No READY build at $ReadyDir. Build v$Version first, or use -Force." }

if (Test-Path $BuildScript) { Info "Script: $BuildScript" }
else { Warn "$BuildScript not found; it will not be stored." }

$uname = (Run-Git config user.name).Text.Trim()
$umail = (Run-Git config user.email).Text.Trim()
if (-not $uname -or -not $umail) {
    Fail 'git identity not set. Run: git config --global user.name "Your Name"  and  git config --global user.email "you@example.com"'
}
Info "Author: $uname <$umail>"

$tagExists = ((Run-Git rev-parse -q --verify "refs/tags/$Tag").Code -eq 0)
$releaseOnly = $false
if ($tagExists -and -not $Force) {
    if ($Release) { $releaseOnly = $true; Info "Tag $Tag already exists - only the Release step will run." }
    else { Fail "Tag $Tag already exists. Use -Force to move it to a new commit, or -Release to add a Release." }
}

if (-not $releaseOnly) {
    # ------------------------------------------------------------ branch
    Step "Branch $Branch"
    $cur = Run-Git symbolic-ref -q --short HEAD
    $curName = ""
    if ($cur.Code -eq 0) { $curName = $cur.Text.Trim() }
    if ($curName -eq $Branch) {
        Info "Already on $Branch."
    } else {
        $branchExists = ((Run-Git rev-parse -q --verify "refs/heads/$Branch").Code -eq 0)
        if ($curName) { Info "Currently on $curName." } else { Info "Currently on a detached HEAD (upstream checkout)." }
        if ($DryRun) {
            if ($branchExists) { Info "Would switch to $Branch (working changes carried over)." }
            else { Info "Would create $Branch here (working changes carried over)." }
        } elseif ($branchExists) {
            Git-Ok switch $Branch | Out-Null
            Info "Switched to $Branch."
        } else {
            Git-Ok switch -c $Branch | Out-Null
            Info "Created $Branch."
        }
    }

    # ------------------------------------------------------------ extras
    Step "Build script, test tools, checkpoint log -> wfview\ic7610\"
    if ($DryRun) {
        Info "Would copy $BuildScript, this script, cw-decoder-tests\*.cpp/*.h and add a CHECKPOINTS.md line."
    } else {
        New-Item -ItemType Directory -Force -Path (Join-Path $ExtraDir "build-scripts") | Out-Null
        if (Test-Path $BuildScript) { Copy-Item $BuildScript (Join-Path $ExtraDir "build-scripts") -Force }
        Copy-Item $MyInvocation.MyCommand.Path (Join-Path $ExtraDir "build-scripts") -Force
        if (Test-Path $TestsDir) {
            $td = Join-Path $ExtraDir "cw-decoder-tests"
            New-Item -ItemType Directory -Force -Path $td | Out-Null
            Get-ChildItem $TestsDir -File | Where-Object { $_.Extension -in ".cpp", ".h", ".hpp", ".txt", ".md", ".ps1", ".sh" } |
                ForEach-Object { Copy-Item $_.FullName $td -Force }
        }
        $log = Join-Path $ExtraDir "CHECKPOINTS.md"
        if (-not (Test-Path $log)) {
            Set-Content -Path $log -Encoding ASCII -Value @(
                "# IC-7610 EXT checkpoints",
                "",
                "Each line is a version that was built and confirmed working, tagged ic7610-vNN.",
                "Upstream base: wfview dcd2c49. Build scripts are in build-scripts\.",
                "",
                "| Version | Date | Notes |",
                "|---|---|---|")
        }
        $line = "| v$Version | $Stamp | $Message |"
        if (-not (Select-String -Path $log -SimpleMatch "| v$Version |" -Quiet)) {
            Add-Content -Path $log -Encoding ASCII -Value $line
        }
        Info "Done."
    }

    # ------------------------------------------------------------ commit
    Step "Commit"
    if ($DryRun) {
        $st = Run-Git status --short
        if ($st.Text) { Write-Host $st.Text } else { Info "(no changes)" }
        Info "Would commit everything above as: IC-7610 EXT v$Version"
    } else {
        Git-Ok add -A | Out-Null
        Write-Host (Git-Ok diff --cached --stat)
        $subject = "IC-7610 EXT v$Version"
        if ($Message) { $subject = "$subject`: $Message" }
        $msgFile = Join-Path $env:TEMP "ic7610-checkpoint-msg.txt"
        Set-Content -Path $msgFile -Encoding ASCII -Value @(
            $subject, "",
            "Major checkpoint, built and confirmed working.",
            "READY build: wfview-IC7610-EXT-v$Version",
            "Build script: ic7610\build-scripts\Build-wfview-IC7610-EXT-v$Version.ps1")
        if ((Run-Git diff --cached --quiet).Code -eq 0) {
            Info "Nothing changed since the last commit; tagging the current commit."
        } else {
            Git-Ok commit -F $msgFile | Out-Null
            Info ("Committed " + (Git-Ok rev-parse --short HEAD).Trim())
        }
        if ($Force -and $tagExists) { Git-Ok tag -f -a $Tag -F $msgFile | Out-Null }
        else { Git-Ok tag -a $Tag -F $msgFile | Out-Null }
        Info "Tagged $Tag"
        Remove-Item $msgFile -ErrorAction SilentlyContinue
    }

    # ------------------------------------------------------------ push
    Step "Push to $RemoteName"
    $ru = Run-Git remote get-url $RemoteName
    if ($ru.Code -ne 0) {
        if ($DryRun) { Info "Would add remote $RemoteName = $RemoteUrl" }
        else { Git-Ok remote add $RemoteName $RemoteUrl | Out-Null; Info "Added remote $RemoteName = $RemoteUrl" }
    } else { Info "Remote $RemoteName = $($ru.Text.Trim())" }

    if ($DryRun -or $NoPush) {
        Info "Skipping push (DryRun/NoPush). Would push $Branch and $Tag."
    } else {
        # Same settings the first GitLab push needed.
        Git-Ok config http.version HTTP/1.1 | Out-Null
        Git-Ok config http.postBuffer 524288000 | Out-Null
        Write-Host (Git-Ok push -u $RemoteName $Branch)
        if ($Force) { Write-Host (Git-Ok push --force $RemoteName "refs/tags/$Tag") }
        else { Write-Host (Git-Ok push $RemoteName "refs/tags/$Tag") }
        Info "Pushed $Branch and $Tag."
    }
}

# ---------------------------------------------------------------- release
if ($Release) {
    Step "GitLab Release for $Tag"
    if ($DryRun) {
        Info "Would zip $ReadyDir, upload it and create the Release."
    } else {
        if (-not (Test-Path $ReadyDir)) { Fail "No READY build at $ReadyDir to attach." }
        $token = $env:GITLAB_TOKEN
        if (-not $token) { Fail 'Set a Personal Access Token (api scope) first:  $env:GITLAB_TOKEN = "glpat-..."' }
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

        $relDir = Join-Path $Scripts "RELEASES"
        New-Item -ItemType Directory -Force -Path $relDir | Out-Null
        $zipName = "wfview-IC7610-EXT-v$Version.zip"
        $zip = Join-Path $relDir $zipName
        if (Test-Path $zip) { Remove-Item $zip -Force }
        Info "Zipping $ReadyDir ..."
        Compress-Archive -Path (Join-Path $ReadyDir "*") -DestinationPath $zip -CompressionLevel Optimal
        Info ("Zip: $zip (" + [math]::Round((Get-Item $zip).Length / 1MB, 1) + " MB)")

        $api = "https://gitlab.com/api/v4/projects/" + [uri]::EscapeDataString($ProjectPath)
        $hdr = @{ "PRIVATE-TOKEN" = $token }
        $pkgUrl = "$api/packages/generic/wfview-ic7610-ext/$Version/$zipName"
        Info "Uploading to the package registry ..."
        Invoke-RestMethod -Method Put -Uri $pkgUrl -Headers $hdr -InFile $zip -ContentType "application/octet-stream" | Out-Null

        $desc = "IC-7610 EXT v$Version - Windows x64 build (Qt 6.8.1, MSVC 2022)."
        if ($Message) { $desc = "$desc`n`n$Message" }
        $body = @{
            name        = "IC-7610 EXT v$Version"
            tag_name    = $Tag
            description = $desc
            assets      = @{ links = @(@{ name = $zipName; url = $pkgUrl; link_type = "package" }) }
        } | ConvertTo-Json -Depth 6
        try {
            Invoke-RestMethod -Method Post -Uri "$api/releases" -Headers $hdr -ContentType "application/json" -Body $body | Out-Null
            Info "Release created: https://gitlab.com/$ProjectPath/-/releases/$Tag"
        } catch {
            Warn ("Release not created: " + $_.Exception.Message + " (does it already exist?)")
        }
    }
}

Step "Done"
if (-not $DryRun -and -not $NoPush -and -not $releaseOnly) {
    Info "Branch: https://gitlab.com/$ProjectPath/-/tree/$Branch"
    Info "Tag:    https://gitlab.com/$ProjectPath/-/tags/$Tag"
}
