#!/bin/bash
# Builds a report PDF from a Markdown file using pandoc + xelatex on the server.
# usage: tools/pdf.sh partA/REPORT.md
set -euo pipefail
cd "$(dirname "$0")/.."

SERVER=t1000@10.5.31.157
REMOTE=docs_asgn2
md="$1"
pdf="${md%.md}.pdf"

./syncer.sh push
ssh -o BatchMode=yes "$SERVER" "cat > /tmp/fixrules.tex <<'X'
\\renewcommand{\\toprule}[1][]{}
\\renewcommand{\\midrule}[1][]{}
\\renewcommand{\\bottomrule}[1][]{}
X
cd $REMOTE/$(dirname "$md") && pandoc $(basename "$md") -o $(basename "$pdf") \
  -V geometry:margin=1in -V fontsize=10pt --pdf-engine=xelatex -H /tmp/fixrules.tex"
scp -q "$SERVER:$REMOTE/$pdf" "$pdf"
echo "$pdf: $(ssh -o BatchMode=yes "$SERVER" "pdfinfo $REMOTE/$pdf | grep Pages")"
