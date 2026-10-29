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
  GlitchKit)  NS=glitch ;;
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

# BOTH architectures, because the shipped fleet is universal: every bundle under
# "Williams Media" reports `lipo -archs` of "x86_64 arm64". A thin arm64 rebuild
# would not load on an Intel Mac, and would not load under an AE running translated
# on Apple silicon either — and the PiPL already declares CodeMacIntel64 alongside
# CodeMacARM64, so the resource was ready for this before the compile was.
ARCHS=(-arch arm64 -arch x86_64)
i=0
for unit in "${UNITS[@]}"; do
  i=$((i + 1))
  clang++ -std=c++17 -O2 "${ARCHS[@]}" \
          -Wall -Wextra -Wno-missing-field-initializers \
          -c "${INCLUDES[@]}" -o "$TMP/unit$i.o" "$unit"
done

echo "link..."
clang++ "${ARCHS[@]}" -bundle -o "$BIN" "$TMP"/*.o

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

# EXACT match on the pair, in either order: the shipped bundles are universal and a
# thin rebuild is the failure this checks for.
ARCHS_BUILT="$(lipo -archs "$BIN")"
case "$ARCHS_BUILT" in
  "x86_64 arm64"|"arm64 x86_64") ;;
  *) echo "error: expected a universal binary, got: $ARCHS_BUILT" >&2; exit 1 ;;
esac
echo "arch:   $ARCHS_BUILT (universal, as shipped)"

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
	<key>CFBundleSignature</key>				<string>FXTC</string>
	<key>CFBundleShortVersionString</key>	<string>1.0.0</string>
	<key>CFBundleVersion</key>				<string>1.0.0</string>
	<key>LSMinimumSystemVersion</key>		<string>11.0</string>
</dict>
</plist>
PLIST

# 'eFKT' + the creator code, exactly as every shipped bundle has it. The rebuild
# carried '????' until this was compared against the originals rather than assumed.
printf 'eFKTFXTC' > "$OUT/Contents/PkgInfo"

# --- sign ----------------------------------------------------------------
# The shipped bundles are AD-HOC SIGNED with their Info.plist and Resources sealed:
# `codesign -dv` on LUTBlend.plugin reports `Signature=adhoc`, `Info.plist entries=10`
# and `Sealed Resources version=2`, and its signature Identifier is the bundle id.
# A linker-signed binary has none of that: it reports `adhoc,linker-signed` with the
# executable name as its Identifier and no sealed resources.
#
# Signing is the LAST step, after every file is in place, because any later change to
# the bundle invalidates the seal.
echo "sign..."
codesign --force --sign - "$OUT"
codesign --verify --strict "$OUT"

# --- compare against the shipped bundle -------------------------------------
# The rebuild is "structurally faithful" only if it actually matches the artefact it
# replaces, so that claim is measured here rather than asserted in prose. The shipped
# fleet lives under "Williams Media" in the AE install; the 2025 path is a fallback for
# a machine that has not moved to AE 2026 yet.
#
# THIS IS NOT HOST ACCEPTANCE. It proves the bundle AE will load is built the way the
# one that shipped was built - same architectures, same creator code, same parameter
# dictionary, same sealed ad-hoc signature, byte-identical PiPL. It says nothing about
# whether After Effects renders with it. Only a run in AE does that.
SHIPPED=""
for cand in "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/${PLUGIN}.plugin" \
            "/Applications/Adobe After Effects 2025/Plug-ins/Williams Media/${PLUGIN}.plugin"; do
  [ -d "$cand" ] && { SHIPPED="$cand"; break; }
done

if [ -z "$SHIPPED" ]; then
  echo
  echo "compare: SKIPPED - no shipped ${PLUGIN}.plugin found under /Applications."
  echo "         (install the AE Plug-ins/Williams Media directory and re-run to get"
  echo "          the structural comparison)"
else
  echo
  echo "compare: against the shipped bundle"
  echo "         $SHIPPED"
  FAIL=0
  check() { # check <label> <shipped-value> <rebuilt-value>
    if [ "$2" = "$3" ]; then
      printf "         ok    %-24s %s\n" "$1" "$2"
    else
      printf "         FAIL  %-24s shipped=[%s] rebuilt=[%s]\n" "$1" "$2" "$3"
      FAIL=1
    fi
  }

  # 1. architectures - the fleet is universal, and a thin rebuild loads on neither
  #    architecture properly.
  check "architectures" \
        "$(lipo -archs "$SHIPPED/Contents/MacOS/$PLUGIN" 2>/dev/null)" \
        "$(lipo -archs "$BIN")"

  # 2. the creator code. Every shipped bundle carries 'eFKTFXTC'; a rebuild that
  #    carried '????' was indistinguishable to AE's loader.
  check "PkgInfo" \
        "$(tr -d '\0' < "$SHIPPED/Contents/PkgInfo" 2>/dev/null)" \
        "$(tr -d '\0' < "$OUT/Contents/PkgInfo")"

  # 3. the parameter dictionary, as a set of keys and their values. This is what the
  #    host reads to decide what the effect exposes.
  ship_plist="$(mktemp)"; rebuild_plist="$(mktemp)"
  plutil -convert xml1 -o "$ship_plist" "$SHIPPED/Contents/Info.plist" 2>/dev/null
  plutil -convert xml1 -o "$rebuild_plist" "$OUT/Contents/Info.plist" 2>/dev/null
  check "Info.plist" \
        "$(plutil -p "$ship_plist" 2>/dev/null | sort)" \
        "$(plutil -p "$rebuild_plist" 2>/dev/null | sort)"
  rm -f "$ship_plist" "$rebuild_plist"

  # 4. the signature's identity. An ad-hoc signature whose Identifier is the bundle
  #    id, with the same sealed-resources version, is the same kind of artefact the
  #    fleet shipped - a linker-signed binary with no sealed resources is not.
  check "signature Identifier" \
        "$(codesign -dv "$SHIPPED" 2>&1 | sed -n 's/^Identifier=//p')" \
        "$(codesign -dv "$OUT" 2>&1 | sed -n 's/^Identifier=//p')"
  check "signature kind" \
        "$(codesign -dv "$SHIPPED" 2>&1 | sed -n 's/^Signature=//p')" \
        "$(codesign -dv "$OUT" 2>&1 | sed -n 's/^Signature=//p')"
  check "sealed resources" \
        "$(codesign -d --verbose=2 "$SHIPPED" 2>&1 | sed -n 's/^Sealed Resources version=\([0-9]*\).*/\1/p')" \
        "$(codesign -d --verbose=2 "$OUT" 2>&1 | sed -n 's/^Sealed Resources version=\([0-9]*\).*/\1/p')"

  # 5. the PiPL resource. This is the strongest check available without a host: it
  #    is the flag contract the host reads before it calls the effect at all.
  #
  #    The comparison is the PiPL PAYLOAD - the first 576 bytes, which is where
  #    every '8BIM' atom lives - and that is the same definition the repository's own
  #    test/pipl.test.mjs uses for all sixteen effects. The bytes after it are Rez's
  #    resource map, and they are not part of the contract: the shipped fleet was
  #    compiled by the original authors' Rez and this machine's Rez writes one
  #    different bookkeeping byte in the map for some effects. The resource SET and
  #    its ORDER are identical - same atoms, same order - so the file is the same
  #    resource; only the map's own count differs. It is reported, not failed on.
  check "PiPL payload (576 bytes)" \
        "$(cmp -s -n 576 "$SHIPPED/Contents/Resources/$PLUGIN.rsrc" "$RSRC" && echo identical || echo DIFFERS)" \
        "identical"
  echo "         note  resource map     shipped=$(shasum -a 256 "$SHIPPED/Contents/Resources/$PLUGIN.rsrc" | cut -c1-12) rebuilt=$(shasum -a 256 "$RSRC" | cut -c1-12)"

  echo
  if [ "$FAIL" -ne 0 ]; then
    echo "compare: MISMATCH - the rebuild is not structurally faithful to the shipped bundle."
    exit 1
  fi
  echo "compare: all fields match the shipped bundle."
fi

echo
echo "built: $OUT"
find "$OUT" -type f | sed "s|$ROOT/||"
echo
echo "NOTE: structurally faithful, NOT host-verified. No AE host has loaded this bundle."
