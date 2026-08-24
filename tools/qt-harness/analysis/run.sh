#!/bin/zsh
# Build the interpretation driver, make a fixture PDF, run the checks.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness

"${HERE:h}/build.sh" analysis

# One paper, long enough that the clusterer produces paragraphs the fake
# gateway can cite (it builds its answer out of the markers it is sent).
mk() {  # mk <name> <seed> <sections>
  python3 -c "
import random, sys
words=('alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu '
       'xi omicron sigma tau upsilon phi chi psi omega').split()
random.seed(int(sys.argv[2]))
out=[]
for p in range(int(sys.argv[3])):
    out.append('Section %d' % p)
    for s in range(6):
        out.append(' '.join(random.choice(words) for _ in range(26)) + '.')
    out.append('')
open(sys.argv[1],'w').write('\n'.join(out))
" "$OUT/$1.txt" "$2" "$3"
  cupsfilter -i text/plain -m application/pdf "$OUT/$1.txt" > "$OUT/$1.pdf" 2>/dev/null
}
# Different lengths → different paper ids.
mk analysis-a 7 5
mk analysis-b 8 4
mk analysis-c 9 3

PDF_A=$OUT/analysis-a.pdf PDF_B=$OUT/analysis-b.pdf PDF_C=$OUT/analysis-c.pdf \
  "$OUT/analysis"
