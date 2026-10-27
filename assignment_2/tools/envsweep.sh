#!/bin/bash
# Runs on the server. Does a result depend on the size of the process environment?
# Each store is run at 4 readers with an unrelated environment variable padded to
# different lengths. A layout-sensitive build swings widely (the Part A store did,
# 8M to 26M reads/s, before its fields were aligned); a sound one stays put.
cd "$(dirname "$0")/.."
stores=("partA/build/global_lock" "partA/build/rwlock" "partB/build/rcu" "partC/build/rcu")
echo "pad bytes: ${stores[*]}"
for n in 0 8 16 24 32 40 48 56 64 72 80 88 96 104 112 120; do
  pad=$(head -c $n /dev/zero | tr '\0' 'x')
  line=$(printf "%3d " $n)
  for b in "${stores[@]}"; do
    v=$(PADVAR=$pad taskset -c 0-15 $b --readers 4 --seconds 3 --csv | awk -F, '/^csv/ {printf "%.1f", $5/1e6}')
    line+=" ${v}M"
  done
  echo "$line"
done
