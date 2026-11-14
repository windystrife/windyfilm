#!/bin/bash
# dr_check.sh - host checks of the Roll + Darkroom parts (docs/DESIGN.md 3.5), WindyStrife. Run in WSL:
#   bash tools/dr_check.sh            roll state machine (native + ARM module object under qemu), format check
# Env: W (default /root/ml/wf_darkroom), the work dir; nothing is written into the repository.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
W=${W:-/root/ml/wf_darkroom}
mkdir -p $W/o
PY="python3 -I -B"
MODFLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -fno-common
 -nostdlib -ffreestanding -fomit-frame-pointer -fno-strict-aliasing -std=gnu99 -fno-unwind-tables
 -fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter -Wno-comment -DMODULE -DMODULE_NAME=windyflm -I$R/src"
rc=0

echo "== roll (native)"
gcc -std=gnu99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -DWF_HOST_PURE -I$R/src -o $W/roll_test \
    $R/src/wf_roll.c $R/tests/host/wf_roll_test.c
$W/roll_test | tail -n 30 || rc=1

echo "== roll (ARM module object, qemu)"
arm-none-eabi-gcc $MODFLAGS -c -o $W/o/wf_roll.o $R/src/wf_roll.c
arm-none-eabi-readelf -A $W/o/wf_roll.o | grep -q "Tag_CPU_arch: v5T$" || { echo "wf_roll.o: not v5T"; rc=1; }
if arm-none-eabi-objdump -d $W/o/wf_roll.o | grep -qE "\s(ldrd|strd)\s"; then echo "wf_roll.o: LDRD / STRD"; rc=1; fi
if arm-none-eabi-nm $W/o/wf_roll.o | grep -qE "__aeabi_(l|ul)divmod"; then echo "wf_roll.o: 64-bit division"; rc=1; fi
arm-none-eabi-nm -u $W/o/wf_roll.o | awk '{print "  import " $2}'
arm-linux-gnueabi-gcc -O1 -marm -march=armv5te -static -w -I$R/src -o $W/roll_test_arm $W/o/wf_roll.o \
    $R/tests/host/wf_roll_test.c -Wl,--no-warn-execstack -Wl,--no-enum-size-warning
qemu-arm-static -cpu arm946 $W/roll_test_arm | tail -n 3 || rc=1

echo "== format strings"
$PY $R/tools/fmt_check.py $W/o/wf_roll.o || rc=1

exit $rc
