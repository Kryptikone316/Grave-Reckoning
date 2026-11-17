#!/bin/bash
# Run the bot on every stage of one difficulty (or a stage list) in parallel.
# usage: tools/simbench.sh <diff 0|1|2> "<stage list>" <runs> [seed]
# e.g.   tools/simbench.sh 0 "1 2 3 4 5 6" 6
DIR=$(cd "$(dirname "$0")/.." && pwd)
SIM="$DIR/build/sim/simtest"
diff=$1; stages=${2:-"$(seq -s' ' 1 20)"}; runs=${3:-6}; seed=${4:-100}
echo "diff    stage clear%  deaths  seconds  max cats gameover | walked caught  trap  yarn stall cleared-waves"
for st in $stages; do echo "$st"; done | xargs -P 8 -I{} sh -c \
  "'$SIM' bot --diff $diff --stage {} --runs $runs --seed $seed $EXTRA | tail -1" | sort -k2 -n
