#!/bin/zsh
# Links one harness driver against the app's own object files.
#
#   tools/qt-harness/build.sh papersync
#   tools/qt-harness/build.sh translation
#
# Every ai-reader object is reused as-is except three, so the code under test
# is what the app ships:
#
#   src/main.cpp.o           → the driver in <name>/main.cpp
#   src/AuthController.cpp.o → common/AuthStub.cpp (the real sign-in needs a
#                              browser round trip through CAS)
#   src/Settings.cpp.o       → a copy generated here with the two keychain
#                              functions emptied out. Settings::setApiKey
#                              writes to the login keychain under service
#                              "ai-reader" — the USER'S REAL API KEY — and a
#                              harness must never touch that. Everything else
#                              in Settings is the real code.
#
# Compile and link flags come out of build.ninja, so there is nothing to keep
# in sync with CMakeLists. Artifacts go to $BUILD/qt-harness, which is
# gitignored.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h}
B=${BUILD:-$REPO/build}
OUT=$B/qt-harness

DRIVER=${1:-}
if [[ -z $DRIVER || ! -f $HERE/$DRIVER/main.cpp ]]; then
  echo "usage: ${0:t} <driver>   (one of: $(cd $HERE && ls -d */ | grep -v common | tr -d '/' | tr '\n' ' '))" >&2
  exit 2
fi
if [[ ! -f $B/build.ninja ]]; then
  echo "no $B/build.ninja — configure and build the app first" >&2
  exit 1
fi
mkdir -p "$OUT"

# Pull one variable out of a ninja build block. Any app source will do for the
# compile flags.
pick() {
  awk -v key="$1" '/^build CMakeFiles\/ai-reader.dir\/src\/Settings.cpp.o:/{f=1}
                   f && $1==key {sub(/^[^=]*= /,""); print; exit}' "$B/build.ninja"
}
picklink() {
  awk -v key="$1" '/^build ai-reader(\.app\/Contents\/MacOS\/ai-reader)?:/{f=1}
                   f && $1==key {sub(/^[^=]*= /,""); print; exit}' "$B/build.ninja"
}
DEFINES=$(pick DEFINES)
FLAGS=$(pick FLAGS)
INCLUDES=$(pick INCLUDES)
LINK_FLAGS=$(picklink LINK_FLAGS)
LINK_LIBS=$(picklink LINK_LIBRARIES)
LINK_PATH=$(picklink LINK_PATH)

# Generate the keychain-free Settings rather than keeping a copy in the repo:
# a copy would rot the first time Settings.cpp changed, and this way the only
# difference from the shipping file is the two emptied bodies.
python3 - "$REPO/src/Settings.cpp" "$OUT/Settings.stub.cpp" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
for fn in ("void Settings::readApiKeyFromKeychain()",
           "void Settings::writeApiKeyToKeychain(const QString &value)"):
    i = s.index(fn)
    open_brace = s.index("{", i)
    depth, j = 0, open_brace
    while True:                      # brace-match the body
        if s[j] == "{": depth += 1
        elif s[j] == "}":
            depth -= 1
            if depth == 0: break
        j += 1
    s = s[:open_brace] + "{ /* keychain disabled in the harness */ }" + s[j + 1:]
open(dst, "w").write(s)
PY

# The driver the build itself uses. `xcrun -f clang++` resolves to a compiler
# that doesn't get the macOS SDK implicitly and fails on TargetConditionals.h.
CXX=${CXX:-/usr/bin/c++}

cd "$B"
compile() {  # compile <source> <object>
  eval "$CXX ${=DEFINES} ${=FLAGS} ${=INCLUDES} -I$HERE/common -I$REPO/src -c $1 -o $2"
}
compile "$HERE/$DRIVER/main.cpp"  "$OUT/$DRIVER-main.o"
compile "$HERE/common/AuthStub.cpp" "$OUT/AuthStub.o"
compile "$OUT/Settings.stub.cpp"    "$OUT/Settings.stub.o"

OBJS=()
for o in $(find CMakeFiles/ai-reader.dir -name '*.o' | sort); do
  case $o in
    */src/main.cpp.o|*/src/AuthController.cpp.o|*/src/Settings.cpp.o) continue ;;
  esac
  OBJS+=$o
done

eval "$CXX ${=FLAGS} ${=LINK_FLAGS} $OUT/$DRIVER-main.o $OUT/AuthStub.o \
  $OUT/Settings.stub.o ${OBJS} -o $OUT/$DRIVER ${=LINK_PATH} ${=LINK_LIBS}"
echo "built $OUT/$DRIVER"
