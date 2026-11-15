#!/bin/bash
# mist_strip.sh OUTDIR [IMG.JPG@EV8@X,Y;X,Y ...] - the Mist strips (owner 11-15): every Mist level (Off, 1/8 .. 1, Black
# 1/8 .. 1) with Halation and Bloom Off, then Mist 1/4 + Halation Wide + Bloom Normal, through the module's own code (host
# case MIST:levels with WF_FX_DUMP=1). Per image: OUTDIR/mist_<name>_frames.png (whole frames), _crop<k>.png (1:1 crops
# round X,Y in frame pixels, at most 4), _combo.png. EV8 = the photo's EV100 x 8 (its exposure: the mist's hot ramp
# follows the shot's EV). An EOS M JPEG L (5184 x 3456). Also the test scene (disc, orange lamp, leaves with sky gaps,
# sky over shadow) on the EOS M and, with the 5D2 build there, on the 5D2 (its TOP/BOT seam crosses every frame).
# The photos stay outside the repository; the PNGs go to OUTDIR.
#   needs the host builds: CAM=EOSM bash test.sh MIST:levels (and CAM=5D2 for the 5D2 sheets)
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PY="python3 -I -B"
O=${1:?usage: mist_strip.sh OUTDIR [IMG.JPG@EV8@X,Y;X,Y ...]}
shift
mkdir -p "$O"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
one() {      # one CAM NAME CROPS [UYVY EV8]
  T=$REPO/build/$1/test/wf_test
  [ -x $T ] || { echo "no $T: run CAM=${1^^} bash test.sh MIST:levels first"; return 0; }
  rm -rf "$W/run"; mkdir -p "$W/run"
  ( cd "$W/run" && env WF_FX_DUMP=1 WF_FX_CROP="$3" ${4:+WF_FX_UYVY=$4} ${5:+WF_FX_EV8=$5} timeout 3600 \
      qemu-arm-static $T MIST levels ) > "$W/$2.log" 2>&1 || true
  grep -E "^RESULT|^FAIL|mist strip on|shot #|^  (white|black|combo|sky|long)" "$W/$2.log" | sed 's/^/  /'
  cp "$W/$2.log" "$O/mist_$2.log"
  $PY $REPO/tests/ui/mist_strip.py levels "$W/run" "$O/mist_$2" "$2"
}
# the test scene: the disc (centre), the orange lamp (x FW/4), the leaves (x 3 FW/4), all on row FH/2
one eosm disc_eosm "2592,1728;1296,1728;3888,1728"
one 5d2 disc_5d2 "2808,1872;1404,1872;4212,1872;2808,1968"
for A in "$@"; do
  J=${A%%@*}; R=${A#*@}; E=${R%%@*}; C=${R#*@}
  n=$(basename "$J" | sed 's/\.[^.]*$//')
  $PY $REPO/tests/ui/bloom_strip.py uyvy "$J" "$W/$n.uyvy" 5184 3456
  one eosm "$n" "$C" "$W/$n.uyvy" "$E"
done
ls -la "$O"
