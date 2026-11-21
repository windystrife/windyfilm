#!/bin/bash
# dr_run.sh - build and run the Roll + Darkroom screen harness (tests/ui/dr_ui_test.c) in WSL; PNGs in $W/shots.
#   bash tests/ui/dr_run.sh [IMG.JPG ...]      (default: four of the owner's Day09 JPEGs as stand-in prints)
#   CAM=5D2 bash tests/ui/dr_run.sh            (the 5D2's keys, four of the owner's 5D2 JPEGs; W .../ui5d2)
# Work dir W (default /root/ml/wf_darkroom/ui). Reads (never writes) the data side's can pack and ML's RBF fonts.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
CAM=${CAM:-EOSM}
[ $CAM = 5D2 ] && W=${W:-/root/ml/wf_darkroom/ui5d2} || W=${W:-/root/ml/wf_darkroom/ui}
export WF_UI_CAM=$CAM
RBF=${RBF:-/root/ml/wf_ui/fonts}
CANS=${CANS:-/root/ml/wf_data/out/WFCANS.DAT}
D=/mnt/f/MagicLatern/_research/raw-path/cr2/Day09_EOSM
PY="python3 -I -B"
mkdir -p $W/build $W/out $W/shots $W/card/ML/DATA $W/thumbs
cd $W
[ -f out/WFFONTS.DAT ] || $PY $REPO/tools/mk_wffont.py out/WFFONTS.DAT
cp out/WFFONTS.DAT card/ML/DATA/
cp "$CANS" card/ML/DATA/WFCANS.DAT
$PY $REPO/tests/ui/mk_fakecat.py build/fake_catalog.c > /dev/null
D5="/mnt/f/MagicLatern/_test mau film/5D2"
if [ $# -gt 0 ]; then IMGS=("$@")
elif [ $CAM = 5D2 ]; then IMGS=("$D5/100EOS5D/IMG_8380.JPG" "$D5/100EOS5D/IMG_8419.JPG" "$D5/Day 10/IMG_8505.JPG"
                                "$D5/100EOS5D/IMG_8381.JPG")
else IMGS=($D/IMG_0208.JPG $D/IMG_0233.JPG $D/IMG_0210.JPG $D/IMG_0227.JPG); fi
[ $CAM = 5D2 ] && PHD=${PHD:-"$D5/100EOS5D"} || PHD=${PHD:-$D}   # the Darkroom's photos: CR2s (RAWB28)
$PY $REPO/tests/ui/dr_mkthumbs.py thumbs "${IMGS[@]}"
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
gcc -std=gnu99 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-comment $SAN -DWF_HOST -DWF_HOST_PURE \
    -I$REPO/tests/ui -I$REPO/src -I$REPO/src/ui \
    $REPO/src/ui/wf_gfx.c $REPO/src/ui/wf_font.c $REPO/src/ui/wf_cans.c $REPO/src/ui/wf_ui.c $REPO/src/ui/wf_dark_ui.c $REPO/src/ui/wf_os_ui.c $REPO/src/ui/wf_prints_ui.c \
    $REPO/src/wf_roll.c $REPO/src/wf_photo.c $REPO/tests/ui/host_ml.c $REPO/tests/ui/fake_core.c $REPO/tests/ui/fake_dark.c \
    build/fake_catalog.c $REPO/tests/ui/dr_ui_test.c -o build/dr_ui_test
rm -f shots/*
set +e
build/dr_ui_test shots card $RBF thumbs "$PHD" 2> build/dr_ui_test.log
rc=$?
set -e
grep -v '^\[log\]' build/dr_ui_test.log || true
$PY $REPO/tests/ui/dr_mkthumbs.py --png shots
$PY $REPO/tests/ui/dr_mkthumbs.py --hud thumbs shots
ls shots
exit $rc
