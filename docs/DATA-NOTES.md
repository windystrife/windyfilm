# WindyFilm data notes (data agent)

Work dir (WSL, root): `/root/ml/wf_data` (out/ = packs, scratch). Always `python3 -I -B`. Repo = `/mnt/f/MagicLatern/_windyfilm`.
Nothing here is committed by the data agent.

## Step 1: can pack  DONE (2026-11-14)
- `cans/gen_cans.py`: our drawing code (from the old gen_cans_real.py), palette part and pack code removed (no
  FILMCAN.DAT read anywhere). API: `films(argv)`, `render(f, w, h, bg)` -> (rgb8, opaque mask).
- `tools/mk_wfcans.py OUT.DAT [--colours N] [--sheet DIR]`: own median cut (112 colours default), OKLab nearest with
  ordered dither on flat areas, RLE per DESIGN 4.4 (runs never cross a row end), glow = mean of saturated pixels x 1.25.
  Edge pixels are pre-blended with the colour each size sits on: big (24,27,33), strip (21,23,28), selected (84,58,22).
- Run: `cd /root/ml/wf_data && python3 -I -B /mnt/f/MagicLatern/_windyfilm/tools/mk_wfcans.py out/WFCANS.DAT --sheet out/cans_sheet`
- Result: 52 cans, npal 112, 1,395,216 B; decoder self-test passes (exact vs the quantised source; colour error vs the
  RGB source mean 5.2 codes). Index entry order = ascending id; `id` = catalogue id, glow in the entry.
- Trap: the can set is `DEFAULT_CODES` in gen_cans.py and must equal the ready rows of films/catalog.py (asserted).

## Step 2: catalogue  DONE
- `films/catalog.py`: every old field kept; new per row `short`, `kind`, `fmt`, `group`, `prints` (SHORT dict + `derive()`),
  `cube`; line1/line2 rewritten for all 52 ready rows (own words); Film LUT cans: `prints = ()`, `fmt` V-Log.
  Tables now in `films/tables/<table>.T17` (4913 words), `_C` / `_P` variants; `t17_path(f, variant)`.
- `check()` uses PIL + Ubuntu Sans (WSL only), asserts short <= 11 chars / 66 px, lines <= 44 chars / 440 px, kind / fmt /
  group / prints, unique shorts, tables present with 19652 B. `pack` prints "id table type flags variant t17".
- Run: `cd /mnt/f/MagicLatern/_windyfilm/films && python3 -I -B catalog.py check` (WSL). Counts: colour 15, cine 4, bw 11, lut 22.
- Trap: the old author-font width check (FILMTTS / rbfw) is gone; name check = <= 24 chars only.

## Step 3: converter  DONE
- `tools/mk_wtab.py` (docstring = semantics): node = JPEG YUV code, BT.601 full range, offset-binary U/V (node 8 neutral,
  node 16 = 255), RGB clamped to 0..255 before the cube lookup, tetrahedral cube, output rounded and clamped. The kernel
  model (`kbuild`, `kwords`, `shoot`) is in the same file.
- Validated against the older tables (comparison only): all 4913 nodes within 1 code (mean 0.00-0.08) for all 30 films,
  out-of-gamut nodes included; photo pixels (/root/ml/loglut/photos.uyvy through the kernel model): mean <= 0.17, p99.9 <= 2,
  max <= 4 codes. B&W: old pack zeroed U/V too (compared that way).
- Run all: `luts/make_cubes.sh /root/ml/wf_data/cubes_flat` then
  `python3 -I -B tools/mk_wtab.py all /root/ml/wf_data/cubes_flat films/tables --old /root/ml/wf_src/main/filmroll_eosm_yuv/films --photos /root/ml/loglut/photos.uyvy`
- `fix_axis`: a neutral-axis dip of <= 2 codes (BLD100_P at the paper black) is lifted; nothing else changed.

## Step 4: sources  DONE
- All 30 looks regenerate from `luts/looks/<CODE>/gen_*.py` (own parametric models). 15 of them read digitised data-sheet CSV/JSON in
  `luts/looks/<CODE>/data/` (publication named in each header). Those data dirs were NOT in any repo checkout: recovered
  from an earlier session scratchpad (`...\3764712d-...\scratchpad\catalogue\looks\*\data`, generators md5-identical to
  the repo ones) and copied in. Comments of three generators were scrubbed of old-port words (gate deny list).
- Not used: preview_fuji_luts.py, yuvre harness, SUPERIA.CUB (a spare cube gen_fuji_luts also writes), PRT400B preset.

## Step 5: print variants  DONE
- `luts/film/src/mk_wfprints.py` (fl_core.py + "srgb" input = camera JPEG): `<CODE>_C.CUB` (negative -> Kodak 2383) and
  `<CODE>_P.CUB` (negative -> the paper in the negative's `target_print`; Crystal Archive II for the Fuji stills, Portra Endura
  for the Kodak stills; not for cine negatives). Needs spektrafilm profile JSON: `WF_SPEKTRAFILM=/mnt/f/MagicLatern/_port/luts_film/data/spektrafilm`
  (read only, nothing downloaded). Paper defaults in PAPER_DEF (D50 view, gain 1.12, flare 0.004).
- Films with variants: PRT160 PRT400 PRT800 SUN200 EVD400 BLD100 PAS400 TRV400 (scan/cine/paper), CIN50D CINE250D TUN800
  (scan/cine). Everything else: scan only. Tables and cubes derived from spektrafilm data are CC BY-SA 4.0 (attribute A. Volpato).
- Run: `bash luts/... ` = `python3 -I -B luts/film/src/mk_wfprints.py /root/ml/wf_data/prints`, then
  `python3 -I -B tools/mk_wtab.py cube prints/X_C.CUB films/tables/X_C.T17` for each.

## Step 6: film pack  DONE
- `python3 -I -B tools/mk_wfpack.py /root/ml/wf_data/out/WFFILMS.DAT`: 49 entries (30 films), 964,156 B, self-test passes.

## Step 7: judge v2  DONE
- `luts/judge/wf_judge.py /root/ml/wf_data/photos <out> --old <old films dir>`; photos = scene / sunset / card PNG (inputs of the
  earlier fuji session). Sheets in the side-session folder `windyfilm-data/`.
