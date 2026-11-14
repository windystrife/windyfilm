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
  - Loading a Film LUT can sets V-Log + LUT with the can's cube.
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
| footer | key hints 13 px: SET Load, L/R Film, UP/DN Row, Q Group, INFO Info, MENU Exit |

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
