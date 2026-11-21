#!/bin/bash
# lab_strip.sh OUTDIR IMG.JPG[@GX,GY[@NX,NY]] ... - the Lab sheets (labfx): Off, the WindyFilm defaults and the film-lab
# Halation + Bloom + Grain at 35 / 8 / 16 / 65 mm on one frame, through the module's own stages (host case FX lab with
# WF_FX_DUMP=1). Per image (an EOS M JPEG L, 5184 x 3456; @GX,GY = the glow crop's centre, @NX,NY = the grain crop's, in
# frame pixels; else picked) two PNGs: lab_compare_NAME.png and lab_gauges_NAME.png. The photos stay outside the
# repository; the PNGs go to OUTDIR.
#   needs the EOS M host build: CAM=EOSM bash test.sh FX:lab (makes build/eosm/test/wf_test)
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PY="python3 -I -B"
O=${1:?usage: lab_strip.sh OUTDIR IMG.JPG[@GX,GY[@NX,NY]] ...}
shift
mkdir -p "$O"
T=$REPO/build/eosm/test
[ -x $T/wf_test ] || { echo "no $T/wf_test: run CAM=EOSM bash test.sh FX:lab first"; exit 1; }
W=${LAB_KEEP:-$(mktemp -d)}                    # LAB_KEEP=DIR keeps the frames (DIR/run_NAME)
[ -n "$LAB_KEEP" ] || trap 'rm -rf "$W"' EXIT
for A in "$@"; do
  IFS=@ read -r J G N <<< "$A"
  n=$(basename "$J" | sed 's/\.[^.]*$//')
  $PY $REPO/tests/ui/bloom_strip.py uyvy "$J" "$W/$n.uyvy" 5184 3456
  rm -rf "$W/run_$n"; mkdir -p "$W/run_$n"
  ( cd "$W/run_$n" && env WF_FX_DUMP=1 WF_FX_UYVY="$W/$n.uyvy" timeout 3600 qemu-arm-static $T/wf_test FX lab ) > "$W/$n.log" 2>&1 || true
  grep -E "^RESULT|lab strip on|^  (off|classic|lab[0-9]+):|FAIL" "$W/$n.log" | sed 's/^/  /'
  $PY $REPO/tests/ui/lab_strip.py sheets "$W/run_$n" "$O" "$n" $G $N
done
ls -la "$O"
