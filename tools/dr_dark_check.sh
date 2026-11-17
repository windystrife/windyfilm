#!/bin/bash
# dr_dark_check.sh - Roll + Darkroom, the per-frame chain on real EOS M RAW+L pairs (docs/DESIGN.md 3.6), WindyStrife.
# WSL. Module objects with build.sh's flags (wf_cr2.o, wf_dev.o, wf_jpe.o, wf_darkp.o + the Log + LUT objects wf_dev needs)
# + their object checks, tests/host/wf_dark_test.c (qemu-arm-static): units, then per pair
#   CR2 -> decode -> levels -> as-shot WB + scene scale + ISO trim -> sRGB base develop -> q95 print with Canon's APP1,
# the print decoded by djpeg and PIL, compared with Canon's own JPEG (mean Y offset in EV, PSNR), the fit against Canon's
# frame for the trim table ("trim ISO n: .. EV"), quarter-size PNGs dev_<n>.png / canon_<n>.png.
# Then the print finishes (DESIGN 3.9, wf_print.o, tools/mk_wffont.py --print-only WFPRINT.DAT): all Off = the plain
# print byte for byte; frames (white / black, thin / wide, details, roll) and date stamps on landscape and portrait
# (Orientation 6 / 8) pairs, each print checked (tools/dr_fin_check.py: canvas size, the picture's luma unchanged, the
# border, the Exif size and orientation) and kept as out/fin_<pair>_<tag>.JPG + a quarter-size PNG as shown.
#
#   bash tools/dr_dark_check.sh [DIR] [N]     the first N pairs of DIR (default Day09 EOS M, all)
#   CAM=5D2 bash tools/dr_dark_check.sh [DIR] [N]   the 5D2 frame (wf_dk_geom; default the owner's Day 10 5D2 pairs, the
#                                             finishes on 100EOS5D pairs: landscape, Orientation 6, 8; FIN_DIR)
# Env: W (default /root/ml/wf_dr_dark), KEEP=1 keeps the prints, FIN=0 skips the finishes, LOGO_SVG = the wordmark of
# the logo runs (default tests/host/logo_test.svg). The photos and every output stay in W: never commit them.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
S=$R/src
W=${W:-/root/ml/wf_dr_dark}
CAM=${CAM:-EOSM}
export WF_DK_CAM=$CAM
if [ $CAM = 5D2 ]; then
  D=${1:-"/mnt/f/MagicLatern/_test mau film/5D2/Day 10"}
  FD=${FIN_DIR:-"/mnt/f/MagicLatern/_test mau film/5D2/100EOS5D"}
  FP="IMG_8380 IMG_8419 IMG_8381"                          # landscape, Orientation 6, Orientation 8 (ISO 200, daylight)
else
  D=${1:-/mnt/f/MagicLatern/_research/raw-path/cr2/Day09_EOSM}
  FD=${FIN_DIR:-$D}
  FP="IMG_0208 IMG_0206 IMG_0212"                          # landscape, Orientation 6, Orientation 8
fi
N=${2:-9999}
PY="python3 -I -B"
G=$W/gen
O=$W/o
mkdir -p $G $O $W/out
rm -f $W/out/*
$PY $R/tools/mk_loglut_tab.py $G/wf_loglut_tab.h > /dev/null
$PY $R/tools/mk_logtab.py $G/wf_logtab.h > /dev/null
$PY $R/tools/mk_logasm_tab.py $G/wf_logtab.h $G/wf_logasm_tab.h > /dev/null
CC=arm-none-eabi-gcc
MODFLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -fno-common
 -nostdlib -ffreestanding -fomit-frame-pointer -fno-strict-aliasing -std=gnu99 -fno-unwind-tables
 -fno-asynchronous-unwind-tables -Wall -Wno-comment -DMODULE -DMODULE_NAME=windyflm -I$S -I$G"
$CC $MODFLAGS -O2 -c -o $O/wf_cr2.o $S/wf_cr2.c
$CC $MODFLAGS -O2 -c -o $O/wf_jpe.o $S/wf_jpe.c
$CC $MODFLAGS -Werror -c -o $O/wf_darkp.o $S/wf_darkp.c
$CC $MODFLAGS -O2 -Werror -c -o $O/wf_print.o $S/wf_print.c
$CC $MODFLAGS -O2 -Werror -c -o $O/wf_dev_c.o $S/wf_dev.c
$CC -mcpu=arm946e-s -marm -c -o $O/wf_dev_arm.o $S/wf_dev_arm.S
arm-none-eabi-ld -r -o $O/wf_dev.o $O/wf_dev_c.o $O/wf_dev_arm.o
$CC $MODFLAGS -DWF_LL_EXTERN_ENC -c -o $O/wf_loglut.o $S/wf_loglut.c
$CC $MODFLAGS -O2 -DWF_LL_ASM -c -o $O/wf_llk.o $S/wf_llk.c
$CC -march=armv5t -marm -c -o $O/wf_llk_arm.o $S/wf_llk_arm.S
$CC $MODFLAGS -c -o $O/wf_logout_tab.o $S/wf_logout_tab.c
$CC -mcpu=arm946e-s -marm -c -o $O/wf_logout_arm.o $S/wf_logout_arm.S
arm-none-eabi-ld -r -o $O/wf_logout.o $O/wf_logout_tab.o $O/wf_logout_arm.o
bad=
for o in wf_cr2 wf_jpe wf_darkp wf_print; do
  arm-none-eabi-readelf -A $O/$o.o | grep -q "Tag_CPU_arch: v5T$" || bad="$bad $o(not v5T)"
  n=$(arm-none-eabi-objdump -d $O/$o.o | grep -ciE "[[:space:]](ldr|str)d[a-z]{0,2}[[:space:]]" || true)
  [ "$n" = 0 ] || bad="$bad $o(LDRD/STRD $n)"
  arm-none-eabi-nm -u $O/$o.o | grep -qE "__aeabi_(l|ul)divmod|__(u)?(div|mod)di3" && bad="$bad $o(64-bit division)"
  echo "$o.o: text $(arm-none-eabi-size $O/$o.o | awk 'NR==2{print $1}') B, imports: $(arm-none-eabi-nm -u $O/$o.o | awk '{print $2}' | tr '\n' ' ')"
done
[ -z "$bad" ] || { echo "object checks failed:$bad"; exit 1; }
arm-none-eabi-objdump -h $O/wf_darkp.o | grep -q rodata.str && $PY $R/tools/fmt_check.py $O/wf_darkp.o

# the print font pack the units and the finish runs read (DESIGN 3.9)
$PY $R/tools/mk_wffont.py $W/WFPRINT.DAT --print-only > /dev/null
HCC="arm-linux-gnueabi-gcc -O1 -marm -march=armv5te -Wall -Wno-comment -I$S -I$R/tests/host -I$G"
$HCC -c -o $O/wf_dark_test.o $R/tests/host/wf_dark_test.c
$HCC -c -o $O/wf_dev_shim.o $R/tests/host/wf_dev_shim.c
arm-linux-gnueabi-gcc -static -o $W/wf_dark_test $O/wf_dark_test.o $O/wf_dev_shim.o $O/wf_cr2.o $O/wf_jpe.o \
  $O/wf_darkp.o $O/wf_print.o $O/wf_dev.o $O/wf_loglut.o $O/wf_llk.o $O/wf_llk_arm.o $O/wf_logout.o -lm \
  -Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning -Wl,--no-warn-execstack

TP=0; TF=0
tally() { read p f < <(awk '/^RESULT/ {print $2, $4}' $1); [ -n "$p" ] || { p=0; f=1; tail -3 $1; }; TP=$((TP + p)); TF=$((TF + f)); }
( cd $W && qemu-arm-static $W/wf_dark_test units ) > $W/units.log 2>&1 || true
grep -E "^(FAIL|RESULT)" $W/units.log | sed 's/^/units: /'
tally $W/units.log

# Canon's JPEG as its UYVY frame (the L JPEG sits at 84, 64 of the CR2 in the Day09 set: dev_cr2.sh's alignment)
cat > $W/j2u.py <<'EOF'
import sys
import numpy as np
from PIL import Image
im = Image.open(sys.argv[1]); im.draft("YCbCr", im.size)
a = np.asarray(im.convert("YCbCr")).astype(np.int32)
h, w, _ = a.shape
u = ((a[:, 0::2, 1] + a[:, 1::2, 1] + 1) // 2 - 128) & 0xFF
v = ((a[:, 0::2, 2] + a[:, 1::2, 2] + 1) // 2 - 128) & 0xFF
o = (u | a[:, 0::2, 0] << 8 | v << 16 | a[:, 1::2, 0] << 24).astype("<u4")
open(sys.argv[2], "wb").write(o.tobytes())
EOF
cat > $W/cmp.py <<'EOF'
import sys, math
import numpy as np
from PIL import Image
p, c, uy, png_d, png_c = sys.argv[1:6]
a = Image.open(p); b = Image.open(c)
W, H = b.size
ok = a.size == b.size == ((5616, 3744) if W == 5616 else (5184, 3456)) and a.mode == "RGB"
ya = np.asarray(a.convert("L")).astype(np.float64); yb = np.asarray(b.convert("L")).astype(np.float64)
# the print against our own UYVY frame (the encoder) and against Canon's JPEG (the develop)
d = np.frombuffer(open(uy, "rb").read(), dtype="<u4").reshape(H, W // 2)
yd = np.empty((H, W)); yd[:, 0::2] = (d >> 8) & 255; yd[:, 1::2] = d >> 24
mse = ((ya - yd) ** 2).mean(); pe = 99.0 if mse == 0 else 10 * math.log10(255 * 255 / mse)
def lin(y): y = np.clip(y / 255, 0, 1); return np.where(y <= 0.04045, y / 12.92, ((y + 0.055) / 1.055) ** 2.4)
m = (yb > 30) & (yb < 220)
ev = math.log2(np.median(lin(ya[m])) / np.median(lin(yb[m])))
mse2 = ((ya - yb) ** 2).mean(); pc = 10 * math.log10(255 * 255 / mse2)
a.resize((W // 4, H // 4), Image.BOX).save(png_d); b.resize((W // 4, H // 4), Image.BOX).save(png_c)
print("cmp %s: decodes %s, Y vs own frame %.1f dB (encoder), vs Canon's JPEG %.1f dB, mid-tone offset %+.2f EV" % (p.split("/")[-1], ok, pe, pc, ev))
sys.exit(0 if ok and pe > 40 else 1)
EOF
k=0
for c in "$D"/*.CR2; do
  b=$(basename "$c" .CR2)
  j="$D/$b.JPG"
  [ -f "$j" ] || continue
  $PY $W/j2u.py "$j" $W/out/$b.jpeg.uyvy
  ( cd $W && qemu-arm-static $W/wf_dark_test frame "$c" "$j" $W/out $W/out/$b.jpeg.uyvy ) > $W/out/$b.log 2>&1 || true
  grep -E "^(frame|trim|print|FAIL)" $W/out/$b.log | sed "s#$D/##; s#$W/out/##"
  tally $W/out/$b.log
  if [ -f $W/out/$b.JPG ]; then
    djpeg -outfile /dev/null $W/out/$b.JPG && echo "djpeg $b: ok" || { echo "djpeg $b: FAIL"; TF=$((TF + 1)); }
    if $PY $W/cmp.py $W/out/$b.JPG "$j" $W/out/$b.dev.uyvy $W/out/dev_$b.png $W/out/canon_$b.png; then TP=$((TP + 1)); else TF=$((TF + 1)); fi
  fi
  rm -f $W/out/$b.jpeg.uyvy $W/out/$b.dev.uyvy
  [ "${KEEP:-0}" = 1 ] || rm -f $W/out/$b.JPG
  k=$((k + 1))
  [ $k -ge $N ] && break
done
# the print finishes: TAG FRAME INFO DATE on pairs of the set (WF_PFR_* / WF_PIN_* / WF_PDT_*)
if [ "${FIN:-1}" = 1 ]; then
  LOOK="WindyFilm: Portra 400 | Print Cine | Strength 100% | Grain Normal | Halation Film | Bloom Low | Mist 1/8"
  fin() {   # fin PAIR TAG FRAME INFO DATE [ROLL|-] [LOGO|-] [MODEL]
    local b=$1 tag=$2 c="$FD/$1.CR2" j="$FD/$1.JPG"
    [ -f "$c" ] && [ -f "$j" ] || { echo "fin $b: not in $FD"; return; }
    ( cd $W && qemu-arm-static $W/wf_dark_test finish "$c" "$j" $W/out $tag $3 $4 $5 $W/WFPRINT.DAT "$LOOK" "${6:--}" \
        "${7:--}" "${8:--}" ) > $W/out/fin_${b}_$tag.log 2>&1 || true
    grep -E "^(finish|FAIL)" $W/out/fin_${b}_$tag.log | sed "s#$FD/##; s#$W/out/##; s/^/fin $b $tag: /"
    tally $W/out/fin_${b}_$tag.log
    [ -f $W/out/${b}_$tag.JPG ] || { TF=$((TF + 1)); return; }
    rm -f $W/out/${b}_$tag.dev.uyvy $W/out/${b}_$tag.print.l8 $W/out/${b}_$tag.wire.l8
    mv $W/out/${b}_$tag.JPG $W/out/fin_${b}_$tag.JPG
    djpeg -outfile /dev/null $W/out/fin_${b}_$tag.JPG || { echo "djpeg fin_${b}_$tag: FAIL"; TF=$((TF + 1)); }
    if $PY $R/tools/dr_fin_check.py $W/out/fin_${b}_$tag.JPG $W/out/fin_${b}_off.JPG "$j" $(awk '/^canvas/ {print $2, $3, $4, $5, $6}' \
         $W/out/fin_${b}_$tag.log) $3 $5 $W/out/fin_${b}_$tag.png $([ "${7:--}" = - ] || echo logo); then TP=$((TP + 1))
    else TF=$((TF + 1)); fi
  }
  set -- $FP
  P1=$1 P2=$2 P3=$3
  for b in $FP; do                                       # landscape, Orientation 6, Orientation 8
    [ -f "$FD/$b.CR2" ] || continue
    ( cd $W && qemu-arm-static $W/wf_dark_test frame "$FD/$b.CR2" "$FD/$b.JPG" $W/out ) > $W/out/fin_${b}_plain.log 2>&1 || true
    ( cd $W && qemu-arm-static $W/wf_dark_test finish "$FD/$b.CR2" "$FD/$b.JPG" $W/out off 0 1 0 $W/WFPRINT.DAT "$LOOK" - ) \
      > $W/out/fin_${b}_off.log 2>&1 || true
    if [ -f $W/out/$b.JPG ] && cmp -s $W/out/$b.JPG $W/out/${b}_off.JPG; then
      echo "fin $b: frame Off + date Off = the plain print, byte for byte ($(stat -c %s $W/out/$b.JPG) B): ok"; TP=$((TP + 1))
    else
      echo "fin $b: frame Off + date Off differs from the plain print: FAIL"; TF=$((TF + 1))
    fi
    mv -f $W/out/${b}_off.JPG $W/out/fin_${b}_off.JPG
    rm -f $W/out/$b.JPG $W/out/$b.dev.uyvy $W/out/$b.print.l8 $W/out/$b.wire.l8 $W/out/${b}_off.*.l8 $W/out/${b}_off.dev.uyvy
  done
  fin $P1 white_info 1 2 0 "ROLL 021 $(printf '\x7f') 07/24"
  fin $P1 black 2 0 0
  fin $P1 whitew_info_date 3 1 1
  fin $P1 date 0 1 2
  fin $P2 blackw_info_date 4 1 3 "ROLL 021 $(printf '\x7f') 01/24"
  fin $P2 white_info 1 1 0
  fin $P3 date 0 1 1
  fin $P3 white_info_date 1 2 1 "ROLL 021 $(printf '\x7f') 07/24"
  # the logo (owner 11-16): a pack from LOGO_SVG (default our own test shape keyed "Canon"; the owner's wordmark only from
  # the private folder, never committed); two with the 5D2's name (on CAM=5D2: its own frame, DESIGN 3.16)
  $PY $R/tools/mk_wflogo.py "${LOGO_SVG:-$R/tests/host/logo_test.svg}" $W/WFLOGO.DAT --maker Canon > /dev/null
  fin $P1 logo_white_info 1 2 2 "ROLL 021 $(printf '\x7f') 07/24" $W/WFLOGO.DAT
  fin $P1 logo_blackw_info 4 1 0 - $W/WFLOGO.DAT
  fin $P2 logo_white_info 1 1 1 - $W/WFLOGO.DAT
  fin $P3 logo_black 2 0 0 - $W/WFLOGO.DAT
  fin $P1 logo5d2_white_info 1 2 0 "ROLL 021 $(printf '\x7f') 07/24" $W/WFLOGO.DAT "Canon EOS 5D Mark II"
  fin $P1 logo5d2_black_info 2 1 2 - $W/WFLOGO.DAT "Canon EOS 5D Mark II"
  rm -f $W/out/*.dev.uyvy $W/out/*_*.print.l8 $W/out/*_*.wire.l8
fi

echo "trim summary (fit vs scene scale per ISO, EV):"
grep -h "^trim ISO" $W/out/*.log | awk '{iso=$3; sub(":","",iso); print iso, $8}' | sort -n | awk '
  {v[$1]=v[$1] " " $2; n[$1]++} END {for (i in v) print "  ISO " i ": n " n[i] ":" v[i]}' | sort -t' ' -k3 -n
echo "dr_dark_check: $k pairs, RESULT $TP / $TF (logs and PNGs in $W/out)"
[ $TF = 0 ]
