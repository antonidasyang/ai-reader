#!/bin/zsh
# Measure what a splitter drag costs in the panes that make it expensive.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" panes
python3 -c "
import random, sys
words=('alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu '
       'xi omicron sigma tau upsilon phi chi psi omega').split()
random.seed(3)
out=[]
for p in range(14):
    out.append('Section %d' % p)
    for s in range(8):
        out.append(' '.join(random.choice(words) for _ in range(28)) + '.')
    out.append('')
open(sys.argv[1],'w').write('\n'.join(out))
" "$OUT/panes-fixture.txt"
cupsfilter -i text/plain -m application/pdf "$OUT/panes-fixture.txt" \
  > "$OUT/panes-fixture.pdf" 2>/dev/null
PDF_A=$OUT/panes-fixture.pdf "$OUT/panes"
