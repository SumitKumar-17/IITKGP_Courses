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

# Each part's own folder gets the *modified files* (as the spec's
# deliverable line literally asks for), not just the diff. Most of them
# are copied straight from the one buildable tree at lua-5.5.0/src/ (the
# source of truth for the final, cumulative interpreter) -- that's
# correct wherever a part's own files are never touched again by a
# later part. lgc.c is the one exception: Part C.2 edits it again after
# Part B, so a straight copy would silently include C.2's arena code in
# what's supposed to be "Part B's lgc.c", next to a diff that only shows
# Part B's small change. Reconstructed instead as pristine + only
# PART_B_DIFF.patch, so the diff and the file in this folder agree.
ORIG_COMMIT=8ec6b56   # "Adding Assignment 1": the untouched originals
PARTB_SRC=$(mktemp -d)
mkdir -p "$PARTB_SRC/assignment_1/lua-5.5.0/src"
git -C .. show "$ORIG_COMMIT:assignment_1/lua-5.5.0/src/lgc.c" \
  > "$PARTB_SRC/assignment_1/lua-5.5.0/src/lgc.c"
git -C .. show "$ORIG_COMMIT:assignment_1/lua-5.5.0/src/lua.h" \
  > "$PARTB_SRC/assignment_1/lua-5.5.0/src/lua.h"
( cd "$PARTB_SRC" && patch -s -p1 < "$OLDPWD/PART_B_DIFF.patch" )

mkdir -p "$STAGE/PartB/modified-src"
cp PART_B_DIFF.patch README.md "$STAGE/PartB/"
cp "$PARTB_SRC/assignment_1/lua-5.5.0/src/lgc.c" \
   "$PARTB_SRC/assignment_1/lua-5.5.0/src/lua.h" "$STAGE/PartB/modified-src/"
rm -rf "$PARTB_SRC"

mkdir -p "$STAGE/PartC/C1/modified-src" "$STAGE/PartC/C2/modified-src"
cp PART_C1_DIFF.patch PART_C1_DESIGN.md "$STAGE/PartC/C1/"
cp lua-5.5.0/src/lparser.c lua-5.5.0/src/lparser.h lua-5.5.0/src/lobject.h lua-5.5.0/src/lfunc.c \
  "$STAGE/PartC/C1/modified-src/"
cp PART_C2_DIFF.patch "$STAGE/PartC/C2/"
cp PART_C2_DESIGN.md "$STAGE/PartC/C2/README.md"
cp lua-5.5.0/src/lgc.c lua-5.5.0/src/lgc.h lua-5.5.0/src/lvm.c lua-5.5.0/src/ldo.c \
  lua-5.5.0/src/lstate.c lua-5.5.0/src/lstate.h lua-5.5.0/src/ltable.c lua-5.5.0/src/ltable.h \
  lua-5.5.0/src/lfunc.c "$STAGE/PartC/C2/modified-src/"

mkdir -p "$STAGE/PartD"
cp PART_D_REPORT.pdf PART_D_REPORT.md "$STAGE/PartD/"
cp -r results/partD "$STAGE/PartD/results"
rm -f "$STAGE/PartD/results/run.log" "$STAGE/PartD/results/DONE.marker"

cat > "$STAGE/INDEX.md" <<'EOF'
# Submission Layout

- `lua-5.5.0/`: the complete, buildable interpreter source, with every
  part's changes applied cumulatively (`make linux` in `src/`, then
  `./lua -v` should print `Lua 5.5.0`). This is the single source of
  truth for every modified file; each `Part*/modified-src/` folder
  below is a copy of just the files that part's own diff touches, kept
  alongside that part's diff and note per the assignment's own wording.
  Also holds the Part A driver files
  (`fullgc.c`/`incrementalgc.c`/`generationalgc.c`), Part B's
  verification driver (`sweeptest.c`), and the Lua-level verification
  scripts used throughout (`esctest.lua`, `arenatest.lua`, `rectest.lua`,
  `gentest.lua`, `corotest.lua`).
- `testbench.lua`: the provided workload, unmodified, used as-is by
  every part.
- `PartA/`: `PART_A_REPORT.pdf`/`.md` and `results/` (callgrind
  outputs, call-graph PNGs, annotated profiles, memcheck logs).
- `PartB/`: `PART_B_DIFF.patch`, `README.md` (the design note), and
  `modified-src/` (`lgc.c`, `lua.h`).
- `PartC/C1/`: `PART_C1_DIFF.patch`, `PART_C1_DESIGN.md` (the design
  note), and `modified-src/` (`lparser.c`, `lparser.h`, `lobject.h`,
  `lfunc.c`).
- `PartC/C2/`: `PART_C2_DIFF.patch`, `README.md` (the design note), and
  `modified-src/` (`lgc.c`, `lgc.h`, `lvm.c`, `ldo.c`, `lstate.c`,
  `lstate.h`, `ltable.c`, `ltable.h`, `lfunc.c`).
- `PartD/`: `PART_D_REPORT.pdf`/`.md` and `results/` (every callgrind
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
