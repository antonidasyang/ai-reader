#!/bin/zsh
# Time a paper switch against two PDFs that are slow for PDFium to draw.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" switch
[[ -f $OUT/switch-a.pdf ]] || python3 "$HERE/heavy-pdf.py" "$OUT/switch-a.pdf" 6 40000 1
[[ -f $OUT/switch-b.pdf ]] || python3 "$HERE/heavy-pdf.py" "$OUT/switch-b.pdf" 6 40000 2
PDF_A=$OUT/switch-a.pdf PDF_B=$OUT/switch-b.pdf "$OUT/switch"
