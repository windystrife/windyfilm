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
4. RAW develop stage: not implemented (section 7); `input=jpeg` only.
5. Preview exposure "Smooth" is EOS M only (5D2 build: no-ops, menu shows Camera).
6. Real timings (shot ms per film / effect) exist only from the camera; host times under qemu say nothing.
