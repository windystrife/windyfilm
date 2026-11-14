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
- `ML/LOGS/WINDYFLM.LOG`: new file each boot. Useful lines: `hook NN ff169414 rom e92d41fc err 0`, `pack ok: N tables`,
  `pack <code> v<variant> loaded`, `yuv NN <buf>/<raw>: film <code> v0 s100 <ms> h<ms> g<ms> m<ms> q<ms> a<aspect>`,
  `shot #n <title>: <s> = look .. + mist .. (TOP .., BOT ..) EV..`, `lut <cube> 33^3 ok|cache ..`, `exif IMG_nnnn.JPG ok`,
  `fx halation|grain|mist .. ready`, `light Day: 5500K`, `expsim: ..`.
- EXIF: UserComment (0x9286, 264 zero bytes on Canon) overwritten in place with `ASCII\0\0\0WindyFilm: <title> | Print ..
  | Strength ..% [| Light ..] [| Grain ..] [| Halation ..] [| Mist ..] [| EV..]`, verified by read-back; never a non-empty
  field, never a rewrite.

## 7. RAW develop stage: how it plugs in (DESIGN 3.1, 3.2)

Nothing RAW is implemented yet. The owner chose **Hybrid + Canon curve inversion** (2026-11-14); DESIGN 3.2 has the
plan, the numbers and the traps. Exact touch points per step:

- **A1** (waits for `luts/canon_curve/<STYLE>.csv` from the owner's RAW+L pairs):
  - read the CSV in `tools/mk_logtab.py` (`wf_dec`) and `tools/mk_loglut_tab.py` (`wf_ll_dec`), in place of
    `srgb_eotf`. Keep `--curve srgb` as the default, so the build stays reproducible without the CSV;
  - fold K into both decode and encode tables (DESIGN 3.2, Headroom). `mk_logasm_tab.py` then follows, and its
    asserts (`wf_dec <= 16384`, `max(wf_lenc) <= 65279`) must still pass;
  - rerun `logchk`, the LOG / LOGCHK host tests and the gate, and write the curve name into the boot log line.
- **P0** (EOS M only): `wf_cam_eosm.c` takes the snapshot in the NN hook before the gate (a static struct, no calls
  except the optional `ff234680`). `wf_main.c` `wf_task` writes the lines and `ML/LOGS/WFPROBE.BIN`. A menu toggle
  goes under a debug entry, default off; nothing is written to the config. Add a host test with a fake parameter block
  and fake EDMAC words, and assert that the frame stays byte-identical.
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
