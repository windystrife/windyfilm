#!/usr/bin/env python3
"""mk_wflog.py OUT.DAT [LOG_DIR] - ML/DATA/WFLOG.DAT, the chain films' log tables for the RAW develop (option (c),
docs/DESIGN.md 1a; the reader is src/wf_dev.c wf_dev_build_pack).

One entry per (chain film, print) of films/catalog.py that has its table in LOG_DIR (default films/tables/log, card
data made by tools/mk_genesis_films.py log): <table><print suffix>.L33, e.g. G08P04.L33. LOG_DIR/domain.txt names the
develop curve and the feed the tables were made for; it goes into the header.

  header 32 B  "WFLG", u16 version 1, u16 count, u32 grid 33, u32 table bytes 215622, u32 index offset 32,
               u32 data offset, u32 FNV-1a of the index, u8 curve (1 S-Log3 .. 5, the develop's WF_LL_*), u8 feed
               (1 = the chain was fed linear Rec.709, 0 = the curve's own gamut), u16 0
  index  24 B  u8 id, u8 variant (catalog.PRINTS index: 3 + print), u8 flags (bit1 B&W), u8 0, char code[8] (the
               table code, NUL padded), u32 offset, u32 bytes, u32 Adler-32 of the table
  data         the .L33 bytes as they are: 33^3 nodes, red fastest, R' G' B' u16 little-endian (value x 65535). Each
               distinct table is stored once, at a 512-byte boundary (whole SD sectors per read); identical prints
               (the chain's source holds five identical slot pairs) point at the same offset.

Entries are sorted by id, then variant. Checks on every table: 215622 bytes, the neutral axis (node i, i, i) never
falls more than 1/128 below its running top (the chain wobbles by up to 0.43 % at its white, node 16 -> 17 in 56 of
the 643 S-Log3 tables), a B&W film's R' = G' = B' everywhere. A self-test re-reads the finished file with an independent parser and
checks the header, the index checksum, every entry against its .L33 and every table checksum (zlib.adler32).
Exit status 1 with "no log tables" when LOG_DIR holds none (build.sh reports the pack as not built, not fatal).
"""
import hashlib, os, re, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "films"))
import catalog

N = 33
TB = N * N * N * 6                      # 215622
ALIGN = 512
FALL = 512                              # 1/128 of full scale; the 643 S-Log3 tables of 15/11/2026 fall by 281 at most
CURVES = {"slog3": 1, "vlog": 2, "logc3": 3}


def fnv(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xffffffff
    return h


def up(x):
    return (x + ALIGN - 1) // ALIGN * ALIGN


def domain(logdir):
    """domain.txt: 'curve <name> <n> gamut <709|native> <gamut name>' -> (curve number, feed)"""
    p = os.path.join(logdir, "domain.txt")
    t = open(p).read().split()
    assert len(t) >= 5 and t[0] == "curve" and t[3] == "gamut", (p, t)
    curve = int(t[2])
    assert 1 <= curve <= 5 and CURVES.get(t[1], curve) == curve, (p, "curve", t[1], t[2])
    assert t[4] in ("709", "native"), (p, "gamut", t[4])
    return curve, 1 if t[4] == "709" else 0


def check(code, b, bw):
    assert len(b) == TB, (code, len(b))
    ax = [struct.unpack_from("<3H", b, 6 * (i + N * i + N * N * i)) for i in range(N)]
    for c in range(3):                  # a fall below the running top: <= FALL (the chain's own wobble at its white)
        top = 0
        for i in range(N):
            assert ax[i][c] >= top - FALL, (code, "neutral axis falls", c, i, [a[c] for a in ax])
            top = max(top, ax[i][c])
    if bw:
        v = memoryview(b).cast("H")
        assert v[0::3] == v[1::3] == v[2::3], (code, "B&W: R' = G' = B' everywhere")


def main():
    a = sys.argv[1:]
    if not a or a[0].startswith("-"):
        print(__doc__)
        sys.exit(2)
    out = a[0]
    logdir = a[1] if len(a) > 1 else os.path.join(HERE, "..", "films", "tables", "log")
    if not os.path.isdir(logdir) or not os.path.exists(os.path.join(logdir, "domain.txt")):
        print("no log tables: %s has no domain.txt (tools/mk_genesis_films.py log makes them)" % logdir)
        sys.exit(1)
    curve, feed = domain(logdir)
    rs, idmax = catalog.rows()
    ents, missing, rows = [], [], 0
    for f in rs:
        if not f or f["lut"] or not f.get("derived") or not re.fullmatch(r"G\d\d", f["table"] or ""):
            continue
        prs = [v for v in (f["prints"] or ()) if v.startswith("p")]
        if not prs:
            continue
        rows += 1
        bw = f["type"] == "bw"
        for v in prs:
            p = os.path.join(logdir, f["table"] + catalog.PSUFFIX[v] + ".L33")
            if not os.path.exists(p):
                missing.append(os.path.basename(p))
                continue
            b = open(p, "rb").read()
            check(f["table"] + catalog.PSUFFIX[v], b, bw)
            assert len(f["table"]) <= 8 and f["id"] <= 255
            ents.append((f["id"], catalog.PRINTS.index(v), 2 if bw else 0, f["table"], b, os.path.basename(p)))
    if missing:
        print("log pack: %d offered tables not in %s (first: %s)" % (len(missing), logdir, ", ".join(missing[:4])))
    if not ents:
        print("no log tables: none of the %d chain rows' tables is in %s" % (rows, logdir))
        sys.exit(1)
    ents.sort(key=lambda e: (e[0], e[1]))
    n = len(ents)
    doff = up(32 + 24 * n)
    where, blobs, end = {}, [], doff     # md5 of a table -> its offset; the distinct tables in file order
    idx = b""
    for i, v, fl, c, b, _ in ents:
        h = hashlib.md5(b).digest()
        if h not in where:
            where[h] = end
            blobs.append(b)
            end = up(end + TB)
        idx += struct.pack("<BBBx8sIII", i, v, fl, c.encode().ljust(8, b"\0"), where[h], TB, zlib.adler32(b))
    hdr = b"WFLG" + struct.pack("<HHIIIIIBBH", 1, n, N, TB, 32, doff, fnv(idx), curve, feed, 0)
    assert len(hdr) == 32 and len(idx) == 24 * n
    with open(out, "wb") as o:
        o.write(hdr + idx + b"\0" * (doff - 32 - 24 * n))
        for k, b in enumerate(blobs):
            o.write(b)
            if k < len(blobs) - 1:
                o.write(b"\0" * (up(TB) - TB))
    selftest(out, ents, curve, feed, len(blobs))
    size = os.path.getsize(out)
    md5 = hashlib.md5(open(out, "rb").read()).hexdigest()
    print("%s: %d entries (%d films), %d tables stored, curve %d feed %s, %d B, md5 %s"
          % (os.path.basename(out), n, len({e[0] for e in ents}), len(blobs), curve, "709" if feed else "native",
             size, md5))


def selftest(path, ents, curve, feed, nblobs):
    """an independent read of the finished file"""
    with open(path, "rb") as f:
        b = f.read()
    magic, ver, n, grid, tb, io, do, ck, cv, fd, z = struct.unpack_from("<4sHHIIIIIBBH", b, 0)
    assert magic == b"WFLG" and ver == 1 and n == len(ents) and grid == N and tb == TB and io == 32 and z == 0
    assert (cv, fd) == (curve, feed), ("curve / feed", cv, fd)
    assert do >= 32 + 24 * n and do % ALIGN == 0 and ck == fnv(b[io:io + 24 * n]), "index checksum"
    seen, offs = set(), set()
    for k in range(n):
        i, v, fl, code, off, ln, s = struct.unpack_from("<BBBx8sIII", b, io + 24 * k)
        code = code.rstrip(b"\0").decode()
        assert (i, v) not in seen, (i, v)
        seen.add((i, v))
        e = ents[k]
        assert (i, v, fl, code) == e[:4] and ln == TB, (k, e[5])
        assert off >= do and off % ALIGN == 0 and off + ln <= len(b), (e[5], "offset", off)
        t = b[off:off + ln]
        assert t == e[4], (e[5], "table bytes differ from the .L33")
        assert zlib.adler32(t) == s, (e[5], "table checksum")
        offs.add(off)
    assert len(offs) == nblobs, ("every stored table is some entry's", len(offs), nblobs)
    assert len(b) == max(offs) + TB, "file ends after the last table"
    print("self-test: %d entries re-read, index + %d table checksums match, every entry = its .L33, %d tables stored"
          " once each, offsets on %d-byte boundaries" % (n, n, nblobs, ALIGN))


if __name__ == "__main__":
    main()
