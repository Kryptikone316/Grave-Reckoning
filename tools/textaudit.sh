#!/bin/sh
# Renders every screen and state of the MIPS build under qemu with --audit and prints any text that
# spills out of the panel, button, caption or cell it sits in (or off the screen).
# usage: tools/textaudit.sh            (prints nothing but a summary when everything fits)
DIR=$(cd "$(dirname "$0")/.." && pwd)
LIST=$(mktemp)
{
  echo "--screen title --cursor 0"; echo "--screen title --cursor 1"; echo "--screen title --cursor 2"
  echo "--screen main"; echo "--screen play"; echo "--screen help"
  for q in 0 1 2 3 4 5 6 7; do echo "--screen exit --cursor $q"; done
  echo "--screen main --graves 5"; echo "--screen stage --diff 1 --stage 14 --graves 12"; echo "--screen stage --diff 2 --stage 30 --graves 30"; echo "--screen scores --diff 1"
  for h in 0 1 2 3; do echo "--screen game --stage 1 --ticks 5 --hint $h"; done
  echo "--screen title --cursor 0 --frame 60"; echo "--screen title --cursor 2 --frame 200"
  for d in 0 1 2; do echo "--screen stage --diff $d --stage 1"; echo "--screen scores --diff $d"; done
  for t in 0 1 2; do echo "--screen options --cursor $t"; done
  for c in 1 2 3 4 5 6; do echo "--screen card --stage $c"; done
  for p in intro ch1 ch2 ch2b ch3 ch3b ch4 ch4b ch5 ch5b ch6 end end2 end2b end3 end4 epilogue; do
    for k in 0 1 2 3; do echo "--screen story --page $p --ticks 9 --cursor $k"; done   # --cursor: which page of that name
  done
  echo "--screen journal --graves 30 --lanterns 300 --cursor 0"; echo "--screen journal --graves 0 --cursor 3"; echo "--screen journal --graves 30 --lanterns 300 --cursor 17"; echo "--screen journal --graves 30 --lanterns 300 --cursor 34"
  for c in 0 5 10; do echo "--screen journal --page earlier --graves 30 --lanterns 300 --deeds 99 --cursor $c"; echo "--screen journal --page earlier --graves 0 --cursor $c"; done
  echo "--screen journalpage --graves 0 --cursor 1"; echo "--screen journalpage --graves 30 --lanterns 999 --deeds 99 --cursor 1"
  for e in $(seq 0 47); do echo "--screen journalpage --graves 30 --lanterns 300 --deeds 99 --cursor $e"; echo "--screen journalpage --graves 10 --lanterns 20 --cursor $e"; done
  for d in 0 1 2; do for s in 1 8 13 18 23 30; do echo "--screen game --diff $d --stage $s --ticks 40"; done; done
  for st in 0 3 5 6 7; do echo "--screen game --diff 2 --stage 30 --ticks 10 --state $st"; done
  echo "--screen game --diff 2 --stage 30 --ticks 10 --state 5 --news"; echo "--screen game --diff 1 --stage 12 --ticks 10 --state 2 --walled"
  echo "--screen pause --diff 2 --stage 30 --ticks 20"
} > "$LIST"
n=$(wc -l < "$LIST")
export DIR
cat "$LIST" | xargs -P 8 -I{} sh -c '"$DIR/tools/shot.sh" /tmp/audit-$$.bmp {} --audit 2>&1 | grep "^TEXT-" | sed "s|^|[{}] |"' | sort -u
echo "text audit: $n screens checked"
rm -f "$LIST"
