#!/bin/bash
# dev_check.sh - RAW develop "B" (src/wf_dev.c, WindyStrife): objects for both cameras + the build.sh object checks, and the
# host test (tests/host/wf_dev_test.c). build.sh builds wf_dev.o / wf_rsrc.o for the EOS M module and test.sh RAWB runs the
# wired path; this script keeps the develop's own unit modes and the memsim bench input (docs/CORE-NOTES.md 10).
#
#   bash tools/dev_check.sh                 objects EOSM + 5D2, host test modes px frame bad span roll rsrc
#   FULL=1 bash tools/dev_check.sh          + the full EOS M frame (writes the bench input to $W/full)
# Env: W = work dir (default /root/ml/wf_rawdev). Run in WSL (arm-none-eabi, arm-linux-gnueabi, qemu-arm-static, python3).
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
S=$R/src
H=$R/tests/host
W=${W:-/root/ml/wf_rawdev}
G=$W/gen
PY="python3 -I -B"
mkdir -p $G $W/o $W/run
$PY $R/tools/mk_loglut_tab.py $G/wf_loglut_tab.h > /dev/null
$PY $R/tools/mk_logtab.py $G/wf_logtab.h > /dev/null
$PY $R/tools/mk_logasm_tab.py $G/wf_logtab.h $G/wf_logasm_tab.h > /dev/null
$PY $R/tools/mk_devmat.py --check $S/wf_dev.c
CC=arm-none-eabi-gcc
bad=
for CAM in EOSM 5D2; do
  CAMDEF=; [ $CAM = 5D2 ] && CAMDEF=-DWF_BUILD_5D2
  # the same flags as build.sh (keep in step); the develop kernel at -O2 like the other hot kernels
  MODFLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -fno-common
   -nostdlib -ffreestanding -fomit-frame-pointer -fno-strict-aliasing -std=gnu99 -fno-unwind-tables
   -fno-asynchronous-unwind-tables -Wall -Wno-comment -DMODULE -DMODULE_NAME=windyflm $CAMDEF -I$S -I$G"
  O=$W/o/$CAM
  mkdir -p $O
  # the develop: C at -O2 + the span kernel in ARMv5TE asm, one object (like wf_logout.o)
  $CC $MODFLAGS -O2 -Werror -c -o $O/wf_dev_c.o $S/wf_dev.c
  $CC -mcpu=arm946e-s -marm -c -o $O/wf_dev_arm.o $S/wf_dev_arm.S
  arm-none-eabi-ld -r -o $O/wf_dev.o $O/wf_dev_c.o $O/wf_dev_arm.o
  $CC $MODFLAGS -DWF_LL_EXTERN_ENC -c -o $O/wf_loglut.o $S/wf_loglut.c
  $CC $MODFLAGS -Werror -c -o $O/wf_rsrc.o $S/wf_rsrc.c
  $CC $MODFLAGS -O2 -DWF_LL_ASM -c -o $O/wf_llk.o $S/wf_llk.c
  $CC -march=armv5t -marm -c -o $O/wf_llk_arm.o $S/wf_llk_arm.S
  $CC $MODFLAGS -c -o $O/wf_logout_tab.o $S/wf_logout_tab.c
  $CC -mcpu=arm946e-s -marm -c -o $O/wf_logout_arm.o $S/wf_logout_arm.S
  arm-none-eabi-ld -r -o $O/wf_logout.o $O/wf_logout_tab.o $O/wf_logout_arm.o
  o=$O/wf_dev.o
  arm-none-eabi-readelf -A $O/wf_dev_c.o | grep -q "Tag_CPU_arch: v5T$" || bad="$bad $CAM/wf_dev_c.o(not v5T)"
  arm-none-eabi-readelf -A $o | grep -q "Tag_CPU_arch: v5TE$" || bad="$bad $CAM/wf_dev.o(not v5TE)"
  n=$(arm-none-eabi-objdump -d $o | grep -ciE "[[:space:]](ldr|str)d[a-z]{0,2}[[:space:]]" || true)
  [ "$n" = 0 ] || bad="$bad $CAM/wf_dev.o(LDRD/STRD $n)"
  arm-none-eabi-nm -u $o | grep -qE "__aeabi_(l|ul)divmod|__(u)?(div|mod)di3" && bad="$bad $CAM/wf_dev.o(64-bit division)"
  imp=$(arm-none-eabi-nm -u $o | awk '{print $2}' | tr '\n' ' ')
  echo "$CAM wf_dev.o: $(arm-none-eabi-size $o | awk 'NR==2 {print $1 " text, " $2 " data, " $3 " bss"}'); imports: $imp"
  arm-none-eabi-readelf -A $O/wf_rsrc.o | grep -q "Tag_CPU_arch: v5T$" || bad="$bad $CAM/wf_rsrc.o(not v5T)"
  arm-none-eabi-nm -u $O/wf_rsrc.o | grep -qE "__aeabi_(l|ul)divmod|__(u)?(div|mod)di3" && bad="$bad $CAM/wf_rsrc.o(64-bit division)"
  imp=$(arm-none-eabi-nm -u $O/wf_rsrc.o | awk '{print $2}' | tr '\n' ' ')
  echo "$CAM wf_rsrc.o: $(arm-none-eabi-size $O/wf_rsrc.o | awk 'NR==2 {print $1 " text, " $2 " data, " $3 " bss"}'); imports: $imp"
done
[ -z "$bad" ] || { echo "object checks failed:$bad"; exit 1; }
echo "objects: wf_dev.o v5TE (C part v5T), 0 LDRD/STRD, no 64-bit division helper; wf_rsrc.o v5T (EOSM, 5D2)"

# host test: module objects (EOS M flags) + shim + scene + test, static ARM Linux, qemu-arm-static
O=$W/o/EOSM
HCC="arm-linux-gnueabi-gcc -O1 -marm -march=armv5te -Wall -Wno-unused-function -Wno-comment -I$S -I$H -I$G"
$HCC -c -o $W/o/wf_dev_test.o $H/wf_dev_test.c
$HCC -c -o $W/o/wf_dev_scene.o $H/wf_dev_scene.c
$HCC -c -o $W/o/wf_dev_shim.o $H/wf_dev_shim.c
arm-linux-gnueabi-gcc -static -o $W/wf_dev_test $W/o/wf_dev_test.o $W/o/wf_dev_scene.o $W/o/wf_dev_shim.o $O/wf_dev.o \
  $O/wf_rsrc.o $O/wf_loglut.o $O/wf_llk.o $O/wf_llk_arm.o $O/wf_logout.o -lm -Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning \
  -Wl,--no-warn-execstack
MODES="px frame bad span roll rsrc"
if [ "${FULL:-0}" = 1 ]; then          # the full EOS M frame, rendered natively (float scene code is slow under qemu)
  gcc -O2 -DWF_DEV_SCENE_MAIN -I$H -o $W/wf_dev_scene $H/wf_dev_scene.c -lm
  mkdir -p $W/full
  $W/wf_dev_scene $W/full 5184 3456 0
  MODES="$MODES full"
fi
set +e
TP=0; TF=0; BAD=
for m in $MODES; do
  d=$W/run/$m
  rm -rf $d; mkdir -p $d
  arg=; [ $m = full ] && { mkdir -p $W/full; arg=$W/full; }
  t0=$(date +%s)
  ( cd $d && qemu-arm-static $W/wf_dev_test $m $arg ) > $W/run/$m.log 2>&1
  rc=$?
  t1=$(date +%s)
  res=$(grep "^RESULT" $W/run/$m.log | tail -1)
  p=$(echo "$res" | sed -n 's/.*: \([0-9]*\) pass.*/\1/p'); f=$(echo "$res" | sed -n 's/.* \([0-9]*\) fail.*/\1/p')
  p=${p:-0}; f=${f:-1}; [ $rc = 0 ] || [ "$f" != 0 ] || f=1
  TP=$((TP + p)); TF=$((TF + f)); [ "$f" = 0 ] || BAD="$BAD $m"
  echo "== $m ($((t1 - t0)) s): ${res:-no RESULT line (rc $rc)}"
  grep -E "^(px|EOSM|5D2|highlights|span|rsrc|FAIL)" $W/run/$m.log
done
echo "dev host test: $TP pass, $TF fail${BAD:+ (failed:$BAD)}"
[ $TF = 0 ]
