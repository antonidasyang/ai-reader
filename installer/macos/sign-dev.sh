#!/bin/sh
# Sign the locally-built dev ai-reader.app with a Developer ID (if one is in the
# keychain) so macOS recognises the app across rebuilds and the keychain
# "Always Allow" choice persists. CMake's default build only ad-hoc signs, and
# ad-hoc signatures change every build -- which re-triggers the keychain prompt
# every launch. No-op (leaves the ad-hoc signature) when no identity is found.
#
# Override the identity with AIREADER_CODESIGN_ID=<name-or-hash>; set it to "-"
# to skip signing entirely.
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

if codesign --force --sign "$ID" "$APP" >/dev/null 2>&1; then
  echo "[sign-dev] signed $(basename "$APP") with $ID"
else
  echo "[sign-dev] codesign failed (continuing with whatever signature exists)"
fi
exit 0
