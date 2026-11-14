#!/bin/bash
# test.sh - WindyFilm host tests (WindyStrife). Run in WSL (arm-none-eabi, arm-linux-gnueabi, qemu-arm-static).
#
#   bash test.sh                    EOS M, every mode
#   CAM=5D2 bash test.sh            5D2, every mode
#   bash test.sh CAM=5D2 FX MIST    only these modes ("PACK" = every PACK case, "PACK:top" = one case)
# Env: OUT = build dir of build.sh (default <repo>/build/<cam>), LOGCHK_NRAND (default 1000000), WF_PACK_REAL = a real
#      WFFILMS.DAT for "PACK real" (default /root/ml/wf_data/out/WFFILMS.DAT; skipped when missing).
# Never run the EOS M and the 5D2 suite at the same time (one build tree each, but the machine is shared).
#
# The module objects are build.sh's own (OUT/obj; build.sh runs first when they are missing). Linked with them: the camera
# layer again with -DWF_HOST (ROM words from the shim), src/wf_ui_stub.c, tests/host/wf_expoff.c (ML struct layouts),
# tests/host/host_ml.c (the ML / DryOS shim) and tests/host/wf_test.c, into one static ARM Linux program run by qemu-arm.
# Each mode runs in a fresh card directory (OUT/test/run/<mode>), its output in OUT/test/<mode>.log.
# LOGCHK: tools/logchk.c, wf_log_words of the shipped wf_logout.o vs the C reference tests/host/wf_logout_ref.c, under
# qemu-arm -cpu arm946 (an ARMv6+ instruction would trap), the five curves in parallel.
set -u
CAM=${CAM:-EOSM}
MODES=
for a in "$@"; do case $a in CAM=*) CAM=${a#CAM=} ;; *) MODES="$MODES $a" ;; esac; done
R=$(cd "$(dirname "$0")" && pwd)
S=$R/src
H=$R/tests/host
case $CAM in
  EOSM) ML=/root/ml/expsim/mls; PLAT=EOSM.202; CAMDEF=; CAMSRC=wf_cam_eosm.c
        ALL="SHOT SHOT:nocam SHOT:busy FX MIST EXIF EXPSIM CFG PACK LIGHT PRINT PROBE PROBE:boot LOG LOG:big LOG:new LOGCHK" ;;
  5D2)  ML=/root/ml/q5d2/ml; PLAT=5D2.212; CAMDEF=-DWF_BUILD_5D2; CAMSRC=wf_cam_5d2.c
        ALL="SHOT SHOT:nocam SHOT:busy 5D2 FX MIST EXIF EXPSIM CFG PACK LIGHT PRINT LOG LOG:big LOG:new LOGCHK" ;;
  *) echo "CAM must be EOSM or 5D2"; exit 2 ;;
esac
MODES=${MODES:-$ALL}
cam=$(echo $CAM | tr A-Z a-z)
B=${OUT:-$R/build/$cam}
O=$B/obj
G=$O/gen
T=$B/test
PACKCASES="ok missing magic index top fnv code dup flags real"
REAL=${WF_PACK_REAL:-/root/ml/wf_data/out/WFFILMS.DAT}

if [ ! -f $O/wf_shot.o ] || [ ! -f $O/wf_logout.o ]; then
  echo "== build.sh CAM=$CAM (module objects missing)"
  CAM=$CAM PACKS=0 OUT=$B bash $R/build.sh > $B.build.log 2>&1
  [ -f $O/wf_shot.o ] || { tail -30 $B.build.log; echo "build failed"; exit 1; }
fi
rm -rf $T
mkdir -p $T/obj $T/run

# the same flags as build.sh (keep in step)
MODFLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -fno-common
 -nostdlib -ffreestanding -fomit-frame-pointer -fno-strict-aliasing -std=gnu99 -fno-unwind-tables
 -fno-asynchronous-unwind-tables -Wall -Wno-comment -DMODULE -DMODULE_NAME=windyflm $CAMDEF -I$S -I$G"
MLFLAGS="-D__ARM__ -I$ML/include -I$ML/src -I$ML/platform/$PLAT -DCONFIG_MAGICLANTERN=1 -DCONFIG_DEBUGMSG=
 -Wno-unused-parameter"
set -e
arm-none-eabi-gcc $MODFLAGS -DWF_HOST -c -o $T/obj/wf_cam_host.o $S/$CAMSRC
arm-none-eabi-gcc $MODFLAGS -c -o $T/obj/wf_ui_stub.o $S/wf_ui_stub.c
arm-none-eabi-gcc $MODFLAGS $MLFLAGS -Wno-unused-function -c -o $T/obj/wf_expoff.o $H/wf_expoff.c
HCC="arm-linux-gnueabi-gcc -O1 -marm -march=armv5te -Wall -Wno-unused-function -Wno-comment -Wno-format-truncation $CAMDEF -I$S -I$H -I$G"
$HCC -c -o $T/obj/host_ml.o $H/host_ml.c
$HCC -c -o $T/obj/wf_test.o $H/wf_test.c
MOD="wf_main.o wf_menu.o wf_shot.o wf_pv.o wf_probe.o wf_loglut.o wf_llk.o wf_llk_arm.o wf_film.o yh_halo.o wf_mist.o yg_grain.o yg_stocks.o
 wf_fx.o wf_expsim.o wf_catalog.o wf_logout.o"
[ -f $O/wf_expk.o ] && MOD="$MOD wf_expk.o"
arm-linux-gnueabi-gcc -static -pthread -o $T/wf_test $T/obj/wf_test.o $T/obj/host_ml.o $(for o in $MOD; do echo $O/$o; done) \
  $T/obj/wf_cam_host.o $T/obj/wf_ui_stub.o $T/obj/wf_expoff.o -lm -Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning -Wl,--no-warn-execstack
set +e
echo "== WindyFilm host tests $CAM: $T/wf_test ($(stat -c %s $T/wf_test) B)"

TP=0; TF=0; BAD=
run() {      # run MODE CASE [FILE]
  local name=$1${2:+_$2} d=$T/run/$1${2:+_$2}
  mkdir -p $d
  local t0=$(date +%s)
  ( cd $d && timeout 1800 qemu-arm-static $T/wf_test "$@" ) > $T/$name.log 2>&1
  local rc=$? t1=$(date +%s)
  local res=$(grep "^RESULT" $T/$name.log | tail -1)
  local p=$(echo "$res" | sed -n 's/.*: \([0-9]*\) pass.*/\1/p') f=$(echo "$res" | sed -n 's/.* \([0-9]*\) fail.*/\1/p')
  p=${p:-0}; f=${f:-1}
  [ $rc = 0 ] || [ "$f" != 0 ] || f=1
  TP=$((TP + p)); TF=$((TF + f))
  printf "  %-14s %3d pass %3d fail  %4ds%s\n" "$name" $p $f $((t1 - t0)) "$([ $rc = 0 ] || echo "  (exit $rc, see $T/$name.log)")"
  [ "$f" = 0 ] || { BAD="$BAD $name"; grep "^FAIL" $T/$name.log | head -8 | sed 's/^/      /'; }
}

for m in $MODES; do
  case $m in
    PACK) for c in $PACKCASES; do
            if [ $c = real ]; then [ -f $REAL ] && run PACK real $REAL || echo "  PACK_real      skipped (no $REAL)"
            else run PACK $c; fi
          done ;;
    PACK:real) [ -f $REAL ] && run PACK real $REAL || echo "  PACK_real      skipped (no $REAL)" ;;
    *:*) run ${m%%:*} ${m#*:} ;;
    LOGCHK)
      arm-none-eabi-gcc $MODFLAGS -Dwf_log_words=ref_log_words -Dwf_lenc=ref_lenc -c -o $T/obj/logref.o $H/wf_logout_ref.c &&
      arm-linux-gnueabi-gcc -O2 -marm -march=armv5te -static -o $T/logchk $R/tools/logchk.c $T/obj/logref.o $O/wf_logout.o \
        -Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning || { echo "  LOGCHK build failed"; TF=$((TF + 1)); BAD="$BAD LOGCHK"; continue; }
      t0=$(date +%s)
      for c in 1 2 3 4 5; do qemu-arm-static -cpu arm946 $T/logchk $c ${LOGCHK_NRAND:-1000000} > $T/LOGCHK_$c.log 2>&1 & done
      wait
      p=0; f=0
      for c in 1 2 3 4 5; do
        n=$(grep -c "OK bit-exact" $T/LOGCHK_$c.log); k=$(grep -c "FAIL" $T/LOGCHK_$c.log)
        p=$((p + n)); f=$((f + k)); [ "$n" -ge 1 ] || f=$((f + 1))
      done
      TP=$((TP + p)); TF=$((TF + f))
      printf "  %-14s %3d pass %3d fail  %4ds  (curves 1..5, qemu -cpu arm946)\n" LOGCHK $p $f $(( $(date +%s) - t0 ))
      [ $f = 0 ] || { BAD="$BAD LOGCHK"; grep -h "FAIL\|mismatch" $T/LOGCHK_*.log | head -8 | sed 's/^/      /'; } ;;
    *) run $m ;;
  esac
done
echo "== $CAM: $TP checks passed, $TF failed${BAD:+ (in:$BAD)}"
[ $TF = 0 ]
