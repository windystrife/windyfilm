#!/bin/bash
# lab_check.sh - the Lab effects (src/yl_lab.c) against the film-lab reference CLI on real frames (WindyStrife's own tool).
#
#   FILM_CLI=/path/to/film_cli bash tools/lab_check.sh OUTDIR FRAME.JPG[:W:H[:HALVES]] ...
#
# Each frame's JPEG becomes the camera's UYVY frame (tools/lab_check.py uyvy: its own Y'CbCr, centred in W x H, default
# the EOS M's 5184 x 3456; HALVES 1 = the frame as two halves, like every 5D2 shot). Then, at each gauge (8 / 16 / 35 /
# 65 mm), halation, bloom, both and grain run through tools/lab_check.c (the camera's code, built for the host) and the
# reference CLI with the same film-lab preset, on the same RGB input. One line per case: mean / max |cam - ref| per
# channel, PSNR, the same inside the glow (pixels the reference moved by 2 levels or more), the glow's mean in both; for
# grain (its pattern is the reference's hash at another offset, so pixel differences mean nothing) the statistics of
# both: the luma change's sd by zone, the share of pixels moved, the autocorrelation. Runs in WSL (python3, numpy, PIL);
# film_cli may be a Windows .exe (WSL interop). Everything is written under OUTDIR.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
FC=${FILM_CLI:?set FILM_CLI to the film-lab reference CLI}
OUT=${1:?OUTDIR}
shift
mkdir -p "$OUT"
cd "$OUT"
gcc -O2 -Wall -Wextra -Wno-unused-parameter -I"$R/src" -o lab_check "$R/tools/lab_check.c" "$R/src/yl_lab.c" -lm
GN=(8mm 16mm 35mm 65mm)
# film-lab's preset numbers per gauge (the same as src/yl_lab.c's tables), in the reference CLI's own argument order
HAL=("80 50 70 25 45 80 15 80 100 50" "80 50 70 20 35 80 15 80 0 50" "70 50 70 15 25 80 15 80 100 50" "70 50 70 10 20 80 15 80 100 50")
BLM=("80 0.8 100 40 4 15 5 50 45 100 1 20" "80 0 80 20 4 20 5 55 55 100 1 20" "80 0 50 30 4 20 5 60 50 100 1 20"
     "80 -0.2 35 20 4 25 5 70 40 100 1 20")
GRN=("50 25 45 65 65 48 1.6 1 1" "45 30 55 65 65 25 1.6 1 1" "35 30 55 65 65 12 1.6 1 1" "15 30 55 65 65 2 1.6 1 1")
for spec in "$@"; do
    IFS=: read -r jpg w h hv <<< "$spec"
    w=${w:-5184}; h=${h:-3456}; hv=${hv:-0}
    nm=$(basename "$jpg"); nm=${nm%.*}
    python3 -I -B "$R/tools/lab_check.py" uyvy "$jpg" "$w" "$h" "$nm.uyvy"
    for g in 0 1 2 3; do
        for what in ${WHATS:-hal bloom both grain}; do
            s="${nm}_${what}_${GN[$g]}"
            t=$(./lab_check "$nm.uyvy" "$w" "$h" "$what" "$g" "$s" "$hv" | tail -1)
            case $what in
                hal)   a="--hal ${HAL[$g]}" ;;
                bloom) a="--bloom ${BLM[$g]}" ;;
                both)  a="--bloom ${BLM[$g]} --hal ${HAL[$g]}" ;;
                grain) a="--grain ${GRN[$g]}" ;;
            esac
            # shellcheck disable=SC2086
            "$FC" "${s}_in.ppm" "${s}_ref.ppm" $a > /dev/null
            if [ $what = grain ]; then
                r=$(python3 -I -B "$R/tools/lab_check.py" grain "${s}_in.ppm" "${s}_cam.ppm" "${s}_ref.ppm" | tr '\n' ' ')
            else
                r=$(python3 -I -B "$R/tools/lab_check.py" cmp "${s}_in.ppm" "${s}_cam.ppm" "${s}_ref.ppm")
            fi
            echo "$nm ${GN[$g]} $what | $r | host $t"
            [ "$nm" = "${KEEP:-}" ] || rm -f "${s}_in.ppm" "${s}_cam.ppm" "${s}_ref.ppm"   # KEEP=name: that frame's images stay
        done
    done
done
