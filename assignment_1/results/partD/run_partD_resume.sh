#!/bin/bash
# Resumes run_partD.sh from D.2 onward (D.1a/D.1c already completed).
set -e
BASE=~/docs_asgn1
CUR=$BASE/lua-5.5.0
STOCK=$BASE/lua-stock
RES=$BASE/results/partD
cd "$RES"

log() { echo "[$(date +%T)] $*"; }

declare -A FILES=( [default]="$CUR/testbench.lua" [m100_n100]="$CUR/testbench_m100_n100.lua" \
                    [m500_n100]="$CUR/testbench_m500_n100.lua" [m5000_n100]="$CUR/testbench_m5000_n100.lua" )

log "D.2: sweep-step sweep"
for step in 5 20 50 200; do
  out="$RES/d2_sweepstep_${step}"
  { /usr/bin/time -v "$CUR/sweepbench" "$step" "$CUR/testbench.lua" ; } > "${out}.time.txt" 2>&1
  valgrind --tool=callgrind --callgrind-out-file="${out}.callgrind" --quiet "$CUR/sweepbench" "$step" "$CUR/testbench.lua"
  callgrind_annotate --threshold=100 "${out}.callgrind" > "${out}.annotate.txt" 2>/dev/null
  log "  d2 step=$step done"
done

log "D.3a: parse-only cost, stock vs current"
for build in stock current; do
  if [ "$build" = "stock" ]; then BIN=$STOCK/parseonly; TBFILE=$STOCK/testbench.lua; else BIN=$CUR/parseonly; TBFILE=$CUR/testbench.lua; fi
  out="$RES/d3a_parseonly_${build}"
  { /usr/bin/time -v "$BIN" 20000 "$TBFILE" ; } > "${out}.time.txt" 2>&1
  valgrind --tool=callgrind --callgrind-out-file="${out}.callgrind" --quiet "$BIN" 200 "$TBFILE"
  log "  d3a $build done"
done

log "D.3b: stock vs current, callgrind, testbench + promobench"
for tag in default m100_n100 m500_n100 m5000_n100 promobench; do
  if [ "$tag" = "promobench" ]; then
    curfile="$CUR/promobench.lua"; stockfile="$STOCK/promobench.lua"
  else
    curfile="${FILES[$tag]}"
    stockfile="$STOCK/$(basename "${FILES[$tag]}")"
  fi
  for build in stock current; do
    if [ "$build" = "stock" ]; then BIN=$STOCK/incrementalgc; FILE=$stockfile; else BIN=$CUR/incrementalgc; FILE=$curfile; fi
    out="$RES/d3b_${tag}_${build}"
    valgrind --tool=callgrind --callgrind-out-file="${out}.callgrind" --quiet "$BIN" "$FILE"
    callgrind_annotate --threshold=100 "${out}.callgrind" > "${out}.annotate.txt" 2>/dev/null
    log "  d3b $tag $build done"
  done
done

log "ALL DONE"
touch "$RES/DONE.marker"
