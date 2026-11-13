#!/bin/bash
# armcheck.sh - cross-compile src/ui for both cameras with the module flags, then check the result:
#   every undefined symbol is in that camera's card .sym or is a wf_api.h name (the core links it in),
#   no 64-bit division helper, no LDRD / STRD, the section sizes (.bss must stay small: malloc at open).
#   bash /mnt/f/MagicLatern/_windyfilm/tests/ui/armcheck.sh
REPO=/mnt/f/MagicLatern/_windyfilm
W=/root/ml/wf_uidev
FLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -D__ARM__
       -DCONFIG_MAGICLANTERN=1 -DCONFIG_DEBUGMSG= -DMODULE -DMODULE_NAME=windyflm -Wall -Wno-comment -Wno-unused-function -fno-common"
SRCS="wf_gfx wf_font wf_cans wf_ui"
API=$(grep -oE '\b(wf_[a-z_]+)\s*\(' $REPO/src/wf_api.h | sed -E 's/\s*\(//' | sort -u; echo wf_films; echo wf_nfilms)
rc=0
for CAM in EOSM 5D2; do
    if [ $CAM = EOSM ]; then
        ML=/root/ml/expsim/mls; PLAT=EOSM.202; SYM=/mnt/f/MagicLatern/EOSM_FilmRoll/ML/modules/EOSM_202.sym
    else
        ML=/root/ml/q5d2/ml; PLAT=5D2.212; SYM=/mnt/f/MagicLatern/5D2_FilmRoll_A13/ML/modules/5D2_212.sym
    fi
    O=$W/build/arm_$CAM
    rm -rf $O; mkdir -p $O/build
    echo "/* armcheck stub: ML's module build generates this from the module README */" > $O/build/module_strings.h
    tr -d '\r' < $SYM | awk '{print $2}' | sort -u > $O/sym.txt
    for s in $SRCS; do
        arm-none-eabi-gcc $FLAGS -I$O -I$ML/include -I$ML/src -I$ML/platform/$PLAT -I$REPO/src -I$REPO/src/ui \
            -c $REPO/src/ui/$s.c -o $O/$s.o || { echo "$CAM: $s.c does not compile"; rc=1; }
    done
    arm-none-eabi-ld -r -o $O/ui.o $(for s in $SRCS; do echo $O/$s.o; done) || rc=1
    echo "== $CAM ($PLAT)"
    arm-none-eabi-size $O/ui.o
    arm-none-eabi-size -A $O/ui.o | awk '$1 ~ /^\.(text|rodata|data|bss)/ {printf "  %-12s %6d\n", $1, $2}'
    arm-none-eabi-nm -u $O/ui.o | awk '{print $2}' | sort -u > $O/undef.txt
    n=0; miss=0
    while read u; do
        n=$((n+1))
        if grep -qx "$u" $O/sym.txt; then :;
        elif echo "$API" | grep -qx "$u"; then echo "  core: $u";
        else echo "  MISSING: $u"; miss=$((miss+1)); fi
    done < $O/undef.txt
    echo "  $n undefined symbols, $miss missing from $(basename $SYM)"
    [ $miss = 0 ] || rc=1
    if grep -qE '__aeabi_(l|ul)divmod|__divdi3|__udivdi3|__moddi3|__umoddi3' $O/undef.txt; then
        echo "  64-bit division helper used"; rc=1; fi
    ld=$(arm-none-eabi-objdump -d $O/ui.o | grep -cE '\s(ldrd|strd)\s')
    echo "  ldrd/strd: $ld"
    [ $ld = 0 ] || rc=1
    echo "  ML API used: $(grep -vxF "$API" $O/undef.txt | tr '\n' ' ')"
done
exit $rc
