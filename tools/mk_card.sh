#!/bin/bash
# mk_card.sh CAM SRC_CARD DST - WindyFilm test card folder (WindyStrife's own tool; PLAYBOOK section 7).
#
#   CAM       EOSM or 5D2 (the build in build/<cam> must be green: tests + gate)
#   SRC_CARD  a working ML card folder of that camera (the Film Roll card): its core pair autoexec.bin + ML/modules/<sym>,
#             ML-SETUP.FIR, ML's own files and the owner's Film LUT cubes are copied unchanged
#   DST       a new folder (refused when it exists)
#
# Left out: every pattern in card/EXCLUDE.txt (the old module, its packs and fonts, old .FRT caches, ML/SETTINGS and
# ML/LOGS contents). Only WINDYFLM.EN is written to ML/SETTINGS: ML's default config keeps Global Draw on, which the
# LiveView preview needs. Then: windyflm.mo, the three packs, the readmes and licences, MD5SUMS.txt, and the checks
# (core pair md5, checksyms, md5sum -c, nothing of EXCLUDE.txt left).
# CUBES (env, optional): the owner's cubes the source card does not hold, as a space-separated list of files named
# <NAME>.CUB (no spaces in the path); each is copied unchanged to ML/DATA/<NAME>.CUB and listed in MD5SUMS.txt. Leica
# Vivid's display cube (fb6bf1d): CUBES=/mnt/f/MagicLatern/LeicaLUT_SR/LEICAVSR.CUB. Never from the repo.
# Run in WSL: bash tools/mk_card.sh EOSM <the EOS M ML card folder> /mnt/f/MagicLatern/EOSM_WindyFilm
set -eu
CUBES=${CUBES:-}
CAM=$1 SRC=$2 DST=$3
R=$(cd "$(dirname "$0")/.." && pwd)
case $CAM in
  EOSM) SYM=EOSM_202.sym; AX=782dbf15802e9bb0860dcf5b5167ab47; SX=09ccc47d0d386b04d1b28cce27d9a1e1; B=$R/build/eosm ;;
  5D2)  SYM=5D2_212.sym;  AX=f04c818a9908e750abfec8cdcdd7c1ee; SX=aa702ecc013bac65af03150043ea64ca; B=$R/build/5d2 ;;
  *) echo "CAM must be EOSM or 5D2"; exit 2 ;;
esac
[ -e "$DST" ] && { echo "$DST exists: refusing to overwrite"; exit 2; }
for f in autoexec.bin ML-SETUP.FIR ML/modules/$SYM; do [ -f "$SRC/$f" ] || { echo "$SRC/$f missing"; exit 2; }; done
for f in windyflm.mo WFFILMS.DAT WFCANS.DAT WFFONTS.DAT; do [ -s "$B/$f" ] || { echo "$B/$f missing: build first"; exit 2; }; done
for c in $CUBES; do
  case ${c##*/} in *.CUB) ;; *) echo "$c: a cube must be named <NAME>.CUB"; exit 2 ;; esac
  [ -s "$c" ] || { echo "$c missing"; exit 2; }
done
[ "$(md5sum < "$SRC/autoexec.bin" | cut -c1-32)" = $AX ] || { echo "autoexec.bin is not the known $CAM core"; exit 2; }
[ "$(md5sum < "$SRC/ML/modules/$SYM" | cut -c1-32)" = $SX ] || { echo "$SYM is not the known $CAM core"; exit 2; }

shopt -s nocaseglob nullglob
mapfile -t EXCL < <(grep -v '^#' "$R/card/EXCLUDE.txt" | sed '/^$/d')
mkdir -p "$DST"
cp -p "$SRC/autoexec.bin" "$SRC/ML-SETUP.FIR" "$DST/"
cp -rp "$SRC/ML" "$DST/ML"
D=$(ls -d "$DST"/ML/[dD][aA][tT][aA])
for p in "${EXCL[@]}"; do
  for f in "$DST"/$p; do [ -f "$f" ] && rm -f "$f"; done
done
mkdir -p "$DST/ML/SETTINGS" "$DST/ML/LOGS"
printf 1 > "$DST/ML/SETTINGS/WINDYFLM.EN"

cp "$B/windyflm.mo" "$DST/ML/modules/"
cp "$B/WFFILMS.DAT" "$B/WFCANS.DAT" "$B/WFFONTS.DAT" "$D/"
for c in $CUBES; do
  for f in "$D"/${c##*/}; do [ -e "$f" ] && { echo "${f#$DST/} already on the source card: refusing to replace it"; exit 1; }; done
  cp -p "$c" "$D/"
  echo "cube: ${c##*/} ($(md5sum < "$c" | cut -c1-32))"
done
cp "$R/card/README_WINDYFILM_$CAM.txt" "$DST/README_WINDYFILM.txt"
cp "$R/card/README_FILMLUT.txt" "$R/card/CHANGELOG_FILMLUT.txt" "$R/licenses/SPEKTRAFILM_LICENSE.txt" \
   "$R/licenses/UBUNTU_FONT_LICENCE.txt" "$DST/"
cp "$R/ATTRIBUTIONS.md" "$DST/ATTRIBUTIONS.txt"
cp "$R/LICENSE" "$DST/LICENSE_WINDYFILM.txt"
( cd "$DST" && find . -type f ! -name MD5SUMS.txt | sed 's|^\./||' | LC_ALL=C sort | xargs -d '\n' md5sum > MD5SUMS.txt )

# checks
python3 -I -B "$R/tools/wf_checksyms.py" "$DST/ML/modules/$SYM" "$DST/ML/modules/windyflm.mo" windyflm
( cd "$DST" && md5sum -c --quiet MD5SUMS.txt ) && echo "md5sum -c: OK ($(wc -l < "$DST/MD5SUMS.txt") files)"
bad=
for p in "${EXCL[@]}"; do
  [ "$p" = "ML/SETTINGS/*" ] && continue                 # WINDYFLM.EN is ours
  for f in "$DST"/$p; do [ -f "$f" ] && bad="$bad ${f#$DST/}"; done
done
[ -z "$bad" ] && echo "nothing of card/EXCLUDE.txt left: OK" || { echo "LEFT OVER:$bad"; exit 1; }
[ "$(ls "$DST/ML/SETTINGS")" = WINDYFLM.EN ] || { echo "ML/SETTINGS: $(ls "$DST/ML/SETTINGS")"; exit 1; }
[ "$(md5sum < "$DST/autoexec.bin" | cut -c1-32)" = $AX ] && [ "$(md5sum < "$DST/ML/modules/$SYM" | cut -c1-32)" = $SX ] \
  && echo "core pair: $AX / $SX unchanged"
echo "windyflm.mo md5 $(md5sum < "$DST/ML/modules/windyflm.mo" | cut -c1-32), $(ls "$D"/*.CUB 2>/dev/null | wc -l) cubes,"\
     "$(du -sh "$DST" | cut -f1)"
