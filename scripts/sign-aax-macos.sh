#!/usr/bin/env bash
# PACE-sign a JUCE-built AAX bundle (macOS).
#
# Order matters: Apple codesign FIRST, then wraptool, and never codesign again
# afterwards - a second Apple signature invalidates the PACE wrap and Pro Tools
# stops seeing the plugin.
#
# Usage:
#   export AAX_ACCOUNT="<iLok account>"
#   export AAX_KEYFILE="$HOME/.pace/eden.p12"
#   export AAX_KEYPASSWORD="<password>"
#   export AAX_WRAP_CONFIG="<PACE Central wrap configuration GUID>"
#   export AAX_SIGN_ID="<DSIG identity>"          # only if your Eden needs it
#   export APPLE_SIGN_ID="Developer ID Application: ... (TEAMID)"   # optional
#   scripts/sign-aax-macos.sh [input] [output]
#
# Needs a physical iLok (or iLok Cloud) with the "Eden Tools" licence, so this
# cannot run on a hosted CI runner.

set -euo pipefail

INPUT=${1:-build/LudensScope_artefacts/Release/AAX/LudensScope.aaxplugin}
OUTPUT=${2:-dist/LudensScope.aaxplugin}

: "${AAX_ACCOUNT:?set AAX_ACCOUNT (iLok account name)}"
: "${AAX_KEYFILE:?set AAX_KEYFILE (PACE Eden .p12)}"
: "${AAX_KEYPASSWORD:?set AAX_KEYPASSWORD}"
: "${AAX_WRAP_CONFIG:?set AAX_WRAP_CONFIG (wrap configuration GUID)}"
SIGN_ID=${AAX_SIGN_ID:-}
APPLE_SIGN_ID=${APPLE_SIGN_ID:-}

WRAP=${AAX_WRAP_TOOL:-$(command -v wraptool || true)}
if [[ -z "$WRAP" || ! -x "$WRAP" ]] && [[ -x "/Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool" ]]; then
  WRAP="/Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool"
fi
if [[ -z "$WRAP" || ! -x "$WRAP" ]]; then
  echo "error: wraptool not found; set AAX_WRAP_TOOL" >&2
  echo "Eden v5 usually: /Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool" >&2
  exit 1
fi
if [[ ! -d "$INPUT" ]]; then
  echo "error: AAX bundle not found: $INPUT" >&2
  echo "build it first: cmake --build build --target LudensScope_AAX --config Release" >&2
  exit 1
fi

BIN_NAME=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$INPUT/Contents/Info.plist" 2>/dev/null || true)
if [[ -z "$BIN_NAME" ]]; then
  BIN_NAME=$(ls "$INPUT/Contents/MacOS" | head -1)
fi
if [[ -z "$BIN_NAME" || ! -f "$INPUT/Contents/MacOS/$BIN_NAME" ]]; then
  echo "error: no executable inside $INPUT/Contents/MacOS" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"
rm -rf "$OUTPUT"
cp -R "$INPUT" "$OUTPUT"

BIN="$OUTPUT/Contents/MacOS/$BIN_NAME"

# 1. Apple signature first, so wraptool sees a codesigned binary.
if [[ -n "$APPLE_SIGN_ID" ]]; then
  echo "codesign: $APPLE_SIGN_ID"
  codesign --force --timestamp --options runtime --sign "$APPLE_SIGN_ID" \
    --deep "$OUTPUT" >/dev/null
else
  echo "warning: APPLE_SIGN_ID is empty, skipping Developer ID codesign"
  echo "         (ad-hoc only; fine for testing, not for distribution)"
  codesign --force --sign - --deep "$OUTPUT" >/dev/null
fi

# 2. PACE wrap. --dsigharden makes wraptool set the hardened-runtime flags Apple
#    needs for notarisation. The binary is wrapped in place.
WRAP_ARGS=(sign --verbose
  --account "$AAX_ACCOUNT"
  --keyfile "$AAX_KEYFILE"
  --keypassword "$AAX_KEYPASSWORD"
  --wcguid "$AAX_WRAP_CONFIG"
  --dsigharden
  --in "$BIN"
  --out "$BIN")
if [[ -n "$SIGN_ID" ]]; then
  WRAP_ARGS+=(--signid "$SIGN_ID")
fi

echo "wraptool: signing $BIN_NAME"
if ! "$WRAP" "${WRAP_ARGS[@]}"; then
  echo "error: wraptool failed. Check: iLok plugged in and 'Eden Tools' licence" >&2
  echo "       activated on it; wrap configuration GUID matches PACE Central." >&2
  echo "       Run '$WRAP --help' if this Eden build uses different flags." >&2
  exit 1
fi

# 3. Verify. No codesign after this point.
if ! codesign --verify --strict --verbose=1 "$OUTPUT" >/dev/null 2>&1; then
  echo "warning: codesign --verify failed after wrapping; check 'codesign -dv'" >&2
fi

echo "signed: $OUTPUT"
echo "next:  pack it into the .pkg, then notarise the pkg (not the plugin)"
echo "check: codesign -dv --verbose=4 '$OUTPUT'"
