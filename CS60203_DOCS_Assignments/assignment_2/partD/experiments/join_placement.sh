#!/bin/bash
# Runs on the server. Why was a join no slower, even faster, with the writer running?
# Joins (1000 outer rows) with and without the 50,000 puts/s writer, at 4 readers (fit on
# separate physical cores) and 8 readers (some must share a core), CPUs 0-15 or one
# logical CPU per physical core plus its spare sibling for the writer.
cd "$(dirname "$0")/.."
echo "readers cpuset writer joins/s(k) x3"
for cfg in "4|0-15" "4|0,2,4,6,8,10,12,14" "8|0-15" "8|0,2,4,6,8,10,12,14,1,3,5,7"; do
  r=${cfg%%|*}; set=${cfg##*|}
  for w in 0 50000; do
    line="$r $set $w"
    for i in 1 2 3; do
      v=$(taskset -c $set build/bench_rcu --mode join --readers $r --seconds 4 --outer 1000 --writer-rate $w | grep -o "ops_per_s=[0-9]*" | head -1 | cut -d= -f2)
      line+=" $((v/1000))"
    done
    echo "$line"
  done
done
