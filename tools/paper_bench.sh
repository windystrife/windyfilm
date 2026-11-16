#!/bin/bash
# paper_bench.sh - memsim cost of the Paper row's table passes on the EOS M model (ARM946E-S, 200 MHz). Run in WSL after
# CAM=EOSM bash build.sh (the module objects in build/eosm/obj). Prints the phases of tools/paper_bench.c: the film set
# without / with a paper (the difference is the paper's own cost), wf_ll_paper, wf_dev_paper.
# Env: QEMU (qemu-arm with plugins), MEMSIM (the memsim plugin), OUT (default build/eosm).
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
QEMU=${QEMU:-/root/ml/rawengine/bin/qemu-arm}
MEMSIM=${MEMSIM:-/root/ml/yh_cam/libmemsim.so}
B=${OUT:-$R/build/eosm}
O=$B/obj
[ -f $O/wf_film.o ] && [ -f $O/wf_dev.o ] || { echo "CAM=EOSM bash build.sh first"; exit 1; }
arm-linux-gnueabi-gcc -O2 -marm -march=armv5te -mtune=arm946e-s -static -w -I$R/src -I$O/gen -o $B/paper_bench \
  $R/tools/paper_bench.c $R/tests/host/wf_dev_shim.c $O/wf_film.o $O/wf_dev.o $O/wf_loglut.o \
  $O/wf_logout.o -Wl,--no-enum-size-warning -Wl,--no-warn-execstack
MARK=0x$(arm-linux-gnueabi-nm $B/paper_bench | awk '$NF=="prof_mark"{print $1}')
$QEMU -cpu arm946 -plugin file=$MEMSIM,arg=mark=$MARK,arg=fb=0x48000000,arg=fbn=4096 -d plugin $B/paper_bench \
  > $B/paper_bench.txt 2>&1 || { cat $B/paper_bench.txt; echo "bench failed"; exit 1; }
cat $B/paper_bench.txt
awk '/^memsim: phase [0-9]+:/ { p=$3; sub(":","",p); split($0,a,"total "); split(a[2],b," "); t[p]=b[1]; split($0,c,"ins "); split(c[2],d," "); n[p]=d[1] }
     END { printf "film set Glossy %.1f ms (%.2f M ins) | Matte %.1f ms (+%.1f ms, +%.2f M ins) | Warm %.1f ms (+%.1f ms) | ",
                  t[1], n[1]/1e6, t[2], t[2]-t[1], (n[2]-n[1])/1e6, t[3], t[3]-t[1];
           printf "wf_ll_paper %.1f ms (%.2f M ins) | wf_dev_paper %.1f ms (%.2f M ins)\n", t[4], n[4]/1e6, t[5], n[5]/1e6 }' \
  $B/paper_bench.txt
