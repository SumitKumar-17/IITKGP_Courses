#!/bin/bash
# Rebuilds Asgn1_22CS30056_22CS30032.zip from scratch, from whatever is
# currently in this directory. Run this after editing any report content
# and any time before the final upload -- it always assembles fresh, so
# it can never go stale from a leftover copy sitting in /tmp.
#
# Usage:
#   ./rebuild_submission.sh            # also regenerates both PDFs first
#   ./rebuild_submission.sh --no-pdf   # zip only, skip the VM round trip
set -euo pipefail
cd "$(dirname "$0")"

if [ "${1:-}" != "--no-pdf" ]; then
  echo "== regenerating PDFs =="
  ./rebuild_pdf.sh PART_A_REPORT.md
  ./rebuild_pdf.sh PART_D_REPORT.md
fi

NAME=Asgn1_22CS30056_22CS30032
STAGE=$(mktemp -d)/$NAME
mkdir -p "$STAGE"

echo "== staging =="
cp ../Readme.md "$STAGE/"
cp testbench.lua "$STAGE/"

mkdir -p "$STAGE/lua-5.5.0"
tar -cf - --exclude='*.o' --exclude='liblua.a' \
  --exclude='./lua' --exclude='./luac' \
  --exclude='./fullgc' --exclude='./incrementalgc' --exclude='./generationalgc' \
  --exclude='./nogc' --exclude='./parseonly' --exclude='./sweepbench' --exclude='./sweeptest' \
  --exclude='./testbench_m*.lua' --exclude='./promobench*.lua' \
  -C lua-5.5.0 . | tar -xf - -C "$STAGE/lua-5.5.0"
rm -f "$STAGE/lua-5.5.0/src/lua" "$STAGE/lua-5.5.0/src/luac"

mkdir -p "$STAGE/PartA/results"
cp PART_A_REPORT.pdf PART_A_REPORT.md "$STAGE/PartA/"
cp results/fullgc.png results/incrementalgc.png results/generationalgc.png "$STAGE/PartA/results/"
cp results/fullgc.callgrind results/incrementalgc.callgrind results/generationalgc.callgrind "$STAGE/PartA/results/"
cp results/fullgc.annotate.txt results/incrementalgc.annotate.txt results/generationalgc.annotate.txt "$STAGE/PartA/results/"
cp results/gcfuncs.txt "$STAGE/PartA/results/"
cp results/fullgc.memcheck.log results/incrementalgc.memcheck.log results/generationalgc.memcheck.log "$STAGE/PartA/results/"

mkdir -p "$STAGE/PartB"
cp PART_B_DIFF.patch README.md "$STAGE/PartB/"

mkdir -p "$STAGE/PartC/C1" "$STAGE/PartC/C2"
cp PART_C1_DIFF.patch PART_C1_DESIGN.md "$STAGE/PartC/C1/"
cp PART_C2_DIFF.patch "$STAGE/PartC/C2/"
cp PART_C2_DESIGN.md "$STAGE/PartC/C2/README.md"

mkdir -p "$STAGE/PartD"
cp PART_D_REPORT.pdf PART_D_REPORT.md "$STAGE/PartD/"
cp -r results/partD "$STAGE/PartD/results"
rm -f "$STAGE/PartD/results/run.log" "$STAGE/PartD/results/DONE.marker"

cat > "$STAGE/INDEX.md" <<'EOF'
# Submission Layout

- `lua-5.5.0/` — the complete, buildable interpreter source, with every
  part's changes applied cumulatively (`make linux` in `src/`, then
  `./lua -v` should print `Lua 5.5.0`). Also holds the Part A driver
  files (`fullgc.c`/`incrementalgc.c`/`generationalgc.c`), Part B's
  verification driver (`sweeptest.c`), and the Lua-level verification
  scripts used throughout (`esctest.lua`, `arenatest.lua`, `rectest.lua`,
  `gentest.lua`, `corotest.lua`).
- `testbench.lua` — the provided workload, unmodified, used as-is by
  every part.
- `PartA/` — `PART_A_REPORT.pdf`/`.md` and `results/` (callgrind
  outputs, call-graph PNGs, annotated profiles, memcheck logs).
- `PartB/` — `PART_B_DIFF.patch` and `README.md` (the design note).
- `PartC/C1/` — `PART_C1_DIFF.patch` and `PART_C1_DESIGN.md` (the design note).
- `PartC/C2/` — `PART_C2_DIFF.patch` and `README.md` (the design note).
- `PartD/` — `PART_D_REPORT.pdf`/`.md` and `results/` (every callgrind
  output, perf log, and `/usr/bin/time` log referenced in the report,
  plus the chart PNGs and the two scripts that produced them,
  `analyze.py`/`make_charts.py`).

Not included: compiled binaries/object files (rebuildable from the
source above), and a scratch comparison build of the *unmodified*
Lua 5.5.0 tree that was used only to produce Part D.3's stock-vs-current
numbers (byte-identical to this repo's own pre-assignment git history
plus copies of the driver files already included here, so shipping it
again would only add size, not information).
EOF

echo "== zipping =="
rm -f "$NAME.zip"
( cd "$(dirname "$STAGE")" && zip -r -q "$OLDPWD/$NAME.zip" "$NAME" )
rm -rf "$(dirname "$STAGE")"

echo "done: $(pwd)/$NAME.zip ($(du -h "$NAME.zip" | cut -f1))"
