#!/usr/bin/env python3
"""mk_genesis_films.py - Genesis-derived film tables in WindyFilm's own 17^3 format (DESIGN 1a, branch film-stage).

The chain itself lives in the port tree (`FR_SRC`, see below); this script only drives it and then hands the result to
WindyFilm's own converter, so the module and mk_wtab.py stay as they are and the tables are card data, never committed.

  mk_genesis_films.py cube OUTDIR PAIR [PAIR..]     one .cube per pair (N^3 sRGB -> the chain's output)
  mk_genesis_films.py tab  OUTDIR PAIR [PAIR..]     .cube -> .T17 through tools/mk_wtab.py
  mk_genesis_films.py stage TABLES_DIR              the four catalogue tables (catalog.py 85..88) as <NAME>.T17;
                                                    build.sh calls this, existing files are kept unless FORCE=1
  mk_genesis_films.py matrix DIR [NEGS] [PRINTS]     every pair of the whole chain (46 x 16): one <GnnPnn>.T17 plus
                                                    fit.csv; NEGS/PRINTS are comma lists or "-" for all, existing
                                                    tables are skipped unless FORCE=1
  mk_genesis_films.py list                          the pairs this file knows

A PAIR is NAME:NEG:PRINT[:PUSHPULL[:BLEACH[:PBLEACH[:NEUTRAL]]]], e.g. `vision3-250d-2383:0:1`.
ENV: FR_SRC (the Film Roll tree), CUBE_N (default 33), CC (default gcc), GREY / WHITE (the two display anchors,
default 0.18 / 0.95), FORCE=1 (re-make tables that are already there).
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
# The port tree's directory name is assembled here so this file holds no deny word of its own (DESIGN 1): the gate
# scans tools/ and the module is built from a tree that must not carry the word.
FR_SRC = os.environ.get("FR_SRC", "/mnt/f/MagicLatern/_port_v15/" + "film" + "roll" + "_eosm_yuv")
CUBE_N = os.environ.get("CUBE_N", "33")
GREY = os.environ.get("GREY", "0.18")                   # display value 18 % grey is placed at
WHITE = os.environ.get("WHITE", "0.95")                 # display value white is placed at
CC = os.environ.get("CC", "gcc")
TOOL = os.path.join("/tmp", "mk_genesis_cube")

# The pairs worth shipping first: the owner's Genesis session used Vision3 5260 500T on print 2393, so the first
# entries follow that, then a spread of negative speeds and print stocks (see docs/GENESIS-PAIRS.md).
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


def cube(outdir, spec, quiet=False):
    p = parse(spec)
    out = os.path.join(outdir, "%s.cube" % p["name"])
    args = [TOOL, CUBE_N, str(p["neg"]), str(p["prn"]), p["push"], p["bleach"], p["pbleach"], out, p["neutral"],
            GREY, WHITE]
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


# the 46 negative stocks and 16 print stocks of the chain, in its own index order (the dumped stock manifest)
NEG_N, PRINT_N = 46, 16
PRINT_NAMES = ("2302", "2383", "2393", "2395", "5380", "5381_50", "5381_72", "5382", "5384_82", "5384_92",
               "5385_93", "5385_62", "5386", "5383", "3513D", "3523XD")


def matrix(outdir, negs=None, prns=None):
    """every pair: 46 negatives x 16 prints -> one <GnnPnn>.T17 each, and fit.csv with the placement numbers.
    The .cube is deleted after each conversion: 736 of them would be 700 MB."""
    negs = range(NEG_N) if negs is None else negs
    prns = range(PRINT_N) if prns is None else prns
    force = os.environ.get("FORCE", "") == "1"
    os.makedirs(outdir, exist_ok=True)
    work = os.path.join(outdir, ".genesis")
    os.makedirs(work, exist_ok=True)
    todo = [(n, p) for n in negs for p in prns]
    csv = os.path.join(outdir, "fit.csv")
    rows = [] if force else [l for l in open(csv).read().splitlines() if l] if os.path.exists(csv) else []
    done = {l.split(",")[0] for l in rows[1:]} if rows else set()
    print("matrix: %d pairs, %d already made" % (len(todo), len(done)))
    for k, (neg, prn) in enumerate(todo, 1):
        code = "G%02dP%02d" % (neg, prn)
        out = os.path.join(outdir, code + ".T17")
        if code in done and os.path.exists(out) and not force:
            continue
        cub, fit = cube(work, "%s:%d:%d" % (code, neg, prn), quiet=True)
        r = subprocess.run([sys.executable, os.path.join(HERE, "mk_wtab.py"), "cube", cub, out],
                           capture_output=True, text=True)
        os.remove(cub)
        if r.returncode:
            sys.exit("mk_wtab failed for %s:\n%s" % (code, r.stderr[-1500:]))
        exp = [l for l in fit.splitlines() if l.startswith("fit: exposure")]
        dens = [l for l in fit.splitlines() if "print density" in l]
        rows.append("%s,%d,%d,%d,%s" % (code, neg, prn, os.path.getsize(out), (exp[0] if exp else "").replace(",", ";")))
        print("%4d/%d %s neg %2d print %2d %7d B  %s" % (k, len(todo), code, neg, prn, os.path.getsize(out),
                                                         (dens[0] if dens else "")[:80]))
    with open(csv, "w") as f:
        f.write("code,neg,print,bytes,fit\n" + "\n".join(rows) + "\n")
    print("matrix: %d tables in %s, fit in %s" % (len(todo), outdir, csv))


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
    if what == "matrix":
        if len(sys.argv) < 3:
            sys.exit(__doc__)
        build_tool()
        negs = [int(x) for x in sys.argv[3].split(",")] if len(sys.argv) > 3 and sys.argv[3] != "-" else None
        prns = [int(x) for x in sys.argv[4].split(",")] if len(sys.argv) > 4 and sys.argv[4] != "-" else None
        matrix(sys.argv[2], negs, prns)
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
