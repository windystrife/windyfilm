#!/bin/bash
# render.sh - WindyFilm phone mockups -> 1080 x 2400 PNGs (stage 1, approval images; not app code).
# Git Bash on Windows (Edge headless). Run mk_assets.py first (WSL, PIL) to fill WORK/assets.
#   bash render.sh WORK OUT [screen.html ...]
# WORK gets a copy of the HTML / CSS next to its assets; OUT gets <screen>.png. Both stay outside the repository
# (owner photos and private Film stage renders inside).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$1; OUT=$2; shift 2
EDGE=${EDGE:-"/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"}
mkdir -p "$WORK" "$OUT"
cp "$HERE"/*.html "$HERE"/wfm.css "$WORK"/
LIST=("$@"); [ ${#LIST[@]} -eq 0 ] && LIST=($(cd "$HERE" && ls [0-9]*.html))
WIN_WORK=$(cygpath -m "$WORK")
for h in "${LIST[@]}"; do
  b=${h%.html}
  "$EDGE" --headless=new --disable-gpu --hide-scrollbars --no-first-run --allow-file-access-from-files \
    --user-data-dir="$(cygpath -w "$WORK")\\edge_prof" --force-device-scale-factor=3 --window-size=360,800 \
    --virtual-time-budget=4000 --screenshot="$(cygpath -w "$OUT")\\$b.png" "file:///$WIN_WORK/$h" 2>/dev/null
  echo "$OUT/$b.png"
done
