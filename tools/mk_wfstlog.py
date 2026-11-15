#!/usr/bin/env python3
"""mk_wfstlog.py OUT.DAT [WFSTAGE.DAT [WFLOG.DAT]] - ML/DATA/WFSTLOG.DAT, the film-stage knob rows on the RAW develop
(chain films on RAW, option (c), docs/DESIGN.md 1a; src/wf_stagek.h "the RAW develop's log grid"). Card data, never
committed; EOS M only (as WFLOG.DAT). WFSTAGE.DAT and WFLOG.DAT default to OUT's directory and must be built first
(build.sh: after both); neither is changed.

With a knob row away from neutral, the module composes a chain film's log table from the film's two halves in
WFSTAGE.DAT, as it composes the 8-bit table, but on the develop's 33^3 log grid: what that needs beyond the halves is
here, measured by tools/mk_wfstage.c (FR_SRC chain, as tools/mk_wfstage.py):
  x section    each log node's exposure axis value per channel: the node's S-Log3 codes decoded as
               tools/mk_genesis_cube.c DOMAIN slog3:M does (the chain's decode, clamped at 0, the inverse of the
               develop's Rec.709 -> S-Gamut3.Cine matrix: tools/mk_genesis_films.py log_domain), measured like the
               negative half's X (mk_wfstage logx); X is the same for every negative (to 1 Q16 unit), so one section
               serves every film
  entries      per (film, print) with both a stage entry and a log table: the log placement the .L33 was made with
               (mk_wfstage logpairs: fit, black, white), and the checksums of the very log table and halves it goes with

  header 32 B  "WFSL", u16 version 1, u16 entries, u16 grid 33, u8 curve, u8 feed (WFLOG.DAT's), u32 index offset 32,
               u32 x offset, u32 file size, u32 FNV-1a of the index, u32 FNV-1a of the x section
  index  36 B  u8 id, u8 variant (catalog.PRINTS order), u8 flags (WFLOG's: bit 1 B&W), u8 0, char code[8] (NUL padded),
               s32 fit (Q16 stops), s32 black (Q16), u32 2^32 / (white - black), u32 Adler-32 of the log table (as
               WFLOG.DAT's index names it), u32 FNV-1a of the negative half, u32 FNV-1a of the print half (as
               WFSTAGE.DAT's checksum lists name them)
  x            at a 512-byte boundary: 35937 nodes (red fastest) x R G B, u16 Q16
Entries are sorted by id, then variant, as both packs. The module takes an entry only when WFSTAGE.DAT's entry of the
same id, variant and code names those halves and WFLOG.DAT holds that table, so the knobs never compose against packs
of another build. A self-test re-reads the file; then mk_wfstage logdat reads all three packs the module's way and
composes every entry at default knobs against its log table.
ENV: FR_SRC, FORCE=1 (re-measure films/tables/.stage/logx.bin and logpairs.bin), CC.
"""
import os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mk_wfstage as S
import mk_genesis_films as G

LG = 33
NODES = LG ** 3
XB = NODES * 6
ALIGN = 512


def rd_stage(path):
    """WFSTAGE.DAT: {(id, variant, code): (negative blob FNV, print blob FNV)}"""
    b = open(path, "rb").read()
    f = struct.unpack_from("<4sHHHHHHHHIIIIIIIIIII", b, 0)
    magic, ver, n, nn, np_ = f[:5]
    io, no, po = f[11], f[12], f[13]
    assert magic == b"WFSG" and ver == 1, path
    out = {}
    for k in range(n):
        i, v, ni, pi, code = struct.unpack_from("<BBBB8s", b, io + 28 * k)
        out[(i, v, code.rstrip(b"\0").decode())] = (struct.unpack_from("<I", b, no + 4 * ni)[0],
                                                    struct.unpack_from("<I", b, po + 4 * pi)[0])
    return out


def rd_log(path):
    """WFLOG.DAT: curve, feed, {(id, variant, code): (flags, Adler-32)}"""
    b = open(path, "rb").read()
    magic, ver, n, grid, tb, io, do, ck, cv, fd, z = struct.unpack_from("<4sHHIIIIIBBH", b, 0)
    assert magic == b"WFLG" and ver == 1 and grid == LG and tb == XB, path
    out = {}
    for k in range(n):
        i, v, fl, code, off, ln, s = struct.unpack_from("<BBBx8sIII", b, io + 24 * k)
        out[(i, v, code.rstrip(b"\0").decode())] = (fl, s)
    return cv, fd, out


def cached(name, cmd, nbytes, dom):
    """films/tables/.stage/<name>, measured when missing, made for another domain, or FORCE=1"""
    path = os.path.join(S.CACHE, name)
    tag = path + ".domain"
    if S.FORCE or not os.path.exists(path) or os.path.getsize(path) != nbytes or not os.path.exists(tag) \
            or open(tag).read() != dom:
        r = S.run(cmd + [path], " ".join(os.path.basename(c) for c in cmd[:2]))
        if r.stderr.strip():
            print("  " + r.stderr.strip())
        open(tag, "w").write(dom)
    b = open(path, "rb").read()
    assert len(b) == nbytes, (path, len(b))
    return b


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    if not a:
        print(__doc__)
        sys.exit(2)
    out = a[0]
    d = os.path.dirname(os.path.abspath(out))
    stp = a[1] if len(a) > 1 else os.path.join(d, "WFSTAGE.DAT")
    lgp = a[2] if len(a) > 2 else os.path.join(d, "WFLOG.DAT")
    for p in (stp, lgp):
        if not os.path.exists(p):
            sys.exit("%s missing: build it first" % p)
    if G.LOG_CURVE != "slog3" or G.LOG_GAMUT != "709":
        sys.exit("the log grid here is S-Log3 fed linear Rec.709 (LOG_CURVE slog3, LOG_GAMUT 709)")
    os.makedirs(S.CACHE, exist_ok=True)
    S.build_tool()
    dom = G.log_domain()
    stage = rd_stage(stp)
    curve, feed, logs = rd_log(lgp)
    assert (curve, feed) == (1, 1), ("WFLOG.DAT is not S-Log3 fed Rec.709", curve, feed)
    ents = [e for e in S.entries() if (e[0], e[1], e[2]) in stage and (e[0], e[1], e[2]) in logs]
    if not ents:
        sys.exit("no pair has both a stage entry and a log table")
    xs = cached("logx.bin", [S.TOOL, "logx", str(min(e[3] for e in ents)), dom], XB, dom)
    pr = struct.unpack("<%di" % (46 * 16 * 3), cached("logpairs.bin", [S.TOOL, "logpairs", dom], 46 * 16 * 12, dom))
    idx = b""
    flat = 0
    for i, v, code, n, p, _ in ents:
        fit, bk, wt = pr[(n * 16 + p) * 3:(n * 16 + p) * 3 + 3]
        if wt - bk <= 1000:                          # no span to pin (as mk_wfstage.py leaves such pairs out)
            flat += 1
            continue
        fl, adl = logs[(i, v, code)]
        nf, pf = stage[(i, v, code)]
        idx += struct.pack("<BBBx8siiIIII", i, v, fl, code.encode().ljust(8, b"\0"), fit, bk,
                           round(4294967296.0 / (wt - bk)), adl, nf, pf)
    n = len(idx) // 36
    xo = (32 + len(idx) + ALIGN - 1) // ALIGN * ALIGN
    size = xo + XB
    hdr = b"WFSL" + struct.pack("<HHHBBIIIII", 1, n, LG, curve, feed, 32, xo, size, S.fnv(idx), S.fnv(xs))
    assert len(hdr) == 32
    blob = hdr + idx + b"\0" * (xo - 32 - len(idx)) + xs
    assert len(blob) == size
    open(out, "wb").write(blob)
    selftest(out, stage, logs)
    print("WFSTLOG: %d entries (%d pairs without a span left out) of %d stage entries and %d log tables" %
          (n, flat, len(stage), len(logs)))
    r = S.run([S.TOOL, "logdat", out, stp, lgp], "mk_wfstage logdat")
    print(r.stdout.rstrip())
    print("%s: %d B" % (os.path.basename(out), size))


def selftest(path, stage, logs):
    """an independent read of the finished file"""
    b = open(path, "rb").read()
    magic, ver, n, grid, cv, fd, io, xo, size, ck, xk = struct.unpack_from("<4sHHHBBIIIII", b, 0)
    assert magic == b"WFSL" and ver == 1 and grid == LG and io == 32 and size == len(b) and xo % ALIGN == 0
    assert xo >= io + 36 * n and size == xo + XB and ck == S.fnv(b[io:io + 36 * n]) and xk == S.fnv(b[xo:])
    seen, last = set(), (-1, -1)
    for k in range(n):
        i, v, fl, code, fit, bk, inv, adl, nf, pf = struct.unpack_from("<BBBx8siiIIII", b, io + 36 * k)
        key = (i, v, code.rstrip(b"\0").decode())
        assert key not in seen and (i, v) > last and inv > 0, key
        seen.add(key)
        last = (i, v)
        assert logs[key] == (fl, adl) and stage[key] == (nf, pf), key
    x = memoryview(b[xo:]).cast("H")
    assert x[0] == x[1] == x[2] == 0, "node 0 (S-Log3 0, linear 0) sits at x 0"
    print("self-test: %d entries re-read, index + x checksums match, each names its WFLOG table and WFSTAGE halves" % n)


if __name__ == "__main__":
    main()
