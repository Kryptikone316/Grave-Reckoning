#!/bin/bash
# Beam-bot sweep over all 90 stage and difficulty pairs in parallel; prints one table when done.
# usage: tools/simall.sh <runs> <outfile> [extra simtest args]
DIR=$(cd "$(dirname "$0")/.." && pwd)
SIM=${SIM:-$DIR/build/sim/simtest}
runs=${1:-4}; out=${2:-$DIR/build/sim/out/all.txt}; shift 2
EXTRA="${*:---width 20 --depth 10 --ticks 14400}"
{ for d in 0 1 2; do for st in $(seq 1 30); do echo "$d $st"; done; done; } | \
  xargs -P 8 -L 1 sh -c "'$SIM' bot --diff \$0 --stage \$1 --runs $runs --seed ${SEED:-100} $EXTRA | tail -1" | \
  sort -k1,1 -k2,2n > "$out.tmp"
{ echo "diff    stage clear%  deaths  seconds  max cats gameover | walked caught  trap  yarn stall cleared-waves"
  awk '{print}' "$out.tmp" | sort -s -k1,1r -k2,2n | awk '$1=="Easy"'; awk '$1=="Normal"' "$out.tmp" | sort -k2,2n; awk '$1=="Hard"' "$out.tmp" | sort -k2,2n; } > "$out"
rm -f "$out.tmp"
