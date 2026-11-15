#!/usr/bin/env python3
"""check_print_slots.py - the chain films' print-stock slots against what their tables really are (DESIGN 1a, branch
film-stage). WindyStrife's own check, run by test.sh (mode SLOTS) on both cameras.

Pins the 15/11/2026 fix (tools/mk_genesis_films.py PRINT_CHAIN): until then G<nn>P<pp> took the chain's print <pp>
directly, so the label p00 = 2302 (the B&W print) sat on a colour table and p14 = 3513D on the B&W one. Three parts;
a part whose data this checkout does not hold is skipped with a note:
  1. labels: mk_genesis_films PRINT_NAMES = the short names in src/wf_menu.c wf_print_name, src/ui/wf_ui.c and
     tests/ui/fake_core.c (same order, "_" written "/"); the full names (wf_print_full, v_print_full) carry the code
     and only 2302's says B&W; PRINT_CHAIN names chain prints 1..15 once each and 3513D none
  2. the chain's own dye tables (FR_SRC/fr_glook_print.bin, the port tree): exactly one of the 16 is B&W and it is
     PRINT_CHAIN[0]; in label order the shared tables are the plugin's five pairs (2383 = 2393, 2395 = 5384/82,
     5380 = 5381/72, 5384/92 = 5385/93, 5385/62 = 5386); the dump's unused print_00 is a copy of print_01
  3. the tables and the catalogue (films/tables/G<nn>P<pp>.T17, films/catalog.py): every offered p00 table is neutral
     (mean |U| + |V| < 5 codes), every other offered slot coloured (> 30) unless the negative is B&W, every offered
     table is there, no P14 file is left, no row offers 3513D or a pair whose print density span in fit.csv is under
     0.05 (its white sits on the base fog), every colour row offers 2383 (src/wf_api.h WF_PRINT_CHAIN_DEF)
  4. the B&W negative (Double-X 5222, chain negative 9, films/catalog.py GX_BW, 15/11/2026): the chain has no
     monochrome negative (FR_SRC/fr_glook_neg.h: negative 9 has three distinct spectral rows, its own interlayer and
     per-channel curves, like the colour stocks), so its row is a B&W NEG printed on 2302 only, every table of it is
     exactly grey (U = V = 0, mk_wtab --bw) and the module's WF_F_BW makes the film-stage composition Y only
Output: "PASS ..." / "FAIL ..." lines, then "RESULT: <n> pass <m> fail"; exit 1 on a failure.
Run in WSL: python3 -I -B tools/check_print_slots.py
"""
import hashlib, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "films"))
sys.path.insert(0, HERE)
import catalog
import mk_genesis_films as G

TABLES = os.path.join(REPO, "films", "tables")
PAIRS = {(1, 2), (3, 8), (4, 6), (9, 10), (11, 12)}     # the plugin's own shared tables, label slots
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


def c_strings(path, start):
    """the string literals of the C initializer that begins at the first line matching `start`"""
    src = open(path, encoding="utf-8").read()
    i = src.index(start)
    body = src[src.index("{", i) + 1:src.index("}", i)]
    return re.findall(r'"([^"]*)"', body)


# ---- 1. labels
short = [n.replace("_", "/") for n in G.PRINT_NAMES]
for path, start, lead in (("src/wf_menu.c", "static const char *wf_print_name[", 3),
                          ("src/ui/wf_ui.c", "static const char *const pn[WF_NPRINTS]", 3),
                          ("tests/ui/fake_core.c", "static const char *const v_print[]", 3)):
    s = c_strings(os.path.join(REPO, path), start)
    check(s[:lead] == ["Scan", "Cine", "Paper"] and s[lead:] == short,
          "%s: print stock names in PRINT_NAMES order (%s)" % (path, " ".join(s[lead:lead + 3]) + " ..."))
for path, start in (("src/wf_menu.c", "static const char *const wf_print_full[16]"),
                    ("tests/ui/fake_core.c", "static const char *const v_print_full[16]")):
    full = c_strings(os.path.join(REPO, path), start)
    check(len(full) == 16 and all(n.split("_")[0] in f for n, f in zip(G.PRINT_NAMES, full)),
          "%s: each full name carries its code" % path)
    check([i for i, f in enumerate(full) if "B&W" in f] == [0], "%s: only p00 (2302) is named B&W" % path)
chain = [c for c in G.PRINT_CHAIN if c is not None]
check(sorted(chain) == list(range(1, 16)) and G.PRINT_CHAIN[14] is None,
      "PRINT_CHAIN: chain prints 1..15 once each, 3513D (p14) none")
check(catalog.PRINTS[3:] == tuple("p%02d" % i for i in range(16)), "catalog PRINTS: p00..p15 after scan / cine / paper")

# ---- 2. the chain's own dye tables
DYE = os.path.join(G.FR_SRC, "fr_glook_print.bin")
M = ((3.2409699419, -1.53738317757, -0.498610760293), (-0.969243636281, 1.87596750151, 0.041555057407),
     (0.055630079697, -0.203976958889, 1.05697151424))
if not os.path.exists(DYE):
    print("note: %s not here (no port tree): the chain's dye tables are not checked" % DYE)
else:
    b = open(DYE, "rb").read()
    blk = 33 ** 3 * 3 * 2
    check(len(b) == 16 * blk, "chain dye tables: 16 x 33^3 x 3 uint16 (%d B)" % len(b))
    chroma, md5 = [], []
    for s in range(16):
        raw = b[s * blk:(s + 1) * blk]
        md5.append(hashlib.md5(raw).hexdigest())
        w = struct.unpack("<%dH" % (len(raw) // 2), raw)
        t = n = 0
        for i in range(0, len(w), 21):                   # every 7th node: X Y Z (Q14) -> Rec.709, max - min
            X, Y, Z = w[i] / 16384.0, w[i + 1] / 16384.0, w[i + 2] / 16384.0
            rgb = [m[0] * X + m[1] * Y + m[2] * Z for m in M]
            t += max(rgb) - min(rgb)
            n += 1
        chroma.append(t / n)
    bw = [s for s in range(16) if chroma[s] < 0.05]
    check(bw == [G.PRINT_CHAIN[0]], "chain dye tables: one B&W table (chain %s, chroma %s), PRINT_CHAIN[0] = %d"
          % (bw, " ".join("%.3f" % chroma[s] for s in bw), G.PRINT_CHAIN[0]))
    rest = [c for c in G.PRINT_CHAIN[1:] if c is not None]
    check(all(chroma[c] > 0.3 for c in rest), "chain dye tables: every other mapped slot is colour (chroma %.3f..%.3f)"
          % (min(chroma[c] for c in rest), max(chroma[c] for c in rest)))
    same = {(a, z) for a in range(16) for z in range(a + 1, 16)
            if G.PRINT_CHAIN[a] is not None and G.PRINT_CHAIN[z] is not None
            and md5[G.PRINT_CHAIN[a]] == md5[G.PRINT_CHAIN[z]]}
    check(same == PAIRS, "label order: shared tables are the plugin's five pairs (%s)" % sorted(same))
    check(md5[0] == md5[1] and 0 not in G.PRINT_CHAIN,
          "the dump's print_00 is a copy of print_01 and no slot uses it")

# ---- 3. the tables and the catalogue
rows = [f for f in catalog.rows()[0] if f and f["derived"] and re.fullmatch(r"G\d\d", f.get("table") or "")]
check(len(rows) == 44, "catalogue: %d chain rows" % len(rows))
check(all("p14" not in f["prints"] for f in rows), "catalogue: no row offers 3513D (p14, no table)")
check(all("p01" in f["prints"] for f in rows if f["type"] != "bw"),
      "catalogue: every colour row offers 2383 (p01), the default print")
bwr = [f for f in rows if f["type"] == "bw"]
check([int(f["table"][1:]) for f in bwr] == sorted(catalog.GX_BW) == [9] and
      all(f["kind"] == "B&W NEG" and "Double-X" in f["name"] and f["prints"] == ("p00",) for f in bwr),
      "catalogue: one B&W chain row, G09 Double-X 5222: B&W NEG, printed on 2302 (p00) only")
have = sorted(n for n in os.listdir(TABLES) if re.fullmatch(r"G\d\dP\d\d\.T17", n)) if os.path.isdir(TABLES) else []
if not have:
    print("note: no G<nn>P<pp>.T17 in %s (no chain tables made here): tables not checked" % TABLES)
else:
    check(not [n for n in have if n.endswith("P14.T17")], "films/tables: no P14 (3513D) table left")
    fit = {}
    if os.path.exists(os.path.join(TABLES, "fit.csv")):
        for l in open(os.path.join(TABLES, "fit.csv")).read().splitlines()[1:]:
            m = re.search(r"print density ([\d.]+)\.\.([\d.]+)", l)
            if m:
                fit[l.split(",")[0]] = float(m.group(2)) - float(m.group(1))

    def uv(path):
        w = struct.unpack("<4913I", open(path, "rb").read()[:4913 * 4])
        t = 0
        for x in w:
            u, v = x & 0xff, (x >> 16) & 0xff
            t += abs(u - 256 if u > 127 else u) + abs(v - 256 if v > 127 else v)
        return t / 4913.0

    miss, flat, grey, colour = [], [], [], []
    bw_neg = 0
    for f in rows:
        n = int(f["table"][1:])
        mono = f["kind"] == "B&W NEG"
        bw_neg += mono
        for p in f["prints"]:
            code = "%sP%s" % (f["table"], p[1:])
            path = os.path.join(TABLES, code + ".T17")
            if not os.path.exists(path):
                miss.append(code)
                continue
            if code in fit and fit[code] < 0.05:
                flat.append("%s (%.3f)" % (code, fit[code]))
            c = uv(path)
            if p == "p00":
                grey.append((c, code))
            elif not mono:
                colour.append((c, code))
    check(not miss, "every offered chain table is there (%d missing%s)" % (len(miss), ": " + " ".join(miss[:6]) if miss else ""))
    check(not flat, "no offered pair has a flat print in fit.csv%s" % (": " + " ".join(flat) if flat else ""))
    check(grey and max(grey)[0] < 5, "p00 (2302) is neutral on all %d negatives offering it (max |U|+|V| %.2f, %s)"
          % (len(grey), max(grey)[0] if grey else 0, max(grey)[1] if grey else "-"))
    check(colour and min(colour)[0] > 30, "every other offered slot is colour: %d tables, min |U|+|V| %.1f (%s)"
          % (len(colour), min(colour)[0] if colour else 0, min(colour)[1] if colour else "-"))
    bwt = sorted(n for n in have if int(n[1:3]) in catalog.GX_BW)
    check(bwt and all(uv(os.path.join(TABLES, n)) == 0 for n in bwt),
          "the B&W negative's tables are exactly grey (U = V = 0, mk_wtab --bw): %d tables" % len(bwt))

# ---- 4. the chain's negatives: none is monochrome by itself
NEGH = os.path.join(G.FR_SRC, "fr_glook_neg.h")
if not os.path.exists(NEGH):
    print("note: %s not here (no port tree): the chain's negatives are not checked" % NEGH)
else:
    src = open(NEGH).read()
    body = src[src.index("gl_neg_stm"):]
    body = body[body.index("{") + 1:body.index("};")]
    stm = [[float(x.strip().rstrip("f")) for x in r.split(",") if x.strip()] for r in re.findall(r"\{([^{}]*)\}", body)]
    spread = [max(abs(r[i] - r[4 + i]) + abs(r[i] - r[8 + i]) for i in range(4)) for r in stm]
    check(len(stm) == 46 and min(spread) > 0.5,
          "chain negatives: all 46 have three distinct spectral rows (min row spread %.2f), so none renders grey by "
          "itself; negative 9 (Double-X) %.2f" % (min(spread), spread[9]))

print("RESULT: %d pass %d fail" % (NP, len(fails)))
sys.exit(1 if fails else 0)
