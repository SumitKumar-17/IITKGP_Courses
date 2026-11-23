#!/bin/bash
# Runs on the server. Sweep of design_bench over designs, shapes, table sizes and
# writer rates, 3 repetitions each.
set -euo pipefail
cd "$(dirname "$0")"
g++ -O2 -std=c++17 -pthread design_bench.cpp -o /tmp/design_bench
B="taskset -c 0-15 /tmp/design_bench"
out=../results/design_sweep.txt
mkdir -p ../results
: > "$out"
for keys in 100000 1000000; do
  segs="512 1024 2048 4096"; [ $keys = 1000000 ] && segs="4096 8192 16384 32768"
  for rate in 50000 0; do
    echo "# keys $keys writer rate $rate (0 = flat out)" >> "$out"
    for rep in 1 2 3; do
      for n in $segs; do
        $B dirseg-inline $n $keys $rate >> "$out"
        $B dirseg-ptr $n $keys $rate >> "$out"
      done
      for shape in "4 8" "5 8" "6 8" "6 16" "6 32" "8 16" "8 32"; do
        set -- $shape
        $B trie $1 $keys $rate $2 >> "$out"
      done
    done
  done
done
echo SWEEPDONE
