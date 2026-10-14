#!/usr/bin/env bash
# build-plugin.sh — assemble a loadable .plugin bundle for one effect.
#
# This is the second half of the recovery: packaging/ holds the PiPL resource
# rebuilt from the recovered spec, src/<ns>/ holds the recovered DSP, and
# plugins/<Effect>/ holds the newly written host integration. This script joins
# them into a bundle.
#
#   ./tools/build-plugin.sh LUTBlend
#
# Requires a local Adobe After Effects SDK. Set AE_SDK_DIR to its "Examples"
# directory, or unpack an SDK under ~/.ae-sdk/ and let this find it.
#
# IMPORTANT: this produces a bundle, not a verified plugin. Nothing here has been
# loaded by After Effects, so host acceptance is UNTESTED. See the plugin's
# README for what that means.

set -euo pipefail

PLUGIN="${1:-}"
if [ -z "$PLUGIN" ]; then
  echo "usage: $0 <PluginName>" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# --- locate the SDK -------------------------------------------------------
find_sdk() {
  if [ -n "${AE_SDK_DIR:-}" ] && [ -f "$AE_SDK_DIR/Headers/AE_Effect.h" ]; then
    echo "$AE_SDK_DIR"; return 0
  fi
  for d in "$HOME"/.ae-sdk/*/ "$HOME"/.ae-sdk/*/*/; do
    if [ -f "${d}Examples/Headers/AE_Effect.h" ]; then echo "${d}Examples"; return 0; fi
  done
  return 1
}
SDK="$(find_sdk)" || {
  echo "error: no AE SDK found." >&2
  echo "       set AE_SDK_DIR to the SDK's Examples directory, or unpack one under ~/.ae-sdk/" >&2
  exit 1
}
echo "sdk:    $SDK"

SRC="$ROOT/plugins/$PLUGIN"
PIPL_R="$ROOT/packaging/${PLUGIN}PiPL.r"
[ -f "$SRC/$PLUGIN.cpp" ]  || { echo "error: missing $SRC/$PLUGIN.cpp" >&2; exit 1; }
[ -f "$PIPL_R" ]            || { echo "error: missing $PIPL_R" >&2; exit 1; }

# Which recovered namespace the effect's DSP lives in. Kept explicit rather than
# guessed, because guessing wrong would silently link the wrong engine.
case "$PLUGIN" in
  LUTBlend)   NS=lutblend ;;
  SpectraKeys|BeatSnap|BeatGridRuler|PathScribe) NS=spectra ;;
  TypeRiser)  NS=typeriser ;;
  *) echo "error: no recovered DSP namespace mapped for $PLUGIN" >&2; exit 1 ;;
esac
DSP_CPP="$ROOT/src/$NS/$NS.cpp"
[ -f "$DSP_CPP" ] || { echo "error: missing $DSP_CPP" >&2; exit 1; }
echo "dsp:    src/$NS"

OUT="$ROOT/build/$PLUGIN.plugin"
BIN="$OUT/Contents/MacOS/$PLUGIN"
RSRC="$OUT/Contents/Resources/$PLUGIN.rsrc"

rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

INCLUDES=(-I "$SDK/Headers" -I "$SDK/Headers/SP" -I "$SDK/Util"
          -I "$ROOT/src/$NS" -I "$SRC")

echo "compile..."
# Object names are NUMBERED, not derived from the source basename. On a
# case-insensitive filesystem (the macOS default) "LUTBlend.cpp.o" and
# "lutblend.cpp.o" are the SAME FILE, so naming objects after their sources makes
# the plugin's object silently overwrite the DSP's — or be overwritten by it — and
# the only symptom is a link error saying EffectMain was never defined. Numbering
# avoids the collision entirely.
#
# -Wno-missing-field-initializers: the SDK's own Param_Utils.h uses {{0}}.
# Every .cpp in the plugin's own directory, plus the recovered DSP, plus the two
# SDK utility translation units the plugin layer needs.
UNITS=()
for f in "$SRC"/*.cpp; do UNITS+=("$f"); done
UNITS+=("$DSP_CPP" "$SDK/Util/AEGP_SuiteHandler.cpp" "$SDK/Util/MissingSuiteError.cpp")

i=0
for unit in "${UNITS[@]}"; do
  i=$((i + 1))
  clang++ -std=c++17 -O2 -arch arm64 \
          -Wall -Wextra -Wno-missing-field-initializers \
          -c "${INCLUDES[@]}" -o "$TMP/unit$i.o" "$unit"
done

echo "link..."
clang++ -arch arm64 -bundle -o "$BIN" "$TMP"/*.o

# The Mach-O must EXPORT EffectMain. Note that visibility is deliberately NOT
# hidden: the shipped bundles export their C++ symbols too (nm -gU on the original
# LUTBlend shows 20, including AEGP_SuiteHandler's), so matching them means not
# adding -fvisibility=hidden. Adding it also silently broke the EffectMain export
# despite DllExport, which is worth not rediscovering.
# EXACT match required. A substring grep for "_EffectMain" also matches
# "_EffectMainRenamed", so a plain `grep -q` here reports success for a plugin
# whose entry point AE will never find. Anchor the whole line.
if ! nm -gU "$BIN" | grep -qE "^[0-9a-f]+ T _EffectMain$"; then
  echo "error: $PLUGIN does not export EffectMain" >&2
  nm -gU "$BIN" | grep -i effectmain >&2 || echo "       (no symbol containing 'effectmain' at all)" >&2
  exit 1
fi
echo "export: _EffectMain ok"

if [ "$(lipo -archs "$BIN")" != "arm64" ]; then
  echo "error: unexpected architectures: $(lipo -archs "$BIN")" >&2
  exit 1
fi

echo "rez PiPL..."
Rez -useDF -o "$RSRC" \
    -I "$SDK/Headers" -I "$SDK/Resources" \
    -d AE_OS_MAC -d AE_PROC_ARM64 \
    "$PIPL_R"

# --- Info.plist -----------------------------------------------------------
# Values recovered from the shipped bundle's Info.plist where available.
cat > "$OUT/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>	<string>English</string>
	<key>CFBundleExecutable</key>			<string>$PLUGIN</string>
	<key>CFBundleIdentifier</key>			<string>com.williamsmedia.aftereffects.$PLUGIN</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key>					<string>$PLUGIN</string>
	<key>CFBundlePackageType</key>			<string>eFKT</string>
	<key>CFBundleSignature</key>				<string>????</string>
	<key>CFBundleShortVersionString</key>	<string>1.0.0</string>
	<key>CFBundleVersion</key>				<string>1.0.0</string>
	<key>LSMinimumSystemVersion</key>		<string>11.0</string>
	<key>NSHighResolutionCapable</key>		<true/>
</dict>
</plist>
PLIST

printf 'eFKT????' > "$OUT/Contents/PkgInfo"

echo
echo "built: $OUT"
find "$OUT" -type f | sed "s|$ROOT/||"
echo
echo "NOTE: assembled, not verified. No AE host has loaded this bundle."
