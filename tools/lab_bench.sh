#!/bin/bash
# lab_bench.sh - memsim cost of the Lab effects per frame on the camera model (ARM946E-S, 200 MHz; the 5D2 camera
# measured 0.83 x memsim, DESIGN 3.2). Run in WSL after CAM=<cam> bash build.sh (the module objects in build/<cam>/obj).
#   bash tools/lab_bench.sh FRAME.uyvy W H hal|bloom|both GAUGE [HALVES]
# Prints what the Lab adds to a shot (tools/lab_bench.c's phases): the gather (on the colour pass), the build, the
# gather2 pass and build2 (both glows), the apply pass, the grain (on a pass the shot has anyway).
# Env: QEMU (qemu-arm with plugins), MEMSIM (the memsim plugin), OUT (default build/eosm).
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
QEMU=${QEMU:-/root/ml/rawengine/bin/qemu-arm}
MEMSIM=${MEMSIM:-/root/ml/yh_cam/libmemsim.so}
B=${OUT:-$R/build/eosm}
O=$B/obj
[ -f $O/yl_lab.o ] || { echo "CAM=<cam> bash build.sh first"; exit 1; }
arm-linux-gnueabi-gcc -O2 -marm -march=armv5te -mtune=arm946e-s -static -w -I$R/src -o $B/lab_bench $R/tools/lab_bench.c \
  $O/yl_lab.o -lm -Wl,--no-enum-size-warning -Wl,--no-warn-execstack
MARK=0x$(arm-linux-gnueabi-nm $B/lab_bench | awk '$NF=="prof_mark"{print $1}')
FBN=$(($2 * $3 * 2 + 4096))
$QEMU -cpu arm946 -plugin file=$MEMSIM,arg=mark=$MARK,arg=fb=0x48000000,arg=fbn=$FBN -d plugin $B/lab_bench "$@" \
  > $B/lab_bench.txt 2>&1 || { cat $B/lab_bench.txt; echo "bench failed"; exit 1; }
grep -v "^memsim: phase" $B/lab_bench.txt
awk '/^memsim: phase [0-9]+:/ { p=$3; sub(":","",p); split($0,a,"total "); split(a[2],b," "); t[p]=b[1] }
     END { g = t[3] - t[2]; r = t[8] - t[2]; s = g + t[4] + t[5] + t[6] + t[7];
           printf "setup %.0f ms (wf_task) | per shot: gather %.0f + build %.0f + gather2 %.0f + build2 %.0f + apply %.0f = %.2f s; grain %.2f s (copies-only pass %.0f ms)\n",
                  t[1], g, t[4], t[5], t[6], t[7], s / 1000, r / 1000, t[2] }' $B/lab_bench.txt
