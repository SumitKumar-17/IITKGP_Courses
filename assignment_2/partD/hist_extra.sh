#!/bin/bash
# Runs on the server. The raw latency histograms behind D.1 and D.3 that d1.sh and d3.sh do
# not keep (the generator reports only percentiles), plus the single-thread-with-a-writer
# configuration of D.1.
set -euo pipefail
cd "$(dirname "$0")"
source lib.sh
QUIET_LOG=results/quiet.log
mkdir -p results/hist
pin="taskset -c 0-15"
out=results/d1_single_inline.txt
: > "$out"

# D.1: one histogram per store at 8 readers (the single-thread store has no writer there).
for store in single global rwlock; do
  wait_quiet
  w=(--writer-rate 50000); [ $store = single ] && w=(--writer-rate 0)
  $pin build/bench_$store --mode point --readers 8 --seconds 5 "${w[@]}" --label d1_$store --hist results/hist/d1_${store}_r8.csv > /dev/null
done
wait_quiet
$pin build/bench_rcu --mode point --readers 8 --seconds 5 --label d1_rcu --hist results/hist/d1_rcu_r8.csv > /dev/null

# D.1: a single thread that does the reads and the writes itself, writer rate 50,000 puts/s.
for rep in 1 2 3; do
  wait_quiet
  echo -n "rep=$rep " >> "$out"
  $pin build/bench_single --mode point --readers 1 --seconds 5 --writer-rate 0 --inline-writer-rate 50000 --label single_inline >> "$out"
done

# D.1: the same single thread serving 2, 4 and 8 clients in turn (it cannot go faster than one thread).
out2=results/d1_single_clients.txt
: > "$out2"
for clients in 2 4 8; do
  for rep in 1 2 3; do
    wait_quiet
    echo -n "clients=$clients rep=$rep " >> "$out2"
    $pin build/bench_single --mode point --readers 1 --seconds 5 --writer-rate 0 --inline-writer-rate 50000 --clients $clients --label single_clients >> "$out2"
  done
done

# D.3: the join histograms, writer on and paused.
for w in 50000 0; do
  wait_quiet
  $pin build/bench_rcu --mode join --readers 8 --seconds 5 --outer 1000 --writer-rate $w --label join_w$w --hist results/hist/d3_join1000_writer$w.csv > /dev/null
done
echo HISTDONE
