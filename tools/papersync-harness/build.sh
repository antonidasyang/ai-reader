#!/bin/zsh
# Links the harness against the app's own object files.
#
# Every ai-reader object except main.cpp.o and AuthController.cpp.o is reused
# as-is, so the code under test is exactly what the app ships; main.cpp is
# replaced by the test driver and AuthController.cpp by a stub (the real one
# can only sign in through a browser). The compile and link flags are read out
# of build.ninja, so this needs no CMake changes and cannot drift from the
# build. Artifacts land in $BUILD/papersync-harness, which is gitignored.
#
# Usage: tools/papersync-harness/build.sh   (after cmake --build build)
#        BUILD=/path/to/other/build tools/papersync-harness/build.sh
set -e
HERE=${0:A:h}
REPO=${HERE:h:h}
B=${BUILD:-$REPO/build}
OUT=$B/papersync-harness

if [[ ! -f $B/build.ninja ]]; then
  echo "no $B/build.ninja — configure and build the app first" >&2
  exit 1
fi
mkdir -p "$OUT"

# Pull one variable out of a ninja build block. Any app source will do for the
# compile flags; PaperSyncService is the one this harness is about.
pick() {
  awk -v key="$1" '/^build CMakeFiles\/ai-reader.dir\/src\/PaperSyncService.cpp.o:/{f=1}
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

# The driver the build itself uses. `xcrun -f clang++` resolves to a compiler
# that doesn't get the macOS SDK implicitly and fails on TargetConditionals.h.
CXX=${CXX:-/usr/bin/c++}

cd "$B"
for f in main AuthStub; do
  eval "$CXX ${=DEFINES} ${=FLAGS} ${=INCLUDES} -I$HERE -I$REPO/src -c $HERE/$f.cpp -o $OUT/$f.o"
done

OBJS=()
for o in $(find CMakeFiles/ai-reader.dir -name '*.o' | sort); do
  case $o in
    */src/main.cpp.o|*/src/AuthController.cpp.o) continue ;;
  esac
  OBJS+=$o
done

eval "$CXX ${=FLAGS} ${=LINK_FLAGS} $OUT/main.o $OUT/AuthStub.o ${OBJS} \
  -o $OUT/papersync-harness ${=LINK_PATH} ${=LINK_LIBS}"
echo "built $OUT/papersync-harness"
