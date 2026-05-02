# copy-vc-runtime.ps1 -- copy MSVC runtime DLLs into dist\
#
# windeployqt's --compiler-runtime flag was deprecated in Qt 6.5 and
# is silently ignored in 6.5+, so the runtime DLLs (vcruntime140.dll,
# vcruntime140_1.dll, msvcp140.dll, ...) never end up beside
# ai-reader.exe by default. End-users on a stock Windows install
# without the Visual C++ Redistributable then hit
#
#     "Cannot continue: VCRUNTIME140_1.dll not found"
#
# on first launch. This script:
#   1. Locates the latest Visual Studio install via vswhere.exe.
#   2. Walks VC\Redist\MSVC\ to find the newest versioned CRT folder.
#   3. Copies every *.dll from x64\Microsoft.VCXXX.CRT\ into dist\.
#
# Called automatically by windeploy.bat after windeployqt finishes.
# Safe to re-run -- copies are idempotent (Copy-Item -Force).

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$Dist
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Dist)) {
    throw "Dist folder not found: $Dist"
}

# vswhere.exe ships with every VS Installer install (2017+) at a
# stable, well-known location. If it's missing, the user doesn't
# have Visual Studio installed at all -- they're building with the
# standalone Build Tools or some other toolchain, and we can't
# locate the runtime automatically.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Warning "[copy-vc-runtime] vswhere.exe not found at $vswhere"
    Write-Warning "[copy-vc-runtime] Visual Studio is not installed via the standard installer."
    Write-Warning "[copy-vc-runtime] Skipping VC runtime copy; ship vc_redist.x64.exe with"
    Write-Warning "                   your installer or copy the DLLs from System32 manually."
    exit 0
}

$vsInstall = & $vswhere -latest -property installationPath -nologo
if (-not $vsInstall) {
    Write-Warning "[copy-vc-runtime] vswhere returned no Visual Studio install."
    exit 0
}
Write-Host "[copy-vc-runtime] VS install: $vsInstall"

$redistRoot = Join-Path $vsInstall "VC\Redist\MSVC"
if (-not (Test-Path $redistRoot)) {
    Write-Warning "[copy-vc-runtime] No Redist tree at $redistRoot"
    Write-Warning "[copy-vc-runtime] Install the 'C++ x64/x86 build tools' VS workload."
    exit 0
}

# Pick the newest versioned subfolder (e.g. 14.40.33807). Any text
# subfolder ('onecore', '<garbage>') gets filtered out by the regex.
$versionDir = Get-ChildItem -Directory $redistRoot |
              Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
              Sort-Object @{Expression={[Version]$_.Name}} -Descending |
              Select-Object -First 1

if (-not $versionDir) {
    Write-Warning "[copy-vc-runtime] No versioned redist folder under $redistRoot"
    exit 0
}

# The CRT subfolder name embeds the toolset version: VC141, VC142,
# VC143, VC144 ... Pick whichever exists today.
$crtCandidates = @(
    (Join-Path $versionDir.FullName "x64\Microsoft.VC144.CRT"),
    (Join-Path $versionDir.FullName "x64\Microsoft.VC143.CRT"),
    (Join-Path $versionDir.FullName "x64\Microsoft.VC142.CRT"),
    (Join-Path $versionDir.FullName "x64\Microsoft.VC141.CRT")
)
$crtDir = $crtCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $crtDir) {
    Write-Warning "[copy-vc-runtime] No Microsoft.VCxxx.CRT folder under $($versionDir.FullName)\x64"
    exit 0
}

Write-Host "[copy-vc-runtime] Copying from: $crtDir"
$copied = 0
Get-ChildItem $crtDir -Filter "*.dll" | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination $Dist -Force
    Write-Host "[copy-vc-runtime]   + $($_.Name)"
    $copied++
}

if ($copied -eq 0) {
    Write-Warning "[copy-vc-runtime] No DLLs were copied."
    exit 0
}

Write-Host "[copy-vc-runtime] Done. $copied runtime DLL(s) staged into $Dist"
