#!/usr/bin/env python3
"""look_sweep.py - the look sweep's lists and gallery (WindyStrife). Films and Film LUT cans of films/catalog.py on real
RAW frames, all from the CR2: the Darkroom's own develop (tests/host/wf_dark_test.c devset: the sRGB base for the films,
each can's own cube for that can) and the Darkroom's look stage (tests/host/wf_test.c LOOK:sweep, path d) with the film's
full default finish (grain, halation, bloom as the camera prints them). The camera's own JPEGs are never a "before" or a
look: only their Exif may caption a frame when the CR2 lacks a tag. tools/look_sweep.sh runs it. The owner's photos and
everything made from them stay outside the repository (WORK, OUT): never commit them.

  python3 -I -B tools/look_sweep.py lists MODE WORK CARD FX_FILM FRAME...
      WORK/devset.txt (wf_dark_test devset), WORK/jobs.txt (LOOK:sweep), WORK/meta.json. MODE films | stage; CARD = a
      card's ML/DATA (the cans' cubes, read only); FX_FILM = the film id of the effect sets; FRAME = NAME=CR2@EV8@TAGS@X,Y
      with EV8 the shot's EV100 x 8 ('-' = from the CR2's Exif, as the Darkroom does), TAGS out of skin, glow, sky, matrix,
      gauge joined by '+' and X,Y the 1:1 crop centre on 5184 x 3456.
  python3 -I -B tools/look_sweep.py gallery WORK OUT
      OUT/<set>/*.jpg (1600 px long side), OUT/crops/<set>/*.jpg (960 x 640 at 1:1), OUT/strips/*.jpg, OUT/index.json +
      index.csv (one row per image and set: set, level, film, frame, every setting, variant, gauge, negative / print),
      OUT/stats.txt (Y percentiles, clipped / crushed share, mean chroma; the nearest looks), films mode also OUT/pairs
      (neutral RAW develop | look) and WORK/agree.txt (the camera's stage against the Darkroom's: the report's, not the
      gallery's).

Sets, films mode: neutral (the develop with no look: the "before"), film (every film and can, its default finish), bloom,
mist (the effect film, one level each, the rest at the default). Stage mode (the film-stage chain films, DESIGN 1a): neutral,
matrix (every negative x every print it has, on the frames tagged matrix), negative (every negative at the camera's
default print, 2383 = WF_PRINT_CHAIN_DEF or a film's first print without it, on every frame), contrast (every frame: a catalogue film at Scan, a Film LUT can
and three chain films at 2383, side by side), grain, halation, bloom (glow frames), gauge (frames tagged gauge).
Env (lists): LOOK_SETS = only these sets (comma list; neutral always), e.g. matrix,contrast to show the matrix first;
LOOK_RESUME=1 leaves out of jobs.txt the shots whose PPMs are already there (meta.json still lists them all).
"""
import csv, importlib.util, json, math, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 5184, 3456
GRAIN = ("Off", "Fine", "Normal", "Strong", "Even Fine", "Even Normal", "Even Strong")   # src/wf_menu.c wf_grain_name
HALO = ("Off", "Wide", "Tight", "Wrap")
BLOOM = ("Off", "Low", "Normal", "Strong")
MIST = ("Off", "White 1/8", "White 1/4", "White 1/2", "White 1", "Black 1/8", "Black 1/4", "Black 1/2", "Black 1")
GAUGE = ("16 mm", "35 mm", "65 mm")
STRENGTH = ("30%", "100%", "125%")
PRINT_NAME = ("Scan", "Cine", "Paper", "2302", "2383", "2393", "2395", "5380", "5381/50", "5381/72", "5382", "5384/82",
              "5384/92", "5385/93", "5385/62", "5386", "5383", "3513D", "3523XD")              # src/wf_menu.c wf_print_name
PRINT_FULL = ("", "", "", "Kodak B&W Print Film 2302", "Kodak Vision Color 2383", "Kodak Vision Premier Color 2393",
              "Kodak Vision Color Teleprint 2395 500T", "Eastman Color Print LC 5380", "Eastman Color Print 5381 (1950)",
              "Eastman Color Print 5381 (1972)", "Eastman Color Print 5382", "Eastman Color Print 5384 (1982)",
              "Eastman Color Print 5384 (1992)", "Eastman Color Print LC 5385 (1993)", "Eastman Color Print 5385 (1962)",
              "Eastman EXR Color Print 5386", "Eastman Color Print 5383", "Fuji Eterna-CP Type 3513D",
              "Fuji Eterna-CP 3523XD")                                                     # src/wf_menu.c wf_print_full
DEF = dict(pr=0, s=1, g=2, h=1, b=2, m=0, ga=1)   # the EOS M's finish out of the box (src/wf_cam.h WF_*_DEF_CAM, WF_GAUGE_DEF)
NEUTRAL = "neutral RAW develop"
STRIP_CAT = (33, 62)          # stage mode's contrast strip: Portra 400 WS (Scan) and the Vision3 250D LUT can ...
STRIP_CHAIN = (92, 97, 128)   # ... next to three chain films at 2383: Vision3 5207 250D, 5219 500T, Eterna 8563 250D
STRIP_LEVEL = {33: "catalogue film, Scan", 62: "Film LUT can", 0: "chain film, print 2383"}


def catalog():
    spec = importlib.util.spec_from_file_location("wf_catalog_src", os.path.join(ROOT, "films", "catalog.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def tiff_tags(b, base=0):
    """IFD0 and the Exif IFD of a TIFF stream at b[base:] (a CR2, or a JPEG's APP1 after 'Exif\\0\\0'): {tag: value}"""
    e = "<" if b[base:base + 2] == b"II" else ">"

    def ifd(o):
        if o <= 0 or base + o + 2 > len(b):
            return {}
        n = struct.unpack_from(e + "H", b, base + o)[0]
        r = {}
        for i in range(n):
            at = base + o + 2 + 12 * i
            if at + 12 > len(b):
                break
            t, ty, cnt, v = struct.unpack_from(e + "HHII", b, at)
            r[t] = (ty, cnt, v, at + 8)
        return r

    def val(ent):
        ty, cnt, v, at = ent
        if ty == 2:                                   # ASCII: inline up to 4 bytes
            s = b[at:at + cnt] if cnt <= 4 else b[base + v:base + v + cnt]
            return s.split(b"\0")[0].decode("latin-1").strip()
        if ty in (5, 10):
            nu, de = struct.unpack_from(e + ("II" if ty == 5 else "ii"), b, base + v)
            return nu / de if de else 0
        return struct.unpack_from(e + ("H" if ty == 3 else "I"), b, at)[0]

    if len(b) < base + 8:
        return {}
    i0 = ifd(struct.unpack_from(e + "I", b, base + 4)[0])
    out = {}
    for t in (0x010F, 0x0110, 0x0132):
        if t in i0:
            out[t] = val(i0[t])
    if 0x8769 in i0:
        x = ifd(i0[0x8769][2])
        for t in (0x829A, 0x829D, 0x8827, 0x9003, 0x920A, 0xA434):
            if t in x:
                try:
                    out[t] = val(x[t])
                except struct.error:
                    pass
    return out


def jpeg_tags(p):
    """the Exif of a JPEG (its APP1 only; the picture is never read)"""
    try:
        b = open(p, "rb").read(256 * 1024)
    except OSError:
        return {}
    i = 2
    while i + 4 < len(b) and b[i] == 0xFF:
        m, n = b[i + 1], struct.unpack_from(">H", b, i + 2)[0]
        if m == 0xE1 and b[i + 4:i + 10] == b"Exif\0\0":
            return tiff_tags(b, i + 10)
        if m == 0xDA:
            break
        i += 2 + n
    return {}


def cr2_tags(p):
    return tiff_tags(open(p, "rb").read(256 * 1024))


def cr2_ev8(p):
    """the shot's EV100 x 8 from a CR2's Exif IFD (ISO 0x8827, ExposureTime 0x829A, FNumber 0x829D) as the Darkroom
    takes it (src/wf_darkp.c wf_dk_ev8, which is within 0.1 EV of this); None when a tag is missing"""
    x = cr2_tags(p)
    iso, t, n = (x.get(k, 0) for k in (0x8827, 0x829A, 0x829D))
    if not (iso and t and n):
        return None
    return round(8 * (math.log2(n * n / t) - math.log2(iso / 100)))


def caption(cr2):
    """camera, lens, exposure, date: from the CR2, any missing tag from the camera's JPEG of the frame (Exif only)"""
    x = cr2_tags(cr2)
    j = jpeg_tags(os.path.splitext(cr2)[0] + ".JPG")
    g = lambda t: x.get(t) or j.get(t)
    t, n, iso, fl = g(0x829A), g(0x829D), g(0x8827), g(0x920A)
    exp = []
    if t:
        exp.append("1/%d s" % round(1 / t) if t < 0.5 else "%.1f s" % t)
    if n:
        exp.append("f/%g" % round(n, 1))
    if iso:
        exp.append("ISO %d" % iso)
    if fl:
        exp.append("%g mm" % round(fl, 1))
    return dict(camera=g(0x0110) or "", lens=g(0xA434) or "", exposure=" ".join(exp),
                date=g(0x9003) or g(0x0132) or "", exif_from="CR2" + (" + JPEG Exif" if any(
                    t not in x and t in j for t in (0x0110, 0xA434, 0x9003)) else ""))


def stem(f):
    return "%03d-%s" % (f["id"], "".join(ch for ch in f["name"] if ch.isalnum()))


def clean(s):
    return s.replace(" ", "").replace("/", "-")


def fx_tag(st):
    return "s%s_g%s_h%s_b%s_m%s_ga%s" % (STRENGTH[st["s"]].rstrip("%"), clean(GRAIN[st["g"]]), HALO[st["h"]], BLOOM[st["b"]],
                                         clean(MIST[st["m"]]), GAUGE[st["ga"]].split()[0])


def lists(mode, work, card, fx_film, frames):
    C = catalog()
    prints = tuple(getattr(C, "PRINTS", ("scan", "cine", "paper")))
    films = [f for f in C.FILMS if f["ready"] and not f["lut"] and f["group"] != "stage"]
    cans = [f for f in C.FILMS if f["ready"] and f["lut"]]
    chain = [f for f in C.FILMS if f["ready"] and f["group"] == "stage"]
    fr = []
    for a in frames:
        name, rest = a.split("=", 1)
        cr2, ev8, tags, xy = rest.split("@")
        x, y = (int(v) for v in xy.split(","))
        e = cr2_ev8(cr2) if ev8 in ("", "-") else int(ev8)
        if e is None:
            sys.exit("look_sweep: %s has no ISO / ExposureTime / FNumber: give its EV8" % cr2)
        fr.append(dict(name=name, cr2=cr2, ev8=e, tags=tags.split("+") if tags else [], crop=[x, y], caption=caption(cr2)))
    os.makedirs(work, exist_ok=True)
    meta = dict(mode=mode, frames=fr, films=[], jobs=[], develops=[], fx_film=fx_film, defaults=DEF, prints=list(prints))
    # the develops, full size: the sRGB base (the films, the neutral) and, films mode, each can through its own cube
    dl = ["F %s %s" % (x["name"], x["cr2"]) for x in fr]
    dl.append("D base srgb 0 0 - 1")
    src = {}
    for f in cans if mode == "films" else [f for f in cans if f["id"] in STRIP_CAT]:
        l = f["lut"]
        cf = os.path.join(card, C.flut_file(l))
        if not os.path.exists(cf):
            print("look_sweep: no cube %s for %s: left out" % (cf, f["name"]))
            continue
        disp = cf.endswith("SR.CUB")
        tag = stem(f)
        dl.append("D %s %s %d %d %s 1" % (tag, "disp" if disp else "cube", C.FLUT_CURVE[l], l, cf))
        meta["develops"].append(dict(tag=tag, film=f["id"], src="disp" if disp else "cube", curve=C.FLUT_CURVE[l],
                                     cube=os.path.basename(cf)))
        src[f["id"]] = (tag, "Darkroom develop through %s (%s)" % (os.path.basename(cf), "Leica display" if disp else
                                                                  "curve %d" % C.FLUT_CURVE[l]))
    open(os.path.join(work, "devset.txt"), "w").write("\n".join(dl) + "\n")
    if mode == "films":
        looks = films + [f for f in cans if f["id"] in src]
    else:
        looks = chain + [f for f in films if f["id"] == fx_film or f["id"] in STRIP_CAT] + \
                [f for f in cans if f["id"] in src]
    for f in looks:
        meta["films"].append(dict(id=f["id"], code=f["code"], name=f["name"], short=f["short"], group=f["group"],
                                  kind=f["kind"], prints=list(f["prints"]), lut=f["lut"], iso=f.get("iso"),
                                  kelvin=f.get("kelvin"), stage=f["group"] == "stage"))

    jobs = []
    only = [s for s in os.environ.get("LOOK_SETS", "").split(",") if s]
    def job(sset, level, x, fid, path="d", crop=True, **kw):
        if only and sset != "neutral" and sset not in only:
            return
        st = dict(DEF, **kw)
        if fid == 0:
            inp, source, name = "base", "Darkroom sRGB develop, no film", "000-neutral__%s__neutral" % x["name"]
        else:
            f = next(t for t in looks if t["id"] == fid)
            inp, source = src.get(fid, ("base", "Darkroom sRGB develop"))
            name = "%s__%s__%s_%s__%s" % (stem(f), x["name"], prints[st["pr"]], fx_tag(st), "dark" if path == "d" else "cam")
        out = "%s/out/%s.ppm" % (work, name)
        for j in jobs:                                              # the same shot in another set (bloom Normal = the film)
            if j["out"] == out:
                if [sset, level] not in j["sets"]:
                    j["sets"].append([sset, level])
                if crop and not j["crop"]:
                    j["crop"] = x["crop"]
                return
        jobs.append(dict(sets=[[sset, level]], frame=x["name"], film=fid, print=st["pr"], strength=st["s"], grain=st["g"],
                         halo=st["h"], bloom=st["b"], mist=st["m"], gauge=st["ga"], path=path, out=out,
                         crop=x["crop"] if crop else None, input="%s/dev/%s_%s.uyvy" % (work, x["name"], inp),
                         source="CR2 %s, %s" % (os.path.basename(x["cr2"]), source)))
    tag = lambda t, n=2: [x for x in fr if t in x["tags"]] or fr[:n]
    for x in fr:
        job("neutral", NEUTRAL, x, 0)
    if mode == "films":
        for f in looks:                                             # film by film: its effect sets are built once
            for x in fr:
                job("film", "default", x, f["id"])
        for x in tag("glow"):
            for b in range(4):
                job("bloom", BLOOM[b], x, fx_film, b=b)
            for m in range(9):
                job("mist", MIST[m], x, fx_film, m=m)
        agree = [x for x in fr if "glow" in x["tags"]][:1] + [x for x in fr if "sky" in x["tags"]][:1]
        for x in agree:                                             # the camera's stage against the Darkroom's (report only)
            for fid in (fx_film, films[0]["id"]):
                for p in ("c", "d"):
                    job("agree", p, x, fid, path=p)
    else:
        p2383 = prints.index("p01") if "p01" in prints else None
        for f in chain:                                             # negative by negative, print by print: one table at a time
            pis = [prints.index(p) for p in f["prints"]]
            dp = p2383 if p2383 in pis else min(pis)                # the camera's print at Print Scan (wf_print_eff:
            for pi in pis:                                          # WF_PRINT_CHAIN_DEF = 2383 when the film has it)
                for x in tag("matrix"):
                    job("matrix", PRINT_NAME[pi], x, f["id"], pr=pi, crop=False)
                if pi == dp:                                        # one level: a film without 2383 (Double-X) on its own
                    for x in fr:                                    # print, which its label names
                        job("negative", "camera default", x, f["id"], pr=dp, crop=False)
        for x in fr:                                                # the chain's contrast next to the catalogue's
            for fid in STRIP_CAT:
                if any(f["id"] == fid for f in looks):
                    job("contrast", STRIP_LEVEL[fid], x, fid, crop=False)
            for fid in STRIP_CHAIN:
                job("contrast", STRIP_LEVEL[0], x, fid, pr=p2383, crop=False)
        fxf = next(f for f in looks if f["id"] == fx_film)          # a chain film shows its effects through 2383
        fp = p2383 if fxf["group"] == "stage" and "p01" in fxf["prints"] else DEF["pr"]
        meta["fx_print"] = fp
        for x in tag("skin"):
            for g in range(len(GRAIN)):
                job("grain", GRAIN[g], x, fx_film, pr=fp, g=g)
        for x in tag("glow"):
            for h in range(len(HALO)):
                job("halation", HALO[h], x, fx_film, pr=fp, h=h)
            for b in range(len(BLOOM)):
                job("bloom", BLOOM[b], x, fx_film, pr=fp, b=b)
        for x in tag("gauge"):
            for ga in range(len(GAUGE)):
                job("gauge", GAUGE[ga], x, fx_film, pr=fp, ga=ga)
    meta["jobs"] = jobs
    fmap = {x["name"]: x for x in fr}
    done = lambda j: os.path.exists(j["out"]) and (not j["crop"] or os.path.exists(j["out"][:-4] + "_c.ppm"))
    todo = [j for j in jobs if not (os.environ.get("LOOK_RESUME") == "1" and done(j))]
    with open(os.path.join(work, "jobs.txt"), "w") as o:
        for j in todo:
            x = fmap[j["frame"]]
            o.write("%s %d %d %d %d %d %d %d %d %d %s %s%s\n" % (
                j["input"], x["ev8"], j["film"], j["print"], j["strength"], j["grain"], j["halo"], j["bloom"], j["mist"],
                j["gauge"], j["out"], j["path"], " %d,%d" % tuple(j["crop"]) if j["crop"] else ""))
    json.dump(meta, open(os.path.join(work, "meta.json"), "w"), indent=1)
    print("look_sweep %s: %d frames, %d looks, %d develops, %d look jobs (%d to render)%s" % (
        mode, len(fr), len(looks), len(dl) - len(fr), len(jobs), len(todo), ", sets " + ",".join(only) if only else ""))


# ------------------------------------------------------------------ gallery
def gallery(work, out):
    import numpy as np
    from PIL import Image, ImageDraw
    meta = json.load(open(os.path.join(work, "meta.json")))
    mode = meta.get("mode", "films")
    prints = meta.get("prints", ["scan", "cine", "paper"])
    fmeta = {f["id"]: f for f in meta["films"]}
    frames = meta["frames"]
    fxf = fmeta[meta["fx_film"]]
    D = meta["defaults"]
    os.makedirs(out, exist_ok=True)
    rows, stats = [], {}

    def save(src, d, name, crop=False):
        im = Image.open(src).convert("RGB")
        if not crop:
            im.thumbnail((1600, 1600), Image.LANCZOS)
        dd = os.path.join(out, d)
        os.makedirs(dd, exist_ok=True)
        p = os.path.join(dd, name + ".jpg")
        im.save(p, quality=90)
        a = np.asarray(im).astype(np.float32)
        y = a @ np.array([0.299, 0.587, 0.114], np.float32)
        q = np.percentile(y, [1, 5, 50, 95, 99])
        stats[os.path.relpath(p, out).replace(os.sep, "/")] = dict(
            p1=round(float(q[0]), 1), p5=round(float(q[1]), 1), p50=round(float(q[2]), 1), p95=round(float(q[3]), 1),
            p99=round(float(q[4]), 1), clip=round(float((a.max(axis=2) >= 254).mean() * 100), 2),
            crush=round(float((y <= 3).mean() * 100), 2), cb=round(float((a[..., 2] - y).mean() * 0.564), 2),
            cr=round(float((a[..., 0] - y).mean() * 0.713), 2))
        return os.path.relpath(p, out).replace(os.sep, "/"), im

    ims, txt, small = {}, {}, {}
    lp = os.path.join(work, "look_detail.log")
    for ln in open(lp, errors="replace") if os.path.exists(lp) else []:
        if ln.startswith("look ") and " | exif " in ln:             # the look text the camera writes in the print's comment
            txt[ln[5:].split(" | ", 1)[0]] = ln.rsplit(" | exif ", 1)[1].strip()
    for j in meta["jobs"]:
        sets = [s for s in j["sets"] if s[0] != "agree"]
        if not sets or not os.path.exists(j["out"]):
            continue
        name = os.path.basename(j["out"])[:-4]
        main = sets[0][0]
        f = fmeta.get(j["film"])
        neutral = j["film"] == 0
        stage = bool(f and f.get("stage"))
        for crop in (False, True):
            p = j["out"][:-4] + "_c.ppm" if crop else j["out"]
            if not os.path.exists(p):
                continue
            rel, im = save(p, ("crops/" if crop else "") + main, name + ("__1to1" if crop else ""), crop)
            if not crop and main in ("matrix", "negative"):
                small[rel] = np.asarray(im.resize((120, 80), Image.BOX)).astype(np.float32)
                im = im.copy()
                im.thumbnail((400, 400), Image.LANCZOS)               # strips only need 400 px
            ims[rel] = im
            for sset, level in sets:
                rows.append(dict(file=rel, set=sset, level=level, kind="1:1 crop" if crop else "full",
                                 frame=j["frame"], film_id=j["film"] or "", film=f["name"] if f else NEUTRAL,
                                 short=(f["short"] or f["name"]) if f else NEUTRAL, group=f["group"] if f else "",
                                 lut=f["lut"] if f else 0,
                                 negative_id=j["film"] if stage else "", negative=f["name"] if stage else "",
                                 print_id=prints[j["print"]] if not neutral else "",
                                 print="" if neutral else PRINT_NAME[j["print"]],
                                 print_full=PRINT_FULL[j["print"]] if not neutral else "",
                                 strength="" if neutral else STRENGTH[j["strength"]],
                                 grain="" if neutral else GRAIN[j["grain"]], halation="" if neutral else HALO[j["halo"]],
                                 bloom="" if neutral else BLOOM[j["bloom"]], mist="" if neutral else MIST[j["mist"]],
                                 gauge="" if neutral else GAUGE[j["gauge"]],
                                 variant="" if neutral or j["print"] < 3 else prints[j["print"]],
                                 crop=j["crop"] if crop else None, path="" if neutral else "Darkroom stage",
                                 source=j["source"], look_text=txt.get(j["out"], "")))

    def grid(items, cols, tw, title, dst):
        th = int(tw * 2 / 3)
        r = (len(items) + cols - 1) // cols
        g = Image.new("RGB", (cols * tw, r * (th + 18) + 22), (18, 18, 18))
        dr = ImageDraw.Draw(g)
        dr.text((6, 4), title, fill=(235, 235, 235))
        for i, (im, lab) in enumerate(items):
            if im is None:
                continue
            t = im.copy()
            t.thumbnail((tw, th), Image.LANCZOS)
            x0, y0 = (i % cols) * tw, 22 + (i // cols) * (th + 18)
            g.paste(t, (x0, y0))
            dr.text((x0 + 4, y0 + th + 3), lab, fill=(240, 210, 120))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        g.save(dst, quality=88)
        return os.path.relpath(dst, out).replace(os.sep, "/")

    by = {}
    for rr in rows:
        by.setdefault((rr["set"], rr["kind"], rr["frame"]), []).append(rr)
    strips = []
    fin = "Grain %s, Halation %s, Bloom %s, Gauge %s%s, 100%%" % (GRAIN[D["g"]], HALO[D["h"]], BLOOM[D["b"]], GAUGE[D["ga"]],
                                                                 ", Scan" if mode == "films" else "")
    fxp = meta.get("fx_print", D["pr"])
    fxlab = fxf["name"] + (", print %s" % PRINT_NAME[fxp] if fxp >= 3 else "")
    sets_lv = (("bloom", BLOOM), ("mist", MIST)) if mode == "films" else \
              (("grain", GRAIN), ("halation", HALO), ("bloom", BLOOM), ("gauge", GAUGE))
    for x in frames:
        n = x["name"]
        nt = [(ims[r["file"]], NEUTRAL) for r in by.get(("neutral", "full", n), [])]
        if mode == "films":
            for kind, tg, tw in (("full", "", 400), ("1:1 crop", "_crop", 480)):
                ntc = [(ims[r["file"]], NEUTRAL) for r in by.get(("neutral", kind, n), [])]
                fl = by.get(("film", kind, n), [])
                for grp, lab in (("films", "every film"), ("cans", "every Film LUT can")):
                    it = ntc + [(ims[r["file"]], "%s %s" % (r["film_id"], r["short"])) for r in fl if bool(r["lut"]) == (grp == "cans")]
                    if len(it) > 1:
                        strips.append(grid(it, 6, tw, "%s: %s from the CR2, its default finish (%s)%s" % (
                            n, lab, fin, ", 1:1 crop" if tg else ""), os.path.join(out, "strips", "%s%s__%s.jpg" % (grp, tg, n))))
        else:
            ng = by.get(("negative", "full", n), [])
            for lv in sorted({r["level"] for r in ng}, key=lambda s: (not s.startswith("camera"), s)):
                it = nt + [(ims[r["file"]], "%s %s%s" % (r["film_id"], r["short"], "" if r["print_id"] == "p01" else
                                                          ", " + r["print"])) for r in ng if r["level"] == lv]
                strips.append(grid(it, 8, 300, "%s: every film-stage negative from the CR2, print %s (2383; a film "
                                   "without it on the print named), default finish (%s)" % (n, lv, fin),
                                   os.path.join(out, "strips", "negatives__%s__%s.jpg" % (clean(lv), n))))
            order = STRIP_CAT + STRIP_CHAIN
            cs = sorted((r for r in by.get(("contrast", "full", n), []) if r["film_id"] in order),
                        key=lambda r: order.index(r["film_id"]))
            if cs:                                                  # Y p5 / p50 / p95 under each: the slope at a glance
                yl = lambda r: "  Y %.0f/%.0f/%.0f" % tuple(stats[r["file"]][k] for k in ("p5", "p50", "p95"))
                nf = by.get(("neutral", "full", n), [])
                lb = lambda r: "%s%s%s" % (r["short"], ", 2383" if r["print_id"] == "p01" else "", yl(r))
                it = [(ims[r["file"]], NEUTRAL + yl(r)) for r in nf] + [(ims[r["file"]], lb(r)) for r in cs]
                strips.append(grid(it, 6, 400, "%s: contrast from the CR2: neutral | catalogue film (Scan) | Film LUT "
                                   "can | chain films at 2383, default finish (%s); Y p5/p50/p95" % (n, fin),
                                   os.path.join(out, "strips", "contrast__%s.jpg" % n)))
            mx = by.get(("matrix", "full", n), [])
            for fid in sorted({r["film_id"] for r in mx}):
                rs = sorted((r for r in mx if r["film_id"] == fid), key=lambda r: prints.index(r["print_id"]))
                it = nt + [(ims[r["file"]], "%s  %s" % (r["print_id"], r["print_full"])) for r in rs]
                strips.append(grid(it, 6, 400, "%s: %s (%s) through every print stock, from the CR2, default finish" % (
                    n, fmeta[fid]["name"], fmeta[fid]["short"]), os.path.join(out, "strips", "contact__%s__%s.jpg" % (
                        stem(fmeta[fid]), n))))
        for kind, tg in (("full", ""), ("1:1 crop", "_crop")):
            for sset, labels in sets_lv:
                rs = sorted(by.get((sset, kind, n), []), key=lambda r: labels.index(r["level"]))
                if len(rs) > 1:
                    it = [(ims[r["file"]], "%s %s" % (sset.capitalize(), r["level"])) for r in rs]
                    strips.append(grid(it, 3 if len(it) in (3, 9) or len(it) > 4 else 4, 640 if tg else 560,
                                       "%s: %s levels from the CR2, film %s, the rest at the default (%s)%s" % (
                                           n, sset, fxlab, fin, ", 1:1 crop" if tg else ""),
                                       os.path.join(out, "strips", "%s%s__%s.jpg" % (sset, tg, n))))
    pairs = []
    if mode == "films":                                             # neutral RAW develop | the look (800 px halves)
        neu = {r["frame"]: ims[r["file"]] for r in rows if r["set"] == "neutral" and r["kind"] == "full"}
        for rr in rows:
            if rr["set"] != "film" or rr["kind"] != "full" or rr["frame"] not in neu:
                continue
            a, b = neu[rr["frame"]].copy(), ims[rr["file"]].copy()
            a.thumbnail((800, 800), Image.LANCZOS)
            b.thumbnail((800, 800), Image.LANCZOS)
            pr = Image.new("RGB", (a.width + b.width + 8, max(a.height, b.height) + 20), (18, 18, 18))
            pr.paste(a, (0, 0))
            pr.paste(b, (a.width + 8, 0))
            dr = ImageDraw.Draw(pr)
            dr.text((4, a.height + 4), NEUTRAL, fill=(235, 235, 235))
            dr.text((a.width + 12, b.height + 4), "%s %s (default finish)" % (rr["film_id"], rr["film"]), fill=(240, 210, 120))
            d = os.path.join(out, "pairs")
            os.makedirs(d, exist_ok=True)
            p = os.path.join(d, os.path.basename(rr["file"])[:-4] + "__pair.jpg")
            pr.save(p, quality=86)
            pairs.append(os.path.relpath(p, out).replace(os.sep, "/"))

    fr_out = [dict(name=x["name"], cr2=os.path.basename(x["cr2"]), ev=round(x["ev8"] / 8, 2), tags=x["tags"], crop=x["crop"],
                   **x["caption"]) for x in frames]
    sets = dict(neutral=[NEUTRAL])
    if mode == "films":
        sets.update(film=["default"], bloom=list(BLOOM), mist=list(MIST), grain=[], halation=[], gauge=[])
    else:
        sets.update(matrix=[PRINT_NAME[i] for i in range(3, len(PRINT_NAME))], negative=sorted({r["level"] for r in rows
                    if r["set"] == "negative"}), contrast=[STRIP_LEVEL[k] for k in STRIP_CAT + (0,)], grain=list(GRAIN),
                    halation=list(HALO), bloom=list(BLOOM), gauge=list(GAUGE))
    json.dump(dict(note="Every image from the CR2 through the Darkroom (develop + look stage); the camera's JPEGs give "
                        "Exif captions only. One row per image and set." + (
                        " Film-stage chain films: Genesis-derived tables (docs/DESIGN.md 1a), private." if mode == "stage"
                        else " variant and gauge wait for film-stage."),
                   mode=mode, defaults=dict(print=PRINT_NAME[D["pr"]], strength=STRENGTH[D["s"]], grain=GRAIN[D["g"]],
                                            halation=HALO[D["h"]], bloom=BLOOM[D["b"]], mist=MIST[D["m"]],
                                            gauge=GAUGE[D["ga"]]),
                   effect_film=dict(id=fxf["id"], name=fxf["name"], print=PRINT_NAME[fxp], print_id=prints[fxp]), frames=fr_out, films=meta["films"], sets=sets,
                   rows=rows, strips=strips, pairs=pairs, stats=stats),
              open(os.path.join(out, "index.json"), "w"), indent=1)
    with open(os.path.join(out, "index.csv"), "w", newline="") as o:
        w = csv.DictWriter(o, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    lines = []
    for x in frames:                                                # the looks nearest each other on each frame
        if mode == "films":
            rs = by.get(("film", "full", x["name"]), [])
            key = lambda r: r["film_id"]
            lab = lambda k: fmeta[k]["short"] or fmeta[k]["name"]
            a = {key(r): np.asarray(ims[r["file"]]).astype(np.int16) for r in rs}
            ids = sorted(a)
            near = sorted((float(np.abs(a[p] - a[q]).mean()), p, q) for i, p in enumerate(ids) for q in ids[i + 1:]
                          if a[p].shape == a[q].shape)
            lines.append("%s nearest: %s" % (x["name"], ", ".join("%s-%s %.1f" % (lab(p), lab(q), d) for d, p, q in near[:8])))
        else:                                                       # 120 x 80 thumbnails: the grain averages out
            cs = by.get(("neutral", "full", x["name"]), []) + by.get(("contrast", "full", x["name"]), [])
            if len(cs) > 1:
                cl = lambda r, s: "%s p5 %.0f p50 %.0f p95 %.0f clip %.1f%% crush %.1f%%" % (
                    r["short"] if r["film_id"] else "neutral", s["p5"], s["p50"], s["p95"], s["clip"], s["crush"])
                lines.append("%s contrast: %s" % (x["name"], "; ".join(cl(r, stats[r["file"]]) for r in cs)))
            rs = [r for r in by.get(("matrix", "full", x["name"]), []) if r["file"] in small]
            if len(rs) < 2:
                continue
            A = np.stack([small[r["file"]].ravel() for r in rs])
            sq = (A * A).sum(1)
            d2 = np.maximum(sq[:, None] + sq[None, :] - 2 * A @ A.T, 0) / A.shape[1]
            iu = np.triu_indices(len(rs), 1)
            dd = np.sqrt(d2[iu])
            order = np.argsort(dd)
            tw = lambda i: "%s/%s" % (rs[i]["short"], rs[i]["print"])
            lines.append("%s: %d combos; rms < 1.0: %d pairs, < 2.0: %d, < 4.0: %d; nearest: %s" % (
                x["name"], len(rs), int((dd < 1).sum()), int((dd < 2).sum()), int((dd < 4).sum()),
                ", ".join("%s~%s %.2f" % (tw(iu[0][k]), tw(iu[1][k]), dd[k]) for k in order[:12])))
    with open(os.path.join(out, "stats.txt"), "w") as o:
        o.write("\n".join(lines) + "\n\n")
        for k in sorted(stats):
            s = stats[k]
            o.write("%-100s Y p1 %5.1f p5 %5.1f p50 %5.1f p95 %5.1f p99 %5.1f clip %5.2f%% crush %5.2f%% cb %+5.2f cr %+5.2f\n" % (
                k, s["p1"], s["p5"], s["p50"], s["p95"], s["p99"], s["clip"], s["crush"], s["cb"], s["cr"]))
    ag = []
    for j in meta["jobs"]:                                          # the camera's stage against the Darkroom's
        if ["agree", "d"] in j["sets"] and os.path.exists(j["out"]):
            c = j["out"][:-10] + "__cam.ppm"
            if os.path.exists(c):
                A = np.asarray(Image.open(c).convert("RGB")).astype(np.float32)
                B = np.asarray(Image.open(j["out"]).convert("RGB")).astype(np.float32)
                k = 6
                bl = lambda z: z[:z.shape[0] // k * k, :z.shape[1] // k * k].reshape(z.shape[0] // k, k, z.shape[1] // k, k, 3).mean((1, 3))
                e, eb = np.abs(A - B), np.abs(bl(A) - bl(B))
                ag.append("%-80s mean |d| R G B %5.2f %5.2f %5.2f, 6 px blocks %5.2f %5.2f %5.2f, mean d %+5.2f" % (
                    os.path.basename(j["out"])[:-10], *e.reshape(-1, 3).mean(0), *eb.reshape(-1, 3).mean(0), float((B - A).mean())))
    if ag:
        open(os.path.join(work, "agree.txt"), "w").write("\n".join(ag) + "\n")
    print("look_sweep gallery (%s): %d rows, %d images, %d strips, %d pairs in %s" % (
        mode, len(rows), len(ims), len(strips), len(pairs), out))


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) >= 6 and a[0] == "lists" and a[1] in ("films", "stage"):
        lists(a[1], a[2], a[3], int(a[4]), a[5:])
    elif len(a) == 3 and a[0] == "gallery":
        gallery(a[1], a[2])
    else:
        sys.exit(__doc__)
