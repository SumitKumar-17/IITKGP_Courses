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

cat > "$STAGE/INDEX.md" <<'EOF'
# NOVA-KV, Assignment 2: layout of this submission

Sumit Kumar (22CS30056), Jatin Mahawar (22CS30032)

| folder | what is in it |
|---|---|
| `common/` | the provided workload generator (unmodified), the shared driver `driver_main.h`, `schema.h` |
| `partA/` | baseline store, three drivers, tests, profiling scripts, flame graphs, layout benchmark. **`REPORT.pdf`** (the 5-page report) |
| `partB/` | RCU store, stress test, design and reclamation benchmarks. **`README.md`**, `PART_B_DIFF.patch` |
| `partC/` | transactions, index, snapshots, join. **`README.md`** (both design notes), `PART_C_DIFF.patch` |
| `partD/` | benchmark harness, scripts, all raw data. **`REPORT.pdf`** |
| `tools/` | summaries, charts, environment and layout checks, the idle-CPU check |

Build and test a part: `make -C partA test` (and `check` for the sanitizer builds), same for `partB`, `partC`.
Each part keeps its own `kvstore.h` / `kvstore.cpp`, so the diffs between parts are real files.
Everything was built and measured with `g++ -std=c++17` on a 32-thread Intel i9-13900K, pinned to
logical CPUs 0 to 15. The store has no durability in any of these parts: if the process dies the data is gone.

Two measurement issues are explained in `partA/REPORT.pdf` (section 2) and `partD/REPORT.pdf` (section 0):
the cache-line layout of the store object, and false sharing inside the provided generator.
EOF

rm -f "$NAME.zip"
( cd "$(dirname "$STAGE")" && zip -r -q "$OLDPWD/$NAME.zip" "$NAME" )
rm -rf "$(dirname "$STAGE")"
echo "$(pwd)/$NAME.zip ($(du -h "$NAME.zip" | cut -f1))"
