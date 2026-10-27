#!/bin/bash
# Runs on the server. Epoch-based vs hazard-pointer reads on the same store.
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -pthread -I. -I../common rcu.cpp bench/kvstore_hp.cpp ../common/workload_gen.cpp -o build/rcu_hp
mkdir -p results
out=results/schemes.txt
: > "$out"
for rep in 1 2 3; do
  for r in 1 2 4 8; do
    taskset -c 0-15 build/rcu --readers $r | sed "s/^/rep$rep epoch  /" >> "$out"
    taskset -c 0-15 build/rcu_hp --readers $r | sed "s/^rcu/rcu_hp/;s/^/rep$rep hazard /" >> "$out"
  done
done
cat "$out"
