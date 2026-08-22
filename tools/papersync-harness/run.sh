#!/bin/zsh
# Build the harness, make the two fixture PDFs, run the checks.
# Takes about a minute: several assertions wait out the 15 s publish throttle.
set -e
HERE=${0:A:h}
REPO=${HERE:h:h}
OUT=${BUILD:-$REPO/build}/papersync-harness

"$HERE/build.sh"

# Two PDFs with real extractable text, of different lengths so they hash to
# different paper ids. cupsfilter ships with macOS.
mk() {  # mk <path> <seed> <sections>
  python3 -c "
import random, sys
words='alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau upsilon'.split()
random.seed(int(sys.argv[2]))
out=[]
for p in range(int(sys.argv[3])):
    out.append('Section %d' % p)
    for s in range(5):
        out.append(' '.join(random.choice(words) for _ in range(26)) + '.')
    out.append('')
open(sys.argv[1],'w').write('\n'.join(out))
" "$OUT/$1.txt" "$2" "$3"
  cupsfilter -i text/plain -m application/pdf "$OUT/$1.txt" > "$OUT/$1.pdf" 2>/dev/null
}
mk a 7 12
mk b 99 5

PDF_A=$OUT/a.pdf PDF_B=$OUT/b.pdf "$OUT/papersync-harness"
