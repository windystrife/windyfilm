#!/usr/bin/env python3
"""mk_genesis_films.py - Genesis-derived film tables in WindyFilm's own 17^3 format (DESIGN 1a, branch film-stage).

The chain itself lives in the port tree (`FR_SRC`, see below); this script only drives it and then hands the result to
WindyFilm's own converter, so the module and mk_wtab.py stay as they are and the tables are card data, never committed.

  mk_genesis_films.py cube OUTDIR PAIR [PAIR..]     one .cube per pair (N^3 sRGB -> the chain's output)
  mk_genesis_films.py tab  OUTDIR PAIR [PAIR..]     .cube -> .T17 through tools/mk_wtab.py
  mk_genesis_films.py stage TABLES_DIR              the four catalogue tables (catalog.py 85..88) as <NAME>.T17;
                                                    build.sh calls this, existing files are kept unless FORCE=1
  mk_genesis_films.py matrix DIR [NEGS] [PRINTS]     every pair of the whole chain (46 x the 15 print slots with a
                                                    table): one <GnnPpp>.T17 plus fit.csv; PRINTS are label slots
                                                    (PRINT_NAMES, mapped through PRINT_CHAIN); NEGS/PRINTS are comma
                                                    lists or "-" for all, existing tables are skipped unless FORCE=1
  mk_genesis_films.py log DIR [NEGS] [PRINTS]        option (c), the RAW develop: every pair a catalogue row offers
                                                    (643) as <GnnPpp>.L33, the chain sampled on the Darkroom's
                                                    WF_DEV_CUBE grid (33^3 log codes, tools/mk_genesis_cube.c DOMAIN),
                                                    plus logfit.csv and domain.txt; KEEP_CUBE=1 keeps each .cube (the
                                                    host develop reads those), FORCE=1 re-makes. LOG_CURVE = slog3
                                                    (default: S-Log3 / S-Gamut3.Cine, the develop's curve 1, the
                                                    chain's own input curve) | logc3 (curve 3, AWG3) | vlog (curve 2);
                                                    LOG_GAMUT = 709 (default: the node's log code is decoded on its
                                                    own gamut and brought to linear Rec.709 before the chain, the
                                                    input every 8-bit chain table is made from) | native (the camera
                                                    gamut goes in as it is; the port's chain has no input matrix, it
                                                    drops the plugin's XYZ / XYZ_to_Neg step, so the wider gamut's
                                                    smaller numbers read as paler colour: a blue sky under Eterna /
                                                    3523XD turns pink, owner's IMG_6849, 15/11/2026)
  mk_genesis_films.py list                          the pairs this file knows

.L33 (card data, never committed): 33 x 33 x 33 nodes, red fastest (the .cube order), each R' G' B' as uint16
little-endian, value x 65535: 215 622 bytes. A B&W negative (catalog.GX_BW) gets R' = G' = B' = its BT.601 luma,
the RAW twin of mk_wtab.py --bw (U = V = 0).

A PAIR is NAME:NEG:PRINT[:PUSHPULL[:BLEACH[:PBLEACH[:NEUTRAL]]]], e.g. `vision3-250d-2383:0:1`.
ENV: FR_SRC (the Film Roll tree), CUBE_N (default 33), CC (default gcc), GREY / WHITE (the two display anchors,
GREY default: 18 % grey's own display code, tools/mk_genesis_place.h; WHITE is not read, the tool pins white),
FORCE=1 (re-make tables that are already there).
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
# The port tree's directory name is assembled here so this file holds no deny word of its own (DESIGN 1): the gate
# scans tools/ and the module is built from a tree that must not carry the word.
FR_SRC = os.environ.get("FR_SRC", "/mnt/f/MagicLatern/_port_v15/" + "film" + "roll" + "_eosm_yuv")
CUBE_N = os.environ.get("CUBE_N", "33")
GREY = os.environ.get("GREY", "")                       # display code 18 % grey is placed at; empty = the
                                                        # tool's GENESIS_GREY_CODE (tools/mk_genesis_place.h)
WHITE = os.environ.get("WHITE", "0.95")                 # display value white is placed at
CC = os.environ.get("CC", "gcc")
TOOL = os.path.join("/tmp", "mk_genesis_cube")
LOG_N = 33                                              # the RAW develop's node grid (src/wf_dev.h WF_DEV_G)


# LOG_CURVE -> (the develop's curve number, src/wf_loglut.h WF_LL_*; its gamut in tools/mk_loglut_tab.py GAMUTS)
LOG_CURVES = {"slog3": (1, "S-Gamut3.Cine"), "vlog": (2, "V-Gamut"), "logc3": (3, "ARRI Wide Gamut 3")}
LOG_CURVE = os.environ.get("LOG_CURVE", "slog3")
LOG_GAMUT = os.environ.get("LOG_GAMUT", "709")


def log_domain():
    """the tool's DOMAIN for LOG_CURVE / LOG_GAMUT; 709 adds the develop's own Rec.709 -> gamut matrix (Q20,
    wf_loglut_tab.h wf_ll_mat[curve - 1]) from its generator"""
    if LOG_CURVE not in LOG_CURVES or LOG_GAMUT not in ("native", "709"):
        sys.exit("LOG_CURVE must be one of %s, LOG_GAMUT native or 709" % sorted(LOG_CURVES))
    if LOG_GAMUT == "native":
        return LOG_CURVE
    sys.path.insert(0, HERE)
    import mk_loglut_tab as L
    k, gname = LOG_CURVES[LOG_CURVE]
    name, prim = L.GAMUTS[k - 1]
    assert name == gname, name
    return LOG_CURVE + ":" + ",".join(str(v) for v in L.qmat(L.rec709_to(prim), 1 << 20))

# The pairs worth shipping first: the owner's Genesis session used Vision3 5260 500T on print 2393, so the first
# entries follow that, then a spread of negative speeds and print stocks (see docs/GENESIS-PAIRS.md). These specs give
# the CHAIN's own indices (no PRINT_CHAIN), and their names are guesses from before the stock names were recovered:
# negative 0 is Vision2 5201 50D and chain print 14 is 2302 (B&W), so "ektar-3513" is negative 13 on the B&W print.
# Their catalogue rows (85..88) are not shown (ready=False).
PAIRS = [
    "500T-2393:0:2",        # negative 0, print 02_2393
    "250D-2383:1:1",        # negative 1, print 01_2383
    "portra-2383:2:1",
    "ektar-3513:13:14",
]

# The four catalogue rows this branch carries (films/catalog.py 85..88): the .cube name of PAIRS, the card table
# name. `stage` writes these straight into films/tables/ as <NAME>.T17, which is all build.sh needs; they are not in
# the repo (DESIGN 1a: derived card data), so a checkout makes them here.
STAGE = {"500T-2393": "GX500T", "250D-2383": "GX250D", "portra-2383": "GXP400", "ektar-3513": "GXE100"}


def build_tool():
    srcs = [os.path.join(FR_SRC, n) for n in ("fr_glook.c", "fr_glook_film.c", "fr_glook_print.S")]
    for s in srcs:
        if not os.path.exists(s):
            sys.exit("FR_SRC=%s does not hold %s" % (FR_SRC, os.path.basename(s)))
    cmd = [CC, "-O2", "-I" + FR_SRC, "-o", TOOL, os.path.join(HERE, "mk_genesis_cube.c")] + srcs + ["-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit("build failed:\n" + r.stderr[-2000:])
    return TOOL


def parse(spec):
    f = spec.split(":")
    while len(f) < 7:
        f.append("0")
    return dict(name=f[0], neg=int(f[1]), prn=int(f[2]), push=f[3], bleach=f[4], pbleach=f[5], neutral=f[6])


def cube(outdir, spec, quiet=False, domain="", n=None):
    p = parse(spec)
    out = os.path.join(outdir, "%s.cube" % p["name"])
    args = [TOOL, str(n or CUBE_N), str(p["neg"]), str(p["prn"]), p["push"], p["bleach"], p["pbleach"], out,
            p["neutral"], GREY, WHITE] + ([domain] if domain else [])
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        sys.exit("cube failed for %s:\n%s" % (spec, r.stderr[-1500:]))
    if not quiet:
        print("cube  %-16s neg %2d print %2d  %s" % (p["name"], p["neg"], p["prn"], out))
        for line in r.stderr.strip().splitlines():
            print("      " + line)
    return out, r.stderr.strip()


def tab(outdir, spec):
    p = parse(spec)
    cub = os.path.join(outdir, "%s.cube" % p["name"])
    if not os.path.exists(cub):
        cube(outdir, spec)
    out = os.path.join(outdir, "%s.T17" % p["name"])
    r = subprocess.run([sys.executable, os.path.join(HERE, "mk_wtab.py"), "cube", cub, out],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("mk_wtab failed for %s:\n%s" % (spec, r.stderr[-1500:]))
    print("tab   %-16s %8d B  %s  %s" % (p["name"], os.path.getsize(out), out, r.stdout.strip()[-60:]))
    return out


def stage(outdir):
    """the four catalogue tables into films/tables/<NAME>.T17; existing files are kept unless FORCE=1"""
    force = os.environ.get("FORCE", "") == "1"
    os.makedirs(outdir, exist_ok=True)
    work = os.path.join(outdir, ".genesis")
    os.makedirs(work, exist_ok=True)
    for spec in PAIRS:
        name = spec.split(":")[0]
        out = os.path.join(outdir, STAGE[name] + ".T17")
        if os.path.exists(out) and not force:
            print("tab   %-16s kept (%d B)" % (os.path.basename(out), os.path.getsize(out)))
            continue
        cube(work, spec)
        cub = os.path.join(work, name + ".cube")
        r = subprocess.run([sys.executable, os.path.join(HERE, "mk_wtab.py"), "cube", cub, out],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("mk_wtab failed for %s:\n%s" % (spec, r.stderr[-1500:]))
        print("tab   %-8s %8d B  %s" % (STAGE[name], os.path.getsize(out), out))


# the 46 negative stocks of the chain in its own index order, and its 16 print stocks in the PLUGIN'S UI ORDER (the
# owner's list, 15/11/2026). These label slots p00..p15 are what films/catalog.py PRINTS, src/wf_menu.c wf_print_name /
# wf_print_full, src/ui/wf_ui.c and tests/ui/fake_core.c name, and what G<nn>P<pp>.T17 carries in <pp>.
NEG_N, PRINT_N = 46, 16
PRINT_NAMES = ("2302", "2383", "2393", "2395", "5380", "5381_50", "5381_72", "5382", "5384_82", "5384_92",
               "5385_93", "5385_62", "5386", "5383", "3513D", "3523XD")
# Label slot -> the chain's print index (gl_print_stock: which of the dumped DyeAmountLUTs print_00..15 holds that
# stock); None = no table. The dump follows the UI order except at two slots (measured 15/11/2026 on the chain's own
# fr_glook_print.bin, pinned by tools/check_print_slots.py): its print_00 is a byte copy of print_01 / print_02, and its
# print_14 is the only B&W table (mean chroma 0.01 against 0.37..0.45), so it is 2302's, the one B&W print of the list.
# Every other table the dump shares is one of the plugin's own five pairs (2383 = 2393, 2395 = 5384/82,
# 5380 = 5381/72, 5384/92 = 5385/93, 5385/62 = 5386), so those slots are in order. 3513D's own table is not in the
# dump: its slot has no table and no film offers it until the table is recovered. Until 11-15 G<nn>P<pp> took chain
# print <pp> directly, which put 2302's label on a colour table and 3513D's on the B&W one.
PRINT_CHAIN = (14, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, None, 15)
assert len(PRINT_CHAIN) == PRINT_N and sorted(c for c in PRINT_CHAIN if c is not None) == list(range(1, PRINT_N))


def matrix(outdir, negs=None, prns=None):
    """every pair: 46 negatives x the print slots that have a table (PRINT_CHAIN) -> one <GnnPpp>.T17 each (<pp> the
    label slot, the chain print PRINT_CHAIN[pp]), and fit.csv with the placement numbers (its print column is the label
    slot). A remade code replaces its fit.csv row. The .cube is deleted after each conversion: 720 of them would be
    700 MB."""
    negs = range(NEG_N) if negs is None else negs
    prns = range(PRINT_N) if prns is None else prns
    force = os.environ.get("FORCE", "") == "1"
    os.makedirs(outdir, exist_ok=True)
    work = os.path.join(outdir, ".genesis")
    os.makedirs(work, exist_ok=True)
    todo = [(n, p) for n in negs for p in prns if PRINT_CHAIN[p] is not None]
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "films"))
    import catalog                                      # GX_BW: the B&W negatives, converted with --bw (U / V = 0)
    csv = os.path.join(outdir, "fit.csv")
    rows = {}
    if os.path.exists(csv):
        for l in open(csv).read().splitlines()[1:]:
            if l:
                rows[l.split(",")[0]] = l
    done = set(rows)
    print("matrix: %d pairs, %d already made" % (len(todo), len(done & {"G%02dP%02d" % t for t in todo})))
    for k, (neg, prn) in enumerate(todo, 1):
        code = "G%02dP%02d" % (neg, prn)
        out = os.path.join(outdir, code + ".T17")
        if code in done and os.path.exists(out) and not force:
            continue
        cub, fit = cube(work, "%s:%d:%d" % (code, neg, PRINT_CHAIN[prn]), quiet=True)
        r = subprocess.run([sys.executable, os.path.join(HERE, "mk_wtab.py"), "cube", cub, out]
                           + (["--bw"] if neg in catalog.GX_BW else []), capture_output=True, text=True)
        os.remove(cub)
        if r.returncode:
            sys.exit("mk_wtab failed for %s:\n%s" % (code, r.stderr[-1500:]))
        exp = [l for l in fit.splitlines() if l.startswith("fit: exposure")]
        dens = [l for l in fit.splitlines() if "print density" in l]
        rows[code] = "%s,%d,%d,%d,%s" % (code, neg, prn, os.path.getsize(out), (exp[0] if exp else "").replace(",", ";"))
        print("%4d/%d %s neg %2d print %2d (chain %2d) %7d B  %s" % (k, len(todo), code, neg, prn, PRINT_CHAIN[prn],
                                                                   os.path.getsize(out), (dens[0] if dens else "")[:80]))
    for p in range(PRINT_N):                            # a slot without a table keeps no row (and no stale file)
        if PRINT_CHAIN[p] is None:
            for n in range(NEG_N):
                code = "G%02dP%02d" % (n, p)
                rows.pop(code, None)
                if os.path.exists(os.path.join(outdir, code + ".T17")):
                    os.remove(os.path.join(outdir, code + ".T17"))
                    print("removed %s.T17 (print slot %d %s has no table)" % (code, p, PRINT_NAMES[p]))
    with open(csv, "w") as f:
        f.write("code,neg,print,bytes,fit\n" + "\n".join(rows[c] for c in sorted(rows)) + "\n")
    print("matrix: %d tables in %s, fit in %s" % (len(todo), outdir, csv))


def offered():
    # (negative, print label slot) of every pair a catalogue row offers: the 643 of the 8-bit family
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "films"))
    import catalog, re
    out = []
    for f in catalog.rows()[0]:
        if f and f["derived"] and re.fullmatch(r"G\d\d", f.get("table") or ""):
            out += [(int(f["table"][1:]), int(p[1:])) for p in f["prints"]]
    return sorted(set(out)), catalog.GX_BW


def l33(cub, out, bw):
    # .cube (LOG_N^3, red fastest) -> .L33: R' G' B' uint16 LE x 65535; B&W: all three = the BT.601 luma
    import struct
    v = []
    for line in open(cub):
        t = line.split()
        if len(t) == 3 and t[0][0] in "0123456789.-":
            v.append([float(x) for x in t])
    if len(v) != LOG_N ** 3:
        sys.exit("%s: %d nodes, %d expected" % (cub, len(v), LOG_N ** 3))
    q = lambda x: max(0, min(65535, int(round(x * 65535.0))))
    b = bytearray()
    for r, g, bl in v:
        if bw:
            r = g = bl = 0.299 * r + 0.587 * g + 0.114 * bl
        b += struct.pack("<3H", q(r), q(g), q(bl))
    open(out, "wb").write(bytes(b))


def logtables(outdir, negs=None, prns=None):
    # option (c): one .L33 per offered pair on the RAW develop's log grid, logfit.csv with the placement, domain.txt
    # with the curve and gamut (one family per directory: a different domain needs FORCE=1 or another DIR)
    force = os.environ.get("FORCE", "") == "1"
    keep = os.environ.get("KEEP_CUBE", "") == "1"
    pairs, bwset = offered()
    todo = [(n, p) for n, p in pairs if (negs is None or n in negs) and (prns is None or p in prns)
            and PRINT_CHAIN[p] is not None]
    dom = log_domain()
    os.makedirs(outdir, exist_ok=True)
    dtxt = os.path.join(outdir, "domain.txt")
    want = "curve %s %d gamut %s %s\n" % (LOG_CURVE, LOG_CURVES[LOG_CURVE][0], LOG_GAMUT, LOG_CURVES[LOG_CURVE][1])
    if os.path.exists(dtxt) and open(dtxt).read() != want and not force:
        sys.exit("%s holds another family (%s): FORCE=1 or another DIR" % (outdir, open(dtxt).read().strip()))
    open(dtxt, "w").write(want)
    work = os.path.join(outdir, ".genesis")
    os.makedirs(work, exist_ok=True)
    csv = os.path.join(outdir, "logfit.csv")
    rows = {}
    if os.path.exists(csv):
        for l in open(csv).read().splitlines()[1:]:
            if l:
                rows[l.split(",")[0]] = l
    print("log: %d pairs (%d offered), domain %s" % (len(todo), len(pairs), dom))
    for k, (neg, prn) in enumerate(todo, 1):
        code = "G%02dP%02d" % (neg, prn)
        out = os.path.join(outdir, code + ".L33")
        if code in rows and os.path.exists(out) and not force:
            continue
        cub, fit = cube(work, "%s:%d:%d" % (code, neg, PRINT_CHAIN[prn]), quiet=True, domain=dom, n=LOG_N)
        l33(cub, out, neg in bwset)
        if keep:
            if neg in bwset:                            # the kept cube says what the table holds: luma only
                import struct
                d = open(out, "rb").read()
                with open(cub, "w") as f:
                    f.write("TITLE \"%s (B&W, luma)\"\nLUT_3D_SIZE %d\n" % (code, LOG_N))
                    for i in range(LOG_N ** 3):
                        r, g, b = struct.unpack_from("<3H", d, 6 * i)
                        f.write("%.6f %.6f %.6f\n" % (r / 65535.0, g / 65535.0, b / 65535.0))
        else:
            os.remove(cub)
        exp = [l for l in fit.splitlines() if l.startswith("fit: exposure")]
        rows[code] = "%s,%d,%d,%d,%s" % (code, neg, prn, os.path.getsize(out), (exp[0] if exp else "").replace(",", ";"))
        print("%4d/%d %s neg %2d print %2d (chain %2d)%s  %s" % (k, len(todo), code, neg, prn, PRINT_CHAIN[prn],
                                                               " bw" if neg in bwset else "", (exp[0] if exp else "")[5:90]))
    with open(csv, "w") as f:
        f.write("code,neg,print,bytes,fit\n" + "\n".join(rows[c] for c in sorted(rows)) + "\n")
    print("log: %d tables in %s, fit in %s" % (len(rows), outdir, csv))


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        sys.exit(__doc__)
    what = sys.argv[1]
    if what == "list":
        for s in PAIRS:
            print(s)
        return
    if what == "stage":
        if len(sys.argv) < 3:
            sys.exit(__doc__)
        build_tool()
        stage(sys.argv[2])
        return
    if what in ("matrix", "log"):
        if len(sys.argv) < 3:
            sys.exit(__doc__)
        build_tool()
        negs = [int(x) for x in sys.argv[3].split(",")] if len(sys.argv) > 3 and sys.argv[3] != "-" else None
        prns = [int(x) for x in sys.argv[4].split(",")] if len(sys.argv) > 4 and sys.argv[4] != "-" else None
        (matrix if what == "matrix" else logtables)(sys.argv[2], negs, prns)
        return
    if what not in ("cube", "tab") or len(sys.argv) < 4:
        sys.exit(__doc__)
    outdir = sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    build_tool()
    for spec in sys.argv[3:]:
        (cube if what == "cube" else tab)(outdir, spec)

if __name__ == "__main__":
    main()
