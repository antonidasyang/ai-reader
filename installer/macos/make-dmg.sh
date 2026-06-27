#!/bin/zsh
# Build a distributable arm64 ai-reader DMG.
#
# If a "Developer ID Application" certificate AND a notarytool keychain profile
# named $NOTARY_PROFILE (default: YXCNotary) are present, the .app + .dmg
# are signed with a hardened runtime, notarized, and stapled. Otherwise an
# AD-HOC signed dmg is produced (Gatekeeper will warn; first launch needs
# right-click -> Open). Ad-hoc is still required on Apple Silicon — unsigned
# arm64 code is killed on launch — so this script always signs at least ad-hoc.
#
# Prereqs (one-time, your Apple account) for the signed/notarized path:
#   1) Create a "Developer ID Application" cert (Xcode > Settings > Accounts >
#      Manage Certificates > + > Developer ID Application) — lands in login keychain.
#   2) xcrun notarytool store-credentials YXCNotary \
#        --apple-id <your-apple-id> --team-id 7S7PRTP374 --password <app-specific-pw>
#
# Prereq build (produces build/ai-reader.app with MicroTeX res staged in it):
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
#         -DCMAKE_POLICY_VERSION_MINIMUM=3.5
#   cmake --build build
#
# Usage:  installer/macos/make-dmg.sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

APP_SRC="$ROOT/build/ai-reader.app"
NOTARY_PROFILE="${NOTARY_PROFILE:-YXCNotary}"
TEAM_ID="${TEAM_ID:-7S7PRTP374}"

# Version: prefer manifest.json's latestVersion, fall back to CMake project().
VER=$(sed -n 's/.*"latestVersion"[[:space:]]*:[[:space:]]*"\([0-9][0-9.]*\)".*/\1/p' manifest.json 2>/dev/null | head -1)
[ -z "$VER" ] && VER=$(sed -n 's/.*project(ai-reader VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
[ -z "$VER" ] && VER="0.0.0"

OUT="$ROOT/installer/Output"
DMG="$OUT/ai-reader-$VER-macOS-arm64.dmg"

[ -d "$APP_SRC" ] || { echo "build first: cmake --build build  (expected $APP_SRC)"; exit 1; }
mkdir -p "$OUT"

MACDEPLOYQT="$(command -v macdeployqt || echo "$(brew --prefix qt)/bin/macdeployqt")"
STAGE="$(mktemp -d)"
APP="$STAGE/ai-reader.app"
cp -R "$APP_SRC" "$APP"

# MicroTeX res/ must be inside the bundle for math to render on machines without
# the dev build tree. The APPLE POST_BUILD step in CMakeLists.txt stages it into
# Contents/Resources/microtex_res; warn loudly if it's missing (stale build).
if [ ! -f "$APP/Contents/Resources/microtex_res/.clatexmath-res_root" ]; then
  echo "WARNING: MicroTeX res/ missing from the app (Contents/Resources/microtex_res)." >&2
  echo "         Math will fall back to raw LaTeX. Rebuild: cmake --build build" >&2
fi

echo "== bundling Qt (macdeployqt) =="
# Homebrew splits Qt into per-module kegs; macdeployqt only searches qtbase/
# qtdeclarative by default, so it can't resolve frameworks the QML imports pull
# in transitively (QtSvg, QtMultimedia, QtVirtualKeyboard, Qt5Compat). Feed it
# every installed qt*/lib as a -libpath to silence the "Cannot resolve" errors.
LIBPATHS=()
for k in /opt/homebrew/opt/qt*/lib; do [ -d "$k" ] && LIBPATHS+=("-libpath=$k"); done
# Tolerate macdeployqt's own codesign step — its rpath fixups invalidate
# Homebrew's existing signatures (e.g. libbrotlicommon), so its verify fails.
# The deployment still completes; we re-sign the whole bundle cleanly below.
"$MACDEPLOYQT" "$APP" -qmldir="$ROOT/qml" "${LIBPATHS[@]}" >/dev/null 2>&1 || true

# macdeployqt occasionally leaves a Homebrew install-name (e.g. libbrotlicommon's
# own LC_ID, or a -change reference) -> rewrite any /opt/homebrew refs to
# bundle-relative @rpath so the app runs on Macs without Homebrew. Must happen
# BEFORE signing (install_name_tool invalidates signatures).
echo "== fixing up dylib paths =="
find "$APP/Contents/Frameworks" -type f -name "*.dylib" 2>/dev/null | while read -r f; do
  idp=$(otool -D "$f" 2>/dev/null | tail -1)
  [[ "$idp" == /opt/homebrew/* ]] && install_name_tool -id "@rpath/$(basename "$f")" "$f" 2>/dev/null || true
  otool -L "$f" 2>/dev/null | awk '/\/opt\/homebrew/{print $1}' | while read -r d; do
    install_name_tool -change "$d" "@rpath/$(basename "$d")" "$f" 2>/dev/null || true
  done
done

# --- code signing ---
ID=$(security find-identity -v -p codesigning 2>/dev/null | grep "Developer ID Application" | head -1 | grep -oE '[0-9A-F]{40}' || true)
SIGNED=0
if [ -n "$ID" ]; then
  echo "== signing with Developer ID ($ID) =="
  # Sign nested code first (frameworks/plugins), then the app, hardened runtime + timestamp.
  find "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" -type f \( -name "*.dylib" -o -perm -u+x \) 2>/dev/null \
    | while read f; do codesign --force --options runtime --timestamp --sign "$ID" "$f" 2>/dev/null || true; done
  codesign --force --deep --options runtime --timestamp --sign "$ID" "$APP"
  codesign --verify --strict --verbose=1 "$APP" && SIGNED=1
else
  echo "== no Developer ID cert -> ad-hoc signing (Gatekeeper warns; first run: right-click > Open) =="
  # Ad-hoc re-sign the whole bundle: fixes the Homebrew signatures macdeployqt
  # broke and makes it launchable on Apple Silicon (unsigned arm64 is killed).
  codesign --force --deep --sign - "$APP" 2>/dev/null || true
fi

# --- build the dmg (drag-to-Applications) ---
echo "== creating dmg =="
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "AI Reader" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null

# --- sign + notarize + staple the dmg (only when Developer ID signed) ---
if [ "$SIGNED" = "1" ]; then
  codesign --force --timestamp --sign "$ID" "$DMG" || true
  if xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1; then
    echo "== notarizing (this can take a few minutes) =="
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
    echo "== notarized + stapled =="
  else
    echo "!! signed but NOT notarized: no notarytool profile '$NOTARY_PROFILE'."
    echo "   run: xcrun notarytool store-credentials $NOTARY_PROFILE --apple-id <id> --team-id $TEAM_ID --password <app-specific-pw>"
  fi
fi

# Leave a standalone copy of the finalized (signed, self-contained) .app next to
# the dmg so it's a first-class deliverable, not only reachable by mounting.
APP_OUT="$OUT/ai-reader.app"
rm -rf "$APP_OUT"
cp -R "$APP" "$APP_OUT"

rm -rf "$STAGE"
echo ""
echo "APP: $APP_OUT"
echo "DMG: $DMG"
if [ "$SIGNED" = "1" ]; then
  echo "(Developer ID signed)"
else
  echo "(AD-HOC signed — Gatekeeper will warn; first run: right-click the app -> Open)"
fi
