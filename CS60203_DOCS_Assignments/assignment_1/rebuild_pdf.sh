#!/bin/bash
# Rebuilds a report PDF from its Markdown source.
#
# Usage:   ./rebuild_pdf.sh PART_A_REPORT.md
#          ./rebuild_pdf.sh PART_D_REPORT.md
#
# Edit the .md content locally, then run this. It copies the file (and,
# for Part D, its chart images under results/partD/) to the VM, runs
# pandoc+xelatex there, and copies the resulting PDF back to this
# directory. Needs SSH access to the VM (same one used throughout the
# assignment) with pandoc/xelatex already installed there.
set -euo pipefail

VM=t1000@10.5.31.157
REMOTE_DIR=docs_asgn1   # relative to the VM login user's home

if [ $# -ne 1 ]; then
  echo "usage: $0 <REPORT>.md" >&2
  exit 1
fi

MD="$1"
PDF="${MD%.md}.pdf"

if [ ! -f "$MD" ]; then
  echo "error: $MD not found in $(pwd)" >&2
  exit 1
fi

echo "[1/4] copying $MD (and any images it references) to the VM..."
tar -czf - "$MD" results/ 2>/dev/null | ssh "$VM" "mkdir -p $REMOTE_DIR && tar -xzf - -C $REMOTE_DIR"

echo "[2/4] writing the booktabs workaround (this VM's texlive install"
echo "      crashes on \\toprule/\\midrule/\\bottomrule otherwise)..."
ssh "$VM" "cat > /tmp/fixrules.tex <<'HDREOF'
\\renewcommand{\\toprule}[1][]{}
\\renewcommand{\\midrule}[1][]{}
\\renewcommand{\\bottomrule}[1][]{}
HDREOF"

echo "[3/4] running pandoc + xelatex..."
ssh "$VM" "cd $REMOTE_DIR && pandoc '$MD' -o '$PDF' \
  -V geometry:margin=1in -V fontsize=10pt --pdf-engine=xelatex \
  -H /tmp/fixrules.tex"

echo "[4/4] copying $PDF back..."
scp "$VM:$REMOTE_DIR/$PDF" "./$PDF"

PAGES=$(ssh "$VM" "pdfinfo '$REMOTE_DIR/$PDF' 2>/dev/null | grep Pages" || echo "?")
echo "done: $PDF ($PAGES)"
