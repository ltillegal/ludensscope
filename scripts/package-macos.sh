#!/usr/bin/env bash
# Build the macOS .pkg for LudensScope.
#
# Used both by CI and by the local signed-AAX flow:
#   1. build AAX, 2. scripts/sign-aax-macos.sh, 3. this script with the signed
#   bundle inside the build tree, 4. notarise the .pkg.
#
# Usage:
#   scripts/package-macos.sh [version] [build-dir] [out-dir]
#
# Environment (all optional):
#   AAX_SIGNED_BUNDLE   path to a PACE-signed .aaxplugin to pack instead of the
#                       unsigned one from the build tree
#   APPLE_INSTALLER_ID  "Developer ID Installer: ... (TEAMID)" to sign the pkg
#   APPLE_ID            Apple ID for notarytool
#   APPLE_TEAM_ID       team id for notarytool
#   APPLE_APP_PASSWORD  app-specific password for notarytool
#
# The AAX bundle is never codesigned here: an Apple signature after PACE's wrap
# invalidates it. See docs/aax-signing.md.

set -euo pipefail

VERSION=${1:-1.1.0}
BUILD=${2:-build}
OUT=${3:-.}

ART="$BUILD/LudensScope_artefacts/Release"
PKG="$OUT/LudensScope-macos.pkg"

if [[ ! -d "$ART" ]]; then
  echo "error: no artefacts at $ART (run a Release build first)" >&2
  exit 1
fi

# Ad-hoc Apple signature for everything except AAX.
for bundle in "$ART/VST3/LudensScope.vst3" \
              "$ART/AU/LudensScope.component" \
              "$ART/CLAP/LudensScope.clap" \
              "$ART/Standalone/LudensScope.app"; do
  [[ -e "$bundle" ]] && codesign --force --deep --sign - "$bundle" >/dev/null
done

rm -rf pkgroot
mkdir -p pkgroot/Library/Audio/Plug-Ins/VST3 \
         pkgroot/Library/Audio/Plug-Ins/Components \
         pkgroot/Library/Audio/Plug-Ins/CLAP \
         "pkgroot/Library/Application Support/Avid/Audio/Plug-Ins" \
         pkgroot/Applications

cp -R "$ART/VST3/LudensScope.vst3" pkgroot/Library/Audio/Plug-Ins/VST3/
cp -R "$ART/AU/LudensScope.component" pkgroot/Library/Audio/Plug-Ins/Components/
cp -R "$ART/CLAP/LudensScope.clap" pkgroot/Library/Audio/Plug-Ins/CLAP/
cp -R "$ART/Standalone/LudensScope.app" pkgroot/Applications/

AAX_SRC=${AAX_SIGNED_BUNDLE:-$ART/AAX/LudensScope.aaxplugin}
if [[ -d "$AAX_SRC" ]]; then
  cp -R "$AAX_SRC" "pkgroot/Library/Application Support/Avid/Audio/Plug-Ins/"
  if [[ -n "${AAX_SIGNED_BUNDLE:-}" ]]; then
    echo "AAX: packing signed bundle from $AAX_SRC"
  else
    echo "AAX: packing UNSIGNED bundle (retail Pro Tools will not load it)"
  fi
else
  echo "AAX: not found at $AAX_SRC, packing without AAX"
fi

# Bundles are relocatable by default: the Installer looks for an existing copy
# of the same bundle identifier elsewhere on the disk and "upgrades" it there.
# All our bundles share one identifier, so it found the app in /Applications and
# tried to move the AAX next to it ("could not move LudensScope.aaxplugin to
# Applications, an item with the same name already exists"). Pinning every
# bundle to its payload path is what makes the upgrade deterministic.
pkgbuild --analyze --root pkgroot "$OUT/components.plist"

python3 - "$OUT/components.plist" <<'PYTHON'
import pathlib, plistlib, sys

path = pathlib.Path (sys.argv[1])
entries = plistlib.loads (path.read_bytes())

for entry in entries:
    entry["BundleIsRelocatable"] = False
    entry["BundleIsVersionChecked"] = False

path.write_bytes (plistlib.dumps (entries))
PYTHON

pkgbuild --root pkgroot --identifier com.ltillegal.ludensscope \
         --component-plist "$OUT/components.plist" \
         --version "$VERSION" --install-location / "$PKG"

rm -f "$OUT/components.plist"

if [[ -n "${APPLE_INSTALLER_ID:-}" ]]; then
  echo "signing pkg with $APPLE_INSTALLER_ID"
  productsign --sign "$APPLE_INSTALLER_ID" "$PKG" "$PKG.signed"
  mv "$PKG.signed" "$PKG"
fi

if [[ -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
  echo "notarising $PKG (this takes a few minutes)"
  xcrun notarytool submit "$PKG" \
    --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_APP_PASSWORD" --wait
  xcrun stapler staple "$PKG"
  xcrun stapler validate "$PKG"
else
  echo "notarisation skipped (APPLE_ID / APPLE_TEAM_ID / APPLE_APP_PASSWORD not set)"
fi

echo "done: $PKG"
shasum -a 256 "$PKG"
