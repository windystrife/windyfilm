#!/usr/bin/env python3
"""catalog.py - the WindyFilm catalogue: the one source of truth (docs/DESIGN.md 4.2).

Every film has a row here: id (26..127, append-only, never reused once deployed), codes, display name, descriptor fields,
family and strip order, effect parameters, the table files and their provenance. Everything else is generated from it:
  catalog.py header OUT.h [--synth N] [--gaps]   fr_catalog.h (the old port's C header; kept working, not used by WindyFilm)
  catalog.py pack                                 the films that go into WFFILMS.DAT: "id table type flags variant t17" lines
  catalog.py docs OUT.md                          docs/FILMS.md
  catalog.py names                                every shipped catalogue string, one per line
  catalog.py check                                naming rules, text widths (Ubuntu Sans via PIL), field and table checks
Per row, besides the old fields (id code name type iso isocode kelvin ev8 family order grain halo spk v21 lut mode prov
ready): `short` (strip label, <= 11 chars and <= 66 px at Ubuntu Sans 13 px wght 500 wdth 82), `line1` / `line2` (our
short description, <= 44 chars and <= 440 px at 15 px wght 500), `kind` (chip text), `fmt` (chip text), `group`
(colour / cine / bw / lut) and `prints` (the table variants made: scan, cine, paper). tools/mk_catalog_c.py (core) reads
them; tables live in films/tables/<table>.T17 (scan), <table>_C.T17 (cine print), <table>_P.T17 (paper print).

A row with ready=False (its look is not finished or not integrated in this build) has no table and is not shown.

Film LUT cans (V10, owner 2026-11-12: "choose LUTs like film rolls"): rows with `lut` = 1..FLUT_N - 1, the menu "Film
LUT" index (fr_plus.c fr_flut_name / FRPLUS.CFG "lut"). They are shelf entries with a descriptor and a can, but no film
table: no T17, no pack entry (WSFILMS.DAT holds the films only), no grain / halation sets of their own (the + LUT modes'
LUT_GRAIN / LUT_HALO / LUT_SPK, set 0). The shelf shows one only while its V-Log cube (_VL) is on the card (fr_shelf.c);
its Load sets Film LUT to that cube and the output to V-Log + LUT (owner 2026-11-13) unless a + LUT output is set
(fr_eosm.c fr_load_poll, fr_modes.h FR_LLUT_CAN).

Two codes per film: `code` is the descriptor's short name (shelf, menu, LiveView info, the module log), `table` the
T17 file name and the pack entry's code (a pack entry is accepted only when its id AND table code match). For the
shipped films 26..31 both stay as deployed (owner 11-10 "keep the old names"); new films use one code for both.

Effects (D4): grain and halation come from the row, not from an output mode. Rows are folded into distinct grain
sets (yg_stocks row, amount scale Q8, rho_max Q8) and halation sets (yh profile, strength Q8); the effect keys are
the set index, so two films on one row with different scales rebuild the effect and two identical films do not.
`rho_max` 0 = the yg_stocks row value (batch 0 commit a: the shipped values, EOSMFX unchanged).
"""
import os, sys, struct, math

HERE = os.path.dirname(os.path.abspath(__file__))
ID0, ID_LIMIT = 26, 127
MODE_MAX = 16                     # fr_modes.h FR_MODE_MAX: 13..16 Canon Log (owner 2026-11-13); films stay at 1..4, 11, 12
# text limits (WindyFilm browser, docs/DESIGN.md 5): name <= 24 chars (wf_film.name), strip label `short` <= 11 chars and
# <= 66 px at Ubuntu Sans 13 px wght 500 wdth 82, description lines <= 44 chars and <= 440 px at 15 px wght 500 wdth 100.
# Measured with PIL ImageFont.getlength on the variable font below (WSL: fonts-ubuntu); check() fails without it.
NAME_CH, SHORT_CH, SHORT_PX, LINE_CH, LINE_PX = 24, 11, 66, 44, 440
FONT_UBUNTU = os.environ.get("WF_FONT", "/usr/share/fonts/truetype/ubuntu/UbuntuSans[wdth,wght].ttf")
KINDS = ("COLOUR NEG", "SLIDE", "B&W NEG", "CINE NEG", "FILM LUT")
FMTS = ("35mm", "120", "V-Log")
GROUPS = ("colour", "cine", "bw", "lut")
PRINTS = ("scan", "cine", "paper")
PSUFFIX = {"scan": "", "cine": "_C", "paper": "_P"}
MAKER = "WindyStrife"
PSTYLE_NEUTRAL = 0x84
FLUT_N = 24                       # src/wf_loglut.h WF_FLUT_N: Technical + 4 film cubes + the owner's 6 (V11) + EKTA P (V13) + 11 Windy (V14T, named Windy 11-14) + Leica Vivid (11-14); a LUT can's `lut` is 1..FLUT_N - 1
FLUT_NAME = ("Technical", "Vision3 250D", "Vision3 500T", "Portra 400", "Pro 400H",          # src/wf_loglut.c wf_flut_name
             "Fuji 3513", "Kodak 2383 v3", "Kodak 2383 v5", "Kodak Ektar", "Windy Neutral", "Luca Bono", "Windy EKTA P",
             "Windy 250D P", "Windy 250D M", "Windy 500T P", "Windy 500T M", "Windy EKTA M", "Windy EKTAa P", "Windy EKTAa M",
             "Windy EXR P", "Windy EXR M", "Windy EXRa P", "Windy EXRa M", "Leica Vivid")
FLUT_VL = (None, "V250D_VL.CUB", "V500T_VL.CUB", "PRT400VL.CUB", "FP400HVL.CUB",             # fr_loglut.c fr_loglut_name(b, 2, l, 0)
           "FJ3513VL.CUB", "KD83V3VL.CUB", "KD83V5VL.CUB", "EKTAR5VL.CUB", "TUNGNTVL.CUB", "LBONO_VL.CUB", "EKTAP_VL.CUB",
           "T250DPVL.CUB", "T250DMVL.CUB", "T500TPVL.CUB", "T500TMVL.CUB", "TEKTAMVL.CUB", "TEKAAPVL.CUB", "TEKAAMVL.CUB",
           "TEXRP_VL.CUB", "TEXRM_VL.CUB", "TEXRAPVL.CUB", "TEXRAMVL.CUB", "LEICAVVL.CUB")
TYPES = {"neg": 0, "slide": 1, "bw": 2}
# yg_stocks.c rows and yh_halo.h profiles that exist in this build (a row of a film that is not ready may name a
# row that a later batch adds; check() asserts the rows of ready films exist)
GRAIN_ROWS = {"Neg 400": 0, "Neg 800T": 1, "Slide 50": 2, "B&W 400": 3, "Neg 160": 4, "Neg 250": 5}
HALO_PROFILES = {"neg": 0, "C800T": 1, "slide": 2, "BW": 3,           # YH_NEG, YH_C800T, YH_SLIDE, YH_BW
                 "spk_s": 4, "spk_w": 5, "spk_c": 6, "spk_n": 7, "spk_bw": 8, "spk_bww": 9}   # YH_SPK_S .. YH_SPK_BWW
# spektrafilm halation (Halation menu "Spektra" / "Spektra + bloom"; yh_halo.h YH_SPK_*, fit_levels.py): the profile of
# the film's anti-halation class as spektrafilm's params_builder assigns it (Portra / Ektar / slide strong, Gold /
# UltraMax / C200 / Xtra weak, Vision3 cine strong, rem-jet removed none); B&W GUESS (spektrafilm has none). Default by
# family; a row's `spk` (profile, strength Q8) overrides it. Strength stays 256 unless the row's README pushes the
# halation as a look (Chungking 320, Switch Azure: their halation scale is kept).
SPK_FAMILY = {"portrait neg": "spk_s", "consumer neg": "spk_w", "saturated / creative": "spk_w", "cine": "spk_c",
              "slide": "spk_s", "B&W": "spk_bw"}
# drawn canister colours per family (ML palette indices: body, band, text on the body), used only when WSCANS.DAT
# has no can for the film: the WindyStrife charcoal body (COLOR_GRAY(20)) and white text on every film, the band
# the nearest ML colour to the family band of cans/WS/gen_cans.py (no 7.3 trade-dress look: no yellow, green,
# orange-red or red + white)
FAMILY = {
    "portrait neg":         (0x2e, 0x0e, 0x01),   # magenta band (cans: plum)
    "consumer neg":         (0x2e, 0x09, 0x01),   # light blue band (cans: azure)
    "saturated / creative": (0x2e, 0x0b, 0x01),   # blue band (cans: violet)
    "cine":                 (0x2e, 0x17, 0x01),   # dark orange band (cans: taupe)
    "slide":                (0x2e, 0x18, 0x01),   # dark cyan band (cans: navy)
    "B&W":                  (0x2e, 0x3e, 0x01),   # COLOR_GRAY(60) band (cans: grey + violet stripes)
    "film LUT":             (0x2e, 0x05, 0x01),   # cyan band (the Film LUT cans, V10; cans: teal)
}
# the + LUT output modes 8..10 take these effect parameters (fr_fx.c, unchanged since the effects merge)
LUT_GRAIN, LUT_HALO, LUT_SPK = ("Neg 400", 256), ("neg", 256), ("spk_s", 256)

# strip labels (the text under a can on the browser strip), by id
SHORT = {26: 'Chrome', 27: 'Classic Neg', 28: 'Pro Neg Std', 29: 'Eterna', 30: 'Chungking', 31: 'Cine 250D', 32: 'Portra 160', 33: 'Portra 400', 34: 'Gold 200', 35: 'Cine 800T', 37: 'Tri-X 400', 38: 'Superia 400', 39: 'Ektar 100', 40: 'UltraMax', 41: 'HP5 Plus', 43: 'Portra 800', 44: 'Pro 400H', 45: 'T-Max 100', 46: 'P3200', 47: 'XP2 Super', 48: 'ColorPlus', 50: 'Cine 50D', 53: 'Delta 100', 54: 'Pan F 50', 55: 'Acros 100', 56: 'Foma 100', 57: 'Lomo 800', 59: 'Azure', 60: 'Pan 200', 61: 'FP4 Plus', 62: 'V3 250D', 63: 'V3 500T', 64: 'Portra LUT', 65: '400H LUT', 66: 'EKTA P', 67: '3513 LUT', 68: '2383 v3', 69: '2383 v5', 70: 'Ektar LUT', 71: 'Neutral', 72: 'Luca Bono', 73: '250D P', 74: '250D M', 75: '500T P', 76: '500T M', 77: 'EKTA M', 78: 'EKTAa P', 79: 'EKTAa M', 80: 'EXR P', 81: 'EXR M', 82: 'EXRa P', 83: 'EXRa M', 84: 'Leica Vivid'}
# table variants made per table code (luts/film/src/mk_wfprints.py: Cine = negative -> 2383 print, Paper = negative -> the
# colour paper of its spektrafilm profile). Films without an entry have the scan look only (no spektrafilm negative:
# slide, B&W, Fuji / Kodak looks without a profile, effect looks). Film LUT cans have none.
_STILL = ("scan", "cine", "paper")
_CINE = ("scan", "cine")
VARIANTS = {"PRT160": _STILL, "PRT400": _STILL, "PRT800": _STILL, "SUN200": _STILL, "EVD400": _STILL, "BLD100": _STILL,
            "PAS400": _STILL, "TRV400": _STILL, "CIN50D": _CINE, "CINE250D": _CINE, "TUN800": _CINE}

# where each film's scan look comes from: the generator (luts/looks/<path>) and the cube it writes (tools/mk_wtab.py reads
# the cube, luts/make_cubes.sh makes them). Film LUT cans have no table.
GEN = {"CCHROME": "fuji/gen_fuji_luts.py", "CLASNEG": "fuji/gen_fuji_luts.py", "PRONEG": "fuji/gen_fuji_luts.py",
       "ETERNA": "fuji/gen_fuji_luts.py", "CHUNGK": "fuji/gen_chungking.py", "CINE250D": "fuji/gen_cine250d.py",
       "PRT160": "PRT160/gen_port160.py"}
CUBE = {"PRT160": "PORT160.CUB"}
def gen_of(table):
    return GEN.get(table, "%s/gen_%s.py" % (table, table.lower()))
def cube_of(table):
    return CUBE.get(table, table + ".CUB")

def derive(f):
    """the browser fields that follow from the row: group, kind, fmt, short, prints"""
    f["group"] = "lut" if f["lut"] else "bw" if f["type"] == "bw" else "cine" if f["family"] == "cine" else "colour"
    f["kind"] = ("FILM LUT" if f["lut"] else "SLIDE" if f["type"] == "slide" else "B&W NEG" if f["type"] == "bw"
                 else "CINE NEG" if f["group"] == "cine" else "COLOUR NEG")
    f["fmt"] = "V-Log" if f["lut"] else "35mm"
    f["short"] = SHORT.get(f["id"], "")
    f["prints"] = () if f["lut"] or not f["ready"] else VARIANTS.get(f["table"], ("scan",))
    if f["ready"] and not f["lut"]:
        f["cube"] = cube_of(f["table"])
        f["prov"] = ("WindyStrife parametric look luts/looks/%s (published data sheets / own judgement, named in the generator) "
                     "-> %s -> tools/mk_wtab.py -> films/tables/%s.T17" % (gen_of(f["table"]), f["cube"], f["table"]))
    return f

def F(id, code, name, type, iso, isocode, kelvin, ev8, family, order, grain, halo, line1="", line2="", table=None,
      ready=False, mode=None, v21=None, t17=None, prov="", fx="", spk=None, lut=0):
    return derive(dict(id=id, code=code, name=name, type=type, iso=iso, isocode=isocode, kelvin=kelvin, ev8=ev8,
                       family=family, order=order, grain=grain, halo=halo, line1=line1, line2=line2, table=table or code,
                       ready=ready, mode=mode, v21=(id >= 32) if v21 is None else v21, t17=t17, prov=prov, fx=fx, spk=spk,
                       lut=lut))

def LUTCAN(id, code, name, iso, isocode, kelvin, order, lut, line1, line2, prov):
    """a Film LUT can (see the top): colour negative, the + LUT modes' effect parameters"""
    return F(id, code, name, "neg", iso, isocode, kelvin, 0, "film LUT", order, LUT_GRAIN, LUT_HALO,
             line1, line2, ready=True, spk=LUT_SPK, lut=lut, prov=prov)

def spk_of(f):
    """the film's spektrafilm halation (profile, strength Q8): its row's `spk`, else its family's class at x1"""
    return f.get("spk") or (SPK_FAMILY.get(f["family"], "spk_s"), 256)

# grain: (row name, scale Q8); halo: (profile, strength Q8). 256 = x1.0. PLAN 5.1 / 5.2 / 3.1; ISO codes 72 = 100,
# 77 = 160, 80 = 200, 83 = 250, 85 = 320, 88 = 400, 96 = 800, 112 = 3200.
FILMS = [
    # ---- shipped films (ids 26..31, output modes 1..4, 11, 12): names and codes as deployed (owner 11-10)
    F(26, "CHROME", "Classic Chrome", "slide", 200, 80, 5200, 0, "slide", 54, ("Slide 50", 256), ("slide", 256),
      "Muted slide colour with firm shadows", "and slightly cooler skies.", table="CCHROME", ready=True, mode=1,
      prov="luts_fuji/gen_fuji_luts.py (public datasheet curves; README.txt)"),
    F(27, "CLSNEG", "Classic Neg", "neg", 400, 88, 5200, 0, "consumer neg", 41, ("Neg 400", 256), ("neg", 256),
      "Hard negative look: cyan-green shadows", "and warm, creamy highlights.", table="CLASNEG", ready=True, mode=2,
      prov="luts_fuji/gen_fuji_luts.py"),
    F(28, "PRONEG", "Pro Neg Std", "neg", 160, 77, 5200, 0, "portrait neg", 35, ("Neg 160", 256), ("neg", 256),
      "Soft, even contrast and gentle colour", "for natural-looking skin.", ready=True, mode=3, prov="luts_fuji/gen_fuji_luts.py"),
    F(29, "ETERNA", "Eterna", "neg", 250, 83, 5200, 0, "cine", 48, ("Neg 250", 256), ("neg", 256),
      "Cinema look: low contrast, low saturation", "and lifted blacks.", ready=True, mode=4, prov="luts_fuji/gen_fuji_luts.py"),
    # Chungking 320: Neg 800T grain x1.25 and negative halation x1.25 (GUESS: the designer's "pushed, coarse" call)
    F(30, "CHUNGK", "Chungking 320", "neg", 320, 85, 5200, 0, "saturated / creative", 44, ("Neg 800T", 320),
      ("neg", 320), "Old-DVD night look: blue-black shadows,", "lime mid-tones, deep crimson reds.", ready=True, mode=11,
      prov="luts_fuji/gen_chungking.py", spk=("spk_w", 320)),
    # Cine 250D: daylight motion-picture negative, 5500 K (its branch); Neg 250 grain, negative halation, both x1
    F(31, "C250D", "Cine 250D", "neg", 250, 83, 5500, 0, "cine", 47, ("Neg 250", 256), ("neg", 256),
      "Daylight cine negative: soft mid-tones", "and a gentle highlight shoulder.", table="CINE250D", ready=True, mode=12,
      prov="luts_fuji/gen_cine250d.py"),
    # ---- the catalogue (PLAN 5.1; ids 32..37 fixed by D13, 38..57 tentative until deployed)
    F(32, "PRT160", "Kodak Portra 160", "neg", 160, 77, 5200, 0, "portrait neg", 31, ("Neg 160", 256), ("neg", 256),
      "Fine portrait negative: soft, warm and", "kind to skin.", ready=True,
      prov="luts_ws/PRT160/gen_port160.py (md5 8a78d3e5), judged PASS (judge v2.1; again v2.1.1: luts_ws/PRT160/judge2_PRT160_v211.json)"),
    F(33, "PRT400", "Portra 400 WS", "neg", 400, 88, 5200, 0, "portrait neg", 32, ("Neg 400", 256), ("neg", 256),
      "Portrait negative: low contrast, soft", "highlight roll-off, warm skin.", ready=True, fx="README: Neg 400 x1.0 (print grain index 37)",
      prov="luts_ws/PRT400/gen_prt400.py (md5 0badfb11), README luts_ws/PRT400/README.md, judged PASS (judge v2.1.1, luts_ws/PRT400/judge2_PRT400.json)"),
    F(34, "SUN200", "Kodak Gold 200", "neg", 200, 80, 5200, 0, "consumer neg", 36, ("Neg 250", 386), ("neg", 256),
      "Sunny consumer negative: golden cast,", "rich colour and peach skin.", ready=True, fx="README: new row Neg 200, sigma 0.95 (index 44); nearest row by ISO = Neg 250 (sigma 0.63): 0.95 / 0.63 = x1.51; negative halation x1.0",
      prov="luts_ws/SUN200/gen_sun200.py (md5 935a081f), README luts_ws/SUN200/README.md, judged PASS (judge v2.1.1, luts_ws/SUN200/judge2_SUN200.json)"),
    F(35, "TUN800", "CineStill 800T", "neg", 800, 96, 3200, 0, "cine", 49, ("Neg 800T", 256), ("C800T", 256),
      "Tungsten cine negative: cool in daylight,", "with red halos round highlights.", ready=True, fx="README: Neg 800T x1.0, C800T x1.0 (no rem-jet)",
      prov="luts_ws/TUN800/gen_tun800.py (md5 2e4ed698), README luts_ws/TUN800/README.md, judged PASS (judge v2.1.1, luts_ws/TUN800/judge2_TUN800.json)",
      spk=("spk_n", 256)),
    F(36, "VSL50", "Vivid Slide 50", "slide", 50, 72, 5200, -3, "slide", 50, ("Slide 50", 256), ("slide", 256)),
    F(37, "PRS400", "Kodak Tri-X 400", "bw", 400, 88, 5200, 0, "B&W", 75, ("B&W 400", 256), ("BW", 256),
      "Classic 400-speed B&W: rich mid-tones,", "open shadows, soft shoulder.", ready=True, fx="README: B&W 400 x1.0 (rms 17), BW x1.0",
      prov="luts_ws/PRS400/gen_prs400.py (md5 c58da452), README luts_ws/PRS400/README.md, judged PASS (judge v2.1.1, luts_ws/PRS400/judge2_PRS400.json)"),
    F(38, "TRV400", "Fuji Superia 400", "neg", 400, 88, 5200, 0, "consumer neg", 40, ("Neg 400", 282), ("neg", 256),
      "Punchy negative: green lean, cool shadows,", "warm highlights.", ready=True, fx="README: Neg 400 x1.1, negative halation",
      prov="luts_ws/TRV400/gen_trv400.py (md5 7e5d0793), README luts_ws/TRV400/README.md, judged PASS (judge v2.1.1, luts_ws/TRV400/judge2_TRV400.json)"),
    F(39, "BLD100", "Kodak Ektar 100", "neg", 100, 72, 5200, 0, "saturated / creative", 42, ("Neg 160", 256), ("neg", 256),
      "Fine-grain negative: vivid colour, strong", "reds and deep blue skies.", ready=True, fx="README: new row Neg 100 from the index 38 at 8x10 against 59 for the 400 portrait (sigma 0.8 x 38 / 59 = 0.515); nearest row Neg 160 (0.51) x1.0",
      prov="luts_ws/BLD100/gen_bld100.py (md5 ad7f5570), README luts_ws/BLD100/README.md, judged PASS (judge v2.1.1, luts_ws/BLD100/judge2_BLD100.json)",
      spk=("spk_s", 256)),
    F(40, "EVD400", "Kodak UltraMax", "neg", 400, 88, 5200, 0, "consumer neg", 39, ("Neg 400", 294), ("neg", 256),
      "Bright, vibrant consumer negative with", "firm contrast and deep skies.", ready=True, fx="README: Neg 400 x1.15, negative halation",
      prov="luts_ws/EVD400/gen_evd400.py (md5 a8744ef9), README luts_ws/EVD400/README.md, judged PASS (judge v2.1.1, luts_ws/EVD400/judge2_EVD400.json)"),
    F(41, "STR400", "Ilford HP5 Plus", "bw", 400, 88, 5200, 0, "B&W", 76, ("B&W 400", 256), ("BW", 256),
      "400-speed B&W: dense mid-tones and", "visible cubic grain.", ready=True, fx="README: B&W 400 x1.0, BW x1.0",
      prov="luts_ws/STR400/gen_str400.py (md5 64f5151a), README luts_ws/STR400/README.md, judged PASS (judge v2.1.1, luts_ws/STR400/judge2_STR400.json)"),
    F(42, "SLD100", "Slide 100", "slide", 100, 72, 5200, -3, "slide", 52, ("Slide 100", 256), ("slide", 256)),
    F(43, "PRT800", "Portra 800 WS", "neg", 800, 96, 5200, 0, "portrait neg", 33, ("Neg 800T", 266), ("neg", 256),
      "Portrait 800: warm, rich colour, wide", "latitude and a grainy texture.", ready=True, fx="README: new row Neg 800 from the index 48 at 4x6 against 37 for the 400 portrait (sigma 0.8 x 48 / 37 = 1.04); nearest row Neg 800T (1.0) x1.04",
      prov="luts_ws/PRT800/gen_prt800.py (md5 fdedaf1c), README luts_ws/PRT800/README.md, judged PASS (judge v2.1.1, luts_ws/PRT800/judge2_PRT800.json)"),
    F(44, "PAS400", "Fuji Pro 400H", "neg", 400, 88, 5200, 0, "portrait neg", 34, ("Neg 400", 218), ("neg", 256),
      "Pastel portrait look: soft, airy colour,", "bright highlights, gentle skin.", ready=True, fx="README: Neg 400 x0.85, negative halation",
      prov="luts_ws/PAS400/gen_pas400.py (md5 8aec87fe), README luts_ws/PAS400/README.md, judged PASS (judge v2.1.1, luts_ws/PAS400/judge2_PAS400.json)"),
    F(45, "FIN100", "Kodak T-Max 100", "bw", 100, 72, 5200, 0, "B&W", 71, ("B&W 400", 120), ("BW", 256),
      "Fine-grain 100-speed B&W with open", "mid-tones.", ready=True, fx="README: new row B&W 100T, rms 8 against 17 for B&W 400: x0.47 of B&W 400; BW x1.0",
      prov="luts_ws/FIN100/gen_fin100.py (md5 10a5a3de), README luts_ws/FIN100/README.md, judged PASS (judge v2.1.1, luts_ws/FIN100/judge2_FIN100.json)"),
    F(46, "NT3200", "T-Max P3200 WS", "bw", 3200, 112, 5200, 0, "B&W", 78, ("B&W 400", 358), ("BW", 256),
      "Pushed 3200-speed B&W: coarse grain", "and blocked shadows.", ready=True, fx="README: new row B&W 3200 = 1.4 x B&W 400 (rms 18 at normal development, pushed): B&W 400 x1.4; BW x1.0",
      prov="luts_ws/NT3200/gen_nt3200.py (md5 84e13258), README luts_ws/NT3200/README.md, judged PASS (judge v2.1.1, luts_ws/NT3200/judge2_NT3200.json)"),
    F(47, "CHM400", "Ilford XP2 Super", "bw", 400, 88, 5200, 0, "B&W", 77, ("B&W 400", 136), ("BW", 256),
      "400-speed dye-based B&W: soft tones", "and wide latitude.", ready=True, fx="README: new row Chromo 400 = 0.53 x B&W 400 (rms 9 against 17): B&W 400 x0.53 (silver-grain row: the shadow weighting of the dye-cloud grain is not reproduced); BW x1.0",
      prov="luts_ws/CHM400/gen_chm400.py (md5 79f4b7d4), README luts_ws/CHM400/README.md, judged PASS (judge v2.1.1, luts_ws/CHM400/judge2_CHM400.json)"),
    F(48, "SNP200", "Kodak ColorPlus", "neg", 200, 80, 5200, 0, "consumer neg", 37, ("Neg 250", 444), ("neg", 256),
      "200-speed consumer negative with a", "warm, soft print look.", ready=True, fx="README: Neg 200 x1.15 = sigma 0.95 x 1.15 = 1.09; nearest row Neg 250 (0.63) x1.73",
      prov="luts_ws/SNP200/gen_snp200.py (md5 27d7a01f), README luts_ws/SNP200/README.md, judged PASS (judge v2.1.1, luts_ws/SNP200/judge2_SNP200.json)"),
    F(49, "TRV200", "Fuji C200", "neg", 200, 80, 5200, 0, "consumer neg", 38, ("Neg 250", 256), ("neg", 256)),   # preset of TRV400: not a film
    F(50, "CIN50D", "Kodak 50D", "neg", 50, 72, 5200, 0, "cine", 46, ("Neg 160", 326), ("C800T", 154),
      "Slow daylight cine negative: clean,", "neutral colour, lab-scan finish.", ready=True, fx="README: new row Neg 50 from the published rms curves of the daylight cine stock (about 0.65 of the tungsten cine shape the rows use, 0.51 for Neg 160): Neg 160 x1.27; C800T x0.6",
      prov="luts_ws/CIN50D/gen_cin50d.py (md5 5d969bbc), README luts_ws/CIN50D/README.md, judged PASS (judge v2.1.1, luts_ws/CIN50D/judge2_CIN50D.json)"),
    F(51, "CSL100", "Clean Slide 100", "slide", 100, 72, 5200, -3, "slide", 53, ("Slide 100", 256), ("slide", 256)),
    F(52, "HER64", "Heritage 64", "slide", 64, 72, 5200, -3, "slide", 51, ("Slide 64", 256), ("slide", 256)),
    F(53, "CRS100", "Ilford Delta 100", "bw", 100, 72, 5200, 0, "B&W", 72, ("B&W 400", 126), ("BW", 256),
      "Fine-grain 100-speed B&W with crisp,", "clean tones.", ready=True, fx="README: B&W 100T x1.05 = 0.47 x 1.05 of B&W 400: x0.49; BW x1.0",
      prov="luts_ws/CRS100/gen_crs100.py (md5 ba716ed2), README luts_ws/CRS100/README.md, judged PASS (judge v2.1.1, luts_ws/CRS100/judge2_CRS100.json)"),
    F(54, "SHP50", "Ilford Pan F 50", "bw", 50, 72, 5200, 0, "B&W", 70, ("B&W 400", 72), ("BW", 256),
      "Slow 50-speed B&W: punchy contrast,", "grain so fine it almost vanishes.", ready=True, fx="README: new row B&W 50 = 0.6 x B&W 100T: 0.6 x 0.47 = x0.28 of B&W 400; BW x1.0",
      prov="luts_ws/SHP50/gen_shp50.py (md5 0e87cd09), README luts_ws/SHP50/README.md, judged PASS (judge v2.1.1, luts_ws/SHP50/judge2_SHP50.json)"),
    F(55, "SLK100", "Fuji Acros 100 II", "bw", 100, 72, 5200, 0, "B&W", 73, ("B&W 400", 108), ("BW", 256),
      "100-speed B&W with dark reds and", "smooth, crisp tones.", ready=True, fx="README: B&W 100T x0.9 = 0.9 x 0.47 = x0.42 of B&W 400; BW x1.0",
      prov="luts_ws/SLK100/gen_slk100.py (md5 20b83692), README luts_ws/SLK100/README.md, judged PASS (judge v2.1.1, luts_ws/SLK100/judge2_SLK100.json)"),
    F(56, "VTG100", "Fomapan 100 WS", "bw", 100, 72, 5200, 0, "B&W", 74, ("B&W 400", 157), ("BW", 640),
      "Vintage 100-speed B&W with low contrast", "and a soft base.", ready=True, fx="README: new row B&W 100C = 1.3 x B&W 100T = x0.61 of B&W 400; new profile BW_WEAK = BW at scale 2.5, radius x1.5: BW x2.5 (strength only, the radius is not reproduced)",
      prov="luts_ws/VTG100/gen_vtg100.py (md5 e728c3a9), README luts_ws/VTG100/README.md, judged PASS (judge v2.1.1, luts_ws/VTG100/judge2_VTG100.json)",
      spk=("spk_bww", 256)),
    F(57, "POP800", "Lomo Color 800", "neg", 800, 96, 5200, 0, "saturated / creative", 43, ("Neg 800T", 320), ("neg", 256),
      "Vivid 800-speed negative: saturated,", "creative colour.", ready=True, fx="README: Neg 800 x1.2 = sigma 1.04 x 1.2 = 1.25 = Neg 800T x1.25 (the set of Chungking 320); negative halation",
      prov="luts_ws/POP800/gen_pop800.py (md5 fd6c7718), README luts_ws/POP800/README.md, judged PASS (judge v2.1.1, luts_ws/POP800/judge2_POP800.json)"),
    F(58, "DLY200", "Kodacolor 200", "neg", 200, 80, 5200, 0, "consumer neg", 66, ("Neg 250", 256), ("neg", 256)),   # preset of SUN200: not a film
    F(59, "CRX125", "Switch Azure", "neg", 125, 72, 5200, 0, "saturated / creative", 45, ("Neg 160", 230), ("neg", 333),
      "Cross-processed look: orange pushed to", "cyan-blue, vivid and punchy.", ready=True, fx="README: Neg 160 x0.9; negative halation x1.3 with a blue-biased halo (the bias is not reproduced: neg x1.30)",
      prov="luts_ws/CRX125/gen_crx125.py (md5 c92ef206), README luts_ws/CRX125/README.md, judge v2.1.1 luts_ws/CRX125/judge2_CRX125.json (effect film: J3 skin gates waived by the owner, J8_kernel_max 8 > 7 accepted, 1-code waiver)",
      spk=("spk_w", 333)),
    F(60, "BAS200", "Pan 200", "bw", 200, 80, 5200, 0, "B&W", 80, ("B&W 400", 192), ("BW", 640),
      "Everyday 200-speed B&W: punchy, fine", "grain and a bright base.", ready=True, fx="README: B&W 400 x0.75; BW_WEAK as VTG100 (no anti-halation layer): BW x2.5 (strength only)",
      prov="luts_ws/BAS200/gen_bas200.py (md5 989624dc), README luts_ws/BAS200/README.md, judged PASS (judge v2.1.1, luts_ws/BAS200/judge2_BAS200.json)",
      spk=("spk_bww", 256)),
    F(61, "CLS125", "Ilford FP4 Plus", "bw", 125, 72, 5200, 0, "B&W", 79, ("B&W 400", 157), ("BW", 256),
      "Fine-grain 125-speed B&W: long tonal", "range and clean highlights.", ready=True, fx="README: new row B&W 125 or the finest cubic row x1.0: the cubic-100 equivalent x0.61 of B&W 400; BW x1.0",
      prov="luts_ws/CLS125/gen_cls125.py (md5 cff91fc8), README luts_ws/CLS125/README.md, judged PASS (judge v2.1.1, luts_ws/CLS125/judge2_CLS125.json)"),
    # ---- Film LUT cans (V10): the 4 film cubes of the menu Film LUT (V9) as shelf entries, first on the strip (V14T, owner 2026-11-13: cans first, B&W films last);
    # ISO / white balance as our rows of the same kind (daylight cine 5500 K as Cine 250D, tungsten 3200 K as CineStill 800T)
    LUTCAN(62, "LV250D", "Vision3 250D LUT", 250, 83, 5500, 1, 1, "Daylight cine negative printed to a", "cinema print stock, as a V-Log LUT.",
           "V9 cube V250D_S3.CUB (spektrafilm profiles, CC BY-SA 4.0)"),
    LUTCAN(63, "LV500T", "Vision3 500T LUT", 500, 91, 3200, 2, 2, "Tungsten cine negative printed to a", "cinema print stock, as a V-Log LUT.",
           "V9 cube V500T_S3.CUB (spektrafilm profiles, CC BY-SA 4.0)"),
    LUTCAN(64, "LPR400", "Portra 400 LUT", 400, 88, 5200, 3, 3, "Portrait negative, neutral scan:", "soft contrast, natural skin.",
           "V9 cube PRT400S3.CUB (spektrafilm profiles, CC BY-SA 4.0)"),
    LUTCAN(65, "LFP400", "Pro 400H LUT", 400, 88, 5200, 4, 4, "Portrait negative, airy scan: pastel", "colour, bright highlights.",
           "V9 cube FP400HS3.CUB (spektrafilm profiles, CC BY-SA 4.0)"),
    # the owner's own Resolve power grade "V-Log EKTA P YRGB" (2026-11-13): his 65^3 V-Log export as EKTAP_VL.CUB, the
    # other logs from it (luts_film/src/mk_usercube.py); not shipped, the can shows once his cube is on the card
    # V14T: "Windy EKTA P" (named Tung until 11-14), the EKTA P look of the owner's film pack (its V-Log 33^3 cube, or his own 65^3 Resolve export)
    LUTCAN(66, "LEKTAP", "Windy EKTA P", 100, 72, 5500, 15, 11, "Slide-film look with a cinema print", "finish, applied as a V-Log LUT.",
           "owner's film pack V-Log cube EKTAP_VL.CUB (Resolve 33^3 or his 65^3 export), not in the repo"),
    # ---- V12 (owner 2026-11-13 "make cans"): the owner's six V-Log cubes of V11 (Resolve 33^3 from his VlogCube folder,
    # licence unknown: on the cards only, never in the repo) as cans after the four; id 66 is the EKTA P can (V13,
    # branch ugrade-can). Print looks as daylight cine, Ektar as ISO 100, the two graders' looks ISO 400.
    LUTCAN(67, "LF3513", "Fuji 3513 LUT", 250, 83, 5500, 5, 5, "Cine print-stock look with a rich,", "contrasty finish, as a V-Log LUT.",
           "owner's V-Log cube FJ3513VL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(68, "LK83V3", "2383 v3 LUT", 250, 83, 5500, 6, 6, "Cinema print-stock look, version 3,", "applied as a V-Log LUT.",
           "owner's V-Log cube KD83V3VL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(69, "LK83V5", "2383 v5 LUT", 250, 83, 5500, 7, 7, "Cinema print-stock look, version 5,", "applied as a V-Log LUT.",
           "owner's V-Log cube KD83V5VL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(70, "LEKTR5", "Kodak Ektar LUT", 100, 72, 5200, 8, 8, "Vivid negative look: punchy colour,", "strong reds, as a V-Log LUT.",
           "owner's V-Log cube EKTAR5VL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(71, "LTUNGN", "Windy Neutral", 400, 88, 5200, 10, 9, "Neutral film look: restrained colour", "and gentle contrast, as a V-Log LUT.",
           "owner's V-Log cube TUNGNTVL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(72, "LLBONO", "Luca Bono LUT", 400, 88, 5200, 9, 10, "Graded film look with warm, rich tones,", "applied as a V-Log LUT.",
           "owner's V-Log cube LBONO_VL.CUB (Resolve 33^3), not in the repo"),
    # ---- V14T (owner 2026-11-13 "make films of these, names starting with Tung"; named "Windy" 11-14 as Film Roll V14U): the eleven other looks of the owner's
    # purchased film pack (its V-Log 33^3 monitoring cubes, the same looks as its Resolve power grades with a flatter
    # curve; on the cards only, never in the repo); id 66 above is their EKTA P. P = 2383 print output, M = simple DI curve.
    LUTCAN(73, "LT25DP", "Windy 250D P", 250, 83, 5500, 11, 12, "Daylight cine negative with a cinema", "print finish, as a V-Log LUT.",
           "owner's film pack V-Log cube T250DPVL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(74, "LT25DM", "Windy 250D M", 250, 83, 5500, 12, 13, "Daylight cine negative with a plain", "intermediate curve, as a V-Log LUT.",
           "owner's film pack V-Log cube T250DMVL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(75, "LT50TP", "Windy 500T P", 500, 91, 3200, 13, 14, "Tungsten cine negative with a cinema", "print finish, as a V-Log LUT.",
           "owner's film pack V-Log cube T500TPVL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(76, "LT50TM", "Windy 500T M", 500, 91, 3200, 14, 15, "Tungsten cine negative with a plain", "intermediate curve, as a V-Log LUT.",
           "owner's film pack V-Log cube T500TMVL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(77, "LTEKTM", "Windy EKTA M", 100, 72, 5500, 16, 16, "Slide-film look with a plain", "intermediate curve, as a V-Log LUT.",
           "owner's film pack V-Log cube TEKTAMVL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(78, "LTEKAP", "Windy EKTAa P", 100, 72, 5500, 17, 17, "Alternate slide-film look with a cinema", "print finish, as a V-Log LUT.",
           "owner's film pack V-Log cube TEKAAPVL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(79, "LTEKAM", "Windy EKTAa M", 100, 72, 5500, 18, 18, "Alternate slide-film look with a plain", "intermediate curve, as a V-Log LUT.",
           "owner's film pack V-Log cube TEKAAMVL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(80, "LTEXRP", "Windy EXR P", 500, 91, 3200, 19, 19, "Classic 500T negative look with a cinema", "print finish, as a V-Log LUT.",
           "owner's film pack V-Log cube TEXRP_VL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(81, "LTEXRM", "Windy EXR M", 500, 91, 3200, 20, 20, "Classic 500T negative look with a plain", "intermediate curve, as a V-Log LUT.",
           "owner's film pack V-Log cube TEXRM_VL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(82, "LTEXAP", "Windy EXRa P", 500, 91, 3200, 21, 21, "Alternate 500T negative look with a cinema", "print finish, as a V-Log LUT.",
           "owner's film pack V-Log cube TEXRAPVL.CUB (Resolve 33^3), not in the repo"),
    LUTCAN(83, "LTEXAM", "Windy EXRa M", 500, 91, 3200, 22, 22, "Alternate 500T negative look with a plain", "intermediate curve, as a V-Log LUT.",
           "owner's film pack V-Log cube TEXRAMVL.CUB (Resolve 33^3), not in the repo"),
    # ---- 11-14 (as Film Roll V14U): "Leica Vivid", the owner's Lumix "L Leica 2" look (made on Picture Style Standard) as a
    # display-referred V-Log 65^3 cube: sRGB -> V-Log -> cube gives that look on the camera's Standard JPEG. So the can
    # reads the JPEG as sRGB whatever the Input curve, has no auto exposure, and its load sets Picture Style Standard
    # (wf_loglut.h WF_FLUT_DISP). Name only: no logo, mark or house colour of the camera maker on the can.
    LUTCAN(84, "LLEICV", "Leica Vivid", 100, 72, 5200, 23, 23, "Vivid colour look made on the camera's", "Standard picture, applied as a LUT.",
           "owner's Lumix L Leica 2 cube as a V-Log 65^3 display LUT LEICAVVL.CUB (Film Roll V14U), not in the repo"),
]
# the author's codes (no own code may equal one; generic notations only, PLAN 5.1)
AUTHOR_CODE_RULE = "no code equal to one of the author's (P160 .. XP2): checked against tools/brandscan.py's list"

def t17_path(f, variant="scan"):
    """films/tables/<table>.T17 (scan), <table>_C.T17 (cine print), <table>_P.T17 (paper print)"""
    if f["t17"] and variant == "scan":
        return f["t17"]
    return os.path.join(HERE, "tables", f["table"] + PSUFFIX[variant] + ".T17")

def load_t17(f, variant="scan"):
    b = open(t17_path(f, variant), "rb").read()
    assert len(b) == 19652, (f["code"], "a table is 4913 words")
    return list(struct.unpack("<4913I", b))

def rows(synth=0, gaps=False):
    """the compiled rows: ids 26..max compiled id, placeholders for ids without a ready film.
    synth > 0: TEST_SYNTH stress catalogue (that many extra synthetic ready films after the last id, their tables
    the shipped films' in turn; the second one a B&W film (type 2: neutral table, B&W grain and halation), the
    third one on its base film's grain row with another amount, so the host test has both cases); gaps: TEST_GAPS
    (ids 31, 34 and the last film id become placeholders; the Film LUT cans stay)"""
    films = {f["id"]: dict(f) for f in FILMS}
    if synth:
        base = [f for f in FILMS if f["ready"] and not f["lut"]]
        top = max(films)
        for i in range(synth):
            b = base[i % len(base)]
            f = dict(b, id=top + 1 + i, code="SYN%03d" % i, table="SYN%03d" % i, name="Synth %d" % i,
                     ready=True, mode=None, v21=True, order=100 + i, t17=t17_path(b), synth=True, lut=0)
            if i == 1:
                f.update(type="bw", family="B&W", grain=("B&W 400", 256), halo=("BW", 256), spk=None)
            if i == 2:
                f.update(grain=(b["grain"][0], b["grain"][1] + 64))
            films[top + 1 + i] = f
    if gaps:
        for i in (31, 34, max(i for i, f in films.items() if not f["lut"])):
            films[i] = dict(films.get(i, {}), id=i, ready=False)
    idmax = max(films)
    out = []
    for i in range(ID0, idmax + 1):
        f = films.get(i)
        out.append(f if f and f.get("ready") else None)
    return out, idmax

def sets(rs):
    gs, hs = [], []
    def gkey(f):
        return (GRAIN_ROWS[f["grain"][0]], f["grain"][1], f.get("rho_q8", 0))
    def hkey(f):
        s = spk_of(f)
        return (HALO_PROFILES[f["halo"][0]], f["halo"][1], HALO_PROFILES[s[0]], s[1])
    lutg = (GRAIN_ROWS[LUT_GRAIN[0]], LUT_GRAIN[1], 0)
    luth = (HALO_PROFILES[LUT_HALO[0]], LUT_HALO[1], HALO_PROFILES[LUT_SPK[0]], LUT_SPK[1])
    gs.append(lutg); hs.append(luth)
    gi, hi = [], []
    for f in rs:
        if not f:
            gi.append(-1); hi.append(-1); continue
        k = gkey(f)
        if k not in gs: gs.append(k)
        gi.append(gs.index(k))
        k = hkey(f)
        if k not in hs: hs.append(k)
        hi.append(hs.index(k))
    return gs, hs, gi, hi

def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

def header(out, synth=0, gaps=False):
    rs, idmax = rows(synth, gaps)
    for f in rs:
        if f: rho_for(f)
    gs, hs, gi, hi = sets(rs)
    if len(gs) > 62 or len(hs) > 62:              # fr_fx.c: an effect key holds set + 1 in bits 0..5
        sys.exit("catalog: %d grain / %d halation sets, at most 62 fit the effect keys" % (len(gs), len(hs)))
    n = len(rs)
    pres = [f for f in rs if f]
    order = sorted(pres, key=lambda f: f["order"])
    direct = [-1] * (MODE_MAX + 1)
    for k, f in enumerate(rs):
        if f and f.get("mode"):
            direct[f["mode"]] = k
    L = []
    L.append("/* fr_catalog.h - generated by films/catalog.py (%s): do not edit */" % (
        "TEST_SYNTH=%d" % synth if synth else "TEST_GAPS" if gaps else "release catalogue"))
    L.append("#ifndef FR_CATALOG_H\n#define FR_CATALOG_H\n#include <stdint.h>")
    L.append("#define FR_CAT_ID0      %d" % ID0)
    L.append("#define FR_CAT_IDMAX    %d                 /* max compiled id */" % idmax)
    L.append("#define FR_CAT_N        %d                 /* rows: ids %d..%d, placeholders included */" % (n, ID0, idmax))
    L.append("#define FR_CAT_NPRESENT %d" % len(pres))
    L.append("#define FR_CAT_NGSET    %d" % len(gs))
    L.append("#define FR_CAT_NHSET    %d" % len(hs))
    L.append("#define FR_CAT_LUT_GSET 0                  /* the + LUT modes' effect sets */\n#define FR_CAT_LUT_HSET 0")
    L.append("#define FR_CAT_MODE_MAX %d" % MODE_MAX)
    L.append("#define FR_CAT_SYNTH    %d" % synth)
    L.append("#define FR_CAT_GAPS     %d" % (1 if gaps else 0))
    L.append("/* row k = id %d + k: 1 = a ready film (descriptor, can, pack entry) or a Film LUT can (descriptor, can), 0 = placeholder */" % ID0)
    L.append("static const uint8_t fr_cat_present[FR_CAT_N] = { %s };" % ", ".join("1" if f else "0" for f in rs))
    L.append("/* pack entry code (T17 name) and descriptor short code; \"-\" for a placeholder */")
    L.append("static const char fr_cat_table[FR_CAT_N][9] = { %s };" % ", ".join(cstr(f["table"] if f else "-") for f in rs))
    L.append("static const char fr_cat_code[FR_CAT_N][7] = { %s };" % ", ".join(cstr(f["code"] if f else "-") for f in rs))
    L.append("/* fr_film_build flags: FR_FT_V21 1 (adaptive levels + per-pixel E, the catalogue films from id 32), FR_FT_MONO 2 */")
    L.append("static const uint8_t fr_cat_flags[FR_CAT_N] = { %s };" % ", ".join(
        str((1 if f["v21"] else 0) | (2 if f["type"] == "bw" else 0)) if f else "0" for f in rs))
    L.append("static const uint8_t fr_cat_type[FR_CAT_N] = { %s };" % ", ".join(str(TYPES[f["type"]]) if f else "0" for f in rs))
    lrow = [-1] * FLUT_N
    for k, f in enumerate(rs):
        if f and f["lut"]:
            lrow[f["lut"]] = k
    L.append("/* Film LUT cans (V10): row k's menu Film LUT index 1..%d (0: a film or a placeholder), and the can row of each Film" % (FLUT_N - 1))
    L.append(" * LUT (-1: none; 0 = Technical). A can has a descriptor and a can image, no card table (no WSFILMS.DAT entry) */")
    L.append("#define FR_CAT_FLUT_N   %d                  /* fr_loglut.h FR_FLUT_N (checked there) */" % FLUT_N)
    L.append("#define FR_CAT_NLUT     %d" % sum(1 for f in rs if f and f["lut"]))
    L.append("#define FR_CAT_NTAB     %d                 /* ready films with a card table (the WSFILMS.DAT entries) */" % sum(1 for f in rs if f and not f["lut"]))
    L.append("static const uint8_t fr_cat_lut[FR_CAT_N] = { %s };" % ", ".join(str(f["lut"]) if f else "0" for f in rs))
    L.append("static const int8_t fr_cat_lutrow[FR_CAT_FLUT_N] = { %s };" % ", ".join(map(str, lrow)))
    L.append("/* effect set of each row (-1: placeholder) */")
    L.append("static const int8_t fr_cat_gset[FR_CAT_N] = { %s };" % ", ".join(map(str, gi)))
    L.append("static const int8_t fr_cat_hset[FR_CAT_N] = { %s };" % ", ".join(map(str, hi)))
    L.append("/* output mode 0..FR_MODE_MAX -> row of its film, -1 for the other modes (films: 1..4, 11, 12) */")
    L.append("static const int8_t fr_cat_direct[FR_CAT_MODE_MAX + 1] = { %s };" % ", ".join(map(str, direct)))
    L.append("/* the strip order of the ready films (family order, PLAN 5.1), as shelf ids */")
    L.append("#define FR_CAT_NORDER %d" % len(order))
    L.append("static const uint8_t fr_cat_order[FR_CAT_NORDER] = { %s };" % ", ".join(str(f["id"]) for f in order))
    L.append("#ifdef FR_CAT_FX\n/* grain sets: yg_stocks row, amount scale Q8 (256 = x1), rho_max Q8 (0 = the row's value) */")
    L.append("static const struct { uint8_t row; uint16_t scale_q8, rho_q8; } fr_cat_gsets[FR_CAT_NGSET] = { %s };" %
             ", ".join("{ %d, %d, %d }" % g for g in gs))
    L.append("/* halation sets: yh profile (YH_NEG 0, YH_C800T 1, YH_SLIDE 2, YH_BW 3), strength Q8 (yh_params.hal_q8); the")
    L.append(" * spektrafilm profile (YH_SPK_S 4 .. YH_SPK_BWW 9) and its strength Q8 (Halation \"Spektra\") */")
    L.append("static const struct { uint8_t prof, sprof; uint16_t hal_q8, shal_q8; } fr_cat_hsets[FR_CAT_NHSET] = { %s };" %
             ", ".join("{ %d, %d, %d, %d }" % (h[0], h[2], h[1], h[3]) for h in hs))
    L.append("#endif")
    L.append("#ifdef FR_CAT_DESC\n/* the 0x50 B descriptors (fr_shelf.c struct fr_desc): maker, name, code, ISO shown, ISO code, type, picture")
    L.append(" * style (Neutral 0x84, 0 params), Kelvin, WB shift, ev8, drawn canister colours (family), details lines */")
    L.append("#define FR_CAT_DESCS \\")
    for f in rs:
        if f:
            c = FAMILY[f["family"]]
            L.append("    { %s, %s, %s, %d, %d, %d, 0x%02x, 0, 0, 0, 0, %d, 0, 0, %d, { 0x%02x, 0x%02x, 0x%02x }, %s, %s }, \\" % (
                cstr(MAKER), cstr(f["name"]), cstr(f["code"]), f["iso"], f["isocode"], TYPES[f["type"]], PSTYLE_NEUTRAL,
                f["kelvin"], f["ev8"], c[0], c[1], c[2], cstr(f["line1"]), cstr(f["line2"])))
        else:
            L.append("    { %s, \"-\", \"-\", 100, 72, 0, 0x%02x, 0, 0, 0, 0, 5200, 0, 0, 0, { 0x02, 0x02, 0x01 }, \"\", \"\" }, \\" % (
                cstr(MAKER), PSTYLE_NEUTRAL))
    L.append("")
    L.append("#endif\n#endif")
    # every table is static (one copy per object that reads it); unused ones must not warn
    L = [l.replace("] = {", "] FR_CAT_U = {", 1) if l.startswith("static const") else l for l in L]
    L.insert(2, "#define FR_CAT_U __attribute__((unused))")
    open(out, "w").write("\n".join(L) + "\n")

def rho_for(f):
    """rho_max (display density range) of a film from its own T17 (PLAN 3.2.4 b): the neutral-axis output at input
    255 and at input 0, rho = log10(L(Y255) / L(max(Y0, 1))), L the sRGB decoding of the code value. Set on the row
    as rho_q8; 0 (the row's value) while RHO_FROM_T17 is off."""
    if not RHO_FROM_T17 or f.get("lut"):
        f["rho_q8"] = 0
        return
    T = load_t17(f)
    y255 = (T[16 * 289 + 8 * 17 + 8] >> 8) & 255
    y0 = (T[0 * 289 + 8 * 17 + 8] >> 8) & 255
    L = lambda v: (v / 255.0) / 12.92 if v / 255.0 <= 0.04045 else ((v / 255.0 + 0.055) / 1.055) ** 2.4
    rho = math.log10(L(y255) / L(max(y0, 1)))
    f["rho_q8"] = int(round(rho * 256))
RHO_FROM_T17 = False

def pack():
    rs, _ = rows(int(os.environ.get("TEST_SYNTH", "0") or 0), os.environ.get("TEST_GAPS", "") == "1")
    for f in rs:
        if f and not f["lut"]:
            for v in f["prints"]:
                print(f["id"], f["table"], TYPES[f["type"]], (1 if f["v21"] else 0) | (2 if f["type"] == "bw" else 0),
                      PRINTS.index(v), t17_path(f, v))

def names():
    for f in FILMS:
        if f["ready"]:
            for s in (f["name"], f["code"], f["table"], f["line1"], f["line2"]):
                if s: print("%d\t%s" % (f["id"], s))

def _fonts():
    """(13 px wght 500 wdth 82, 15 px wght 500 wdth 100) Ubuntu Sans, or None when PIL / the font is missing"""
    try:
        from PIL import ImageFont
        def f(sz, wd):
            ft = ImageFont.truetype(FONT_UBUNTU, sz)
            ft.set_variation_by_axes([wd, 500])
            return ft
        return f(13, 82), f(15, 100)
    except Exception:
        return None

def check():
    bad = []
    fonts = _fonts()
    if not fonts:
        bad.append("Ubuntu Sans / PIL not found (%s, WF_FONT): the text widths cannot be checked (run in WSL)" % FONT_UBUNTU)
    ids, codes = set(), set()
    for f in FILMS:
        i, c = f["id"], f["code"]
        if i in ids: bad.append("id %d twice" % i)
        ids.add(i)
        if not (ID0 <= i <= ID_LIMIT): bad.append("id %d outside %d..%d" % (i, ID0, ID_LIMIT))
        if c in codes: bad.append("code %s twice" % c)
        codes.add(c)
        if not (1 <= len(c) <= 6 and c == c.upper() and c.isalnum()): bad.append("code %r: 1..6 upper-case letters/digits" % c)
        if len(f["table"]) > 8: bad.append("table code %r longer than 8" % f["table"])
        if len(f["name"]) > NAME_CH: bad.append("%s: name %r over %d chars" % (c, f["name"], NAME_CH))
        if f["ready"]:
            sh = f["short"]
            if not sh or len(sh) > SHORT_CH: bad.append("%s: short %r must be 1..%d chars" % (c, sh, SHORT_CH))
            elif fonts and fonts[0].getlength(sh) > SHORT_PX: bad.append("%s: short %r is %.1f px, limit %d" % (c, sh, fonts[0].getlength(sh), SHORT_PX))
            for ln in (f["line1"], f["line2"]):
                if not ln or len(ln) > LINE_CH: bad.append("%s: line %r must be 1..%d chars" % (c, ln, LINE_CH))
                elif fonts and fonts[1].getlength(ln) > LINE_PX: bad.append("%s: line %r is %.1f px, limit %d" % (c, ln, fonts[1].getlength(ln), LINE_PX))
            if f["kind"] not in KINDS: bad.append("%s: kind %r" % (c, f["kind"]))
            if f["fmt"] not in FMTS: bad.append("%s: fmt %r" % (c, f["fmt"]))
            if f["group"] not in GROUPS: bad.append("%s: group %r" % (c, f["group"]))
            if f["group"] != ("lut" if f["lut"] else "bw" if f["type"] == "bw" else "cine" if f["family"] == "cine" else "colour"):
                bad.append("%s: group %r does not follow type / family" % (c, f["group"]))
            if f["lut"]:
                if f["prints"] != (): bad.append("%s: a Film LUT can has no print variants" % c)
            else:
                pr = f["prints"]
                if not isinstance(pr, tuple) or not pr or pr[0] != "scan" or any(v not in PRINTS for v in pr) or len(set(pr)) != len(pr):
                    bad.append("%s: prints %r must be a tuple starting with 'scan' of %s" % (c, pr, PRINTS))
                else:
                    for v in pr:
                        if not os.path.exists(t17_path(f, v)): bad.append("%s: %s missing" % (c, t17_path(f, v)))
                        elif os.path.getsize(t17_path(f, v)) != 19652: bad.append("%s: %s is not 4913 words" % (c, t17_path(f, v)))
        if f["family"] not in FAMILY: bad.append("%s: family %r" % (c, f["family"]))
        if f["mode"] is not None and (i > 31 or not (1 <= f["mode"] <= 4 or f["mode"] in (11, 12))): bad.append("%s: film output modes are frozen (D4)" % c)
        if f["ready"]:
            if f["grain"][0] not in GRAIN_ROWS: bad.append("%s: grain row %r not in yg_stocks.c" % (c, f["grain"][0]))
            if f["halo"][0] not in HALO_PROFILES: bad.append("%s: halation profile %r not in yh_halo.h" % (c, f["halo"][0]))
            if f["v21"] != (i >= 32): bad.append("%s: FR_FT_V21 must be on exactly for ids 32.." % c)
            if not f["line1"]: bad.append("%s: details line 1 empty" % c)
        if (f["type"] == "bw") != f["halo"][0].startswith("BW"):  # fr_fx.c: a B&W set writes Y only (PLAN 5.2)
            bad.append("%s: a B&W film takes a B&W halation profile, a colour film a colour one" % c)
        s = spk_of(f)
        if s[0] not in HALO_PROFILES or not s[0].startswith("spk_"):
            bad.append("%s: spektrafilm profile %r not a YH_SPK_* of yh_halo.h" % (c, s[0]))
        elif (f["type"] == "bw") != s[0].startswith("spk_bw"):
            bad.append("%s: a B&W film takes a B&W spektrafilm profile, a colour film a colour one" % c)
        if not 0 < s[1] <= 1024: bad.append("%s: spektrafilm strength %d outside 1..1024" % (c, s[1]))
    shorts = {}
    for f in FILMS:
        if f["ready"]: shorts.setdefault(f["short"], []).append(f["code"])
    for sh, cs in shorts.items():
        if len(cs) > 1: bad.append("short %r used twice: %s" % (sh, ", ".join(cs)))
    ngr = {}
    for f in FILMS:
        if f["ready"]: ngr[f["group"]] = ngr.get(f["group"], 0) + 1
    luts = [f["lut"] for f in FILMS if f["lut"]]
    for f in FILMS:
        if f["lut"] and (not 1 <= f["lut"] < FLUT_N or not f["ready"] or f["mode"] is not None or f["t17"]
                         or f["grain"] != LUT_GRAIN or f["halo"] != LUT_HALO or spk_of(f) != LUT_SPK):
            bad.append("%s: a Film LUT can is ready, lut 1..%d, no mode / T17, the + LUT effect parameters" % (f["code"], FLUT_N - 1))
    if len(set(luts)) != len(luts): bad.append("a Film LUT has two cans")
    # strip order (owner 2026-11-13): the Film LUT cans first (the Tung cans too), then the colour films, the B&W films last
    rd = [f for f in FILMS if f["ready"]]
    if max(f["order"] for f in rd if f["lut"]) >= min(f["order"] for f in rd if not f["lut"]):
        bad.append("the Film LUT cans come first on the strip")
    if max(f["order"] for f in rd if f["type"] != "bw") >= min(f["order"] for f in rd if f["type"] == "bw"):
        bad.append("the B&W films come last on the strip")
    orders = [f["order"] for f in FILMS]
    if len(set(orders)) != len(orders): bad.append("strip order values not unique")
    for b in bad: print("catalog: " + b)
    print("catalog: %d films (%d ready: %s), ids %d..%d, texts <= %d chars / %d px; %s" % (
        len(FILMS), sum(f["ready"] for f in FILMS), ", ".join("%s %d" % kv for kv in sorted(ngr.items())), ID0,
        max(f["id"] for f in FILMS), LINE_CH, LINE_PX, "ok" if not bad else "%d problem(s)" % len(bad)))
    return not bad

def docs(out):
    L = ["# WindyFilm films (generated by films/catalog.py)", "",
         "Shelf id, name, code, type, ISO, white balance, family, effects, and whether this build ships the film.",
         "Each film's look comes from its own generator; its provenance is named in the last column.",
         "From id 32 the names are the films' common names (owner decision 2026-11-11), fitted to the shelf width; the",
         "looks are our own parametric models (OKLab, per film generator), not made from scans or LUTs of those",
         "products; several take published data-sheet curves as inputs, cited in each generator. The names are",
         "trademarks of their owners; this project is not affiliated with them.", "",
         "| id | name | code | type | ISO | WB K | family | grain | halation | spektrafilm halation | in this build | provenance |",
         "|---:|---|---|---|---:|---:|---|---|---|---|:-:|---|"]
    for f in sorted(FILMS, key=lambda f: f["id"]):
        g = "%s x%.2f" % (f["grain"][0], f["grain"][1] / 256.0)
        h = "%s x%.2f" % (f["halo"][0], f["halo"][1] / 256.0)
        s = spk_of(f)
        if f["lut"]:                                  # a Film LUT can: no table, the + LUT effects
            g = h = "as + LUT"
        L.append("| %d | %s | %s | %s | %d | %d | %s | %s | %s | %s | %s | %s |" % (
            f["id"], f["name"], f["code"], f["type"], f["iso"], f["kelvin"], f["family"], g, h,
            "as + LUT" if f["lut"] else "%s x%.2f" % (s[0], s[1] / 256.0),
            "cube" if f["lut"] else "yes" if f["ready"] else "placeholder", f["prov"] or "-"))
    L += ["", "## spektrafilm halation (Halation: Spektra / Spektra + bloom)", "",
          "The halation model of spektrafilm (github andreavolpato/spektrafilm, GPLv3; its published model numbers, no code):",
          "light reflected back from the film base re-exposes the emulsion in 3 bounces, Gaussians of sigma, 1.41 sigma and",
          "1.73 sigma with weights 4/7, 2/7, 1/7, the same sigma in every layer (65 um still, 50 um cine; 9.4 / 7.2 px at",
          "36 mm per 5184 px), each layer re-exposed by its own light, strengths red / green / blue by the film's",
          "anti-halation class: strong 0.015 / 0.005 / 0, weak 0.08 / 0.02 / 0, none 0.30 / 0.10 / 0.015. Profiles",
          "`spk_s` still strong, `spk_w` still weak, `spk_c` cine strong, `spk_n` cine none (rem-jet removed); `spk_bw` /",
          "`spk_bww` B&W strong / weak are a GUESS (spektrafilm has no B&W film). Per-level weights: proto/halation_bloom/",
          "fit_levels.py. Kept from this build: the knee (1 EV over grey: spektrafilm has no threshold but renormalises by",
          "1 + strength, which a display-referred frame cannot do without a colour shift) and the clipped-highlight boost."]
    L += ["", "## Effects mapping (grain row and halation profile of each catalogue film)", "",
          "Only rows of `yg_stocks.c` and profiles of `yh_halo.h` that exist in this build are used (no new row, no new",
          "profile in this release). Each film's README proposes a grain row and a halation profile, some of them new;",
          "the nearest existing row is taken (the row of the nearest ISO of the same type) with an amount scale Q8",
          "(256 = x1.0) = the README's target sigma / that row's sigma (colour rows: Neg 160 0.51, Neg 250 0.63,",
          "Neg 400 0.80, Neg 800T 1.00 of the tungsten cine shape; B&W 400 rms 17 = 1.0; B&W 100T rms 8 = 0.47), and the halation",
          "profile of the README with its strength scale. B&W films keep U and V at exactly 0 and halate luma only (the",
          "`BW` profile). A README effect that the build cannot express (a halo radius, a blue-biased halo, the shadow",
          "weighting of a dye-cloud grain) is named in the note and left out.", "",
          "| id | code | grain row x scale (Q8) | halation x strength (Q8) | spektrafilm x strength (Q8) | basis |",
          "|---:|---|---|---|---|---|"]
    for f in sorted(FILMS, key=lambda f: f["id"]):
        if f["ready"] and f["id"] >= 32 and not f["lut"]:
            s = spk_of(f)
            L.append("| %d | %s | %s x%.2f (%d) | %s x%.2f (%d) | %s x%.2f (%d) | %s |" % (
                f["id"], f["code"], f["grain"][0], f["grain"][1] / 256.0, f["grain"][1], f["halo"][0],
                f["halo"][1] / 256.0, f["halo"][1], s[0], s[1] / 256.0, s[1], f["fx"] or "unchanged (shipped with id 32)"))
    L += ["", "## Film LUT cans (V10)", "",
          "Rows of family `film LUT` (\"cube\" above) are no film of ours: no table in WSFILMS.DAT, no look of their own.",
          "Each stands for one film cube of the menu Film LUT (V9, docs/README_EOSM.txt): the shelf shows the can only",
          "while its V-Log cube is in ML/DATA, and its Load sets Film LUT to that cube and the output to V-Log + LUT",
          "(a + LUT output already set stays, its curve kept: S-Log3 / LogC3 + LUT then use the _S3 / _LC cube, so all",
          "three cubes of the film belong on the card). Grain, halation and mist are those of the + LUT outputs.", "",
          "| id | code | Film LUT | V-Log cube |", "|---:|---|---|---|"]
    for f in sorted(FILMS, key=lambda f: f["id"]):
        if f["lut"]:
            L.append("| %d | %s | %s | ML/DATA/%s |" % (f["id"], f["code"], FLUT_NAME[f["lut"]], FLUT_VL[f["lut"]]))
    L += ["", "Output modes 1..4, 11 and 12 of the Film Roll menu shoot ids 26..31 directly; every other film is",
          "used through the shelf (\"Loaded film\"). Ids never change once deployed; a placeholder keeps its id."]
    open(out, "w").write("\n".join(L) + "\n")

if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "header":
        synth = int(a[a.index("--synth") + 1]) if "--synth" in a else 0
        header(a[1], synth, "--gaps" in a)
    elif a and a[0] == "pack":
        pack()
    elif a and a[0] == "docs":
        docs(a[1])
    elif a and a[0] == "names":
        names()
    elif a and a[0] == "check":
        sys.exit(0 if check() else 1)
    else:
        print(__doc__); sys.exit(2)
