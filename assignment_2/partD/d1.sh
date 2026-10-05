#!/bin/bash
# Runs on the server. D.1: point-get throughput and latency for every store at
# 1, 2, 4 and 8 readers, using the provided workload generator with its default
# parameters (100k trades, hot_every 100, 5 s) and the background writer from
# common/driver_main.h (50,000 puts/s). 3 repetitions, pinned to the 8 P-cores.
#
# single_thread has no synchronisation, so it cannot run with a writer. It is
# measured with one reader (the real single-thread case) and, for reference,
# with more read-only readers and no writer: the ceiling an unsynchronised
# store reaches when nothing ever writes.
set -euo pipefail
cd "$(dirname "$0")/.."
source partD/lib.sh
QUIET_LOG=partD/results/quiet.log
mkdir -p partD/results
out=partD/results/d1.csv
echo "config,readers,rep,ops,rps,p50_us,p99_us,p999_us" > "$out"
pin="taskset -c 0-15"

# Beside the result, keep user and system CPU seconds, wall seconds and the
# involuntary and voluntary context switches of each run. A lock that makes
# threads sleep shows up there, and so does a change in how it behaves.
echo "config readers rep user_s sys_s wall_s invol_cs vol_cs" > partD/results/d1_time.txt

run() {   # label binary readers rep [extra args]
  local label=$1 bin=$2 r=$3 rep=$4; shift 4
  /usr/bin/time -a -o partD/results/d1_time.txt -f "$label $r $rep %U %S %e %c %w" \
    $pin "$bin" --readers "$r" "$@" --csv | awk -F, -v l="$label" -v rep="$rep" '/^csv/ {printf "%s,%s,%s,%s,%s,%s,%s,%s\n", l,$3,rep,$4,$5,$6,$7,$8}' >> "$out"
}

for rep in 1 2 3; do
  for r in 1 2 4 8; do
    wait_quiet
    run global_lock partA/build/global_lock $r $rep
    run rwlock      partA/build/rwlock      $r $rep
    run rcu         partB/build/rcu         $r $rep
    run rcu_partC   partC/build/rcu         $r $rep
    run single_thread_nowriter partA/build/single_thread $r $rep
  done
  echo "rep $rep done"
done
echo D1DONE
