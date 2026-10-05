#!/bin/bash
# Moves files between this machine and the benchmark server.
#
#   ./syncer.sh push            local sources -> server (never touches results)
#   ./syncer.sh pull            server results/ folders -> local
#   ./syncer.sh pullall         everything on the server -> local (asks first)
#   ./syncer.sh run <cmd...>    push, then run a command in the server project dir
#   ./syncer.sh sh              open a shell in the server project dir
#   ./syncer.sh part <X> <tgt>  push, then `make -C part<X> <tgt>` on the server
set -euo pipefail
cd "$(dirname "$0")"

SERVER=t1000@10.5.31.157
REMOTE=docs_asgn2
SSH="ssh -o BatchMode=yes"
EXCL=(--exclude='pass.txt' --exclude='*.o' --exclude='*.zip' --exclude='build/'
      --exclude='bin/' --exclude='.git/' --exclude='FlameGraph/')

push() {
  rsync -az -e "$SSH" "${EXCL[@]}" --exclude='results/' ./ "$SERVER:$REMOTE/"
}

pull() {
  for d in part?/results; do
    mkdir -p "$d"
    rsync -az -e "$SSH" "$SERVER:$REMOTE/$d/" "$d/"
  done
}

case "${1:-}" in
  push) push ;;
  pull) pull ;;
  pullall)
    read -rp "overwrite local files with the server copy? [y/N] " a
    [ "$a" = y ] && rsync -az -e "$SSH" "${EXCL[@]}" "$SERVER:$REMOTE/" ./
    ;;
  run) shift; push; $SSH "$SERVER" "cd $REMOTE && $*" ;;
  part) push; $SSH "$SERVER" "cd $REMOTE && make -C part$2 ${3:-all}" ;;
  sh) $SSH -t "$SERVER" "cd $REMOTE && exec \$SHELL -l" ;;
  *) sed -n '2,10p' "$0"; exit 1 ;;
esac
