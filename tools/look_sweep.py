#!/usr/bin/env python3
"""look_sweep.py - the look sweep's lists and gallery (WindyStrife). Every film and Film LUT can of films/catalog.py on
real RAW frames, through the Darkroom's own develop (tests/host/wf_dark_test.c devset) and the module's look stage
(tests/host/wf_test.c LOOK:sweep); tools/look_sweep.sh runs it. The owner's photos and everything made from them stay
outside the repository (WORK, OUT): never commit them.

  python3 -I -B tools/look_sweep.py lists WORK CARD FX_FILM FRAME...
      WORK/devset.txt (wf_dark_test devset), WORK/jobs.txt (LOOK:sweep), WORK/meta.json. CARD = a card's ML/DATA (the
      cans' cubes, read only); FX_FILM = the film id the effect strips use; FRAME = NAME=CR2@EV8@TAGS@X,Y with EV8 the
      shot's EV100 x 8 ('-' = from the CR2's Exif, as the Darkroom does), TAGS out of skin, glow, sky joined by '+'
      (which strips the frame is in) and X,Y the 1:1 crop centre on the 5184 x 3456 frame.
  python3 -I -B tools/look_sweep.py gallery WORK OUT [CAMERA_JPG_DIR]
      OUT/<kind>/*.jpg (1600 px long side, 1:1 crops as they are), OUT/strips/*.jpg, OUT/index.json, OUT/index.csv,
      OUT/stats.txt (per image: Y percentiles, clipped / crushed share, the mean chroma; the films nearest each other).
      CAMERA_JPG_DIR: the camera's own JPEGs of the frames (shown next to the Darkroom's can of the same look).
"""
import csv, importlib.util, json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 5184, 3456
GRAIN = ("Off", "Fine", "Normal", "Strong")
HALO = ("Off", "Film", "Spektra")
BLOOM = ("Off", "Low", "Normal", "Strong")
MIST = ("Off", "White 1/8", "White 1/4", "White 1/2", "White 1", "Black 1/8", "Black 1/4", "Black 1/2", "Black 1")
STRENGTH = ("30%", "100%", "125%")
PRINTS = ("scan", "cine", "paper")
COMBO = dict(g=3, h=2, b=2, m=6)                     # every effect at once: Grain Strong, Halation Spektra, Bloom Normal, Black Mist 1/4


def catalog():
    spec = importlib.util.spec_from_file_location("wf_catalog_src", os.path.join(ROOT, "films", "catalog.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def cr2_ev8(p):
    """the shot's EV100 x 8 from a CR2's Exif IFD (ISO 0x8827, ExposureTime 0x829A, FNumber 0x829D) as the Darkroom
    takes it (src/wf_darkp.c wf_dk_ev8, which is within 0.1 EV of this); None when a tag is missing"""
    import math, struct
    b = open(p, "rb").read(256 * 1024)
    e = "<" if b[:2] == b"II" else ">"

    def ifd(o):
        n = struct.unpack_from(e + "H", b, o)[0]
        return {t: (ty, v, o + 2 + 12 * i + 8) for i in range(n)
                for t, ty, _, v in [struct.unpack_from(e + "HHII", b, o + 2 + 12 * i)]}

    def val(ent):
        ty, v, at = ent
        if ty == 5:
            nu, de = struct.unpack_from(e + "II", b, v)
            return nu / de if de else 0
        return struct.unpack_from(e + ("H" if ty == 3 else "I"), b, at)[0]

    i0 = ifd(struct.unpack_from(e + "I", b, 4)[0]) if len(b) > 8 else {}
    if 0x8769 not in i0:
        return None
    x = ifd(i0[0x8769][1])
    iso, t, n = (val(x[k]) if k in x else 0 for k in (0x8827, 0x829A, 0x829D))
    if not (iso and t and n):
        return None
    return round(8 * (math.log2(n * n / t) - math.log2(iso / 100)))


def stem(f):
    return "%03d-%s" % (f["id"], "".join(ch for ch in f["name"] if ch.isalnum()))


def fx_tag(g, h, b, m, s=1):
    return "s%s_g%s_h%s_b%s_m%s" % (STRENGTH[s].rstrip("%"), GRAIN[g], HALO[h], BLOOM[b], MIST[m].replace(" ", "").replace("/", "-"))


def lists(work, card, fx_film, frames):
    C = catalog()
    films = [f for f in C.FILMS if f["ready"] and not f["lut"]]
    cans = [f for f in C.FILMS if f["ready"] and f["lut"]]
    fr = []
    for a in frames:
        name, rest = a.split("=", 1)
        cr2, ev8, tags, xy = rest.split("@")
        x, y = (int(v) for v in xy.split(","))
        e = cr2_ev8(cr2) if ev8 in ("", "-") else int(ev8)
        if e is None:
            sys.exit("look_sweep: %s has no ISO / ExposureTime / FNumber: give its EV8" % cr2)
        fr.append(dict(name=name, cr2=cr2, ev8=e, tags=tags.split("+") if tags else [], crop=[x, y]))
    os.makedirs(work, exist_ok=True)
    meta = dict(frames=fr, films=[], jobs=[], develops=[], fx_film=fx_film)
    for f in films + cans:
        meta["films"].append(dict(id=f["id"], code=f["code"], name=f["name"], short=f["short"], group=f["group"],
                                  kind=f["kind"], prints=list(f["prints"]), lut=f["lut"]))
    # the develops: the sRGB base (full size for the look stage, a third for the "before"), each can through its cube
    dl = ["F %s %s" % (x["name"], x["cr2"]) for x in fr]
    dl.append("D base srgb 0 0 - 1")
    dl.append("D before srgb 0 0 - 3")
    meta["develops"].append(dict(tag="before", film=None, src="srgb"))
    for f in cans:
        l = f["lut"]
        cf = os.path.join(card, C.flut_file(l))
        disp = cf.endswith("SR.CUB")
        if not os.path.exists(cf):
            print("look_sweep: no cube %s for %s" % (cf, f["name"]))
            continue
        tag = stem(f)
        dl.append("D %s %s %d %d %s 3" % (tag, "disp" if disp else "cube", C.FLUT_CURVE[l], l, cf))
        meta["develops"].append(dict(tag=tag, film=f["id"], src="disp" if disp else "cube", curve=C.FLUT_CURVE[l],
                                     cube=os.path.basename(cf)))
    open(os.path.join(work, "devset.txt"), "w").write("\n".join(dl) + "\n")

    jobs = []
    def job(kind, x, fid, pr=0, s=1, g=0, h=0, b=0, m=0, path="c", crop=False):
        f = next(t for t in films if t["id"] == fid)
        out = "%s/%s/%s__%s__%s_%s__%s" % (work, "out", stem(f), x["name"], PRINTS[pr], fx_tag(g, h, b, m, s),
                                           "dark" if path == "d" else "cam")
        for j in jobs:                                              # the same shot (e.g. an effect's Off = the film's Scan)
            if j["out"] == out + ".ppm":
                if kind != j["kind"] and kind not in j.setdefault("also", []):
                    j["also"].append(kind)                          # it also opens that kind's strip
                if crop and not j["crop"]:
                    j["crop"] = x["crop"]
                return
        jobs.append(dict(kind=kind, frame=x["name"], film=fid, print=pr, strength=s, grain=g, halo=h, bloom=b, mist=m,
                         path=path, out=out + ".ppm", crop=x["crop"] if crop else None))
    sk = [x for x in fr if "skin" in x["tags"]] or fr[:2]
    gl = [x for x in fr if "glow" in x["tags"]] or fr[:2]
    for x in fr:                                                    # every film, Scan, effects off, on every frame
        for f in films:
            job("film", x, f["id"])
    for x in sk[:2]:                                                # the other prints on two frames
        for f in films:
            for pr in range(1, 3):
                if PRINTS[pr] in f["prints"]:
                    job("print", x, f["id"], pr)
    for x in sk[:1]:                                                # strength on one frame
        for s in (0, 2):
            job("strength", x, fx_film, s=s)
    for x in sk:                                                    # grain on skin, 1:1 crops
        for g in range(4):
            job("grain", x, fx_film, g=g, crop=True)
    for x in gl:                                                    # halation, bloom, mist where there is light
        for h in range(3):
            job("halation", x, fx_film, h=h, crop=True)
        for b in range(4):
            job("bloom", x, fx_film, b=b, crop=True)
        for m in range(9):
            job("mist", x, fx_film, m=m, crop=True)
    for x in fr:                                                    # every effect at once on every frame
        job("combo", x, fx_film, crop=True, **COMBO)
    agree = [x for x in fr if "glow" in x["tags"]][:1] + [x for x in fr if "sky" in x["tags"]][:1]
    for x in agree:                                                 # the camera's stage against the Darkroom's
        for fid in (fx_film, films[0]["id"]):
            for fx in (dict(), COMBO):
                for p in ("c", "d"):
                    job("agree", x, fid, path=p, **fx)
    meta["jobs"] = jobs
    fmap = {x["name"]: x for x in fr}
    with open(os.path.join(work, "jobs.txt"), "w") as o:
        for j in jobs:
            x = fmap[j["frame"]]
            o.write("%s/%s_base.uyvy %d %d %d %d %d %d %d %d %s %s%s\n" % (
                work + "/dev", x["name"], x["ev8"], j["film"], j["print"], j["strength"], j["grain"], j["halo"], j["bloom"],
                j["mist"], j["out"], j["path"], " %d,%d" % tuple(j["crop"]) if j["crop"] else ""))
    json.dump(meta, open(os.path.join(work, "meta.json"), "w"), indent=1)
    print("look_sweep: %d frames, %d films, %d cans, %d develops, %d look jobs" % (len(fr), len(films), len(cans),
                                                                                    len(meta["develops"]), len(jobs)))


# ------------------------------------------------------------------ gallery
def gallery(work, out, camdir=None):
    import numpy as np
    from PIL import Image, ImageDraw
    meta = json.load(open(os.path.join(work, "meta.json")))
    fmeta = {f["id"]: f for f in meta["films"]}
    frames = meta["frames"]
    os.makedirs(out, exist_ok=True)
    rows, stats, also = [], {}, []

    def save(src, kind, name, crop=False):
        im = Image.open(src).convert("RGB")
        if not crop:
            im.thumbnail((1600, 1600), Image.LANCZOS)
        d = os.path.join(out, kind)
        os.makedirs(d, exist_ok=True)
        p = os.path.join(d, name + ".jpg")
        im.save(p, quality=90)
        a = np.asarray(im).astype(np.float32)
        y = a @ np.array([0.299, 0.587, 0.114], np.float32)
        cb = (a[..., 2] - y).mean() * 0.564
        cr = (a[..., 0] - y).mean() * 0.713
        q = np.percentile(y, [1, 5, 50, 95, 99])
        stats[os.path.relpath(p, out)] = dict(p1=round(float(q[0]), 1), p5=round(float(q[1]), 1), p50=round(float(q[2]), 1),
                                              p95=round(float(q[3]), 1), p99=round(float(q[4]), 1),
                                              clip=round(float((a.max(axis=2) >= 254).mean() * 100), 2),
                                              crush=round(float((y <= 3).mean() * 100), 2), cb=round(float(cb), 2), cr=round(float(cr), 2))
        return os.path.relpath(p, out), im

    def row(rel, frame, film, prt, s, g, h, b, m, path, source, kind):
        f = fmeta.get(film) if film else None
        rows.append(dict(file=rel, kind=kind, frame=frame, film_id=film or "", film=f["name"] if f else "(none)",
                         group=f["group"] if f else "", print=prt, strength=s, grain=g, halation=h, bloom=b, mist=m,
                         path=path, source=source))

    before = {}
    for x in frames:                                                # the "before": the sRGB develop, no look
        p = os.path.join(work, "dev", "%s_before.ppm" % x["name"])
        if os.path.exists(p):
            rel, im = save(p, "before", "%s__before" % x["name"])
            before[x["name"]] = im
            row(rel, x["name"], None, "", "", "", "", "", "", "Darkroom develop", "CR2 sRGB develop", "before")
    for d in meta["develops"]:                                      # the cans: the Darkroom's develop through the cube
        if not d["film"]:
            continue
        f = fmeta[d["film"]]
        for x in frames:
            p = os.path.join(work, "dev", "%s_%s.ppm" % (x["name"], d["tag"]))
            if os.path.exists(p):
                rel, _ = save(p, "cans", "%s__%s__lut" % (d["tag"], x["name"]))
                row(rel, x["name"], f["id"], "-", "100%", "Off", "Off", "Off", "Off", "Darkroom",
                    "CR2 %s develop, %s" % (d["src"], d["cube"]), "can")
    for j in meta["jobs"]:
        if not os.path.exists(j["out"]):
            continue
        name = os.path.basename(j["out"])[:-4]
        rel, _ = save(j["out"], j["kind"] + "s" if j["kind"] in ("film", "print") else "fx", name)
        path = "Darkroom stage" if j["path"] == "d" else "in-camera stage"
        args = (PRINTS[j["print"]], STRENGTH[j["strength"]], GRAIN[j["grain"]], HALO[j["halo"]], BLOOM[j["bloom"]], MIST[j["mist"]])
        row(rel, j["frame"], j["film"], *args, path, "CR2 sRGB develop", j["kind"])
        also += [(a, rows[-1]) for a in j.get("also", [])]
        cp = j["out"][:-4] + "_c.ppm"
        if j["crop"] and os.path.exists(cp):
            rel, _ = save(cp, "crops", name + "__1to1", crop=True)
            row(rel, j["frame"], j["film"], *args, path, "CR2 sRGB develop, 1:1 crop at %d,%d" % tuple(j["crop"]), j["kind"] + " crop")
            also += [(a + " crop", rows[-1]) for a in j.get("also", [])]
    if camdir:                                                      # the camera's own JPEG (its look is in the EXIF)
        for x in frames:
            p = os.path.join(camdir, x["name"] + ".JPG")
            if os.path.exists(p):
                rel, _ = save(p, "camera", "%s__camera" % x["name"])
                row(rel, x["name"], None, "", "", "", "", "", "", "camera JPEG", "Canon JPEG + WindyFilm in camera (EXIF)", "camera")

    # strips: the before + every film on a frame (contact grids), the effect strips, the before / after pairs
    def grid(items, cols, tw, title, dst):
        from PIL import ImageFont
        th = int(tw * 2 / 3)
        n = len(items)
        r = (n + cols - 1) // cols
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
        return os.path.relpath(dst, out)

    strips = []
    by = {}
    for rr in rows:
        by.setdefault((rr["kind"], rr["frame"]), []).append(rr)
    for k, rr in also:                                              # an effect's Off leads its strip
        by.setdefault((k, rr["frame"]), []).insert(0, rr)
    for x in frames:
        it = []
        if x["name"] in before:
            it.append((before[x["name"]], "before (sRGB develop)"))
        for rr in by.get(("film", x["name"]), []):
            it.append((Image.open(os.path.join(out, rr["file"])), "%s %s" % (rr["film_id"], fmeta[rr["film_id"]]["short"] or rr["film"])))
        if it:
            strips.append(grid(it, 6, 400, "%s: every film, Scan, effects off (in-camera stage on the Darkroom's sRGB develop)" % x["name"],
                               os.path.join(out, "strips", "films__%s.jpg" % x["name"])))
        it = [(before[x["name"]], "before")] if x["name"] in before else []
        for rr in by.get(("can", x["name"]), []):
            it.append((Image.open(os.path.join(out, rr["file"])), "%s %s" % (rr["film_id"], rr["film"])))
        if len(it) > 1:
            strips.append(grid(it, 6, 400, "%s: every Film LUT can (the Darkroom's develop through its cube)" % x["name"],
                               os.path.join(out, "strips", "cans__%s.jpg" % x["name"])))
        pr = by.get(("print", x["name"]), [])
        if pr:                                                      # one row per film: Scan, Paper, Cine (blank: none)
            scan = {rr["film_id"]: rr for rr in by.get(("film", x["name"]), [])}
            it = []
            for fid in sorted({rr["film_id"] for rr in pr}):
                for p in ("scan", "paper", "cine"):
                    rr = scan.get(fid) if p == "scan" else next((r for r in pr if r["film_id"] == fid and r["print"] == p), None)
                    it.append((Image.open(os.path.join(out, rr["file"])) if rr else None,
                               "%s %s %s" % (fid, fmeta[fid]["short"] or fmeta[fid]["name"], p) if rr else ""))
            strips.append(grid(it, 6, 400, "%s: each print of the films that have prints (Scan, Paper, Cine)" % x["name"],
                               os.path.join(out, "strips", "prints__%s.jpg" % x["name"])))
        for kind, key, labels in (("grain", "grain", GRAIN), ("halation", "halation", HALO), ("bloom", "bloom", BLOOM),
                                  ("mist", "mist", MIST)):
            for crop in ("", " crop"):
                rs = by.get((kind + crop, x["name"]), [])
                if len(rs) > 1:
                    it = [(Image.open(os.path.join(out, rr["file"])), "%s %s" % (kind.capitalize(), rr[key])) for rr in rs]
                    strips.append(grid(it, 3 if kind == "mist" else len(it), 640 if crop else 560,
                                       "%s: %s levels, film %s%s" % (x["name"], kind, fmeta[meta["fx_film"]]["name"],
                                                                      ", 1:1 crop" if crop else ""),
                                       os.path.join(out, "strips", "%s%s__%s.jpg" % (kind, "_crop" if crop else "", x["name"]))))
    # pairs: before | look, every film and every can on every frame
    pairs = []
    for rr in rows:
        if rr["kind"] in ("film", "can") and rr["frame"] in before:
            a = before[rr["frame"]]
            b = Image.open(os.path.join(out, rr["file"]))
            pr = Image.new("RGB", (a.width + b.width + 8, max(a.height, b.height)), (18, 18, 18))
            pr.paste(a, (0, 0))
            pr.paste(b, (a.width + 8, 0))
            d = os.path.join(out, "pairs")
            os.makedirs(d, exist_ok=True)
            p = os.path.join(d, os.path.basename(rr["file"])[:-4] + "__pair.jpg")
            pr.save(p, quality=86)
            pairs.append(os.path.relpath(p, out))

    json.dump(dict(rows=rows, strips=strips, pairs=len(pairs), stats=stats), open(os.path.join(out, "index.json"), "w"), indent=1)
    with open(os.path.join(out, "index.csv"), "w", newline="") as o:
        w = csv.DictWriter(o, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    # the films nearest each other on each frame (mean |RGB| difference of the 1600 px images)
    lines = []
    for x in frames:
        rs = by.get(("film", x["name"]), []) + by.get(("can", x["name"]), [])
        ims = {rr["film_id"]: np.asarray(Image.open(os.path.join(out, rr["file"])).convert("RGB")).astype(np.int16) for rr in rs}
        ids = sorted(ims)
        near = []
        for i, a in enumerate(ids):
            for b in ids[i + 1:]:
                if ims[a].shape == ims[b].shape:
                    near.append((float(np.abs(ims[a] - ims[b]).mean()), a, b))
        near.sort()
        lines.append("%s nearest: %s" % (x["name"], ", ".join("%s-%s %.1f" % (fmeta[a]["short"] or fmeta[a]["name"],
                                                                                fmeta[b]["short"] or fmeta[b]["name"], d)
                                                             for d, a, b in near[:8])))
    with open(os.path.join(out, "stats.txt"), "w") as o:
        o.write("\n".join(lines) + "\n\n")
        for k in sorted(stats):
            s = stats[k]
            o.write("%-90s Y p1 %5.1f p5 %5.1f p50 %5.1f p95 %5.1f p99 %5.1f clip %5.2f%% crush %5.2f%% cb %+5.2f cr %+5.2f\n" % (
                k, s["p1"], s["p5"], s["p50"], s["p95"], s["p99"], s["clip"], s["crush"], s["cb"], s["cr"]))
    print("look_sweep gallery: %d images, %d strips, %d pairs in %s" % (len(rows), len(strips), len(pairs), out))


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) >= 5 and a[0] == "lists":
        lists(a[1], a[2], int(a[3]), a[4:])
    elif len(a) >= 3 and a[0] == "gallery":
        gallery(a[1], a[2], a[3] if len(a) > 3 else None)
    else:
        sys.exit(__doc__)
