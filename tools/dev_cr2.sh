#!/bin/bash
# dev_cr2.sh - the RAW develop "B" on real EOS M RAW+L pairs (WindyStrife). Run in WSL after bash tools/dev_check.sh
# (it uses $W/wf_dev_test and the module objects in $W/o/EOSM). The photos and everything this writes stay outside the
# repository: never commit them.
#
#   bash tools/dev_cr2.sh DIR [N]     the first N pairs IMG_xxxx.CR2 + IMG_xxxx.JPG of DIR (default all)
# Per pair, in $W/cr2/IMG_xxxx: tools/cr2_dec (raw.r14, meta.txt with the measured clip pile-up), tools/jpg_uyvy.py at
# the JPEG's window (84, 64: align.txt must say 0 0), $W/dev_a1ref (ref_a1.uyvy + dec.bin, when the tree has A1),
# wf_dev_test cr2 (cr2.log), quarter-size PNGs dev_lut / ref_a1 / ref_lut / dev_srgb / jpeg; the big files are deleted
# afterwards (KEEP=1 keeps them). Summary lines on stdout.
# A1 (the Canon input curves: wf_ll_incurve in src/wf_loglut.c, luts/canon_curve/EOSM_Film1.csv + EOSM_Neutral.csv):
# dev_a1ref is built from this tree's own Log + LUT sources with both curve slots. A1SLOT = the slot the pairs were shot
# for (1 Canon Film 1 = Picture Style User Def. 1 with Film 1, the default; 2 Canon Neutral; 0 = no A1 reference).
# Env: W (default /root/ml/wf_rawdev), X0 Y0 (default 84 64), A1SLOT, KEEP.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
S=$R/src
W=${W:-/root/ml/wf_rawdev}
D=$1
N=${2:-9999}
X0=${X0:-84}
Y0=${Y0:-64}
PY="python3 -I -B"
[ -d "$D" ] && [ -x $W/wf_dev_test ] || { echo "usage: dev_cr2.sh DIR [N] (after bash tools/dev_check.sh)"; exit 2; }
gcc -O2 -w -o $W/cr2_dec $R/tools/cr2_dec.c
A1=0
C1=$R/luts/canon_curve/EOSM_Film1.csv
C2=$R/luts/canon_curve/EOSM_Neutral.csv
if [ "${A1SLOT:-1}" != 0 ] && grep -q wf_ll_incurve $S/wf_loglut.c && [ -f $C1 ] && [ -f $C2 ]; then
  # the module's Log + LUT objects with both curve slots (module flags, as build.sh) + dev_a1ref, scene, shim
  G=$W/gen_a1
  O=$W/o/a1
  mkdir -p $G $O
  $PY $R/tools/mk_loglut_tab.py $G/wf_loglut_tab.h $C1 $C2 > /dev/null
  $PY $R/tools/mk_logtab.py $G/wf_logtab.h > /dev/null
  $PY $R/tools/mk_logasm_tab.py $G/wf_logtab.h $G/wf_logasm_tab.h > /dev/null
  CC=arm-none-eabi-gcc
  MODFLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -fno-common
   -nostdlib -ffreestanding -fomit-frame-pointer -fno-strict-aliasing -std=gnu99 -fno-unwind-tables
   -fno-asynchronous-unwind-tables -Wall -Wno-comment -DMODULE -DMODULE_NAME=windyflm -I$S -I$G"
  $CC $MODFLAGS -DWF_LL_EXTERN_ENC -c -o $O/wf_loglut.o $S/wf_loglut.c
  $CC $MODFLAGS -O2 -DWF_LL_ASM -c -o $O/wf_llk.o $S/wf_llk.c
  $CC -march=armv5t -marm -c -o $O/wf_llk_arm.o $S/wf_llk_arm.S
  $CC $MODFLAGS -c -o $O/wf_logout_tab.o $S/wf_logout_tab.c
  $CC -mcpu=arm946e-s -marm -c -o $O/wf_logout_arm.o $S/wf_logout_arm.S
  HCC="arm-linux-gnueabi-gcc -O1 -marm -march=armv5te -w -I$S -I$G -I$R/tests/host"
  $HCC -c -o $O/dev_a1ref.o $R/tools/dev_a1ref.c
  $HCC -c -o $O/scene.o $R/tests/host/wf_dev_scene.c
  $HCC -c -o $O/shim.o $R/tests/host/wf_dev_shim.c
  arm-linux-gnueabi-gcc -static -o $W/dev_a1ref $O/dev_a1ref.o $O/scene.o $O/shim.o $O/wf_loglut.o $O/wf_llk.o \
    $O/wf_llk_arm.o $O/wf_logout_tab.o $O/wf_logout_arm.o -lm -Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning \
    -Wl,--no-warn-execstack
  A1=${A1SLOT:-1}
  echo "dev_cr2: A1 reference with curve slot $A1"
else
  echo "dev_cr2: no A1 in this tree (or A1SLOT=0): the develop is compared with the sRGB-decoded JPEG path only"
fi
k=0
for c in "$D"/*.CR2; do
  b=$(basename "$c" .CR2)
  j="$D/$b.JPG"
  [ -f "$j" ] || continue
  o=$W/cr2/$b
  rm -rf "$o"; mkdir -p "$o"
  $W/cr2_dec "$c" "$o" > "$o/dec.txt"
  $PY $R/tools/jpg_uyvy.py "$j" "$o" $X0 $Y0 > "$o/align.log"
  read jw jh < <(awk '{print $4, $5}' "$o/align.txt")
  [ $A1 != 0 ] && { ( cd "$o" && qemu-arm-static $W/dev_a1ref "$o" $A1 $jw $jh ) > "$o/a1.log" 2>&1 || true; }
  ( cd "$o" && qemu-arm-static $W/wf_dev_test cr2 "$o" $X0 $Y0 ) > "$o/cr2.log" 2>&1 || true
  echo "== $b: $(sed 's/.*offset/offset/' "$o/align.log")"
  grep -E "^(cr2|FAIL|RESULT)" "$o/cr2.log" | sed "s#$o#$b#"
  for f in dev_lut ref_a1 ref_lut dev_srgb jpeg; do
    [ -f "$o/$f.uyvy" ] && $PY $R/tools/jpg_uyvy.py --png "$o/$f.uyvy" $jw $jh "$o/$f.png" 4
  done
  [ "${KEEP:-0}" = 1 ] || rm -f "$o/raw.r14" "$o"/*.uyvy
  k=$((k + 1))
  [ $k -ge $N ] && break
done
echo "dev_cr2: $k pairs, logs and PNGs in $W/cr2"
