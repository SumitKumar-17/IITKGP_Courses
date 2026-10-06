#!/bin/bash
# Runs on the server. What does the write-ahead log cost, and what does it buy?
#
#   E1  the D.1 setup (8 readers, one writer at 50,000 puts/s) with each durability level:
#       reads must not change; the writer's commit latency and sustained rate show the cost.
#   E2  one committer flat out, 1, 4 and 16 keys per transaction, with 0 and 8 readers.
#   E3  group commit: Sync level, 1 to 32 committing threads, three group delays.
#
# The table is loaded first and is not part of the log, so these are steady-state numbers.
set -euo pipefail
cd "$(dirname "$0")/../partD"
source lib.sh
mkdir -p ../partE/results
QUIET_LOG=../partE/results/quiet.log
out=../partE/results/bench_wal.txt
: > "$out"
pin="taskset -c 0-15"
B=build/bench_wal
W=/home/t1000/docs_asgn2/partE/results/bench.wal

gcc -O2 ../tools/fsync_probe.c -o /tmp/fsync_probe

# The disk's fsync latency changes from one stretch of time to another (we saw about 220 us
# and about 1.5 ms for identical runs), so each run starts with the disk flushed and settled,
# and records what a quick fdatasync probe measured just before it.
run() {   # tag, then bench arguments
  local tag=$1; shift
  sync; sleep 2
  local probe; probe=$(/tmp/fsync_probe "$(dirname $W)" 200)
  echo -n "$tag probe_us=$probe " >> "$out"
  $pin $B --mode point --seconds 5 --wal-path $W "$@" >> "$out"
  rm -f $W
}

for rep in 1 2 3; do
  for mode in none async write sync; do
    wait_quiet
    w=(); [ $mode != none ] && w=(--wal $mode)
    run "part=E1 rep=$rep" --readers 8 --writer-rate 50000 --label "$mode" "${w[@]}"
  done
done

for rep in 1 2 3; do
  for readers in 0 8; do
    for mode in none async write sync; do
      for k in 1 4 16; do
        wait_quiet
        w=(); [ $mode != none ] && w=(--wal $mode)
        run "part=E2 rep=$rep txn=$k" --readers $readers --writer-rate -1 --writer-txn $k --label "$mode" "${w[@]}"
      done
    done
  done
done

for rep in 1 2; do
  for delay in 0 100 500; do
    for c in 1 2 4 8 16 32; do
      wait_quiet
      run "part=E3 rep=$rep delay=$delay" --readers 0 --writer-rate -1 --writer-txn 1 --committers $c \
          --wal sync --wal-delay $delay --label "sync"
    done
  done
done
echo E_BENCH_DONE
