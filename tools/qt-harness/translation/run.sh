#!/bin/zsh
# Build the translation-cancel driver, make a fixture PDF, run the checks.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness

"${HERE:h}/build.sh" translation

python3 -c "
import random, sys
words='alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron'.split()
random.seed(11)
out=[]
for p in range(10):
    out.append('Section %d' % p)
    for s in range(5):
        out.append(' '.join(random.choice(words) for _ in range(24)) + '.')
    out.append('')
open(sys.argv[1],'w').write('\n'.join(out))
" "$OUT/t.txt"
cupsfilter -i text/plain -m application/pdf "$OUT/t.txt" > "$OUT/t.pdf" 2>/dev/null

PDF_A=$OUT/t.pdf "$OUT/translation"
