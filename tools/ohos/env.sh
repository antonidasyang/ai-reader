# Shared settings for the HarmonyOS scripts in this directory. Sourced, not run.
# Every value can be overridden from the environment.

AIREADER_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Qt 6.12.x for HarmonyOS plus the matching macOS host Qt, laid out the way
# the Qt online installer does it: $QT_ROOT/{macos,harmonyos}.
QT_VERSION="${QT_VERSION:-6.12.1}"
QT_ROOT="${QT_ROOT:-$HOME/Qt/$QT_VERSION}"
QT_HOST="$QT_ROOT/macos"
QT_OHOS="$QT_ROOT/harmonyos"
# Where build-qtpdf.sh keeps the qtwebengine checkout and the host gn.
QT_SRC_ROOT="${QT_SRC_ROOT:-$HOME/Qt/src}"

# DevEco Studio ships the HarmonyOS SDK, hvigor, node, hdc and a JDK.
DEVECO="${DEVECO:-/Applications/DevEco-Studio.app/Contents}"
export DEVECO_SDK_HOME="${DEVECO_SDK_HOME:-$DEVECO/sdk}"
OHOS_SDK_ROOT="${OHOS_SDK_ROOT:-$DEVECO_SDK_HOME/default/openharmony}"
HVIGORW="${HVIGORW:-$DEVECO/tools/hvigor/bin/hvigorw}"
HDC="${HDC:-$OHOS_SDK_ROOT/toolchains/hdc}"
export PATH="$DEVECO/tools/node/bin:$PATH"
export JAVA_HOME="${JAVA_HOME:-$DEVECO/jbr/Contents/Home}"

BUILD_DIR="${BUILD_DIR:-$AIREADER_ROOT/build-ohos}"
BUNDLE_NAME="${BUNDLE_NAME:-com.d2ssoft.aireader}"

die() { echo "error: $*" >&2; exit 1; }
