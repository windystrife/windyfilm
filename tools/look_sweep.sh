#!/bin/bash
# look_sweep.sh - every film and Film LUT can on real EOS M RAW frames (WindyStrife), all from the CR2: the Darkroom's own
# develop (tests/host/wf_dark_test.c devset, full size: the sRGB base for the films, each can's own cube for that can),
# the Darkroom's look stage with the film's default finish (test.sh LOOK:sweep with the real pack: grain, halation, bloom
# as the camera prints them; the Bloom and Mist sets), then the gallery (tools/look_sweep.py). The camera's JPEGs are
# never a look or a "before" (Exif captions only). WSL, after build.sh for the EOS M.
#
#   bash tools/look_sweep.sh MODE WORK OUT CARD FX_FILM FRAME...
#     MODE films (every film and can) or stage (the film-stage chain films x their prints, DESIGN 1a: private);
#     WORK  work dir (frames, PPMs, logs); OUT the gallery; CARD a card's ML/DATA (the cans' cubes, only read);
#     FX_FILM the film id of the effect strips; FRAME = NAME=CR2@EV8@TAGS@X,Y (tools/look_sweep.py lists)
# Env: STEP = dev | look | gallery (one step only), KEEP=1 keeps the full-size develops (dev/*.uyvy, ~36 MB each),
#      PACK = the WFFILMS.DAT the looks use (default build/eosm/WFFILMS.DAT; stage: a PACKS=1 card's, the chain tables).
# The photos and everything written stay in WORK and OUT, outside the repository: never commit them.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
S=$R/src
MODE=$1; W=$2; OUT=$3; CARD=$4; FXF=$5
shift 5
[ -n "$FXF" ] && [ -d "$CARD" ] && { [ "$MODE" = films ] || [ "$MODE" = stage ]; } || { echo "usage: look_sweep.sh films|stage WORK OUT CARD FX_FILM FRAME..."; exit 2; }
PACK=${PACK:-$R/build/eosm/WFFILMS.DAT}
PY="python3 -I -B"
mkdir -p $W/o $W/gen $W/dev $W/out $OUT
$PY $R/tools/look_sweep.py lists $MODE $W $CARD $FXF "$@"

if [ -z "$STEP" ] || [ "$STEP" = dev ]; then
  G=$W/gen; O=$W/o
  $PY $R/tools/mk_loglut_tab.py $G/wf_loglut_tab.h > /dev/null
  $PY $R/tools/mk_logtab.py $G/wf_logtab.h > /dev/null
  $PY $R/tools/mk_logasm_tab.py $G/wf_logtab.h $G/wf_logasm_tab.h > /dev/null
  CC=arm-none-eabi-gcc
  MODFLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -fno-common
   -nostdlib -ffreestanding -fomit-frame-pointer -fno-strict-aliasing -std=gnu99 -fno-unwind-tables
   -fno-asynchronous-unwind-tables -Wall -Wno-comment -DMODULE -DMODULE_NAME=windyflm -I$S -I$G"
  $CC $MODFLAGS -O2 -c -o $O/wf_cr2.o $S/wf_cr2.c
  $CC $MODFLAGS -O2 -c -o $O/wf_jpe.o $S/wf_jpe.c
  $CC $MODFLAGS -c -o $O/wf_darkp.o $S/wf_darkp.c
  $CC $MODFLAGS -O2 -c -o $O/wf_print.o $S/wf_print.c
  $CC $MODFLAGS -O2 -c -o $O/wf_dev_c.o $S/wf_dev.c
  $CC -mcpu=arm946e-s -marm -c -o $O/wf_dev_arm.o $S/wf_dev_arm.S
  arm-none-eabi-ld -r -o $O/wf_dev.o $O/wf_dev_c.o $O/wf_dev_arm.o
  $CC $MODFLAGS -DWF_LL_EXTERN_ENC -c -o $O/wf_loglut.o $S/wf_loglut.c
  $CC $MODFLAGS -O2 -DWF_LL_ASM -c -o $O/wf_llk.o $S/wf_llk.c
  $CC -march=armv5t -marm -c -o $O/wf_llk_arm.o $S/wf_llk_arm.S
  $CC $MODFLAGS -c -o $O/wf_logout_tab.o $S/wf_logout_tab.c
  $CC -mcpu=arm946e-s -marm -c -o $O/wf_logout_arm.o $S/wf_logout_arm.S
  arm-none-eabi-ld -r -o $O/wf_logout.o $O/wf_logout_tab.o $O/wf_logout_arm.o
  HCC="arm-linux-gnueabi-gcc -O1 -marm -march=armv5te -Wall -Wno-comment -I$S -I$R/tests/host -I$G"
  $HCC -c -o $O/wf_dark_test.o $R/tests/host/wf_dark_test.c
  $HCC -c -o $O/wf_dev_shim.o $R/tests/host/wf_dev_shim.c
  arm-linux-gnueabi-gcc -static -o $W/wf_dark_test $O/wf_dark_test.o $O/wf_dev_shim.o $O/wf_cr2.o $O/wf_jpe.o \
    $O/wf_darkp.o $O/wf_print.o $O/wf_dev.o $O/wf_loglut.o $O/wf_llk.o $O/wf_llk_arm.o $O/wf_logout.o -lm \
    -Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning -Wl,--no-warn-execstack
  t0=$(date +%s)
  ( cd $W && qemu-arm-static $W/wf_dark_test devset $W/devset.txt $W/dev ) > $W/dev.log 2>&1 || true
  echo "dev: $(grep -c '^develop ' $W/dev.log) develops in $(( $(date +%s) - t0 )) s, $(grep -E '^(FAIL|RESULT)' $W/dev.log | tail -3 | tr '\n' ' ')"
fi

if [ -z "$STEP" ] || [ "$STEP" = look ]; then
  t0=$(date +%s)
  ( cd $R && CAM=EOSM WF_LOOK_PACK=$PACK WF_LOOK_JOBS=$W/jobs.txt WF_LOOK_CUBES=$CARD bash test.sh LOOK:sweep ) > $W/look.log 2>&1 || true
  cp $R/build/eosm/test/LOOK_sweep.log $W/look_detail.log 2>/dev/null || true
  echo "look: $(grep -c '^look ' $W/look_detail.log) shots in $(( $(date +%s) - t0 )) s, $(grep -E 'checks passed|FAIL' $W/look.log | head -3 | tr '\n' ' ')"
fi

if [ -z "$STEP" ] || [ "$STEP" = gallery ]; then
  $PY $R/tools/look_sweep.py gallery $W $OUT
  if [ "$KEEP" != 1 ]; then
    rm -f $W/dev/*.uyvy
  fi
fi
