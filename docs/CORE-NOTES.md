# WindyFilm core notes (WindyStrife)

Core = everything in `src/` except `src/ui/` and the frozen `src/wf_api.h`, plus `build.sh`, `test.sh`, `tests/host/`,
`tools/mk_catalog_c.py`, the generated-table tools (`mk_logtab.py`, `mk_logasm_tab.py`, `mk_loglut_tab.py`,
`mk_exptab.py`), `tools/logchk.c`, `tools/tcc_link.c`, `tools/wf_checksyms.py`, `tools/provenance_gate.py`,
`tools/gate_allow.txt`. Contract: `docs/DESIGN.md` sections 1, 3, 3.1, 4, 6, 9 and `src/wf_api.h` (unchanged by core).

## 1. State (2026-11-14)

| Stage | What | State |
|---|---|---|
| 1 | skeleton, both cameras link, gate | done |
| 2 | shot pipeline (worker, look, dispatch, chunk loop), effects, WFFILMS.DAT reader | done |
| 3 | ML menu "WindyFilm", WINDYFLM.CFG, EXIF UserComment, Light, Print, log | done |
| 4 | LiveView preview (display filter) + Preview exposure (EOS M) | done |
| 5 | host tests (`test.sh`) | done, see section 4 |

Not on a camera yet: nothing in this repo has run on an EOS M or a 5D2 body. The host tests run the camera objects
themselves under qemu-arm, with a shim for ML / DryOS.

## 2. Source map

| File | Role |
|---|---|
| `wf_main.c` | module info / CBRs (CBR_KEYPRESS, CBR_DISPLAY_FILTER) / strings, init / deinit, `wf_task`, the log ring, `wf_card_read`, `wf_status`, `wf_gen` |
| `wf_cam_eosm.c`, `wf_cam_5d2.c` | camera input layer (one per build): ROM word checks, hooks, gates, fills `struct wf_frame` |
| `wf_frame.h` | input stage (DESIGN 3.1): `struct wf_frame`, formats UYVY8 (done), RAW14 / LOG16 (reserved) |
| `wf_shot.c` | worker task `film_yuv`, `wf_look_now` (struct `wf_look`), `wf_dispatch` on (fmt, output), chunk loop + effects, event ring -> shot log / EXIF entries, Log + LUT tables, WFFILMS.DAT reader |
| `wf_menu.c` | state, rows of wf_api.h, `wf_load` / `wf_avail`, ML menu, WINDYFLM.CFG, EXIF writer, Light (WB), Print |
| `wf_pv.c` | preview display filter, its own film set |
| `wf_expsim.c`, `wf_expk.c` | Preview exposure "Smooth" (EOS M only; 5D2 build: no-ops) |
| `wf_film.c` | film tables: 17^3 table -> 2.5D set, `wf_film_words` kernel |
| `wf_loglut.c`, `wf_llk.c`, `wf_llk_arm.S` | Log + LUT: .cube streaming parser, .FRT cache, `wf_ll_words` (asm) / `wf_ll_words_c` |
| `wf_logout_arm.S`, `wf_logout_tab.c` | plain log outputs `wf_log_words` (ARMv5TE asm); C reference `tests/host/wf_logout_ref.c` |
| `wf_fx.c`, `yh_halo.c`, `yg_grain.c`, `yg_stocks.c`, `wf_mist.c` | halation + bloom, grain, mist; memory + hand-over protocol in the `wf_fx.c` header |
| `wf_ui_stub.c` | browser stubs, linked only when `src/ui/wf_ui.c` is missing (and by the host test) |

Thread and sharing rules are written at the top of `wf_main.c`, `wf_shot.c`, `wf_fx.c`, `wf_pv.c`: worker and hooks never
log, never touch files; `wf_task` does all card I/O; tables are published by one pointer / key store after the
worker is seen idle or parked.

## 3. Build

```
# WSL, repo root
CAM=EOSM bash build.sh            # -> build/eosm/windyflm.mo (+ WFFILMS.DAT, WFCANS.DAT, WFFONTS.DAT unless PACKS=0)
CAM=5D2  bash build.sh            # -> build/5d2/windyflm.mo
```

- Card .sym files and their md5 are fixed in `build.sh` (EOS M `EOSM_202.sym` 09ccc47d = the autoexec 782dbf15 pair,
  trap Q25; 5D2 `5D2_212.sym` aa702ecc). ML headers: EOS M `/root/ml/expsim/mls`, 5D2 `/root/ml/q5d2/ml`.
- Checks in `build.sh`: arch tag per object (v5T, `wf_logout.o` v5TE), no LDRD / STRD, no 64-bit division helper,
  wf_lenc layout for the log asm, globals = `__module_{info,cbr,strings}_windyflm` only, `tools/wf_checksyms.py`
  (every import in the card .sym, relocation types ML's loader handles), `tools/tcc_link.c` = the camera's own TCC
  loader object linked on the host under qemu (image size, module_info / strings read back), then the gate.
- Last build (2026-11-14): EOS M image 329,184 B (103 imports), 5D2 324,928 B (98 imports); .bss ~67 KB; relocations
  ABS32 / CALL / JUMP24 only. Big buffers are malloc'd at run time (shot film set 96.5 KB, Log + LUT tables 181 KB,
  preview set 96.5 KB, effect blocks from shoot memory / Canon heap).
- 32-bit division helpers (`__aeabi_idiv`, `uidiv`, `idivmod`, `uidivmod`) are imported: they are in both card .sym.

## 4. Host tests

```
CAM=EOSM bash test.sh                  # every EOS M mode
CAM=5D2  bash test.sh                  # every 5D2 mode (never both at once)
bash test.sh CAM=5D2 FX PACK:top       # some modes / one PACK case
```

`test.sh` takes the objects of `build.sh` (`build/<cam>/obj`, building them when missing), compiles the camera layer
again with `-DWF_HOST`, `src/wf_ui_stub.c`, `tests/host/wf_expoff.c` (ML struct layouts from ML's own headers),
`tests/host/host_ml.c` (ML / DryOS shim: pthread tasks, POSIX semaphores with DryOS timeouts, FIO on the current
directory, shoot memory from an arena, property / WB / ExpSim effects, hook registry, ROM words of both cameras) and
`tests/host/wf_test.c`, and links one static ARM Linux program run by `qemu-arm-static`. Frames are mapped at
0x48000000 / 0x4C000000 (inside the shoot pool range the camera layer accepts) with a PROT_NONE page behind each; the
rest of each buffer holds a sentinel that must survive. Film tables are synthetic (identity, shifted, mono) written into
a test WFFILMS.DAT; cubes are identity .cube files. No author table, cube or binary is read.
Each mode runs in `build/<cam>/test/run/<mode>` (a fresh card), log `build/<cam>/test/<mode>.log`.

| Mode | What it checks |
|---|---|
| SHOT | init (module_info, CBRs, hooks, pack index), film shots bit-exact vs `wf_film_build` + `wf_film_words` and the expected shift, Strength 30 / 125 %, B&W mono, identity (every code within 2), aspects 1:1 / 4:3 / 16:9 packed with nothing written past them (EOS M), gates JPEG M / RAW / HDR / r2 / bad buffer (EOS M), Diana halves (EOS M), Log 1..5 vs `wf_log_words`, Log + LUT (tables = `wf_loglut_load`, shot vs `wf_ll_words`, asm = C kernel, .FRT cache written and used, missing cube, a Film LUT can), Off / no film untouched, preview bit-exact + Fast + gates, keys |
| SHOT:nocam | wrong ROM word: "Unsupported firmware", no hook, WB never touched |
| SHOT:busy | patch_hook_function says "already patched": status "Disable filmroll.mo" |
| 5D2 | (5D2) TOP / BOT pairing: BOT alone plain, owed BOT, 20 s timeout, r1 / RAW / buffer gates, LogC3 halves |
| FX | grain + halation keys ready, both run, glow (luma + red) round a light, zero-mean grain far away, 1:1 = no halation (EOS M), plain log no effects, no shoot memory -> shot without halation -> retry, everything freed |
| MIST | white mist glow, far dark unchanged, light stays bright, halves = whole frame and no seam step, black mist less wide glow, freed |
| EXIF | `wf_ex_write` LE / BE / filled / not JPEG / no Exif / short field / missing; ring vs IMG numbers: one shot, two shots while busy, RAW-only gap, a refused JPEG keeps the numbers, EXIF info Off |
| EXPSIM | dEV metered / manual / bad, smoothing + hysteresis + clamp + flash, gain tables, ExpSim off by us, filter output bit-exact (q -8), back on, a write that does not take, given back at unload (5D2: stays Camera) |
| CFG | every key read (case, spaces, unknown keys), saved 1.5 s after a change in the exact format, garbage values / >600 B ignored, userwb restored at unload |
| PACK | ok (+ `wf_card_read`: size, short read, missing, skip), missing / magic / index -> films plain, top byte / checksum -> that table dropped, wrong code / flags -> skipped, duplicate -> first wins, real (data's pack, all ready films have their scan table, sweep clean) |
| LIGHT | Day (film kelvin), Cloudy, Tungsten, Auto restores mode + shift, config keeps userwb, film change follows its kelvin, kelvin changed behind Light's back is set again, unload restores |
| PRINT | Scan / Cine / Paper rows and shots, wrap, scan-only film greyed, pack without the print = scan, kept across films, greyed for log |
| HS | Highlights / Shadows: `wf_hs_bend` vs the formula for all 121 pairs (identity and a film's N), never down, `WF_FT_HS` packing; 0 / 0 film sets bit-identical to the builder of 7f9e294 (FNV), h / s change N only; rows (no wrap), film shot + shot / yuv log + EXIF + preview (rebuilt on a change); Log + LUT bent after the load, `.FRT` unbent, grey at the bent N, an h / s change bends again without a load, a new EI loads and bends; flat Log / Off ignore it; `hl` / `sh` saved and read |
| LOGCHK | `wf_log_words` asm vs C reference on all 2^24 (U, V, Y) x 2 sets + random + small + n <= 0, qemu `-cpu arm946` |

Results 2026-11-14: see section 9.

## 5. Provenance gate

`tools/provenance_gate.py --mo build/<cam>/windyflm.mo --packs build/<cam> --core-sym <card .sym>` (build.sh runs it).
Deny input: the old module's binary (path assembled inside the script, read only). Rules as implemented:

- strings >= 6 chars of the old binary found in our .mo / packs: allowed only via `tools/gate_allow.txt` sections
  (`str`: ELF section names, ML module API words, format strings, English / Canon words, public film stock names) or
  when the string is a core .sym name or a run of one character. `deny-ok` lines (exact coexistence texts:
  "Disable filmroll.mo", the card folder names `EOSM_FilmRoll`, `5D2_FilmRoll_A13`) are cut out before any check.
- 32-byte windows of allocated PROGBITS equal to the old binary's, ignoring windows where one byte value fills >= 28 of
  32 (zero / fill runs).
- deny words (case-blind: author name, FILMCAN, FILMROLL, DER file names, Genesis names) in `src/ tools/ films/ cans/
  tests/ build.sh test.sh`, the .mo and the packs (not `docs/`; the gate's own two files are skipped).
- byte-equal files vs every DER file under `/root/ml/wf_src` (hashes).
- `WF_GATE_SHOW=n` prints more findings.

## 6. Card files, config, log

- `ML/SETTINGS/WINDYFLM.CFG` (no MODULE_CONFIGS on purpose: ML would write a file of the same name): `input=jpeg`
  (reserved), `film=<id>`, `output=film|log|lut|off`, `curve=slog3|vlog|logc3|clog2|clog3`, `flut=technical|<code>`,
  `lutei=-3..6`, `print=scan|cine|paper`, `light=auto|day|cloudy|tungsten`, `strength=subtle|normal|strong`,
  `grain=off|fine|normal|strong`, `halation=off|halation|bloom|spk|spkbloom`, `mist=off|w8|w4|w2|w1|b8|b4|b2|b1`,
  `preview=off|on|fast`, `exposure=camera|smooth`, `exptrim=-16..16`, `exif=on|off`, `userwb=mode,kelvin,gm,ba`
  (only while Light holds the WB). Written by `wf_task` 1.5 s after a change, at most 600 B.
- `ML/LOGS/WINDYFLM.LOG`: appended across boots. A boot starts with `WindyFilm <date>: camera ..` (logged in
  `wf_init`); at the first write of a boot a log over 512 KB becomes `WINDYFLM.OLD`. Lines wait in a 16 KB byte ring
  until the card takes them (a refused open / a short write is retried every 200 ms, logged once as `log: card refused N
  writes in .. ms (first: <step> rc <rc>, job <j>), lines kept`); a line that finds the ring full is counted
  (`log: N lines lost so far`). Card files are written through `src/wf_fio.h` (wf_main.c): a create, remove or
  rename only while Canon is idle (`lens_info.job_state` 0), a create only once the name is verified gone, since a
  create over an existing name leaves two entries of that name (the 11-14 card: two `WINDYFLM.LOG`, trap W24); appends
  and writes at an offset open the existing file. The log, WFPROBE.BIN, WINDYFLM.CFG and the `.FRT` caches all use it;
  gate check 5 fails the build on a direct `FIO_CreateFile` / `FIO_RemoveFile` / `FIO_RenameFile` anywhere else.
  Formats: only `%d %x %X %s` (+ `-` / `0`, width); `tools/fmt_check.py` fails the build otherwise (trap W23).
  Useful lines: `hook NN ff169414 rom e92d41fc err 0`, `pack ok: N tables`,
  `pack <code> v<variant> loaded`, `yuv NN <buf>/<raw>: film <code> v0 s100 <ms> h<ms> g<ms> m<ms> q<ms> a<aspect>`,
  `shot #n <title>: <s> = look .. + mist .. (TOP .., BOT ..) EV..`, `lut <cube> 33^3 ok|cache ..`, `exif IMG_nnnn.JPG ok`,
  Highlights / Shadows when not 0: ` hl+3 sh-2` in the yuv, shot and lut lines, `lut: highlights +4 shadows -2` (bent again),
  `fx halation|grain|mist .. ready`, `light Day: 5500K`, `expsim: ..`.
- EXIF: UserComment (0x9286, 264 zero bytes on Canon) overwritten in place with `ASCII\0\0\0WindyFilm: <title> | Print ..
  | Strength ..% [| Highlights ..] [| Shadows ..] [| Light ..] [| Grain ..] [| Halation ..] [| Mist ..] [| EV..]`, verified by read-back; never a non-empty
  field, never a rewrite.

## 7. RAW develop stage: how it plugs in (DESIGN 3.1, 3.2)

Nothing RAW is implemented yet. The owner chose **Hybrid + Canon curve inversion** (2026-11-14); DESIGN 3.2 has the
plan, the numbers and the traps. Exact touch points per step:

- **A1** (done for Log + LUT and the cans; curve `luts/canon_curve/EOSM_Film1.csv`, Picture Style Film 1; slot 2
  Canon Neutral is wired and waits for `EOSM_Neutral.csv`):
  - `tools/mk_loglut_tab.py <out> [film1.csv|-] [neutral.csv|-]`: `read_curve()` checks codes 0..255, strictly
    increasing, 0 <= v <= 4.0; `wf_ll_dec_c<k>[1022]` interpolates per quarter code, with `WF_LL_HAVE_C<k>` and
    `WF_LL_C<k>_SRC`. `build.sh` passes each slot's CSV when it exists (env `CURVE_FILM1` / `CURVE_NEUTRAL` override);
  - slots (`WF_LL_NINC` = 3 = `WF_INC_N`): 0 sRGB, 1 Film 1 (Picture Style 0x21), 2 Neutral (0x84, CSV pending).
    `wf_ll_curve_have(k)` / `wf_ll_curve_src(k)`; a slot without a CSV decodes as sRGB (`wf_ll_inc()` = 0);
  - `wf_loglut.c`:
    - `struct wf_ll_b.dec` picks the table;
    - in `wf_ll_coords` the gain multiply (`eik`) and the log-segment up-shift (`eib && x > 1.0`) are separate steps.
      `wf_ll_build` sets `eib` / `eid` for the curve even at gain 1;
    - `wf_ll_lin8` follows the table, and the auto grey reference is `WF_LL_GREY18` (0.18 Q24);
    - the cache key gets `| slot << 17` (bits 17-18; Film 1 keeps bit 17);
  - state: `wf_incurve` / `wf_incurve_t()` (wf_menu.c, cfg `incurve`), `wf_look.incurve`. `wf_ll_poll` reloads on a
    change (`wf_ll_tried_inc`), and `wf_dispatch` stamps the tables' value on the look;
  - host tests (SHOT), per slot built in: the curve's first code at 0.18 comes out within 2 codes of sRGB code 118;
    if code 250 is above 1.25 it comes out at least 8 codes above sRGB white; the slots' tables differ from sRGB and
    from each other; going back to sRGB gives the same tables; a slot without a CSV reads sRGB; CFG round trip plus
    `incurve=Neutral`. `wf_test.c` includes the generated `wf_loglut_tab.h` for the decode tables (`t_dec`);
  - Picture Style: `wf_incurve_t()` also needs `wf_style_ok(k)` = `wf_style_why(k) == WF_PSW_OK`:
    `lens_info.raw_picstyle` is the slot's style (`wf_inc_ps[]` in wf_menu.c), for Film 1 the User Def. 1 base
    (`wf_ps_base`, own `prop_add_handler(PROP_PICSTYLE_OF_USERDEF1)` in `wf_menu_init`) is below 0x80 (a style file),
    and `picstyle_get_current_contrast()` equals `wf_inc_con[]` (Film 1 -2, Neutral 0). Unknown values pass. New
    imports: `picstyle_get_current_contrast` (both), `prop_add_handler` (5D2; EOS M had it). The host test uses
    `h_li_set("picstyle", ..)`, `h_ps_contrast` and `h_prop_send(0x0206000a, ..)`: the other curve's style, another
    Contrast and base Auto (0x87) each reload the sRGB tables; the right values bring the curve's tables back;
  - a synthetic slot-2 CSV (scratch, never committed) checks the Neutral path before the measured curve exists:
    `CURVE_NEUTRAL=<csv> OUT=<scratch dir>` for both build.sh and test.sh;
  - still open: flat Log (`wf_dec`, K fold).
- **P0** (EOS M a675c8a, record version 2 since 11-14 afternoon; 5D2 from `raw-path/P0_5D2_SPEC.md`, record 0x0102).
  Host modes `PROBE` + `PROBE:boot` (EOS M), `PROBE` (5D2). The camera-independent part is `wf_probe.c` /
  `wf_probe.h`:
  - one record slot per probed call kind (`PRB_NK` 2: the 5D2 BOT comes a few hundred ms after its TOP, often before
    wf_task flushed the TOP), each a `struct wf_prb` (4696 B; `mode` bits 8-15 = the kind) plus the source addresses
    the camera named; a call whose slot is still full is skipped and counted; up to `PRB_MAX` 64 records a boot;
  - the camera layer hands a `struct wf_prb_cam` to `wf_prb_setup()` in `wf_cam_init()`: pool bounds, log names, and
    `read()` (block words, register words, the optional ROM call, raw source addresses) and `cwin` (per source, the
    window the 1 KB copy comes from). EOS M: `prb_read_eosm` (parameter block 0x41E82CEC, five EDMAC words,
    `ff234680`; sources raw, raw2 = `prb_other()` (the other raw buffer, +-32 MB), A0, A1; an empty block takes the raw
    from WR0; copies raw / raw2 from +16 MB, A0 / A1 from +0);
  - 5D2: `prb_read_5d2` (the spec's pb[56] table: job handle from the FrontDevelop / RearDevelop state, H[0..4],
    P fields and P[0..14] only for a live job (H[0] = 0xFFCD0761), the engio shadow base [0x8FBC] and 9 EDMAC shadows
    read inline, this half's Mem1ToYuv block; registers 208 / a08 / 308 / d08 / 408; sources raw0 = MEM1 + 0x76C8, qr,
    a08, m2r; ROM getters 0xFF8AC4B8 / 0xFF8AA9D4 / 0xFF8AC4F8 / 0xFF8AEB84 with H and 0xFF9A550C with 0xC0F04208,
    live job only; pointers only when aligned and inside RAM, sources only inside 0x44000000..0x5C000000);
  - hook: `wf_prb_enter(regs, kind)` at the probed call (EOS M: NN with r2 == 0; 5D2: TOP / BOT; before the gates),
    then `wf_prb_leave(kind, rc, t)` re-hashes the windows after the worker or the reject;

  `wf_cam_poll()` (wf_task) calls `wf_prb_flush()`: the lines of a taken record go to the log at once; the record is
  copied to `prb_out` (the hook may take the next one) and written at its own offset (`prb_written` x 4696; the first
  record of a boot makes a new file, once Canon is idle and the old file is verified gone), so a retry after a refused open or a short write never shifts the file. Retries every
  200 ms for 30 s, logged as `probe: record #N not written .. yet: <step> rc <rc> (shot job <j>)` / `.. written .. on
  try K`. The full slots are logged in seq order, then go one by one through `prb_out`. The menu entry "RAW probe" is
  Off / On / On + ROM call and is not in the config (both cameras). The host shim fakes the parameter block, the EDMAC
  words and the ROM lookup (`wf_host_mem_read`, `wf_host_memnavi`); for the 5D2 Canon words by address (`h_mem_set`)
  and the getters (`wf_host_romcall`); the EOS M other raw buffer and the 5D2 raw are mapped at 0x46000000 /
  0x50000000 (`fc`).
  The probe's own `" %08x"` lines are built word by word: a long repeated format string matched the old binary in the
  gate.
- **H**: the aux-plane fields in `src/wf_frame.h`; `wf_cam_eosm.c` fills them from what P0 found. A new stage goes in
  `wf_shot.c` `wf_dispatch()` for (UYVY8 + aux, Log + LUT); it works on bands or after a scan pass (DESIGN 3.2). The
  reconstruction is a new file `src/wf_hl.c`, with a host test on a synthetic raw plane.

General places a RAW path touches:

1. Camera layer (`wf_cam_*.c`): a hook where ML's raw buffer is valid fills `struct wf_frame` with `fmt =
   WF_FMT_RAW14`, `data` = the raw buffer, `dst` = Canon's UYVY buffer of the same shot (or a LOG16 work buffer),
   `black`, `white`, `cfa`, `bpp`, geometry and `part`. Only this file knows where those buffers are.
2. `wf_dispatch()` in `wf_shot.c`: today `fmt != WF_FMT_UYVY8` returns `WF_ST_NOSTAGE` (logged "nostage", frame
   untouched). A develop stage adds a case: develop RAW14 -> UYVY8 into `dst`, then run the unchanged UYVY8 cases on a
   frame whose `data = dst` (film / log / Log + LUT / effects need no change); or develop RAW14 -> LOG16 and call a
   16-bit look stage (new kernels, own tables) per output.
3. The look (`struct wf_look`) is already resolved independently of the format; effects take chunks of words from
   `wf_colour_fx`, so a 16-bit path needs its own chunk loop only if it does not go through UYVY8.
4. Config: `input=jpeg` is written and read (ignored) today; a RAW path adds `input=raw` and a menu choice. The UI and
   `wf_api.h` do not change unless the browser must show the input.
5. Memory: a RAW14 frame of 5184x3456 is 31 MB; the develop stage needs its own shoot-memory plan (wf_fx.c rules: no
   SRM fallback, give it back in playback).

## 8. Traps

- Windows python is broken: every Python runs in WSL, `python3 -I -B`. Long WSL commands go through script files
  (no `$` or heredocs on the `wsl.exe` command line).
- EOS M card: autoexec and .sym are ONE pair (782dbf15 / 09ccc47d); a module linked against another .sym loads and
  then calls wrong addresses.
- Imports must exist in BOTH cards' .sym (5D2 has no `get_expsim`, `prop_add_handler`, `prop_request_change_wait`,
  `bmp_*`: only the EOS M camera layer / expsim use them).
- No LDRD / STRD, `-march=armv5t`, no 64-bit `/` or `%` (`__aeabi_ldivmod` is not exported): `wf_loglut.c` has its own
  `wf_ll_udiv64`.
- DryOS: `take_semaphore(s, 0)` waits for ever, a timeout returns 9; task ids and handles are even, errors odd.
- The worker and hooks never log or touch files; a parked worker (`wf_parked`) counts as idle for `wf_task`'s waits.
- EOS M `wf_exp_vpp` defaults to 0x404C8 (a camera address): host code must point it at a fake before init.
- `pic_quality & 0xFF00` != 0 is JPEG M / S (not hooked); RAW only = top byte 0x04.
- The ML menu entry "Open film browser" only requests; `wf_task` opens the browser after the ML menu has closed
  (up to ~2 s). `wf_ui_close` is called only from module deinit (a task that can sleep).
- `build.sh` exits non-zero when the gate fails, after the .mo is written (`test.sh` still finds the objects).

## 9. Results and open items

Last run 2026-11-14, per camera `CAM=<cam> PACKS=1 bash build.sh` then `CAM=<cam> bash test.sh` (EOS M first, then 5D2;
the repo at d1e7883 + core files not yet committed).

Build:

| | EOS M 2.0.2 | 5D2 2.1.2 |
|---|---|---|
| image (camera TCC loader, host) | 329,184 B | 324,928 B |
| imports (all in the card .sym) | 103 | 98 |
| relocations | ABS32 2203, CALL 77, JUMP24 8 | ABS32 2147, CALL 72, JUMP24 8 |
| packs | WFFILMS.DAT 964,156 B, WFCANS.DAT 1,356,112 B, WFFONTS.DAT 22,111 B | same |

Gate (both builds): strings 560 checked -> 0 found; 32-byte windows -> 0 equal; byte-equal 29 DER files -> 0 equal;
deny words: one finding, `films/catalog.py` line 390 (the `header()` function of data's catalogue script, which writes
an old-style `fr_catalog.h` that the WindyFilm build does not use, has the comment
`/* max compiled id = the sub_100 clamp (patch_mo.py) */` next to `FR_CAT_IDMAX`). The scan covers `films/`, so
`build.sh` ends with "gate: FAIL" until that line is changed; the core files, the .mo and the packs are clean.

Host tests (checks passed / failed):

| Mode | EOS M | 5D2 |
|---|---|---|
| SHOT | 89 / 0 | 72 / 0 |
| SHOT:nocam | 10 / 0 | 10 / 0 |
| SHOT:busy | 7 / 0 | 7 / 0 |
| 5D2 | - | 23 / 0 |
| FX | 21 / 0 | 20 / 0 |
| MIST | 20 / 0 | 18 / 0 |
| EXIF | 27 / 0 | 27 / 0 |
| EXPSIM | 24 / 0 | 8 / 0 (no Preview exposure on 5D2) |
| CFG | 17 / 0 | 17 / 0 |
| PACK (10 cases) | 111 / 0 | 111 / 0 |
| LIGHT | 18 / 0 | 18 / 0 |
| PRINT | 37 / 0 | 37 / 0 |
| LOGCHK | 5 / 0 | 5 / 0 |
| total | 386 / 0 | 373 / 0 |

Some measured values (EOS M; 5D2 in brackets where they differ): film A scan table (+12 Y) mean dY +11.97, Strength
30 % +3.99, 125 % +14.97, identity / mono / Diana halves +0.00; grain far from a light sd 1.16 (1.22); halation glow dY
+42.2 (+32.5), red dV +1.65 (+1.40); white mist glow +49.5 (+40.5), wide +2.77 (+2.32), black mist wide +0.80 (+0.60);
mist halves vs whole frame -0.12; data's real pack: 49 tables, scan table for 30 of 30 ready films, 19 print variants;
Light Day 5500K from the film, Cloudy 6900K.

Open items:

1. `films/catalog.py` line 390 (data owner): drop "patch_mo.py" / "sub_100" from the generated comment; the gate then
   passes. Core did not edit it (not a core file).
2. `tools/gate_allow.txt`, section "public film stock and maker names": strings that the old binary also contains
   because both use the makers' product names (and "very fine grain." from the catalogue text). They are listed one by
   one for the lead / owner to accept or to have renamed in `films/`.
3. Nothing has run on a camera. First card test, EOS M: log line `hook NN ff169414 rom e92d41fc err 0`, one JPEG L per
   aspect, `exif IMG_nnnn.JPG ok`, Preview on / Fast, Light, Log + LUT with a real cube; 5D2: TOP / BOT pairing in the
   log (`TOP .., BOT ..`), one shot per output. With `filmroll.mo` still loaded the status must read
   "Disable filmroll.mo" and nothing is hooked.
4. RAW develop stage: "B" is wired on the EOS M on branch `raw-develop` (section 10.7: menu "RAW develop", RAW+L single
   shots, about 32-40 s per shot), not run on a camera yet. The 5D2 has none. Config `input=raw` is not used.
5. Preview exposure "Smooth" is EOS M only (5D2 build: no-ops, menu shows Camera).
6. Real timings (shot ms per film / effect) exist only from the camera; host times under qemu say nothing.

## 10. RAW develop "B" (branch `raw-develop`, wired on EOS M, not run on a camera)

The full in-camera develop of DESIGN 3.2 "B": an optional slow mode. It develops Canon's 14-bit raw of the shot and
writes the graded UYVY 4:2:2 frame into Canon's YUV buffer, so Canon's JPEG encoder runs as usual. EOS M only; the
develop code takes the 5D2 matrix too, but the 5D2 module has no RAW develop (`WF_RAWB_CAN` 0). Since 2026-11-14 it is
wired (10.7): menu "RAW develop", RAW+L single shots. 10.1-10.6 are the develop itself as first written; 10.7 is what
the wiring changed and measured.

### 10.1 Files (all new; no existing file changed)

| File | What |
|---|---|
| `src/wf_dev.h` | API, tables, job / stat structs; the design summary in the header comment |
| `src/wf_dev.c` | load-time builder, per-shot prepare (pass A, WB fit, highlight map, matrix), rows driver, highlight path, C reference of the span kernel |
| `src/wf_dev_arm.S` | `wf_dv_span_s`: fused MHC demosaic + matrix + CLZ encode + tetrahedral + UYVY pack for runs without clipping (ARMv5TE: SMULxy / SMLAxy) |
| `tests/host/wf_dev_test.c` | host test, modes `px frame full bad span cr2` |
| `tests/host/wf_dev_scene.c/.h` | synthetic shot: one linear scene rendered as a packed 14-bit raw and as Canon's UYVY frame; cube writer |
| `tests/host/wf_dev_shim.c` | the few ML / FIO / memory calls the test and the bench need |
| `tools/dev_check.sh` | objects for both cameras + build.sh's object checks + the host test |
| `tools/dev_bench.c`, `tools/dev_bench.sh` | memsim timing on the EOS M frame |
| `tools/mk_devmat.py` | the camera matrices in `wf_dev.c` (dcraw's method from the public ColorMatrix numbers), `--check` |
| `tools/cr2_dec.c` | own minimal CR2 reader: raw.r14 (ML layout) + meta.txt (ISO, measured black, as-shot WB, clip pile-up) |
| `tools/jpg_uyvy.py` | Canon's JPEG -> UYVY at the raw window, alignment check by phase correlation; UYVY -> PNG |
| `tools/dev_a1ref.c` | the JPEG path's reference with A1's Canon curve (`ref_a1.uyvy`) + the curve table for the fit (`dec.bin`) |
| `tools/dev_cr2.sh` | the real-shot test on RAW+L pairs |

### 10.2 Pipeline and fixed point

Per shot `wf_dev_prepare` (worker, before the first row):
1. **Pass A** over the raw at quad (2 x 2) resolution: the clipped quads and the highlight ratio samples (below), plus
   the WB fit samples.
2. **WB + exposure fit** against Canon's own UYVY frame of the shot (read before any row overwrites it). It takes
   near-neutral, unclipped mid-tones on a 16 px grid, decodes them with `job.dec` (the JPEG's input curve: A1's Canon
   curve slot, or sRGB), goes to camera space and sums the ratios. So the develop follows Canon's AWB, WB shift, ISO and
   exposure without reading a property. A caller may pass its own scales (`job.wb_q8`).
3. **Highlight map:** per 32 x 32 px block, R/G and B/G come only from quads whose brightest photosite is 55-92 % of clip
   and that have no clipped photosite within 2 quads (>= 3 px). Holes are filled from neighbour blocks (8 rounds), then
   neutral.
4. **One matrix** M = scene gain x (Rec.709 -> log gamut) x (camera -> Rec.709) x per-shot scales. The coefficients
   are int16 at shift sh, the largest with sum |M| x 57344 < 2^31 - 2^sh, and the result is floored. sh >= 7 is `fast`:
   x < 2^24, no upper clamp.

Rows `wf_dev_rows(ya, yb)` take any band, so the NN full frame and the Diana halves run the same code. Per output pixel:
1. Unpack the 14-bit rows to int16 with black subtracted, into a 5-row ring in cached scratch.
2. MHC 5 x 5 demosaic. The values fit int16: max 28670 at black 0.
3. M by SMULxy / SMLAxy.
4. CLZ index `((a << c) >> 23) - (c << 8) + (23 - sh) * 256`, with c = clz(a | 256 << sh). The table entry is
   `(cell x word stride) << 9 | frac7` and folds in the log curve, the cube DOMAIN and the clamp.
5. Tetrahedral interpolation over a 33^3 node table. The lanes are Y9 + 1 | (Cb9 + 1) << 16 and Cr9 + 1: the + 1 is the
   rounding, and the weights sum to 128. Y per pixel, Cb / Cr as the pair mean.
6. Pack the UYVY word into a cached row, then `wf_copyw` it into Canon's buffer.

Node sources (`wf_dev_build`, wf_task): `WF_DEV_CUBE` (the Log + LUT / Film LUT cube resampled at the nodes, with the same
log curves, gamut, LUT EI and Film LUT auto exposure as `wf_loglut.c`), `WF_DEV_LOG` (plain log) and `WF_DEV_SRGB` (a
Canon-like 8-bit base frame for the film tables: `wf_film_words` then runs unchanged on the developed frame).

**Kernel split.** Runs of blocks without clipping go to `wf_dv_span_s` (asm, about 110 instructions per pixel, one
stack slot per pair). Inside a block with clipping, a per-pair dirty mask still sends the clean pairs to the span
kernel. Only the dirty pairs take the C highlight path:
- one or two clipped channels are rebuilt from the unclipped ones with the block's ratios;
- all three clipped: neutral at the brightest white-balanced channel. That is the lowest level consistent with every
  channel being clipped, so a fully clipped sky sits above Canon's JPEG clip (10.4).

The output of the asm path is bit-exact with the C reference (`wf_dev_kernel(1)`; mode `span`, the frame tests,
cropped windows).

Memory: tables 356,400 B (`struct wf_dev_tabs`), scratch `wf_dev_scratch_bytes(5184, 3456)` = 392,472 B; both cached,
shoot memory. Code: `wf_dev.o` 32,521 B text, 56 B bss. Imports: `__aeabi_idiv`, `__aeabi_idivmod`,
`__aeabi_uidiv`, `__mem_malloc`, `__mem_free`, `memcpy`, `wf_copyw`, `wf_lenc`, `wf_ll_parse` (no 64-bit division,
no LDRD / STRD; the C part is v5T, the object v5TE like `wf_logout.o`).

### 10.3 Timing (memsim, ARM946E-S model 200 MHz, EOS M 5184 x 3456, NCNB raw and frame)

Synthetic scene, 11 % highlight pixels (`FULL=1 bash tools/dev_check.sh; bash tools/dev_bench.sh`):

| Version | rows | shot (prepare + rows) |
|---|---|---|
| C only (`WF_DEV_NO_ASM`) | 30.57 s (4110 M instructions) | 34.00 s |
| two-pass asm (MHC, then LUT) | 26.03 s (2913 M) | 29.47 s |
| **fused span kernel + dirty-pair masks (this branch)** | **25.59 s** (2893 M; core 20.7 s, stall 4.87 s, 16.6 M linefills) | **29.03 s** |

`wf_dev_build` 290 ms (load time), prepare 3.44 s (396 M instructions), checksum bbbeb78c (= `wf_dev_test full`).
That is under the 43 s baseline and inside the report's 25-30 s guess, but above the 25 s floor. Where the rest goes:
- the span kernel: about 2.0 G instructions;
- the highlight path: about 0.7 G;
- pass A: 88 instructions per quad.

The two-pass version lost its gain to D-cache misses: the ARM946E-S does not allocate on a store miss, so the temporary
buffer was refetched. Hence the fused kernel with a stack slot.

**Real raws are slower than the scene.** The fixed-WB job, the shipped object (10.4):

| shot | prepare | rows | shot |
|---|---|---|---|
| IMG_0208 (no clipping) | 3.36 s | 25.91 s (core 15.8 s, stall 10.1 s) | 29.3 s |
| IMG_0210 (no clipping) | 3.17 s | 27.17 s (stall 11.4 s) | 30.3 s |
| IMG_0233 (1.5 M highlight px) | 3.20 s | 32.38 s (stall 11.9 s) | 35.6 s |

Real colour spreads the tetrahedral lookups over more of the 356 KB node table; the D-cache is 8 KB. So 29-36 s per
shot. A sky as large as IMG_0206's (4.8 M highlight pixels) would add more.

The next step for speed is to cut the node-table misses. Candidates:
- a smaller node table: 17^3 nodes, or 16-bit lanes;
- going through the rows in tiles.

### 10.4 Quality

Synthetic (`dev_check.sh`, 478 checks with `FULL=1`, 0 fail):
- pixel look vs the JPEG path on the same light: Y mean 0.32-0.43, max 2 codes, over 5 curves, cubes 2 / 17 / 33 / 65,
  LUT EI and auto exposure;
- frame patches: Y mean 0.45 (max 1.34), C 0.23;
- WB fit within 0.04 %; exposure +0.3 %;
- highlights: one or two clipped channels keep R/G, B/G within 1 %.

Real EOS M RAW+L pairs (`tools/dev_cr2.sh`, WSL; the photos stay outside the repo):
- The JPEG L sits at raw (84, 64), exactly (phase-correlation offset 0, 0 on every pair). The CR2's image data runs
  72..5280 x 52..3528. Black measures 2047-2048.
- **The white level depends on the ISO and must come from the camera.** Clip pile-ups measured in the 35 CR2s:

  | ISO | clip |
  |---|---|
  | 100 | 13582-13583 |
  | 160, 320 | 12635-12636 |
  | 200 | 15303 |

  The fixed 15200 of the brief left the 12636 pile unflagged at ISO 320: green clipped, the white-balanced R and B rose
  above it, and the sky came out magenta. `cr2_dec` now writes the pile-up when a real one exists (>= 1000 photosites
  at one value and 8 x the density just below it). The test uses that value; without one it falls back to the table
  above, the same values as `raw-path/proto/curve/match.py`.
- Day09 is shot in Picture Style Film 1, so the JPEG path's reference must use A1's Film 1 curve. With the sRGB decode
  the comparison is off by about 20 codes; that is the JPEG path's error, not the develop's.

All 35 pairs (`tools/dev_cr2.sh`: 34 Day09 Film 1 pairs, A1 slot 1; IMG_6821 Neutral, slot 2) pass, 12 / 0 each:
- the WB fit is within 1.35 % (R/G) and 1.43 % (B/G) of Canon's as-shot WB on average, worst -2.2 / -5.2 %;
- on flat 16 x 16 blocks without clipped raw, the develop against Log + LUT of Canon's JPEG with A1's curve:
  |dY| 1.68 on average (worst pair 5.24), p90 3.2, |dC| 1.00.

| pairs | ISO | white | mean abs dY (unclipped) | mean abs dC | blocks with clipped raw | dY on those blocks |
|---|---|---|---|---|---|---|
| 0206-0226 (21) | 320 | 12636 (the 3 shots without clipping ran with the old 15200 fallback) | 1.63 (0.41-5.24) | 0.91 | 0-44 % | +2.4..+19.4 |
| 0227-0230 (4) | 100 | 13582 | 1.17 | 0.63 | 14-29 % | +8.1..+31.2 |
| 0231-0233 (3) | 160 | 12636 | 1.06 | 0.52 | 19-22 % | +17.2..+21.5 |
| 0234-0239 (6) | 200 | 15303 | 2.67 | 1.93 | 0-26 % | +7.0..+22.7 |
| 6821 Neutral | 200 | 15303 | **0.67** (p90 1.3) | 0.32 | 0.2 % | - |

On blocks with clipped raw the develop is brighter than the JPEG path by design. Canon's JPEG (so A1 too) stops at its
clip; the develop rebuilds the highlights, and a fully clipped area sits at the brightest white-balanced channel, the
lowest level consistent with the raw. Eye check (IMG_0206 sky, IMG_6821 street): the sky is neutral, tone and colour
match ref_a1, and the develop is a bit crisper. If the owner prefers Canon's clipped look there, it is a one-line change
in the all-clipped branch of `wf_dv_hl`: cap at the green clip.

B renders for the A1-vs-B sheet: IMG_0208 / 0210 / 0233 were run with Canon's as-shot WB and match.py's fixed scene
scale (grey = 11352 / 2^3.3 DN), V-Log, `T250DMVL.CUB`. Notes and files are in the shared notes folder, `a1-vs-b/`.
- **memsim on the real raws:** 29.3 / 30.3 / 35.6 s per shot. Memory stalls in the rows are 10-12 s, against 4.9 s
  for the synthetic scene: real colour spreads the tetrahedral lookups over more of the node table.
- **Against main's A1 Log + LUT, same cube:** median dY -0.4 / -0.8 / -0.2, except the rebuilt highlights.

### 10.5 Integration points (for the merge, after P0)

1. **Camera layer** (`wf_cam_eosm.c`, at the hook where the raw is valid) fills a `struct wf_dev_job`:
   - `raw`, `pitch` 9240;
   - active area `ax0 72, ay0 52, ax1 5280, ay1 3528` (the CR2's numbers; check against ML's `raw_info` on the camera);
   - `x0 84, y0 64` = Canon's JPEG L crop (any even x0, y0 work);
   - `w 5184, h 3456` for L 3:2 (other aspects / sizes: from the YUV geometry P0 logs);
   - `black` and **`white` from the camera per shot**: ML's `raw_info` / autodetect, or a per-ISO table (10.4). Never
     the fixed 15200;
   - `cfa` 0 (RGGB at 84, 64);
   - `canon` = Canon's UYVY buffer, read for the fit before any row is written; `canon_y0` / `canon_h` for Diana halves.
2. **WB source:** the fit is the default. If P0 finds Canon's colour block (the as-shot WB levels), the caller may pass
   `wb_q8` instead; the fit stays within -4.7..+3.1 % of Canon's as-shot WB on the real pairs.
3. **`job.dec`** = the active A1 slot's table (`wf_ll_dec_c<k>`, through `wf_ll_inc()`), 0 for sRGB. The develop then
   matches what Log + LUT does with the same JPEG.
4. **`wf_dispatch()`** (wf_shot.c) gets the `WF_FMT_RAW14` case: `wf_dev_prepare`, then `wf_dev_rows` over the frame
   (or each Diana half), then the unchanged effects on the developed UYVY8 frame (grain, halation, mist).
   - Log + LUT / Film LUT cans: `WF_DEV_CUBE` with the active cube, curve, EI and auto flag;
   - plain Log: `WF_DEV_LOG`;
   - film stocks: `WF_DEV_SRGB`, then `wf_film_words` on the developed frame.
5. **wf_task** builds the tables with `wf_dev_build` on a look change (file I/O there, like `wf_loglut_load`). The look
   tables, 356 KB, and the per-shot scratch, 392 KB, come from shoot memory under the wf_fx.c rules (no SRM fallback),
   given back in playback. The worker only calls `wf_dev_prepare` / `wf_dev_rows`: no allocation, log, file or wait.
6. **Builder vs main's A1.** Main's `wf_ll_coords` puts light above 1.0 on the log segment whenever `eib` is set (a
   LUT gain or a curve slot). `wf_dv_logcode` always does: the matrix carries the gain, and the log segment applies above
   1.0. With the sRGB decode at gain 1, main's light never exceeds 1.0, so the two agree in every case.
   `wf_dv_eik / eib / eid` are copies of `wf_ll_eik / eib / eid`; keep them in step. The merge trial below ran `px`
   against main's `wf_loglut.c`.
7. **Time:** the hook holds Canon's task for 29-36 s per shot (memsim on real raws, 10.3; more with a large clipped
   sky). It is a slow mode: config `input=raw` + a menu choice. DESIGN 3.2 says the owner decides whether the wait is
   acceptable.
8. **build.sh / test.sh:**
   - add `wf_dev.o` = `ld -r` of `wf_dev_c.o` (module flags `-O2`) and `wf_dev_arm.o` (`-mcpu=arm946e-s -marm`);
   - extend the v5TE exception in the object check to `wf_dev.o` (as `wf_logout.o`);
   - the trial build below did exactly this;
   - `tools/dev_check.sh` can become a test.sh mode later.
9. **Memory check on the camera:** the raw is read through the uncached alias (memsim: NCNB). The tables and scratch
   must be cacheable.

Checks run on 2026-11-14 (branch = 50a727d + these files; WSL; EOS M first, then 5D2):

| | EOS M 2.0.2 | 5D2 2.1.2 |
|---|---|---|
| `build.sh` (branch as is, PACKS=0) | image 329,216 B, gate PASS | image 324,960 B, gate PASS |
| `test.sh` | 386 / 0 | 373 / 0 |
| trial build with `wf_dev.o` linked in (point 8) | image 361,760 B (+32,544), 103 imports (unchanged), TCC link OK, gate PASS | image 357,504 B (+32,544), 98 imports, TCC link OK, gate PASS |

Merge trial: main's tree (d7728d8: A1 slots, P0) plus these files in scratch. Results:
- no file clashes;
- `dev_check.sh`: objects OK for both cameras, host test 469 / 0 (px 68, frame 56, bad 13, span 332). `px` runs
  against main's `wf_loglut.c`;
- `dev_cr2.sh` built `dev_a1ref` from that tree with both curve slots:
  - IMG_0206 (slot 1): 12 / 0;
  - IMG_6821 (slot 2): 12 / 0;
  - the numbers equal the branch's runs.

### 10.6 Running it

```
bash tools/dev_check.sh                      # objects (EOSM, 5D2) + host test px frame bad span
FULL=1 bash tools/dev_check.sh               # + the EOS M frame (input for the bench)
bash tools/dev_bench.sh                      # memsim phases (QEMU / MEMSIM env)
bash tools/dev_cr2.sh <dir with IMG_xxxx.CR2 + .JPG> [N]    # real pairs; A1SLOT=2 for Neutral, KEEP=1 keeps files
```
`dev_cr2.sh` builds `dev_a1ref` when the tree has A1 (main's `wf_ll_incurve` and `luts/canon_curve/*.csv`). Without
A1 the develop is compared with the sRGB-decoded JPEG path only. `CUBE=srgb bash tools/dev_bench.sh` times the shipped
film case (SRGB tables, `wf_dev_levels`, shoulder).

### 10.7 Wired (2026-11-14, EOS M)

**Flow.**
1. Camera layer (`wf_cam_eosm.c rawb_fill`, NN hook, only while the menu is on) checks the gates:
   - `pic_quality == 0x3070000` (RAW + L Fine) exactly;
   - aspect 3:2;
   - `drive_mode` single / self-timer (0, 0x10, 0x11).
   It then lists the raw candidates (below) and sets `fmt = WF_FMT_RAW14`, black 2048, white per ISO, raw crop 84 / 64,
   masked columns 16..64. Otherwise `rb_skip` holds the reason.
2. `wf_dispatch` (wf_shot.c) turns RAW14 back into UYVY8 (Canon's frame stays the fallback) and calls `wf_rawb_stage`
   (src/wf_rawb.c):
   - tables ready for this look's key;
   - `wf_rs_pick` (src/wf_rsrc.c) picks the buffer against Canon's YUV;
   - `wf_dev_levels`: black from the masked columns, white from the clip pile-up, else the ISO value;
   - `wf_dev_prepare`, then `wf_dev_rows` in 64-row bands with `msleep(10)` between bands;
   - stops on module unload, `ml_shutdown_requested`, half-shutter held 1.5 s, or 90 s.
3. After a develop:
   - film: `wf_film_words` (after the white-balance shot step);
   - Log + LUT: the develop already applied the cube (`wf_nop_words` keeps the effects);
   - Log: as is.
   A stop gives `WF_ST_RBSTOP`: no look on a half developed frame. Not developed: the 8-bit path as before, with the
   reason in the log.
4. Tables: `wf_task` (`wf_rawb_poll`) builds `wf_dev_build` into one shoot memory block, 790 KB (tables 356 KB +
   scratch + alignment, cacheable alias). It is rebuilt on a look change and given back in playback, menu off and
   unload. Publish protocol: clear `rb_have`, back off while a develop holds `rb_live`, publish last. Keys:
   - film: SRGB;
   - Log: LOG with the curve;
   - Log + LUT / cans: CUBE with curve, cube, EI and auto.
   No memory → 8-bit, with a retry every 10 s.

**Raw source** (P0 data, 10-15 shots): pitch 9240, 5280 x 3528. The candidates, each pool-checked and deduplicated:
- S = (Eeko block + 0x50) - 480242, or else EDMAC WR0 - 32626440;
- then S - 32 MB and S + 32 MB.
`wf_rs_pick` scores luma NCC on blocks, mapping YUV (x, y) to raw (84 + x, 64 + y).

| Pairs | own score (x 1000) | other buffers |
|---|---|---|
| burst IMG_6822..6835, Neutral decode | 997-1000 | neighbours up to 986 (same scene) |
| the same, code^2 (the sRGB case) | 989-998 | up to 982 |
| Day09 0227 / 0233 / 0236 / 0238 | 911-963 | <= 513 |

So the threshold 800 holds with a wide margin. Two shots of the same scene can score within 2 %; the tie rule
(within 3, the earlier candidate S wins) only matters if S held the previous shot. Bursts are gated off anyway.

**Log lines per shot.** One of:
- `rawb: 8-bit (<gate> <value>)`;
- `rawb: 8-bit (buffer <why>, n, scores) ms`;
- `rawb: buf <k> <addr> of <n>, scores, blocks`, then `black / white / wb / clip / roll`, then
  `<done|stopped> rows, pick / lev / prep / rows ms (<reason>)`.
A raw that changed during the develop (hash of 16 windows) is logged too. The shot line gets ` raw` / ` raw-stop`.

**Menu.** "RAW develop" Off / On (slow) sits before RAW probe and is off at every boot. The warning row gives the
state: RAW+L needed, drive, loading, no memory, table error, ready ("About 30 s per shot. Hold half-shutter to stop.").
`NotifyBox` shows "RAW develop: preparing" / "RAW develop NN%" every second during a develop. No card writes besides
the log.

**Checks** (WSL, 2026-11-14):

| Check | Result |
|---|---|
| EOS M `test.sh` | 622 / 0 (RAWB 59: bit-exact develop vs the direct call, both buffers, WR0 path, no-match → 8-bit, every gate, half-shutter, time limit, LOG / LLUT keys, playback / off / no memory) |
| 5D2 `test.sh` | 505 / 0 (RAWB 14: no develop, film shot bit-exact) |
| `dev_check.sh` | 537 / 0 (px 68, frame 56, bad 13, span 332, roll 52, rsrc 16) |
| build EOS M | wf_dev.o v5TE, wf_rsrc.o v5T, gate PASS |

**Time** (memsim, synthetic scene, `CUBE=srgb`):

| Case | prepare | rows | develop |
|---|---|---|---|
| film | 4.90 s (levels + fit + map) | 26.58 s | 31.47 s |
| CUBE V-Log | 3.43 s | 25.72 s | 29.15 s |

Add 0.54 s of band naps, the pick (a few hundred ms, not benched) and the film look pass. Real raws took 10-20 % longer
in rows (10.3), so plan on 32-40 s per shot.

**Open risks.**
- Watchdog: there is no sign of one. Shots already hold the hook several seconds, but none has held it 30 s.
- Power off during a develop: the band loop sees `ml_shutdown_requested` within one band. Whether Canon's shutdown
  waits for the hook is not known.
- The S + 32 MB candidate: pool-checked on the host. On the camera any pool address reads, so only the NCC guards it.
- A stopped develop leaves a half developed JPEG; the CR2 is never touched.
- EXIF does not mark a B shot yet; the log does.
- Highlights / Shadows (main ab72f7d): films get it through the film tables after the develop; Log + LUT with B
  does not (the develop's own node table is not bent).
- Halation on Log + LUT B frames reads graded words, whose highlights sit below 255.
- IMG_0206: a magenta patch from the highlight rebuild.
- `wf_dev_levels` can false-trigger on sparse synthetic histograms; real raws gave the right pile-up.

### 10.8 5D2 (2026-11-14, from raw-path P0_5D2_SPEC.md; not probed on the camera)

**Camera layer** (`wf_cam_5d2.c rawb_fill`, at both hooks while the menu is on):
- gates: RAW + L fine, drive single / self-timer;
- job handle H: TOP `[0x369C + 0x3C]`, BOT `[0x3630 + 0x2C]`;
- `H[0] == 0xFFCD0761`, P = H[1], MEM1 = `[P + 0xC68]`;
- raw = MEM1 + 0x76C8, 5792 x 3804, pitch 10136, GBRG, black 1024. It must sit inside the RscMgr pool below
  0x5C000000;
- every pointer is read only where `prb_ok` says it cannot fault;
- white per ISO is a fallback only: 13300 for the 1/3-stop-down ISOs (ISO 640 measured 13318), 15600 otherwise. The
  measured pile-up wins.

**Halves** (`wf_rawb.c`):
- The TOP does the whole of the EOS M stage on its half:
  - pick: NCC of YUV rows 0..1871 against raw rows 56..;
  - levels;
  - prepare, with the WB fit on the TOP half;
  - rows 0..1871.
- The BOT continues it only when all of these hold:
  - same tables generation and key;
  - same raw address;
  - raw hash unchanged (`rb_top_*`).
  It then writes rows 1872..3743 with the TOP's scratch.
- Otherwise the BOT gets the 8-bit look, with the reason "TOP half not developed" or "raw changed since the TOP half".
- A stopped TOP is not `WF_ST_TOUCHED`, so the camera layer leaves the BOT plain: no look anywhere on that shot.

**Width:**
- `wf_dev` needs w as a multiple of 32, and 5616 is not one. So the develop covers columns 8..5607: x0 176, w 5600.
- `wf_dev_job.canon_pw` (new, 0 = w / 2) lets the WB fit read the 5616-wide half.
- The 8 edge columns on each side keep Canon's pixels under the 8-bit look.

**Host test** (5D2 RAWB):
- TOP + BOT bit-exact against the direct develop;
- no match → 8-bit;
- raw changed between the hooks → BOT 8-bit;
- no live job, and MEM1 past 0x5C000000 → not read;
- gates;
- half-shutter stop in the TOP.

**Not known until the camera runs:**
- whether raw0 really holds this shot at both hooks: the P0 probe run answers it;
- the RearDevelop task's patience while the TOP holds FrontDevelop about 20 s;
- white levels per ISO.
