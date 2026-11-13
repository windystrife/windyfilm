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
  map hand-over is wired, as before), Strength 30 / 100 / 125.
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
- **Log:** `ML/LOGS/WINDYFLM.LOG`: boot line (camera, ROM check, hook result, pack and can status), and per shot: film,
  variant, timings.
- **Config:** `ML/SETTINGS/WINDYFLM.CFG`, plain `key=value` lines: film id, print, light, grain, halation, mist,
  strength, output, curve, Film LUT, LUT EI, preview, preview exposure, EXIF.
- **Coexistence:** do not load together with `filmroll.mo`. Symbols: everything except ML's `__module_*_windyflm` is
  made local (`objcopy --keep-global-symbols`), so the TCC loader never sees a clash.

## 4. Interfaces

### 4.1 `src/wf_api.h`

Frozen: cameras, the catalogue struct, rows, load, status, UI entry points, log and card read. The core implements
everything except `wf_ui_*`. The UI calls only these functions plus ML's drawing API.

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

- `npal` defaults to 112. The UI maps palette index i to hardware entry `base + i` (base chosen by the UI, section 5).
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
| rows | 7 rows from y 136, 19 px pitch; focus bar in the accent colour, chevrons on the focused value; Strength as 5 bars |
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
