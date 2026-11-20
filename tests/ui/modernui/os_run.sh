#!/bin/bash
# os_run.sh - build and run the modernUI film OS mockups (tests/ui/modernui/os_mock.c) for both cameras in WSL.
#   bash tests/ui/modernui/os_run.sh            (any checkout: REPO is this script's own tree)
# Work dir W (default /root/ml/wf_modern): its own font, can and display font packs, built from this tree once.
# PNGs: $OUT (default the mocks folder in _windyfilm_docs). The photos (stand-in LiveView frames, the prints and the
# contact sheet thumbnails) are the owner's and stay outside the repository: PHOTOS = mk_app_assets.py's output.
set -e
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
W=${W:-/root/ml/wf_modern}
OUT=${OUT:-/mnt/f/MagicLatern/_windyfilm_docs/modernui_mocks}
PHOTOS=${PHOTOS:-$OUT/apps_src}
RBF=${RBF:-/root/ml/wf_ui/fonts}
LV=${LV:-/mnt/f/MagicLatern/_test mau film/IMG_6853.JPG}
LV5=${LV5:-/mnt/f/MagicLatern/_test mau film/5D2/100EOS5D/IMG_8380.JPG}
PY="python3 -I -B"
mkdir -p $W/build $W/out $W/shots $W/card/ML/DATA
cd $W
[ -f out/WFFONTS.DAT ] || $PY $REPO/tools/mk_wffont.py out/WFFONTS.DAT
[ -f out/WFCANS.DAT ] || $PY $REPO/tools/mk_wfcans.py out/WFCANS.DAT --colours 103
[ -f out/WFOSFONT.DAT ] || $PY $REPO/tests/ui/modernui/mk_osfont.py out/WFOSFONT.DAT
cp out/WFFONTS.DAT out/WFCANS.DAT card/ML/DATA/
$PY $REPO/tests/ui/mk_fakecat.py build/fake_catalog.c > /dev/null
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
gcc -std=gnu99 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-comment $SAN -DWF_HOST -DWF_HOST_PURE \
    -I$REPO/tests/ui -I$REPO/src -I$REPO/src/ui \
    $REPO/src/ui/wf_gfx.c $REPO/src/ui/wf_font.c $REPO/src/ui/wf_cans.c $REPO/src/ui/wf_ui.c $REPO/src/ui/wf_dark_ui.c \
    $REPO/src/wf_roll.c $REPO/src/wf_photo.c $REPO/tests/ui/host_ml.c $REPO/tests/ui/fake_core.c $REPO/tests/ui/fake_dark.c \
    build/fake_catalog.c $REPO/tests/ui/modernui/os_mock.c -o build/os_mock
rm -f shots/*
for CAM in EOSM 5D2; do
    build/os_mock shots card $RBF out/WFOSFONT.DAT $CAM 2> build/os_$CAM.log || { cat build/os_$CAM.log; exit 1; }
    grep -v '^\[log\]' build/os_$CAM.log || true
done
$PY $REPO/tests/ui/modernui/os_comp.py shots "$PHOTOS" "$LV" "$LV5" "$OUT"
# the Darkroom stays the RAWB28 screen: tests/ui/dr_run.sh run first with W=$W/dr (EOS M) and CAM=5D2 W=$W/dr5d2
for p in eosm:dr 5d2:dr5d2; do
    c=${p%%:*}; d=$W/${p##*:}/shots
    [ -f $d/rawb28_sheet_cam12.png ] || continue
    cp $d/rawb28_sheet_cam12.png "$OUT/${c}_10_darkroom.png"
    cp $d/dark_expose_52.png "$OUT/${c}_11_darkroom_develop.png"
done
$PY $REPO/tests/ui/modernui/os_sheet.py "$OUT"
