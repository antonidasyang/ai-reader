#!/bin/zsh
# Build the interpretation driver, make a fixture PDF, run the checks.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness

"${HERE:h}/build.sh" analysis

# One paper, long enough that the clusterer produces paragraphs the fake
# gateway can cite (it builds its answer out of the markers it is sent).
python3 -c "
import random, sys
words=('alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu '
       'xi omicron sigma tau upsilon phi chi psi omega').split()
random.seed(7)
out=[]
for p in range(5):
    out.append('Section %d' % p)
    for s in range(6):
        out.append(' '.join(random.choice(words) for _ in range(26)) + '.')
    out.append('')
open(sys.argv[1],'w').write('\n'.join(out))
" "$OUT/analysis-fixture.txt"
cupsfilter -i text/plain -m application/pdf "$OUT/analysis-fixture.txt" \
  > "$OUT/analysis-fixture.pdf" 2>/dev/null

PDF_A=$OUT/analysis-fixture.pdf "$OUT/analysis"
