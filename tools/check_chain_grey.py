#!/usr/bin/env python3
"""check_chain_grey.py - the chain films' mid-grey against the catalogue films' (DESIGN 1a, branch film-stage).
WindyStrife's own check, run by test.sh (mode GREY) on both cameras.

  check_chain_grey.py [TABLES_DIR [LOG_DIR]]     default films/tables, films/tables/log

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
The log family (option (c), the RAW develop: <GnnPpp>.L33 in LOG_DIR, tools/mk_genesis_films.py log) is read the same
way on its own grid: scene-linear 0.18 is the log code of its curve (LOG_DIR/domain.txt: S-Log3 0.4106, LogC3 EI800
0.3910, V-Log 0.4233; a neutral is the same in every gamut), linear between the two neutral nodes around it, and its
output luma (BT.601 of R'G'B') must land within 1/8 stop of the 8-bit table of the same pair, every offered pair must have one,
and the log family's median must sit within 1/16 stop of the 8-bit chain median. No log tables: not checked (they are
made on demand).
Output: "PASS ..." / "FAIL ..." lines, then "RESULT: <n> pass <m> fail"; exit 1 on a failure.
Run in WSL: python3 -I -B tools/check_chain_grey.py
"""
import math, os, re, statistics, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "films"))
import catalog

TABLES = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "films", "tables")
LOGS = sys.argv[2] if len(sys.argv) > 2 else os.path.join(TABLES, "log")
LOG_N = 33
LOG_GREYS = {"slog3": 420.0 / 1023.0,                                                  # 0.4106
             "logc3": 0.247190 * math.log10(5.555556 * 0.18 + 0.052272) + 0.385537,     # 0.3910
             "vlog": 0.241514 * math.log10(0.18 + 0.00873) + 0.598206}                  # 0.4233
_dt = os.path.join(LOGS, "domain.txt")
LOG_CURVE = open(_dt).read().split()[1] if os.path.exists(_dt) else "slog3"
LOG_GREY = LOG_GREYS.get(LOG_CURVE)                 # log code of scene-linear 0.18 on the family's curve
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


def log_grey_out(path):
    # output luma code of scene-linear 0.18 on the .L33 neutral axis (linear between the two nodes, as the develop's
    # tetrahedral interpolation is on that axis)
    d = open(path, "rb").read()
    if len(d) != 6 * LOG_N ** 3:
        return None
    c, t = divmod(LOG_GREY * (LOG_N - 1), 1.0)
    c = int(c)
    def y(k):
        r, g, b = struct.unpack_from("<3H", d, 6 * (k + LOG_N * k + LOG_N * LOG_N * k))
        return (0.299 * r + 0.587 * g + 0.114 * b) * 255.0 / 65535.0
    return y(c) + (y(c + 1) - y(c)) * t


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

logs = sorted(n for n in os.listdir(LOGS) if n.endswith(".L33")) if os.path.isdir(LOGS) else []
if not logs:
    print("note: %s holds no log tables (option (c), made on demand): the log family is not checked" % LOGS)
elif not chain:
    print("note: log tables but no 8-bit chain tables in %s: the log family is not checked" % TABLES)
elif LOG_GREY is None:
    check(False, "log family curve %r (%s) is one of %s" % (LOG_CURVE, _dt, sorted(LOG_GREYS)))
else:
    lg = {n[:-4]: log_grey_out(os.path.join(LOGS, n)) for n in logs}
    bad = sorted(n for n, v in lg.items() if v is None)
    check(not bad, "every log table is %d bytes%s" % (6 * LOG_N ** 3, "; bad: " + " ".join(bad[:8]) if bad else ""))
    lg = {n: v for n, v in lg.items() if v is not None}
    want = sorted(n[:-4] for n in set(offered))
    miss = [n for n in want if n not in lg]
    check(not miss, "every offered chain pair has a log table (%d of %d)%s" % (len(want) - len(miss), len(want),
          "; missing: " + " ".join(miss[:8]) if miss else ""))
    pairs = [(n, v, chain[n + ".T17"]) for n, v in lg.items() if n + ".T17" in chain]
    dd = sorted(stops(v, e) for _, v, e in pairs)
    far = sorted((abs(stops(v, e)), n, v, e) for n, v, e in pairs if abs(stops(v, e)) > 0.125)
    lm, em = statistics.median(v for _, v, _ in pairs), statistics.median(e for _, _, e in pairs)
    print("note: log family (%s, %d tables) grey median %.1f against the 8-bit %.1f; log - 8-bit per pair %+.3f..%+.3f "
          "stops" % (LOG_CURVE, len(pairs), lm, em, dd[0], dd[-1]))
    check(not far, "every log table's grey is within 1/8 stop of its 8-bit table%s"
          % ("; out: " + " ".join("%s %.1f/%.1f" % (n, v, e) for _, n, v, e in far[:8]) if far else ""))
    check(abs(stops(lm, em)) <= 0.0625, "log median grey %.1f is within 1/16 stop of the 8-bit chain's %.1f (%+.3f "
          "stops)" % (lm, em, stops(lm, em)))

print("RESULT: %d pass %d fail" % (NP, len(fails)))
sys.exit(1 if fails else 0)
