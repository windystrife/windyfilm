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

## 2. Repo layout and who owns what

During Phase 2, agents write only inside their own paths and never commit. The lead session commits explicit paths.

| path | owner | what |
|---|---|---|
| `src/wf_api.h` | lead | frozen interface (section 4) |
| `src/*.c`, `src/*.h`, `src/*.S` | core | module, camera layer, worker, kernels, effects, menu, config, EXIF, preview, log |
| `src/ui/*` | ui | browser (B2 small text), can pack loader, own font renderer, icons |
| `films/catalog.py`, `films/tables/*.T17` | data | catalogue rows, regenerated tables (scan / cine / paper variants) |
| `tools/mk_wtab.py`, `tools/mk_wfpack.py`, `tools/mk_wfcans.py`, `cans/*`, `luts/*` | data | cube -> table converter, film pack, can pack, film cube model |
| `tools/mk_wffont.py` | ui | Ubuntu Sans -> WFFONTS.DAT |
| `tools/mk_catalog_c.py`, `tools/mk_*tab.py` (log tables), `tools/provenance_gate.py`, `build.sh`, `test.sh`, `tests/host/*` | core | C catalogue, generated tables, build, host tests, gate |
| `tests/ui/*` | ui | UI host test (PPM dumps) |
| `README.md`, `ATTRIBUTIONS.md`, `LICENSE`, `QWEN.md`, `AGENTS.md`, `docs/*` | lead | docs, Qwen hand-off |

WSL work dirs (one each, never shared): core `/root/ml/wf_core`, ui `/root/ml/wf_uidev`, data `/root/ml/wf_data`.
`/root/ml/wf_ui` holds the mockup inputs.

## 3. Architecture

```
module_init ─ wf_cam_init (ROM word checks) ─ wf_cfg_load ─ menu ─ wf_task (own task, prio 0x1C)
   CBRs: CBR_KEYPRESS (browser + open key), CBR_DISPLAY_FILTER (LiveView preview), CBR_SHOOT_TASK if needed
wf_task loop (~50 ms): pack reader, Log + LUT cube loader, preview tables, EXIF writer, config save, log flush, UI redraw
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
- **Effects:** grain (`yg_*`), halation and bloom (`yh_*`), Mist (`wf_mist`, EOS M; on the 5D2 halves only once its
  map hand-over is wired, as before), Strength 30 / 100 / 125, Highlights / Shadows -5..+5 (section 3.3).
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
- **Config:** `ML/SETTINGS/WINDYFLM.CFG`, plain `key=value` lines: film id, print, light, grain, halation, mist,
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
  `WF_ROW_HL` / `WF_ROW_SH` between Strength and Film LUT, no wrap at the ends. 9 rows do not fit the 19 px pitch above
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
- the roll state machine `wf_roll.c`, the darkroom task `wf_dark.c` with its pure parts `wf_darkp.c`, and the screens
  `ui/wf_dark_ui.c`.

None of it is code from the Film Roll author, and Canon's ROM JPEG encoder is not used: the CR2 is developed outside
Canon's pipeline, so there is no Canon YUV buffer to hand it. Nothing Genesis-related was opened. The reference video
gave the idea and the layout only. None of its code, bitmaps or fonts was opened, and no reference frame is in the repo.
Stocks keep their Windy names, and there are no brand logos.

**Roll (shooting).**
- **One roll at a time.** The roll picker (UI screen ROLL) has shelf tabs Colour / Slide / B&W / Cine / LUT, a can
  carousel, and 3 / 6 / 12 / 24 / 36 exposures (default 24). SET loads the roll and plays the same load animation as a film
  load in the browser ("Loading the roll", the can, the film's chips and "12 EXP"), then the UI closes and LiveView
  shows. A key skips the rest of the animation.
  - A roll with no frames yet is simply reloaded with the new film or count. A loaded roll with frames refuses a new
    load ("This roll has frames: PLAY develops it (Darkroom)").
  - **Close and load** (owner 11-15). A finished roll (full, or rewound with frames left) asks first: "Roll 021 has 12
    undeveloped frames. / Close it and load the new roll? / <film> x12 / (the RAW/JPG files stay on the card)", keys
    "SET Close and load / MENU Cancel" (`roll_confirm`, a scrim over the picker). SET calls `wf_roll_req_swap`: wf_task
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
  - It is read at boot. A file that does not parse means no roll (logged); an empty file (a save that did not finish)
    too.
- **Frames.** wf_task counts frames while Canon is idle (job_state 0, no shot pending) and the file number from
  `get_shooting_card()` has been stable over two polls. `wf_roll_seen` then adds every new number that has a CR2:
  - numbers wrap 9999 -> 1 with the folder + 1;
  - a jump of more than 64 means the counter was reset elsewhere: the roll re-anchors and adds nothing;
  - frames past the count are not added, and at the count the roll is FULL.
- **RAW+L forced.** While a roll can shoot, wf_task sets `PROP_PIC_QUALITY` to RAW + L fine when it differs. The old
  value comes back once the roll is full, rewound or closed, and at unload. A frame without a CR2 does not count.
- **The shot's JPEG** gets the module's look of the roll's film at once, so the card holds a picture: the normal
  8-bit look, or B's develop when the menu "RAW develop" is On.
- **Both modes at once** (owner 11-14 evening, replacing that afternoon's "one mode at a time"): with a roll loaded
  and RAW develop On, each RAW+L shot is developed by B right away (about 30 s, as without a roll) and also counts as a
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
    (`wf_ui_open_scr`, the same open as TRASH), and INFO is taken and opens the roll picker (a new roll, through the
    close-and-load question). Only while the box is up.
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
  - EOS M only (the 5D2 has no roll yet: `wf_roll_req_swap` returns -4 there).
- **End / close.** "End roll now" (menu) ends the roll early (the roll is rewound: no more frames); starting the darkroom rewinds a loaded roll first.
- **A full or rewound roll keeps** (until the Darkroom develops it or Close roll): the film loaded and locked (another
  film is refused; the same film plays the load animation); the film look on every later JPEG (and B with RAW develop
  On, if the quality Canon gets back is RAW+L: B develops only RAW+L shots); RAW+L and image review back to Canon's
  own values (forced only while the roll can still take frames); PLAY still blocked while Blind roll is On.
  "Close roll" gives up on the frames not yet developed: they become K and their CR2s stay untouched.

**Darkroom (developing).**
- **Entry.** ML menu "Darkroom" opens the darkroom screen. SET starts. MENU stops, after a second MENU to confirm,
  and closes the screen when nothing runs.
  - From the WindyFilm UI (owner 11-15, EOS M): "[PLAY] Darkroom" in the footer of the roll picker, and of the browser
    while a roll is on the camera. It is lit when the roll has frames to develop and dim otherwise, with the reason in
    the status pill ("No frames to develop", "Load a roll first"; `wfdr_dev_why`). PLAY opens the darkroom screen
    when lit. With no roll on the camera the browser's PLAY still closes it, as before. The end-of-roll notice's SET
    goes to the same screen.
  - Starting needs: not movie mode, a card, battery >= 30 %, and no B develop of a shot still running.
- **Task "wf_dark"**, prio 0x1F (below wf_task 0x1E and the worker 0x1C), lives for one run.
  - At start: PauseLiveView, powersave_prohibit, then the memory:
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

## 4. Interfaces

### 4.1 `src/wf_api.h`

Frozen: cameras, the catalogue struct, rows, load, status, UI entry points, log and card read. The core implements
everything except `wf_ui_*`. The UI calls only these functions plus ML's drawing API. Rows (2026-11-14): Light, Print,
Grain, Halation, Mist, Strength, Highlights, Shadows, Film LUT (`WF_NROWS` 9).

### 4.2 Catalogue

- `films/catalog.py` (data) holds the rows. It keeps every field the old catalogue had (id, code, name, type, iso,
  isocode, kelvin, ev8, family, order, grain, halo, spk, v21, lut, mode, prov, ready) and adds:
  - `short` (<= 11 chars at 13 px condensed);
  - `line1`, `line2` (each <= 44 chars at 15 px: our short description; the generic LUT-can line1 is rewritten);
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

## 5. Browser UI (B2 small text)

Mockups: notes folder `newmodule-mockups/mock_B2s_strip_x2.png` and `mock_B2s_strip_lut_x2.png`; drawing code in
`mockup.py` (`variant_b2s`).

The layout, 720x480 (the ML bitmap):

| area | content |
|---|---|
| header (0-33) | mark, "WindyFilm" argnor23, "by WindyStrife" 13 px, loaded chip 15 px |
| big can | 160x226 at (45, 40), glow in the film's colour (16 palette rings, recoloured per film), floor shadow |
| right column x 262 | name argnor28 at y 38; chips (kind / ISO / format) 13 px at y 72; SET Load button; description 15 px, 2 lines at y 96 / 113 |
| rows | 9 rows, 7 shown from y 136 at 19 px pitch; the list scrolls with the focus (2 px scroll bar at x 712); focus bar in the accent colour, chevrons on the focused value; Strength as 5 bars |
| tabs | Colour / Cine / B&W / Film LUT with counts at y 276, underline on the active one; counter "n / N" |
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
  animation and closes, without a load; on the roll picker the loaded roll's own film and count do the same (a full or
  rewound roll keeps its message). The errors keep the browser open with their message.
- **Never a dead camera (owner 11-14, EOS M: sometimes neither the film menu nor the ML menu opened):** the browser
  takes every key, so a UI task that does not run left both menus dead. The film worker (prio 0x1C) develops a shot for
  seconds (RAW develop 35-45 s), and wf_task (0x1E) builds the grain tables for 2-19 s without sleeping; the owner's
  RAWB2 log shows the old prio-0x1E UI task waiting out that build (DryOS does not time-slice equal priorities). The UI
  task now runs at 0x1B, above both and below ML's menu_task (0x1A) and Canon's GUI. It beats at every step; a key that finds no
  beat for 2 s goes to Canon / ML and asks the task to close when it runs (log `ui: no beat for N ms (state, phase)`;
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

- `build.sh CAM=EOSM|5D2` -> `build/<cam>/windyflm.mo` plus the card files (WFFILMS.DAT, WFCANS.DAT, WFFONTS.DAT, cubes
  are separate).
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
ML/DATA/WFFILMS.DAT, WFCANS.DAT, WFFONTS.DAT
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
