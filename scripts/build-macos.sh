#!/usr/bin/env bash
#
# Build, sign and install LudensScope on macOS as a universal VST3 + AU.
#
set -euo pipefail

cd "$(dirname "$0")/.."

cmake --preset macos-universal
cmake --build --preset macos-universal -j "$(sysctl -n hw.ncpu)"

VST3_PLUGIN="build/LudensScope_artefacts/Release/VST3/LudensScope.vst3"
AU_PLUGIN="build/LudensScope_artefacts/Release/AU/LudensScope.component"

# Ad-hoc signature is required for arm64. For distribution, replace "-" with a
# Developer ID identity and notarise both bundles.
for bundle in "$VST3_PLUGIN" "$AU_PLUGIN"; do
  if [[ ! -d "$bundle" ]]; then
    echo "error: bundle not found at $bundle" >&2
    exit 1
  fi

  codesign --force --deep --sign - "$bundle"
done

VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"

mkdir -p "$VST3_DEST" "$AU_DEST"
rm -rf "$VST3_DEST/LudensScope.vst3" "$AU_DEST/LudensScope.component"
cp -R "$VST3_PLUGIN" "$VST3_DEST/"
cp -R "$AU_PLUGIN" "$AU_DEST/"

echo "Installed:"
echo "  $VST3_DEST/LudensScope.vst3"
echo "  $AU_DEST/LudensScope.component"
echo "Cubase: Studio -> VST Plug-in Manager -> Rescan."
echo "AU check: auval -v aufx LDSC LDNS"
