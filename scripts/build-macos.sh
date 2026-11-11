#!/usr/bin/env bash
#
# Build, sign and install LudensScope as a universal VST3 on macOS.
#
set -euo pipefail

cd "$(dirname "$0")/.."

cmake --preset macos-universal
cmake --build --preset macos-universal -j "$(sysctl -n hw.ncpu)"

PLUGIN="build/LudensScope_artefacts/Release/VST3/LudensScope.vst3"

if [[ ! -d "$PLUGIN" ]]; then
  echo "error: plugin bundle not found at $PLUGIN" >&2
  exit 1
fi

# Ad-hoc signature is required for arm64. For distribution, replace "-" with a
# Developer ID identity and notarise the bundle.
codesign --force --deep --sign - "$PLUGIN"

DEST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$DEST"
rm -rf "$DEST/LudensScope.vst3"
cp -R "$PLUGIN" "$DEST/"

echo "Installed: $DEST/LudensScope.vst3"
echo "Rescan plugins in Cubase (Studio -> VST Plug-in Manager)."
