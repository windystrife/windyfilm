#!/bin/bash
# dr_jpe_check.sh - the Darkroom JPEG encoder src/wf_jpe.c (WindyStrife): module object checks, host test under qemu,
# decodes with standard decoders (djpeg, PIL), PSNR on synthetic frames and on real EOS M JPEGs, memsim timing.
#   bash tools/dr_jpe_check.sh            (WSL; work dir $W, default /root/ml/wf_dr_jpe)
# Env: W, PAIRS (dir with the owner's IMG_xxxx.JPG, default the Day09 set), NREAL (default 3), BENCH=0 skips memsim,
# QEMU / MEMSIM (memsim plugin, as tools/dev_bench.sh). The photos and every file written stay in $W (never commit them);
# the big frames are deleted at the end, the logs kept ($W/check.log, $W/psnr.txt, $W/bench.txt).
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
S=$R/src
W=${W:-/root/ml/wf_dr_jpe}
PAIRS=${PAIRS:-/mnt/f/MagicLatern/_research/raw-path/cr2/Day09_EOSM}
NREAL=${NREAL:-3}
QEMU=${QEMU:-/root/ml/rawengine/bin/qemu-arm}
MEMSIM=${MEMSIM:-/root/ml/yh_cam/libmemsim.so}
PY="python3 -I -B"
mkdir -p $W/syn $W/real $W/out
rm -f $W/syn/* $W/real/* $W/out/*
exec > >(tee $W/check.log) 2>&1

# ---------------------------------------------------------------- 1. module object (build.sh's flags) + its checks
MODFLAGS="-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math -fsingle-precision-constant -fno-common
 -nostdlib -ffreestanding -fomit-frame-pointer -fno-strict-aliasing -std=gnu99 -fno-unwind-tables
 -fno-asynchronous-unwind-tables -Wall -Wextra -Wno-comment -DMODULE -DMODULE_NAME=windyflm -I$S"
arm-none-eabi-gcc $MODFLAGS -O2 -c -o $W/wf_jpe.o $S/wf_jpe.c
arm-none-eabi-readelf -A $W/wf_jpe.o | grep -q "Tag_CPU_arch: v5T$" || { echo "wf_jpe.o: not v5T"; exit 1; }
n=$(arm-none-eabi-objdump -d $W/wf_jpe.o | grep -ciE "[[:space:]](ldr|str)d[a-z]{0,2}[[:space:]]" || true)
[ "$n" = 0 ] || { echo "wf_jpe.o: $n LDRD / STRD"; exit 1; }
if arm-none-eabi-nm -u $W/wf_jpe.o | grep -qE "__aeabi_(l|ul)divmod|__(u)?(div|mod)di3"; then echo "64-bit division"; exit 1; fi
echo "wf_jpe.o: v5T, 0 LDRD/STRD, no 64-bit division; text $(arm-none-eabi-size $W/wf_jpe.o | awk 'NR==2{print $1}') B," \
     "data $(arm-none-eabi-size $W/wf_jpe.o | awk 'NR==2{print $2 + $3}') B; imports: $(arm-none-eabi-nm -u $W/wf_jpe.o | awk '{print $2}' | tr '\n' ' ')"
if arm-none-eabi-readelf -SW $W/wf_jpe.o | grep -q "\.rodata\.str"; then $PY $R/tools/fmt_check.py $W/wf_jpe.o; echo "fmt_check: ok"
else echo "fmt_check: no string literals in wf_jpe.o (nothing to check)"; fi
DW="flo""rin|film""can|film""roll|lib""jpeg|ijg|gene""sis"   # assembled from pieces (provenance_gate.py check 3)
if grep -qiE "$DW" $S/wf_jpe.c $S/wf_jpe.h $R/tests/host/wf_jpe_test.c; then
  echo "deny word in the encoder sources"; exit 1; fi

# ---------------------------------------------------------------- 2. host test program (qemu-arm)
arm-linux-gnueabi-gcc -O1 -marm -march=armv5te -static -w -I$S -o $W/wf_jpe_test $R/tests/host/wf_jpe_test.c $W/wf_jpe.o \
  -Wl,--no-enum-size-warning -Wl,--no-warn-execstack
T="qemu-arm-static $W/wf_jpe_test"
$T fail | tail -2

# ---------------------------------------------------------------- 3. inputs: synthetic + real (Canon JPEG -> UYVY + APP1)
$T synth $W/syn
cat > $W/prep.py <<'EOF'
import sys, numpy as np
from PIL import Image
def app1(data):
    i = 2
    while i + 4 <= len(data) and data[i] == 0xFF:
        m, n = data[i + 1], data[i + 2] << 8 | data[i + 3]
        if m == 0xE1: return data[i:i + 2 + n]
        if m == 0xDA: break
        i += 2 + n
    return None
src, out = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
im = Image.open(src); im.draft("YCbCr", im.size); im = im.convert("YCbCr") if im.mode != "YCbCr" else im
a = np.asarray(im).astype(np.int32)
h, w, _ = a.shape
w &= ~1
a = a[:, :w]
y = a[..., 0].astype(np.uint8)
u = ((a[:, 0::2, 1] + a[:, 1::2, 1] + 1) // 2 - 128).astype(np.int8)
v = ((a[:, 0::2, 2] + a[:, 1::2, 2] + 1) // 2 - 128).astype(np.int8)
o = np.empty((h, w // 2, 4), np.uint8)
o[..., 0] = u.view(np.uint8); o[..., 1] = y[:, 0::2]; o[..., 2] = v.view(np.uint8); o[..., 3] = y[:, 1::2]
o.tofile(out + ".uyvy")
open(out + ".app1", "wb").write(app1(data) or b"")
c = o[:480, :320].copy(); c.tofile(out + "_crop.uyvy")
print(w, h)
EOF
REAL=
k=0
for j in "$PAIRS"/IMG_02*.JPG; do
  [ -f "$j" ] || continue
  b=$(basename "$j" .JPG)
  read jw jh < <($PY $W/prep.py "$j" $W/real/$b)
  echo "$b $jw $jh" >> $W/real/list.txt
  REAL="$REAL $b"
  k=$((k + 1)); [ $k -ge $NREAL ] && break
done

# ---------------------------------------------------------------- 4. encode
while read nm w h; do
  for q in 50 90 100; do $T enc $W/syn/$nm.uyvy $w $h $q 16 4096 $W/out/syn_${nm}_q$q.jpg > /dev/null; done
done < $W/syn/list.txt
for b in $REAL; do
  read nm w h < <(grep "^$b " $W/real/list.txt)
  for q in 90 95 98; do
    $T enc $W/real/$b.uyvy $w $h $q 16 65536 $W/out/real_${b}_q$q.jpg $W/real/$b.app1
  done
done
# the bytes do not depend on the band or the output buffer
b1=$(echo $REAL | awk '{print $1}')
$T same $W/real/${b1}_crop.uyvy 640 480 95 | tail -1
$T same $W/syn/noise_640x480.uyvy 640 480 100 | tail -1
$T same $W/syn/noise_30x17.uyvy 30 17 100 | tail -1

# ---------------------------------------------------------------- 5. decode with djpeg + PIL, markers, PSNR
cat > $W/judge.py <<'EOF'
import sys, os, subprocess, warnings, numpy as np
from PIL import Image
W = sys.argv[1]
rows, bad = [], 0
def markers(d, w, h):
    assert d[:2] == b"\xff\xd8", "no SOI"
    i, sof, seen = 2, None, []
    while True:
        assert d[i] == 0xFF, "segment at %d" % i
        m = d[i + 1]; n = d[i + 2] << 8 | d[i + 3]
        seen.append(m)
        if m == 0xC0: sof = d[i + 4:i + 2 + n]
        if m == 0xDA: i += 2 + n; break
        i += 2 + n
    assert sof is not None and sof[0] == 8 and (sof[1] << 8 | sof[2]) == h and (sof[3] << 8 | sof[4]) == w, "SOF0"
    assert sof[5] == 3 and sof[7] == 0x21 and sof[10] == 0x11 and sof[13] == 0x11, "sampling"
    assert d[-2:] == b"\xff\xd9", "no EOI at the end"
    e = d[i:-2]
    k = 0
    while True:
        k = e.find(b"\xff", k)
        if k < 0: break
        assert e[k + 1] == 0, "marker 0xFF%02X inside the scan" % e[k + 1]
        k += 2
    return seen
def psnr(a, b):
    m = np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2)
    return 99.0 if m == 0 else 10 * np.log10(255.0 ** 2 / m)
def uyvy(path, w, h):
    o = np.fromfile(path, np.uint8).reshape(h, w // 2, 4)
    y = np.empty((h, w), np.uint8); y[:, 0::2] = o[..., 1]; y[:, 1::2] = o[..., 3]
    u = o[..., 0].view(np.int8).astype(np.int32) + 128; v = o[..., 2].view(np.int8).astype(np.int32) + 128
    return y, u, v
def to_rgb(y, cb, cr):
    y = y.astype(np.float64); cb = cb - 128.0; cr = cr - 128.0
    r = y + 1.402 * cr; g = y - 0.344136 * cb - 0.714136 * cr; b = y + 1.772 * cb
    return np.clip(np.rint(np.stack([r, g, b], -1)), 0, 255)
lists = [("syn", l.split()) for l in open(W + "/syn/list.txt")] + [("real", l.split()) for l in open(W + "/real/list.txt")]
for kind, (nm, w, h) in lists:
    w, h = int(w), int(h)
    y0, u0, v0 = uyvy("%s/%s/%s.uyvy" % (W, kind, nm), w, h)
    for f in sorted(os.listdir(W + "/out")):
        if not f.startswith("%s_%s_q" % (kind, nm)) or not f.endswith(".jpg"): continue
        q = int(f[:-4].split("_q")[-1])
        p = W + "/out/" + f
        d = open(p, "rb").read()
        try:
            seen = markers(d, w, h)
            r = subprocess.run(["djpeg", "-nosmooth", "-outfile", p + ".ppm", p], capture_output=True)
            assert r.returncode == 0 and not r.stderr, "djpeg: %d %s" % (r.returncode, r.stderr[:200])
            rgb_d = np.asarray(Image.open(p + ".ppm")).astype(np.float64)
            os.remove(p + ".ppm")
            assert rgb_d.shape == (h, w, 3), "djpeg size"
            with warnings.catch_warnings():
                warnings.simplefilter("error")
                im = Image.open(p); im.draft("YCbCr", im.size); im.load()
            assert im.size == (w, h) and im.mode == "YCbCr", "PIL size / mode %s %s" % (im.size, im.mode)
            a = np.asarray(im).astype(np.int32)
            py, pu, pv = psnr(a[..., 0], y0), psnr((a[:, 0::2, 1] + a[:, 1::2, 1] + 1) // 2, u0), psnr((a[:, 0::2, 2] + a[:, 1::2, 2] + 1) // 2, v0)
            ref = to_rgb(y0, np.repeat(u0, 2, 1), np.repeat(v0, 2, 1))
            prgb = psnr(rgb_d, ref)
            ok = True
            if kind == "real":
                ex = Image.open(p).getexif()
                a1 = open("%s/real/%s.app1" % (W, nm), "rb").read()
                assert len(a1) > 4 and d.find(a1) == 2, "APP1 not passed through byte for byte"
                assert ex.get(0x010F, "").startswith("Canon"), "Exif Make %r" % ex.get(0x010F)
                if q == 95 and py < 40: ok = False
            if kind == "syn" and q == 100 and not nm.startswith("noise") and not nm.startswith("check") and py < 45: ok = False
            if nm.startswith("flat") and q >= 90 and np.max(np.abs(a[..., 0] - y0)) > 2: ok = False
            rows.append((kind, nm, q, len(d), py, pu, pv, prgb, ok))
            if not ok: bad += 1
        except Exception as ex:
            bad += 1
            rows.append((kind, nm, q, len(d), 0, 0, 0, 0, "ERR " + str(ex)))
print("%-5s %-22s %4s %10s %7s %7s %7s %8s %s" % ("kind", "frame", "q", "bytes", "Y dB", "Cb dB", "Cr dB", "RGB dB", "ok"))
for r in rows:
    print("%-5s %-22s %4d %10d %7.2f %7.2f %7.2f %8.2f %s" % r)
print("RESULT judge %d / %d" % (len(rows) - bad, bad))
sys.exit(1 if bad else 0)
EOF
set +e
$PY $W/judge.py $W | tee $W/psnr.txt
JR=${PIPESTATUS[0]}
set -e
command -v exiftool > /dev/null && for b in $REAL; do
  echo "exiftool $b q95: $(exiftool -s -s -s -Make -Model -DateTimeOriginal -ExposureTime -ISO $W/out/real_${b}_q95.jpg | tr '\n' ' ')"; done

# ---------------------------------------------------------------- 6. memsim: one EOS M frame at q95
if [ "${BENCH:-1}" = 1 ] && [ -x $QEMU ] && [ -f $MEMSIM ]; then
  MARK=0x$(arm-linux-gnueabi-nm $W/wf_jpe_test | awk '$NF=="prof_mark"{print $1}')
  read nm w h < <(grep "^$b1 " $W/real/list.txt)
  $QEMU -cpu arm946 -plugin file=$MEMSIM,arg=mark=$MARK,arg=fb=0x48000000,arg=fbn=1048576 -d plugin \
    $W/wf_jpe_test bench $W/real/$b1.uyvy $w $h 95 > $W/bench.txt 2>&1 || { cat $W/bench.txt; echo "bench failed"; exit 1; }
  grep -E "^bench|memsim: phase 2" $W/bench.txt
  awk '/^memsim: phase 2:/ { split($0,a,"total "); split(a[2],b," "); split($0,c,"ins "); split(c[2],d," ");
       printf "memsim encode %s %dx%d q95: %.0f ms (%.0f M instructions) at the 200 MHz ARM946E-S model\n", "'$b1'", '$w', '$h', b[1], d[1]/1e6 }' $W/bench.txt
fi

# ---------------------------------------------------------------- 7. clean the big files (logs stay)
rm -f $W/real/*.uyvy $W/out/real_*.jpg $W/syn/*5184x3456* $W/out/*5184x3456*
echo "dr_jpe_check: done (judge rc $JR), logs in $W"
exit $JR
