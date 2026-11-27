# PACE-sign a JUCE-built AAX bundle (Windows).
#
# Order matters: wraptool first, then Authenticode (signtool) on the wrapped
# binary. See docs/aax-signing.md.
#
# Usage:
#   $env:AAX_ACCOUNT="<iLok account>"
#   $env:AAX_KEYFILE="C:\pace\eden.p12"
#   $env:AAX_KEYPASSWORD="<password>"
#   $env:AAX_WRAP_CONFIG="<wrap configuration GUID>"
#   $env:AAX_SIGN_ID="<DSIG identity>"        # only if your Eden needs it
#   .\scripts\sign-aax-windows.ps1 [input] [output]
#
# Needs a physical iLok (or iLok Cloud) with the "Eden Tools" licence.

param(
    [string]$InputBundle = "build/LudensScope_artefacts/Release/AAX/LudensScope.aaxplugin",
    [string]$OutputBundle = "dist/LudensScope.aaxplugin"
)

$ErrorActionPreference = "Stop"

$account = $env:AAX_ACCOUNT
$keyfile = $env:AAX_KEYFILE
$keypassword = $env:AAX_KEYPASSWORD
$wrapConfig = $env:AAX_WRAP_CONFIG
$signId = $env:AAX_SIGN_ID
$wrap = $env:AAX_WRAP_TOOL

if (-not $wrap) {
    $cmd = Get-Command wraptool -ErrorAction SilentlyContinue
    if ($cmd) { $wrap = $cmd.Source }
}
if (-not $wrap) {
    $default = Get-ChildItem "C:\Program Files\PACE Anti-Piracy" -Recurse -Filter wraptool.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($default) { $wrap = $default.FullName }
}

if (-not $account)     { throw "set AAX_ACCOUNT (iLok account name)" }
if (-not $keyfile)     { throw "set AAX_KEYFILE (PACE Eden .p12)" }
if (-not $keypassword) { throw "set AAX_KEYPASSWORD" }
if (-not $wrapConfig)  { throw "set AAX_WRAP_CONFIG (wrap configuration GUID)" }
if (-not (Test-Path $InputBundle)) {
    throw "AAX bundle not found: $InputBundle (build it first: cmake --build build --target LudensScope_AAX --config Release)"
}
if (-not $wrap -or -not (Test-Path $wrap)) { throw "wraptool not found; set AAX_WRAP_TOOL" }

# Windows AAX bundles nest the real PE file:
#   LudensScope.aaxplugin\Contents\x64\LudensScope.aaxplugin
$binary = Get-ChildItem -Path $InputBundle -Recurse -File -Filter "*.aaxplugin" |
    Select-Object -First 1
if (-not $binary) { throw "no .aaxplugin binary inside $InputBundle" }

New-Item -ItemType Directory -Force -Path (Split-Path $OutputBundle) | Out-Null
if (Test-Path $OutputBundle) { Remove-Item -Recurse -Force $OutputBundle }
Copy-Item -Recurse $InputBundle $OutputBundle

$relative = $binary.FullName.Substring((Resolve-Path $InputBundle).Path.Length).TrimStart('\')
$outBinary = Join-Path $OutputBundle $relative

$wrapArgs = @("sign", "--verbose",
    "--account", $account,
    "--keyfile", $keyfile,
    "--keypassword", $keypassword,
    "--wcguid", $wrapConfig,
    "--in", $outBinary,
    "--out", $outBinary)
if ($signId) { $wrapArgs += @("--signid", $signId) }

Write-Host "wraptool: signing $relative"
& $wrap @wrapArgs
if ($LASTEXITCODE -ne 0) {
    throw "wraptool failed ($LASTEXITCODE). Check: iLok plugged in with the 'Eden Tools' licence, and that the wrap configuration GUID matches PACE Central."
}

Write-Host "signed: $OutputBundle"
Write-Host "next:  Authenticode-sign the .exe installer (not the plugin), then ship."
