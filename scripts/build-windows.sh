#!/usr/bin/env bash
# Cross-compile a JUCE VST3 for Windows from Linux, verify it actually loads
# under Wine, and package it into dist/ — no GitHub Actions minutes spent.
#
# Usage:
#   build-windows.sh <PluginTargetName> [repo_root]
#
# <PluginTargetName> is the juce_add_plugin target (the VST3 target is
# "<Name>_VST3" and the artefact dir is "<Name>_artefacts"). repo_root defaults
# to the current directory.
#
# Requires: cmake, g++-mingw-w64-x86-64-posix, wine, zip. The toolchain file
# (cmake/toolchain-mingw64.cmake) and the mingw-compat shim must be in the repo
# (copy them from this skill's assets/ if the project doesn't have them yet).
set -euo pipefail

NAME="${1:?usage: build-windows.sh <PluginTargetName> [repo_root]}"
ROOT="${2:-$(pwd)}"
cd "$ROOT"

BUILD_DIR="build-win"
VST3="$BUILD_DIR/${NAME}_artefacts/Release/VST3/${NAME}.vst3"
SCAN_SRC="$(dirname "$0")/../assets/vst3scan.c"
SCAN_EXE="$(mktemp -d)/vst3scan.exe"

echo "==> Configuring $NAME for Windows (MinGW, JUCE 7 pin, fully static)"
# JUCE 8 dropped MinGW support, so the cross-build pins JUCE 7.0.12. The
# toolchain file selects the mingw-posix compilers and the case-fix shim.
cmake -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -D${NAME}_JUCE_TAG=7.0.12 >/dev/null

echo "==> Building ${NAME}_VST3"
cmake --build "$BUILD_DIR" --target "${NAME}_VST3" --parallel "$(nproc)"

if [ ! -e "$VST3" ]; then
    echo "!! build did not produce $VST3" >&2
    exit 1
fi

echo "==> Verifying the VST3 loads under Wine (factory walk + instantiate)"
x86_64-w64-mingw32-gcc -O2 "$SCAN_SRC" -o "$SCAN_EXE"
# Wine reads a Z: drive mapped to the Linux root; hand it a Windows-style path.
if ! WINEDEBUG=-all wine "$SCAN_EXE" "Z:$(pwd)/$VST3" 2>/dev/null \
        | grep -qi "instantiated IComponent OK"; then
    echo "!! VST3 failed to instantiate under Wine — not packaging" >&2
    WINEDEBUG=-all wine "$SCAN_EXE" "Z:$(pwd)/$VST3" 2>/dev/null | grep -iv "^wine\|fixme" | tail -20 >&2 || true
    exit 1
fi
echo "   OK — component instantiates"

echo "==> Packaging into dist/"
mkdir -p dist
ZIP="dist/${NAME}-VST3-Windows-x64.zip"
rm -f "$ZIP"
( cd "$BUILD_DIR/${NAME}_artefacts/Release/VST3" && zip -r -q "$OLDPWD/$ZIP" "${NAME}.vst3" )
echo "   wrote $ZIP ($(stat -c%s "$ZIP" 2>/dev/null || stat -f%z "$ZIP") bytes)"
echo "==> Done. Windows VST3 built, verified, and packaged — zero CI minutes."
