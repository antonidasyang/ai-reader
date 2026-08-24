#!/bin/zsh
# Open every dialog offscreen and fail on any QML warning.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" dialogs
"$OUT/dialogs"
