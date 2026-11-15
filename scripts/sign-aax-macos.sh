#!/usr/bin/env bash
# Sign a JUCE-built AAX bundle with the PACE Eden wraptool.
#
# Prerequisites (see docs/aax-signing.md):
#   - Avid approval + AAX SDK access
#   - PACE Eden access, PACE Central "wrap configuration" GUID
#   - iLok account, DSIG signing identity
#
# Usage:
#   AAX_SIGN_ID=... AAX_WRAP_CONFIG=... scripts/sign-aax-macos.sh [input] [output]
#
# The exact wraptool flags differ between Eden versions; adjust WRAP_ARGS if
# your versions prints a different help. The script only wraps/signs, it does
# not rebuild the pkg - replace the .aaxplugin in the installer afterwards.

set -euo pipefail

INPUT=${1:-build/LudensScope_artefacts/Release/AAX/LudensScope.aaxplugin}
OUTPUT=${2:-dist/LudensScope.aaxplugin}

WRAP=${AAX_WRAP_TOOL:-$(command -v wraptool || true)}
SIGN_ID=${AAX_SIGN_ID:?set AAX_SIGN_ID (PACE DSIG identity)}
WRAP_CONFIG=${AAX_WRAP_CONFIG:?set AAX_WRAP_CONFIG (PACE Central wrap configuration GUID)}

if [[ ! -d "$INPUT" ]]; then
  echo "error: AAX bundle not found: $INPUT" >&2
  echo "build it first (Release) or pass the path as the first argument" >&2
  exit 1
fi

if [[ -z "$WRAP" || ! -x "$WRAP" ]]; then
  echo "error: wraptool not found in PATH; set AAX_WRAP_TOOL" >&2
  echo "PACE Eden v5 usually: /Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"
rm -rf "$OUTPUT"

# Eden wraptool invocation. The AAX SDK/PACE sign the bundle in place when an
# output path is omitted; keep an explicit copy so the build tree stays clean.
cp -R "$INPUT" "$OUTPUT"

# Common Eden form: wraptool --signid <dsig> --wrapConfig <guid> <bundle>
# Some versions also need -i/-o; adjust here if needed.
"$WRAP" --signid "$SIGN_ID" --wrapConfig "$WRAP_CONFIG" "$OUTPUT"

echo "signed: $OUTPUT"
echo "verify: codesign -dv --verbose=2 '$OUTPUT' && open in Pro Tools (developer build + iLok)"
