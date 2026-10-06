#!/bin/bash
# Runs on the server. Why was a 1,000-row join about 10% faster with the writer running?
# Guess: the writer's copy-on-write allocates the nodes it touches back to back, so after enough
# writes the rows a join re-reads sit closer together in memory than they did after the load.
# Test: join with the writer PAUSED, on a table that was rewritten by 2,000,000 puts beforehand.
# If the guess is right it should be about as fast as with the writer running, and faster than a
# table that was only loaded.
cd "$(dirname "$0")/.."
source lib.sh
QUIET_LOG=results/quiet.log
out=results/join_locality.txt
: > "$out"
pin="taskset -c 0-15"
run() {   # tag, args
  local tag=$1; shift
  wait_quiet
  echo -n "$tag " >> "$out"
  $pin build/bench_rcu --mode join --readers 8 --seconds 5 --outer 1000 "$@" | grep -o "ops_per_s=[0-9]*" | head -1 >> "$out"
}
for rep in 1 2 3; do
  run "rep=$rep variant=paused_fresh"        --writer-rate 0
  run "rep=$rep variant=paused_prewritten"   --writer-rate 0 --prewrite 2000000
  run "rep=$rep variant=writer_on_fresh"     --writer-rate 50000
  run "rep=$rep variant=writer_on_prewritten" --writer-rate 50000 --prewrite 2000000
done
echo LOCALITYDONE
