#!/bin/sh
# Sign the locally-built dev ai-reader.app with a Developer ID (if one is in the
# keychain) so macOS recognises the app across rebuilds and the keychain
# "Always Allow" choice persists. CMake's default build only ad-hoc signs, and
# ad-hoc signatures change every build -- which re-triggers the keychain prompt
# every launch. No-op (leaves the ad-hoc signature) when no identity is found.
#
# Override the identity with AIREADER_CODESIGN_ID=<name-or-hash>; set it to "-"
# to skip signing entirely.
#
# NOTE: codesign needs the private key out of the login keychain, and that is
# only reachable from a GUI (Aqua) session. Built from a background/ssh session
# -- which is what `launchctl managername` reports for an agent or a remote
# shell -- it fails with errSecInternalComponent and the bundle silently stays
# ad-hoc. This script used to hide that error; now it says so, because an
# unnoticed ad-hoc bundle is exactly what causes the endless keychain prompts.
APP="$1"
[ -d "$APP" ] || exit 0

ID="${AIREADER_CODESIGN_ID:-}"
if [ "$ID" = "-" ]; then
  exit 0
fi
if [ -z "$ID" ]; then
  ID=$(security find-identity -v -p codesigning 2>/dev/null \
        | grep -m1 'Developer ID Application' \
        | grep -oE '[0-9A-F]{40}')
fi
if [ -z "$ID" ]; then
  echo "[sign-dev] no Developer ID identity; leaving ad-hoc signature"
  exit 0
fi

ERR=$(codesign --force --sign "$ID" "$APP" 2>&1)
if [ $? -eq 0 ]; then
  echo "[sign-dev] signed $(basename "$APP") with $ID"
  codesign -d --requirements - "$APP" 2>&1 | sed 's/^/[sign-dev] /'
  exit 0
fi

echo "[sign-dev] WARNING: codesign failed, bundle is still ad-hoc signed."
echo "$ERR" | sed 's/^/[sign-dev]   /'
case "$ERR" in
  *errSecInternalComponent*)
    echo "[sign-dev]   The signing key is not reachable from a '$(launchctl managername 2>/dev/null)' session."
    echo "[sign-dev]   Re-run this from Terminal.app and click 'Always Allow', or grant"
    echo "[sign-dev]   codesign standing access once:"
    echo "[sign-dev]     security set-key-partition-list -S apple-tool:,apple:,codesign: \\"
    echo "[sign-dev]       -s -k <login-password> ~/Library/Keychains/login.keychain-db"
    ;;
esac
# Still exit 0: an unsigned dev build should not fail the build, only warn.
exit 0
