#!/bin/bash
# Repeats the Part D.1 comparison on any Linux machine with g++ (C++17) and make.
# Copy the whole folder there, then from its top level:
#
#   tools/second_machine.sh            about 6 minutes: 3 runs of 5 s per point
#   tools/second_machine.sh quick      about 2 minutes: 1 run of 3 s per point
#
# What it does: builds partD/bench.cpp against the Part A stores (no sync, global lock, rwlock) and
# the final RCU store (Part C), runs each at 1, 2, 4 and 8 readers with the 50,000 puts/s background
# writer (none for the unsynchronised store), and prints the median reads per second and the tail
# latencies. Everything is also saved to second_machine_results.txt.
#
# Optional: CPUS=0-7 tools/second_machine.sh   to restrict the cores used (default: all of them).
# Close other programs first. Fewer than 8 hardware threads is fine: the 8-reader rows then share cores.
set -u
cd "$(dirname "$0")/.."
reps=3; secs=5
[ "${1:-}" = quick ] && { reps=1; secs=3; }

command -v g++ >/dev/null || { echo "g++ not found"; exit 1; }
make -C partD -s build/bench_rcu build/bench_rwlock build/bench_global build/bench_single 2>&1 | grep -E "error"
for b in rcu rwlock global single; do
  [ -x partD/build/bench_$b ] || { echo "build failed: partD/build/bench_$b is missing"; exit 1; }
done

out=second_machine_results.txt
{
  echo "machine:  $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
  echo "threads:  $(nproc)   kernel: $(uname -r)   date: $(date -u +%F)"
  echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
  echo "runs:     $reps x ${secs}s per point, median shown"
  echo
} | tee "$out"

pin=""
if command -v taskset >/dev/null; then pin="taskset -c ${CPUS:-0-$(($(nproc) - 1))}"; fi

# One key=value field of a result line, matched exactly (ops_per_s must not match writer_ops_per_s).
field() { tr ' ' '\n' <<< "$1" | grep "^$2=" | head -1 | cut -d= -f2; }

printf "%-12s %8s %14s %10s %10s\n" store readers "reads/s (M)" "p99 (ns)" "p999 (ns)" | tee -a "$out"
for store in single global rwlock rcu; do
  bin=partD/build/bench_$store
  [ $store = rcu ] && bin=partD/build/bench_rcu
  w=(--writer-rate 50000); [ $store = single ] && w=(--writer-rate 0)
  for r in 1 2 4 8; do
    rows=""
    for i in $(seq $reps); do
      line=$($pin $bin --mode point --readers $r --seconds $secs "${w[@]}" --label $store)
      rows+="$(field "$line" ops_per_s) $(field "$line" p99_ns) $(field "$line" p999_ns)"$'\n'
    done
    rps=$(awk 'NF {print $1}' <<< "$rows" | sort -n | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')
    p99=$(awk 'NF {print $2}' <<< "$rows" | sort -n | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')
    p999=$(awk 'NF {print $3}' <<< "$rows" | sort -n | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')
    printf "%-12s %8d %14.1f %10d %10d\n" $store $r "$(awk -v x=$rps 'BEGIN {print x/1e6}')" $p99 $p999 | tee -a "$out"
  done
done
echo | tee -a "$out"
echo "Saved to $out. Send that file back."
