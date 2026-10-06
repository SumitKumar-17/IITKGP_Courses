#!/bin/bash
# Runs on the server. Saves the output of the correctness tests on the final builds, and
# the full-table-copy cost, so the READMEs can quote numbers that exist in a file.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p partA/results partB/results partC/results partE/results
make -C partA -s && make -C partB -s && make -C partC -s && make -C partE -s
{ partA/build/kvstore_test; make -C partA -s check 2>&1 | grep -E 'passed|PASS|FAIL|rwlock/'; } > partA/results/tests.txt
{ partB/build/kvstore_test; partB/build/stress_test 6 5 64; } > partB/results/tests.txt
{ partC/build/kvstore_test; partC/build/stress_test 6 5 64; partC/build/txn_test 3; } > partC/results/tests.txt
{ partE/build/kvstore_test; partE/build/stress_test 6 5 64; partE/build/txn_test 3; partE/build/wal_test; partE/build/crash_test 40; } > partE/results/tests.txt
g++ -O2 -std=c++17 -pthread partB/bench/design_bench.cpp -o /tmp/design_bench
{ for k in 2000 5000 10000 20000; do taskset -c 0-15 /tmp/design_bench full 0 $k 50000; done; } > partB/results/full_copy.txt
cat partA/results/tests.txt partB/results/tests.txt partC/results/tests.txt partE/results/tests.txt partB/results/full_copy.txt
