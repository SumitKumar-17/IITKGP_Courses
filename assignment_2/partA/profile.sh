#!/bin/bash
# Runs on the server. perf stat and perf record for global_lock and rwlock at
# 1, 2, 4 and 8 readers, attached to the running process for a fixed window so
# the load phase and the final latency sort stay out of the numbers.
#
# usage: ./profile.sh [seconds-of-window]
set -euo pipefail
cd "$(dirname "$0")"

FG=../FlameGraph
OUT=results/profile
CPUS=0-15
WINDOW=${1:-3}
EVENTS=task-clock,cycles,instructions,cache-references,cache-misses,L1-dcache-load-misses,LLC-load-misses,context-switches

mkdir -p "$OUT"
for prog in global_lock rwlock; do
  for r in 1 2 4 8; do
    tag=${prog}_r$r
    taskset -c $CPUS build/$prog --readers $r --seconds $((2 * WINDOW + 5)) > "$OUT/$tag.run.txt" &
    pid=$!
    sleep 2

    perf stat -e $EVENTS -p $pid -o "$OUT/$tag.stat.txt" -- sleep $WINDOW
    perf record -F 997 --call-graph dwarf,16384 -p $pid -o /tmp/$tag.perf.data -- sleep $WINDOW > /dev/null 2>&1
    wait $pid

    perf script -i /tmp/$tag.perf.data 2>/dev/null | $FG/stackcollapse-perf.pl > "$OUT/$tag.folded"
    $FG/flamegraph.pl --title "$prog, $r reader thread(s)" --width 1400 "$OUT/$tag.folded" > "$OUT/$tag.svg"
    rm -f /tmp/$tag.perf.data
    echo "done $tag"
  done
done
