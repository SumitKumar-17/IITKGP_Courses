#!/bin/bash
# Runs on the server. Does the result depend on where the store object lands?
# The driver's KVStore is a local variable, so its address moves with the size of the
# process environment. Each build is run at 4 readers with an unrelated environment
# variable padded to different lengths; 3 runs of 3 s each, median shown.
cd "$(dirname "$0")/.."
make -C partA -s all ablation 2>&1 | grep -E "error"
builds=("global_lock_unaligned" "global_lock" "rwlock_unaligned" "rwlock")
echo "pad_bytes ${builds[*]}   (median M reads/s, 4 readers)"
for n in 0 8 16 24 32 40 48 56 64 72 80 88 96 104 112 120; do
  pad=$(head -c $n /dev/zero | tr '\0' 'x')
  line=$(printf "%9d" $n)
  for b in "${builds[@]}"; do
    v=$(for i in 1 2 3; do PADVAR=$pad taskset -c 0-15 partA/build/$b --readers 4 --seconds 3 --csv | awk -F, '/^csv/ {print $5/1e6}'; done | sort -n | sed -n 2p)
    line+=$(printf " %6.1f" $v)
  done
  echo "$line"
done
