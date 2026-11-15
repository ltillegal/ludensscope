# Sign a JUCE-built AAX bundle with the PACE Eden wraptool (Windows).
# See docs/aax-signing.md for the prerequisites (Avid approval, PACE Central
# wrap configuration GUID, iLok account, DSIG identity).
#
# Usage:
#   $env:AAX_SIGN_ID="..."; $env:AAX_WRAP_CONFIG="..."; .\sign-aax-windows.ps1 [input] [output]

param(
    [string]$InputBundle = "build/LudensScope_artefacts/Release/AAX/LudensScope.aaxplugin",
    [string]$OutputBundle = "dist/LudensScope.aaxplugin"
)

$ErrorActionPreference = "Stop"

$signId = $env:AAX_SIGN_ID
$wrapConfig = $env:AAX_WRAP_CONFIG
$wrap = $env:AAX_WRAP_TOOL
if (-not $wrap) {
    $cmd = Get-Command wraptool -ErrorAction SilentlyContinue
    if ($cmd) { $wrap = $cmd.Source }
}

if (-not $signId) { throw "set AAX_SIGN_ID (PACE DSIG identity)" }
if (-not $wrapConfig) { throw "set AAX_WRAP_CONFIG (PACE Central wrap configuration GUID)" }
if (-not (Test-Path $InputBundle)) { throw "AAX bundle not found: $InputBundle" }
if (-not $wrap -or -not (Test-Path $wrap)) { throw "wraptool not found; set AAX_WRAP_TOOL" }

New-Item -ItemType Directory -Force -Path (Split-Path $OutputBundle) | Out-Null
if (Test-Path $OutputBundle) { Remove-Item -Recurse -Force $OutputBundle }
Copy-Item -Recurse $InputBundle $OutputBundle

& $wrap --signid $signId --wrapConfig $wrapConfig $OutputBundle
if ($LASTEXITCODE -ne 0) { throw "wraptool failed with exit code $LASTEXITCODE" }

Write-Host "signed: $OutputBundle"
Write-Host "verify in Pro Tools (developer build + iLok) and replace the .aaxplugin in the installer."
