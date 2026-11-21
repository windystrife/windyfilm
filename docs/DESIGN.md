# WindyFilm: design (Phase 2, 2026-11-14)

WindyFilm (`windyflm.mo`) is a standalone Magic Lantern module for the Canon EOS M 2.0.2 and the 5D Mark II 2.1.2.
It gives each JPEG a film look at the shot. It replaces the Film Roll port, which was WindyStrife code patched into the
binary-only `filmroll.mo` by florinkiritescu.ro. **WindyFilm holds WindyStrife's own work only.** Credit:
WindyStrife. Third-party data is attributed in `ATTRIBUTIONS.md`.

The phase 1 audit (OWN / GLUE / DEP / DER for every file of the port) is in the side-session notes,
`NEW-MODULE-PHASE1.md`. This file is the contract the parts are built against.

## 1. Provenance rules (hard)

- Reference source, read only: `/root/ml/wf_src/main` = port repo at d802f03 (main, V14T);
  `/root/ml/wf_src/5d2` = branch 5d2 at 359e278 (`filmroll_eosm_yuv/` only).
- **Port only OWN files and the OWN parts of GLUE files** (list in section 9). Rename the `fr_` prefix to `wf_`. `yh_`
  (halation) and `yg_` (grain) are ours and keep their prefix.
- **Never take** anything from:
  - the author's module (`filmroll.mo`), `FILMCAN.DAT` or the FILM*.RBF fonts;
  - the DER files: `fr_kern.c`, `fr_opt110.S`, `fr_authprev.*`, `proto/speed/*`, `fr_5d2.c`;
  - anything Genesis-derived: `fr_glook*`, `fr_gx*`, `mk_print_lut.py` on main / v11-glook, `HANDOFF-GENESIS-HALATION.md`.
- **Data.**
  - The old `films/*.T17` were produced by the author's loader, so they are not reused: every table is regenerated
    by our own converter (`tools/mk_wtab.py`). The old tables may be read only to compare.
  - The old can packs used the author's palette: new palette, new pack.
- `filmroll.mo` (`/mnt/f/MagicLatern/6D/ML/modules/filmroll.mo`) may be read **only** by the provenance gate, as a
  deny-list input. It is never linked, patched or called.
- spektrafilm: its code is GPLv3, so take ideas only. Its profile data is CC BY-SA 4.0 (A. Volpato), so attribute it.
- Never commit: `*.mo`, `*.DAT`, `*.sym`, ROMs or firmware, the owner's cubes (CinePrint, Leica, V-Log grades, EKTA P
  export), `.FRT` caches.

### 1a. Declared exception on branch `film-stage` (owner, 14 Nov 2026)

**This branch is NOT clean-room. Do not hand it over as if it were, and never merge it into `main` without this
paragraph staying at the top of the merge.**

**Published 21 Nov 2026 (RAWB28).** The owner states that permission was obtained from Cullen Kelly Color for what is
derived from Genesis Pro and made the repo and the RAWB28 release public ("Có phép cả Genesis, public", 20 Nov). The
Genesis-derived parts are listed in `ATTRIBUTIONS.md` ("Derived from Genesis Pro"); the generated tables and
`WFSTAGE.DAT` stay card data, never committed.

Branch `film-stage` carries film tables whose numbers are derived from **Genesis Pro 1.6.6** (Cullen Kelly Color, a
commercial OFX film-emulation plugin, unrelated to the filmroll.mo author): the negative-stock DLogE (121 points),
`StMtoPD` 3x4, the interlayer polynomial `IIE` 3x10, `Print_DLogE` 101, the `DyeAmountLUT` (16 print stocks, 65^3
each, subsampled to 33^3) and the `Lin_to_LogFN` convention. The owner accepted the legal risk for his private use on
14 Nov 2026, the same way he accepted it for the Film Roll port.

What this means in practice:

- The **module code stays ours**: the Genesis chain runs only in a host-side generator (`tools/mk_genesis_films.py`),
  which samples it into the 17^3 table a WindyFilm film already is. Nothing of the plugin's is linked, patched or
  called at runtime, and the module's size does not change.
- The **generated tables are card data** (`ML/DATA`), never committed; they are Genesis-derived all the same, so a
  card made from this branch carries no clean-room claim either.
- `tools/provenance_gate.py` keeps its deny list for `main` and `raw-develop`. On `film-stage` only, the
  Genesis file names are allowed and the gate prints `gate: this tree is NOT clean-room (Genesis-derived tables)` on
  every build, so a green gate can never be mistaken for a clean tree again.
- **`print-frame` carries the same exception** from its merge of `film-stage` (15 Nov 2026, the owner asked for the
  merge). The gate treats it like `film-stage`. Its print finishes (DESIGN 3.9) were written clean-room up to 47f9597,
  the commit before the merge: take that commit, not the merge, where a clean tree is needed.
- **`darkroom` carries the same exception** from its merge of `print-frame` (with `film-stage` 4f92b21; 15 Nov 2026,
  the owner: "merge fillm stage đi bạn"). The gate treats it like `film-stage`. Its own work was clean-room up to
  fe4190e, the commit before the merge. Its look sweep of the chain films (`tools/look_sweep.sh stage`) shows
  Genesis-derived tables on the owner's photos: private, never on a public page.

What this branch adds on top (14 Nov 2026, same day):

- **Four films** in the catalogue, ids 85..88: `GX500T` Vision3 500T 2393, `GX250D` Vision3 250D 2383, `GXP400` Portra
  400 2383, `GXE100` Ektar 100 3513 (a negative stock and a print stock of the chain per film). They are plain
  WindyFilm films: one 17^3 table each, grain and halation from their rows, no code of the chain in the module.
- **Placement**: the chain ends on the print's dye density, so its own black is the print's base fog (~0.03) and its
  white whatever that print reaches (0.43..0.60 for these four as measured on 14 Nov 2026, 0.45 for every pair
  in the 15 Nov tables). Stretching the *input* to fill the display crushed
  everything below code 0.35 into the toe (measured), so the two ends are pinned on the **output** instead - base fog
  to 0, print white to 1, i.e. printer points - and the exposure offset is fitted so the JPEG's 18 % grey code
  (sRGB 0.461, code 118) comes out on itself (`tools/mk_genesis_place.h`). What is left in between is the film's own
  curve. Until 15 Nov 2026 the target was 0.18 itself, a linear value used as a
  code: grey came out near code 46, two stops under the catalogue films (owner's sweep); `tools/check_chain_grey.py`
  (test.sh GREY) now holds every chain table within half a stop of the catalogue films' grey.
- **One module.** The films are part of `windyflm.mo` like every other film: no second build, no second module, no
  separate settings file (owner 14 Nov 2026: "code tiếp từ windyfilm, rồi dùng đường RAW của windyfilm"). The tables
  ride on the WindyFilm the camera already has.
- The four tables are **not committed** (bullet above): `build.sh` calls `tools/mk_genesis_films.py stage
  films/tables` when they are missing, and `mk_wfpack.py` leaves a film off the card when its table is not there. A
  tree that never had the port tree builds the other 53 films and says so.

**What branch `grain-halation` adds on top (15 Nov 2026, cut from `film-stage`) — the plugin's numbers in the CODE.**
The owner asked for the plugin's grain and halation in the module as two named stages, **WindyGrain** and
**WindyHalation** (the browser rows that used to read `Grain` and `Halation`; since 11-15 they read `Grain` and
`Halation` again, next to a `Bloom` and a `Gauge` row, and the stage names live on in this section and the code
comments). This is a bigger step than the four
tables: the *module* now carries constants of the plugin, not only sampled card data.

- **WindyGrain** (`src/yg_grain.c`, `yg_gen_*`): the plugin's three 64-point amplitude tables (`grainTable_r/g/b`,
  `CreateFilmGrain` at `kernel_source.cl:2745`, dumped in `genesis_re/halation_performance_grain_head.cl.txt`),
  converted once to Q8 display levels and kept as `int16_t[64]`; its `prng(x, y, seed, channel)`, its straight line
  into the tail for the lowest 1/64 of the rank, its `saturation` mix, its `amplitude`, and the bilinear upsample of a
  grain buffer smaller than the frame (`ApplyFilmGrain`:2804) as the gauge / size mapping. The code that reads them is
  ours (integer, no divide, no float per pixel); the amplitude does not depend on the level, as in the plugin.
- **WindyHalation** (`src/yh_halo.c`, `yh_gen_*`): the light wrap of `LightWrapHalationStepOne` /
  `LightWrapStepTwo` (`halation_step1_step2.cl.txt`, `kernel_source.cl:1723 / :2340`): the arctan highlight mask with
  its 0.94 threshold, the difference of blur, the per-channel arctan gate `1 - clamp(0.491 - 0.507*atan(...))`, the
  radius `max(1, maxDim * light_wrap_radius / (2048 * gauge))`, and `halation_strength` as the final scale. The blur
  is two box passes on the 1/8 grid the module's own halation already uses (a Gaussian, as the plugin's Performance
  kernel). Two domain differences are declared where the code is: our frame is display-referred, so the arctan's own
  `0.58..0.95` window is laid onto codes 240..255, and the delta is added in code units rather than in LogC3.
- **What is NOT taken**: no shader text, no plugin binary, no `.ofx`; nothing is linked, patched or called. The
  `light_wrap_color` default is the one constant we could not read exactly (0.40 / 0.50 / 0.45, the third a GUESS
  from the hand-off note).
- **The plugin's stages are options, not replacements.** `Fine / Normal / Strong` (grain) and `Wide / Tight`
  (halation; the bloom has its own `Bloom` row since 11-15, it was `Wide + Glow` / `Tight + Glow`) are unchanged in
  what they do; `Even Fine / Even Normal / Even Strong` (grain) and `Wrap` (halation) are appended to the two rows; the
  labels say what they do and name no plugin or other project since 11-15, this section and the code comments keep
  the source. The `Gauge` row (16 / 35 / 65 mm, 11-15) scales the Yedlin grain's pitch, the plugin grain's own gauge
  and the reach of both halos; at `35 mm` (the default) every other film, table, stage and setting behaves exactly as
  before (the `FXAB` host test measures both).
- `tools/provenance_gate.py` allows this branch the same exception as `film-stage` (`NOT_CLEAN` lists both) and still
  prints `gate: this tree is NOT clean-room` on every build. **Do not publish this branch, do not merge it into
  `main` without this section, and never describe a card made from it as clean-room.**

**The chain's own knobs on the camera (15 Nov 2026, `film-stage`) — two-stage tables.** The owner asked for the
chain's settings as rows: `Film exp` (+-2 stops in thirds), `Push / pull` (+-1 stop in thirds), `Exp comp`,
`Printer R / G / B` (+-12 printer points, 0.025 logE each) and `Interlayer` (0..200 % in 25 % steps). A single 17^3
table per pair cannot move with them, so the generator also writes the chain **in two halves** as card data
(`ML/DATA/WFSTAGE.DAT`, `tools/mk_wfstage.py` + `tools/mk_wfstage.c`, layout in `src/wf_stagek.h`):

- **negative half** per negative stock: display code -> exposure axis (`X`), axis -> density per channel (`A1`), and a
  33^3 grid density -> printing density at interlayer 0 plus its interlayer part (`A2`); **print half** per print
  stock: printing density -> print density per channel (`B1`) and a 33^3 grid print density -> the print's raw output
  (`B2`); **per pair** the fitted exposure and the two pinned ends of the shipped table, and the FNV-1a of that very
  `.T17` (the module takes an entry only when `WFFILMS.DAT` holds the same table, so the knobs never compose against
  another chain build).
- The halves are **measurements of the chain**, not its formulas: `tools/mk_wfstage.c` links the chain in the port
  tree on the PC (as `mk_genesis_cube.c` does) and drives it as a black box (exposure sweeps, bisection, printer
  offsets), reading the values its own trace prints through a host-only `fprintf` hook (`tools/mk_wfstage_hook.h`,
  `-DGL_FILM_TRACE`; the chain source is not edited). Nothing of the plugin is linked, patched or called on the camera.
- **Module**: `src/wf_stagek.c` composes a 17^3 table from the two halves with generic 1D / tetrahedral interpolation
  (integer, armv5t, no division) in two passes, so one half is in memory at a time (435 KB, then 217 KB, plus 29 KB;
  `WF_MEM_TMP`, freed at once). It runs in `wf_task` when a knob or the film changes, never per pixel: the per-pixel
  kernel, its 17^3 table and its cost stay as they are. At default knobs the shipped table is used, not a composed one.
  The module grows by the compose code and the index reader (image 502 KB of the 512 KB target, EOS M).
- **Accuracy** (against the chain run directly at the 4913 nodes, 7 pairs x 12 knob sets): mean 0.25..0.6 codes, max
  Y 1..8, V up to 13 on saturated nodes at the gamut edge. Default composition against the shipped tables, all 699
  pairs: mean of means 0.29, worst mean 0.63, worst max 14.
- `Push / pull` in this chain is only a shift of the exposure axis (the same scale as `Film exp`), and `Exp comp`
  cancels both (the chain's `exposure = -push`), keeping the fitted placement; the help lines say so.
- **The shipped matrix tables were stale** (found 15 Nov while checking the composition): 503 of 704 `G<nn>P<pp>.T17`
  differed from the current chain by more than 1 code on average (worst 56), baked before the chain's 14 Nov change
  (clamp after `StMtoPD`). All 736 matrix tables and the four stage films were regenerated from the current chain; the
  looks of those pairs move to the current chain.
- `WFSTAGE.DAT` (22.6 MB, 44 negative and 16 print halves) is card data like the tables: Genesis-derived, never
  committed; without it the seven rows stay greyed and every film behaves as before.

**The RAW develop's log family (option (c), 15 Nov 2026, generator on `film-stage`, module work on `chain-log`).**

- **What it is:** a second derived family, one table per offered pair (643). `tools/mk_genesis_films.py log` writes
  `films/tables/log/<GnnPpp>.L33`: 33^3 nodes, R'G'B' uint16 each, 215,622 B; card data, never committed.
- **How it is sampled:** `tools/mk_genesis_cube.c` DOMAIN `slog3:<Q20 matrix>` samples the chain on the Darkroom's
  WF_DEV_CUBE grid for curve 1, S-Log3 / S-Gamut3.Cine, the chain's own input curve (`gl_log_to_lin(GL_SLOG3)`).
  Path: S-Log3 code -> S-Gamut3.Cine linear -> the inverse of the develop's own Rec.709 -> S-Gamut3.Cine matrix
  (wf_ll_mat[0], from `tools/mk_loglut_tab.py`) -> the chain. The develop's cube input is a Log + LUT can's, so on a RAW
  shot the develop's film shoulder (job.roll) folds the sensor's stops above the print's white into it.
- **Why Rec.709 goes into the chain, not the camera gamut:** the plugin's chain is S-Log3 -> linear -> XYZ ->
  XYZ_to_Neg; the port drops both matrices (fr_glook_film.c), so its chain takes whatever primaries it is given.
  Every 8-bit chain table is made from Rec.709. Fed S-Gamut3.Cine as it is (LOG_GAMUT=native), colours come out
  paler and a blue sky under Eterna / 3523XD turns pink (owner's IMG_6849, 15 Nov 2026). LogC3 (curve 3, AWG3) and
  V-Log (curve 2) domains exist for tests (LOG_CURVE); the family on the card is one curve, named in `domain.txt`.
- **Stored once:** the chain's dye tables are byte-identical for five print pairs. This is in the source dump
  `fr_glook_print.bin` (chain prints {0,1,2}, {3,8}, {4,6}, {9,10}, {11,12}); `check_print_slots.py` pins it. So
  643 offered pairs hold 428 distinct tables. The packs store each distinct table once (WFSTAGE halves, WFFILMS
  entries, the log pack) and point every alias at it. The menu keeps its 15 print names.
- **Placement:** the same as the 8-bit family. Scene-linear 0.18 lands on the 8-bit table's grey code
  (`check_chain_grey.py`), with a 1/400-stop scan.
- **What does not change:** the 8-bit tables, the 8-bit path and the JPEG path.


## 2. Repo layout and who owns what

During Phase 2, agents write only inside their own paths and never commit. The lead session commits explicit paths.

| path | owner | what |
|---|---|---|
| `src/wf_api.h` | lead | frozen interface (section 4) |
| `src/*.c`, `src/*.h`, `src/*.S` | core | module, camera layer, worker, kernels, effects, menu, config, EXIF, preview, log |
| `src/ui/*` | ui | browser (B2 small text), can pack loader, own font renderer, icons |
| `films/catalog.py`, `films/tables/*.T17` | data | catalogue rows, regenerated tables (scan / cine / paper variants) |
| `tools/mk_wtab.py`, `tools/mk_wfpack.py`, `tools/mk_wfcans.py`, `cans/*`, `luts/*` | data | cube -> table converter, film pack, can pack, film cube model |
| `tools/mk_wffont.py` | ui | Ubuntu Sans -> WFFONTS.DAT (UI), WFPRINT.DAT (Darkroom print caption, EOS M) |
| `tools/mk_catalog_c.py`, `tools/mk_*tab.py` (log tables), `tools/provenance_gate.py`, `build.sh`, `test.sh`, `tests/host/*` | core | C catalogue, generated tables, build, host tests, gate |
| `tests/ui/*` | ui | UI host test (PPM dumps) |
| `README*.md`, `ATTRIBUTIONS.md`, `LICENSE`, `docs/*` | lead | docs (the agent hand-off files stay local, `.gitignore`) |

WSL work dirs (one each, never shared): core `/root/ml/wf_core`, ui `/root/ml/wf_uidev`, data `/root/ml/wf_data`.
`/root/ml/wf_ui` holds the mockup inputs.

## 3. Architecture

```
module_init ─ wf_cam_init (ROM word checks) ─ wf_cfg_load ─ menu ─ wf_task (own task, prio 0x1C)
   CBRs: CBR_KEYPRESS (browser + open key), CBR_DISPLAY_FILTER (LiveView preview), CBR_SHOOT_TASK if needed
wf_task loop (~50 ms): roll (first) ─ a darkroom run when asked ─ pack reader, Log + LUT cube loader, effects, preview
      tables, EXIF writer, config save, log flush
shot: Canon YUV conversion site ─ hook (patch_hook_function) ─ gate ─ semaphore ─ worker task film_yuv (prio 0x1C)
      worker: chunks of 288 words into cached scratch ─ kernel (film / log / Log + LUT) ─ effects (grain, halation, mist)
```

- **Camera layer** (`wf_cam_eosm.c`, `wf_cam_5d2.c`, one per build):
  - EOS M: NN `ff169414` (r1, 5184x3456, full frame) and Diana TOP `ff17036c` (r8) / BOT `ff16b580` (r6), halves of
    5184x1728.
  - 5D2 Path A: TOP `0xFF888884` / BOT `0xFF8867A0`, 5616x1872 halves, with the 12-word ROM check (from `fr_5d2_hook.c`,
    which is OWN).
  - If the ROM words differ: status "Unsupported firmware" and no hook.
  - **No post-shot develop fallback**: that path was the author's code.
  - If `patch_hook_function` fails because the address is already patched: status "Disable filmroll.mo", logged,
    and no hook.
- **Frame data.** Canon UYVY {U s8, Y0, V s8, Y1}, chroma signed. No LDRD / STRD (the trampoline leaves sp at 4 mod 8).
  `-march=armv5t`. No `/` or `%` on 64-bit values in module code (the `__aeabi_ldivmod` trap).
- **Outputs.**
  - Film: the active catalogue film, through the 17^3 table and the 2.5D kernel.
  - Log: S-Log3 / V-Log / LogC3 / Canon Log 2 / Canon Log 3, flat.
  - Log + LUT: the same 5 curves plus a card cube.
  - Off.
  - Loading a Film LUT can sets Log + LUT with the can's cube on that cube's own curve (`WF_FLUT_CURVE`): V-Log, the
    twelve CinePrint looks Canon Log 2 since 11-14 (3.5).
- **Effects:** grain (`yg_*`), halation and bloom (`yh_*`: one stage, two rows and settings since RAWB11), Mist (`wf_mist`;
  on the 5D2 in halves, the TOP's map kept for its BOT; checked 11-15, section 3.8), Strength 30 / 100 / 125,
  Highlights / Shadows -5..+5 (section 3.3).
- **Light** (new row): Auto = the camera's WB, untouched. Day = the stock's `kelvin`. Cloudy = kelvin x 1.25, rounded to
  100 K. Tungsten = 3200 K.
  - Set through `lens_set_kelvin`, at load and when the row changes.
  - The user's WB (mode, kelvin, shift) is saved before the first change and restored on Auto, on unload and at
    module exit.
- **Print** (new row): Scan / Cine print / Paper selects the table variant 0 / 1 / 2 of the active film in WFFILMS.DAT.
  - A variant that does not exist is skipped by the row step. `wf_row_avail` is 0 when only Scan exists.
  - Film LUT cans: Print is not available.
- **Preview:** LiveView display filter, the same kernels on the preview tables. Preview exposure (`wf_expsim`) needs ML
  core patch 0004. It is already in the EOS M card core; on the 5D2, check and gate.
- **EXIF:** UserComment `WindyFilm <film> <print> <light> G<n> H<n> M<n> S<n> EV<x>` (core decides the exact text).
- **Log:** `ML/LOGS/WINDYFLM.LOG`, appended across boots (over 512 KB at boot: kept as `WINDYFLM.OLD`): boot line
  (camera, ROM check, hook result, pack and can status), and per shot: film, variant, timings. Lines wait in a ring
  while the card is busy; lost lines are counted in the log.
- **Config:** `ML/SETTINGS/WINDYFLM.CFG`, plain `key=value` lines: film id, print, light, grain, halation, bloom, mist,
  strength, output, curve, Film LUT, LUT EI, preview, preview exposure, EXIF.
- **Coexistence:** do not load together with `filmroll.mo`. Symbols: everything except ML's `__module_*_windyflm` is
  made local (`objcopy --keep-global-symbols`), so the TCC loader never sees a clash.

### 3.1 Input stage: open for a RAW path (owner request 2026-11-14)

Today every look grades Canon's developed 8-bit UYVY 4:2:2 frame. The RAW path was researched by the raw-film-engine
session (report `F:\MagicLatern\_research\raw-path\BAOCAO-raw-path.md`, ROM notes `rom_ssraw_RE.md` there). The
owner's choice and the plan built on it are in 3.2. The pipeline must take a new input without a rewrite:

```
camera input layer  ->  [develop stage]  ->  look stage  ->  output
(wf_cam_*: hook,        (future: black,      (film table /     (today: in place in Canon's UYVY buffer;
 buffers, halves)        WB, demosaic,        log / Log+LUT /   future: write UYVY for Canon's encoder,
                         matrix, log16)       effects)          or our own JPEG)
```

- **`src/wf_frame.h`** (core, internal): `struct wf_frame` = format tag + geometry + buffers. Fields: `fmt`,
  `part` (0 full / 1 top / 2 bottom), `w`, `h`, `pitch`, `data`, `dst` (the output buffer when a develop stage writes
  somewhere other than its input), and for RAW `black`, `white`, `cfa`, `bpp`.
- **Formats:**
  - `WF_FMT_UYVY8`: Canon's developed 4:2:2, processed in place. The only format implemented now.
  - `WF_FMT_RAW14` (reserved): ML's raw buffer (`raw_info`), packed Bayer.
  - `WF_FMT_LOG16` (reserved): a 16-bit working buffer, log-encoded 4:2:2 or 4:4:4. A develop stage writes it and a
    16-bit look path reads it.
- **The camera input layer is the only code that knows hook registers, Canon buffer addresses and frame sizes.** It
  fills a `wf_frame` and hands it to the worker. Kernels and effects never read registers or fixed addresses; they take
  a `wf_frame` (or the chunk callback the worker gives them).
- **The look is resolved once per shot into `struct wf_look`**, independent of the input format. It holds the output
  (film / log / Log + LUT / off), the table and variant, the curve, the cube, the effects, Strength and Light. The
  worker dispatches on (`frame.fmt`, `look.output`). A missing combination is logged and the frame is left untouched.
- **Look stages declare the formats they accept.** Today: the UYVY8 kernels. A RAW path later adds a develop stage
  plus a LOG16 variant of the Log + LUT / film path, without touching the UYVY8 kernels, the menu rows or the UI.
- **Config:** key `input` is reserved (value `jpeg`, the only one now). The menu shows no input choice until a RAW
  path exists.

### 3.2 RAW plan chosen by the owner: Hybrid + Canon curve inversion (2026-11-14)

What the research found (synthetic HDR scene through an EOS M sensor model and the owner's V-Log cube T250DMVL;
times from memsim at 200 MHz, where the 5D2 camera measured 0.83 x memsim; EOS M not measured; +-20-30 %):
- The 8-bit path loses the highlights. Neutral grey goes flat at +3.1 EV over 18 % grey (RAW: +5.0 EV). Warm lights at
  +2..+5 EV keep chroma 36 of 68 and drift 26 degrees in hue. Neon and lamp cores turn into a cold grey, because the
  V-Log cube expects 5-6 EV above linear 1.0, while JPEG clip is only +2.5 EV. Shadows: no visible gain with a film cube.
- There is no >8-bit demosaiced frame to hook. Canon develops raw -> SSRAW (5-layer pyramid; layer 0 is 14/16-bit
  Bayer) -> UYVY 8-bit inside the Eeko hardware.
- A full in-camera develop costs 43 s per shot (MHC demosaic); half resolution costs 11 s but gives only 4.5 MP.

The plan, in this order. Each step is our own code; the prototypes in `raw-path\proto\` are WindyStrife's and may be
read for ideas.

**A1. Invert Canon's real Picture Style tone curve instead of the sRGB EOTF (both cameras, 0 s per shot).**
- In the simulation, the error in the sun area drops from 12.4 to 2.6 codes. Truly clipped areas stay colourless.
- It applies to the scene-linear outputs only: Log, Log + LUT and the Film LUT cans. Catalogue film tables come from
  display-referred sRGB-in / sRGB-out cubes (`mk_wtab.py`), so they keep reading the JPEG as it is.
- **The curve comes from the owner's RAW+L pairs** (3-5 pairs with the Picture Style in use and the look off;
  `F:\MagicLatern\_research\raw-path\cr2\`). It is delivered as `luts/canon_curve/<STYLE>.csv`:
  - comment lines start with `#` and name the camera, the Picture Style with its settings, and the source pairs;
  - then 256 rows `code,linear` for codes 0..255, strictly increasing;
  - linear is scene-linear Rec.709 with 18 % grey = 0.18, and code 255 maps to at most 4.0.

  One curve serves all three channels. Columns `code,r,g,b` are accepted if the pairs show a per-channel curve.
- **Implemented (Log + LUT and the cans), 14 Nov 2026.** The first curve is `luts/canon_curve/EOSM_Film1.csv`,
  measured by the raw-film-engine session from the owner's RAW+L pairs:
  - EOS M, Picture Style **Film 1** (User Def. 1, a third-party style file), not Standard;
  - one curve for R, G and B; 18 % grey = 0.18 at code 139 (an assumed scale); white clips at code 242 with linear
    2.07.

  Because the curve fits that one style only, it is a menu choice, not a replacement:
  - menu "Input curve" = sRGB (default) / Canon Film 1 / Canon Neutral, config key `incurve=srgb|film1|neutral`;
  - curve slots: slot 1 = Film 1 (`luts/canon_curve/EOSM_Film1.csv`), slot 2 = Neutral
    (`luts/canon_curve/EOSM_Neutral.csv`, prepared 14 Nov; it waits for raw-film-engine's measured curve);
  - `build.sh` passes one CSV per slot (or `-`) to `tools/mk_loglut_tab.py`, which emits `wf_ll_dec_c<k>[1022]` (Q24)
    and `WF_LL_HAVE_C<k> 1`. The env vars `CURVE_FILM1` / `CURVE_NEUTRAL` point a slot at another CSV (tests). A slot
    without a CSV has no table: the menu skips it, and a config that names it reads sRGB. Without any CSV the entry
    is greyed;
  - only the load-time Log + LUT builder (`wf_loglut.c`) reads it, so the cost per shot is 0. Flat Log (the ARM kernel,
    `wf_dec`), the preview exposure tables and the catalogue film tables keep the sRGB EOTF;
  - the shot's EXIF comment says `| In Canon Film 1` (or `| In Canon Neutral`), and the `lut` log line says `in-film1`
    (`in-neutral`).
- **Headroom without a K fold.** The Log + LUT builder already handles linear above 1.0 for the LUT EI. Above white
  each curve is `a log10(x + b) + c`, so the builder shifts x down by 2^up and adds `up a log10 2` in the encode
  domain, which is exact. The Canon decode (max 2.07 -> 34.7 M in Q24, below 2^31) goes through that same path:
  - the gain multiply (only when the LUT EI or auto is set) and the up-shift (whenever `eib` is set and x > 1.0) are
    now separate steps;
  - the curve sets `eib` / `eid` even at gain 1. sRGB without a gain keeps `eib = 0`, so its tables are bit-identical
    to before (host test).
- **The K fold is still the plan for flat Log.** It applies if the owner wants Film 1 there too, because the ARM
  kernel relies on `wf_dec <= 16384` and `max(wf_lenc) <= 65279`:
  - decode stores linear / K, with K = the curve's value at code 255 rounded up to a power of 2;
  - encode is generated as `enc(i) = curve(i / 4096 * K)`;
  - then check `logchk` and the host LOG tests.
- **Auto exposure of the Film LUT cubes.** The grey reference is 0.18 with the curve; sRGB keeps `lin(118)`.
- **Table cache.** Bits 17-18 of the `.FRT` key hold the curve slot (Film 1 = bit 17, as in the first version;
  Neutral = bit 18). Old caches have 0 there, so sRGB caches stay valid.
- **Picture Style check.** Each curve is used only under the Picture Style and Contrast it was measured at
  (raw-film-engine read them from the makernote PSInfo):
  - Film 1: `lens_info.raw_picstyle` = User Def. 1 (0x21), User Def. 1 holds a loaded style file
    (`PROP_PICSTYLE_OF_USERDEF1` below 0x80, as ML's picstyle.c tells them apart; the pairs had PC 1 = 0x41), and
    Contrast -2. The slot alone is not enough: on 14 Nov the owner's EOS M had User Def. 1 based on Auto;
  - Neutral: `raw_picstyle` = Neutral (0x84) and Contrast 0;
  - the base comes from the module's own `prop_add_handler(PROP_PICSTYLE_OF_USERDEF1)` (0x0206000a on DIGIC 4 / 5),
    the Contrast from ML's `picstyle_get_current_contrast()`. Not reported yet (style 0, base -1, Contrast -10)
    counts as fitting.

  Under anything else:
  - the JPEG is read as sRGB;
  - `wf_ll_poll` reloads the tables;
  - the menu says what differs: `Picture Style is <name>, not User Def. 1` (or `not Neutral`), `User Def. 1 is a
    built-in style, not the Film 1 file`, or `Style contrast is <n>, measured at <m>`;
  - the `lut` log line says `srgb(style <hex>)`, `srgb(base <hex>)` or `srgb(contrast <n>)`. With a curve in use it
    says `in-film1 b<base> c<contrast>` (`bff` / `c-10`: not reported).

  The slot is checked, not the style file it holds. A table per style comes later, and only if the owner shoots
  several styles. The 5D2 gets the same table, but the curve was measured on the EOS M only.
- Optional later: **A2** (HTP + the same inversion, sun-area error 1.4, 3 degrees of hue drift) needs ISO >= 200 and
  costs 1 EV of shadow noise. It is the owner's call.

**P0. Probe that only logs (EOS M, never changes the image).** The probe answers whether the raw and SSRAW are still
intact at the hook. It is implemented in commit a675c8a (`wf_cam_eosm.c`, host test `PROBE`).
- It is a menu toggle under a debug entry, default off.
- At the NN hook entry (`CompleteSsrawToYuv` 0xFF169414; r0 = job, r1 = finished YUV), the camera layer snapshots into
  a static struct:
  - the 56 words at 0x41E82CEC (the Eeko copy of the 0xE0 parameter block, uncached alias): +0x50 raw input,
    +0x64..+0x74 SSRAW A[0..4], +0x78 B-set valid, +0x94.. output geometry;
  - EDMAC 0xC0F04008 (ML's raw WR0), 0xC0F26308, 0xC0F26408, 0xC0F26A08, 0xC0F26B08. At completion these hold the
    last stripe, so they are a cross-check only;
  - `ff234680(&out[2], &tmp, 1, i)` for i = 0..4 (MEMNAVI_GetReadMemory; out[0] = A[i], out[1] = B[i]). It is a ROM
    call from the hook ([likely] safe), so it has its own sub-toggle;
  - an FNV-1a over the first 4 KB at the raw address and at A[0] / A[1], plus a copy of the first 1 KB of layers 0 and 1.
    Read an address only if it lies in the shoot pool 0x40D6C000..0x60000000.
- `wf_task` writes the lines to `WINDYFLM.LOG` and the 1 KB copies to `ML/LOGS/WFPROBE.BIN` after the shot. Hooks never
  log. A record the card refuses (busy with the shot) is retried at its own offset; the log names the failing step.
  The first record of a boot makes a new file only once Canon is idle (card writes: `src/wf_fio.h`).
- **Record version 2 (11-14, after the first run):** the sources are raw, raw2 (the same place in the other raw buffer:
  two buffers 32 MB apart, starts 0x46004894 / 0x48004894; the last frame of a burst finds +0x50 still on the previous
  shot), A[0], A[1]; with +0x50 = 0 (first shot after power-on) the raw is WR0 - 32626440 + 480242. Copies: raw and raw2
  from +16 MB, A[0] / A[1] from +0. Measured on the 11-14 run (raw-film-engine): YUV (x, y) = raw (84 + x, 64 + y),
  black 2048.
- **5D2 (record 0x0102, raw-path/P0_5D2_SPEC.md):** taken at the TOP and the BOT hook, one record slot per hook kind.
  pb[56] = the job handle and H[0..4] of this half's develop state, P fields (MEM1 at P+0xC68), P[0..14], the engio
  shadow base and 9 EDMAC shadows, this half's Mem1ToYuv block; sources raw0 = MEM1 + 0x76C8, qr, a08, m2r (copies from
  +16 MB, inside the raw); "On + ROM call" runs 5 leaf getters for a live job only; pool 0x44000000..0x5C000000.
- **Run it with RAW+L, single and continuous drive.** Compare the raw checksum with the CR2. The open risk: after
  `DelayCompleteRawToSsraw`, Canon calls `ReallocateMemory` / `sssCompleteMemShrinkCB`, so in burst the raw may already
  be reused. SSRAW layer 0 is released only inside 0xFF169414, after our entry hook, which makes it the fallback.
  WindyFilm already finishes the frame before the hook returns, so nothing is lost to the next shot.

**H. Hybrid highlights (EOS M first; Log + LUT and Film LUT cans).** The source is the raw or SSRAW layer 0,
whichever P0 shows intact.
- The 8-bit look pass stays, with A1.
- Mark the 16x16 blocks where max(R', G', B') >= 241. The frame has 324 x 216 = 69,984 blocks: a byte map is 70 KB,
  a bit map 8.75 KB.
- In marked blocks only:
  1. read the raw rows and build 2x2 superpixels (half resolution);
  2. black and white levels, WB, the camera matrix;
  3. rebuild the clipped channels from the block's colour ratios;
  4. blend with the A1 value in linear light (weight smoothstep from 240 to 253 on the 8-bit max);
  5. V-Log, then the cube, then UYVY.
- **Trap: colour ratios.** Take them only from pixels at least 3 px from the clipped area and between 55 % and 92 % of
  clip. Samples right at the edge give a pink core and green / magenta rims.
- **Raw geometry (EOS M, ML `raw.c`):**
  - 5280 x 3528, pitch 9240 (14-bit packed);
  - active area 5184 x 3456 at left 72 / top 52, which maps 1:1 onto the NN frame (Diana halves: add the half's row
    offset);
  - black 2048, white 13400 at ISO 100 and 15200 at the other ISOs.

  The SSRAW layer-0 packing and stride come from the P0 dump.
- **The UYVY8 kernels work in place**, so the hybrid stage needs a marked block's input values before the look pass
  overwrites them. Two choices:
  - (a) a scan pass before the look pass: simple, 2.4 s memsim on the heavy scene;
  - (b) process bands of 16 rows: scan the band, run the look on it, then fix its marked blocks. A band is 166 KB of
    scratch, which saves most of the second read.

  Measure on the camera before choosing.
- **Budget:** 5.7 s on the heavy scene (9.7 % of blocks: scan 2.44 s + blocks 3.26 s); 1.85 s at 5 %. With the scan
  folded in, about 0.37 s per 1 % of blocks. Memory is about 0.3 MB plus the block map.
- **`struct wf_frame` gets an optional aux plane**: `aux_fmt` (none / RAW14 packed / SSRAW layer 0), `aux`,
  `aux_pitch`, `aux_x0`, `aux_y0`, `aux_black`, `aux_white`, `aux_cfa`. In Hybrid the main `fmt` stays `WF_FMT_UYVY8`;
  the aux plane is a side input, so the UYVY8 kernels, the menu rows and the UI do not change. `WF_FMT_RAW14` as the
  main format stays reserved for B.
- **Config:** `input=jpeg|hybrid`. The menu gets a "Highlights: 8-bit / RAW" row once H works on the camera.
- **5D2:** the ROM analysis covered the EOS M only. The 5D2 gets A1; H on the 5D2 needs its own ROM work first.

**B. Full develop (later, optional "slow" mode):** 35-45 s per shot, only if the owner accepts the wait. It uses
`WF_FMT_RAW14` as the main format and a LOG16 look path, as laid out in 3.1.

### 3.3 Highlights / Shadows (owner 2026-11-14)

Two settings, "Highlights" and "Shadows", -5..+5 each, default 0, for the film looks and Log + LUT (the Film LUT cans
too), never flat Log. The math is WindyStrife's own (the V14U spec); WindyFilm has its own code.

- **Curve.** On the neutral-axis curve N of `Y' = N[y] + E`: `N'[y] = N[y] + round((h x^2 (255 - x) + s x (255 - x)^2)
  / 385341)`, x = N[y]. Two cubic humps, peaks at x = 170 (h) and 85 (s), 0 at black and white, about 6.4 codes a step;
  +5 lifts code 170 by 32. Rounding is half away from zero (d < 0: -((192670 - d) / 385341), else (d + 192670) /
  385341). The result is kept non-decreasing: `p = clamp(x + d, p, 255)`. `wf_hs_bend` (wf_film.c) does it. The
  divisor is a constant, so the camera code is a UMULL, never `__aeabi_uidiv`.
- **Films.** Build time only: `WF_FT_HS(h, s)` puts the two values as signed 4-bit fields in bits 16-23 of the
  `wf_film_build` flags. The builder bends N last, after the Strong pass. E and C keep their values relative to the
  unbent N, so every colour at a luma moves with its grey. 0 / 0 adds no bit and bends nothing: the set is
  bit-identical to the builder before the feature (host mode HS: FNV of 8 sets against 7f9e294). The shot set's key
  compares the flags, so h / s are in it. The preview key `wf_pv_fkey` carries them in bits 12-19.
- **Log + LUT.** `wf_ll_poll` bends the loaded tables' N after `wf_loglut_load_cached`. The `.FRT` cache is written
  inside the load, so it holds the tables unbent and needs no new key. When only h / s change, the poll waits for the
  readers as for any change, then bends again from a 256-byte copy of the loaded N, with no card read (log `lut:
  highlights +4 shadows -2`). A shot takes the tables' values (`wf_ll_have_hs`), as it takes their cube and EI.
- **State.** `wf_hl` / `wf_sh`; config keys `hl` / `sh` (-5..5; out of range or not a number is ignored); EXIF `|
  Highlights +3 | Shadows -2` and the log lines (`yuv .. hl+3 sh-2`, `shot #n <title> hl+3 sh-2: ..`, `lut <cube> ..
  hl+3 sh-2`) only when a value is not 0. Text through `wf_hs_text` ("-5".."+5"; no `%+d` on the camera).
- **UI.** ML menu entries "Highlights" / "Shadows" (row entries, greyed where they do nothing). Browser rows
  `WF_ROW_HL` / `WF_ROW_SH` between Strength and Film LUT, no wrap at the ends. 9 rows (10 with Bloom) do not fit the 19 px pitch above
  the tabs (y 136-268), so 7 show and the list scrolls with the focus, with a 2 px scroll bar at x 712. The font keeps
  its size. Icons: a tone disc, upper / lower half lit. Host render: `tests/ui` shots `b2s_hs`, `b2s_hs_top`.

### 3.4 Leica Vivid and the Windy names (owner 2026-11-14, as Film Roll V14U)

- **Leica Vivid** = Film LUT 23 (`WF_FLUT_LEICAV`, `WF_FLUT_N` 24), can id 84 `LLEICV`, cube `ML/DATA/LEICAVSR.CUB`: the
  owner's Lumix "L Leica 2" display cube (33^3, sRGB in, sRGB out), made on Picture Style Standard as the S9's Real Time
  LUT uses it (Standard + the LUT). Owner 11-14: "grade tren srgb luon ko ra vlog dau". So the cube is applied straight
  to the JPEG's sRGB codes (`wf_ll_disp`): per node R'G'B' code / 255 -> the cube's domain -> grid, no decode, no log
  gamut matrix, no log curve, no resample through V-Log; the output is the display picture as for every cube. The file
  is `<code>SR.CUB` (and `SR.FRT`) whatever the Log curve, so it needs no V-Log and the menu never asks for it.
  `WF_FLUT_DISP(l)` marks it:
  - the JPEG is always read as sRGB: `wf_incurve_t()` returns sRGB while Log + LUT is on it (the menu says so), so the
    look, the poll and the load all see sRGB;
  - no auto exposure (`wf_ll_auto` 0, as Technical); LUT EI by hand still works: the sRGB code is decoded
    (`wf_ll_dec`), the light scaled by 2^(EI/3), then encoded back with the inverse of the same table
    (`wf_ll_reenc`; light over white = 255) before the grid;
  - the `.FRT` key holds `wf_ll_disp` in bit 19 (the input curve bits are 0 for it), so a cache made the other way is
    rebuilt;
  - choosing it (can load, Film LUT row, ML menu) asks for Picture Style Standard 0x81 (`prop_request_change` of
    PROP_PICTURE_STYLE 0x80000028) unless the camera is on it already; log `style: Standard for Leica Vivid`;
  - the can shows only with its cube on the card (as every Film LUT can; the old V-Log file `LEICAVVL.CUB` does not
    count); the first 33^3 load takes a few seconds, then the `.FRT` cache (written through wf_fio.h);
  - Highlights / Shadows bend it like any Log + LUT output (3.3).
  - Can art: our house layout, graphite body, champagne accent, the name in our font. No logo, red dot, script lettering
    or other mark of the camera maker.
  - Labels say it is a look on the Standard picture, not a log look (owner 11-14 14:00): can foot "STD + LUT", lip text
    "digital look" without "35mm" (gen_cans `side` / `side35`), chip "Standard" (catalog `fmt`, FMTS), info page "Look on
    the Standard picture", ML menu Output "Look on Standard" and title "Leica Vivid (look on Standard)".
- **Windy names:** the 12 cans of the owner's film pack and his neutral cube (Film LUT 9, 11..22; can ids 66, 71,
  73..83) are named "Windy ..." instead of "Tung ..." (catalogue, `wf_flut_name`, cans with "Windy" on the body). Cube
  file names and config codes stay the same (`TUNGNTVL.CUB`, `flut=TUNGNT`, ...); `flut=leicav` selects Leica Vivid.

### 3.5 The CinePrint looks on Canon Log 2 (owner 2026-11-14)

Owner 11-14: "ma tien chuyen luon may film windy tru cai natuaral dung lut clog2 voi log clog2 cho dung canon di chu
nhi", then "thay het qua clog2 thoi ban may cai cineprint ay", then richer colour: the cubes are 65^3 exports of the
pack's C-Log2 PowerGrades. The twelve looks of the CinePrint Studio pack (Windy EKTA P = Film LUT 11 and Windy 250D P
.. EXRa M = 12..22; can ids 66, 73..83) load Canon Log 2 cubes instead of V-Log ones. Windy Neutral (9, the owner's own
grade), Luca Bono, the Kodak / Fuji cubes and Leica Vivid are unchanged.
- The cubes: `YRGB - Canon Log2/C-Log2 <look> YRGB.drx` (6 nodes, DaVinci YRGB) applied in Resolve Studio 21 and
  exported as 65PT LUTs ("C-Log2 <look> 65.cube", share Anh/CinePrint_CLog2_65 with MD5_8.txt and export_log.txt; copy
  F:\MagicLatern\CinePrint_CLog2_65). Spatial nodes are not in a LUT: our grain / halation stay. 6.3..7.6 MB each, all
  values in 0..1 (nothing clamped); the M looks have lifted blacks (min 0.05..0.09). The pack's OSM cubes ("OSM - Canon
  Log2", 33^3, a flatter curve, the same input domain) also load.
- `wf_flut_curve[]` (wf_loglut.c) / `WF_FLUT_CURVE(l)`: the curve each cube was made for (V-Log, `WF_LL_CLOG2` for
  11..22; out of range V-Log). films/catalog.py `FLUT_CURVE` is the same table (`catalog.py check` reads the C one) and
  gives the can chip `fmt` "C-Log2" and the cube name `flut_file()`.
- A can shows (`wf_cube_have`, scanned at start and when the browser opens) and loads only with the cube of its own
  curve: `<code>C2.CUB` (EKTAP_C2, T250DPC2, T250DMC2, T500TPC2, T500TMC2, TEKTAMC2, TEKAAPC2, TEKAAMC2, TEXRP_C2,
  TEXRM_C2, TEXRAPC2, TEXRAMC2). Its Load sets Log + LUT and that curve; `wf_loaded()` matches only on it.
- Windy EKTA P's can no longer loads `EKTAP_VL.CUB` (the owner's "Tung EKTA P V-Log" export, 33^3) nor the Film Roll
  card's `EKTAP_C2.CUB` ("Tung EKTA P C-Log2", made by mk_usercube from that export). `tools/mk_card.sh
  CINEPRINT_C2=<folder>` copies the twelve cubes under the C2 names (`catalog.py cubes` maps them, all twelve required)
  and replaces a same-named cube of the source card. Per look it takes, first found, any case: the C2 card name,
  "C-Log2 <look> 65.cube", "C-Log2 <look> YRGB.cube" or "C-Log2 <look> OSM.cube"; a cube must say LUT_3D_SIZE 2..65.
- Film LUT row (`wf_flut_step`): each cube comes on its own curve while the curve in use is the current cube's own; a
  Log curve set by hand that is not the cube's own (S-Log3 on Vision3 250D, V-Log on a CinePrint look) stays, and then
  only the cubes on the card for that curve come (V-Log + T250DPVL.CUB, EKTAP_VL.CUB, ...). Technical keeps the curve.
  The menu's "%s is a Canon Log 2 cube: set Log curve to Canon Log 2." advice is left out when V-Log is set and the
  look's old VL cube is on the card (`wf_cube_vl`, same scan).
- 65^3 needs no code change: the loader streams any 2..65 grid (two B-slabs held: peak 111 KB at 65^3); a 65^3 file
  over a 33^3 one of the same name is parsed again (the .FRT key holds the cube's size and content samples); the .FRT
  is 181,436 B whatever the grid. Host test CUBE65 (+ `WF_CUBE_REAL=a.cube:b.cube` to time real cubes). Time in qemu:
  the PowerGrade 65^3 cubes 369..379 ms (P, 6.4..6.7 MB) and 413..418 ms (M, 7.6 MB), the 33^3 EKTAP_VL.CUB 101..106
  ms: 3.6x / 4.0x (wf_loglut.c's instruction counts: 3.8x). The owner's EOS M log (RAWB2) has 33^3 first loads of
  4.2..6.7 s (EKTAP_VL 4.9 s), so a 65^3 first load on the EOS M takes about 18..20 s (15..27 s over that spread); a
  cache load stays 0.2..0.6 s (18..19 ms in qemu for every size). Not measured on the 5D2. 12 cubes = 84 MB of card.
  One .FRT per cube: a change of LUT EI or of the Input curve in use (also the fall-back to sRGB when the Picture
  Style does not fit) parses the cube again (about 20 s at 65^3, 5 s at 33^3); Highlights / Shadows do not (applied to
  the cached tables).
- Unchanged by the curve: auto exposure (`wf_ll_auto` for every film cube), the `.FRT` cache (the key holds the curve
  in `mode` and the file name ends C2, so V-Log caches are never reused), the B develop on raw-develop (wf_dev handles
  curves 1..5 the same way and takes `wf_loglut_name(.., lk.curve, ..)`).
- Gamut A/B (notes `clog2-ab/` OSM, `clog2-ab-pg65/` PowerGrade 65, `clog2-ab/tool/`; Day09 JPEGs IMG_0208 250D P /
  0210 500T M / 0233 EKTA P / 0215 EXRa P). OSM: the module with Cinema Gamut and the C2 cube against the module with
  V-Gamut and the VL cube: mean dE00 0.53..1.09, p95 <= 2.6 (the same look); Rec.709 primaries instead 3.4..8.8 (p95
  15..25), legal range 2.9..3.8. PowerGrade 65 (a different, denser look, so not the same picture as the V-Log OSM
  one): vs the V-Log OSM render mean dE00 8.2..9.8 for the module, Cinema Gamut nearest in the float renders (8.7..10.3)
  against Rec.709 primaries 11.8..14.2 (oversaturated: teal sky, hot orange) and legal range 8.8..11.9. So the
  full-range code and Cinema Gamut the module uses stay right. Auto exposure lifts the PowerGrades' darker mids: +0.7..
  +0.8 EV, EKTA P +1.5 EV (OSM +0.3..+0.5).

### 3.6 Roll + Darkroom (owner 2026-11-14)

You shoot a roll: one film, 3 / 6 / 12 / 24 / 36 RAW+L frames (3 and 6: owner 11-15, a short roll), no playback. Afterwards the in-camera Darkroom develops each
frame's CR2 from the card with the roll's film, through WindyFilm's own full RAW develop B, into a print JPEG. The EOS M
comes first. The 5D2 follows once the EOS M works (raw 5792 x 3804, pitch 10136, black 1024, GBRG).

**Clean-room.** The Darkroom is a new path, written from scratch for WindyFilm:
- the CR2 reader `wf_cr2.c`: a camera port of WindyStrife's host tool `tools/cr2_dec.c`, from the public TIFF / CR2 /
  ITU-T T.81 (lossless JPEG) descriptions;
- the develop `wf_dev.c` (B, section 3.2);
- the baseline JPEG encoder `wf_jpe.c`, from T.81 and its Annex K tables;
- the roll state machine `wf_roll.c`, the darkroom run `wf_dark.c` (inside wf_task) with its pure parts `wf_darkp.c`, and the screens
  `ui/wf_dark_ui.c`.

None of it is code from the Film Roll author, and Canon's ROM JPEG encoder is not used: the CR2 is developed outside
Canon's pipeline, so there is no Canon YUV buffer to hand it. Nothing Genesis-related was opened. The reference video
gave the idea and the layout only. None of its code, bitmaps or fonts was opened, and no reference frame is in the repo.
Stocks keep their Windy names, and there are no brand logos.

**Roll (shooting).**
- **One roll at a time.** Since 11-17 a roll is loaded from the WindyFilm browser in Darkroom mode (3.12): its
  Exposures row (3 / 6 / 12 / 24 / 36, default 24) and SET on a film. Until then the roll picker (UI screen ROLL: shelf
  tabs, a can carousel, the exposures) did it; the owner found the browser nicer and the picker is gone (its rules below
  are the browser's now). SET loads the roll and plays the browser's load animation, then the UI closes and LiveView
  shows. A key skips the rest of the animation.
  - A roll with no frames yet is simply reloaded with the new film or count. A loaded roll with frames refuses a new
    load ("This roll has frames: PLAY develops it (Darkroom)").
  - **Close and load** (owner 11-15). A finished roll (full, or rewound with frames left) asks first: "Roll 021 has 12
    undeveloped frames. / Close it and load the new roll? / <film> x12 / (the RAW/JPG files stay on the card)", keys
    "SET Close and load / MENU Cancel" (the browser's `draw_swap` since 11-17). SET calls `wf_roll_req_swap`: wf_task
    closes the roll (its frames become K, `roll: 021 closed for a new roll, 12 frame(s) kept undeveloped`) and loads
    the new one at once, with the load animation. MENU cancels and other keys do nothing. There is no queue of rolls:
    the closed roll's frames are not developed later.
  - The film is locked while a roll is open: the browser refuses a film change.
- **Roll file** `ML/SETTINGS/WFROLL.TXT` (format in `wf_roll.h`): plain text, always 1536 bytes.
  - wf_task is its only writer. It creates the file once with `wf_fcreate` while Canon is idle, then rewrites it in
    place with `wf_fopen_w(WF_FO_AT, 0)`. It is never created twice (trap W24): once one create has made the file
    (even when its write was cut short), every later try opens it in place.
  - A failed try is logged once, with its step (`roll: ... not saved (write rc 100), retry`), and retried on the next
    poll; the first good write after failures logs `saved after n tries`.
  - File handles: any value but 0 (and -1) is a good FIO handle (`WF_FH_OK`). DryOS hands out odd handles as often
    as even ones; RAWB8 took 3 / 5 / 7 for errors and left the file open, so the roll file stayed 0 bytes and the
    log, config and .RBT writes failed now and then ("rc 3/5/7").
  - It is read at boot, the first thing in wf_task's loop after the worker and the hooks, before anything of the look
    is built (owner RAWB9: the roll came at 10.9 / 15.2 s, after the grain build, and RAW+L, review off and the HUD
    waited for it). A file that does not parse means no roll (logged); an empty file (a save that did not finish) too.
- **Frames.** wf_task counts frames while Canon is idle (job_state 0, no shot pending) and the file number from
  `get_shooting_card()` has been stable over two polls. `wf_roll_seen` then adds every new number that has a CR2:
  - numbers wrap 9999 -> 1 with the folder + 1;
  - a jump of more than 64 means the counter was reset elsewhere: the roll re-anchors and adds nothing;
  - frames past the count are not added, and at the count the roll is FULL.
- **RAW+L forced.** While a roll can shoot, wf_task sets `PROP_PIC_QUALITY` to RAW + L fine when it differs. The old
  value comes back once the roll is full, rewound or closed, and at unload. A frame without a CR2 does not count.
- **The shot's JPEG is Canon's own** (owner RAWB9, 11-15: the in-camera look took 23..36 s a frame, "chụp nó lại
  thành xử lý luôn"). While a roll is in the camera (loaded, full or rewound) and RAW develop is Off, `wf_look_use()`
  is `WF_LU_NONE`:
  - the NN / TOP / BOT hooks hand nothing to the worker: the frame keeps Canon's JPEG and the shot costs only Canon's
    own wait. The log says `yuv NN#n roll frame: Canon's JPEG, no look (the Darkroom prints it)`; each such JPEG keeps
    its empty place in the EXIF ring (the numbering), no UserComment is written;
  - nothing of the look is built: no Log + LUT tables (the 21 s cube read), no halation / grain / mist (8..13 s for the
    grain), and what was built is given back (about 6 MB of shoot memory). Loading a roll after a film change costs
    nothing either. The LiveView preview of a film look stays (the pack table is a quick read); a Log + LUT look is
    not previewed while a roll is in (its tables are not there).
  - The Darkroom prints the frames later with the full look. Closing the roll (or developing all of it) brings the
    shot path back (`WF_LU_SHOTS`): the tables are built again for normal shots.
- **Both modes at once** (owner 11-14 evening, replacing that afternoon's "one mode at a time"; kept as the owner's
  opt-in after RAWB9: RAW develop is Off at every boot): with a roll loaded and RAW develop On (`WF_LU_SHOTS`: the look
  is built as without a roll), each RAW+L shot is developed by B right away (about 30 s, as without a roll) and also counts as a
  roll frame whose CR2 the Darkroom prints later with the roll's film. Loading a roll leaves RAW develop alone, and
  RAW develop leaves the roll alone. The frame counts once Canon is idle and B has finished (no shot pending), so
  "WIND ON" comes after B's develop.
  - Memory: B holds one shoot-memory block (tables + scratch, about 790 KB) while it is On. The darkroom wants a
    35.8 MB contiguous frame from the same shoot memory, so B gives its block back while a run lives
    (`wf_rawb_poll` sees `wf_dark_busy()`). The run waits up to 10 s for that (a cube build may be under way) before it allocates (logged), and B
    builds its tables again after the run. The darkroom does not start while a shot's B develop runs
    (`wf_rawb_busy()`, "Not now: the camera is busy").
- **Blind roll** (menu, default On):
  - image review off (`PROP_REVIEW_TIME` 0, the old value back afterwards);
  - PLAY blocked: CBR_KEYPRESS eats it (outside the ML menu) while a roll is in the camera (loaded or rewound, not yet developed or closed) and the darkroom is not running, and shows a short note.
  - Off leaves Canon's review and PLAY alone.
  - Since RAWB28 (owner 2026-11-20) also a browser row in Darkroom mode, under Exposures (3.19); the same `wf_blind`.
- **HUD** over LiveView (wf_task draws it, ML colour indices, solid black chips). The film info shows for 2.5 s,
  then fades (owner 11-14: "giu phim info 2-3 s"):
  - "ROLL 021 <short name>" top left;
  - the big counter "5/12" bottom right, red when full, with "WIND ON" for 1 s after a frame, or "END OF ROLL" /
    "REWOUND";
  - an exposure ladder on the right edge.
  - The fade: 0.4 s in grey with every other pixel cleared (a dither: ML's bitmap palette has no alpha). Then wf_task
    clears our own rectangles to 0 once, and only a small dim counter "5/12" stays (grey on a black chip, red when
    full), so the frames left are always in view.
  - The info shows again for 2.5 s when LiveView comes back (or the ML menu, the UI or PLAY goes away), after each
    shot (Canon's job ends), when a frame counts, and when the roll, its film or its state changes.
  - The HUD shares the UI's font pack `WFFONTS.DAT` (`wff_open` counts its users; `wff_reload` reads it again for a
    user that has it). A pack Canon's card kept from the HUD (card busy, as at boot) is read again every 2 s, at most 5
    times, while the UI is closed (the UI retries its own, main c114bfe).
- **End-of-roll notice** (owner 11-15: the small red counter is easy to miss). A box in the middle of LiveView (x 96..624,
  y 118..336, clear of the chip, the counter and the ladder; `wfdr_notice`, ML colour indices, a red frame):
  - full: "END OF ROLL", "12/12 . <film> . ROLL 021"; rewound ("End roll now", or a stopped darkroom run with frames
    left): "ROLL REWOUND", "7 frames . <film> . ROLL 021" (the frames still to develop);
  - "Roll finished: develop it in the Darkroom, or load a new roll", "Develop: open WindyFilm > Darkroom", the keys
    "[SET] Darkroom   [INFO] New roll", and the small line "The shutter stays locked until then" (while the shutter
    lock below holds; otherwise "Shots from now on are not counted, WindyFilm keeps working").
  - When: 5 s when the roll ends on this boot (its last frame counts, or End roll now); 2.5 s again on the HUD info rule
    (LiveView back, the ML menu / UI / PLAY gone, a shot done) while the roll stays full / rewound with frames to
    develop. A transition with no LiveView waits for it. Then the HUD's 0.4 s dithered fade, then the box goes back to
    0 and `redraw()` gives Canon's own LiveView marks back.
  - A key press (CBR_KEYPRESS: shutter, arrows, wheel, MENU, INFO, PLAY, ...) drops it at once and goes on to Canon / ML
    as before; SET / Q / joystick centre (EOS M LiveView sends SET as Q) is taken and opens the Darkroom screen
    (`wf_ui_open_scr`, the same open as TRASH), and INFO is taken and opens the browser in Darkroom mode on its
    Exposures row (`WF_UI_NEW_ROLL`: a new roll, through the close-and-load question). Only while the box is up.
  - Never under the ML menu, the UI, PLAY, in movie mode or during a darkroom run (a run's own rewind gives none).
    The log says `roll: end notice full|rewound` each time it shows.
- **Shutter lock** (owner 11-15: a finished roll takes no more shots). While the roll is FULL, or REWOUND with frames
  left to develop, wf_task holds ML's UI lock bit `UILOCK_SHUTTER` (0x0001): `gui_uilock(icu_uilock | 1)`, the call
  ML's exmem.c uses against ERR70 (it waits up to 2 s on the property, so only wf_task calls it).
  - The lock is derived from the roll state on every poll and never stored: nothing on the card holds it. It is
    released when the Darkroom starts (and stays off while a run lives), on Close roll, when a new roll loads, at
    unload (`wf_dark_end`), whenever the roll is not full / rewound, in movie mode and while recording
    (`__recording || __recording_custom`). Each change logs `roll: shutter blocked (full|rewound), UI lock 1` or
    `roll: shutter free, UI lock 0`.
  - Half- and full-shutter presses in LiveView (not under the ML menu, the UI or PLAY) are also taken by CBR_KEYPRESS
    and show the end-of-roll notice at once, so the press is answered. Whether Canon's half-press AF still runs under
    `UILOCK_SHUTTER` is a camera check.
  - With no roll on the camera nothing changes: no UI lock call, no key taken (host test).
  - EOS M only (the 5D2 has no roll yet: `wf_roll_req_swap` returns -4 there). Both cameras from `dark-5d2` (3.18).
- **End / close.** "End roll now" (menu) ends the roll early (the roll is rewound: no more frames); starting the darkroom rewinds a loaded roll first.
- **A full or rewound roll keeps** (until the Darkroom develops it or Close roll): the film loaded and locked (another
  film is refused; the same film plays the load animation); Canon's own JPEG on any later shot (`WF_LU_NONE`; with RAW
  develop On, B and the look, if the quality Canon gets back is RAW+L: B develops only RAW+L shots); RAW+L and image review back to Canon's
  own values (forced only while the roll can still take frames); PLAY still blocked while Blind roll is On.
  "Close roll" gives up on the frames not yet developed: they become K and their CR2s stay untouched.

**Darkroom (developing).**
- **Entry.** ML menu "Darkroom" opens the darkroom screen. SET starts. MENU stops, after a second MENU to confirm,
  and closes the screen when nothing runs.
  - From the WindyFilm UI (owner 11-15, EOS M): "[PLAY] Darkroom" in the footer of the browser while a roll is on
    the camera or the mode chip is on Darkroom (3.12). It is lit when the roll has frames to develop and dim otherwise, with the reason in
    the status pill ("No frames to develop", "Load a roll first"; `wfdr_dev_why`). PLAY opens the darkroom screen
    when lit. With no roll on the camera, in JPEG / RAW develop mode, the browser's PLAY still closes it, as before;
    in Darkroom mode it says "Load a roll first". The end-of-roll notice's SET
    goes to the same screen.
  - Starting needs: not movie mode, a card, battery >= 30 %, and no B develop of a shot still running.
- **Inside wf_task**, no task of its own (owner RAWB9: DryOS's table was full, `tasks 104 of 104`, the UI task on the
  last slot, and `task_create("wf_dark")` was refused at every SET: `dark: no task (5)`, the owner waited 4 minutes).
  `wf_dark_start` (UI / menu) only checks and asks (`dk_go`); wf_task's next loop runs the whole run in `wf_dark_poll`,
  right after `wf_roll_poll`, and comes back when the roll is developed or stopped. No refusal is possible.
  - wf_task's other polls wait meanwhile; between bands the run calls `wf_task_yield` (the EOS M ML-menu guard, the
    browser request) and writes the log between frames. The UI task (prio 0x1B, above wf_task 0x1E) draws and takes
    MENU as before. The worker (0x1C) runs the look stage, as for a shot. B's tables are given back by calling
    `wf_rawb_poll` from the run's own wait.
  - The start needs the worker up (a boot's first seconds: "Starting: try again in a moment").
  - **Getting ready** (`WF_DP_MIX`, frame -1, "Mixing the chemistry"): `wf_look_use()` is `WF_LU_DARK` now, so the run
    builds the look's effects (halation, grain, mist) for its post code and loads the film table, before the big
    blocks are taken: `dark: look ready 10150ms: film table, halation, grain` ("-" after a part with no memory). No
    shot LUT is built (the develop tables hold the cube) and the halation is built whatever the camera's aspect (the
    print is 3:2).
  - Then: PauseLiveView, powersave_prohibit, then the memory:
    - raw: 32,598,720 B from the SRM suite (a contiguous run of its buffers);
    - frame: 35,831,808 B (5184 x 3456 UYVY) from `shoot_malloc_suite_contig`, else another SRM run;
    - a 256 KB DMA read buffer, a 256 KB cacheable decoder input, the 128 KB CR2 head, the encoder state and a 64 KB
      out buffer;
    - the develop tables and scratch (356 + 392 KB), built once per run for the roll's look with B's choice: a file
      read for Log + LUT cubes (`wf_dev_build`; the curve picks the cube, so the CinePrint cans read `<code>C2.CUB`
      as the V-Log cans read `<code>VL.CUB`), and Leica Vivid's display cube `LEICAVSR.CUB` on the sRGB develop's
      codes with no log step (`wf_dev_build_disp`, Highlights / Shadows on its output). The log names it
      (`dark: tables disp c0 LEICAVSR.CUB`). Cube tables come from B's cache `ML/DATA/<cube>.RBT` when it
      matches: same struct, camera and key (`wf_rawb_rbt_key`, one function for both), so a run reads what B wrote
      and B, rebuilding after the run, reads its own file (`, LEICAVSR.RBT hit`). A miss is built and written
      (Canon is idle during a run).
  - If the raw or the frame cannot be had, the run fails with "no memory" and the log lists every chunk. A banded
    fallback (no halation / mist) is the next step only if the camera run needs it.
  - Everything is freed and LiveView resumed when the run ends.
- **Per frame**, four phases (the UI names them; `wf_dark_view` publishes the progress):
  1. **Load** ("Loading the negative"):
     - open the CR2 read-only; 128 KB head -> `wf_cr2_parse`; 4 KB at the strip -> `wf_cr2_parse_ljpeg`;
     - decode 64 JPEG rows per step from 256 KB reads (DMA buffer, then a copy into the cached input);
     - Canon's APP1 from the first 64 KB of the paired JPG (`wf_dk_app1`); no JPG means a JFIF APP0 instead.
  2. **Expose** ("Exposing the paper"):
     - levels by `wf_dev_levels` (masked columns 16..64, clip pile-up), else the per-ISO white (`wf_dk_white`);
     - WB = Canon's as-shot levels from the CR2's MakerNote ColorData;
     - exposure = the fixed scene scale (clip at 0.18 x 2^3.3, as B's default) plus a per-ISO trim (`wf_dk_trim10`),
       with the film shoulder on;
     - `wf_dev_rows` in 64-row bands into the frame. The 288 x 192 print thumbnail is gathered from the rows
       (`wf_dk_thumb_rows`), so the print fades in on the easel.
  3. **Fix** ("In the fixer"): the look stage through the worker. `wf_shot_run` gets the frame flagged as developed
     and runs the same stage as after B:
     - films: the film table + effects;
     - Log + LUT: effects only (the cube is in the develop tables);
     - plain log: nothing.

     The effects' exposure gain (the halation and mist hot ramps, `fx_hot`) takes the CR2's own EV100: ISO,
     ExposureTime 0x829A and FNumber 0x829D from the Exif IFD (`wf_dk_ev8`, to 1/8 EV), logged " ev<EV>R". Until
     11-15 it read the camera's dials at develop time ('M' / 'F' / 'L'), so a daylight frame developed indoors got the
     night glows. A CR2 without those tags logs " ev?" (the middle gain, no point boost).

     Darkroom frames never touch the EXIF ring of the shot path: they are logged as "dark" lines.
  4. **Dry** ("Washing and drying"):
     - encode at q95, 4:2:2, in 16-row bands (`wf_jpe`), with Canon's APP1 copied in and our UserComment ("WindyFilm
       Darkroom: <look> | ROLL 021 F01");
     - write in 64 KB chunks;
     - mark the frame D, save the roll file, hang the print on the wire (the last 8 prints, 72 x 48).
- **Output** `<card root>ROLLS/ROLLnnn/Fnn_xxxx.JPG`: nn is the frame 01..36, xxxx Canon's IMG number.
  - The directories are made with `FIO_CreateDirectory` through `wf_fmkdir` (only while Canon is idle, checked with
    `is_dir`).
  - Nothing is written in DCIM. The CR2 and Canon's JPG are never touched.
  - A print of the same name from a stopped run is removed first.
- **Stop / resume.**
  - MENU, unload and power-off stop after the current band. The partial print is removed and the frame stays S, so
    the next run starts with it.
  - Battery below 15 % stops the run ("Battery low: charge, then develop the rest").
  - A frame that fails (no file, bad CR2, decode or write error) becomes E, is logged, and the run goes on.
- **Log** (wf_log ring): one line per frame, e.g. `dark F01 IMG_0206 iso 320: load 6.1 expose 31.2 fix 7.4 dry 10.3
  s, 9.6 MB, wb as-shot, trim +0.0, black 2047 white 12636`, plus start / stop / memory lines.
- **Time per EOS M frame** (memsim ARM946E-S 200 MHz, plus the host chain):

  | step | time |
  |---|---|
  | CR2 decode | 5.3 s (cached input; 9.2 s uncached) |
  | develop B | 29-36 s |
  | look stage | as at shot time |
  | encode q95 | 9.7 s |
  | card | about 22 MB read + 7-10 MB written |

  That is about 50-60 s a frame, so a 36-frame roll takes about 35 min. Host check: `tools/dr_dark_check.sh`, the
  whole chain on the Day09 pairs (prints decode with djpeg and PIL, the encoder at 44 dB Y against its own frame, the
  mid-tone level against Canon's JPEG).
- **UI** (`ui/wf_dark_ui.c`; host renders `tests/ui/dr_run.sh`):
  - a safelight-red room;
  - the easel with the print fading in (palette only);
  - the phase names;
  - the wire with a 700 ms slide;
  - a dial with progress ticks and a seconds hand;
  - "n to go - about m min", battery % and clock.

### 3.7 Bloom on its own row (RAWB11, owner 2026-11-15)

"tách cái bloom ra 1 UI riêng nhé để chọn các mức độ bloom nữa, bạn đừng gắn với halation nữa".
- **Two settings.** Halation `wf_halo` 0..2 = Off / Film / Spektra (the catalogue's profile or its spk profile).
  Bloom `wf_bloom` 0..3 = Off / Low / Normal / Strong: bloom_q8 0 / 256 / 512 / 896 of bloom_frac 0.06. Normal is the
  old "+ bloom" (512 since Strength v2), Low half of it, Strong a clear step that keeps the frame (the bloom strip,
  `tests/ui/bloom_strip.sh`; GUESSES like the rest). Bloom is a soft glow in the light's own colour (M_k's identity
  part); halation is the red edge from the base (its `hw x xt` part).
- **One stage.** Either one on runs `yh_halo` with the matrix built from the two parts (`hal_q8` 0 when Halation is Off;
  knee / boosts from the film's profile either way). Both off: no key, no stage, no memory (the old Off). The daylight
  half-strength ramp (`yh_set_hot` hs) scales the halation part only, as before.
- **Key.** `wf_fx_hk` = (set + 1) | halation << 6 | bloom << 8 (`WF_FXK_HALO` / `WF_FXK_BLOOM`).
- **Rows / menu.** `WF_ROW_BLOOM` right after Halation, the same availability (3:2, a post code), its own icon (a
  solid core in a thinning dither); ML menu entry "Bloom" under "Halation".
- **Config.** `halation=off|halation|spk`, `bloom=off|low|normal|strong`. An old file: `halation=bloom` = Film + Normal,
  `spkbloom` = Spektra + Normal, `halation` / `spk` = Bloom Off, unless the file has its own `bloom=` line. Defaults:
  EOS M Film + Normal (the old "Film + bloom"), 5D2 both Off.
- **Text.** Ready line `fx halation neg x1.25, bloom normal ready` / `fx halation off (neg), bloom low ready`; the shot
  line names the stage by what ran (`+ halation`, `+ bloom low`, `+ halation+bloom strong`); EXIF / print comment
  `| Halation Film | Bloom Normal`; the Darkroom's `dark: look ready ..: halation + bloom normal`. A roll's prints use
  the Halation and Bloom settings of the run, as before.
- **Names since the film-stage merge** (15 Nov 2026, print-frame, DESIGN 1a): Halation Off / Wide / Tight / Wrap
  (`wf_halo` 0..3; Wide = Film above, Tight = Spektra, Wrap = the light wrap, config `gen`), a Gauge row 16 / 35 / 65 mm
  that scales grain and the halation stage (key bits `WF_FXK_HGAUGE` / `WF_FXK_GGAUGE`), and Grain Off / Fine / Normal
  / Strong / Even Fine / Even Normal / Even Strong. The text reads `| Halation Wide | Bloom Normal`, `| Gauge 16 mm`
  when not 35 mm.

### 3.8 Mist, checked (owner 2026-11-15)

"kiểm tra luôn cái mist đi bạn". The model (`wf_mist.h`) is unchanged: k 0.08 / 0.15 / 0.28 / 0.45 for 1/8 .. 1 (white
and black alike; v2 of 11-12), three Gaussians (sigma 31 / 124 / 467 px), white weights 0.25 / 0.35 / 0.40, black
0.55 / 0.35 / 0.10 with its source knee, the hot ramp from code 250 (x8 at EV <= 7, x1 from EV 13). What the check
changed and found:
- **Chroma rounding.** U and V were shifted down by up to one code on every pixel (a plain `>> 24` of a signed product):
  the dark of a frame -0.5 with white mist, -1.0 with black, a faint green cast. Rounded now (SMLAL onto 2^23).
- **The halves' mirror** (Diana halves on the EOS M, every 5D2 shot: the seam at frame row 1872). The TOP guesses the
  rows below the seam from its own: until 11-15 a mirror eroded over +-2 cells. A light ON the seam lost 2 cells a side,
  and the BOT's pull drew two dark lobes under it (-32 / -64 codes at white / black 1 against the whole frame); a light
  24 px above the seam still left +33 / +41 below it. Now a light is mirrored only where it reaches the seam (per
  column, from the seam row outwards, a cell over code 250's light is taken only while no brighter than the one taken
  before it): +-1.5 codes in both cases. Sky and walls mirror as before, so a horizon on the seam still lays a skirt
  of glow over the ground below it (+41 / +43 at white / black 1, as before; the TOP cannot see below the seam).
- **The shot line** split in two when it would pass 110 characters (mist + halation + bloom + grain + halves): the
  second line `shot #n ... (TOP .., BOT ..) EV12` keeps the EV.
- **Order.** The 8-bit path: mist on Canon's words, before the colour kernel (film table or cube). RAW develop B and
  the Darkroom with Log + LUT: the cube is in the develop, so mist runs on the graded codes, after the cube (with
  Film output it runs before the film table, as on 8-bit).
- **Strength on photos** (`tests/ui/mist_strip.sh`). The veil is additive in linear light, so how much the dark lifts
  depends on the scene: a backlit tree (47 % sky, IMG_0218) lifts deep blacks from Y 10 to 33 at white 1/8 and to 76 at
  white 1 (black 1/8 +17); a dusk street (19 % sky, IMG_0220) +7 at white 1/8, +25 at white 1, +2 at black 1/8. Light
  cores give up their own light (1 - k): -2 at 1/8, -16..-20 at 1. Left as tuned (k was raised x2..2.7 after the
  owner's camera test of 11-12).

### 3.9 Print finishes: frame and date stamp (owner 2026-11-15, EOS M Darkroom only)

Two optional finishes on a Darkroom print (`src/wf_print.c`, pure code; `wf_dark.c` does the I/O). Both Off (the
defaults) = the print is byte for byte today's (`tools/dr_dark_check.sh` compares the files).
- **Settings.**
  - `wf_pr_frame` 0..4 = Off / White / Black / White wide / Black wide.
  - `wf_pr_info` 0..2 = Off / On / On + roll; it needs a frame, so it is greyed while Frame is Off.
  - `wf_pr_date` 0..3 = Off / 'YY MM DD / DD MM 'YY / MM DD 'YY.
  - Config `frame=off|white|black|whitewide|blackwide`, `info=off|on|roll` (default on), `date=off|ymd|dmy|mdy`.
  - ML menu "Print frame", "Print info", "Date stamp" under "Darkroom" ("Camera JPEG", below, is the fourth).
  - Darkroom screen: a PRINT block of three rows (four since RAWB21, five since RAWB27: RAW files, below). UP / DOWN
    pick a row; LEFT / RIGHT / the wheel change it; this works
    while idle, stopped or failed, not during a run. The easel shows a sketch of the finished print while no print is
    on it. A run takes a snapshot of the three settings at start (`dark: roll .. print frame f info i date d`).
- **Frame.**
  - The canvas is laid out as the viewer shows it, then mapped back into the stored image for Orientation 3 / 6 / 8,
    so the caption sits under the picture and turns with it.
  - Border at a 3456 px short side (scaled with it): 112 px (about 3 %), wide 256 px. With Info on, the caption adds
    464 px to the bottom border.
  - Every offset and size is a multiple of 16 in the stored image, so the picture's 16 x 8 MCUs are the same blocks.
    The host check shows the picture's luma inside a framed print equals the plain print's, value for value.
  - The canvas is made band by band while the encoder streams: the frame colour, the picture's own rows and the
    caption band (rendered once). There is no downscale and no second frame buffer.
  - The APP1 Exif PixelX / YDimension are rewritten to the canvas size (`wf_dk_dims`). The orientation is kept.
  - White frame Y 255 with captions Y 30 / 84; black frame Y 12 with captions Y 236 / 196 (lines 2 / 3 and the roll
    were Y 118 / 150 up to RAWB26; owner 11-18 "sửa info exif cho font chữ nổi hơn tí"). No logo from the repo:
    the maker's wordmark comes only from the owner's card-time pack (Logo, below).
- **Caption** (`wf_pf_caption`, from `wf_dk_shot`: the Exif of the print's APP1, gaps filled from the CR2's own tags).
  - Line 1: camera / lens (84 px, medium). The lens is Exif LensModel (0xA434). The 5D2 writes Exif 2.21 without it,
    so Canon's MakerNote 0x0095 is read then (`wf_dk_mnlens`, Make Canon; owner 2026-11-21 on a 5D2 print: "ko hiện
    canon 50mm STM à"). The 5D2 names the lenses it knows there ("EF85mm f/1.8 USM" in the owner's 100EOS5D files) and
    writes only the focal length for others ("35mm" in Day 10, LensType 255): such a name is left out, line 2 has the
    focal length. What the 5D2 writes for the EF 50mm f/1.8 STM (2015, newer than its firmware 2.1.2) is not known yet:
    no 5D2 file with it here (the EOS M writes "EF50mm f/1.8 STM", LensType 4156).
  - Line 2: focal length, f-number, shutter, ISO (62 px, wght 500 since RAWB27, was 400).
  - Line 3: the film, then Print / Grain / Halation / Bloom / Mist from the look's text (62 px, the same).
  - On + roll adds `ROLL 021 · 07/24` at the right of line 1.
  - Items that do not fit the picture width are dropped from the end: line 1's lens, then line 3's effects; the film
    name is cut last.
- **Font pack** `ML/DATA/WFPRINT.DAT` (`tools/mk_wffont.py --print-only`, own format "WFPF" v1, 169,746 B, in the
  tool's header). Ubuntu Sans at 84 px wght 500 and 62 px wght 500 (400 up to RAWB26: the same size, so the cap
  height, the lines' places and the logo's size do not move), 4-bit anti-aliased, ASCII plus the middle dot,
  with kerning, FNV-1a checked. Pack missing or bad: frames are printed without the details, and the log says so once
  per run.
- **Date stamp** (`wf_pf_stamp`).
  - Orange seven-segment digits like a film camera's date back, drawn from their geometry. The segments are hexagons
    with 45-degree tips, slanted 8 degrees, with a soft core and a glow, screened over the picture. The colour turns
    red-orange at the rim.
  - The digit height is 3.6 % of the short side. The stamp sits at the bottom right of the picture as shown (all four
    orientations), inside the picture, frame or no frame.
  - The date comes from the APP1's DateTimeOriginal, else the CR2's. With no date the stamp is skipped and logged.
- **Logo** (owner 2026-11-16, RAWB21). The camera maker's wordmark in a framed print, beside our own lettering of the
  model.
  - The wordmark is the maker's trademark. Its SVG stays in the owner's private folder
    (`F:/MagicLatern/_windyfilm_docs/private/logo/`, out of git; `.gitignore` has `*wordmark*` and `*.DAT`).
    `tools/mk_card.sh LOGO=<svg>` makes the pack `ML/DATA/WFLOGO.DAT` at card time with `tools/mk_wflogo.py` (own
    format "WFLG" v1, in the tool's header, FNV-1a checked). Without the pack on the card the caption is text only,
    as before. The repo's tests use our own test shape (`tests/host/logo_test.svg`), never the owner's file.
  - Pack: image 0 = the wordmark rasterised from the SVG's paths (192 px tall, 8 x 8 supersampled coverage), keyed by
    the maker ("Canon"); then one lettering per Exif Model ("EOS M", "EOS 5D MARK II", " MARK II" at 0.64 size), drawn
    by the tool from our own monoline geometry. We do not trace the maker's model logotypes.
  - Drawn only on a shot whose Exif Model starts with the maker's key. A model with no lettering in the pack keeps the
    model name as text, without the maker's word ("EOS 6D").
  - With a caption (Info On): the wordmark sits right-aligned in the caption, its top on line 1's cap line, its bottom
    clear of line 2's caps; the lettering is under it on line 2's baseline, no wider than the wordmark. Line 1 drops
    the model (the lettering names it); lines 1 / 2 end before the logo; "ROLL 021 · 07/24" moves to the right of line
    3.
  - Without a caption (Info Off): the wordmark (2/5 of the bottom border tall) and the lettering side by side,
    centred in the bottom border.
  - Colours: white frames the maker's red #BF1920 (Y 76, Cb -24, Cr +82); black frames the wordmark light (Y 228),
    as on the camera body. The lettering takes the caption's line 1 tone.
  - The maps are area-resampled (Q12 taps, at most 10x down) into scratch, then screened by the band code like the
    caption. The 16-px grid and the picture's own blocks are unchanged (the logo is in the border only).
- **Camera JPEG** (owner 2026-11-16, RAWB21): Keep (default) / Overwrite / Over + print (2026-11-17) / Print + over
  (2026-11-20, RAWB28). Shoot RAW+L quickly; at the Darkroom the
  print also replaces the shot's own JPEG in DCIM, so the camera's playback and a card reader show the print. The roll
  copy in `ROLLS/ROLL00n/` is always written first.
  - Config `jpeg=keep|overwrite|both|printover` (`jpeg2=prefix|folder` still read and saved, unused since RAWB28); ML
    menu "Camera JPEG" under "Darkroom"; the
    fourth PRINT row on the Darkroom screen
    (icon: an arrow into a sheet). Snapshot at start: `... jpeg j logo nK` in the start line.
  - Order per frame: roll print written, read back and checked; then `IMG_nnnn.WFT` in the same DCIM folder; read
    back and checked; then renamed over `IMG_nnnn.JPG` (`wf_frename`). The CR2 is never touched. Any step failing
    leaves the camera's JPEG (or, if only the rename failed after its removal, the print in the .WFT, logged).
  - Check = FNV-1a of every byte outside the APP1 while writing, compared on read-back, plus the APP1 bytes compared.
  - Frame Off: the .WFT is the roll print's bytes. Frame On (owner 2026-11-17, RAWB23: the DCIM file "has the film
    colour but no frame, date, logo or caption"): a second encode of the whole print fitted into Canon's 5184 x 3456
    with the Exif size set back, so Canon's playback sees the size it made (a bigger canvas could be refused).
    `dk_fit` (`src/wf_dark.c`): `wf_pf_fit` lays the frame out natively round the biggest picture (3:2 kept, at least
    half size) whose canvas fits: landscape thin frame with the details = picture 4320 x 2880 in a 4512 x 3456 frame,
    portrait = 4560 x 3040 in 5168 x 3232 (shown 3232 x 5168); borders, caption band and logo at that size, the print
    fonts at their own size (the text reads as on the print; a line that no longer fits the narrower picture drops
    its last items, as on the print). `wf_pf_pad` centres it on 5184 x 3456, the margin in the frame colour. The
    caption band and the logo maps are drawn again from what `dk_finish` left in the scratch (fonts and logo pack in
    the 512 KB pack area, the shot, the roll text); the picture, date stamp included, is area-resampled from the
    developed frame (`wf_pf_scale`, `src/wf_print.c`: Q12 overlap weights, each source row resampled across once, 3
    rows cached, ~97 KB). Chosen over shrinking the finished 5408 x 4144 print: the text and logo are drawn sharp at
    their final size, and only the picture is resampled. Memory: the roll print is written and checked by then, so
    its band, row buffer and maps are reused (the fitted band is smaller); `dk_fin_need` also counts the fitted
    print's need with Overwrite. No fit or no room: the picture alone at 5184 x 3456, as before, logged.
  - Exif thumbnail (both the roll print and the DCIM file): Canon's 160 x 120 IFD1 JPEG is replaced by ours of the
    print (`wf_dk_xthumb_*`, `src/wf_darkp.c`): the band rows are averaged into 160 x 120 while encoding, encoded at
    q90 down to q40 until it fits the slot (the IFD1 JPEG to the APP1's end when only zeros follow), our JFIF APP0
    dropped, the rest zero-filled; the APP1 is rewritten in place at offset 2. Too big at q40 = Canon's kept.
  - **One JPEG in DCIM** (owner 2026-11-20, RAWB28, approved with the mock images `_windyfilm_docs/rawb28_ui/`):
    DCIM keeps one JPEG per shot, the one Canon's PLAY shows; the print is always the roll's copy in `ROLLS/`, never
    copied again. With a frame (`dk_camjpg`, `src/wf_dark.c`):
    - **Over + print**: `IMG_nnnn.JPG` becomes the picture alone at 5184 x 3456, date stamp in it (`DK_CJ_PIC`).
      Nothing else is written.
    - **Print + over**: `IMG_nnnn.JPG` becomes the print fitted into 5184 x 3456 (`DK_CJ_FIT`, as Overwrite), and the
      picture alone at 5184 x 3456 goes into the run's ROLLS folder under the shot's own name:
      `ROLLS/ROLL021/IMG_0208.JPG` beside `F01_0208.JPG` (and beside the CR2 that Move to ROLLS puts there; a develop
      again writes into ROLL021B like its prints). The print first, then the picture; a stop in the first skips the
      second. Both go through `dk_dcim_put` (a .WFT of the target's name beside it, read back, renamed onto it); the
      second encode costs about as much as the first (~35 s a frame on the EOS M, logged per file).
    - Frame Off: the print is the picture, so both are Overwrite (one file).
    - The Darkroom screen says where the files go (`dr_where`, `src/ui/wf_dark_ui.c`): while Camera JPEG or RAW files
      has the focus, the idle block's two lines read "PLAY (DCIM): Canon's JPEG / the picture / the print[ + CR2]" and
      "ROLLS/ROLL021: the print (always)[ + the picture][ + CR2]" ("ROLLS: ..." when too wide), from the values now.
    - Up to RAWB27 Over + print also wrote the print into DCIM as a second JPEG, `WFP_nnnn.JPG` beside the pair
      (config `jpeg2=prefix`) or `NNNWFILM/WFP_nnnn.JPG` (`jpeg2=folder`), named by `wf_roll_jpg2` (`src/wf_roll.c`).
      Both repeat a DCF file or folder number, which Canon's playback may merge or hide; the owner dropped it for
      Print + over. `wf_roll_jpg2` is kept (unused by the run, `tests/host/wf_roll_test.c` still checks it); the
      DCF reasoning is in git history (this section up to commit 8a5fee1).
  - **Playback of an overwritten RAW+L pair** (owner 2026-11-17, RAWB24/25: "sao jpg nó overwrite rồi mà preview vẫn
    là ảnh chưa qua xử lí"; still Canon's colours after a power cycle, so no cache from the time of the shot).
    - What a real EOS M pair holds (`tools/jpg_pictures.py` on IMG_6839 and IMG_6851, firmware 2.0.2). The JPEG: two
      pictures, the main image (5184 x 3456, Y 2x1) and Exif IFD1's 160 x 120 thumbnail; no MPF APP2, no MakerNote
      preview (0x00B6), no bytes after the EOI; a second APP1 is XMP with `xmp:Rating` only. The CR2: four, IFD0 a
      5184 x 3456 JPEG, IFD1 a 160 x 120 JPEG byte for byte the JPEG's, IFD2 660 x 441 uncompressed 16-bit RGB, IFD3
      the raw data.
    - Overwrite replaces both pictures of the JPEG. ROLL:jpeg "pictures" checks it on every overwritten file: two
      image starts only, no APP2, nothing after the EOI, the thumbnail 160 x 120 Y 2x1 like Canon's, neither Canon's
      thumbnail nor the start of Canon's scan anywhere in the file. Our encoder writes Canon's own layout (SOI, APP1,
      one DQT with both tables, SOF0 Y 2x1, one DHT, SOS), so the format is not a reason to refuse it.
    - So the Canon picture on the screen can only come from the CR2: for a RAW+L pair the playback draws the CR2's
      own previews (by elimination; HUONGDAN step ai proves it on the card: the CR2 moved out on the PC, the frame
      then shows the film look). The CR2 stays byte for byte as shot (the rule), so nothing of ours changes.
    - Options for the owner: as now (a pair shows Canon's look on the camera; the film look is the JPEG on the PC
      or phone, and ROLLS), or an opt-in that moves each developed frame's CR2 into its `ROLLS/ROLLnnn/` so DCIM holds
      the JPEG alone and playback shows it. The owner took the second (17-11, RAWB26): "RAW after Darkroom" below.
      Not proposed: hiding the CR2 by name or attribute (PC software would not read it), our previews written into
      the CR2 (the rule), a second CR2 (the card's space).
  - **RAW after Darkroom** (owner 2026-11-17, RAWB26: "Thêm tuỳ chọn chuyển CR2"; both cameras). An ML menu row after
    Camera JPEG: **Keep in DCIM** (default) / **Move to ROLLS**; config `cr2=keep|move`; `wf_pr_cr2`, taken at the
    run's start like the other print settings (`dk_pcr`). Since RAWB27 (owner 11-18: "RAW after darkroom cho chọn lúc
    tráng film đi bạn, chứ bật thế hơi lâu") also the Darkroom's fifth PRINT row **RAW files** (`WF_PO_RAW`, the same
    `wf_pr_cr2` through `wf_popt_*`, so the menu row and the screen row always agree): values "Keep in DCIM" / "Move to
    ROLLS"; with Camera JPEG Keep it is greyed (`wf_popt_avail`) and shows "needs Overwrite" in place of its value, no
    arrows, and LEFT / RIGHT / the wheel there only say "Camera JPEG is Keep: the CR2 files stay in DCIM" (a message
    under the idle block, `DR_MY` 410, gone with the next key). Icon: a folder with an arrow down in it. The idle
    block's two lines move one row down (`DR_IY` 365 / 387). The EOS M and the 5D2 draw the same screen (CAM=5D2
    dr_run: the same pixels); the shelf's rolls and a develop again show the same five rows.
    - When: Move, Camera JPEG not Keep, and that frame's JPEG(s) written and read back (`dk_camjpg` says so:
      `IMG_nnnn.JPG`, and with Print + over and a frame also the picture in ROLLS). Otherwise the CR2 stays and one
      log line says why: Keep (the run says it once at its start), a frame shot RAW only, a JPEG without Canon's Exif,
      a print that did not read back, a stop in the camera JPEG, the picture in ROLLS not written.
    - Where: `ROLLS/ROLLnnn/IMG_nnnn.CR2` (`wf_roll_cr2_moved`): Canon's name, the roll's first folder for every
      develop pass (ROLL021, not ROLL021B), so one folder holds a roll's CR2s.
    - How, bytes unchanged. EOS M (its ML core defines `CONFIG_FIO_RENAMEFILE_WORKS`: `FIO_RenameFile` is Canon's
      own): `wf_frename`, ok when the source is gone and the target has the source's size; a rename that leaves the
      source in DCIM falls back to the copy. 5D2 (no such config: ML's `FIO_RenameFile` there is `FIO_CopyFile` +
      `FIO_RemoveFile`, nothing read back): the copy. The copy: the CR2 through `M.dma` (256 KB chunks) into
      `wf_fopen_w(WF_FO_NEW)`, the size checked, then both files read back and compared chunk by chunk (`M.in` holds
      the source's chunk: the decoder's input, free after the decode), and only then `wf_fremove` on the source. Any
      failure (open, create, read, write, size, read back, not the same, a stop) removes the copy and keeps the CR2.
      A target there already: the same bytes = the source removed, else both kept. All inside the run (wf_task),
      through wf_fio.h only (W13, W24); no new imports (W11: FIO_OpenFile / ReadFile / WriteFile / CloseFile /
      GetFileSize are imported already; no memcmp, `dk_neq` compares).
    - Found again: `dk_frame` reads `ROLLS/ROLLnnn/` when the DCIM CR2 is gone (`dark: F01 IMG_0208.CR2 read from
      ROLLS/ROLL021/`); the shelf and develop again count a frame whose CR2 is in either place (`dk_has_cr2`); a CR2
      in neither is skipped as before.
    - Canon's side, unchecked on the camera (HUONGDAN step aj, 5D2 step m). File numbers: the JPEG keeps its number in
      DCIM, so the highest number in the folder does not change (Continuous numbering keeps its own counter; Auto
      reset sees the same highest file). Playback: whether its list notices a CR2 that ML removed before a power cycle
      is not known for DryOS; the step looks at PLAY right after the run and again after a power cycle. (Owner
      2026-11-21, 5D2: it does not; "Canon's PLAY after a run" below.)
    - Costs (told to the owner): an importer that pairs RAW + JPEG by folder sees the JPEG alone in DCIM; Canon's erase
      takes only the JPEG; the 5D2 copy reads the CR2 twice and writes it once (about 25 MB a frame; the time is in the
      log line); a rename across folders on DryOS is first tried by the card run.
  - Not checked on the camera yet: PLAY after Print + over (the print in DCIM, the CR2 still beside it unless moved,
    so 3.9's playback note applies), and the time of the second encode on the 5D2.
  - **Canon's PLAY after a run** (owner 2026-11-21, 5D2: after a Darkroom run some prints show in PLAY and some do
    not, the files on the card whole; a power cycle shows them, and so does the card out and in). PLAY lists the card
    from Canon's file catalog, filled at card mount; FIO changes files without telling it, so a JPEG a run replaced
    or a CR2 it moved keeps its old entry. The fix tells the catalog after each frame:
    - `dk_dcim_mark` (wf_dark.c) notes that a frame is about to change a file under `/DCIM/` (the camera JPEG's
      rename in `dk_dcim_put`, a CR2's rename or removal in `dk_cr2_move`). After the frame, `dk_cat_tell` calls
      `wf_cam_cat_tell(<the frame's DCIM JPEG>)` (camera layer): `FM_GetObjectHandleByPath(path, &handle)` turns the
      DCF path into Canon's object handle, `FC_ReScanFile(handle)` locks the catalog, finds every file of that folder
      and number again (which of JPG / CR2 are there, size, time) and unlocks (0 = done). The JPEG is always there
      when its CR2 moved, so one call covers both. Addresses: 5D2 2.1.2 0xFF8453FC / 0xFF856A64, EOS M 2.0.2
      0xFF118EB0 / 0xFF1167B0 (the owner's ROM dumps, by their debug strings; the same code on both bodies).
    - Guards: model + firmware, the two prologues and the literal of the file manager's ready word (5D2 0x1ED8,
      EOS M 0x3E4E4), checked once; that word != 0 (FM_GetObjectHandleByPath asserts otherwise); the path of the DCF
      shape, 29 chars (it is copied into a 48-byte stack buffer). The Darkroom runs in wf_task: never a hook.
    - The first success logs `dark: F01 Canon's file list read again (PLAY shows the new print), N ms`; a failure
      logs `dark: Fnn Canon's file list not read again (<body | no FM | path | Canon> <code>): ...` and sets
      `wf_dark_dcim_changed()`. Only then the old way out shows: the DONE screen's two lines "Card out and in (or
      power off/on)" / "so Canon's PLAY shows the new prints", and `wf_dark_play_notice()` (wf_dark.h), a 3 s
      NotifyBox "PLAY may miss new prints:" / "Card out and in, or power off/on" when PLAY goes to Canon with the UI
      closed and the camera not in playback (wf_main.c `wf_keypress`; the key still goes to Canon). RAM only.
    - FC_ReScanFile has no caller in either firmware (it is there, complete, with its log lines), so the first
      camera run is its test: PLAY right after a run, without a power cycle, should list every print.
    - Checks: `test.sh` ROLL:jpeg: the Keep roll asks nothing; the Overwrite roll asks once per frame with the frame's
      `B:/DCIM/100CANON/IMG_nnnn.JPG`, the rescan gets the handle, no notice; the framed roll with Canon refusing
      (0x82110007) logs each frame that changed DCIM (not F03, which has no camera JPEG) and shows the notice into
      PLAY, none out of it. `tests/ui/dr_ui_test.c` 9b: the flag adds the two lines, no other pixel.
- **Memory.** Nothing new is allocated: the pack (<= 512 KB), the caption band (<= about 3.7 MB) and the row buffer
  (about 197 KB) use `M.raw`, which is free scratch in the DRY phase. The stamp's map (about 148 KB) uses `M.raw` too.
  Fonts / text are static (about 1 KB BSS). The logo pack (<= 384 KB) is read beside the fonts in the 512 KB pack
  area; its maps and resample tables (about 90..100 KB at 5184 x 3456) go after the row buffer, so the scratch asks
  for 192 KB more only when a frame is on and WFLOGO.DAT is on the card. The Exif thumbnail's sums, JPEG and encoder
  buffer use `M.in` (free in DRY).
- **Text.** `dark: F07 frame white o1 5408x4144 (Exif size set), band .. KB, ..ms`,
  `dark: F07 caption <line 1> | <line 2> | <line 3>`, `dark: F07 date stamp 06 03 '24 o1 ..ms`,
  `dark: F07 logo Canon 529x111 + lettering Canon EOS M, .. KB`, `dark: F07 print Exif thumbnail 160x120 q90 .. B`,
  `dark: F07 camera JPEG: the print fitted 5184x3456, picture 4320x2880 at 432,96, logo`, `dark: F07 camera JPEG
  caption ...`, `dark: F07 camera JPEG IMG_0208.JPG overwritten: the print fitted 5184x3456, .. KB, ..ms` (or `the
  print` with Frame Off, `the picture 5184x3456` when it could not be fitted); every refusal names the step and the
  file kept. Over + print: `dark: Camera JPEG Over + print: the picture over IMG_nnnn.JPG, the print in ROLLS only`
  at the start, then per frame `... IMG_0208.JPG overwritten: the picture 5184x3456, .. KB, ..ms`. Print + over:
  `dark: Camera JPEG Print + over: the print over IMG_nnnn.JPG, the picture as ROLLS/ROLLnnn/IMG_nnnn.JPG`, then `... IMG_0208.JPG overwritten: the print fitted 5184x3456, .. KB, ..ms`, `... ROLL021/
  IMG_0208.JPG written: the picture 5184x3456, .. KB, ..ms` and `dark: F07 camera JPEG: the print in DCIM written,
  the picture in ROLLS written, ..ms` (either `not written`). Frame Off: `dark: Camera JPEG Print + over: frame Off,
  so as Overwrite (one file)`. RAW after Darkroom: `dark: RAW after Darkroom Move to ROLLS:
  each CR2 goes to ROLLS/ROLL021/ once its frame's JPEG is in DCIM` at the start (or `..., but Camera JPEG is Keep: the
  CR2s stay in DCIM`), then per frame `dark: F07 IMG_0208.CR2 moved to ROLLS/ROLL021/ (renamed, .. KB, ..ms)` (5D2
  `(copied and read back, ...)`, EOS M fallback `(rename failed: copied and read back, ...)`) or `dark: F07
  IMG_0208.CR2 kept in DCIM: its JPEG was not written` / `...: the copy to ROLLS/ROLL021/ failed (not the same)`, and
  on a later pass `dark: F07 IMG_0208.CR2 read from ROLLS/ROLL021/`.
- **Checks.**
  - `tests/host/wf_dark_test.c`: layouts for every frame / info / orientation, Exif parsing in both byte orders,
    caption and date text, the pack, band text, stamp place and colour, frame-Off identity. A `finish` mode prints
    one CR2 with given settings.
  - `tools/dr_dark_check.sh` (FIN=1): Off = plain print byte for byte on three frames, then samples checked by
    `tools/dr_fin_check.py`: canvas size, picture luma = plain, stamp at the bottom right as shown, frame corners,
    Exif size and orientation; with the test logo pack the red (white frames) or light (black frames) pixels outside
    the picture, caption and border placements, EOS M and 5D2 lettering. `tests/host/wf_dark_test.c` units: the pack
    parser (bad magic / FNV / sizes, maker key), placement, maps, the thumbnail slot and put.
  - `test.sh` ROLL:jpeg (real RAW+L pairs through the module under qemu): Keep leaves DCIM byte for byte; Overwrite
    with Frame Off = the DCIM JPEG is the roll print's bytes; with a frame + logo = the DCIM JPEG is the print fitted
    into 5184 x 3456 (the layout lines: picture 4320 x 2880 at 432,96 landscape, 4560 x 3040 at 104,208 portrait, the
    logo) with the Exif size set back while the roll print is framed full size; `wf_pf_fit` / `wf_pf_pad` for every
    frame / info / orientation, `wf_pf_scale` within 1.25 of the exact area mean (two roundings), chroma flat, half
    size and short memory refused; `WF_OW_OUT=dir` keeps these DCIM files and adds a black frame roll (the owner's
    preview images); no .WFT left, CR2s unchanged, a missing JPEG is logged and
    skipped, both files carry our Exif thumbnail. Over + print (black frame, `jpeg2=folder` set and ignored): each
    `IMG_nnnn.JPG` the picture at 5184 x 3456, no picture in ROLLS, no `WFP_` file and no `100WFILM/`. Print + over
    (white frame): each `IMG_nnnn.JPG` the print fitted, `ROLLS/ROLLnnn/IMG_nnnn.JPG` the picture at 5184 x 3456,
    both read by `tools/jpg_pictures.py` rules (two pictures, our thumbnail), the log lines, no .WFT, the CR2s
    unchanged. `tests/host/
    wf_roll_test.c`: `wf_roll_jpg2` names, temps, Canon's other prefixes, our own prefix refused, non-DCF names and
    short buffers.
  - `tests/ui/dr_ui_test.c` 3b: the PRINT rows (Camera JPEG the fourth, Keep / Overwrite / Over + print / Print +
    over; RAW files the
    fifth: greyed with Camera JPEG Keep, its keys say why and change nothing; with Overwrite RIGHT / the wheel step
    Keep in DCIM / Move to ROLLS and wrap; the message gone with the next key) and the easel sketch.
  - `test.sh` ROLL:cr2mv (RAW after Darkroom, both cameras, real RAW+L pairs): the row Keep by default, shown, SET
    steps it; Move with Camera JPEG Keep moves nothing; Overwrite + Move: F01's CR2 in `ROLLS/ROLL002/` byte for byte
    and gone from DCIM (EOS M renamed, 5D2 copied), F02's copy made wrong by `h_fio_wflip` (EOS M: its rename refused
    first by `h_fio_ren_fail`) leaves no copy and its CR2 in DCIM, F03 shot RAW only keeps its CR2; develop again:
    F01 read from ROLLS, nothing skipped, F02 moved by the copy (EOS M: the fallback), the pass-2 prints. A Print +
    over pass (roll 003, white frame): F01 / F03 pictures in `ROLLS/ROLL003/` and their CR2s moved; F02's picture made
    wrong by `h_fio_wflip` on `ROLLS/ROLL003/IMG_0708.WFT`: "the picture in ROLLS not written" and its CR2 kept in
    DCIM. CFG reads and saves `cr2=`; `tests/host/wf_roll_test.c` checks `wf_roll_cr2_moved`.
  - The 5D2 has no Darkroom: `wf_print.o` and WFPRINT.DAT are EOS M only (both cameras from `dark-5d2`, 3.18).

### 3.10 Paper finish (owner 2026-11-16, both cameras and the Darkroom web core)

One row, "Paper": Glossy (default) / Matte / Warm / Cool. It is the surface and the base the print is on, for every
film look at its final stage (the films, the chain films, the CinePrint cans and Leica Vivid on Log + LUT, RAW develop
B and the Darkroom); flat Log and Film LUT Off have no print, so the row is greyed there and does nothing.

- **Clean room.** Our own model from physical paper behaviour and the orders of magnitude public paper data sheets
  give (surface Dmax, base white, image tone); nothing from any other film module's code or tables.
  `tools/mk_paper.py` holds the model and writes `src/wf_paper_tab.h` (`--check` = the header is what the model makes).
- **Model.** Light L = the sRGB EOTF of the output code, display-relative (0 = the look's own black, a glossy Dmax; 1 =
  its paper white). Glossy is the identity. Matte: the surface scatters its first-surface reflection over the whole
  picture, `L' = s + (1 - s) L`, s = 0.0116 from Dmax 2.20 (glossy) -> 1.75 (matte); code 0 -> 27.9, 118 -> 120.8, 255
  stays. The flare is white, so the shadows lose a little contrast and colour, a neutral stays neutral. Warm / Cool:
  `L'_c = w_c L_c^k_c`, w the paper base (largest channel 1, so white never clips), k the image tone as a density
  scale per channel (nil at white, grows with density), solved for CIELAB targets (D65): Warm base a* 0.6 b* +3.6
  (white 255 / 250 / 244), deep tone (L 0.05) a* 1.2 b* +3.0; Cool base a* 0.6 b* -3.2 (white 247 / 249 / 255), deep
  a* 0.2 b* -2.8. The header holds 7 curves (Matte one, Warm and Cool three each) at codes 0, 4, .., 256 as code x 16;
  `wf_paper_ch` interpolates linearly (max error 0.133 code). `wf_paper_ycc16` takes a Y'CbCr x 16 value through
  R'G'B' (BT.601 full range, the Q16 matrices of `wf_dev_trim`), the curves, and back.
- **Where (all in the tables, last, after Highlights / Shadows; nothing per pixel).**
  - Films: `wf_film_build` flag `WF_FT_PAPER(p)` (bits 24-25). Each node's output (`N[y_l] + E - 128`, U', V') goes
    through the paper; N through it with the neutral axis's own colour (kept non-decreasing); E taken against the new
    N. The flag's low byte is all `t->flags` keeps, and Glossy adds no bit: a Glossy set is the set of before, bit for
    bit. The shot set's key and the preview key (`wf_pv_fkey` bits 24-25) carry the paper.
  - Log + LUT: `wf_ll_paper` after the load and the Highlights / Shadows bend. The `.FRT` cache stays unpapered (the
    paper goes on after the cache read or write). The poll's HS-only fast path (no card read) runs only when the paper
    is Glossy now and before; any paper change reloads.
  - RAW develop B and the Darkroom: `wf_dev_paper` on the develop table (every node's Y9 / Cb9 / Cr9) once per table
    build, for the cube / Log + LUT tables and the chain films' log-pack table. The SRGB base develop carries no paper
    (its film table does). The `.RBT` caches stay unpapered: their key is the look with paper 0, and the paper goes on
    after the read; a papered cube table does not owe the cache a write. `rb_key` carries the paper (chain bit 25, DISP
    bits 4-5, CUBE bits 24-25).
- **State.** `wf_paper` (`wf_paper_t()` = it in range, else Glossy); `wf_look.paper` takes it on a film or Log + LUT; config
  key `paper=glossy|matte|warm|cool` (after `sh=`; anything else reads as Glossy); EXIF / print comment `| Paper Matte`
  and the log lines (`shot #n <title> .. paper Matte`, `yuv .. p1`, `lut <cube> .. paper Matte`, `rawb: paper Matte ..ms`,
  `dark: .. paper Matte`) only when it is not Glossy.
- **UI.** ML menu entry "Paper" after Print stock; browser row `WF_ROW_PAPER` after `WF_ROW_PRINT` (`WF_NROWS` 23),
  wraps; its icon is a sheet with a lifted corner and two sheen lines (`tools/mk_wffont.py`). Host render: tests/ui
  shot `b2s_paper`.
- **Cost (memsim, ARM946E-S 200 MHz, `tools/paper_bench.sh`).** Film set +30 ms (+4.07 M instructions) on top of its
  60 ms, once per look change; `wf_ll_paper` 56.5 ms once per Log + LUT load; `wf_dev_paper` 54.9 ms once per develop
  table build (B, Darkroom). Per pixel: 0 (the shot kernels read the same tables). Glossy: 0.
- **Checks.** Host mode PAPER (the model, Glossy FNV against the HS gold, every film kind on Matte / Warm / Cool, B&W
  tone, rows / shot / log / EXIF / preview, Log + LUT with the unpapered .FRT and the fast path, flat Log / Off, cfg);
  RAWB cases (Leica Vivid Warm on the cached .RBT, a chain film on Matte); CFG (the saved line); tests/ui (the row,
  `b2s_paper`).

### 3.11 Screen Off while a roll develops (owner 2026-11-16, EOS M Darkroom)

A frame takes about 2 min, a roll many; the owner wants the LCD off meanwhile to save battery, chosen on the Darkroom
screen itself ("one UI"), at its top right.
- **Control.** One chip at the top right of the Darkroom screen (`src/ui/wf_dark_ui.c` `dr_chip`), right of the clock
  and battery: an LCD icon (lit = On, an empty outline = Off) and "Screen On" / "Screen Off". No ML menu row. Config
  `dkscreen=on|off` (default on), saved like the other rows (`wf_dk_scr`, `wf_dk_scr_step`, `src/wf_menu.c`).
- **Keys.** Idle (and stopped / failed): the chip is a focus after the PRINT rows (five since RAWB27): UP from Frame (or
  DOWN from RAW files) reaches it, LEFT / RIGHT / the wheel switch it; SET still starts the run. While a run lives the chip is
  the only control: drawn focused, LEFT / RIGHT / the wheel switch it (the rows are hidden then). Done: shown, not
  focused.
- **Run with Screen Off** (`src/wf_dark.c`). From after LiveView's pause to the run's end the display may go dark:
  `WF_DS_WAKE_MS` (10 s) after the last key on the Darkroom screen (SET counts), the UI's tick (`wf_dark_dark`) asks ML's
  `display_off` (Canon's TurnOffDisplay through ML's GUI task; EOSM_202.sym exports it), at most every 2 s. `dk_screen`
  no longer turns the display on before each frame while it is due dark. A key while dark (`wf_dark_wake`, every key of
  the screen, MENU too) only wakes it: `display_on`, 10 s again, the key does nothing else. Awake, keys work as
  before (MENU twice stops; LEFT / RIGHT switch the chip back to On). The UI draws nothing while the display reads
  dark (`wfdr_dark`, `src/ui/wf_ui.c`), and redraws on waking. Screen On = as before (the run keeps the LCD on).
- **Screen On keep-alive** (owner 2026-11-17, RAWB23: the EOS M's LCD flashed and went dark in the middle of a frame
  with Screen On and came back at the end; `dk_screen` only looked at frame boundaries, ~185 s apart). `wf_dark_keep`
  (`src/wf_dark.c`) looks every second while a Screen On run lives (`dk_running`, `dk_dark_ok`): a display that reads
  off gets ML's `display_on`. The Darkroom screen's tick calls it (UI task 0x1B, above wf_task's 0x1E, so long worker
  steps do not hold it back); the worker's band yields call it too, only when the UI has not for 3 s (UI closed by
  half-shutter). Its own give-up like `dk_screen`'s: an ask still off at the next look is a miss, 3 in a row and none
  again this run (`dark: display off while Fnn runs: turned on 3 times, it stays off (not asked again this run)`).
  Prevention: `powersave_prohibit` (already asked at the start) is asked again every 30 s. ML's
  `idle_wakeup_reset_counters` is not used: when ML's "turn display off" countdown is at 0, its reset runs ML's
  `idle_display_on`, which calls `ResumeLiveView` in the middle of the run; and ML's idle countdowns cannot blank a
  paused LiveView anyway (`liveview_display_idle` is 0 then). So the blank is Canon's own, not ML's or the power-off
  timer's. No redraw on a re-light: the bitmap keeps the Darkroom while dark, the next 1 s redraw is the usual
  double-buffered copy, and `src/ui/wf_ui.c` rewrites our palette on the first tick the display reads on again (not
  at the next 100 ms probe), so Canon's palette never shows on our pixels. The flash the owner saw is Canon blanking
  the LCD, not our code: nothing of ours draws or clears when it goes off.
- **End.** Done, stopped or failed: no more dark from the tick, an off asked under 2 s before is let land, then
  `dk_screen("after the run")` turns the display on so the result shows; `powersave_permit` then leaves Canon's own
  auto-off to Canon. Unloading after a dark run: one `display_on` (no wait).
- **5D2: the screen stays** (owner 2026-11-21, a 5D2 roll: "lúc screenoff bật lại ko vào được darkroom nữa"). Canon's
  dialog lies under the 5D2's UI (3.18: mode 2 without LiveView, 38 in it), and `dlg_lost` (`src/ui/wf_ui.c`) closed
  the UI when that mode left or the display read off. So the first dark of a Screen Off run closed the Darkroom screen
  (`ui: 5D2: gui mode 38, display 0: closing`), the run went on, and the key that woke the display showed Canon's
  screen. Now the Darkroom screen while a run lives (`wfdr_live`: the view RUN / STOPPING, which lasts until the run's
  end has turned the display on) is kept through a dark display (`ui: 5D2: gui mode N, display 0 while the Darkroom
  runs: kept`, once), and a key wakes that screen. A mode Canon left meanwhile is asked for again once the display is
  on (never while dark: Canon's menu mode lights the display), every 2 s, 3 times (`ui: 5D2: gui mode 0, asked for 38
  again (n)`); not back 2 s after the third, the UI closes as before. Idle, or after the run, a dark display or a lost
  mode closes it as before. The EOS M has no Canon dialog under the UI: unchanged.
- **Text.** `dark: Screen Off: the display goes dark 10 s after the last key` at the start, `dark: Screen Off: display
  turned off N times, woken M times` at the end. The footer while running says "Screen Off: any key wakes it for 10 s".
  Screen On: after each frame `dark: Fnn screen: relit N` (`, first off at T ms` = ms into the frame when it first read
  off), its own line because the frame's line already fills the 120 characters of a log line; at the end, when any,
  `dark: Screen On: display found off while frames ran, turned on again N times`.
- **Checks.** `test.sh` ROLL:jpeg roll 002 (a real run, the test standing in for the screen's tick and keys): shown
  after a key, dark when due (display_off), a key wakes it (display_on) and only that, dark again, no turn-on before
  the frames, on at the end, the end line. CFG: `dkscreen=` read and saved. `tests/ui/dr_ui_test.c`: focus order, the
  switch, a dark run draws nothing, MENU / RIGHT on the dark screen only wake it, RIGHT awake switches it; shots
  `dark_screen_chip_on`, `dark_screen_off`, `dark_run_screen_off_wake`, `dark_run_screen_on`. Screen On keep-alive:
  ROLL:jpeg roll 001 (a real run, the test standing in for the tick while F02 runs): on = nothing; off = turned on;
  within its second = not looked at; the ask took; stuck off = 3 asks then given up; the F01 / F02 / F03 screen lines
  (F02 `relit 4, first off at`), the give-up line and the end line; roll 002 (Screen Off) has no screen lines.
  `dr_ui_test.c`: the dark screen's tick calls it on a Screen On run. 5D2 (11-21), `dr_ui_test.c` 15b on both cameras
  (`CAM=5D2 dr_run.sh`): kept while dark, a key wakes the Darkroom and does nothing else, no ask while dark, one ask
  after the wake brings Canon's mode back, a mode lost for good gets 3 asks and then the close; the harness's Screen
  Off now turns its display off as the camera does, so step 5b checks the screen stays too. With the old `wf_ui.c`
  `CAM=5D2 dr_run.sh` fails 12 checks. Not checked on the camera: whether Canon leaves its dialog when the display
  goes off (the log's `kept` line says the mode).
- **Not checked on the camera:** that Canon keeps the display off while LiveView is paused, and how the EOS M's own
  keys reach ML while it is dark (the first key may also be seen by Canon). The 5D2 has no Darkroom: stubs only.

### 3.12 One screen: the browser's mode chip (owner 2026-11-17, branch `wf-onescreen`)

The owner wants to switch shooting modes and see the result at once, with the Darkroom shot from the same screen: the
WindyFilm browser picks the roll and its length too, and RAW develop and the JPEG path show there as well. The Roll
picker screen is gone (owner 11-17: "the original WindyFilm UI looks nicer"): every way to a new roll opens the browser.
- **Control** (`src/ui/wf_ui.c` `mode_chip`). At the browser's top right the MODE chip replaces the loaded-film chip:
  an icon (JPEG = a picture with hills and a sun, RAW develop = filter squares, Darkroom = a film strip) and the mode's
  name; with a roll in the camera it reads "Darkroom 5/24" (frames shot / exposures). It looks and works like the
  Darkroom screen's Screen chip: UP from the first row (or DOWN from the last) focuses it, LEFT / RIGHT / the wheel /
  SET step it. Messages (`say`) show in an amber chip to its left; "by WindyStrife" hides when there is no room.
  Round 2 (3.14): the top bar holds the section pill (left, in place of "by WindyStrife") and this chip; LEFT / RIGHT
  pick one of them, SET / the wheel step the chip; messages are a toast over the browser.
- **Modes.** JPEG = `wf_rawb` off, no roll; RAW develop = `wf_rawb` on, no roll; Darkroom = the Film roll flow. The
  mode is read from live state each frame (`mode_now`): a roll on the camera that is not set aside = Darkroom (whatever
  `wf_rawb` says: "both at once" stays reachable through the ML menu and the chip shows the main mode), else Darkroom
  picked on the chip with no roll yet (`mode_dk`, UI only, not kept across a reboot), else `wf_rawb`. The ML menu's
  entries stay and stay in step (the chip writes `wf_rawb`; the menu's changes show on the chip).
- **Cameras.** Darkroom is EOS M only. RAW develop needs `wf_rawb_can()` (EOS M today). The chip shows only when a
  mode besides JPEG exists: the 5D2 has no chip and its header is unchanged. (From `dark-5d2`, 3.18: the chip and all
  three modes on both cameras.)
- **Darkroom mode.** An "Exposures 3 / 6 / 12 / 24 / 36" row heads the rows (cassette icon, the chosen count in a pill;
  default 24); with a roll that has frames it shows the roll's count, greyed, "on the roll" (LEFT / RIGHT say "The roll
  in the camera keeps its count"). SET on a can loads a roll of it (`wf_roll_req_load`) by the Roll picker's rules: the
  roll's own film picked again = the load animation, no load; a roll still shooting with frames keeps its film ("This
  roll has frames: PLAY develops it (Darkroom)"); a finished roll (FULL / REWOUND with frames) brings the picker's
  close-and-load question in the browser (`draw_swap`, SET yes = `wf_roll_req_swap`, MENU no). The load button reads
  "SET Load roll", and the roll's film shows "On the roll". The PLAY hint stays ("PLAY Darkroom"). Under it since
  RAWB28 the Blind roll row (3.19).
- **Leaving Darkroom with a roll** (`mode_set`). Frames are never lost:
  - LOADED with 0 frames: the roll is taken out (`wf_roll_req_close`), "Roll 021 taken out: no frame on it".
  - LOADED with frames: a question in the browser (`draw_leave`): "Roll 021: 5 of 24 frames shot", "End the roll now
    and shoot JPEG?", the frames go to the Darkroom (PLAY develops them with the roll's film, which stays loaded until
    then), new shots are not on the roll. SET = End the roll now (`wf_roll_req_rewind`, today's rewind) and set it aside;
    MENU = Keep the roll (still Darkroom). Nothing else answers it, and MENU there does not close the browser.
  - FULL or REWOUND: no question, it is set aside at once ("Roll 021 waits for the Darkroom: PLAY develops it").
  - Back on Darkroom (the chip), the roll set aside is the camera's roll again (`wf_roll_shelve(0)`).
- **Set aside** (`src/wf_dark.c`, `wf_roll_shelve` / `wf_roll_shelved`; not the shelf of 3.15). A roll set aside keeps its frames and its
  film for PLAY -> Darkroom, but the camera shoots in the chosen mode: the shutter is not held (`roll_shutter`), the
  look is built (`wf_look_use` no longer returns `WF_LU_NONE` for it), PLAY and image review are Canon's (no blind-roll
  block), no HUD and no end notice. New shots are not frames (`wf_roll_seen` counts a LOADED roll only, and the rewind
  is carried out before the next frame poll). The browser refuses another film while a set-aside roll has frames
  ("Roll 021 waits for the Darkroom: its film stays until then"). RAM only: a roll load / close / close-and-load, the
  roll developed or closed, the chip back on Darkroom and a reboot end it. Log lines `roll: 021 set aside: ...`,
  `roll: 021 back in use (no longer set aside)`, `roll: 021 no longer set aside (close | a new roll)`.
- **The way to a new roll** (owner 11-17, `wf_ui_open_scr(WF_UI_NEW_ROLL)`, `new_roll_view`). The ML menu's "Film
  roll" and the end-of-roll notice's INFO open the browser in Darkroom mode (a roll set aside comes back into use),
  the roll's film under the cursor and the focus on the Exposures row. Asked while the browser is open (the ML menu),
  it switches the open browser. A finished roll (FULL / REWOUND with frames) leaves the row to the next roll: it is not
  greyed, it reads "next roll", and SET on a film asks to close the roll and load that count (above). The 5D2 opens the
  browser with "Rolls come to this camera later (EOS M first)". `WF_UI_SCR_DARK` = the Darkroom screen, anything else =
  the browser; the picker's code (`wf_dark_ui.c` roll section, `wf_cans.c` `wfc_draw_scaled`) and its UI checks went.
- **PLAY.** In Darkroom mode, or with a roll on the camera, PLAY goes to the Darkroom screen when there are frames to
  develop, else says why ("Load a roll first", "No frames to develop"); in JPEG / RAW develop mode with no roll it closes
  the browser as before.
- **Footer.** It follows the mode: "SET Load" / "SET Load roll"; chip focused "SET <the next mode's name>", "L/R
  Section / mode" (round 2); a question "SET Yes  MENU No" (the PLAY hint hides then). The INFO key map adds "Up from the top row: mode chip: L / R".
- **Departures from today's rewind**, for the owner: the rewound roll no longer holds the shutter or blocks PLAY while
  it waits (that is what lets the new mode shoot), and its film stays loaded until the Darkroom. After a reboot the
  shelf is gone: the roll is the camera's roll again (the chip shows Darkroom, the shutter is held as a finished roll);
  leaving Darkroom again sets it aside without a question.
- **Checks.** `tests/ui/ui_test.c` section 7 "one screen" (fake core `tests/ui/fake_dark.c`: shelf, `wf_rawb`,
  `wf_rawb_can`): the chip's focus order, modes stepping and `wf_rawb` following, the Exposures row, a roll of 24 loaded
  from the browser, the chip "Darkroom 5/24", a film refused while the roll shoots, the leave question (MENU keeps,
  LEFT ignored, SET ends + sets aside), back to Darkroom, the close-and-load question, a finished roll set aside without
  a question, an empty roll taken out, the 5D2 with no chip; shots `one_jpeg_chip`, `one_rawb`, `one_dark_noroll`,
  `one_dark_roll`, `one_leave`, `one_left_rawb`, `one_swap`; the way to a new roll (a full roll of 24, INFO's request:
  Darkroom mode, Exposures, the roll's film, "next roll", wheel to 12, SET on another film = close and load 12; the
  request while open; the 5D2's message), shots `one_from_notice`, `one_light_autowb`. `tests/ui/dr_ui_test.c` now
  drives the browser for what the picker did (shots `roll_new`, `roll_loaded_msg`, `dev_newroll_play`). `test.sh` ROLL:
  INFO on the notice asks for screen 1 (`WF_UI_NEW_ROLL`). `test.sh` ROLL `t_roll_lock`: a full roll set aside frees
  the shutter, builds the look, frees PLAY, a new shot is not a frame; back = held and `WF_LU_NONE` again; a close ends
  the shelf (`wf_roll_shelve(1)` = -1 after); a new roll fills and holds as before.
- **Not checked on the camera:** everything above (the chip, the questions, shooting JPEG / RAW develop with a roll set
  aside, PLAY going to Canon's playback then, the Darkroom developing it afterwards, the new-roll entry from the ML menu
  and the notice).

### 3.13 Light Auto = Auto WB in the Darkroom (owner 2026-11-17, branch `wf-onescreen`)

The owner: Light Auto should mean Auto WB; Day / Cloudy / Tungsten stay as they are; "not developing film, then the WB
you set". Canon's WB setting is never changed for it.
- **Darkroom** (`src/wf_dark.c` `dk_frame`). Every CR2 keeps two sets of WB levels in its MakerNote ColorData: the
  as-shot ones (short 63..66, the WB set on the camera) and Canon's Auto WB of the scene (short 68..71; colour
  temperatures at 67 and 72). Checked on the owner's 35 EOS M CR2s (ColorData 1312 entries, version 10): shot on
  Daylight, the as-shot levels stay on Daylight and the Auto ones follow the scene (IMG_0208: as shot 1935 / 1024 / 1024
  / 1792, Auto 2236 / 1024 / 1024 / 1502). `wf_cr2_parse` reads both (`wb`, `wb_auto`; Auto only for the 63 layout with
  at least 73 entries, levels out of 1..16383 = none). With the roll's Light on Auto, `wf_dk_wb_pick` hands the Auto
  levels to `wf_dk_scales`; Day / Cloudy / Tungsten keep the as-shot ones (the camera was set to them at the shot). A
  CR2 without Auto levels develops as shot, logged once a run (`dark: IMG_0208: no Auto WB levels in the CR2 ...`). The
  frame log's `wb` reads `auto`, `as-shot` or `daylight` (no levels at all).
- **Shots** (JPEG path and RAW develop B) are unchanged: they keep the WB set on the camera. B develops from the raw in
  memory at the shot, before Canon writes the CR2, and Canon's Auto levels are only in that file; the JPEG path works
  on Canon's YUV, already balanced. Neither can rebalance to Auto inside our own processing today, and the camera's WB
  is not touched to get it.
- **Name** (round 2, owner 11-17). The Light row reads Auto / Day / Cloudy / Shade / Tungsten on both cameras ("Auto
  WB" of round 1 is "Auto" again; EOS M help "Auto: Darkroom prints use Canon's auto WB (shots: yours). Day, Cloudy,
  Shade, Tungsten."). The config keys are `light=auto|day|cloudy|shade|tungsten`. Shade sets 1.45 x the stock's
  daylight kelvin, rounded to 100 K (5200 -> 7500 K; a T stock, Day = 3200 K, -> 4600 K); Cloudy stays 1.25 x.
  Day / Cloudy / Shade / Tungsten set the camera's kelvin as before; Auto never changes the camera's WB.
- **Checks.** `test.sh` LIGHT: the row's name on each camera ("Auto" on both since round 2) and the Shade step.
  LIGHT:awb (EOS M): a synthetic CR2 head (ColorData 1312,
  as shot on Daylight, Auto different) parses both sets; Light Auto picks Auto and the scales' R/G and B/G follow it;
  Day / Cloudy / Tungsten keep as shot; bad Auto levels and the 582-entry layout fall back to as shot; the owner's
  IMG_0208.CR2 gives the levels above. ROLL:mem: the real RAW+L pairs develop with `wb auto` (default Light Auto).
- **Not checked:** the 5D2's CR2 layout (no 5D2 CR2 on disk; the 5D2 has no Darkroom), the prints' colour on the
  camera.

### 3.14 Film | Film stage: two sections in the browser (owner 2026-11-17, round 2, branch `wf-onescreen`)

The owner's mock `_windyfilm_docs/onescreen_ui/MOCK_film_filmstage_flow_v2.html` (left as it is) splits the browser
into the Film section (the film looks on Canon's JPEG) and the Film stage section (the chain films: a negative printed
on a print stock), with JPEG 8-bit / RAW develop / Darkroom in Film stage, so the 8-bit JPEG path and RAW develop can be
compared, the develop time shown.
- **Section pill** (`src/ui/wf_ui.c` `sec_pill`, `sec_set`, `sec_apply`). "Film | Film stage" at the top left in place
  of "by WindyStrife", the section in white; the mode chip stays at the top right. The top bar is one focus stop above
  the rows (UP from the first row, DOWN from the last; `FOC_TOP`, `tsub` 0 pill / 1 chip): LEFT = the pill, RIGHT = the
  chip, SET / the wheel / the joystick press toggle the pill or step the chip, DOWN goes back to the rows. The section
  is the selected film's; the pill selects the other section's last tab and film (its first tab with films the first
  time). The 5D2 gets the pill (no chip: no RAW develop, no rolls). Footer on the top bar: "SET Film / Film stage" on
  the pill, "SET <next mode>" on the chip, "L/R Section / mode", "DOWN Rows".
- **Tabs per section** (`tab_of`, `next_tab`, `film_step`; virtual tabs, the catalogue's groups unchanged). Film =
  Colour 15 / Cine 4 / B&W 11 / Film LUT 23; Film stage = the chain films by their negative's family, read from the
  name's start: Vision3 4, Vision 15 (Vision, Vision2, Primetime), ECN/EXR 15 (EXR, ECN), Fuji 9 (the rest), B&W 1
  (Double-X) = 44. TAP cycles the section's tabs only; LEFT / RIGHT walk its films across its tabs and stop at its ends;
  the counter ("4 / 44") and the strip's scroll bar count inside the section. A tab of 8 cans or fewer shows them all.
- **Film stage modes** (`mode_now`, `mname`, `rollrows`). JPEG reads "JPEG 8-bit" in Film stage (the chip, SET's footer
  label, Info's Mode row; Film keeps "JPEG"). The Exposures row is always there and SET always loads a roll:
  - RAW develop: B develops each frame at the shot (`wf_look_use` = `WF_LU_SHOTS` for a roll with RAW develop on).
  - JPEG 8-bit: the look at the shot on Canon's 8-bit JPEG (`wf_roll_look8` = 1, `wf_look_use` = `WF_LU_SHOTS`).
  - Darkroom: Canon's own files on the roll; the Darkroom prints them later (as in Film).
  Every frame counts on the roll (frame detection is unchanged: Canon's files). With a roll in, the chip changes how the
  next frames develop, no question ("Roll 001 goes on: the next frames develop at the shot"): nothing is lost.
  `wf_roll_look8` (`src/wf_dark.c`, `wf_roll_look8_set` logs `roll: JPEG 8-bit on|off`) is saved as `rolllook=on|off`
  in WINDYFLM.CFG. Film keeps the Exposures row and rolls in Darkroom mode only.
- **Leaving a section with its roll in** (`sec_set`, `sec_answer`, `draw_sec_leave`). Never a frame lost; the words
  say what happens in the current mode (coordinator 11-17: only a Darkroom roll has frames waiting). `roll_devd()` = a
  Film stage roll in RAW develop or JPEG 8-bit (`wf_rawb` or `wf_roll_look8`: its frames were developed at the shot):
  - LOADED with frames: "Roll 001: 7 of 24 frames shot", "Close it and switch to Film?", SET = Close and switch, MENU =
    Keep the roll (MENU there does not close the browser). Darkroom roll: "The 7 frames shot wait for the Darkroom:
    PLAY develops them", "with <film>, which stays loaded until then"; SET rewinds it and sets it aside
    (`wf_roll_req_rewind` + `wf_roll_shelve(1)`), "Roll 001 ended: 7 frames wait for the Darkroom (PLAY)". Developed
    roll: "The 7 frames are already developed. The roll just ends here.", "The files stay on the card."; SET closes it
    (`wf_roll_req_close`: not set aside, so no film is held for a Darkroom it does not need), "Roll 001 ended: 7 frames
    developed".
  - LOADED with 0 frames: taken out ("Roll 001 taken out: no frame on it").
  - FULL / REWOUND: no question. Darkroom roll: set aside ("Roll 009 waits for the Darkroom: PLAY develops it");
    developed roll: closed ("Roll 009 ended: 12 frames developed").
  - The close-and-load question (`draw_swap`, a finished roll and SET on another can) reads "Roll 009: 12 frames,
    already developed." for a developed roll, "Roll 009 has 12 undeveloped frames." otherwise.
  The words follow the mode at the time of the question: a Film stage roll whose mode changed part way is judged by its
  current mode. The browser opens on the roll's section (its film under the cursor); a new-roll request in Film stage
  keeps the mode.
- **EXIF lines** (`exif_lines`). Under a Film stage film's chips, the row values for the film under the cursor:
  "EXIF <negative> -> <print stock> . Exp <film exp> . Push <n> . Comp <exp comp>" and "Printer R/G/B . Interlayer
  <n> . <Light>"; the dots and the arrow are drawn (our fonts are ASCII). Film films keep their two description lines.
- **What the develop writes** (`src/wf_menu.c` `wf_exif_tail`, `wf_dev_note`, `wf_dev_last`, `wf_path_name`;
  `WF_PATH_*` in `wf_api.h`). The EXIF text (`wf_exif_text`: the non-default knobs, as before) gets " | Mode <path> |
  Dev <n.n> s" for Film stage films only: at the shot (`src/wf_shot.c` flush: JPEG 8-bit, or RAW develop when B ran;
  the time = its two stages) and in the Darkroom's print comment (`src/wf_dark.c`: Darkroom, the develop stages). Every
  develop (all films) is noted for the UI and logged `dev time: <path> <ms> ms`.
- **Info page** (INFO in both sections, `draw_info`; INFO again closes it). Under the "Info . <film>" title the film's
  chips (kind / ISO / format, y 70) and its catalogue description (`line1` / `line2`, 15 px at y 91 / 108: the Film
  films' browser text; the chain films' facts line, e.g. "500-speed tungsten cine negative, 3200 K.", and official name,
  `films/catalog.py` `gx_desc`; owner 11-17 after the camera photo of the old overlay), a rule, then two columns from
  y 136 at a 17 px pitch (15 px text, the browser description's pitch; a chain film's 26 pairs end at y 357): Film,
  Table (the film's code; a chain film its negative's, G08), Daylight (its Light Day kelvin, "3200 K (tungsten)" for a
  T stock), the rows the film writes (the Film stage's own keys in amber: Film exp, Push / pull, Exp comp, Printer R G B, Interlayer,
  Bleach bypass, Print bleach, Neutral neg / print), Mode (with the roll's count), Last develop "31.2 s (RAW develop)",
  On the card. Then the key help (`info_keys`, owner 11-17 "all three back": the old overlay's block with today's keys,
  13 px, three columns x three rows at y 366..414; EOS M L/R, UP/DN, RING, SET, TAP, INFO, PLAY, MENU, Half-shutter,
  the 5D2 its footer's names DIAL / JOY / PICT and MENU or TRASH) and one note line at y 424 (when it is written; Film
  stage "Amber = Film stage only."), above the footer.
- **Messages** (`draw_toast`). `say()` shows an amber toast over the browser (y 188..224, the mock's): the header holds
  the pill and the chip now (round 1's message chip in the header went).
- **Cans** (`cans/gen_cans.py` `chain_style`). The Film stage cans are our own Vision3-style cassette: vertical text, no
  logo glyphs; Kodak / Eastman yellow, Fuji green, Double-X white. The Film section's cans are unchanged.
- **Light** (3.13): Auto / Day / Cloudy / Shade / Tungsten on both cameras; Auto = Auto WB inside our own develop
  (the Darkroom), never the camera's WB.
- **Decisions taken here** (for the owner): leaving a section with a Darkroom roll rewinds and sets it aside (not a
  close that drops its frames), with a developed roll closes it (it needs no Darkroom); the stage tab is "ECN/EXR" (ASCII); a Film stage mode change with a roll needs no question; the
  Info's stage keys are amber (the mock's accent, no new colour); a JPEG 8-bit roll's frames are still counted by
  Canon's files.
- **Checks.** `tests/ui/ui_test.c` section 8 (the tab counts above; the pill: focus, SET, LEFT / RIGHT, DOWN; TAP inside
  each section; JPEG 8-bit + Exposures in Film stage; a roll of 24 G08 with `wf_roll_look8`; the chip with the roll:
  RAW develop / Darkroom / JPEG 8-bit, no question, the roll goes on; Info Film / Film stage with `fake_set_dev`; the
  leave question: RIGHT ignored, MENU keeps, SET ends it; an empty roll taken out; a finished roll set aside; the
  5D2's pill), shots `fs_pill_focus`, `fs_pill_stage`, `fs_jpeg8_chip`, `fs_rawdev_roll`, `fs_darkroom`,
  `fs_info_stage`, `fs_leave_stage`, `fs_ended_jpeg8`, `fs_info_film`, `fs_stage_tab_cans`, `fs_stage_tab_fuji`,
  `fs_swap_jpeg8`, `fs_leave_stage_dark`, `fs_5d2_pill` (a JPEG 8-bit roll ends at the pill, closed; a finished JPEG
  8-bit roll's close-and-load words and its end at the pill; a Darkroom Film stage roll keeps the Darkroom words and is
  set aside); sections 1,
  2, 7 and `dr_ui_test.c` follow the top bar's keys. `test.sh` CFG (`rolllook`), FX:bloom (`wf_exif_tail` strings: Film
  films none, RAW develop 31240 ms = "Dev 31.2 s", Darkroom without a time, JPEG 8-bit, a short buffer; `wf_dev_note` /
  `wf_dev_last` and its log), ROLL (a JPEG 8-bit roll = `WF_LU_SHOTS`), ROLL:mem (the Darkroom's `dev time` log),
  LIGHT (Auto on both, Shade).
- **Not checked on the camera:** all of it (the pill and tabs on the EOS M / 5D2 screens, the three Film stage modes
  shooting onto a roll, the develop times, the EXIF / print comment text).

### 3.15 The shelf: many rolls waiting, develop again (owner 2026-11-17, branch `wf-onescreen`)

- **Owner's question:** "hỏi cái next roll rồi thì không quay lại darkroom để tráng phim lại được à". Up to here there
  was one roll: close-and-load closed the old roll (its undeveloped frames left the Darkroom, the files stayed on the
  card) and a done roll could never be developed again. The owner chose both fixes: many rolls waiting, and develop
  again.
- **The shelf** (`src/wf_dark.c` `shelf_put` / `shelf_read` / `shelf_scan`, `wf_shelf_now` / `wf_shelf_rescan` in
  `wf_dark.h`). A roll that leaves the camera with frames keeps its roll file in its own folder,
  `ROLLS/ROLLnnn/WFROLL.TXT`, next to its prints. The list comes from the card, so it survives reboots and card swaps:
  `FIO_FindFirstEx` over `ROLLS/`, each `ROLLnnn[B..Z]` folder's newest pass that has a roll file, newest first counting
  down from the camera's roll number (999 wraps to 1). `WF_SHELF_MAX` = 16 are listed; older ones are counted and
  logged. wf_task lists again when Canon is idle and something changed (a roll put on it, a run's end, the Darkroom
  screen opened). The camera's open roll is never in the list (the Darkroom lists it itself).
  - A roll goes on it at close-and-load ("a new roll": rewound; "developed at the shot": closed, RAW develop or JPEG
    8-bit), at Close roll with frames ("closed"), when the camera's roll is developed ("developed"), and at boot for
    the camera's last done roll without a copy ("developed before the shelf").
  - Close-and-load writes the copy first; only then does the roll leave the camera (state EMPTY, its number kept: the
    next roll takes the next number). A failed write leaves the roll in the camera and loads nothing (logged).
  - A new roll never takes a number whose folder holds a roll file ("roll: ROLL021 is on the card's shelf already:
    this roll is 022").
  - Folders without a roll file (rolls developed before the shelf) are not listed (their frame list is gone); the log
    counts them.
- **The Darkroom picks the roll** (`src/ui/wf_dark_ui.c`; the old separate Roll picker stays gone). A ROLL row tops
  the info block: "< ROLL 023 . on the shelf   2/4 >" (in the camera / on the shelf / developed; the dots are drawn,
  WFFONTS.DAT has no middle dot). LEFT / RIGHT / the ring on it change the roll: the camera's open roll with frames
  first, then the shelf. UP / DOWN: Screen chip, ROLL, (Film), the PRINT rows. SET starts the roll shown.
  - A shelf roll with frames left: its film in large type, "12 frames to develop", "Prints go to ROLLS/ROLL023", the
    time.
  - A developed roll: "Developed 12/12 . develop again" and a Film row (the roll's own film by default). SET there
    opens the browser in pick mode: header chip "Film for ROLL 021, develop 2", footer "SET Use this film", the mode
    chip and the Exposures row off, nothing loaded. SET returns to the Darkroom with that film, MENU / PLAY without
    one. The browser's can colours are placed again on the way back (`wfc_pal_again`: the Darkroom's print tones use
    those palette entries). Footer "SET Develop again".
  - Nothing to develop (no camera roll with frames, empty shelf): "No rolls to develop", "Shoot a roll first"; footer
    MENU Back only.
  - PLAY in the browser opens the Darkroom when the camera holds a roll with frames or the shelf has rolls: on the
    camera's roll, else the newest shelf roll.
- **Develop again** (`wf_roll_again`, `src/wf_roll.c`): pass dev + 1 (2..26), `own` = the roll's first film, `film` =
  the one picked, every frame whose CR2 is still on the card to develop (a gone CR2 is kept out and logged). Prints go
  to `ROLLS/ROLLnnnB` .. `ROLLnnnZ`, not the request's "ROLL001_2": DryOS takes 8.3 names only (8 characters). Each
  pass has its own roll file; `dev=` / `own=` lines appear from pass 2 on (pass 1's file is as before; a dev above 26
  is refused). The first prints are never touched. DCIM follows the Camera JPEG setting as for any run.
- **A shelf roll's run** (`dk_shelf_begin`): its newest pass read back and made ready (FULL / LOADED rewound, a gone
  CR2 kept out: `wf_roll_shelf_prep`), or its next pass made; its film loaded for the run and the film before put back
  after it. The camera's own roll is not touched: it stays full and held (the shutter goes free during the run as for
  any run). One roll per run, memory and timing as before; the pass's roll file is rewritten in place as frames are
  developed, so a stop resumes later.
- **Exif:** from pass 2 the UserComment reads "WindyFilm Darkroom: <look> | ROLL 001 F01 | develop 2".
- **Log lines:** `roll: 001 on the shelf (a new roll): 3/3, 3 to develop, rewound`; `roll: shelf 2 rolls (002 done
  3/3, 001B done 3/3), 12ms`; `roll: shelf: 3 older rolls not listed (the newest 16 are)`; `roll: shelf: 2 roll
  folders without a roll file (developed before the shelf): ROLL004 ROLL005`; `dark: roll 001 from the shelf: film X,
  2 to develop, prints in ROLLS/ROLL001`; `dark: roll 001 develop 2: film X, 3 frames, prints in ROLLS/ROLL001B`;
  `dark: roll 001 F02 100/IMG_0602.CR2 is gone: skipped`; `dark: film 31 for roll 001 (load 0), film 12 back after
  the run`; `ui: develop 2 of roll 001 with film 31 <name>`.
- **Set aside is not the shelf.** The mode chip's set-aside (3.12) keeps the camera's roll in the camera, RAM only.
  Its log lines now say `back in use (no longer set aside)` / `no longer set aside (close)` (they said "shelf").
- **5D2:** no Darkroom, so no close-and-load and an empty shelf; the browser offers no Darkroom (no PLAY hint). Branch
  `dark-5d2` turns all of it on for the 5D2 (3.18).
- **Checks:** `wf_roll_test` (names and their parse, shelf prep, develop again, dev=27 refused); ROLL:lock (the
  close-and-load's shelf line and file); ROLL:shelf (real RAW+L pairs: on the shelf, its copy read back, the shelf
  roll developed while the camera's roll waits with F02's CR2 gone, develop 2 with a B&W film into ROLL001B with
  "develop 2" in each print, the first prints byte for byte, the camera's roll developed onto the shelf);
  ROLL:shelfboot (a card with passes, a folder without a roll file and the camera's developed roll: the list at boot,
  its order, a new roll's number past a shelf roll); `dr_ui_test.c`
  section 16 and `ui_test.c` (`shelf_swap`). Approval images `shelf_*.png` in `_windyfilm_docs/onescreen_ui/`.
- **Not checked on the camera:** all of it (the ROLLS listing on the EOS M's card and its time, the screens).

### 3.16 ISO from the film (owner 2026-11-17, branch `wf-onescreen`)

Owner: "lúc set film sao vụ iso ko nhận đúng theo film roll nữa??". The old Film Roll set the camera's ISO to the film's
speed when a film was loaded; WindyFilm never touched the ISO (no `lens_set_rawiso` anywhere before this).

- **When:** the browser's SET that loads a film (JPEG, RAW develop, both cameras) and a roll load (the EOS M's
  Darkroom rolls and Film stage rolls, `roll_req` in `wf_dark.c`). Never while browsing (the cursor on a can), never at
  boot (the film or the roll restored from the config / roll file), never for the Darkroom's own film changes (a shelf
  roll, a develop again, the film back after the run: those are bare `wf_load` calls). SET on the film already loaded
  is no load (the animation only), so it sets nothing.
- **What:** `wf_iso_film(id, roll)` (wf_api.h) asks; `wf_task` sets it (`wf_iso_poll` in `wf_menu.c`) because
  `lens_set_rawiso` waits for the camera (`lens_wait_readytotakepic`, `prop_request_change_wait`). The speed is the
  film's box speed (`wf_films[].iso`; the Film stage films carry their real speed since 4f92b21, Double-X EI 250).
  `wf_roll_iso` (wf_roll.c, pure) picks Canon's step nearest by ratio: 1/3 stops (100, 125, 160 ...), from 100 (200
  with Highlight Tone Priority, `get_htp`) to 12800 on the EOS M and 6400 on the 5D2 (its expansion range is a C.Fn
  that may be off). A body set to whole stops (the 5D2's C.Fn I-3) refuses a third: the nearest whole stop then.
- **Auto ISO gives way:** a film has one speed, as on the old Film Roll. The log says it (`, was Auto ISO`); Auto is
  back from the Canon menu, and the next load sets the film's speed again.
- **Log:** `load: ISO 400 (film speed)` / `roll: ISO 400 (film speed)`; `ISO 100 (film speed 50, the nearest the camera
  takes)`; `ISO 200 (film speed 250; the camera takes whole stops only)`; `ISO 400 not taken by the camera (ISO stays
  250)` (a scene mode, Auto+); `film N has no speed, ISO left as it is`.
- **Not here:** white balance (Light keeps its own rules, 3.13); Push / pull and Film exp (Film stage knobs) do not
  change the ISO set: the box speed only.
- **Imports:** `lens_set_rawiso` and `get_htp` are in both cards' `.sym` (W11).
- **Checks:** `wf_roll_test` (steps, floor, HTP, whole stops, ceilings, every 1/3 step to itself); the ISO mode of
  `test.sh` on both cameras (boot untouched, a bare `wf_load` untouched, Auto to 400, 50 to 100, HTP 200, whole stops
  250 to 200, a body that takes none; EOS M: a roll load); ROLL:shelf (the shelf roll and the develop again set no
  ISO); ROLL:shelfboot (boot with a roll in: no ISO); `ui_test.c` (SET that loads asks once, SET on the loaded film
  does not).
- **Not checked on the camera:** the ISO change itself (Canon's property in M / Av / P, the time it takes).

### 3.17 Quick keys: ISO, aperture and Magic Zoom from the EOS M's keys (owner 2026-11-17, branch `wf-onescreen`)

Owner: "làm thêm default set các phím tắt cho EOS M chỉnh iso với khẩu độ nhanh hơn" and "shortcut thêm được vụ magic
zoom luôn lúc MF nữa". On the EOS M ISO is only on the Q screen or in Canon's menu, aperture in M needs UP then the
ring, and there is no zoom key. EOS M only (the 5D2 has an ISO button and two dials: its row says so, the keys go on).

- **What a module sees** (core with patch 0002, `menu.c` `handle_longpress_events`): in stills LiveView with no dialog,
  DOWN and the ring centre (Q/SET) are offered to modules (`module_offer_key`) before the core starts its long-press
  timers; a module that lets them go on keeps the long press (DOWN 500 ms: TRASH, the WindyFilm browser; SET 500 ms:
  Canon's Quick Control). A short DOWN or SET did nothing in LiveView (`longpress_check` fakes the short press only in
  the ML menu). LEFT / RIGHT / UP, INFO, MENU, PLAY, the shutter come through `handle_module_keys` before Canon.
  **The ring does not** (owner RAWB28 KEYTRACE.LOG, 20-11, M mode): in plain LiveView (GUI mode 0) Canon turns it into
  its own exposure change and ML's GUI queue gets only `GMT_GUICMD_PRESS_BUTTON_SOMETHING` (0x54, arg 0) per click, no
  `WHEEL_*` code, which the module sees as key 0 (as on the 5D2 in LiveView, 3.x Keys). No unpress of DOWN / LEFT / RIGHT reaches a module (`BGMT_UNPRESS_UDLR` is not
  there on the EOS M). Canon's own: ring = shutter (M) / aperture (Av) / shift (P); UP = Av / exposure comp.; LEFT =
  drive mode; RIGHT = AE lock; DOWN = erase (PLAY).
- **The row** `Quick keys` (Shoot > WindyFilm, config `qkeys=off|down|lr`): **DOWN ring** (default, takes nothing of
  Canon's): a short DOWN makes the ring set ISO, DOWN again aperture (M mode with a lens with contacts; in Av the ring
  sets it already), DOWN again the camera's own. **L/R ISO** (faster, takes Canon's drive mode and AE lock keys in
  LiveView; drive mode stays on the Q screen): LEFT / RIGHT set ISO a third down / up, DOWN gives the aperture ring.
  **Off**: the keys do what they did. Both mappings: **SET with manual focus** (MF, or a lens without contacts) turns
  ML's Magic Zoom on / off.
- **The ring read back from Canon** (RAWB28): while the ring is ours, `qk_watch` (wf_task, each pass) keeps the code
  Canon's ring moves in this mode: Tv in M and Tv, the aperture in Av (`lens_info.raw_shutter` / `raw_aperture`, from
  ML's property handlers). When Canon moves it, the turn's clicks (`wf_roll_dial_clicks`: the 1/3- or 1/2-stop grid
  codes passed, signed; a bigger code = up, so the camera's own dial direction setting holds) go to ISO / aperture and
  the code is put back (`lens_set_rawshutter` / `lens_set_rawaperture`), as ML's Expo lock follows Canon's changes.
  Each caught turn counts as a turn for the 6 s. A code Canon will not take back: no ISO step on top, the ring goes
  back to the camera. P shifts the program (Tv and aperture together, no way back): DOWN in P says so ("P: the ring
  shifts. Tv, Av or M", 2.5 s) and the ring stays Canon's; L/R ISO still works in P. `WHEEL_*` keys, if a body sends
  them, are still taken as before. Seen for a moment on Canon's display: its Tv / aperture steps and comes back.
- **How:** `wf_main.c` maps the keys (`wf_qk_map`) and `wf_qk_key` (GUI task, `wf_menu.c`) only notes: the ring mode,
  turns, L/R steps, the zoom toggle; DOWN and SET always go on (their long presses stay). A long DOWN's TRASH, 6 s
  without a turn, the shutter, UP, LEFT / RIGHT (DOWN ring), INFO / MENU / PLAY / Q give the ring back. `wf_qk_poll`
  (wf_task) does the rest: ISO by `lens_set_rawiso` one 1/3 stop a click (`wf_roll_iso_step`, Auto: up = 100, down
  stays Auto; a body that refuses a third: whole stops), aperture by `lens_set_rawaperture` (`wf_roll_av_step`: 1/3
  grid, or 1/2 when the camera is on half stops; the lens' widest / smallest from `lens_info`), Magic Zoom by keeping
  `zoom_overlay_set_countdown` up each pass (40 zebra passes; 0 to turn it off). Magic Zoom needs ML's Overlay > Magic
  Zoom and Global Draw on: else the box says which. Off by itself when the screen is no longer plain LiveView (a long
  SET's Q screen, the menu, the browser, PLAY), when focus goes back to AF, at unload.
- **Where:** only in stills LiveView with nothing on top: `lv`, no ML menu, no browser, `gui_state` idle,
  `display_idle()`, not movie, not PLAY, Canon's zoom off (`lv_dispsize` 1). ISO needs P / Tv / Av / M (else the box:
  "Set by the camera in this mode").
- **The box** (`wfdr_toast`, `src/ui/wf_dark_ui.c`, drawn by `wf_dark_toast` in `wf_dark.c` into ML's bitmap): bottom
  middle (y 356..426, at most 400 wide), clear of the roll HUD and the end-of-roll notice; the value big ("ISO 400",
  "f/2.8", "MAGIC ZOOM"), under it "[RING] ISO  [DOWN] Aperture" or a grey line. While the ring is ours it stays (6 s
  after the last turn), else 1.5 s; 0.4 s fade; its pixels back to 0 then a redraw (Canon's marks) and the roll HUD
  again. The font pack is held while boxes come and 10 s after.
- **Log:** `keys: ring ISO (ISO 400)`, `keys: ISO 500`, `keys: ISO 200 (whole stops)`, `keys: ISO 250 not taken by the
  camera (ISO 200)`, `keys: ring aperture (f/2.8)`, `keys: f/3.2`, `keys: ring back to the camera`, `keys: Magic Zoom
  on` / `off` / `off (the screen changed)` / `off (focus on AF again)`, `keys: Magic Zoom needs ML Overlay > Magic
  Zoom`, `keys: ISO is set by the camera in this mode (9)`; RAWB28: `keys: ring read from Canon's Tv (code 104, mode
  3)`, `keys: ring turn 1 (Tv code 104 -> 107, put back: 104)`, `keys: the camera kept Tv code 107 (asked 104): the ring
  goes back to the camera`, `keys: in P the ring shifts the program: the ISO ring needs Tv, Av or M`.
- **Imports** (both `.sym`, W11): `display_idle`, `gui_state`, `lv_dispsize`, `is_manual_focus`,
  `zoom_overlay_set_countdown`, `menu_get_value_from_script`, `lens_set_rawaperture`, `raw2iso` (with
  `lens_set_rawiso`, `get_htp` from 3.16); RAWB28 `lens_set_rawshutter`. (A `CBR_KEYPRESS_RAW` counter of the 0x54
  events was tried and dropped: the gate finds that core name among the third-party module's strings; the core's
  KEYTRACE build logs them anyway.)
- **Checks:** `wf_roll_test` (ISO steps: thirds, whole, Auto, ends, HTP; aperture steps: thirds, halves, the lens'
  ends; f-numbers); `test.sh` QKEYS (EOS M: the whole DOWN ring cycle, ring turns used / passed, the four ways back,
  Av, a scene mode, the six not-LiveView cases, Auto ISO, whole stops, a refusal, L/R ISO, Off, Magic Zoom on / off /
  Q screen / AF / not on in ML / no Global Draw, a lens without contacts, unload; 5D2: every key goes on);
  `dr_run.sh` (the box's renders, clear of the roll HUD's widest and the notice box, CLEAR gives every pixel back, a
  narrow box over a wide one).
- **Checks (RAWB28):** `wf_roll_test` (clicks: thirds up / down, halves, an end off the grid, none); QKEYS replays the
  owner's KEYTRACE (0x54 as key 0 goes on, Canon's Tv a third up / down / two at once -> ISO, Tv put back, a turn keeps
  the ring past the DOWN's 6 s, the aperture ring from Tv, a body that keeps its Tv, Av from the aperture, P told and
  left alone).
- **Not checked on the camera:** the RAWB28 read-back itself (that Canon's Tv / aperture property comes back to
  `lens_info` while the ring turns, how long the put-back shows on Canon's display, clicks lost when turned very fast
  or with Tv at its end), how fast Canon takes ISO / aperture while the ring turns, Magic Zoom's
  countdown rate (40 passes), the box over Canon's LiveView info.

### 3.18 The Darkroom on the 5D Mark II (owner 2026-11-17, branch `dark-5d2`)

- **Owner:** "5d mark II sao ko thấy darkroom để chọn". Up to RAWB25 the 5D2 had no Darkroom: no Darkroom on the mode
  chip, a roll load refused (-4), the ML menu's Darkroom entries greyed ("The Darkroom runs on the EOS M first").
  Branch `dark-5d2` is cut from `wf-onescreen` cedbfb0 (RAWB25) and turns on the same code on the 5D2, as far as host
  tests on the owner's own 5D2 RAW+L pairs prove it (`F:\MagicLatern\_test mau film\5D2`: Day 10, 141 pairs; 100EOS5D).
- **The 5D2 frame in its CR2** (`src/wf_darkp.h` `struct wf_dk_geom`, `WF_DK_GEOM_5D2`; `wf_dk_geom(cam)`; the EOS M's
  numbers moved into `WF_DK_GEOM_EOSM`, unchanged):
  - sensor image 5792 x 3804, pitch 10136 B (ML packed 14-bit), black 1024 from the masked columns 16..143, image data
    from column 158 / row 52;
  - Canon's L JPEG is 5616 x 3744 at (168, 56) (by correlation on every pair), GBRG there; the print is that size;
  - `wf_dev` develops widths that are a multiple of 32, so the develop is 5632 wide from column 152 and the print takes
    columns 16..5631 of each developed row (`wf_dev_rows_win`, new in `src/wf_dev.c`); `WF_DV_M` reads 8 columns past
    the window, still inside the row's 10136 bytes;
  - ColorData 1250 (5D2): the as-shot WB levels at index 63, Canon's Auto WB at 68 (Light Auto, 3.13).
- **Levels and trim** (`src/wf_darkp.c`):
  - white `wf_dk_white`: 13300 for ISO 160 / 320 / 640 / 1250, else 16000, from the clip pile-ups in the owner's pairs
    (1250: 13264..13318; every other ISO 15761..16383, 2500 and 5000 included). 160 / 320 / 640 have no pairs and are
    taken as 1250;
  - trim `wf_dk_trim10`: +0.7 EV at every ISO, the median (+0.74) of B's fit against Canon's L JPEG over 55 pairs. The
    per-ISO medians follow the scenes more than the ISO: left out one pair at a time, the single median misses by
    0.27 EV on average, a per-ISO table by 0.35;
  - the result against Canon's own JPEG (`tools/dr_dark_check.sh` CAM=5D2, all 141 Day 10 pairs): the fit left after
    the trim has median +0.11 EV; the mid-tone offset (the median of linear Y where Canon's Y is 30..220) has median
    -0.20 EV, 10th / 90th percentile -0.64 / +0.36 (the EOS M's 34 Day09 pairs: +0.23). Not tuned further: the spread
    follows the scenes more than the ISO.
- **Memory** (`src/wf_dark.c`). The 5D2's SRM chunk is 38,720 KB (0x25D0000; host model `tests/host/host_ml.c`): the raw
  (37,654 KB) fits with about 1 MB left. The UYVY frame (41,067 KB) goes to shoot memory, in one block or split
  (`dk_split_rows`: the head from shoot memory, the rest over the raw's used rows), as on the EOS M. The halation state
  needs 6,819 KB of contiguous shoot memory on the 5D2 (EOS M 5.7 MB); without it `dark: look ready` shows a `-` after
  halation (wanted, not built) and the prints go without it. A 5D2 build logs at each run's start: `dark: 5D2 memory: biggest shoot block N KB
  (frame 41067 KB in one block, split head N KB)`: the first camera run says which path it takes.
- **Roll frames** (`src/wf_cam_5d2.c`). With a roll in the camera (`wf_look_use() == WF_LU_NONE`) both halves stay
  Canon's JPEG, logged `yuv TOP plain: roll frame: Canon's JPEG, no look (the Darkroom prints it)` (and BOT), as
  `wf_cam_eosm.c` does. Before this the 5D2 hook gave roll frames the look (found by ROLL:dark on the 5D2).
- **UI** (`src/ui/wf_ui.c`, `src/wf_menu.c`):
  - the mode chip has JPEG / RAW develop / Darkroom on both cameras (`mode_ok`); PLAY opens the Darkroom on the 5D2 too
    (`dev_key`);
  - 5D2 footers: Darkroom mode "SET Load roll, L/R Film, UP/DN Row, DIAL Value, JOY Edit, PICT Group, MENU Exit"; with a
    roll it drops L/R for "[PLAY] Darkroom"; editing a row shows only the row's keys (no PLAY); the pill with the chip
    "SET Film / Film stage, L/R Section / mode, DOWN Rows, PICT Group, INFO Info, MENU Exit"; the film pick of a develop
    again "PICT Group"; the chip footer names PICT for TAP;
  - the 5D2 Info page's key help: "SET load the film, or a roll", "PLAY Darkroom (a roll on)";
  - the ML menu: the Darkroom entries lose the 5D2 warning; the Light help is one text on both.
- **Unchanged and shared with the EOS M:** roll, close-and-load and the shelf (3.15), develop again, the finishes (3.9,
  frame / caption / date / logo at 5616 x 3744), Camera JPEG Keep / Overwrite / Over + print, Screen On / Off (3.11),
  the end-of-roll notice and the shutter lock (`UILOCK_SHUTTER` is 0x0001 in the 5D2's ML too), the run inside wf_task
  (W13). The battery is the 5D2's percentage (`GetBatteryLevel`): a run starts at 30 % and stops below 15 %.
- **Cards:** the 5D2 card gets WFPRINT.DAT and WFLOGO.DAT too (`tools/mk_card.sh`); `card/HUONGDAN_DARKROOM_5D2.txt`
  holds the first camera run's steps and the log lines to send back.
- **Checks:**
  - `CAM=5D2 tools/dr_dark_check.sh`: 141 Day 10 pairs plus the finishes on three 100EOS5D pairs (landscape,
    Orientation 6, 8), 3111 / 0. Frame Off + date Off is the plain print byte for byte; frames, captions, dates and
    the logo are checked on all three; the biggest logo maps are 110,292 B, under `DK_LG_B` (192 KB).
  - `CAM=5D2 test.sh` 1742 / 0. Every ROLL mode and LIGHT:awb now runs on the 5D2: ROLL:dark found the roll-frame
    bug. ROLL:mem and ROLL:jpeg run on the 5D2's pairs, its SRM chunk and a 7 MB shoot cap.
  - EOS M `test.sh` 2138 / 0; EOS M `dr_dark_check.sh` 1078 / 0.
  - `tests/ui/run.sh`, `dr_run.sh` and `CAM=5D2 dr_run.sh` PASS. `ui_test.c` walks the 5D2 browser: chip to
    Darkroom, Exposures by the dial, SET loads a roll, JOY edit, Info, PLAY to the Darkroom, the ML menu's Film roll.
  - Approval images: `_windyfilm_docs/dark5d2_ui/`.
- **Not checked on the camera:** all of it. The risks the first run answers: whether the frame gets one shoot block or
  the split, and whether halation finds its 6.8 MB; the time per frame (the screen's estimate is still the EOS M's
  55 s); the display and keys while LiveView is paused, and a run started without LiveView (viewfinder); the end-of-roll
  notice, drawn only over LiveView (with the viewfinder only the shutter lock shows the roll is full); the battery
  percentage during a run.
- **After the merge into `wf-onescreen` (RAWB26):** the Quick keys row is hidden on the 5D2 (3.17); ISO from the film
  (3.16) sets the 5D2's ISO on a roll load too; RAW after Darkroom (3.9) copies the CR2 on the 5D2 and renames it on the
  EOS M; B's fallback white (`rb_white`, `wf_cam_5d2.c`) takes ISO 2500 / 5000 as 15600, not 13300 (the CR2s clip them
  at 16268..16332).

### 3.19 The Darkroom's photos, Print + over and Blind roll in the browser (owner 2026-11-20, RAWB28, branch `wf-onescreen`)
Approved with the mock images `_windyfilm_docs/rawb28_ui/` 01-04 (AskUserQuestion 11-20: "Duyệt, làm RAWB28"); the
Blind roll row added the same day at the owner's word ("trước đây bấm play ko thấy được ảnh lúc liveview h thì có
tùy chọn cho nhìn thấy"), image 05. Print + over is 3.9.
- **The photos.** Up to RAWB27 the easel showed a drawn sketch. Now, both cameras:
  - ROLL row, the chip, Film (focus not on a PRINT row): a **contact sheet** of the picked roll (camera or shelf),
    `dr_contact` (`src/ui/wf_dark_ui.c`): strips on the easel's paper as a contact print shows negatives (dark film
    between, a portrait frame on its side). Up to 12 frames: 3 strips of 4 at 72 x 48; else up to 6 strips of 6 at
    46 x 30 (36 shown; WF_DK_SHEET). A frame not read yet is grey; one without a CR2 dark with a dash; the frame the
    run prints first (`wf_roll_next_dev`) has the safelight's amber mark under it.
  - PRINT rows: the print sketch (`dr_ppreview`) draws that first frame's photo (288 x 192, a portrait 128 x 192) in
    the frame / caption / date layout of the rows, so changing Frame / Info / Date redraws round the same photo. No
    photo yet: the drawn sky as before. After a run, the last print is shown as before unless the photo is ready.
  - INFO (footer "Hide photos" / "Show photos"): `wf_dk_photo`, config `dkphoto=on|off` (default on, written after
    `dkscreen=`), `wf_dk_photo_set` (`src/wf_menu.c`). Off = RAWB27's easel; nothing is read or allocated.
- **Where the photos come from** (`src/wf_photo.c`, shared by the module and the UI harness's fake). The CR2's IFD2:
  Canon's uncompressed 16-bit RGB small picture (EOS M 660 x 441, 5D2 362 x 234; little-endian, linear light, a
  masked band at the edge), orientation from IFD0 tag 274. `wf_ph_find` reads 8 bytes, then 512 at IFD0 / 1 / 2, and
  takes IFD2 only if it is uncompressed RGB 16-bit, one strip, <= 680 wide (`wf_ph_rgb_ok`); `wf_ph_cell` reads three
  middle rows per cell band; `wf_ph_big` reads 8 rows at a time. The black comes from the masked columns, the white
  point from `wf_ph_levels` (a 0.5 % clip per end); an sRGB table maps to 8-bit luma (`wf_ph_to8`); `wf_ph_turn`
  turns by the orientation. No RAW or JPEG decode. Read per frame (EOS M, 3960 B a row): 3 rows per cell band (90 rows at 46 x 30,
  144 at 72 x 48) and, for the photo, all 441 rows (1.7 MB).
- **The loader** (`ph_poll`, `src/wf_dark.c`, in wf_task's idle polls; W13: the hook only flags). The UI task asks
  each tick (`wf_dark_photos(num, cam)`: returns at once, `n` = 0 until a sheet starts); wf_task reads, at most 100 ms
  a poll: the first-printed frame's cell and photo first, then the other cells in order. Memory: one 242 KB block
  (36 cells 46 x 30 or 12 at 72 x 48, the photo, its 16-bit rows) + 32 KB DMA (`__mem_malloc`), freed 3 s after the
  last ask, when a run starts and at unload; a changed roll (FNV of its count, frames, next frame, last file) starts
  again; no memory = the sketch, asked again after 10 s. A CR2 moved to ROLLS is found there (`wf_roll_cr2_moved`).
  Card reads only through FIO_OpenFile / ReadFile / CloseFile (imported already; W11, W24).
- **Blind roll in the browser.** Darkroom mode with a roll: a row under Exposures (`FOC_BLIND`, `rollrows()` = 2,
  `src/ui/wf_ui.c`): an eye icon, struck through when On; "Blind roll", the note "no review, no PLAY" / "review + PLAY
  show the frames", Off / On pills; LEFT / RIGHT / the wheel step it, SET loads as on Exposures (`wf_blind_set`;
  log `ui: blind roll on|off`). The menu row's help says it is also in WindyFilm.
- **Text.** `dark: photos roll 21: 12 frames read, 0 without, picture 288x192, 1840ms` (or `camera roll`), `dark:
  photos: no memory (274 KB), the sketch`, `dark: photos: roll 21 (pass 1) not read back`.
- **Checks.**
  - `tests/host/wf_dark_test.c` units: a synthetic TIFF in four orientations (find, cell halves, the big read in
    ceil(214 / 8) reads, dims, where the dark half lands), a short file / no strip / big-endian / compressed refused,
    levels and the sRGB midpoint. `tools/dr_dark_check.sh`: on every real CR2 (EOS M and 5D2) the small RGB's size,
    cells and photo means (`photo ...` lines); `wf_photo.o` -Werror and in the object checks.
  - `tests/ui/dr_run.sh` (EOS M and CAM=5D2): the contact sheet from the test rolls' CR2s, the sketch with the
    photo, INFO off / on, the shelf roll; the shared reader gives the mock images pixel for pixel (13 shots each
    camera). `tests/ui/run.sh`: the Blind roll row (focus order, On / Off, the menu value follows, shot
    `one_blind_off`).
  - `test.sh` CFG: `dkphoto=` and `jpeg=printover` read and saved; ROLL:jpeg and ROLL:cr2mv as in 3.9.
- **Not checked on the camera:** how long a sheet takes to fill (the card's seek time per frame), the photos' look
  against the camera's own playback, and Blind roll Off with PLAY in LiveView during a roll.

### 3.20 Lab: the film-lab Halation, Bloom and Grain (owner 2026-11-21, branch `labfx`)
The owner's request: Halation / Bloom / Grain options with the numbers of film-lab, the existing ones kept. film-lab is
the owner's film-look reference: formulas and preset tables (one set per gauge, 8 / 16 / 35 / 65 mm), its own
C++ and JS code and a CLI that renders PPM files. Its tone stages before the bloom are not on the camera (the film's
own table does that part). Nothing of film-lab is copied here: `src/yl_lab.c` is a separate fixed-point
implementation (ARMv5TE, no FPU, no divide per pixel; soft float only once per setting), the presets are its tables.
Bloom follows film-lab's Bloom stage as well (my call: film-lab runs Bloom, then Halation, then Grain, and its presets
hold all three per gauge).
- **Choices.** The last choice of Grain (7), Halation (4) and Bloom (4) is **Lab**; Gauge gains **8 mm** (value 3, the
  menu steps 8 -> 16 -> 35 -> 65 mm). Config words `grain=lab`, `halation=lab`, `bloom=lab`, `gauge=8`. The Lab values
  are the same for every film (film-lab's presets know no film). Rules (`wf_fx.h`): a Lab value in the Halation or the
  Bloom row makes the stage the Lab one, and a WindyFilm value in the other row does not run (the menu shows it unused,
  as Bloom with Wrap); 8 mm is a Lab gauge: the WindyFilm choices run as 16 mm there (`WF_GAUGE_CLASSIC`, menu advice,
  the EXIF writes 16 mm). EXIF / shot line: `| Grain Lab | Halation Lab | Bloom Lab | Gauge 8 mm`, `+ halation
  lab+bloom lab + grain`; the Darkroom's look line `halation lab + bloom lab, grain lab`. Keys: the Lab stage `(set + 1)
  | 4096 Lab halation | 8192 Lab bloom | gauge << 10`, the Lab grain `(set + 1) | 2048 | gauge << 9`.
- **Where it runs** (`wf_shot.c` wf_colour_fx). The Lab stage gathers the coloured frame (film-lab works on the finished
  image; the WindyFilm stage gathers the camera's words before the colour pass). With both on, a second read-only pass
  gathers the halation layer again from the bloomed frame (`yl_gather2_chunk`, as film-lab blurs the halation of the
  bloomed image), then one apply pass writes both. Grain after them, per row. Halves (every 5D2 shot, Diana): the BOT
  fades from the TOP's glow over 256 rows (`YL_XF`), checked against the whole frame at the seam (FX:lab).
- **The halo's glow composite** (film-lab 2026-11-21; owner: the sky rim first). The halo is the whole scattered light,
  (near glow x its weight + far glow x its weight) x mix, added per channel through film-lab's glow composite: as it is
  while it fills at most half the headroom 1 - v (the knee), past that 1 - (1 - v) / 2 x exp(1 - 2a / (1 - v)), the
  same value and slope at the knee, towards white without reaching it. Until then the sharp core (0.75 x the layer) was
  left out of the halo and the sum clamped: that kept highlights from clipping, but just inside every highlight edge a
  band got no halo at all, a pale rim (cyan on a sky) between the orange halo outside and the halo inside. On the
  camera, per pixel and without a divide: 2a / (1 - v) from the reciprocal of the headroom's top 11 bits (a table of
  1024; ARMv5TE's CLZ), the exp from a table at 1/64 steps, linear in between: within 1.6 Q4 levels (a tenth of a
  level) of the formula in double. The linear path's limit counts the halo twice, so a word takes it only where the
  composite adds the halo as it is. Bloom and Grain are unchanged.
- **What the camera does differently**, each measured against the reference CLI (`tools/lab_check.sh`):
  - halation and bloom are blurred on grids of cell means, 1/8 (halation) and 1/16 (bloom), then interpolated per
    pixel pair; the blur variance is the reference's three-box Gaussian minus what the grid adds;
  - the colour, the clamps, the halo's glow composite and the bloom's ease-off in the highlights are per pixel, on the
    camera's UYVY words (full-range BT.601), as deltas: a word without glow is kept bit-exact; a word whose channels
    cannot clamp and whose halo stays under the knee on every channel takes the linear path (its Y / U / V change from
    the word's glow once), the others the exact per-pixel path;
  - the bloom's edge mask uses the largest luma step per 16-px cell instead of a per-pixel Sobel;
  - the grain is film-lab's lattice noise (its hash at whole cells of the cell size s, bilinear in between over the weights'
    root sum of squares, R G B on their shifted lattices, the clump blur, the gain that keeps the strength of the
    800-row preview at any size) on a **periodic lattice**: the field's 256 x 256 samples over one period, computed once
    per gauge with the exact kernel per axis, d px apart (an even whole number near 0.5 sqrt(sigma^2 + 0.22 s^2)),
    a pixel mixing the four samples round it, the variance this loses put back ((2 + rho) / 3 per axis); a random
    offset per shot; the lattice's turn (1 degree in the presets) is left out. Per row the sample rows are mixed once into a
    period-long row (Y per pixel, U / V per word). Measured: within ~3 % rms of the exact field (same lattice).

    | | 8 mm | 16 mm | 35 mm | 65 mm |
    |---|---|---|---|---|
    | EOS M (3456 rows): cell px, d, period px | 38.2, 10, 2560 | 21.0, 6, 1536 | 11.4, 4, 1024 | 3.9, 2, 512 |
    | 5D2 (3744 rows): cell px, d, period px | 41.3, 10, 2560 | 22.9, 6, 1536 | 12.3, 4, 1024 | 4.3, 2, 512 |
    | strength gain (EOS M / 5D2) | 0.347 / 0.350 | 0.375 / 0.385 | 0.461 / 0.487 | 0.982 / 1.082 |
- **Accuracy** (`tools/lab_check.sh`, the camera's code built for the host against the reference CLI on the same RGB
  input; EOS M frame unless noted). Glows: PSNR over the frame and the largest difference in levels:

    | IMG_6851 | 8 mm | 16 mm | 35 mm | 65 mm |
    |---|---|---|---|---|
    | Halation | 54.6 dB, 6 | 55.0 dB, 6 | 56.0 dB, 6 | 56.3 dB, 7 |
    | Bloom | 55.8 dB, 12 | 58.1 dB, 11 | 58.8 dB, 10 | 60.6 dB, 9 |
    | both | 51.9 dB, 16 | 53.0 dB, 17 | 54.2 dB, 15 | 55.1 dB, 16 |

  (With the glow composite, against the reference's own; the old composite measured against its old reference gave
  Halation 53.3 .. 54.6 dB, at most 22 levels, both 50.0 .. 53.3 dB, at most 37: the largest differences were where the
  1/8 grid's glow and the per-pixel core met the reference's full-resolution layer.)

  Grain (its pattern is another draw, so the statistics): the luma change's sd in the mid tones, camera / reference
  through the camera's UYVY round trip (the camera's grain is rounded to whole codes, which adds near-white noise):
  8 mm 1.94 / 1.90, 16 mm 2.10 / 2.07, 35 mm 1.66 / 1.64, 65 mm 0.75 / 0.79; autocorrelation at 12 px 0.83 / 0.82, 0.46 /
  0.43 (8, 35 mm). The other frames (glows at 35 mm; both, over the four gauges):

    | frame | Halation | Bloom | both | both, 8 .. 65 mm |
    |---|---|---|---|---|
    | IMG_6853 (EOS M, street at sunset) | 51.1 dB, 13 | 46.5 dB, 22 | 46.1 dB, 22 | 45.3 .. 46.1 dB |
    | IMG_0216 (EOS M, backlit tree, Darkroom set) | 43.3 dB, 32 | 46.8 dB, 43 | 40.4 dB, 41 | 40.1 .. 40.5 dB |
    | IMG_0214 (EOS M, the same) | 43.9 dB, 33 | 45.8 dB, 49 | 41.0 dB, 44 | 41.0 .. 41.1 dB |
    | IMG_8553 (5D2, two halves) | 50.6 dB, 30 | 41.2 dB, 64 | 42.7 dB, 64 | 42.4 .. 45.7 dB |

  Where most of the frame glows (the backlit trees: 89 .. 99 % of the pixels) the mean difference inside the glow is
  under one level (0.75 .. 0.94), p99 7 .. 8 (Halation) and 12 .. 13 (both): the differences sit on thin edges (wires,
  twigs), where the grids' glows part from the reference's full-resolution blur; crops of the camera's and the
  reference's frames look the same. The 5D2's Bloom as one whole frame: 54.5 .. 57.9 dB, at most 9 levels; the rest is
  the halves: the TOP is graded before the BOT's light exists, so the TOP's rows within the bloom's reach of the seam
  miss it (the mirror guesses only wide light at the seam), as with the WindyFilm stage. Grain on every frame: the mid
  tones' sd within 1 .. 4 % of the reference through UYVY at 8 / 16 / 35 mm, 65 mm 3 .. 5 % under it.
- **Time per shot** (memsim, the EOS M's ARM946 model; the 5D2 is ~0.83x of its memsim; with the glow composite):
  EOS M, 35 mm, both glows 17.0 s (gather 3.5, build 2.0, gather2 2.9, build2 1.0, apply 7.6), 8 mm 19.4 s; halation
  alone 9.1 s; grain 3.5 s (8 mm 4.1). The glows' time follows how much of the frame glows: IMG_0216 (backlit, ~97 % of
  the pixels in the glow) takes 45.5 s at 35 mm, mostly the per-pixel paths of the apply (25.1 s) and gather2 (11.1
  s); halation alone 30.5 s there (25.5 with the old composite): most of its sky passes the knee, so most words take
  the apply's per-pixel path. 5D2 (IMG_8553, two halves, memsim x 0.83): both 25.4 s at 35 mm. Measured with the old
  composite and not again (the code they time is unchanged or close): 65 mm both 17.7 s, bloom alone 9.8 s (IMG_0216
  24.5), 5D2 8 / 65 mm 32.9 / 26.3 s, halation alone 13.9, bloom alone 14.7, grain 3.4 (3.3 .. 3.9). Once per setting:
  1.1 .. 1.4 s.
- **Memory.** The glows: one block of `yl_mem_bytes` (EOS M 3.79 MB both, 2.34 halation, 1.38 bloom; 5D2 4.44 / 2.74 /
  1.62 MB), as the WindyFilm stage's. Grain: 213 KB + 1.4 KB (the samples, the period row, the tables), and 665 KB of
  shoot memory while it is built.
- **Checks.** `test.sh` FX:lab (both cameras): the glow composite against its formula in double (within 2 Q4 levels,
  the halo as it is below the knee, rising, never past white) and film-lab's sky edge (a bright sky against dark
  leaves, 35 mm: 4 px inside the edge at least half the red the inside gets; the old composite gave +8 against +25),
  config words, rows and keys, each glow alone and both on the disc frame
  (a red-orange halo, a glow in the light's colour, both more than either), halves = whole frame at the seam (EOS M;
  again with a white band over the frame's top: at most 12 levels apart over the YL_XF rows, as the TOP's halation and
  bloom rows round the seam are kept in blocks of their own; sharing one block put the band's glow into the BOT's first
  rows), the rows' rules, 8 mm, the grain on a mid grey (8 mm sd 1.2 .. 3, 65 mm 0.35 .. 1.2), EXIF, steps, the config
  written back, the memory given back. The WindyFilm models bit-exact with the RAWB28 tree (6e7f700): `WF_SHOT_HASH`
  over SHOT, FX, FXAB, FX:bloom, MIST, MIST:levels, EXIF, STAGE, STAGE:dark, PRINT, PRINT:chain, PAPER, HS, RAWB and
  ROLL:dark, 167 shots, all the same (EOS M; the frame's post time fixed in the host build of the camera file for the
  run, since every shot's grain seed mixes it in: without that, base against base differs). `tools/lab_check.sh` (accuracy), `tools/lab_bench.sh` (memsim
  phases), `tests/ui/lab_strip.sh` (the comparison sheets: Off / WindyFilm / Lab, the gauges), `tests/ui/run.sh` shots
  `lab_rows`, `lab_gauge`.
- **Not checked on the camera:** the times (memsim only), the memory at the shot on a full card, and the look on the
  camera's own screen.

## 4. Interfaces

### 4.1 `src/wf_api.h`

Frozen: cameras, the catalogue struct, rows, load, status, UI entry points, log and card read. The core implements
everything except `wf_ui_*`. The UI calls only these functions plus ML's drawing API. Rows (2026-11-14): Light, Print,
Grain, Halation, Bloom, Mist, Strength, Highlights, Shadows, Film LUT (`WF_NROWS` 10; Bloom since RAWB11). Since
2026-11-15 (`film-stage`, merged into `print-frame`): Light, Print stock, Grain, Halation, Bloom, Gauge, Mist, Strength,
Highlights, Shadows, Film LUT, then the film-stage knobs Film exp, Push / pull, Exp comp, Printer R, Printer G, Printer
B, Interlayer (`WF_NROWS` 18; the knob rows are available only on a film-stage pair that `ML/DATA/WFSTAGE.DAT` carries,
section 1a). Since 2026-11-16: Bleach bypass, Print bleach, Neutral neg, Neutral print after Interlayer (`WF_NROWS` 22),
then Paper after Print stock (`WF_NROWS` 23, section 3.10).

### 4.2 Catalogue

- `films/catalog.py` (data) holds the rows. It keeps every field the old catalogue had (id, code, name, type, iso,
  isocode, kelvin, ev8, family, order, grain, halo, spk, v21, lut, mode, prov, ready) and adds:
  - `short` (<= 11 chars at 13 px condensed);
  - `line1`, `line2` (each <= 44 chars at 15 px: our short description; the generic LUT-can line1 is rewritten; the
    chain films: `gx_desc` (box speed, balance, Light Day kelvin) and the official name, 11-17);
  - `kind`, `fmt` (the chips);
  - `group` (`colour` / `cine` / `bw` / `lut`);
  - `prints` (the variants made, e.g. `("scan", "cine", "paper")`).
- `tools/mk_catalog_c.py` (core) reads the rows and writes `build/wf_catalog.c` (`wf_films[]` per wf_api.h plus the
  core-only tables: grain sets, halation profiles, EV, LUT cube names).
- `catalog.py check` asserts lengths and the presence of tables and variants.

### 4.3 `ML/DATA/WFFILMS.DAT` (data writes, core reads)

```
header 32 B  "WFFT", u16 version 1, u16 count, u32 words 4913, u32 entry bytes 19652, u32 index offset 32,
             u32 data offset, u32 FNV-1a of the index, u32 0
index  24 B  u8 id, u8 variant (0 scan / 1 cine / 2 paper), u8 flags (bit0 v21, bit1 B&W), u8 pad,
             char code[8] (NUL padded), u32 offset, u32 bytes, u32 FNV-1a of the table
data         per entry: 4913 words {U s8, Y, V s8, 0}, node (Ycell*289 + Ucell*17 + Vcell), 16-code cells, neutral U/V 0x00
```

- The table meaning is exactly what `fr_film_build` consumes (read it in `fr_film.c`). B&W: every U / V byte is 0x00.
- The module accepts an entry only when its id and code match the compiled catalogue.

### 4.4 `ML/DATA/WFCANS.DAT` (data writes, ui reads)

```
header 64 B  "WFCN", u16 version 1, u16 count, u16 npal (<= 128), u16 nsizes 3,
             sizes[3] {u16 w, u16 h} = big 160x226, strip 52x73, selected 58x82,
             u32 palette offset, u32 index offset, u32 data offset, u32 FNV-1a of all bytes after the header, zero pad
palette      npal x {u8 r, g, b, 0}
index  32 B  u8 id, u8 pad[3], u8 glow r, g, b, u8 pad, u32 offset[3], u32 bytes[3]
data         RLE per image, row-major: 0x00..0x7F one pixel of that palette index; 0xFF one transparent pixel;
             0x80..0xFE a run of (c - 0x7E) = 2..128 pixels of the next byte (an index, or 0xFF = transparent)
```

- `npal` defaults to 103: the UI palette range 0x50..0xFE (175 entries) holds 21 fixed, 6 background, 33 glow, 12 shadow and 103 can colours. The UI maps palette index i to its can block.
  If the UI needs fewer colours, the data side regenerates with `--colours N`.
- `glow` = the film's glow colour (the mean of its saturated pixels x 1.25, as in the mockup).
- Drawing code: our `gen_cans_real.py` (compose / lin). Palette: median cut over our cans only.

### 4.5 `ML/DATA/WFFONTS.DAT` (ui writes and reads)

Own format, decided by the UI side. Glyphs from Ubuntu Sans (Ubuntu Font Licence 1.0), rendered 1-bit:
- 15 px Medium;
- 13 px Medium;
- 13 px Medium width 82.

Drawn by our own renderer, which needs none of ML's 7 dynamic font slots. Title and film name use ML's `argnor23` /
`argnor28` through `rbf_draw_string` / `font_large` etc.

### 4.6 `ML/DATA/WFPRINT.DAT` (core reads; the Darkroom's cameras: EOS M, and the 5D2 from `dark-5d2`)

The Darkroom's print caption font (section 3.9): `tools/mk_wffont.py WFPRINT.DAT --print-only`, run by `build.sh`.
"WFPF" v1, two fonts (84 px wght 500, 62 px wght 500; 400 up to RAWB26), ASCII 32..126 plus 127 = U+00B7, 4-bit
coverage, kern pairs,
FNV-1a over everything after the header. The byte layout is in the tool's docstring. `wf_pf_fonts` checks the magic,
version, sizes and hash, and `mk_wffont.py` checks its own renderer against PIL. The Darkroom reads the pack per
framed print into `M.raw` scratch (at most `WF_PF_MAXB` 512 KB).

## 5. Browser UI (B2 small text)

Mockups: notes folder `newmodule-mockups/mock_B2s_strip_x2.png` and `mock_B2s_strip_lut_x2.png`; drawing code in
`mockup.py` (`variant_b2s`).

The layout, 720x480 (the ML bitmap):

| area | content |
|---|---|
| header (0-33) | mark, "WindyFilm" argnor23, "by WindyStrife" 13 px, loaded chip 15 px (on `wf-onescreen`: the section pill "Film \| Film stage" in place of "by WindyStrife", 3.14; EOS M: the MODE chip at the right, 3.12; the 5D2 keeps the loaded chip) |
| big can | 160x226 at (45, 40), glow in the film's colour (16 palette rings, recoloured per film), floor shadow |
| right column x 262 | name argnor28 at y 38; chips (kind / ISO / format) 13 px at y 72; SET Load button; description 15 px, 2 lines at y 96 / 113 (Film stage: the two EXIF lines 13 px, 3.14) |
| rows | 18 rows since the film-stage merge (10 before), 7 shown from y 136 at 19 px pitch; the list scrolls with the focus (2 px scroll bar at x 712); focus bar in the accent colour, chevrons on the focused value; Strength as 5 bars |
| tabs | Colour / Cine / B&W / Film LUT with counts at y 276, underline on the active one; counter "n / N" (`wf-onescreen`: the section's tabs, Film stage Vision3 / Vision / ECN/EXR / Fuji / B&W, the counter inside the section, 3.14) |
| strip | 35 mm band y 300-436 with sprocket holes; cans 52x73 with short names (13 px condensed) under them; selected can 58x82 framed in the accent colour; edge fades and arrows |
| scrollbar | y 441-446 |
| footer | key hints 13 px: SET Load, L/R Film, UP/DN Row, RING Value, TAP Group, INFO Info, MENU Exit (5D2: DIAL Value, JOY Edit, PICT Group; while editing L/R Value, JOY Done) |

- **Colours:** accent amber (244, 166, 52), teal (38, 168, 158) for the Film LUT group, background gradient
  (24, 27, 33) -> (10, 11, 14).
- **Drawing:** draw into the idle buffer, then copy (`bmp_draw_to_idle` / `bmp_idle_copy`). Redraw only when
  `wf_gen()` or the UI state changes.
- **Palette:** take a free range with `LCD_Palette` / `EngDrvOut` (read `src/bmp.c` and `tweaks.c` in the ML tree for
  the entry format on DIGIC 4 and 5), and restore it on close.
- **Keys:**
  - EOS M: through ML core patches 0002 / 0003 (module UI keys, Canon dialog mode 93 when the module calls
    `canon_gui_disable_front_buffer`).
  - 5D2: joystick / wheel / SET / Q / INFO / MENU.
  - Open from the WindyFilm menu entry, or the open key (EOS M long DOWN, 5D2 TRASH).
  - 5D2 without LiveView (owner 11-14: it came up only with LV on): the browser asks Canon for its MENU GUI mode 2 before
    it takes the front buffer, as ML's own menu does there (`piggyback_canon_menu`, GUIMODE_ML_MENU = lv ? 38 : 2). That
    mode turns the display on and keeps it on. If it does not, `display_on()`. On close it asks for mode 0 while the front
    buffer is still ours. Canon leaving the mode or the display going off closes the browser. Opened from the ML menu,
    it first waits for the menu's own mode 0. A TRASH that does not open the browser logs why (`ui: TRASH not taken`).
  - 5D2 in LiveView (owner 11-14: "the settings cannot be changed from the UI on the 5D2"): Canon's plain LV (mode 0)
    turns the dials into exposure changes and sends the module no WHEEL codes. The browser asks for mode 38 there, ML's
    LV menu mode (consts.h: "a gui mode which lets you use the wheel and all other keys"), with Canon's bitmap muted
    (`bmp_off`) until the front buffer is ours, as `piggyback_canon_menu` / `close_canon_menu` do. No mode while
    recording (ML: 0). Fallback if a dial still sends nothing: the joystick press toggles edit on the focused row, then
    LEFT / RIGHT step its value (outlined row, footer "JOY Edit" / "JOY Done"); SET only loads on the 5D2.
- **Load (owner 11-14, "like Film Roll: a load animation, then LiveView"):** SET on a film that loads plays our own
  animation (about 1.15 s: the film's leader winds out of its can, drawn from WFCANS.DAT, to a gate that lights with
  frame 1, over the dimmed browser), then the browser closes. EOS M: the browser is open only in stills LV, so LiveView
  is back. 5D2: opened without LV, `force_liveview()` once the screen is Canon's again. A key skips the rest; a
  half-press closes without LiveView. The film already loaded picked again (a roll's own film too) plays the same
  animation and closes, without a load; in Darkroom mode the loaded roll's own film and count do the same (a full or
  rewound roll asks to close it and load). The errors keep the browser open with their message.
- **Never a dead camera (owner 11-14, EOS M: sometimes neither the film menu nor the ML menu opened):** the browser
  takes every key, so a UI task that does not run left both menus dead. The film worker (prio 0x1C) develops a shot for
  seconds (RAW develop 35-45 s), and wf_task (0x1E) builds the grain tables for 2-19 s without sleeping; the owner's
  RAWB2 log shows the old prio-0x1E UI task waiting out that build (DryOS does not time-slice equal priorities). The UI
  task now runs at 0x1B, above both and below ML's menu_task (0x1A) and Canon's GUI. It beats at every step; a key that finds no
  beat for 2 s (10 s while it opens inside a pack read: owner RAWB9, the cans read waited 2.2 / 7.3 s on a card busy
  with the roll's last frame) goes to Canon / ML and asks the task to close when it runs (log `ui: no beat for N ms (state, phase)`;
  phases listed at the top of `src/ui/wf_ui.c`). The EOS M two-finger tap then opens the ML menu (no module took MENU).
  The open is claimed under `cli` (TRASH in the GUI task and the menu entry in wf_task cannot start two tasks), a failed
  `task_create` leaves the browser closed, and keys pass on while the task starts LiveView after a load.
- **The ML menu over LiveView (owner RAWB7, EOS M: "the LiveView screen paints over" the menu a two-finger tap
  opened):** the ML menu lives on Canon's LV Set dialog (GUI mode 93); ML asks for it once at the open and every 2 s.
  The owner's KEYTRACE: right after Canon's own MENU, that first ask did not take and the menu ran on mode 0 until
  ML's 2 s ask. Nothing of ours was open. Our side: (1) the browser never gives the front buffer back while the ML menu
  is up (a close under it waits for the menu to go, then gives everything back and redraws; an open under it is
  refused), and the roll HUD does not draw under it; (2) the EOS M guard, `wf_ui_menu_guard` (RAWB8 ran it in a task
  of its own, `wf_mguard`; that extra task filled DryOS's task table at boot and the worker was refused, so from 15 Nov
  it runs in wf_task's 10 ms waits; while wf_task reads a LUT for seconds, ML's own 2 s ask is left): while the ML menu is up in stills LV on mode 0, it asks for 93
  150 ms after the open and then every 300 ms, 8 times at most (log `menu: canon dialog 0 -> asked 93 (try n)`, then
  `menu: canon dialog 93 after try n, N ms ...` or `still 0 after 8 tries`); an ask that lands after the menu closed
  is asked back to 0 within 1 s. Not on the 5D2 (no such report there).
- **Pack reads while Canon holds the card** (owner's RAWB2 log, EOS M 10 s after boot: "fonts WFFONTS.DAT not on the
  card, cans WFCANS.DAT read failed" with both files there). `wf_card_read` returns `WF_RD_*` (wf_api.h); a file it
  cannot size is `MISSING` only when the card answers for `AUTOEXEC.BIN`, else `BUSY`. At open the pack reads are tried
  again for up to 1.5 s (beating); a pack still not read is read again every 2 s while the browser is up (5 times) and
  drawn when it loads; until then the small text is ML's term12 and every can is the plain one (16 colours; no pack
  state is kept after a failure). A can image whose read fails is not cached: plain for now, read again after 1 s.
- **INFO** toggles a details overlay: full description, ISO, kelvin, prints, table source.

## 6. Build and tests

- `build.sh CAM=EOSM|5D2` -> `build/<cam>/windyflm.mo` plus the card files (WFFILMS.DAT, WFCANS.DAT, WFFONTS.DAT,
  WFPRINT.DAT; cubes are separate).
  - Flags as the port's: `-Os -march=armv5t -marm -mlong-calls -mfloat-abi=soft -ffast-math
    -fsingle-precision-constant`; kernels `-O2`; asm `-mcpu=arm946e-s -marm`.
  - ML headers: EOS M `/root/ml/expsim/mls` (the card core: autoexec 782dbf15, EOSM_202.sym md5 09ccc47d); 5D2 the
    q5d2 tree.
  - Every imported symbol must exist in that camera's card `.sym` (checksyms).
  - TCC link check (fxlab tccload): report the image size. Target < 0.5 MB.
- `test.sh` runs the host harness (own code, modelled on the old eosm_test.c, no author binary) in modes:
  SHOT (film / log / Log + LUT on a frame, against references), FX, MIST, EXIF, EXPSIM, LOGCHK, CFG, PACK, LIGHT,
  PRINT, 5D2. Plus `tests/ui` (PPM dumps of the browser). Never run the EOS M and 5D2 suites at the same time.
- `tools/provenance_gate.py` (run by `build.sh`, fails the build):
  - no printable string of >= 6 chars from `filmroll.mo` appears in `windyflm.mo` or the card packs, except an
    allowlist of generic ML / libc strings kept in `tools/gate_allow.txt`;
  - no 32-byte window of `windyflm.mo` code / data equals one in `filmroll.mo` (ignoring runs of a single byte);
  - deny words: florinkiritescu, FILMCAN, FILMROLL, filmroll, plus the DER and Genesis file names, in sources and
    packs;
  - no file under `src/`, `tools/`, `films/`, `cans/` is byte-equal to a DER file.
  - public standard tables (added with the darkroom's JPEG encoder, 11-14): the ITU-T T.81 Annex K tables every
    baseline JPEG encoder carries (zig-zag order of Figure A.6 and its inverse, K.1 / K.2 quantisation tables in
    natural and zig-zag order, K.3-K.6 Huffman code counts and values) are built by the gate from the standard and
    masked to 0 in both modules (8-, 16- and 32-bit LE arrays, whole tables only) before the string and window checks;
    the bytes around a table are still compared, and the gate prints what it masked. `src/wf_jpe.c` holds them as
    plain `uint8_t` arrays typed from the standard.

## 7. Card layout

Separate folders, so the current Film Roll cards stay usable: `F:\MagicLatern\EOSM_WindyFilm`,
`F:\MagicLatern\5D2_WindyFilm`.

```
ML/modules/windyflm.mo
ML/DATA/WFFILMS.DAT, WFCANS.DAT, WFFONTS.DAT (EOS M also WFPRINT.DAT, the Darkroom print caption)
ML/LUTS/... (Film LUT cubes: ours from luts/, the owner's own cubes copied by the owner)
README_WINDYFILM.txt, README_FILMLUT.txt, SPEKTRAFILM_LICENSE.txt, ATTRIBUTIONS.txt
```

The core files (autoexec.bin + the `.sym`) stay the ones each card already has; they are one pair (trap Q25).
`filmroll.mo` is not on the WindyFilm cards. Deploying needs the owner's OK.

## 8. Phases inside Phase 2

1. Skeleton builds and links for both cameras (stubs where parts are missing). Gate green.
2. Core port: shot pipeline + effects + menu + config + EXIF + log. Host tests green.
3. Data: converter + regenerated tables + variants + can pack + descriptions; judge v2 sheets.
4. UI: browser on the host (PPM), then on the camera.
5. Integration, both cameras, docs, Qwen hand-off, commit. Card only with the owner's OK; push only after the owner
   creates the private repo `windystrife/windyfilm` and the 404 check passes.

## 9. Port list (from the phase 1 audit)

**OWN, port with `fr_` renamed to `wf_`:**
- fr_film.c/.h
- fr_loglut.c/.h, fr_llk.c, fr_llk_arm.S
- fr_logout.c, fr_logout_arm.S, fr_logout_tab.c
- mk_logtab.py, mk_logasm_tab.py, mk_loglut_tab.py, logchk.c
- fr_fx.c/.h, yh_halo.c/.h, yh_tables.h, yh_weights.h
- yg_grain.c/.h, yg_stocks.c
- fr_mist.c/.h, mist_render.c
- fr_expsim.c/.h, fr_expk.c, fr_expoff.c, mk_exptab.py
- fr_modes.h, fr_pv.h, fr_cam.h
- films/catalog.py
- fr_5d2_hook.c (5d2 branch)
- cans/WS/gen_cans_real.py (drawing only)
- luts_film/src

**GLUE, keep our logic and rewrite the ties:**
- **fr_eosm.c.** Keep:
  - hook gating (`fr_yuv_hook_eosm`), `fr_worker`, `fr_patch_hook`, `fr_run` dispatch, `fr_colour_fx`, `fr_chunks` /
    `fr_copy8`;
  - the pack reader `fr_ws_*`, `fr_dispf` + the preview polls, the shot log, `fr_ll_poll`.

  Drop:
  - `fr_ft_msleep` / film_task ties, `sub_410` logging, the B32 .bss / `fr_data` reads;
  - mode 0 / `fr_yuv_hook_orig`, `fr_filmyuv_apply` / `fr_lut_*`, `fr_kern_select` / `fr_kwork` / `fr_kverify`;
  - `fr_pv_auth_poll` and `FR_PVK_AUTHOR`, `fr_dev_interactive`.
- **fr_plus.c.** Keep the menu, the config (now WINDYFLM.CFG), EXIF `fr_exif_*`, `fr_shot_ev8`, LUT EI. Drop
  "Look engine" (`fr_eng_*`) and `fr_title_tag`.
- **fr_cbr.c:** rewrite as our own CBR list.
- **fr_shelf.c:** only the catalogue and LUT-can logic (`fr_lutcans_scan` etc.). The rest was edits of the author's
  shelf: drop it. The browser replaces it.

**Tests to rewrite on our own harness:** exif_test.inc, expsim_test.inc, mist_test.inc, v10_test.inc and the own parts
of eosm_test.c.

**DEP (drop):**
- eosm_test.c harness glue, test.sh, xmodes.sh, apr_inst.py, aprace.inc, patch_mo.py, audit_mo.py;
- the author parts of build.sh and mk_tccload.sh;
- tools/fxlab/retarget.py, tools/yuvre/harness.c, luts_fuji/preview_fuji_luts.py, cans/fcan_tool.py;
- the palette part of the can generators.
