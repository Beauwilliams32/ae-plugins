#!/usr/bin/env bash
# Build the fleet demo harness against the recovered namespaces.
#
# The recovered sources in src/ are compiled UNMODIFIED: this script only adds
# include paths and the harness itself. tools/demo/harness.cpp is new code that
# calls them; see its header comment for what the outputs are and are not.
set -euo pipefail

cd "$(dirname "$0")/../.."

NAMESPACES=(
  glitch lutblend typeriser spectra speedramp echo colorpulse autoflickerfix
  beatsnap chromakey noisefield pathscribe beatgrid maskflow shake
)

INCLUDES=()
SOURCES=()
for ns in "${NAMESPACES[@]}"; do
  if [[ -f "src/$ns/$ns.h" ]]; then
    INCLUDES+=("-Isrc/$ns")
    SOURCES+=("src/$ns/$ns.cpp")
  else
    echo "skip: src/$ns/$ns.h not recovered" >&2
  fi
done

mkdir -p build
clang++ -std=c++17 -O2 -Wall -Wno-unused-variable -Wno-unused-function \
  "${INCLUDES[@]}" \
  tools/demo/harness.cpp "${SOURCES[@]}" \
  -o build/fleet-demo

echo "built build/fleet-demo (${#SOURCES[@]} recovered namespaces)"
