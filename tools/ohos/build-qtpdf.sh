#!/usr/bin/env bash
# Build QtPdf (+ QtPdfQuick) for HarmonyOS from qtwebengine source and install
# it into $QT_OHOS. Qt ships no QtPdf for HarmonyOS, but qtwebengine's 6.12
# branch can build it for OHOS. Needs: setup-qt.sh done, python3, ninja,
# node, gperf, bison, flex (Xcode CLT has the last three). ~5 GB of source.
set -euo pipefail
source "$(dirname "$0")/env.sh"

# Pinned: the 6.12 branch head this was verified with, and its Chromium.
QTWEBENGINE_COMMIT=d4735ce2f841ffbe7cbac9ee7fd954f00514825f
src="$QT_SRC_ROOT/qtwebengine-6.12"
gn_prefix="$QT_SRC_ROOT/gn-host"
build="$QT_SRC_ROOT/build-qtpdf-ohos"
patches="$AIREADER_ROOT/tools/ohos/patches"

[ -x "$QT_OHOS/bin/qt-cmake" ] || die "run setup-qt.sh first"
mkdir -p "$QT_SRC_ROOT"

if [ ! -d "$src/.git" ]; then
    echo "Fetching qtwebengine $QTWEBENGINE_COMMIT"
    git init -q "$src"
    git -C "$src" fetch -q --depth 1 https://code.qt.io/qt/qtwebengine.git "$QTWEBENGINE_COMMIT"
    git -C "$src" checkout -q FETCH_HEAD
fi
chromium_sha=$(git -C "$src" ls-tree HEAD src/3rdparty | awk '{print $3}')
if [ ! -d "$src/src/3rdparty/.git" ]; then
    echo "Fetching qtwebengine-chromium $chromium_sha (large)"
    git init -q "$src/src/3rdparty"
    git -C "$src/src/3rdparty" fetch -q --depth 1 \
        https://code.qt.io/qt/qtwebengine-chromium.git "$chromium_sha"
    git -C "$src/src/3rdparty" checkout -q FETCH_HEAD
fi

# PDFium only looks for system fonts in Linux directories; HarmonyOS keeps
# them in /system/fonts. Without this, PDFs that don't embed their fonts
# (common for Chinese ones) render with blank glyphs.
for p in "$patches"/pdfium-*.patch; do
    if git -C "$src/src/3rdparty" apply --check "$p" 2>/dev/null; then
        echo "Applying $(basename "$p")"
        git -C "$src/src/3rdparty" apply "$p"
    fi
done

# Python with html5lib for Chromium's build scripts.
venv="$QT_SRC_ROOT/venv"
[ -x "$venv/bin/python" ] || python3 -m venv "$venv"
"$venv/bin/python" -c "import html5lib" 2>/dev/null || "$venv/bin/pip" -q install html5lib

# Cross builds need a host gn of the exact version qtwebengine expects.
# Any other gn on PATH (depot_tools!) is picked up first and fails, so
# put ours in front and drop depot_tools.
if [ ! -x "$gn_prefix/bin/gn" ]; then
    echo "Building host gn"
    cmake -G Ninja -S "$src/src/gn" -B "$QT_SRC_ROOT/build-gn" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
        -DCMAKE_INSTALL_PREFIX="$gn_prefix" -DPython3_EXECUTABLE="$venv/bin/python" >/dev/null
    cmake --build "$QT_SRC_ROOT/build-gn" --target install >/dev/null
fi
PATH="$gn_prefix/bin:$venv/bin:$(echo "$PATH" | tr ':' '\n' | grep -v depot_tools | paste -sd: -)"
export PATH

echo "Configuring QtPdf for HarmonyOS"
"$QT_OHOS/bin/qt-cmake" -G Ninja -S "$src" -B "$build" \
    -DQT_HOST_PATH="$QT_HOST" -DOHOS_SDK_ROOT="$OHOS_SDK_ROOT" \
    -DCMAKE_INSTALL_PREFIX="$QT_OHOS" -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE="$venv/bin/python" -DGn_EXECUTABLE="$gn_prefix/bin/gn" \
    -DFEATURE_qtwebengine_build=OFF -DFEATURE_qtpdf_build=ON \
    -DFEATURE_qtpdf_quick_build=ON -DFEATURE_qtpdf_widgets_build=OFF \
    -DFEATURE_pdf_v8=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
    > "$build.configure.log" 2>&1 || die "configure failed, see $build.configure.log"

echo "Building QtPdf (a few minutes)"
cmake --build "$build" > "$build.log" 2>&1 || die "build failed, see $build.log"
cmake --install "$build" >/dev/null
ls "$QT_OHOS/lib/libQt6Pdf.so" "$QT_OHOS/lib/libQt6PdfQuick.so"
echo "done. Next: tools/ohos/build.sh"
