#!/usr/bin/env bash
# Build AiReader for HarmonyOS PC and package it as a HAP.
#
#   tools/ohos/build.sh            debug HAP (installable on the emulator)
#   tools/ohos/build.sh --release  release HAP, signed if QT_HARMONYOS_SIGNING_*
#                                  is set (see docs/harmonyos.md)
#
# Output: build-ohos/AiReader-<version>[-debug][-unsigned].hap
set -euo pipefail
source "$(dirname "$0")/env.sh"

release=0
[ "${1:-}" = "--release" ] && release=1

[ -x "$QT_OHOS/bin/qt-cmake" ] || die "no Qt for HarmonyOS at $QT_OHOS (tools/ohos/setup-qt.sh)"
[ -e "$QT_OHOS/lib/libQt6Pdf.so" ] || die "no QtPdf in $QT_OHOS (tools/ohos/build-qtpdf.sh)"
[ -d "$OHOS_SDK_ROOT/native" ] || die "no HarmonyOS SDK at $OHOS_SDK_ROOT (install DevEco Studio)"

version=$(sed -nE 's/^project\(ai-reader VERSION ([0-9.]+).*/\1/p' "$AIREADER_ROOT/CMakeLists.txt")

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    "$QT_OHOS/bin/qt-cmake" -G Ninja -S "$AIREADER_ROOT" -B "$BUILD_DIR" \
        -DQT_HOST_PATH="$QT_HOST" -DOHOS_SDK_ROOT="$OHOS_SDK_ROOT" \
        -DHARMONYOS_HVIGOR="$HVIGORW" -DCMAKE_BUILD_TYPE=Release \
        -DAIREADER_OHOS_BUNDLE_NAME="$BUNDLE_NAME"
fi

cmake --build "$BUILD_DIR" --target ai-reader
out="$BUILD_DIR/harmonyos-build"
if [ $release = 0 ]; then
    # Qt's own target: always a debug-mode (debuggable) HAP.
    cmake --build "$BUILD_DIR" --target ai-reader_make_hap
    suffix=-debug
else
    # The make_hap target has no release switch; drive the deploy tool
    # directly. It signs when QT_HARMONYOS_SIGNING_* is in the environment.
    "$QT_HOST/bin/harmonydeployqt" \
        --input "$BUILD_DIR/ai-reader-harmony-deployment-settings.json" \
        --output "$out" --hvigor "$HVIGORW" --release
    suffix=
fi

hapdir="$out/entry/build/default/outputs/default"
if [ -n "${QT_HARMONYOS_SIGNING_PROFILE:-}" ]; then
    hap="$hapdir/entry-default-signed.hap"
else
    hap="$hapdir/entry-default-unsigned.hap"
    suffix="$suffix-unsigned"
fi
dest="$BUILD_DIR/AiReader-$version$suffix.hap"
cp "$hap" "$dest"
echo
echo "HAP: $dest ($(du -h "$dest" | cut -f1))"
