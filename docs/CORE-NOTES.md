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
| `wf_fx.c`, `yh_halo.c`, `yg_grain.c`, `yg_stocks.c`, `wf_mist.c` | halation and bloom (one stage, two rows), grain, mist; memory + hand-over protocol in the `wf_fx.c` header |
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
| LEICA | Leica Vivid can 84 / Film LUT 23: catalogue names (no "Tung" left), cube names LEICAVSR.CUB / .FRT for every Log curve, not V-Log only; hidden without LEICAVSR.CUB (the old LEICAVVL.CUB does not count); identity cube on the sRGB codes gives Y' back (<= 1 code), the V-Log path would not; S-Log3 and auto give the same tables; LUT EI +1 EV = light x2 re-encoded (100 -> 138); the .FRT key keeps the two paths apart (bit 19); its load asks for Picture Style Standard once (none when already Standard, none for a Windy can), reads sRGB with Film 1 chosen, no auto exposure (tables = sRGB, auto off), Film LUT row reaches it and asks too, `flut=leicav` |
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
  `grain=off|fine|normal|strong`, `halation=off|halation|spk`, `bloom=off|low|normal|strong` (RAWB11; an old `halation=bloom|spkbloom` reads as
  `halation|spk` + `bloom=normal`), `mist=off|w8|w4|w2|w1|b8|b4|b2|b1`,
  `preview=off|on|fast`, `exposure=camera|smooth`, `exptrim=-16..16`, `exif=on|off`,
  `shoulder=i|a|d` (chain-log: the chain films' shoulder colour on RAW, 10.9; no line = i), `userwb=mode,kelvin,gm,ba`
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
  `shot #n <title>: <s> = look .. + mist .. (TOP .., BOT ..) EV..` (from 110 characters on, two lines: the second
  `shot #n ... (TOP .., BOT ..) EV..`, so the EV stays), `lut <cube> 33^3 ok|cache ..`, `exif IMG_nnnn.JPG ok`,
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

Chain films on RAW with the shoulder I (10.9, the default since 2026-11-15): +2.2..3.9 s of rows per shot on the
real raws (memsim), the per-shot table 5 ms.

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

**Leica Vivid on B** (owner 11-14: "van trang tu raw ay, co ma ko convert qua vlog dau"): `WF_DEV_DISP` in wf_dev.c.
The SRGB base develop's grid and enc (sRGB code, the film shoulder `j.roll`), nodes = the owner's display cube itself
(`ML/DATA/LEICAVSR.CUB` = "L Leica 2.cube", 33^3, md5 9740c77f, sRGB in / sRGB out) sampled at those codes: no log curve,
no V-Log wrapper. LUT EI scales the linear light, no auto exposure; Highlights / Shadows bend each cube output channel with
wf_hs_bend's curve at build time (key bits 24-31). Grain / halation / mist as every Log + LUT output (wf_colour_fx).
The JPEG path (8-bit fallback) still uses main's LEICAVVL.CUB.

**5D2 raw base** (probe 11-14, 8 records, TOP and BOT, IMG_4467..4476): MEM1 + 0x76C8 is CR2 row 1 (byte-exact or NCC 1.0000,
0..1 LSB). B now reads CR2 row 0 at MEM1 + 0x76C8 - pitch; before that its CFA rows were swapped (GBRG read as RGGB).

**B table cache `.RBT`** (11-14 evening, top session's request after the owner's RAWB2 log: a 33^3 B build took 0.8-6 s
on the EOS M, after every boot, look change and playback -> shoot, and a 65^3 CinePrint cube parses ~6x longer):
- cube tables only (CUBE, DISP); SRGB / LOG tables are computed and stay uncached. File ML/DATA/<cube name>.RBT next to
  the cube (T250DPC2.CUB -> T250DPC2.RBT), 356,440 B = 9 key words + `struct wf_dev_tabs` + FNV-1a.
- key: "RBT1", `wf_dev_tabs_id` (WF_DEV_TABS_VER, table size, camera, src, curve and every constant table the build
  reads: camera matrix, LUT EI gains, sRGB decode, the curve's gamut matrix / encode / EI constants), the look key
  (`rb_key`: source, curve, Film LUT, LUT EI; DISP also H / S), auto exposure + camera, table size, then the cube's size,
  time stamp and content hash (`wf_llb_cube_id` = the .FRT's wf_llc_stat + wf_llc_key). One file per cube.
- generic helpers in wf_loglut.c (`wf_llb_cube_id / read / write`) on the .FRT's I/O; writes through `wf_fcreate`
  (Canon idle only). A busy write is owed and retried every 2 s while the same tables stay published
  (`rawb: <name>.RBT written N ms`). A read that fails the key, size or hash rebuilds and rewrites.
- log suffix on `rawb: tables ...`: `, T250DPC2.RBT hit` / `none|key|bad, written N ms` / `write later (busy)` /
  `not written (step)`. The time on a hit line is the read time.
- host (RAWB test): 33^3 build 37 ms / hit 25 ms, 65^3 build 193 ms / hit 26 ms; hits are bit-exact against a build.
  Camera estimate from the .FRT (181 KB, 0.2-0.6 s per hit): about 0.3-1 s per .RBT hit.
- bump WF_DEV_TABS_VER whenever wf_dev_build's arithmetic changes a cube's tables. card/EXCLUDE.txt drops `*.RBT`.

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

### 10.9 Shoulder colour on chain films (chain-log, 2026-11-15, the owner picked i)

The film shoulder (`j.roll`, 10.7) maps each enc index on its own: per channel (A), so a bright coloured light walks
off toward white channel by channel. The owner compared A, D (one shift from the pixel's luma for all three channels:
the hue kept to the top) and I (D up to the look's white, then a smooth step over 1.5 stops toward each channel's own
shift: lights keep their colour up to the print's white, then roll into cream / white) and picked **I** for the chain
films on RAW. `struct wf_dev_job.hue` (`WF_DEV_HUE_A / I / D`) asks for it; `wf_dev_prepare` takes it only with the
shoulder on, `WF_DEV_CUBE` and a log curve (1..5), else A. Callers: B (`wf_rawb_stage`) and the Darkroom
(`dk_frame`) pass `wf_rb_hue` for a chain film on the log pack (`RB_K_LG` / `dk_lgk`), A for everything else (Log +
LUT cans, Leica, plain log, the sRGB base develop of films and of the 8-bit chain). `wf_rb_hue` = WINDYFLM.CFG
`shoulder=i|a|d`, i when the line is missing; a gives the RAWB14 / RAWB15 frame bit for bit (wf_test RAWB:chain).

**Per shot** (`wf_dv_roll`): instead of A's three enc copies, one table T of `WF_DEV_NENC + 256` words in `enc2`:
`T[i] = (uint16)(s(i) + 2) | w(i) << 16`, s = A's map as a shift in quarter enc steps (signed: the shoulder holds the
look back, then lifts it near the sensor's white), w Q14 = 0 up to the look's white, smooth step over 384 steps, 1
beyond (D: 0); the top 256 words repeat the last. Logged as `shoulder <a|i|d> <ms>` on B's `rawb: black ...` line and
the Darkroom's `dark: F.. IMG_....` line (`st.ro_ms` through `j.ms`).

**Per pixel** (C reference `wf_dv_hue_idx`, asm `wf_dv_span_hs`): luma of the three non-negative accumulators with the
gamut's Rec.709-Y row (`wf_dv_hue_lw`, x 32768, SMULW), im = enc index of the luma + 256; d = T[im] low, w = T[im]
high; per channel i += (d (16384 - w) + h w) >> 16 with h = T[i] low, clamped to 0 .. NENC - 1, then the plain enc
table. Pre-test: R and G accumulators both under `hk` (every gamut's B weight is negative, so the luma is under the
knee) take the plain lookups. A neutral pixel has im = i: knee, target and grey stay A's. `wf_dv_span_hs` is the span
kernel macro with the shoulder block (`KERNEL name, hue` in wf_dev_arm.S); `wf_dv_span_s` assembles byte-identical to
before. Tests: dev mode `hue` (luma rows against inv(wf_ll_mat), asm = C over 4 phases x 9 scales, I and D on a
neutral ramp within 2 codes of A and equal below the knee), wf_test RAWB:chain (I default, a / d through the switch,
each bit-exact against the direct develop), CFG (`shoulder=`), RAWB (Log + LUT logs `shoulder a`).

**Cost** (memsim, `HUE=a|i|d [CAM=5D2] [CURVE=1 CUBE=.. SCENE=..] bash tools/dev_bench.sh`):

Per-shot table ("shoulder tables", its own memsim phase through `j.ms`): A 9.1-10.9 ms (three per-channel enc copies),
I 4.1-4.9 ms, D 4.0-4.7 ms. Rows, the frame 5184 x 3456 (instructions per pixel in brackets):

| Shot | A (RAWB14 / 15) | **I** | D |
|---|---|---|---|
| synthetic scene, V-Log `FILM33.CUB`, knee 1905, 11 % highlight px | 25.60 s (161.7) | **28.52 s** (177.1) | 28.52 s (177.1) |
| IMG_6849, G03P01 log table on S-Log3, knee 1296, bright facade, no clip | 22.70 s (120.9) | **26.57 s** (150.0) | 26.23 s (149.0) |
| IMG_6856, G03P01, night street, 339 k highlight px | 23.15 s (128.0) | **25.33 s** (144.5) | 25.11 s (143.7) |

- I costs the real chain-film shots +2.2 s (6856) to +3.9 s (6849) of rows, +9 to +17 %: the luma (3 SMULW), T[im] and
  the per-channel shift with its MAC run on every pixel the pre-test does not send to the plain lookups. D saves only
  0.2-0.3 s of that (the w > 0 MAC). A's per-pixel path and checksum are unchanged (no shoulder: bbbeb78c, 25.86 s).
- Shot (prepare + rows; prepare 4.64-4.68 s here includes `wf_dev_levels`, logged apart as `lev` on the camera):
  IMG_6849 27.35 -> 31.21 s, IMG_6856 27.83 -> 30.00 s.
- 5D2 develop (`CAM=5D2`: its matrix and object, the same EOS M frames): the same within 0.1 s (A 22.64 / 23.12, I 26.52
  / 25.30, D 26.17 / 25.07 s). Its frame is 5600 x 3744 = 1.17 x: about 26.5 -> 31.0 s (6849) and 27.0 -> 29.6 s (6856)
  of rows. The 5D2 has no WFLOG.DAT, so its B never takes I today; the Darkroom is EOS M only.
- On the camera: `shoulder i NNms` (the table) and `rows NN ms` on B's lines; the Darkroom's `expose` time.

**Colour trim on i** (the owner after the 16-raw preview: i a little less saturated, saturation and vibrance -10 %, the
detail kept; 16 Nov, after the trim preview: still a bit strong, that again x 0.9, so -19 / -19). `wf_dev_trim(t, sat, vib)` works on the built nodes: Y' of every node stays, Cb / Cr x (1 + sat / 100)
(1 + vib / 100 x s), s = max - min of the node's R'G'B' (BT.601 back from its Y'CbCr, 0..1). `WF_DEV_HUE_I_SAT` =
`WF_DEV_HUE_I_VIB` = -19 (were -10 / -10 on 15 Nov): a muted colour x 0.81, the strongest x 0.81 x 0.81 = 0.656, the -10
trim applied twice to 0.3 % (x 0.655 .. 0.776 over the dev test cube's nodes with |c| >= 64); a neutral is untouched, so the tone, the shoulder table (it reads the nodes' Y') and every detail stay.
`wf_rawb_logpk_build` applies it when `wf_rb_hue` is I, so B and the Darkroom both get it with the table. B's `rb_key`
carries bit 24 = trim: leaving i (shoulder=a / d) rebuilds the table without it, a stays the RAWB14 / RAWB15 frame bit
for bit, D has no trim. Cost: once per table build, 20.6 ms in memsim (2.9 M instructions over the 35 937 nodes, two
software divisions each), inside the logged `tables log pack ... NNms`; nothing per pixel. Tests: dev `hue` part 4 (0 / 0
a no-op, only the nodes change, Y' and neutrals kept, each Cb / Cr between (1 + sat)(1 + vib) and 1 + sat of its own
with its sign, from the two defines),
wf_test RAWB:chain (I + trim bit-exact against the direct develop with the trim; without it every Y byte equal and U / V
nearer 0), darkroom `devset` hue field `i` (the camera's, with the trim) / `i0` (without). On the owner's 16 raws through
the darkroom devset, i0 and i differ in no Y byte (cmp).

### 10.10 Knob rows on RAW (chain-log, 2026-11-16; milestone 2: Interlayer)

A chain film on the log pack with a film-stage knob row moved used to keep the 8-bit chain (`WF_RLG_KNOBS`). Interlayer
(0..200 %) now stays on RAW: B (`wf_rawb_logpk_build`) and the Darkroom (`dk_logpk`) build the pair's log table on the
camera. Film exp, Push, Exp comp and the printer points followed in milestone 3 (10.11, which changes the WFSTLOG.DAT
format, the log lines and `wf_rawb_logpk` described here).

**The table** (`rb_sl_build`, wf_rawb.c): T(k) = composed(k) + r, with r = shipped .L33 - composed(default). Both
compositions are the log grid's two passes (wf_stagek.h `wf_sg_neg_pass_x` / `wf_sg_prn_pass_rgb`) on the pair's halves
from WFSTAGE.DAT. The residual carries what the composition misses against the chain (at default over the 643 entries:
mean 0.145, worst mean 0.268, worst max 19.8 8-bit codes; milestone 1, `mk_wfstage logcheck` / `logdat`), so a knob
table sits on the same base as the shipped table, which default knobs keep. r is int8 in quarter 8-bit codes (64 Q16
units, half away from 0, clipped at +-127 = +-31.75 codes and the clip count logged; the shipped entries peak at 19.8).
Interlayer and the printer points move no exposure, so r is read at its own node; Film exp / Push / Exp comp move the
knee and need r moved with it (`logcheck`'s "moved" column, milestone 3).

**Data:** ML/DATA/WFSTLOG.DAT (tools/mk_wfstlog.py; EOS M, built after WFSTAGE.DAT and WFLOG.DAT, 239,174 B for 643
entries): the x section (each log node's exposure axis value per channel, the same for every negative) and per entry the
log placement (fit, black, 2^32 / (white - black)) with the checksums of the halves and of the log table it goes with.
An entry whose halves (FNV-1a) or log table (Adler-32) are not the card's gives "packs of another build" and the 8-bit
chain for that pair until the next boot.

**On the camera** (B's worker; the Darkroom's look check):
1. WFSTLOG.DAT header and index (FNV-1a) -> the entry; its halves' checksums against WFSTAGE.DAT's.
2. The negative half (434,856 B, TMP) read and checked (`wf_sg_read_half`); the x section streamed in 8,172-byte chunks
   (FNV-1a at the end), each chunk through both negative passes: pd0 (default) into the tables block, pd (the knobs)
   into the develop scratch. The half is freed.
3. The print half (217,192 B, TMP) read; the shipped table's samples streamed from WFLOG.DAT (`wf_dev_pack_samples`,
   Adler-32 checked, at most 1,364 nodes a call): the print pass at pd0, r into the develop scratch.
4. `wf_dev_build_gen`: the pack path's build from samples (bit-identical to `wf_dev_build_pack` on the same samples,
   dev `pack`), 33 calls, each the print pass at pd + r. The print half is freed; i's colour trim as on the pack path.
Shoot memory is unchanged (790 KB: the tables block and the scratch hold pd0, pd and r); TMP peaks at the negative half
plus two 8 KB chunk buffers.

**Key:** `rb_key` has no room for the knob key, so B keeps `rb_have_stk` beside `rb_have` (published first, behind a
barrier), `rb_top_stk` and `rb_tried_stk`; `rb_stk` is the look's knob key on the log-pack kind, 0 otherwise.
`wf_rawb_logpk` takes a knob key only when `rb_sl_knobs` holds (no exposure offset and no printer points: Interlayer
alone) and the pair has not failed; the rest keeps `WF_RLG_KNOBS`. Logs: `rawb: tables knobs <code> v<n> k<key>
interlayer <%> c<curve> <ms>`, the Darkroom's `dark: tables knobs ...`, a failure `rawb: knob table ...: <why> (<rc>),
the 8-bit chain`, a clip `rawb: knob table ...: residual clipped at <n> of 107811 values`.

**Cost** (memsim ARM946E-S, one knob table; G03 v4 and G38 v18 at Interlayer 0 / 50 / 200 %, the same to 0.5 ms):
both negative passes 219.8 ms, the residual 118.1 ms, the fill 113.1 ms: 451 ms on top of the build from samples (the
pack path's), and about 1.1 MB of SD reads (index up to 23 KB, x 216 KB, halves 435 + 217 KB, the log table 216 KB).
Once per knob change, nothing per shot. Code (text + data of the touched objects): EOS M +6,026 B (wf_rawb +3,960,
wf_dev +1,332, wf_shot +588, wf_dark +146), 5D2 +5,876 B; the 5D2 has no WFLOG.DAT / WFSTLOG.DAT, so it never runs.

**Tests:** dev `pack` (gen = pack bit for bit on 16 builds, samples streamed as they are, a flipped byte, the callback's
code, missing pack / entry); wf_test RAWB:knobs (EOS M: Interlayer 0 / 50 / 200 % bit-exact against the direct develop
with the composed table + residual worked out in the test, the clip logged, Film exp -> the 8-bit chain, default
knobs again = the shipped table's frame, a WFSTLOG.DAT for other halves -> "packs of another build"); RAWB:chain
unchanged (shoulder=a = RAWB15 bit for bit). LOOK:sweep takes `WF_LOOK_STAGE` (a WFSTAGE.DAT) and `WF_LOOK_CFG` (extra
config lines, e.g. `stage=0,0,0,0,0,0,2`) for sheets with the 8-bit chain at knob rows.

**Owner sheet** (main's notes `gallery-log/interlayer-m2/`, the 16 raws, real kernels, shoulder i): bright-half C*, mean
of 16, at Interlayer 100 / 50 / 0 %: G03P01 8-bit 28.5 / 26.1 / 23.8, RAW 24.9 / 23.0 / 21.2; G38P15 8-bit 35.0 / 34.3
/ 33.1, RAW 29.1 / 28.6 / 27.8. Both paths move together; RAW stays paler by i's trim, the detail as before.

### 10.11 Knob rows on RAW, milestone 3: Film exp, Push, Exp comp, Printer R / G / B (chain-log, 2026-11-16)

Every film-stage knob row now composes on RAW. A chain film on the log pack keeps the 8-bit chain only for strength 30 /
125 %, Highlights / Shadows, a pair whose WFSTLOG.DAT entry fails, or no WFSTLOG.DAT on the card. `rb_sl_knobs` is gone:
`rb_sl_ok` = a WFSTLOG.DAT was read, the pair has not failed and WFSTAGE.DAT has its halves. Default knobs (key 0: no
offset left after Exp comp, printer 0, Interlayer 100 %) still take the shipped table through the pack path, so
shoulder=a stays RAWB15 bit for bit.

**Printer points** move no exposure: T(k) = composed(k) + r at the node itself, as Interlayer.

**Film exp / Push / Exp comp** reach the negative pass as one exposure offset in thirds of a stop (`wf_sg_off`: Exp
comp on = 0, else Film exp + Push, -9..+9; `wf_sg_unkey` gives it whole as fexp). The offset moves the knee, so the
residual must move with it: r is read where the node's S-Log3 linear is 2^(offset / 3) times its own, per axis (a
brighter offset reads a higher node). Milestone 1's `logcheck` "moved" column is that read in doubles; the module's is
integer:
- `wf_sg_res_quant(le, rgb, n, r)`: r = shipped - composed(default) in quarter 8-bit codes (64 Q16 units, half away
  from 0, clipped at +-127, the clips counted). This was inline in rb_sl_res; now one function for the camera and the
  generator.
- `wf_sg_res_add(r, pos, node0, n, rgb)`: rgb += r x 64 read at the moved node. pos = the offset's shift row (33 axis
  positions, node units Q8), or 0 for none. Per axis i0 = min(p >> 8, 31), f = p - 256 i0; lerp(a, b) = a + (((b - a)
  f + 128) >> 8) over R, then G, then B; the sum clamped to 0..65535. r holds all 35,937 nodes (a moved read can reach
  any of them), which it already did for the fill.

**WFSTLOG.DAT v2** (tools/mk_wfstlog.py, 240,198 B for 643 entries): header 48 B (`WFSL`, u16 version 2, u16 n, u16
grid 33, u8 curve, u8 feed, u32 index offset 48, u32 x offset, u32 size, u32 FNV-1a index, u32 FNV-1a x, u32 shift
offset = 48 + 36 n, u16 rows 19, s16 first offset -9, u32 FNV-1a shift, u32 0), the index (36 B entries, as v1), the
shift section (19 rows x 33 u16, offset -9 first: `mk_wfstage logshift`, from the same `lg_shift` as logcheck's moved
column, lrint x 256), the x section at the next 512-byte boundary. A v1 file (milestone 2's card) gives "WFSTLOG.DAT not a
pack": every knob table falls back to the 8-bit chain, nothing else changes. An offset outside the rows gives
`RB_SL_E_SHIFT` ("exposure offset not in WFSTLOG.DAT") and the 8-bit chain for that pair.

**On the camera** (B and the Darkroom, the 10.10 order): step 1 reads the shift section after the index (1,254 B,
FNV-1a) and copies the offset's row into `rb_sl.pos` (66 B in rb_sl_build's frame); step 4's fill calls `wf_sg_res_add`
with that row when fexp != 0. No new reads beyond those 1,254 B; shoot memory unchanged (790 KB).

**What it fixes** (`mk_wfstage logcheck`, the 5 pairs x 14 knob sets of milestone 1, now with a "module" column = this
code; module = moved to 0.01 node in every knee): the knee node (`lg_knee`: the neutral axis' fractional node index
where its luma first reaches its monotone top - 256, where the develop's shoulder finds white) for chain / composed +
residual at its node / module:
- G15P10 exp +1: 13.94 / 14.75 / 13.95; exp -2: 20.96 / 21.85 / 20.97; exp +2: 11.77 / 11.94 / 11.88.
- G03P01 exp -2: 21.59 / 21.88 / 21.75. G09P14 (B&W) exp -2: 20.98 / 21.53 / 20.98.
- Mean error against the chain, all sets: module <= 0.267 8-bit codes (G15P10 mixed); exp -2 on G15P10 0.283 -> 0.143.
- **The one knee miss: G38P15 (Eterna 8553 / 3523XD), Film exp +2 (offset +6, printer 0, Interlayer 100 %): chain
  12.44, composed 12.87, module 12.85.** The composition puts that knee 0.43 node late and the residual read at the
  moved node takes back 0.02 of it. Mean error 0.099 codes, max 9.0.
- Two smaller ones, G21P05 (offset +3: Film exp +1 or Push +1): chain 14.02, composed 14.84, module 14.81; G21P05 mixed
  (Film exp +2/3, Push -1/3 = offset +1, printer +3 / -2 / +4, Interlayer 150 %): chain 15.94, composed 15.99, module 16.16 (the moved residual
  overshoots by 0.2 node; mean 0.126).
- Printer R +12 on G21P05: one node 26.8 codes off the chain in R, the same with or without the residual (the
  composition's own miss at the printer's end; mean 0.148).
- `logdat` over the 643 entries: residual |max| 79 quarter codes, 0 clipped; the shift section equals `lg_shift_row`;
  Film exp +1 through the module's table lifts 17 of 17 neutral nodes in every entry.

**Cost** (memsim ARM946E-S, G03 v4 and G38 v18, the module's wf_stagek.o): negative passes 219-220 ms, the residual
123.6 ms (wf_sg_res_quant, a call per slab: +5.5 ms against the inline loop), the fill 117.8 ms at the node itself
(Interlayer / printer; +4.7 ms) or 160-163 ms read at the moved node (offset +3, -9 with Printer R +12, +7 mixed): 461
ms per knob table without an offset, 504 ms with one, plus the build from samples (the pack path's). SD reads about 1.1
MB as 10.10 + 1,254 B. Host and ARM give the same samples. Code (text + data, against milestone 2): EOS M +1,183 B
(wf_stagek +832, wf_rawb +235, wf_dark +116), 5D2 +1,067 B; the 5D2 has no WFLOG.DAT / WFSTLOG.DAT, so it never runs.

**Logs:** `rawb: tables knobs <code> v<n> k<key> exp <offset> prn <r> <g> <b> interlayer <%> c<curve> <ms> (sum <x>,
<n> KB shoot memory)`, the Darkroom's `dark: tables knobs ...` the same; exp is the offset left after Exp comp.

**Tests:** wf_test RAWB:knobs (EOS M, WFSTLOG.DAT v2 written by the test with synthetic shift rows k x 256 + 8 o k, so
the fractions vary and the top nodes clamp): Interlayer 0 / 50 / 200 %, Film exp +1, Film exp -2 + Push -1 (offset -9,
the bottom row) with Printer R +12, Exp comp on (offset 0, Printer R +12 at the node), a mixed set (offset +7, printer
+3 / -2 / +4, Interlayer 150 %) and the same again after strength 30 % -> 100 %: each bit-exact against the direct
develop of the test's own reference (the composer at the knobs + the residual read with the test's integer trilinear),
the moved read changing 105,000-107,000 of 107,811 residual values; Film exp +1 + Push -1 (offset 0) and every row back
give the default key and the shipped table's frame bit for bit; strength 30 % with knobs -> the 8-bit chain; other
halves -> "packs of another build". `mk_wfstlog.py` self-test: v2 header, monotone rows, offset 0 = identity, brighter
offsets read higher nodes. `mk_wfstage logknob ... FEXP PUSH COMP PRNR PRNG PRNB IIE` writes the module's knob table as
a .L33 (sheets).

### 10.12 Four more knob rows: Bleach bypass, Print bleach, Neutral neg, Neutral print (chain-log, 2026-11-16)

The owner asked for the chain's four remaining look controls as rows, on the 8-bit path and on RAW. They go after
Interlayer, 0..100 % in 25 % steps, default 0 (the chain's default), greyed without knob data (a version 1 WFSTAGE.DAT,
or none). Default knobs still take the shipped tables (key 0), so shoulder=a stays RAWB15 bit for bit, and every other
knob set composes exactly as before (host: the same RAW samples as HEAD's composer on G03 v4 / G38 v18 over 5 sets of
Interlayer, offset and printer, -9..+7 thirds, R -12..+12).

**What the chain does** (fr_glook_film.c, all four held at 0 until now) and how the composer reaches it with data only:
- *Neutral neg / Neutral print* (`neutral_tonescale`, `neutral_density`, curve3): R's and B's curve move toward G's at the
  same input. A1 / B1 share one 257-point axis per half, so the composer blends its own rows: d += (A1[G](x) - d) x
  step / 4 (B1 the same). No data, and A2 / B2 need no wider grid (measured below: the plan's widening was not needed).
- *Bleach bypass* (`bleach`, after the interlayer, before auto-balance and printer): pd_c += amount x (pd_R + pd_G +
  pd_B). A2's base holds the auto-balance (measured at printer 0), so the composer adds amount x (sum of the A2 values -
  bs) to every channel; bs = the auto-balance summed (Q16 208404 = 3 x 1.06), measured from the chain's trace at
  bleach 100 % and 50 % on all 46 negatives (`mk_wfstage kd`).
- *Print bleach* (`print_bleach`): linear light x 10^(-amount x the sum of the print densities), before the gamut map
  (scale-invariant) and the encode (a power), so one gain on B2's raw output: g(s), s = amount x the sum of B1's three
  outputs, a 257-entry Q16 table over s = 0..16 measured at display white and checked at grey (`mk_wfstage kd`: one gain
  within 40 Q16 units). Applied before the pinned ends.
- **Amount scale.** The chain takes 0..1 for both bleach amounts, but 0.25 already takes mid grey from L* 55 to 85
  (Bleach bypass) or to 21 (Print bleach) on 2 chain films x 16 frames. The rows use amount = step / 64
  (`WF_SG_BL_SHIFT` 6): 100 % = 1/16, L* 55 -> 68 / 43, C* -18 / -22 %, close to linear in the steps. The neutral rows
  blend 0..1.

**Knob data** `struct wf_sg_kd` (bs, g[257], pad: 520 B) at the end of **WFSTAGE.DAT version 2**: header u16 version 2,
u32 at 56 = its offset (the end of the print blobs), u32 at 60 = its FNV-1a, the size includes it. The negative / print
blobs are byte-equal to version 1's (21,323,868 B -> 21,324,388 B, bytes 64..end of v1 equal), so WFSTLOG.DAT's per-half
FNVs stay valid. `mk_wfstage.py` caches the data in films/tables/.stage/kd.bin (new file; FORCE=1 re-measures). The
module reads version 1 or 2; a version 1 pack hides the four rows and composes them as 0 (the cfg keeps their values).

**Key** (32-bit, default 0): key = 1 + 2 x idx, idx mixed radix over offset (19) x printer R / G / B (25 each) x
interlayer (9) x the four rows (5 each) < 1,669,921,875. wf_sg_unkey divides by constants (multiply-high: wf_stagek.o
imports nothing). Keys are not stored: the cfg and roll files hold the row values.

**RAW** (B and the Darkroom): the same passes over the log grid. The residual (shipped .L33 - default composition) gets
a weight per node, `gn` from wf_sg_prn_pass_rgb = the Print bleach gain (it scales the print's output, so it scales the
composition's error) x (4 - Bleach bypass step) / 4. Bleach bypass moves the knee (100 %: 16.87 -> 14.18 nodes on
G03P01) and the residual sits at the old knee: read at the node it put that knee 0.63 node late; faded out, the RAW
table at 100 % is the composition alone (as the 8-bit table always is). `wf_sg_res_w` says when the weight is needed;
wf_rawb.c allocates gn (33 x 33 u16) only then.

**Accuracy** (`mk_wfstage check`, 8-bit vs the chain at the 4913 nodes, 5 pairs; `logcheck`, RAW vs the chain at the
35,937 log nodes, 5 pairs):
- 8-bit: Bleach bypass 50 / 100 % max 7.8 codes, mean <= 0.28; Print bleach 50 / 100 % max 7.0, mean <= 0.42; Neutral
  neg / print / both 100 % max 16.4 (G03P01 neutral print, V at a saturated node), mean <= 0.41; the four mixed max 11.3,
  mean <= 0.39. The other knobs measure max 4..14, mean 0.3..0.4 the same way.
- RAW module: Bleach bypass mean <= 0.135, max 13.1; Print bleach mean <= 0.085, max 12.2 (the residual takes 0.13 ->
  0.06); Neutral mean <= 0.135, max 13.8; the four mixed (Film exp +1, Bleach 50, Print 75, Neutral 100 / 25) mean
  <= 0.194, max 15.5 (R at gamut-edge nodes, the composition's own miss there).
- Knee (lg_knee) chain / module: Bleach 100 % 14.18 / 14.13, 14.06 / 14.06, 14.30 / 14.27, 13.98 / 13.98, 13.98 / 13.98;
  Print bleach, Neutral, mixed within 0.03 node. **The one knee miss: G03P01 (Vision3 5207 / 2383) Bleach bypass 50 %
  (offset 0, printer 0, Interlayer 100 %): chain 15.29, composed 15.26, module 15.45** (half the residual left).
- `dat` (v2) over the 643 entries: default composition vs shipped mean of means 0.268 as before; the knob data matches
  `kd`; Print bleach 100 % darkens grey (>= 10 of 13 nodes) in every entry.

**Cost** (memsim ARM946E-S, G03 v4, per RAW knob table: negative passes / residual / fill): the new rows at 0 226 / 123 /
119 ms (HEAD 220 / 124 / 118); Bleach 100 % 235 / 123 / 114; Print bleach 100 % 226 / 123 / 153; Neutral both 100 %
240 / 123 / 142; mixed with Film exp +1 242 / 123 / 200 (565 ms, HEAD's Film exp +1 alone 505). The 8-bit composition
(4913 nodes) 37..43 ms. The node functions come twice (`wf_sg_nnode0` / `wf_sg_pnode0` with the four rows at 0, the
code of before; `...k` with them): one copy cost 8 % of every pass. Code (text + data) against milestone 3: wf_stagek
+2,376 B (1,208 of it the second copies), wf_shot +368, wf_rawb +307, wf_dark +147; whole image EOS M 715,408 -> 721,184 B (5D2 675,120 B).

**UI / cfg / EXIF:** rows "Bleach bypass", "Print bleach", "Neutral neg", "Neutral print" (texts "0%".."100%", 4 new
browser icons from tools/mk_wffont.py: 22 rows), `stage=` 7 or 11 values (any other count, a trailing comma or a value
out of range: the line is ignored), saved with 11; EXIF " | Bleach bypass 50%", " | Print bleach 25%", " | Neutral neg
100%", " | Neutral print 50%" when not 0. Logs: `rawb: tables knobs ... interlayer <%> bleach <%> <%> neutral <%> <%>
c<curve> ...` and the Darkroom's `dark: tables knobs ...` the same.

**Tests:** wf_test STAGE (the eleven rows on a version 2 pack, Bleach bypass 25 / 100 %, Print bleach 100 %, Neutral neg
100 % + Neutral print 50 % + Print bleach 25 %: each bit-exact against wf_sg_compose on the synthetic halves, key, EXIF,
cfg 11 values), STAGE:missing (cfg 7 / 11 values read, 8 / 12 values and out of range ignored), STAGE:v1 (a version 1
pack: seven rows, the four greyed, Film exp +1's key with the cfg's four nonzero, the cfg keeps them), RAWB:knobs (Bleach
50 %, Print bleach 75 %, Neutral neg 100 %, Neutral print 25 %, then + Film exp +1: bit-exact against the test's
reference with the weighted residual; back to the shipped table). dev_check 1177/0 (pack 238/0), EOS M 1840/0, 5D2 1344/0, gate
PASS on both.

### 10.13 Interlayer starts at 50 %; the browser's rows follow the film under the cursor (chain-log, 2026-11-16)

Owner, 11-16, after the four rows: "mặc định interlayer cỡ 50 thôi nhé để mình chụp thử", and the rows in the WindyFilm
UI changed only after SET ("ý là hàng ui ấy cái nào bấm đc cái nào ko").

**Interlayer start.** `WF_SG_IIE_START` 2 (wf_stagek.h) is where the row starts: the variable's initial value, a bad or
out-of-range value, a bad stage= line. `WF_SG_IIE_DEF` 4 keeps its meaning: 100 %, the chain's own setting, and with the
other rows at 0 the shipped table (`wf_sg_default`, the key 0). So the default look of a film-stage film is now a
composed table: on the 8-bit path the composer once per knob set (37..43 ms), on RAW the knob table (about half a
second per knob set, memsim in 10.12; it needs WFSTLOG.DAT, else the 8-bit chain), not the log pack's table. EXIF says "| Interlayer 50%". A card
whose WINDYFLM.CFG already has a stage= line keeps that line's value (every save writes it, so any card that saved since
the knob rows has one): the README tells the owner to set the row once. "shoulder=a bit-exact to RAWB15" and "default
knobs = the shipped table" now mean Interlayer 100 % with the other rows at 0.

**Rows by the film under the cursor.** The browser drew the info column of the film under the cursor but greyed its
rows by the loaded film (`wf_row_avail` reads `wf_look_now`). New in wf_api.h: `wf_row_avail_for`, `wf_row_text_for`,
`wf_row_text_long_for`, `wf_row_step_for` (row, film id); the plain four are these with the loaded film. wf_shot.c
`wf_look_for(lk, id)` is the look SET on that film would give (a film on its tables with output Film, a can on its
cube's own curve + LUT); the look code moved into `wf_look_at(lk, out, k, curve, flut)`, which `wf_look_now` calls with
the menu state, so its output is unchanged. `wf_row_avail_for` takes the loaded film (`wf_loaded()`) through
`wf_row_avail` (its output and curve as set); the last film on plain log or Off is not loaded, so its rows show as SET
would load it. wf_ui.c draws the rows and steps them (`step_row`) with the browser film's id: a film-stage film lights
its knob rows, a plain film greys them, a can greys Print stock and Strength, and Print stock shows and steps that film's
prints. The values are the module's own (one set for every film), so a row set before SET is the look once the film is
loaded. The ML menu still reads the loaded film. wf_look_for needs nothing of wf_menu.c beyond what wf_look_now reads
(the darkroom WASM builds wf_shot.c with its own menu stub, not wf_ui.c / wf_menu.c).

**Tests:** STAGE (no stage line: Interlayer "50%", the key of { iie 2 }, EXIF, the shot bit-exact against wf_sg_compose;
the `_for` rows on the loaded chain film = the menu's, on a plain film knob rows greyed and Print "Scan", on a can Log +
LUT rows, on plain log the menu's knob rows greyed and the browser's on; then Interlayer 100 % gives the key 0 and the
rest as before), STAGE:bad (the bad stage line keeps Interlayer 50 %, so the bad negative half shows at once), CFG (a
fresh save writes interlayer 2). STAGE:dark and RAWB:knobs start from a cfg stage line at 100 % (their baseline is the
shipped table). tests/ui: fake_core.c has the 22 rows (it had 18 since the four rows: ASan SEGV in draw_info_column),
the `_for` functions and `fake_set_film_grey`; ui_test 4c browses to a film that greys Interlayer: no step there, a step
on the loaded film (shot b2s_rows_by_film). dev_check 1177/0 (pack 238/0), EOS M 1846/0, 5D2 1350/0, tests/ui PASS,
gate PASS on both. Image: EOS M 721,184 -> 721,808 B, 5D2 675,120 -> 675,760 B.

**Fix to 10.12 in the same commit:** tools/mk_wfstlog.py read WFSTAGE.DAT as version 1 only, so a PACKS=1 EOS M build
after 102b0ce left WFSTLOG.DAT out (build.sh removes a pack whose tool fails; an old file in build/ hid it). It takes
version 1 or 2 now: version 2 only appends the knob data, the index and blobs it reads are as before.

## 11. Paper finish (branch `paper-finish`, 2026-11-16, both cameras + the Darkroom web core)

Owner, 11-16: a "Paper" row, Glossy (default) / Matte / Warm / Cool, at the print / final stage of every film look.
The design, model and numbers are DESIGN 3.10; this section is how it sits in the code.

**Files.** New: `tools/mk_paper.py` (the model; writes / checks `src/wf_paper_tab.h`, 7 curves x 65 knots, code x 16),
`tools/paper_bench.c` + `tools/paper_bench.sh` (memsim cost). The curves and `wf_paper_name` / `wf_paper_ch` /
`wf_paper_ycc16` / `wf_ll_paper` live in `wf_loglut.c`, because every object that builds tables links wf_loglut.o
(dev_check links wf_dev.o without wf_film.o). `wf_film.c` `wf_film_paper` (static, the build's last step, flag
`WF_FT_PAPER` bits 24-25), `wf_dev.c` `wf_dev_paper`. `wf_shot.c` `wf_paper_t` (here so the WASM needs only the
variable), the look's `paper`, the Log + LUT poll, the shot / yuv log text. `wf_rawb.c` / `wf_dark.c` the develop tables.
`wf_menu.c` row, cfg, EXIF, ML menu entry. `wf_pv.c` the preview key.

**Invariants (each has a test).**
- Glossy is the code of before: no flag bit (`t->flags` keeps the low byte only, so the set's bytes are unchanged), no
  `wf_ll_paper` / `wf_dev_paper` call, no key bit (rb_key, wf_pv_fkey, the shot set's flags), no cfg / EXIF / log text
  beyond the new `paper=glossy` line in a saved WINDYFLM.CFG. Checked: host HS gold FNV, paper_bench (film FNV
  5f01b244 at Glossy), the WASM (8 of 8 renders byte-equal to the core before), the module's shot tests.
- Caches stay unpapered. `.FRT`: written inside the load, the paper goes on after. `.RBT`: `wf_rawb_rbt_key` hashes
  the look with paper 0; the paper goes on after the read; a papered table never pays the owed write (`wr ==
  WF_FE_BUSY && !lk.paper`), the next Glossy build of that look writes it.
- The Log + LUT HS-only fast path (bend N again from the 256-byte copy, no card read) runs only with paper Glossy now
  and before: a paper is applied after the bend, so it cannot be bent again in place. Any change with a paper reloads
  (a .FRT hit).
- Paper is last everywhere, after Highlights / Shadows, the Strong pass and the chain's trim.

**Darkroom web core** (the WASM; not in this repo): it compiles wf_film.c / wf_dev.c / wf_loglut.c / wf_shot.c as they
are, and its prep.py extracts wf_rawb.c / wf_dark.c pieces verbatim, so the paper reaches its tables through rb_key /
`wf_rawb_rbt_key` / `wf_rawb_logpk_build` without a change. Its
own files need: `volatile int wf_paper;` and the EXIF item (wfd_menu.c), paper after the RBT in its table build and its
log (wfd_dark.c), `paper` in its look key, the `wfd_look` argument and the JSON (wfdark.c), and a `paper` field in the
look API (wfdark.js: "glossy" | "matte" | "warm" | "cool" or 0..3). Patch + diff handed to the coordinator 11-16.

**Cost** (memsim, 200 MHz): film set +30 ms (+4.07 M ins) on 60 ms; `wf_ll_paper` 56.5 ms per load; `wf_dev_paper`
54.9 ms per develop table; 0 per pixel.

**Found on the way.** The physical flare flattens the deepest tones (Matte maps 16..48 to a span of about 22 codes, not
32): tests compare against the model, not a fixed contrast. A Leica Vivid "RBT hit" test must wait for the 0 / 0 table
build first (else it races the cache write). The camera Darkroom's 200-byte print comment (dk_frame) takes at most 14
more bytes (` | Paper Matte`).
