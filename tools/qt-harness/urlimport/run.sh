#!/bin/zsh
# Turning a pasted link into a PDF on disk.
#   URLIMPORT_LIVE=1 run.sh   also downloads one real paper from arXiv
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" urlimport
"$OUT/urlimport"
