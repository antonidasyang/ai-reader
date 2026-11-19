#!/usr/bin/env bash
# Install the newest HAP from build-ohos/ on the connected HarmonyOS device or
# emulator (hdc), launch it and follow the app's log.
#
#   tools/ohos/run.sh           install over the existing app (keeps its data)
#   tools/ohos/run.sh --fresh   uninstall first: a true first launch
#   tools/ohos/run.sh <x.hap>   install that HAP instead
#
# Start the emulator from DevEco Studio's Device Manager (or
# "$DEVECO/tools/emulator/Emulator -hvd 'MateBook Pro'") first. The emulator
# accepts unsigned HAPs; real devices need a signed one.
set -euo pipefail
source "$(dirname "$0")/env.sh"

fresh=0 hap=
for a in "$@"; do
    case "$a" in
        --fresh) fresh=1 ;;
        *.hap) hap=$a ;;
        *) die "unknown argument $a" ;;
    esac
done
[ -n "$hap" ] || hap=$(ls -t "$BUILD_DIR"/AiReader-*.hap 2>/dev/null | head -1)
[ -f "$hap" ] || die "no HAP found; run tools/ohos/build.sh"
[ -n "$("$HDC" list targets | grep -v Empty)" ] || die "no device (start the emulator)"

"$HDC" shell aa force-stop "$BUNDLE_NAME" >/dev/null 2>&1 || true
[ $fresh = 1 ] && "$HDC" uninstall "$BUNDLE_NAME" >/dev/null 2>&1 || true
echo "Installing $(basename "$hap")"
"$HDC" install -r "$hap" | grep -E "msg:|error" || true
"$HDC" shell hilog -r >/dev/null
"$HDC" shell aa start -a QAbility -b "$BUNDLE_NAME"
echo "--- app log (Ctrl-C to stop) ---"
exec "$HDC" shell hilog -T AIReader
