#!/bin/bash
# luts/make_cubes.sh OUTDIR - runs every look generator of luts/looks and collects the 33^3 cubes flat in OUTDIR
# (OUTDIR/<NAME>.CUB, per-generator logs and checks in OUTDIR/gen/<generator>/). WSL, ~1 min with 16 jobs.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:?outdir}
mkdir -p "$OUT/gen"
run() { g=$1; o=$2; mkdir -p "$o"; ( cd "$(dirname "$g")" && python3 -I -B "$g" "$o" > "$o/gen.log" 2>&1 || echo "FAILED $g" ); }
export -f run
for g in "$HERE"/looks/*/gen_*.py; do echo "$g $OUT/gen/$(basename "$g" .py)"; done | xargs -P 16 -L1 bash -c 'run $0 $1'
for c in "$OUT"/gen/*/*.CUB; do cp "$c" "$OUT/"; done
rm -f "$OUT"/SUPERIA.CUB "$OUT"/PRT400B.CUB          # a Superia look we do not ship; the alternate Portra 400 preset
ls "$OUT"/*.CUB | wc -l
