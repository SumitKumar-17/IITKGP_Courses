#!/bin/bash
# Runs on the server: the whole Part D measurement set, one experiment after another.
set -euo pipefail
cd "$(dirname "$0")/.."
true
mkdir -p partD/results
: > partD/results/quiet.log
partD/d1.sh
partD/d2.sh
partD/d3.sh
partD/profile_rcu.sh
echo ALLDONE
