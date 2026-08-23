#!/bin/zsh
# Build the translation-cancel driver, make a fixture PDF, run the checks.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness

"${HERE:h}/build.sh" translation

# Two papers, so the "keep translating after a tab switch" checks have
# somewhere to switch to. Different lengths → different paper ids.
mk() {  # mk <name> <seed> <sections>
  python3 -c "
import random, sys
words='alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron'.split()
random.seed(int(sys.argv[2]))
out=[]
for p in range(int(sys.argv[3])):
    out.append('Section %d' % p)
    for s in range(5):
        out.append(' '.join(random.choice(words) for _ in range(24)) + '.')
    out.append('')
open(sys.argv[1],'w').write('\n'.join(out))
" "$OUT/$1.txt" "$2" "$3"
  cupsfilter -i text/plain -m application/pdf "$OUT/$1.txt" > "$OUT/$1.pdf" 2>/dev/null
}
mk t 11 4
mk u 12 3

PDF_A=$OUT/t.pdf PDF_B=$OUT/u.pdf "$OUT/translation"
