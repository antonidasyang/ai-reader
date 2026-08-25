#!/bin/zsh
# Saved pane layouts: names, widths as a share of the window, and the trip
# out to the account and back.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" layout
"$OUT/layout"
