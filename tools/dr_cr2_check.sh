#!/bin/bash
# dr_cr2_check.sh - the Darkroom's CR2 reader (src/wf_cr2.c, WindyStrife) against tools/cr2_dec.c on real CR2s. WSL.
#
#   bash tools/dr_cr2_check.sh [DIR] [N]     the first N CR2s of DIR (default: all of the owner's Day09 EOS M set, then
#                                            IMG_6821 of 1114_EOSM_Neutral)
# 1. tools/cr2_dec.c (gcc, x86) and the module object (arm-none-eabi-gcc, build.sh's module flags at -O2) + build.sh's
#    object checks (Tag_CPU_arch v5T, no LDRD / STRD, no 64-bit division helper); the object's imports are listed.
# 2. tests/host/wf_cr2_test.c linked with that object (qemu-arm-static): per CR2 the module decode through rd() in 256 KB
#    chunks against cr2_dec's raw.r14 + meta.txt, bit for bit; on the first CR2 also odd short reads, the 8 px and 1 px
#    output paths (a -DWF_CR2_TEST object) and the broken-input cases.
# 3. memsim (ARM946E-S model, 200 MHz): one decode of the first CR2 with the raw in the NCNB shoot pool region and the
#    input buffer cached / uncached. Skipped when QEMU / MEMSIM are missing (env BENCH=0 skips it too).
# Env: W (work dir, default /root/ml/wf_dr_cr2), QEMU (qemu-arm with plugins), MEMSIM (the memsim plugin), BENCH.
# The photos and the decoded raws stay in W (raw.r14 is deleted after each file); never commit them.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
S=$R/src
W=${W:-/root/ml/wf_dr_cr2}
CR=/mnt/f/MagicLatern/_research/raw-path/cr2
D=${1:-$CR/Day09_EOSM}
N=${2:-9999}
QEMU=${QEMU:-/root/ml/rawengine/bin/qemu-arm}
MEMSIM=${MEMSIM:-/root/ml/yh_cam/libmemsim.so}
mkdir -p $W/o $W/ref
cd $W
gcc -O2 -w -o $W/cr2_dec $R/tools/cr2_dec.c

CC=arm-none-eabi-gcc
MODFLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -fno-common
 -nostdlib -ffreestanding -fomit-frame-pointer -fno-strict-aliasing -std=gnu99 -fno-unwind-tables
 -fno-asynchronous-unwind-tables -Wall -Wno-comment -DMODULE -DMODULE_NAME=windyflm -I$S"
$CC $MODFLAGS -O2 -c -o $W/o/wf_cr2.o $S/wf_cr2.c
$CC $MODFLAGS -O2 -DWF_CR2_TEST -c -o $W/o/wf_cr2_t.o $S/wf_cr2.c
o=$W/o/wf_cr2.o
bad=
arm-none-eabi-readelf -A $o | grep -q "Tag_CPU_arch: v5T$" || bad="$bad not-v5T"
n=$(arm-none-eabi-objdump -d $o | grep -ciE "[[:space:]](ldr|str)d[a-z]{0,2}[[:space:]]" || true)
[ "$n" = 0 ] || bad="$bad LDRD/STRD:$n"
arm-none-eabi-nm -u $o | grep -qE "__aeabi_(l|ul)divmod|__(u)?(div|mod)di3" && bad="$bad 64-bit-division"
[ -z "$bad" ] || { echo "object checks failed:$bad"; exit 1; }
echo "wf_cr2.o: v5T, 0 LDRD/STRD, no 64-bit division; text $(arm-none-eabi-size $o | awk 'NR==2{print $1}') B," \
     "imports: $(arm-none-eabi-nm -u $o | awk '{print $2}' | tr '\n' ' ')"

HCC="arm-linux-gnueabi-gcc -O2 -marm -march=armv5te -static -w -I$S"
LDX="-Wl,--no-enum-size-warning -Wl,--no-wchar-size-warning -Wl,--no-warn-execstack"
$HCC -o $W/wf_cr2_test $R/tests/host/wf_cr2_test.c $W/o/wf_cr2.o $LDX
$HCC -o $W/wf_cr2_test_t $R/tests/host/wf_cr2_test.c $W/o/wf_cr2_t.o $LDX

pass=0; fail=0; k=0; first=
run() {                                          # run LOG CMD..: count the RESULT line
  local log=$1; shift
  "$@" > $log 2>&1 || true
  local p f
  read p f < <(awk '/^RESULT/ {print $2, $4}' $log)
  [ -n "$p" ] || { p=0; f=1; echo "  (no RESULT in $log)"; tail -3 $log; }
  pass=$((pass + p)); fail=$((fail + f))
  [ "$f" = 0 ] || grep -E "^FAIL" $log | head -5
}
one() {                                          # one CR2: reference, check
  local c=$1 b=$(basename "$1" .CR2)
  rm -f $W/ref/raw.r14 $W/ref/meta.txt
  $W/cr2_dec "$c" $W/ref > $W/ref/$b.dec.txt
  run $W/$b.log qemu-arm-static $W/wf_cr2_test check "$c" $W/ref
  echo "$b: $(grep -E '^(PASS|FAIL)' $W/$b.log | head -1 | cut -c1-110)"
  if [ -z "$first" ]; then
    first=$c
    for m in short force8 force1; do
      run $W/$b.$m.log qemu-arm-static $W/wf_cr2_test_t check "$c" $W/ref $m
      echo "  $m: $(grep -E '^(PASS|FAIL)' $W/$b.$m.log | head -1 | cut -c1-100)"
    done
    run $W/bad.log qemu-arm-static $W/wf_cr2_test bad "$c"
    echo "  broken inputs: $(awk '/^RESULT/' $W/bad.log)"; grep -vE "^(RESULT|FAIL)" $W/bad.log | sed 's/^/    /'
    grep -m1 "^cr2 " $W/$b.log | cut -c1-400
    if [ "${BENCH:-1}" = 1 ] && [ -x $QEMU ] && [ -f $MEMSIM ]; then
      MARK=0x$(arm-linux-gnueabi-nm $W/wf_cr2_test | awk '$NF=="prof_mark"{print $1}')
      FBN=$((9240 * 3528 + 256 * 1024 + 4096))
      for mode in cached uncached; do
        $QEMU -cpu arm946 -plugin file=$MEMSIM,arg=mark=$MARK,arg=fb=0x48000000,arg=fbn=$FBN -d plugin \
          $W/wf_cr2_test bench "$c" $mode $W/ref > $W/bench_$mode.txt 2>&1 || true
        echo "  memsim ($mode input): $(grep -E '^bench|^memsim: phase 1' $W/bench_$mode.txt | tr '\n' ' ')"
        grep -q "^RESULT [0-9]* pass 0 fail" $W/bench_$mode.txt || { echo "  bench compare failed"; fail=$((fail + 1)); }
      done
    fi
  fi
  rm -f $W/ref/raw.r14
  k=$((k + 1))
}
for c in "$D"/*.CR2; do
  [ -f "$c" ] || continue
  one "$c"
  [ $k -ge $N ] && break
done
[ -z "$1" ] && [ -f $CR/1114_EOSM_Neutral/IMG_6821.CR2 ] && one $CR/1114_EOSM_Neutral/IMG_6821.CR2
echo "dr_cr2_check: $k CR2s, checks $pass pass / $fail fail (logs in $W)"
[ $fail = 0 ]
