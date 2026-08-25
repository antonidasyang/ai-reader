#!/bin/zsh
# The page the browser is left on after signing in.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" login
"$OUT/login"
