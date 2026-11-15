#!/usr/bin/env python3
"""mk_wfstage.py OUT.DAT [--check] - ML/DATA/WFSTAGE.DAT, the two halves of the film-stage chain (DESIGN 1a, branch
film-stage; src/wf_stagek.h). WindyStrife's own packer: the halves are measured by tools/mk_wfstage.c, which links the
chain in the port tree (FR_SRC, as tools/mk_genesis_films.py) and is never part of the module. Card data, never
committed; a tree without the port tree builds no WFSTAGE.DAT and the films keep their shipped tables (the knob rows
then stay hidden).

One entry per (film, print variant) of films/catalog.py whose table is a chain pair: films/tables/G<nn>P<pp>.T17 (the
chain rows; <pp> is the label slot, its halves the chain print tools/mk_genesis_films.py PRINT_CHAIN names) and the four
stage films of tools/mk_genesis_films.py (GX500T ...).
  header 64 B  "WFSG", u16 version 1, u16 entries, u16 negatives, u16 prints, u16 33, u16 33, u16 257, u16 1021,
               u32 negative blob bytes, u32 print blob bytes, u32 index offset 64, u32 negative checksums offset,
               u32 print checksums offset, u32 negatives offset, u32 prints offset,
               u32 FNV-1a of index + both checksum lists, u32 file size, u32 0, u32 0
  index  28 B  u8 id, u8 variant (catalog.PRINTS order), u8 negative blob, u8 print blob, char code[8] (the table
               code, NUL padded), s32 fitted exposure (Q16 stops), s32 black (Q16), u32 2^32 / (white - black),
               u32 FNV-1a of the shipped .T17 (the module takes the entry only when WFFILMS.DAT holds that very table,
               so the knobs never compose against a pack made from another chain build)
  checksums    u32 FNV-1a per negative blob, then per print blob
  blobs        struct wf_sg_neg (434,856 B) each, then struct wf_sg_prn (217,192 B) each
Entries are sorted by id, then variant. The module takes an entry only when id and code match its catalogue.
ENV: FR_SRC (default as tools/mk_genesis_films.py), FORCE=1 (re-measure the cached halves in films/tables/.stage/),
CC (default gcc).
"""
import os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "films"))
sys.path.insert(0, HERE)
import catalog
import mk_genesis_films as G

NA, NB, N1, NX = 33, 33, 257, 1021
NEG_BYTES = 24 + 2 * (NX + 1) + 2 * 3 * N1 + 2 + 12 * NA ** 3
PRN_BYTES = 24 + 2 * 3 * N1 + 2 + 6 * NB ** 3 + 2
CACHE = os.path.join(REPO, "films", "tables", ".stage")
BUILD = os.path.join("/tmp", "mk_wfstage_build")
TOOL = os.path.join(BUILD, "mk_wfstage")
CC = os.environ.get("CC", "gcc")
FORCE = os.environ.get("FORCE", "") == "1"
# the four stage films (catalog.py 85..88): table name -> (negative, print), from tools/mk_genesis_films.py PAIRS
GX = {G.STAGE[s.split(":")[0]]: (int(s.split(":")[1]), int(s.split(":")[2])) for s in G.PAIRS}


def fnv(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xffffffff
    return h


def run(cmd, what):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit("%s failed:\n%s%s" % (what, r.stdout[-1500:], r.stderr[-1500:]))
    return r


def build_tool():
    srcs = {n: os.path.join(G.FR_SRC, n) for n in ("fr_glook.c", "fr_glook_film.c", "fr_glook_print.S")}
    for s in srcs.values():
        if not os.path.exists(s):
            sys.exit("FR_SRC=%s does not hold %s: no WFSTAGE.DAT" % (G.FR_SRC, os.path.basename(s)))
    os.makedirs(BUILD, exist_ok=True)
    inc = "-I" + G.FR_SRC
    o = lambda n: os.path.join(BUILD, n + ".o")
    # the chain's film stage with its own trace switch on; the trace lands in tools/mk_wfstage.c (mk_wfstage_hook.h)
    run([CC, "-O2", inc, "-DGL_FILM_TRACE", "-include", os.path.join(HERE, "mk_wfstage_hook.h"), "-c",
         srcs["fr_glook_film.c"], "-o", o("film")], "chain film stage")
    run([CC, "-O2", inc, "-c", srcs["fr_glook.c"], "-o", o("glook")], "chain")
    run([CC, "-O2", inc, "-c", srcs["fr_glook_print.S"], "-o", o("print")], "chain print data")
    run([CC, "-O2", "-Wall", inc, "-I" + os.path.join(REPO, "src"), os.path.join(HERE, "mk_wfstage.c"),
         os.path.join(REPO, "src", "wf_stagek.c"), o("film"), o("glook"), o("print"), "-lm", "-o", TOOL],
        "mk_wfstage.c")


def entries():
    """(id, variant index, table code, negative, print, .T17 path) per catalogue (film, variant) that is a chain pair
    whose shipped table is there (the code is the table code, as WFFILMS.DAT and the module's catalogue hold it)"""
    rs, _ = catalog.rows()
    out = []
    for f in rs:
        if not f or f["lut"]:
            continue
        for v in f["prints"]:
            t17 = catalog.t17_path(f, v)
            name = os.path.basename(t17)[:-4]
            m = re.fullmatch(r"G(\d\d)P(\d\d)", name)
            # a chain row's <pp> is the label slot; the halves are the chain's own print (mk_genesis_films PRINT_CHAIN)
            pair = (int(m.group(1)), G.PRINT_CHAIN[int(m.group(2))]) if m else GX.get(name)
            if pair and pair[1] is not None and os.path.exists(t17):
                assert len(f["table"]) <= 8 and f["id"] <= 255, f["table"]
                out.append((f["id"], catalog.PRINTS.index(v), f["table"], pair[0], pair[1], t17))
    out.sort(key=lambda e: (e[0], e[1]))
    return out


def half(kind, k, nbytes):
    """a cached half (films/tables/.stage/N<nn>.bin / P<pp>.bin), measured when missing or FORCE=1"""
    path = os.path.join(CACHE, "%s%02d.bin" % ("N" if kind == "neg" else "P", k))
    if FORCE or not os.path.exists(path) or os.path.getsize(path) != nbytes:
        r = run([TOOL, kind, str(k), path], "mk_wfstage %s %d" % (kind, k))
        print("  " + r.stderr.strip())
    b = open(path, "rb").read()
    assert len(b) == nbytes, (path, len(b))
    return b


def main():
    a = sys.argv[1:]
    if not a or a[0].startswith("--"):
        print(__doc__)
        sys.exit(2)
    out = a[0]
    os.makedirs(CACHE, exist_ok=True)
    build_tool()
    ents = entries()
    if not ents:
        sys.exit("no chain pairs in the catalogue")
    negs = sorted({e[3] for e in ents})
    prns = sorted({e[4] for e in ents})
    pairs_path = os.path.join(CACHE, "pairs.bin")
    if FORCE or not os.path.exists(pairs_path):
        run([TOOL, "pairs", pairs_path], "mk_wfstage pairs")
    pr = struct.unpack("<%di" % (46 * 16 * 3), open(pairs_path, "rb").read())
    print("WFSTAGE: %d catalogue pairs, %d negatives, %d prints" % (len(ents), len(negs), len(prns)))

    # a pair whose print white sits (almost) on its black has no usable span to pin: it keeps its shipped table only
    flat = [e for e in ents if (lambda r: r[2] - r[1])(pr[(e[3] * 16 + e[4]) * 3:(e[3] * 16 + e[4]) * 3 + 3]) <= 1000]
    for e in flat:
        print("  %s %s (negative %d, chain print %d): print white on its black, left out" % (e[2], catalog.PRINTS[e[1]], e[3], e[4]))
    ents = [e for e in ents if e not in flat]
    negs = sorted({e[3] for e in ents})
    prns = sorted({e[4] for e in ents})
    nb = [half("neg", n, NEG_BYTES) for n in negs]
    pb = [half("print", p, PRN_BYTES) for p in prns]
    idx = b""
    for i, v, code, n, p, t17 in ents:
        fit, bk, wt = pr[(n * 16 + p) * 3:(n * 16 + p) * 3 + 3]
        inv = round(4294967296.0 / (wt - bk))
        idx += struct.pack("<BBBB8siiII", i, v, negs.index(n), prns.index(p), code.encode().ljust(8, b"\0"), fit, bk,
                           inv, fnv(open(t17, "rb").read()))
    ntab = b"".join(struct.pack("<I", fnv(b)) for b in nb)
    ptab = b"".join(struct.pack("<I", fnv(b)) for b in pb)
    io = 64
    no = io + len(idx)
    po = no + len(ntab)
    do = po + len(ptab)
    do = (do + 3) & ~3
    pd = do + NEG_BYTES * len(nb)
    size = pd + PRN_BYTES * len(pb)
    hdr = b"WFSG" + struct.pack("<HHHHHHHHIIIIIIIIIII", 1, len(ents), len(nb), len(pb), NA, NB, N1, NX, NEG_BYTES,
                                PRN_BYTES, io, no, po, do, pd, fnv(idx + ntab + ptab), size, 0, 0)
    assert len(hdr) == 64
    blob = hdr + idx + ntab + ptab
    blob += b"\0" * (do - len(blob)) + b"".join(nb) + b"".join(pb)
    assert len(blob) == size
    open(out, "wb").write(blob)
    selftest(out, ents)
    # read back the way the module reads it, every entry composed at default knobs against its shipped table
    lst = os.path.join(BUILD, "tables.txt")
    bw = {f["id"] for f in catalog.rows()[0] if f and f["type"] == "bw"}   # WF_F_BW: the module composes Y only
    open(lst, "w").write("".join(e[5] + (" bw" if e[0] in bw else "") + "\n" for e in ents))
    r = run([TOOL, "dat", out, lst], "mk_wfstage dat")
    print(r.stdout.rstrip())
    print("%s: %d B" % (os.path.basename(out), size))
    if "--check" in a:
        for i, v, code, n, p, t17 in ents[::97]:
            r = run([TOOL, "check", str(n), str(p), os.path.join(CACHE, "N%02d.bin" % n),
                     os.path.join(CACHE, "P%02d.bin" % p)] + ([t17] if os.path.exists(t17) else []), "check")
            print(r.stdout.rstrip())


def selftest(path, ents):
    b = open(path, "rb").read()
    f = struct.unpack_from("<4sHHHHHHHHIIIIIIIIIII", b, 0)
    magic, ver, n, nn, np_, na, nb, n1, nx, negb, prnb, io, no, po, do, pd, ck, size, z0, z1 = f
    assert magic == b"WFSG" and ver == 1 and n == len(ents) and (na, nb, n1, nx) == (NA, NB, N1, NX)
    assert (negb, prnb, io) == (NEG_BYTES, PRN_BYTES, 64) and size == len(b) and z0 == z1 == 0
    assert no == io + 28 * n and po == no + 4 * nn and do >= po + 4 * np_ and do % 4 == 0 and pd == do + nn * negb
    assert ck == fnv(b[io:po + 4 * np_]), "index checksum"
    for k in range(nn):
        assert struct.unpack_from("<I", b, no + 4 * k)[0] == fnv(b[do + k * negb:do + (k + 1) * negb]), ("neg", k)
    for k in range(np_):
        assert struct.unpack_from("<I", b, po + 4 * k)[0] == fnv(b[pd + k * prnb:pd + (k + 1) * prnb]), ("print", k)
    seen = set()
    for k in range(n):
        i, v, ni, pi, code, fit, bk, inv, tf = struct.unpack_from("<BBBB8siiII", b, io + 28 * k)
        assert (i, v) not in seen and ni < nn and pi < np_ and inv > 0 and tf == fnv(open(ents[k][5], "rb").read())
        seen.add((i, v))
        assert (i, v, code.rstrip(b"\0").decode()) == ents[k][:3], k
    print("self-test: %d entries, index + %d + %d blob checksums match" % (n, nn, np_))


if __name__ == "__main__":
    main()
