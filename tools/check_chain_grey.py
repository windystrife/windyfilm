#!/usr/bin/env python3
"""check_chain_grey.py - the chain films' mid-grey against the catalogue films' (DESIGN 1a, branch film-stage).
WindyStrife's own check, run by test.sh (mode GREY) on both cameras.

  check_chain_grey.py [TABLES_DIR]       default films/tables

A grey patch at 18 % - sRGB code 118, the code the catalogue films are built around (DESIGN A1) - is read through
every table on the neutral axis (U = V = 0, linear between the nodes Y 112 and 128). The catalogue films (every .T17
that is not a chain table) give the reference: the median of their outputs. Every chain table a catalogue row offers
(G<nn>P<pp>, at its default knobs) and every stage film's table (GX*) must:
  - land within half a stop of the catalogue median,
  - not fall under the darkest catalogue film by more than a quarter stop,
and the chain tables' median must sit within a quarter stop of the catalogue median. Stops are measured on the
sRGB-decoded output. Until 15/11/2026 the chain was placed with grey at display code 0.18 (Y ~46), a linear value used
as a code: two stops under the catalogue (owner's sweep IMG_6844 / IMG_6856). The film-stage composition at default
knobs is the shipped table: `mk_wfstage dat` (run by the WFSTAGE pack build) holds it to the shipped grey nodes within 3
codes, so this check covers that path too.
Output: "PASS ..." / "FAIL ..." lines, then "RESULT: <n> pass <m> fail"; exit 1 on a failure.
Run in WSL: python3 -I -B tools/check_chain_grey.py
"""
import math, os, re, statistics, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "films"))
import catalog

TABLES = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "films", "tables")
GREY_IN = 118                      # sRGB code of 18 % grey
CHAIN = re.compile(r"(G\d\dP\d\d|GX[0-9A-Z]+)\.T17")
NP = 0
fails = []


def check(ok, what):
    global NP
    if ok:
        NP += 1
        print("PASS " + what)
    else:
        fails.append(what)
        print("FAIL " + what)


def grey_out(path):
    """output Y of a neutral patch at GREY_IN: nodes stand for Y = 16 * cell, U = V = 128 is cell 8"""
    w = struct.unpack("<4913I", open(path, "rb").read()[:4913 * 4])
    c, t = divmod(GREY_IN, 16)
    a, b = ((w[k * 289 + 8 * 17 + 8] >> 8) & 255 for k in (c, c + 1))
    return a + (b - a) * t / 16.0


def lin(code):
    v = max(code, 0.5) / 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def stops(a, b):
    """a over b in stops, on the sRGB-decoded codes"""
    return math.log2(lin(a) / lin(b))


names = sorted(os.listdir(TABLES)) if os.path.isdir(TABLES) else []
cat = {n: grey_out(os.path.join(TABLES, n)) for n in names if n.endswith(".T17") and not CHAIN.fullmatch(n)}
rows = [f for f in catalog.rows()[0] if f and f["derived"] and re.fullmatch(r"G\d\d", f.get("table") or "")]
offered = ["%sP%s.T17" % (f["table"], p[1:]) for f in rows for p in f["prints"]]
chain = {n: grey_out(os.path.join(TABLES, n)) for n in sorted(set(offered)) + [n for n in names if n.startswith("GX")]
         if os.path.exists(os.path.join(TABLES, n))}

if len(cat) < 10 or not chain:
    print("note: %s holds %d catalogue and %d chain tables (tables not made here): grey not checked"
          % (TABLES, len(cat), len(chain)))
else:
    ref = statistics.median(cat.values())
    lo = min(cat, key=cat.get)
    print("note: grey patch Y %d -> catalogue (%d tables) median %.1f, %.1f (%s) .. %.1f (%s)"
          % (GREY_IN, len(cat), ref, cat[lo], lo[:-4], max(cat.values()), max(cat, key=cat.get)[:-4]))
    cm = statistics.median(chain.values())
    dk, br = min(chain, key=chain.get), max(chain, key=chain.get)
    print("note: chain (%d tables) median %.1f, %.1f (%s) .. %.1f (%s)"
          % (len(chain), cm, chain[dk], dk[:-4], chain[br], br[:-4]))
    check(len(chain) >= len(set(offered)), "every offered chain table is there (%d of %d)" % (len(chain) - len(
          [n for n in chain if n.startswith("GX")]), len(set(offered))))
    check(abs(stops(cm, ref)) <= 0.25, "chain median grey %.1f is within 1/4 stop of the catalogue's %.1f (%+.2f stops)"
          % (cm, ref, stops(cm, ref)))
    far = sorted((abs(stops(v, ref)), n[:-4], v) for n, v in chain.items() if abs(stops(v, ref)) > 0.5)
    check(not far, "every chain table's grey is within 1/2 stop of the catalogue's (%+.2f..%+.2f stops)%s"
          % (stops(chain[dk], ref), stops(chain[br], ref),
             "; out: " + " ".join("%s %.0f" % (n, v) for _, n, v in far[:8]) if far else ""))
    under = [n[:-4] for n, v in chain.items() if stops(v, cat[lo]) < -0.25]
    check(not under, "no chain table is over 1/4 stop under the darkest catalogue film (%s %.1f)%s"
          % (lo[:-4], cat[lo], "; under: " + " ".join(under[:8]) if under else ""))

print("RESULT: %d pass %d fail" % (NP, len(fails)))
sys.exit(1 if fails else 0)
