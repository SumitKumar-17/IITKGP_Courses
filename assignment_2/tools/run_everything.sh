#!/bin/bash
# Runs on the server. Every measurement behind the reports, in order, on a machine
# prepared with: performance governor, deep idle states (C2, C3) off,
# kernel.kptr_restrict=0 for the flame graphs.
#
#   tests      unit, stress and transaction tests, plain and under the sanitizers
#   Part A     throughput sweep, perf stat, flame graphs
#   Part B     design comparison, epoch vs hazard pointers
#   Part D     D.1, D.2, D.3, RCU flame graph
#   check      environment-size sweep (results must not depend on it)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p partD/results
log() { echo "== $*"; }

make -C partA -s && make -C partB -s && make -C partC -s && make -C partD -s

log tests
for p in A B C; do make -C part$p check 2>&1 | grep -E "passed|PASS|FAIL|ERROR|Sanitizer|mismatch" > partD/results/check_part$p.txt || true; done

log "Part A scaling"
partA/scaling.sh > /dev/null
log "Part A profile"
rm -rf partA/results/profile
partA/profile.sh 3 | tail -1
( cd partA/results/profile && for f in *.svg; do
    google-chrome --headless=new --no-sandbox --disable-gpu --hide-scrollbars --window-size=1420,700 \
      --screenshot="${f%.svg}.png" "file://$PWD/$f" > /dev/null 2>&1 || true
  done )

log "Part B design sweep"
partB/bench/sweep.sh | tail -1
log "Part B schemes"
partB/compare_schemes.sh > /dev/null

log "Part D"
partD/run_all.sh

log "environment-size check"
tools/envsweep.sh > partD/results/envsweep.txt
echo EVERYTHINGDONE
