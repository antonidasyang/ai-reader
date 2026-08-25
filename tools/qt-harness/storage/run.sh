#!/bin/zsh
# Where the app keeps things: its directories, and its one JSON settings file.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" storage
"$OUT/storage"
