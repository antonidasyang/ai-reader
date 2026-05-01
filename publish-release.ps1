# publish-release.ps1 — cuts a GitHub release + updates manifest.json
#
# Workflow per release:
#   1. Bump CMakeLists.txt project(... VERSION X.Y.Z)
#   2. Bump #define MyAppVersion in AiReader.iss to match
#   3. cmake --build build --config Release
#   4. windeploy.bat
#   5. "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" AiReader.iss
#   6. .\sign-windows.ps1 -Pfx ...   (D9; optional but recommended)
#   7. .\publish-release.ps1 -Version X.Y.Z -Notes "Changelog…"
#
# What this script does:
#   * Locates installer\AiReader-Setup-<Version>.exe.
#   * Computes its SHA-256.
#   * Creates (or reuses) a draft GitHub release tagged vX.Y.Z via the
#     gh CLI; uploads the .exe as a release asset.
#   * Rewrites manifest.json with the new version, downloadUrl,
#     sha256, releaseDate, and releaseNotes.
#   * Commits + pushes manifest.json on the current branch so the
#     in-app UpdateChecker (which polls main's manifest.json) flips
#     existing installs into "update available" state.
#
# Prerequisites:
#   * gh CLI installed, authenticated via `gh auth login`
#   * git on PATH and the working tree clean except for manifest.json
#   * The installer .exe already built and (ideally) signed

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$Version,

    [Parameter(Mandatory=$true)]
    [string]$Notes,

    [string]$ReleaseDate = (Get-Date -Format "yyyy-MM-dd"),

    [string]$Repo = "antonidasyang/ai-reader",

    [string]$Branch = "main",

    # When set, creates the GitHub release as a draft so it doesn't
    # publish immediately. Useful when you want to manually edit the
    # release notes on GitHub before flipping the toggle.
    [switch]$Draft,

    # Skip the gh release upload — only refresh manifest.json + push.
    # Use this when re-publishing a manifest after a hotfix release
    # was uploaded by some other means.
    [switch]$ManifestOnly
)

$ErrorActionPreference = "Stop"

$root      = $PSScriptRoot
$installer = Join-Path $root "installer\AiReader-Setup-$Version.exe"
$manifest  = Join-Path $root "manifest.json"

if (-not (Test-Path $installer)) {
    throw "Installer not found: $installer  (build it first via ISCC.exe AiReader.iss)"
}

# SHA-256 of the installer for the manifest.
Write-Host "[publish] hashing $installer"
$sha = (Get-FileHash -Algorithm SHA256 $installer).Hash.ToLower()
Write-Host "[publish] sha256 = $sha"

$tag         = "v$Version"
$downloadUrl = "https://github.com/$Repo/releases/download/$tag/AiReader-Setup-$Version.exe"

if (-not $ManifestOnly) {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw "gh CLI not on PATH. Install GitHub CLI and run 'gh auth login'."
    }

    # Create the release if it doesn't already exist; otherwise reuse it.
    $exists = & gh release view $tag --repo $Repo 2>$null
    if (-not $exists) {
        $args = @("release", "create", $tag,
                  "--repo", $Repo,
                  "--title", "AI Reader $Version",
                  "--notes", $Notes,
                  "--target", $Branch)
        if ($Draft) { $args += "--draft" }
        Write-Host "[publish] gh $($args -join ' ')"
        & gh @args
        if ($LASTEXITCODE -ne 0) { throw "gh release create failed." }
    } else {
        Write-Host "[publish] release $tag already exists, will only upload the asset."
    }

    Write-Host "[publish] uploading $installer"
    & gh release upload $tag $installer --repo $Repo --clobber
    if ($LASTEXITCODE -ne 0) { throw "gh release upload failed." }
}

# Rewrite manifest.json. We keep the file format simple (one platform
# entry today) so eyes-on review of the diff stays trivial when we
# add macOS / Linux later.
Write-Host "[publish] rewriting $manifest"
$json = [ordered]@{
    schemaVersion  = 1
    latestVersion  = $Version
    releaseDate    = $ReleaseDate
    releaseNotes   = $Notes
    platforms      = [ordered]@{
        "windows-x64" = [ordered]@{
            downloadUrl = $downloadUrl
            sha256      = $sha
        }
    }
}
$body = $json | ConvertTo-Json -Depth 8
Set-Content -Path $manifest -Value $body -Encoding UTF8

Write-Host "[publish] committing + pushing manifest.json"
& git add $manifest
& git commit -m "chore(release): manifest -> v$Version"
if ($LASTEXITCODE -ne 0) { throw "git commit failed." }
& git push origin $Branch
if ($LASTEXITCODE -ne 0) { throw "git push failed." }

Write-Host "[publish] done. Existing installs will see the update on their next launch check."
