#!/bin/sh
# Sign the locally-built dev ai-reader.app so macOS keeps recognising it as the
# same program across rebuilds -- which is what makes the keychain's "Always
# Allow" stick. CMake's default build only ad-hoc signs, and an ad-hoc
# signature changes every build: the keychain ACL never matches, so every
# secret the app touches re-prompts. With a 30s sync poll behind it, that is a
# password dialog every half minute until the app is killed.
#
# Identity, in order:
#   1. $AIREADER_CODESIGN_ID   -- explicit override ("-" skips signing)
#   2. the local dev identity from setup-dev-signing.sh
#   3. a Developer ID Application cert
#   4. nothing -> leave the ad-hoc signature, and SAY SO
#
# (2) outranks (3) on purpose. A login-keychain key is only reachable from an
# Aqua session, so preferring the Developer ID would sign when you build from
# Terminal and fail when a background agent or CI builds -- and an identity
# that flips between builds re-triggers the prompts just like ad-hoc does. The
# dev identity signs identically from every session. Distribution is unaffected:
# make-dmg.sh still signs the shipped .dmg with the Developer ID and notarizes.
APP="$1"
[ -d "$APP" ] || exit 0

DEV_KC="$HOME/Library/Keychains/ai-reader-dev.keychain-db"
DEV_CN="AI Reader Dev Signing"
KC_ARG=""

ID="${AIREADER_CODESIGN_ID:-}"
[ "$ID" = "-" ] && exit 0

if [ -z "$ID" ] && [ -f "$DEV_KC" ]; then
  # A keychain is locked again after every reboot; we own this password.
  PW_FILE="$HOME/.ai-reader-signing/keychain-password"
  [ -f "$PW_FILE" ] && security unlock-keychain -p "$(cat "$PW_FILE")" "$DEV_KC" 2>/dev/null
  ID=$(security find-identity -p codesigning "$DEV_KC" 2>/dev/null \
        | grep -m1 "$DEV_CN" | grep -oE '[0-9A-F]{40}')
  [ -n "$ID" ] && KC_ARG="$DEV_KC"
fi

if [ -z "$ID" ]; then
  ID=$(security find-identity -v -p codesigning 2>/dev/null \
        | grep -m1 'Developer ID Application' \
        | grep -oE '[0-9A-F]{40}')
fi

if [ -z "$ID" ]; then
  echo "[sign-dev] no signing identity; leaving ad-hoc signature."
  echo "[sign-dev]   run installer/macos/setup-dev-signing.sh once to fix this."
  exit 0
fi

if [ -n "$KC_ARG" ]; then
  ERR=$(codesign --force --keychain "$KC_ARG" --sign "$ID" "$APP" 2>&1)
else
  ERR=$(codesign --force --sign "$ID" "$APP" 2>&1)
fi

if [ $? -eq 0 ]; then
  echo "[sign-dev] signed $(basename "$APP") with $ID"
  codesign -d --requirements - "$APP" 2>&1 | grep designated | sed 's/^/[sign-dev] /'
  exit 0
fi

echo "[sign-dev] WARNING: codesign failed, bundle is still ad-hoc signed."
echo "$ERR" | sed 's/^/[sign-dev]   /'
case "$ERR" in
  *errSecInternalComponent*)
    echo "[sign-dev]   The key is not reachable from a '$(launchctl managername 2>/dev/null)' session."
    echo "[sign-dev]   Run installer/macos/setup-dev-signing.sh once; it builds an"
    echo "[sign-dev]   identity that signs from any session type."
    ;;
esac
# Warn, never fail the build over a dev signature.
exit 0
