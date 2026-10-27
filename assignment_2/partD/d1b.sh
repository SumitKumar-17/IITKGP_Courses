#!/bin/bash
# Runs on the server. D.1 again with the histogram harness (bench.cpp) instead of the
# generator's per-call vectors: same four stores, same access pattern, same 50,000
# puts/s background writer, 1/2/4/8 readers, 3 repetitions. The point is to see what
# the stores do when the measurement itself costs less and shares nothing.
set -euo pipefail
cd "$(dirname "$0")"
source lib.sh
QUIET_LOG=results/quiet.log
out=results/d1b.txt
: > "$out"
pin="taskset -c 0-15"
for rep in 1 2 3; do
  for r in 1 2 4 8; do
    wait_quiet
    echo -n "store=single rep=$rep " >> "$out"; $pin build/bench_single --mode point --readers $r --seconds 5 --writer-rate 0 --label single >> "$out"
    echo -n "store=global_lock rep=$rep " >> "$out"; $pin build/bench_global --mode point --readers $r --seconds 5 --label global_lock >> "$out"
    echo -n "store=rwlock rep=$rep " >> "$out"; $pin build/bench_rwlock --mode point --readers $r --seconds 5 --label rwlock >> "$out"
    echo -n "store=rcu rep=$rep " >> "$out"; $pin build/bench_rcu --mode point --readers $r --seconds 5 --label rcu >> "$out"
  done
done
echo D1BDONE
