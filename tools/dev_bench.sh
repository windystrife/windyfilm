#!/bin/bash
# dev_bench.sh - memsim timing of the RAW develop "B" on the EOS M frame (WindyStrife). Run in WSL after
# FULL=1 bash tools/dev_check.sh (module objects in $W/o/EOSM, the native scene in $W/full).
#   bash tools/dev_bench.sh            -> $W/bench.txt and the phase table (ARM946E-S model, 200 MHz, NCNB shoot pool)
#   HUE=i bash tools/dev_bench.sh      a chain film's shot: the cube with the film shoulder, colour a / i / d (j.hue);
#                                      CAM=5D2 links the 5D2 object and builds the 5D2's develop (same EOS M scene)
# Env: W (default /root/ml/wf_rawdev), QEMU (qemu-arm with plugins), MEMSIM (the memsim plugin), CUBE (default the
# scene's synthetic FILM33.CUB), HUE, CAM (EOSM / 5D2, with HUE), CURVE (WF_LL_*, with HUE; 1 = S-Log3, a chain film's
# log table), SCENE (default $W/full: a dir with scene.r14 + scene.uyvy, crop.txt for a real pair's window).
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
W=${W:-/root/ml/wf_rawdev}
QEMU=${QEMU:-/root/ml/rawengine/bin/qemu-arm}
MEMSIM=${MEMSIM:-/root/ml/yh_cam/libmemsim.so}
CAM=${CAM:-EOSM}
O=$W/o/$CAM
[ -f $O/wf_dev.o ] && [ -f $W/full/scene.r14 ] || { echo "run FULL=1 bash tools/dev_check.sh first"; exit 1; }
arm-linux-gnueabi-gcc -O2 -marm -march=armv5te -mtune=arm946e-s -static -w -I$R/src -I$W/gen -o $W/dev_bench \
  -DWF_DEV_SHIM_NO_COPYW $R/tools/dev_bench.c $R/tests/host/wf_dev_shim.c $O/wf_dev.o $O/wf_loglut.o $O/wf_logout.o \
  -Wl,--no-enum-size-warning -Wl,--no-warn-execstack
MARK=0x$(arm-linux-gnueabi-nm $W/dev_bench | awk '$NF=="prof_mark"{print $1}')
FBN=$((5280 * 14 / 8 * 3528 + 5184 * 3456 * 2))
if [ -n "${HUE:-}" ]; then ARGS="${CUBE:--} $HUE $(echo $CAM | tr A-Z a-z) ${CURVE:-}"; else ARGS=${CUBE:-}; fi
$QEMU -cpu arm946 -plugin file=$MEMSIM,arg=mark=$MARK,arg=fb=0x48000000,arg=fbn=$FBN -d plugin $W/dev_bench ${SCENE:-$W/full} $ARGS \
  > $W/bench.txt 2>&1 || { cat $W/bench.txt; echo "bench failed"; exit 1; }
cat $W/bench.txt
# with HUE: phases 2 + 3 + 4 = prepare (3 = the shoulder tables), 5 = rows; else 2 = prepare, 3 = rows
awk -v px=$((5184 * 3456)) '/^memsim: phase [0-9]+:/ { p=$3; sub(":","",p); split($0,a,"total "); split(a[2],b," "); t[p]=b[1]; split($0,c,"ins "); split(c[2],d," "); n[p]=d[1] }
     END { if (5 in t) { pr = t[2] + t[3] + t[4]; pn = n[2] + n[3] + n[4]; r = 5;
             printf "shoulder tables %.1f ms (%.2f M ins) | ", t[3], n[3]/1e6 }
           else { pr = t[2]; pn = n[2]; r = 3 }
           printf "build %.0f ms (%.0f M ins) | prepare %.0f ms (%.0f M ins) | rows %.0f ms (%.0f M ins, %.1f ins/px) | shot (prepare + rows) %.2f s\n",
                  t[1], n[1]/1e6, pr, pn/1e6, t[r], n[r]/1e6, n[r]/px, (pr+t[r])/1000 }' $W/bench.txt
