#!/bin/bash
# Assembles Asgn2_22CS30056_22CS30032.zip from whatever is in this directory.
#
#   ./submitter.sh            rebuild every report PDF, then zip
#   ./submitter.sh --no-pdf   zip only
#   ./submitter.sh A C        only these parts' PDFs are rebuilt, then zip
set -euo pipefail
cd "$(dirname "$0")"

NAME=Asgn2_22CS30056_22CS30032
PARTS=(A B C D E)

args=("$@")
build_pdf=1
if [ "${args[0]:-}" = "--no-pdf" ]; then build_pdf=0; args=(); fi
[ ${#args[@]} -gt 0 ] && want=("${args[@]}") || want=("${PARTS[@]}")

if [ $build_pdf = 1 ]; then
  for p in "${want[@]}"; do
    md="part$p/REPORT.md"
    [ -f "$md" ] && tools/pdf.sh "$md"
  done
fi

STAGE=$(mktemp -d)/$NAME
mkdir -p "$STAGE/common"
cp common/*.h common/*.cpp "$STAGE/common/"
cp ../Readme.md "$STAGE/Readme.md" 2>/dev/null || true

for p in "${PARTS[@]}"; do
  d="part$p"
  [ -d "$d" ] || continue
  [ -n "$(find "$d" -type f 2>/dev/null | head -1)" ] || continue
  mkdir -p "$STAGE/$d"
  tar -cf - --exclude='*.o' --exclude='build' --exclude='bin' -C "$d" . | tar -xf - -C "$STAGE/$d"
done

mkdir -p "$STAGE/tools"
cp tools/*.py tools/*.sh tools/*.c "$STAGE/tools/"
cp assignment_2.pdf "$STAGE/" 2>/dev/null || true
[ -f verification.txt ] && cp verification.txt "$STAGE/" || true

cat > "$STAGE/INDEX.md" <<'EOF'
# NOVA-KV, Assignment 2

Sumit Kumar (22CS30056), Jatin Mahawar (22CS30032)

The store has no durability in Parts A to D: if the process dies, the data is gone. Part E (the bonus)
adds a write-ahead log.

## What is where, in the order of the assignment

| Part | Asked for | Files |
|---|---|---|
| A | store, sanity test, three drivers | `partA/kvstore.{h,cpp}`, `kvstore_test.cpp`, `single_thread.cpp`, `global_lock.cpp`, `rwlock.cpp` |
| A | perf outputs and flame graphs | `partA/results/profile/` (perf stat, folded stacks, SVG and PNG for both lock stores at 1, 2, 4, 8 readers) |
| A | layout microbenchmark | `partA/bench/layout_bench.cpp` (NSM, DSM, PAX), `table_bench.cpp` (hash table choice) |
| A | report, 5 pages at most | `partA/REPORT.pdf` (source `REPORT.md`) |
| B | RCU store and its diff against Part A | `partB/kvstore.{h,cpp}`, `PART_B_DIFF.patch` |
| B | README on reclamation and the tunable | `partB/README.md` |
| B | stress test, no torn rows | `partB/stress_test.cpp`, output in `partB/results/tests.txt` |
| C | transactions, index, snapshots, join, and the diff against Part B | `partC/kvstore.{h,cpp}`, `join.h`, `PART_C_DIFF.patch` |
| C | design notes (C.1 isolation, C.2 index and join) | `partC/README.md` |
| C | tests | `partC/txn_test.cpp`, `kvstore_test.cpp`, output in `partC/results/tests.txt` |
| D | D.1, D.2, D.3 results, explanations, raw data | `partD/REPORT.pdf` (source `REPORT.md`), raw data in `partD/results/` |
| D | benchmark harness and scripts | `partD/bench.cpp`, `d1.sh`, `d1b.sh`, `d2.sh`, `d3.sh`, `hist_extra.sh`, `experiments/` |
| E | write-ahead log and its diff against Part C | `partE/wal.{h,cpp}`, `kvstore.{h,cpp}`, `PART_E_DIFF.patch` |
| E | fsync and group commit note, cost, what is missing | `partE/README.md` |
| E | tests | `partE/wal_test.cpp`, `crash_test.cpp`, output in `partE/results/tests.txt` |

`common/` holds the provided workload generator (unchanged), the shared driver `driver_main.h` and `schema.h`.
`tools/` holds the scripts that summarise results, draw the charts and check the measurement setup.
`verification.txt` is the log of building every part from a clean copy and running every test.

Each part keeps its own `kvstore.h` and `kvstore.cpp`, so the diffs between parts are real files.
The header of each store marks which methods are the Appendix A interface and which are extras.

## Build and test

    make -C partA test        # unit test; `make -C partA check` adds the sanitizer builds
    make -C partB test        # likewise partC and partE
    tools/verify_all.sh       # everything, from scratch, with PASS or FAIL per step

## Reproduce the measurements

All of them ran on one 32-thread Intel i9-13900K, pinned to logical CPUs 0 to 15, with `g++ -std=c++17`.

| script | measures | output |
|---|---|---|
| `partA/scaling.sh`, `partA/profile.sh` | Part A throughput, perf stat, flame graphs | `partA/results/` |
| `partB/bench/sweep.sh`, `partB/compare_schemes.sh` | version structure choice, epochs against hazard pointers | `partB/results/` |
| `partD/d1.sh`, `d1b.sh` | D.1 with the provided generator, and with our harness | `partD/results/d1.csv`, `d1b.txt` |
| `partD/d2.sh` | D.2 reclaim batch tuning | `partD/results/d2.txt`, `hist/` |
| `partD/d3.sh`, `partD/experiments/join_locality.sh` | D.3 join, commits, copy-on-write | `partD/results/d3.txt`, `join_locality.txt` |
| `partE/bench_wal.sh` | what the log costs, group commit | `partE/results/bench_wal.txt` |
| `tools/second_machine.sh` | the D.1 comparison on another machine, in about six minutes | `second_machine_results.txt` |
| `tools/envsweep.sh`, `tools/layout_sensitivity.sh` | that results do not depend on where the store object lands in memory | `partD/results/envsweep.txt`, `partA/results/layout_sensitivity.txt` |

Two measurement problems are explained in `partA/REPORT.pdf` (section 2) and `partD/REPORT.pdf` (section 0):
the cache line layout of the store object, and false sharing inside the provided generator.
EOF

rm -f "$NAME.zip"
( cd "$(dirname "$STAGE")" && zip -r -q "$OLDPWD/$NAME.zip" "$NAME" )
rm -rf "$(dirname "$STAGE")"
echo "$(pwd)/$NAME.zip ($(du -h "$NAME.zip" | cut -f1))"
