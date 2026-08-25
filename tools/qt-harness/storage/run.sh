#!/bin/zsh
# The storage-identity rename, with a user's data in the way.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" storage
"$OUT/storage"
