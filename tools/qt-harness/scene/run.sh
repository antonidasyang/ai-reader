#!/bin/zsh
# Time the moments a big project made slow: a paper switch, a close reading
# landing part by part, another paper's interpretation being stored.
#
#   PDFS=a.pdf:b.pdf tools/qt-harness/scene/run.sh   # two real papers instead
set -e
HERE=${0:A:h}
REPO=${HERE:h:h:h}
OUT=${BUILD:-$REPO/build}/qt-harness
"${HERE:h}/build.sh" scene
mk() {  # mk <name> <seed> <sections>
  python3 -c "
import random, sys
words=('alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu '
       'xi omicron sigma tau upsilon phi chi psi omega').split()
random.seed(int(sys.argv[2]))
out=[]
for p in range(int(sys.argv[3])):
    out.append('Section %d' % p)
    for s in range(8):
        out.append(' '.join(random.choice(words) for _ in range(28)) + '.')
    out.append('')
open(sys.argv[1],'w').write('\n'.join(out))
" "$OUT/$1.txt" "$2" "$3"
  cupsfilter -i text/plain -m application/pdf "$OUT/$1.txt" > "$OUT/$1.pdf" 2>/dev/null
}
if [[ -z $PDFS ]]; then
  [[ -f $OUT/scene-a.pdf ]] || mk scene-a 11 14
  [[ -f $OUT/scene-b.pdf ]] || mk scene-b 12 9
  PDFS=$OUT/scene-a.pdf:$OUT/scene-b.pdf
fi
PDFS=$PDFS "$OUT/scene"
