#!/bin/bash
# Runs on the server. Saves the output of the correctness tests on the final builds, and
# the full-table-copy cost, so the READMEs can quote numbers that exist in a file.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p partB/results partC/results
make -C partB -s && make -C partC -s
{ partB/build/kvstore_test; partB/build/stress_test 6 5 64; } > partB/results/tests.txt
{ partC/build/kvstore_test; partC/build/stress_test 6 5 64; partC/build/txn_test 3; } > partC/results/tests.txt
g++ -O2 -std=c++17 -pthread partB/bench/design_bench.cpp -o /tmp/design_bench
{ for k in 2000 5000 10000 20000; do taskset -c 0-15 /tmp/design_bench full 0 $k 50000; done; } > partB/results/full_copy.txt
cat partB/results/tests.txt partC/results/tests.txt partB/results/full_copy.txt
