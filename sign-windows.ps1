# sign-windows.ps1 — code-sign ai-reader.exe + the Inno Setup installer
#
# Two cert backends are supported:
#
#   * .pfx file + password  (legacy code-signing certs, still common
#     for OV certs that have to live on disk).
#   * Azure Trusted Signing  (the modern Microsoft path — keys never
#     leave Azure; ATS earns SmartScreen reputation faster than a
#     plain OV cert and is cheaper than an EV cert).
#
# Sign both binaries (the .exe inside dist\ and the installer in
# installer\) so the in-place .exe doesn't trigger SmartScreen in
# isolation when users uninstall + re-launch from %LOCALAPPDATA%.
#
# Examples:
#
#   # PFX
#   .\sign-windows.ps1 -PfxPath C:\certs\ai-reader.pfx `
#                       -PfxPassword (Read-Host -AsSecureString)
#
#   # Azure Trusted Signing (config file format documented at
#   #   https://github.com/vcsjones/AzureSignTool#configuration )
#   .\sign-windows.ps1 -Azure -AzureConfig .\ats.json
#
#   # Sign one specific file:
#   .\sign-windows.ps1 -PfxPath ... -Files dist\ai-reader.exe
#
# Prerequisites:
#   * For -PfxPath: signtool.exe on PATH (ships with the Windows SDK;
#     usually under C:\Program Files (x86)\Windows Kits\10\bin\<ver>\x64\).
#   * For -Azure:    AzureSignTool installed (`dotnet tool install
#     --global AzureSignTool`) and Azure CLI logged in.
#   * Files to sign must already exist — this script only signs, it
#     doesn't build.

[CmdletBinding(DefaultParameterSetName='Pfx')]
param(
    # ── PFX backend ─────────────────────────────────────────────────
    [Parameter(ParameterSetName='Pfx', Mandatory=$true)]
    [string]$PfxPath,

    # SecureString recommended; plaintext only for unattended CI
    # where the secret comes from a vault env-var.
    [Parameter(ParameterSetName='Pfx', Mandatory=$true)]
    $PfxPassword,

    # ── Azure Trusted Signing backend ───────────────────────────────
    [Parameter(ParameterSetName='Azure', Mandatory=$true)]
    [switch]$Azure,

    [Parameter(ParameterSetName='Azure', Mandatory=$true)]
    [string]$AzureConfig,

    # ── Common options ──────────────────────────────────────────────
    [string]$TimestampUrl = "http://timestamp.digicert.com",

    [string]$Description = "AI Reader",

    [string]$DescriptionUrl = "https://github.com/antonidasyang/ai-reader",

    # Default targets: the staged exe + the latest installer.
    # Override with -Files to sign one specific binary.
    [string[]]$Files
)

$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
if (-not $Files -or $Files.Count -eq 0) {
    $candidates = @()
    $exe = Join-Path $root "dist\ai-reader.exe"
    if (Test-Path $exe) { $candidates += $exe }

    $installerDir = Join-Path $root "installer"
    if (Test-Path $installerDir) {
        $candidates += (Get-ChildItem -Path $installerDir -Filter "AiReader-Setup-*.exe" |
                        Sort-Object LastWriteTime -Descending |
                        ForEach-Object { $_.FullName })
    }
    if ($candidates.Count -eq 0) {
        throw "No targets found. Build the binary + installer first, or pass -Files."
    }
    $Files = $candidates
}

Write-Host "[sign] targets:"
foreach ($f in $Files) { Write-Host "  - $f" }

if ($Azure) {
    if (-not (Get-Command AzureSignTool -ErrorAction SilentlyContinue)) {
        throw "AzureSignTool not on PATH. Install: dotnet tool install --global AzureSignTool"
    }
    if (-not (Test-Path $AzureConfig)) {
        throw "Azure config not found: $AzureConfig"
    }
    foreach ($f in $Files) {
        Write-Host "[sign] AzureSignTool $f"
        & AzureSignTool sign `
            --azure-key-vault-config $AzureConfig `
            --description $Description `
            --description-url $DescriptionUrl `
            --timestamp-rfc3161 $TimestampUrl `
            --timestamp-digest sha256 `
            --file-digest sha256 `
            --colors `
            $f
        if ($LASTEXITCODE -ne 0) { throw "AzureSignTool failed for $f" }
    }
} else {
    if (-not (Test-Path $PfxPath)) {
        throw "PFX not found: $PfxPath"
    }
    if (-not (Get-Command signtool.exe -ErrorAction SilentlyContinue)) {
        throw "signtool.exe not on PATH. Install the Windows SDK and add e.g. " +
              "C:\Program Files (x86)\Windows Kits\10\bin\<ver>\x64 to PATH."
    }

    # SecureString → plaintext for the signtool /p arg. Passwords on
    # the command line aren't ideal but signtool has no stdin path.
    $plainPwd = $PfxPassword
    if ($PfxPassword -is [System.Security.SecureString]) {
        $bstr = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($PfxPassword)
        try {
            $plainPwd = [System.Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
        } finally {
            [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
        }
    }

    foreach ($f in $Files) {
        Write-Host "[sign] signtool $f"
        & signtool.exe sign `
            /fd SHA256 `
            /tr $TimestampUrl `
            /td SHA256 `
            /f $PfxPath `
            /p $plainPwd `
            /d $Description `
            /du $DescriptionUrl `
            $f
        if ($LASTEXITCODE -ne 0) { throw "signtool failed for $f" }
    }
}

Write-Host "[sign] verifying signatures…"
foreach ($f in $Files) {
    & signtool.exe verify /pa /v $f
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "verify failed for $f (signtool exit $LASTEXITCODE)"
    }
}

Write-Host "[sign] done."
