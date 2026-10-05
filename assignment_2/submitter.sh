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
cp common/workload_gen.h common/workload_gen.cpp "$STAGE/common/"
cp ../Readme.md "$STAGE/Readme.md" 2>/dev/null || true

for p in "${PARTS[@]}"; do
  d="part$p"
  [ -d "$d" ] || continue
  [ -n "$(ls -A "$d" 2>/dev/null)" ] || continue
  mkdir -p "$STAGE/$d"
  tar -cf - --exclude='*.o' --exclude='build' --exclude='bin' -C "$d" . | tar -xf - -C "$STAGE/$d"
done

rm -f "$NAME.zip"
( cd "$(dirname "$STAGE")" && zip -r -q "$OLDPWD/$NAME.zip" "$NAME" )
rm -rf "$(dirname "$STAGE")"
echo "$(pwd)/$NAME.zip ($(du -h "$NAME.zip" | cut -f1))"
