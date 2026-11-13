#!/usr/bin/env python3
"""mk_wfpack.py OUT.DAT [--synth N] [--gaps] - ML/DATA/WFFILMS.DAT, the card pack of the WindyFilm tables (DESIGN 4.3).

One entry per (ready film, print variant) of films/catalog.py: scan = films/tables/<table>.T17, cine = <table>_C.T17,
paper = <table>_P.T17. Placeholders and Film LUT cans have none.

  header 32 B  "WFFT", u16 version 1, u16 count, u32 words 4913, u32 entry bytes 19652, u32 index offset 32,
               u32 data offset, u32 FNV-1a of the index, u32 0
  index  24 B  u8 id, u8 variant (0 scan / 1 cine / 2 paper), u8 flags (bit0 v21, bit1 B&W), u8 pad,
               char code[8] (the table code, NUL padded), u32 offset, u32 bytes, u32 FNV-1a of the table
  data         per entry: 4913 words {U s8, Y, V s8, 0}, node Ycell * 289 + Ucell * 17 + Vcell, 16-code cells,
               neutral U / V 0x00

Entries are sorted by id, then variant. The module accepts an entry only when its id and code match the compiled
catalogue. Checks on every table: 4913 words, top byte 0, neutral axis rising with Y, a B&W film's U / V bytes all 0x00.
A self-test re-reads the finished file with an independent parser and checks every checksum.
TEST_SYNTH (--synth N): synthetic entries after the last id, each a copy of a ready film with its U lane shifted by
1 + i (saturating), so a host test can prove which entry a shot used. --gaps: TEST_GAPS (see catalog.py rows()).
"""
import os, sys, struct, hashlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "films"))
sys.path.insert(0, HERE)
import catalog
import mk_wtab as W

WORDS = 4913
ENT = WORDS * 4

def fnv(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xffffffff
    return h


def check(code, w, bw):
    assert len(w) == WORDS, code
    assert all(e >> 24 == 0 for e in w), (code, "top byte")
    ax = [(w[j * 289 + 8 * 17 + 8] >> 8) & 255 for j in range(17)]
    assert all(ax[i] <= ax[i + 1] for i in range(16)), (code, "neutral axis", ax)
    if bw:
        assert all((e & 0xff00ff) == 0 for e in w), (code, "B&W: U / V bytes must be 0x00")

def main():
    a = sys.argv[1:]
    if not a or a[0].startswith("--"):
        print(__doc__); sys.exit(2)
    out = a[0]
    synth = int(a[a.index("--synth") + 1]) if "--synth" in a else 0
    rs, idmax = catalog.rows(synth, "--gaps" in a)
    ents = []
    si = 0
    for f in rs:
        if not f or f["lut"]:
            continue
        bw = f["type"] == "bw"
        for v in f["prints"]:
            b = open(catalog.t17_path(f, v), "rb").read()
            assert len(b) == ENT, (f["code"], v, len(b))
            w = list(struct.unpack("<%dI" % WORDS, b))
            if f.get("synth") and v == "scan":
                d = 1 + si; si += 1
                w = [((e & ~255) | (max(-128, min(127, ((e & 255) - 256 if e & 128 else e & 255) + d)) & 255)) for e in w]
            check(f["table"], w, bw)
            flags = (1 if f["v21"] else 0) | (2 if bw else 0)
            assert len(f["table"]) <= 8 and f["id"] <= 255
            ents.append((f["id"], catalog.PRINTS.index(v), flags, f["table"], struct.pack("<%dI" % WORDS, *w)))
    ents.sort(key=lambda e: (e[0], e[1]))
    n = len(ents)
    idx_off = 32
    data_off = idx_off + 24 * n
    idx, data = b"", b""
    for k, (i, v, fl, c, tb) in enumerate(ents):
        idx += struct.pack("<BBBx8sIII", i, v, fl, c.encode().ljust(8, b"\0"), data_off + k * ENT, ENT, fnv(tb))
        data += tb
    hdr = b"WFFT" + struct.pack("<HHIIIIII", 1, n, WORDS, ENT, idx_off, data_off, fnv(idx), 0)
    assert len(hdr) == 32 and len(idx) == 24 * n
    blob = hdr + idx + data
    assert len(blob) == 32 + 24 * n + ENT * n
    open(out, "wb").write(blob)
    selftest(out, ents)
    print("%s: %d entries (%d films), %d B, md5 %s" % (os.path.basename(out), n, len({e[0] for e in ents}), len(blob),
                                                      hashlib.md5(blob).hexdigest()))

def selftest(path, ents):
    b = open(path, "rb").read()
    magic, ver, n, words, ent, io, do, ck, z = struct.unpack_from("<4sHHIIIIII", b, 0)
    assert magic == b"WFFT" and ver == 1 and n == len(ents) and words == WORDS and ent == ENT and io == 32 and z == 0
    assert do == 32 + 24 * n and ck == fnv(b[io:io + 24 * n]), "index checksum"
    seen = set()
    for k in range(n):
        i, v, fl, code, off, ln, c = struct.unpack_from("<BBBx8sIII", b, io + 24 * k)
        code = code.rstrip(b"\0").decode()
        assert (i, v) not in seen
        seen.add((i, v))
        assert (i, v, fl, code) == ents[k][:4] and ln == ENT and off == do + k * ENT, k
        tb = b[off:off + ln]
        assert fnv(tb) == c, ("table checksum", code, v)
        w = struct.unpack("<%dI" % WORDS, tb)
        check(code, list(w), bool(fl & 2))
    print("self-test: %d entries re-read, index + %d table checksums match, 4913 words, top byte 0, neutral axes rise, B&W U/V 0" % (n, n))

if __name__ == "__main__":
    main()
