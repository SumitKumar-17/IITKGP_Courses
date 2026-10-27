#!/bin/bash
# Runs on the server. perf stat and a flame graph for the RCU store (Part B build)
# at 1 and 8 readers, attached to the running process for a fixed window, the same
# way partA/profile.sh does it. Needs kernel.kptr_restrict=0 to name kernel frames.
set -euo pipefail
cd "$(dirname "$0")/.."
FG=FlameGraph
OUT=partD/results/profile
WINDOW=3
EVENTS=task-clock,cycles,instructions,cache-references,cache-misses,L1-dcache-load-misses,LLC-load-misses,context-switches
mkdir -p "$OUT"
for r in 1 8; do
  tag=rcu_r$r
  taskset -c 0-15 partB/build/rcu --readers $r --seconds $((2 * WINDOW + 5)) > "$OUT/$tag.run.txt" &
  pid=$!
  sleep 2
  perf stat -e $EVENTS -p $pid -o "$OUT/$tag.stat.txt" -- sleep $WINDOW
  perf record -F 997 --call-graph dwarf,16384 -p $pid -o /tmp/$tag.perf.data -- sleep $WINDOW > /dev/null 2>&1
  wait $pid
  perf script -i /tmp/$tag.perf.data 2>/dev/null | $FG/stackcollapse-perf.pl > "$OUT/$tag.folded"
  $FG/flamegraph.pl --title "rcu (Part B), $r reader thread(s)" --width 1400 "$OUT/$tag.folded" > "$OUT/$tag.svg"
  rm -f /tmp/$tag.perf.data
  google-chrome --headless=new --no-sandbox --disable-gpu --hide-scrollbars --window-size=1420,700 \
    --screenshot="$OUT/$tag.png" "file://$PWD/$OUT/$tag.svg" > /dev/null 2>&1 || true
  echo "done $tag"
done
