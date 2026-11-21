#!/bin/bash
# render.sh - WindyFilm phone mockups -> PNGs at x3: 1080 x 2400, or a screen's own size (stage 1, approval images; not app code).
# Git Bash on Windows (Edge headless). Run mk_assets.py first (WSL, PIL) to fill WORK/assets.
#   bash render.sh WORK OUT [screen.html ...]
# WORK gets a copy of the HTML / CSS next to its assets; OUT gets <screen>.png. Both stay outside the repository
# (owner photos and private Film stage renders inside).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$1; OUT=$2; shift 2
EDGE=${EDGE:-"/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"}
mkdir -p "$WORK" "$OUT"
cp "$HERE"/*.html "$HERE"/*.js "$HERE"/wfm.css "$WORK"/
LIST=("$@"); [ ${#LIST[@]} -eq 0 ] && LIST=($(cd "$HERE" && ls [0-9]*.html))
WIN_WORK=$(cygpath -m "$WORK")
for h in "${LIST[@]}"; do
  b=${h%.html}
  # a screen may set its own window: <meta name="wf-size" content="WxH"> (foldable mocks); default 360x800
  sz=$(grep -o 'name="wf-size" content="[0-9]*x[0-9]*"' "$HERE/$h" | grep -o '[0-9]*x[0-9]*' || true)
  sz=${sz:-360x800}
  # one Edge profile per screen: an Edge still closing on a shared profile swallows the next call. msedge.exe returns
  # before the screenshot is written, so wait for a new, settled PNG (up to 60 s) before the next screen.
  stamp="$WORK/.stamp_$b"; : > "$stamp"; sleep 1
  "$EDGE" --headless=new --disable-gpu --hide-scrollbars --no-first-run --allow-file-access-from-files \
    --user-data-dir="$(cygpath -w "$WORK")\\edge_prof\\$b" --force-device-scale-factor=3 --window-size=${sz/x/,} \
    --virtual-time-budget=4000 --screenshot="$(cygpath -w "$OUT")\\$b.png" "file:///$WIN_WORK/$h" 2>/dev/null
  ok=0; last=-1
  for i in $(seq 1 120); do
    if [ -s "$OUT/$b.png" ] && [ "$OUT/$b.png" -nt "$stamp" ]; then
      n=$(stat -c %s "$OUT/$b.png"); [ "$n" = "$last" ] && { ok=1; break; }; last=$n
    fi
    sleep 0.5
  done
  rm -f "$stamp"
  [ $ok = 1 ] || { echo "no new $OUT/$b.png" >&2; exit 1; }
  echo "$OUT/$b.png"
done
