#!/bin/zsh
# The one queue: what starts, what is refused, what waits, what is never lost.
# build.sh brings the app's own objects up to date before it links, so this
# always runs against the current code, never yesterday's.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" tasks
"$OUT/tasks"
