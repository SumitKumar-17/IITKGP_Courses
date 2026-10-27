#!/bin/bash
# Runs on the server. Plain throughput/latency sweep, no profiler attached.
# 3 repetitions of each configuration, pinned to the 8 P-cores (16 threads).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p results
out=results/scaling.txt
: > "$out"
for rep in 1 2 3; do
  taskset -c 0-15 build/single_thread --readers 1 | sed "s/^/rep$rep /" >> "$out"
  for prog in global_lock rwlock; do
    for r in 1 2 4 8; do
      taskset -c 0-15 build/$prog --readers $r | sed "s/^/rep$rep /" >> "$out"
    done
  done
done
cat "$out"
