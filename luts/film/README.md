# luts_film: film-emulation cubes for the "Log + LUT" outputs

The cubes behind the ML menu **Shoot > Film LUT** (EOS M V9, 5D2 A8): 4 stocks x 5 log curves (Canon Log 2 / 3
added 2026-11-13, branch ugrade-can; the 5D2 port has the first 3), 65^3, input = the camera log curve + gamut of the
output mode, output = sRGB-encoded display values.

| card name | stock | path | log |
|---|---|---|---|
| `V250D_S3/VL/LC/C2/C3.CUB` | Vision3 250D | negative > 2383 print | S-Log3 / V-Log / LogC3 / Canon Log 2 / Canon Log 3 |
| `V500T_S3/VL/LC/C2/C3.CUB` | Vision3 500T | negative > 2383 print | same |
| `PRT400S3/VL/LC/C2/C3.CUB` | Portra 400 | negative > neutral scan | same |
| `FP400HS3/VL/LC/C2/C3.CUB` | Pro 400H | negative > neutral scan | same |

Canon Log 2 / 3: v1.2 (Canon input transform 202007), Cinema Gamut; `mk_filmluts.py --logs clog2,clog3` makes the 8
(the default `--logs` now makes all 20; the first 12 come out byte-identical). `fl_check.py` / `fl_render.py` still
cover the first three logs only (their technical reference cubes come from proto/loglut, which has no Canon Log yet).

The owner's own grade (Film LUT "EKTA P", can 66): `src/mk_usercube.py EXPORT.cube OUT` turns his 65^3 Resolve export
(V-Log / V-Gamut in, sRGB out) into `EKTAP_S3/VL/LC/C2/C3.CUB` (target log -> linear -> XYZ -> V-Gamut -> V-Log ->
tetrahedral lookup in the export) and prints the difference of each cube to the export on 20,000 in-camera colours.
A smooth grade agrees within 0.3 codes; hard clips inside the grade cost a few codes at p99.9 near the clip (for an
exact cube per log, export it from Resolve with a CST from that log to V-Log in front of the grade). The export and
the cubes are the owner's: not in the repo.

- Model: `src/fl_core.py` (own code; the chain is in its docstring), stock table and defaults in `src/mk_filmluts.py`
  (`--only V250D,PRT400`, `--n 65`, `--set key=value`).
- Film data: **spektrafilm profiles, CC BY-SA 4.0, A. Volpato** (github.com/andreavolpato/spektrafilm), read from its
  JSON files in `data/spektrafilm/` (`get_spektrafilm.sh`, pinned commit, git-ignored). spektrafilm's code is GPLv3
  and is not used or copied here. The cubes are CC BY-SA 4.0 too: `README_FILMLUT.txt` (= `ref/README_FILMLUT.txt`),
  `SPEKTRAFILM_LICENSE.txt` and `CHANGELOG_FILMLUT.txt` go on every card that has the cubes.
- CIE 1931 CMFs and CIE daylight components: `data/*.csv` (CIE datasets).

## Make them

```bash
# WSL as root, each step from a script file
bash /mnt/f/MagicLatern/_port/luts_film/get_spektrafilm.sh          # once (a download: owner's OK)
bash /mnt/f/MagicLatern/_port/luts_film/mk_all.sh /root/ml/flut/out  # 12 cubes + check.txt, ~1-2 min per cube
```

Same parameters give the same bytes: on 2026-11-12 the repo copy rebuilt `V250D_S3/VL/LC.CUB` identical to the card
(md5 list in the old port's `docs/HANDOFF-QWEN.md`, `F:\MagicLatern\_port`). `ref/` holds the shipped run's `params.json`, `check.txt`, `CHANGELOG.txt` and
`camerr_table.txt`.

## What the camera makes of a cube

The module never evaluates the cube per pixel: `fr_loglut.c` resamples it onto a 33 x 33 x 33 Y'CbCr table and
`fr_llk.c` interpolates that in integers (4:2:2). `src/fl_camerr.py` / `src/fl_render.py` run the cubes through an x86
build of exactly that code (`mk_lltest.sh`, after `proto/loglut/build.sh` + `mkframes.py`). Result
(`ref/camerr_table.txt`): the camera grid floors the error at about 0.25 (Y) / 0.18 (C) mean codes; 65^3 is close to
that floor, 33^3 is up to +8 codes at p99.9. First load of a 65^3 cube on the camera: ~2 s (7.4 MB read), then the
`<name>.FRT` cache (~181 KB) loads in ~20 ms.
