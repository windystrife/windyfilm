#!/bin/bash
# run.sh - build and run the src/ui host harness in WSL; screenshots + comparison sheets in $W/shots.
#   bash tests/ui/run.sh [CANS.DAT]        (any checkout: REPO is this script's own tree)
# Work dir W (default /root/ml/wf_uidev). CANS.DAT defaults to the data side's pack /root/ml/wf_data/out/WFCANS.DAT.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
W=${W:-/root/ml/wf_uidev}
MOCK=/mnt/c/Users/win11/AppData/Local/Claude-Data/claude_pzs8sxrjxfjjc/side-session-notes/local_7f7b9268-a975-405a-8e0b-94190bc0da8c/newmodule-mockups
RBF=/root/ml/wf_ui/fonts
CANS=${1:-/root/ml/wf_data/out/WFCANS.DAT}
mkdir -p $W
cd $W
mkdir -p build out shots card/ML/DATA card_nocans/ML/DATA card_empty/ML/DATA
[ -f out/WFFONTS.DAT ] || python3 -I -B $REPO/tools/mk_wffont.py out/WFFONTS.DAT
python3 -I -B $REPO/tests/ui/mk_fakecat.py build/fake_catalog.c
cp out/WFFONTS.DAT card/ML/DATA/
cp out/WFFONTS.DAT card_nocans/ML/DATA/
rm -f card_nocans/ML/DATA/WFCANS.DAT card_empty/ML/DATA/*
cp "$CANS" card/ML/DATA/WFCANS.DAT
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
gcc -std=gnu99 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-comment $SAN -DWF_HOST -DWF_HOST_PURE \
    -I$REPO/tests/ui -I$REPO/src -I$REPO/src/ui \
    $REPO/src/ui/wf_gfx.c $REPO/src/ui/wf_font.c $REPO/src/ui/wf_cans.c $REPO/src/ui/wf_ui.c $REPO/src/ui/wf_dark_ui.c \
    $REPO/src/wf_roll.c $REPO/tests/ui/host_ml.c $REPO/tests/ui/fake_core.c $REPO/tests/ui/fake_dark.c \
    build/fake_catalog.c $REPO/tests/ui/ui_test.c \
    -o build/ui_test
rm -f shots/*
set +e
build/ui_test shots card card_nocans $RBF card_empty 2> build/ui_test.log
rc=$?
set -e
grep -v '^\[log\]' build/ui_test.log || true
grep '^\[log\] ui: open' build/ui_test.log | head -2
python3 -I -B $REPO/tests/ui/compare.py shots $MOCK > /dev/null
ls shots
exit $rc
