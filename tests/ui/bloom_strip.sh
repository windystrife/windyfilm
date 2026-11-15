#!/bin/bash
# bloom_strip.sh OUTDIR [IMG.JPG[@X,Y] ...] - the Bloom strip (RAWB11): halation Off, Bloom Off / Low / Normal / Strong on one
# frame, through the module's own halation stage (host case FX:bloom with WF_FX_DUMP=1). One strip PNG per image (an EOS M
# JPEG L, 5184 x 3456; @X,Y = the centre of the 1:2 crop in frame pixels, else the brightest area) and one for the
# test disc. The photos stay outside the repository; the PNGs go to OUTDIR.
#   needs the EOS M host build: CAM=EOSM bash test.sh FX:bloom (makes build/eosm/test/wf_test)
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PY="python3 -I -B"
O=${1:?usage: bloom_strip.sh OUTDIR [IMG.JPG[@X,Y] ...]}
shift
mkdir -p "$O"
T=$REPO/build/eosm/test
[ -x $T/wf_test ] || { echo "no $T/wf_test: run CAM=EOSM bash test.sh FX:bloom first"; exit 1; }
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
one() {      # one NAME [UYVY [X,Y]]
  rm -rf "$W/run"; mkdir -p "$W/run"
  ( cd "$W/run" && env WF_FX_DUMP=1 ${2:+WF_FX_UYVY=$2} timeout 1800 qemu-arm-static $T/wf_test FX bloom ) > "$W/$1.log" 2>&1 || true
  grep -E "^RESULT|bloom strip on|shot #" "$W/$1.log" | sed 's/^/  /'
  $PY $REPO/tests/ui/bloom_strip.py strip "$W/run" "$O/bloom_strip_$1.png" "$1" $3
}
one disc
for A in "$@"; do
  J=${A%@*}; C=; [ "$J" = "$A" ] || C=${A##*@}
  n=$(basename "$J" | sed 's/\.[^.]*$//')
  $PY $REPO/tests/ui/bloom_strip.py uyvy "$J" "$W/$n.uyvy" 5184 3456
  one "$n" "$W/$n.uyvy" $C
done
ls -la "$O"
