#!/bin/bash
# Runs on the server. D.3: joins, multi-key commits, copy-on-write cost.
set -euo pipefail
cd "$(dirname "$0")"
source lib.sh
QUIET_LOG=results/quiet.log
mkdir -p results
out=results/d3.txt
: > "$out"
pin="taskset -c 0-15"

# (a) index-nested-loop join, 8 readers, writer on (50,000 puts/s on trades) vs off.
for outer in 1000 100000; do
  for rep in 1 2 3; do
    for w in 50000 0; do
      wait_quiet
      echo -n "part=join outer=$outer writer=$w rep=$rep " >> "$out"
      $pin build/bench_rcu --mode join --readers 8 --seconds 5 --outer $outer --writer-rate $w --label join >> "$out"
    done
  done
done

# (b) commit throughput vs transaction size, writer flat out; with and without readers.
for readers in 0 8; do
  for k in 1 4 16; do
    for rep in 1 2 3; do
      wait_quiet
      echo -n "part=commit store=rcu readers=$readers txn=$k rep=$rep " >> "$out"
      $pin build/bench_rcu --mode point --readers $readers --seconds 5 --writer-rate -1 --writer-txn $k --label commit >> "$out"
      echo -n "part=commit store=rwlock readers=$readers txn=$k rep=$rep " >> "$out"
      $pin build/bench_rwlock --mode point --readers $readers --seconds 5 --writer-rate -1 --writer-txn $k --label commit >> "$out"
    done
  done
done

# (c) one big transaction: copy-on-write cost against a store that updates in place.
echo "part=bigtxn store=rcu" >> "$out"
$pin build/bench_rcu --mode bigtxn --reps 7 >> "$out"
echo "part=bigtxn store=rwlock" >> "$out"
$pin build/bench_rwlock --mode bigtxn --reps 7 >> "$out"
echo D3DONE
