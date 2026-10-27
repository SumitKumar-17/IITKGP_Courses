#!/bin/bash
# Runs on the server. Do the provided generator's own latency vectors cost throughput?
# Each store is built twice, against the original workload_gen.cpp and a copy whose
# per-reader vectors are padded to one cache line each. Everything else is identical.
cd "$(dirname "$0")/../.."
mkdir -p partD/build
inc="-std=c++17 -O2 -g -fno-omit-frame-pointer -pthread -I../common"
for pair in "global_lock:partA:1" "rwlock:partA:2" "rcu:partB:0" "rcu_c:partC:0"; do
  IFS=: read name part sync <<< "$pair"
  drv=$name; [ $name = rcu_c ] && drv=rcu
  flags="$inc -I../$part"; [ $sync != 0 ] && flags="$flags -DNOVA_SYNC=$sync"
  src=$([ $part = partA ] && echo "../partA/$drv.cpp ../partA/kvstore.cpp" || echo "../$part/rcu.cpp ../$part/kvstore.cpp")
  ( cd partD && g++ $flags $src ../common/workload_gen.cpp -o build/gen_orig_$name &&
    g++ $flags $src experiments/workload_gen_padded.cpp -o build/gen_pad_$name ) || exit 1
done
echo "store readers original_M padded_M   (median of 5, alternating)"
for pair in "global_lock 4" "global_lock 8" "rwlock 4" "rwlock 8" "rcu 4" "rcu 8" "rcu_c 8"; do
  set -- $pair
  o=(); p=()
  for i in 1 2 3 4 5; do
    o+=($(taskset -c 0-15 partD/build/gen_orig_$1 --readers $2 --csv | awk -F, '/^csv/ {print $5/1e6}'))
    p+=($(taskset -c 0-15 partD/build/gen_pad_$1 --readers $2 --csv | awk -F, '/^csv/ {print $5/1e6}'))
  done
  mo=$(printf "%s\n" "${o[@]}" | sort -n | sed -n 3p); mp=$(printf "%s\n" "${p[@]}" | sort -n | sed -n 3p)
  printf "%-12s %d %8.1f %8.1f\n" $1 $2 $mo $mp
done
