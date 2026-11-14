#!/bin/bash
# build.sh - WindyFilm module build (WindyStrife): build/<cam>/windyflm.mo (+ the card packs when their tools run).
#
#   CAM=EOSM bash build.sh        (default)   Canon EOS M 2.0.2
#   CAM=5D2  bash build.sh                    Canon EOS 5D Mark II 2.1.2
#   bash build.sh CAM=5D2                     same, as an argument
#   STAGE=1 CAM=EOSM bash build.sh            the film-stage build: windyfls.mo, its own menu entry "Film Stage", its
#                                             own WINDYFLS.CFG / WINDYFLS.LOG and a rose accent (src/wf_build.h), so
#                                             it installs beside windyflm.mo and is never mistaken for it. Output goes to
#                                             build/<cam>-stage unless OUT says otherwise.
# Env: OUT = output dir (default <repo>/build/<cam>, ignored by git), PACKS=0 skips the card packs,
#      CURVE_FILM1 / CURVE_NEUTRAL = other input-curve CSVs (default luts/canon_curve/EOSM_Film1.csv / EOSM_Neutral.csv).
# Run in WSL (arm-none-eabi, arm-linux-gnueabi, qemu-arm-static, python3). Steps:
#   1. generated headers (log / Log + LUT / preview exposure tables, ML module strings) and the catalogue
#      (films/catalog.py -> wf_catalog.c)
#   2. compile: src/*.c src/*.S (one camera layer), src/ui/*.c (src/wf_ui_stub.c when src/ui/wf_ui.c is not there),
#      the catalogue; checks: CPU arch tags, no LDRD / STRD, no 64-bit division helper
#   3. ld -r, strip debug, objcopy --keep-global-symbols (only ML's __module_*_<module> stay global); printf format
#      check (tools/fmt_check.py: only %d %x %X %s, Canon's vsnprintf has no %u / %lu / %zu / %+d and breaks %c)
#   4. checksyms against the camera's card .sym; TCC link check with the camera's own loader object (image size)
#   5. card packs (tools/mk_wfpack.py, mk_wfcans.py, mk_wffont.py; a failing pack tool is reported, not fatal)
#   6. provenance gate (tools/provenance_gate.py): any finding fails the build (check 5: card files are made / removed /
#      renamed only through src/wf_fio.h)
set -e
for a in "$@"; do case $a in CAM=*) CAM=${a#CAM=} ;; STAGE=*) STAGE=${a#STAGE=} ;; *) echo "unknown argument $a"; exit 2 ;; esac; done
CAM=${CAM:-EOSM}
# the module this build produces: windyflm (catalogue) or windyfls (film stage, src/wf_build.h)
if [ "${STAGE:-0}" = 1 ]; then MN=windyfls; MNAME="WindyFilm Stage"; MCFG=ML/SETTINGS/WINDYFLS.CFG
  MLOG=ML/LOGS/WINDYFLS.LOG; MSUM="Film stage: the same engine, chain-derived film tables (declared)"
  STAGEDEF=-DWF_STAGE; SUF=-stage
else MN=windyflm; MNAME="WindyFilm"; MCFG=ML/SETTINGS/WINDYFLM.CFG; MLOG=ML/LOGS/WINDYFLM.LOG
  MSUM="Film looks at the shot (JPEG)"; STAGEDEF=; SUF=
fi
R=$(cd "$(dirname "$0")" && pwd)
S=$R/src
case $CAM in
  EOSM) ML=/root/ml/expsim/mls; PLAT=EOSM.202; SYM=/mnt/f/MagicLatern/EOSM_FilmRoll/ML/modules/EOSM_202.sym
        SYMMD5=09ccc47d0d386b04d1b28cce27d9a1e1; CAMDEF=; CAMSRC=wf_cam_eosm.c ;;
  5D2)  ML=/root/ml/q5d2/ml; PLAT=5D2.212; SYM=/mnt/f/MagicLatern/5D2_FilmRoll_A13/ML/modules/5D2_212.sym
        SYMMD5=aa702ecc013bac65af03150043ea64ca; CAMDEF=-DWF_BUILD_5D2; CAMSRC=wf_cam_5d2.c ;;
  *) echo "CAM must be EOSM or 5D2"; exit 2 ;;
esac
TCCLIB=$ML/platform/$PLAT/build/tcc/libtccx.o     # the camera's own TCC loader object (ML build of that card)
cam=$(echo $CAM | tr A-Z a-z)
B=${OUT:-$R/build/$cam$SUF}
O=$B/obj
G=$O/gen
PY="python3 -I -B"
rm -rf $O
mkdir -p $O $G/build
[ "$(md5sum < $SYM | cut -c1-32)" = $SYMMD5 ] || { echo "card sym $SYM: unexpected md5 (the card's autoexec + sym pair)"; exit 1; }
echo "== $MNAME $CAM ($MN): ML headers $ML, card sym $(basename $SYM) (md5 ${SYMMD5:0:8}), out $B"

# ---------------------------------------------------------------- 1. generated sources
$PY $R/tools/mk_logtab.py $G/wf_logtab.h
$PY $R/tools/mk_logasm_tab.py $G/wf_logtab.h $G/wf_logasm_tab.h
# input curves (menu "Input curve", DESIGN 3.2 A1): slot 1 Canon Film 1, slot 2 Canon Neutral; a missing CSV = no slot
CURVE_FILM1=${CURVE_FILM1:-$R/luts/canon_curve/EOSM_Film1.csv}
CURVE_NEUTRAL=${CURVE_NEUTRAL:-$R/luts/canon_curve/EOSM_Neutral.csv}
$PY $R/tools/mk_loglut_tab.py $G/wf_loglut_tab.h $([ -f $CURVE_FILM1 ] && echo $CURVE_FILM1 || echo -) \
  $([ -f $CURVE_NEUTRAL ] && echo $CURVE_NEUTRAL || echo -)
$PY $R/tools/mk_exptab.py $G/wf_exptab.h
# the four film-stage films (films/catalog.py 85..88) are declared Genesis-derived card data and are NOT in the repo
# (DESIGN 1a), so a checkout makes them here, from the port tree, before the catalogue and the pack are built. Without
# the tree (a plain clone) the films are simply not on the card: mk_wfpack.py skips a table that is not there.
ngx=$(ls $R/films/tables/GX500T.T17 $R/films/tables/GX250D.T17 $R/films/tables/GXP400.T17 \
        $R/films/tables/GXE100.T17 2>/dev/null | wc -l)
if [ "$ngx" != 4 ]; then
  if $PY $R/tools/mk_genesis_films.py stage $R/films/tables > $O/gen_tables.log 2>&1; then
    echo "film-stage tables: $ngx of 4 were there, now $(ls $R/films/tables/GX*.T17 | wc -l)"
  else
    echo "film-stage tables: NOT made ($(tail -2 $O/gen_tables.log | tr '\n' ' ')); those films stay off the card"
  fi
fi
cat > $G/build/module_strings.h <<EOF
/* generated by build.sh: the strings ML's Modules menu shows */
#define MODULE_STRINGS() \\
  MODULE_STRINGS_START() \\
    MODULE_STRING("Name", "$MNAME") \\
    MODULE_STRING("Author", "WindyStrife") \\
    MODULE_STRING("Summary", "$MSUM") \\
    MODULE_STRING("Description", "Film stocks, Log and Log + LUT outputs, grain, halation and mist applied to each JPEG in camera. Settings: $MCFG, log: $MLOG.") \\
    MODULE_STRING("Camera", "$CAM") \\
    MODULE_STRING("Build date", "$(date -u +%Y-%m-%d)") \\
  MODULE_STRINGS_END()
EOF
$PY $R/tools/mk_catalog_c.py $B/wf_catalog.c

# ---------------------------------------------------------------- 2. compile
MODFLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -fno-common
 -nostdlib -ffreestanding -fomit-frame-pointer -fno-strict-aliasing -std=gnu99 -fno-unwind-tables
 -fno-asynchronous-unwind-tables -Wall -Wno-comment -DMODULE -DMODULE_NAME=$MN $STAGEDEF $CAMDEF -I$S -I$G"
MLFLAGS="-D__ARM__ -I$ML/include -I$ML/src -I$ML/platform/$PLAT -DCONFIG_MAGICLANTERN=1 -DCONFIG_DEBUGMSG=
 -Wno-unused-parameter"
CC=arm-none-eabi-gcc
cc() { local o=$1; shift; $CC "$@" -c -o $O/$o; OBJS="$OBJS $o"; }
OBJS=
cc wf_main.o     $MODFLAGS $MLFLAGS $S/wf_main.c
cc wf_menu.o     $MODFLAGS $MLFLAGS $S/wf_menu.c
cc wf_shot.o     $MODFLAGS $S/wf_shot.c
cc wf_pv.o       $MODFLAGS $S/wf_pv.c
cc wf_cam.o      $MODFLAGS $S/$CAMSRC
cc wf_probe.o    $MODFLAGS $S/wf_probe.c
cc wf_loglut.o   $MODFLAGS -DWF_LL_EXTERN_ENC $S/wf_loglut.c
cc wf_llk.o      $MODFLAGS -O2 -DWF_LL_ASM $S/wf_llk.c
cc wf_llk_arm.o  -march=armv5t -marm $S/wf_llk_arm.S
cc wf_film.o     $MODFLAGS -O2 $S/wf_film.c
cc yh_halo.o     $MODFLAGS -O2 -DYH_BAKED $S/yh_halo.c
cc wf_mist.o     $MODFLAGS -O2 -DYH_BAKED $S/wf_mist.c
cc yg_grain.o    $MODFLAGS -O2 $S/yg_grain.c
cc yg_stocks.o   $MODFLAGS $S/yg_stocks.c
cc wf_fx.o       $MODFLAGS $S/wf_fx.c
cc wf_expsim.o   $MODFLAGS $MLFLAGS $S/wf_expsim.c
[ $CAM = EOSM ] && cc wf_expk.o $MODFLAGS -O2 $S/wf_expk.c     # 5D2: wf_expsim.c has the no-op wf_exp_words
cc wf_catalog.o  $MODFLAGS $B/wf_catalog.c
# log outputs: tables at the module flags, the kernel in ARMv5TE asm; one object (lenc_layout below)
$CC $MODFLAGS -c -o $O/wf_logout_tab.o $S/wf_logout_tab.c
$CC -mcpu=arm946e-s -marm -c -o $O/wf_logout_arm.o $S/wf_logout_arm.S
arm-none-eabi-ld -r -o $O/wf_logout.o $O/wf_logout_tab.o $O/wf_logout_arm.o
OBJS="$OBJS wf_logout.o"
# browser UI (src/ui, the UI side's files) or the stub
if [ -f $S/ui/wf_ui.c ]; then
  UI="ui (src/ui)"
  for f in $S/ui/*.c; do
    cc ui_$(basename $f .c).o $MODFLAGS $MLFLAGS -I$S/ui -Wno-comment -Wno-unused-function $f
  done
else
  UI="stub (src/wf_ui_stub.c)"
  cc wf_ui_stub.o $MODFLAGS $S/wf_ui_stub.c
fi
echo "compiled:$OBJS; browser: $UI"
cd $O
# lenc_layout OBJ: wf_logout_arm.S reaches wf_lenc[k][idx] as LDRH [B, 2 (idx + t)] with B = wf_lasm + 1024 and a link-time
# distance: both in one section, L = wf_lenc - B even and -65536 <= L <= 65534 - 2 (5 * 4097 - 1) = 24566
lenc_layout() {
  local s
  s=$(arm-none-eabi-readelf -sW $1 | awk '($8=="wf_lenc" || $8=="wf_lasm") {print $8, $2, $3, $7}' | sort)
  set -- $1 $s
  [ "$2" = wf_lasm ] && [ "$6" = wf_lenc ] && [ $# = 9 ] || { echo "$1: wf_lasm / wf_lenc not found: $s"; exit 1; }
  local L=$(( 16#$7 - 16#$3 - 1024 )) HI=$(( 65534 - 2 * (5 * 4097 - 1) ))
  [[ $5 =~ ^[0-9]+$ ]] && [ "$5" = "$9" ] || { echo "$1: wf_lasm (ndx $5) and wf_lenc (ndx $9) not in one section"; exit 1; }
  [ "$8" = 40970 ] || { echo "$1: wf_lenc size $8, the asm row stride needs 5 x 8194"; exit 1; }
  [ $(( L % 2 )) = 0 ] && [ $L -ge -65536 ] && [ $L -le $HI ] || { echo "$1: wf_lenc - (wf_lasm + 1024) = $L, needs even -65536..$HI"; exit 1; }
  echo "$1: wf_lenc - (wf_lasm + 1024) = $L (even, -65536..$HI, one section): OK"
}
lenc_layout wf_logout.o
bad=
for o in $OBJS; do
  case $o in wf_logout.o) arch=v5TE ;; *) arch=v5T ;; esac
  arm-none-eabi-readelf -A $o | grep -q "Tag_CPU_arch: $arch$" || bad="$bad $o(not $arch)"
  n=$(arm-none-eabi-objdump -d $o | grep -ciE "[[:space:]](ldr|str)d[a-z]{0,2}[[:space:]]" || true)
  [ "$n" = 0 ] || bad="$bad $o(LDRD/STRD $n)"
  arm-none-eabi-nm -u $o | grep -qE "__aeabi_(l|ul)divmod|__(u)?(div|mod)di3" && bad="$bad $o(64-bit division)"
done
[ -z "$bad" ] || { echo "object checks failed:$bad"; exit 1; }
echo "objects: v5T (wf_logout.o v5TE), 0 LDRD/STRD, no 64-bit division helper"
UD=$(for o in $OBJS; do arm-none-eabi-nm -u $o | awk -v o=$o '$2 ~ /^__aeabi_(ui|i)div/ {print o":"$2}'; done | tr '\n' ' ')
echo "32-bit division helpers: ${UD:-none}"

# ---------------------------------------------------------------- 3. link
arm-none-eabi-ld -r -o ${MN}_r.o $OBJS
arm-none-eabi-strip --strip-debug ${MN}_r.o
lenc_layout ${MN}_r.o
arm-none-eabi-nm ${MN}_r.o | awk -v mn=$MN '$2 ~ /^[A-TV-Z]$/ && index($3, "__module_") == 1 && $3 ~ ("_" mn "$") {print $3}' > keep.txt
grep -qx __module_info_$MN keep.txt || { echo "no __module_info_$MN"; exit 1; }
arm-none-eabi-objcopy --keep-global-symbols=keep.txt ${MN}_r.o $B/$MN.mo
echo "global: $(tr '\n' ' ' < keep.txt)"
arm-none-eabi-size -A $B/$MN.mo | awk '$1 ~ /^\./ && $2 > 0 {printf "  %-18s %7d\n", $1, $2}'
# printf formats: Canon's vsnprintf has no %u / %lu / %zu / %+d / floats and prints %c wrong (tools/fmt_check.py)
$PY $R/tools/fmt_check.py $B/$MN.mo || { echo "format check FAILED (tools/fmt_check.py)"; exit 1; }

# ---------------------------------------------------------------- 4. checksyms + TCC link check
$PY $R/tools/wf_checksyms.py $SYM $B/$MN.mo $MN --tcc-syms tcc_syms.c
arm-linux-gnueabi-gcc -O2 -marm -march=armv5te -static -w -fno-builtin -o tcc_link $R/tools/tcc_link.c tcc_syms.c $TCCLIB -lm
qemu-arm-static ./tcc_link $B/$MN.mo $MN > tcc.log 2>&1 || { cat tcc.log; echo "TCC link check FAILED"; exit 1; }
grep "^\[tcc\]" tcc.log
IMG=$(awk '/^\[tcc\] linked: image/{print $4}' tcc.log)
grep -q "__module_info_$MN: .* -> OK" tcc.log || { echo "TCC: module_info not OK"; exit 1; }
[ "$IMG" -lt 524288 ] && echo "image $IMG B (target < 524288 B): OK" || echo "WARNING: image $IMG B is over the 0.5 MB target"

# ---------------------------------------------------------------- 5. card packs
if [ "${PACKS:-1}" = 1 ]; then
  for p in "mk_wfpack.py WFFILMS.DAT" "mk_wfcans.py WFCANS.DAT" "mk_wffont.py WFFONTS.DAT"; do
    set -- $p
    if $PY $R/tools/$1 $B/$2 > pack_$2.log 2>&1; then echo "pack $2: $(stat -c %s $B/$2) B"
    else echo "pack $2: NOT built ($1 failed, see $O/pack_$2.log)"; rm -f $B/$2; fi
  done
fi

# ---------------------------------------------------------------- 6. provenance gate
$PY $R/tools/provenance_gate.py --mo $B/$MN.mo --packs $B --core-sym $SYM
echo "== $CAM done: $B/$MN.mo $(stat -c %s $B/$MN.mo) B, image $IMG B, md5 $(md5sum < $B/$MN.mo | cut -c1-32)"
