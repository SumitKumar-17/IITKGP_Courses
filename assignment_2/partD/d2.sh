#!/bin/bash
# Runs on the server. D.2: reclaim-batch tuning.
#
# 8 readers do point gets for 5 s (the generator's access pattern, per-call timed),
# one writer updates trades, and the RCU store (Part C) reclaims in one of three ways:
#   bounded --batch N     the store's own pass, at most N versions at a time
#   none                  never free
#   stw --trigger T       naive stop-the-world: when T versions wait, park all readers,
#                         free the whole list, release them
#
# Two scenarios:
#   steady  writer 50,000 puts/s, nothing pinning old versions (the D.1 setup).
#           Short reads never fall behind, so there is little for reclamation to do.
#   bursty  writer 300,000 puts/s, and a thread pins a snapshot for 50 ms of every
#           100 ms, like a join in progress. A backlog builds while it is held and
#           has to be drained afterwards, which is where the batch size matters.
set -euo pipefail
cd "$(dirname "$0")"
source lib.sh
QUIET_LOG=results/quiet.log
mkdir -p results
out=results/d2.txt
: > "$out"
B="taskset -c 0-15 build/bench_rcu --mode point --readers 8 --seconds 5"

run() {   # scenario label args...
  local sc=$1 label=$2; shift 2
  for rep in 1 2 3; do
    wait_quiet
    echo -n "scenario=$sc rep=$rep " >> "$out"
    $B --label "$label" "$@" >> "$out"
  done
}

steady="--writer-rate 50000"
bursty="--writer-rate 300000 --hold-ms 50 --hold-period 100"

for batch in 8 64 256 1024; do run steady "batch$batch" $steady --reclaim bounded --batch $batch; done
run steady none $steady --reclaim none
run steady stw4096 $steady --reclaim stw --trigger 4096

for batch in 1 8 64 256 1024 4096; do run bursty "batch$batch" $bursty --reclaim bounded --batch $batch; done
run bursty none $bursty --reclaim none
for trig in 4096 16384; do run bursty "stw$trig" $bursty --reclaim stw --trigger $trig; done

# one representative histogram per configuration, for the raw-data deliverable
mkdir -p results/hist
for batch in 8 1024; do
  $B --label "bursty_batch$batch" $bursty --reclaim bounded --batch $batch --hist results/hist/bursty_batch$batch.csv > /dev/null
done
$B --label bursty_stw16384 $bursty --reclaim stw --trigger 16384 --hist results/hist/bursty_stw16384.csv > /dev/null
$B --label bursty_none $bursty --reclaim none --hist results/hist/bursty_none.csv > /dev/null
echo D2DONE
