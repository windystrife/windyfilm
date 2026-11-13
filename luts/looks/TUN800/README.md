# TUN800 (colour negative; judge v2.1 final: PASS)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`): **PASS**, no failing gate. J5 nearest DLY200 2.15 (parts scenes/colour/skin [2.5, 2.11, 1.83]); second SUN200 2.59.  J5 is in the 2.0..3.0 band: owner note.

## Colour-negative separation round (neg_separation.md)
Unchanged film (PASS before and after).

Measured axes through the v2.1 kernel: W_b/W_a 0.00/0.00, S 1.051, K 0.980, B 33, T_a/T_b -0.49/-0.33, R 1.087.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- TUN800.T17 (harness `lut TUN800.CUB TUN800.T17`): 0f281295b76d7d3e546d9390930220b1
- TUN800.CUB: 94898294962338fba242b3fd6e33afe5
- gen_tun800.py: 2e4ed698e2b1c4cbc93b2b00d310bd5d (`python3 -I gen_tun800.py <outdir>` writes the CUB)
- judge2_TUN800\ : judge2_TUN800.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: Neg 800T row
- Halation: C800T

## Earlier README (before the separation round)
# Tungsten 800 (TUN800, catalogue id 35, type neg)

An own film for the camera: a 500T-class, tungsten-balanced (3200 K) motion-picture colour negative
without rem-jet, rated EI 800 and lab-scanned. The descriptor sets WB to 3200 K, so the table receives
a JPEG that is already balanced for tungsten light. Tungsten-lit scenes arrive neutral. Daylight scenes
arrive blue, and stay blue, as the stock does without its 85 filter.

The table carries:
- the stock's tone: a toe from the datasheet curve, a lifted soft black, denser mids and open highlights;
- richer colour;
- teal-leaning shadows and warm amber highlights on the neutral axis;
- neutral mid greys (PLAN 6.3, J-TUNG).

The red halos around lights come from the halation effect, not from the table.

## Final files

| File | md5 |
|---|---|
| TUN800.CUB (33^3, from `gen_tun800.py`) | 94898294962338fba242b3fd6e33afe5 |
| TUN800.T17 (harness `lut TUN800.CUB TUN800.T17`) | 0f281295b76d7d3e546d9390930220b1 |

Both judges were run on exactly these two files; their JSONs record the same md5s.

## Model

`gen_tun800.py` has two parts.

**Tone (neg_scan module).** The tone comes from the digitised characteristic curves [K1] p.4 TI2647F:
1. the JPEG is inverted to scene stops;
2. the film is exposed at EI 800, which is 2/3 stop under the EI 500 box speed;
3. a scanner model follows: each layer is normalised to its straight-line slope, mid grey is pinned
   with a partial correction (`comp`), contrast is `k`, and the black and white points are set.

**Colour.** This is the PORT160 OKLab model:
- per-hue dH/S/dL;
- global chroma with a soft knee;
- chroma against lightness;
- a grey-axis tint curve with skin, strong-colour, cool and toe guards;
- soft gamut compression;
- a linear-light veil (the lifted black).

Every number is measured, derived or marked GUESS. The docstring and the comments next to each
number say which.

Code changes against gen_port160.py:
- **`GAP_TOL` in `gamut_soft`.** OKLab constant-hue rays toward the blue primary leave sRGB by up to
  -7e-4 linear. With the old strict test, pure blues counted as out of gamut, and compressing them
  broke cube luma monotonicity. Only colours at the sRGB surface move.
- **New optional keys:** `headroom`, `skin_gather`, `tl_cool`, `veil_keep_hue` and neg `comp`. When a
  key is absent, the old behaviour is unchanged.

## Sources (cited, read through WebSearch/WebFetch, nothing saved from the web)

- **[K1]** Kodak Publication H-1-5219t, VISION3 500T 5219/7219 Technical Data, Revised 7-15 (July 2015):
  - p.1 exposure indexes: EI 500 at 3200 K; extended highlight latitude;
  - p.3 TI2647B MTF;
  - p.4 TI2647F characteristic curves, TI2647C granularity and TI2647D spectral sensitivity;
  - p.5 TI2647E dye density.

  These were digitised into the `h1_5219t_*.csv` files. Each csv names its publication, revision, page
  and figure.
- **[D1]** analog.cafe review of 500T, 2025-05-05 (updated 2026-01-22). Daylight without a filter gives
  a blue/teal cast; colour is accurate under 3200 K; shadows are the weak point.
- **[D2]** phillipreeve.net, "Analogue Adventures - Part 7", 2022-05-30, on the rem-jet-free 800T stock.
  It shows red halos around point lights.

## Claims (relative, from the sources; checked as J7 by judge_tun800.py, all PASS)

| Claim | What it asserts | Result |
|---|---|---|
| C1 | Mid contrast below CHUNGK (1.391) and CINE250D_PRINT (1.131), above ETERNA (0.888), and within 0.92..1.08 of the JPEG ([K1] p.4: one long straight line) | 0.932 |
| C2 | Grey mids neutral at WB 3200 K ([K1] p.1): \|a\|,\|b\| <= 0.015 | 0.0077 |
| C3 | Soft toe at EI 800: shadow/mid contrast below the JPEG's | 0.762 vs 1.0 |
| C4 | Highlight latitude ([K1] p.1, p.4): slope over codes 200..245 > 0.6, white within 248..252 | slope 1.109, white [252, 251, 249] |

## Guesses (look calls)

**JPEG inverse:** code 255 sits at +3.0 stops.

**Scanner:**
- `k` 0.92;
- `comp` 0.15: the lab corrects 15 % of the under-rating, so mid grey 118 becomes 113;
- black point `beta` 0.55 / `qb` 0.25, giving a black of 18 codes;
- white 0.9855: code 255 maps to 249..252.

**Neutral-axis tint:**
- teal shadows: hue about 200, C up to 0.026 at L 0.18, gone by L 0.36;
- neutral between L 0.36 and 0.70;
- amber highlights: hue about 68, C up to 0.021 at L 0.94;
- neutral again at white.

**Colour:**
- sat 1.22 with a knee from C 0.12 to 0.28;
- satL;
- per-hue edits.

**Guards:**
- skin, strong-colour and toe guards on the tint;
- `skin_sat` 0.30;
- `skin_gather` 0.15;
- `veil_keep_hue` 1.0;
- `tl_cool` (1.0, 1.0, 215).

[D1] and [D2] give only the direction of the tint, not its amount.

## Grain and halation proposal (catalogue row; effects, not this table)

- **Grain:** Neg 800T x1.0. [K1] p.4 TI2647C shows a fast tungsten negative's granularity, and EI 800
  pushes it a little.
- **Halation:** C800T x1.0. Without rem-jet there is no anti-halation layer, and [D2] shows red halos
  around point lights. [K1] p.3 shows the red layer's MTF lowest at high frequency.
- **Not tested here:** the halation through EOSMFX. This work covered only the table, so the effect
  strength still needs a check on the camera or EOSMFX.

## Light-row note (critic 14)

The shelf's Light row applies offsets of +700, +1500 and -2300 K. The module (sub_15c) clamps the
resulting Kelvin to 2500..10000. On this 3200 K film, "Tungsten" therefore gives 2500 K, never 900 K.

The real problem is what the row means: its offsets assume a daylight base. For a 3200 K film, the
owner should choose one of two options:
- accept the clamp, or
- skip the Light row for films under 4000 K.

## Judging

**Judge v2** (`catalogue/judge2`, `--type tung`): **PASS on every check.**
- J1: d2 0.0474 / p99.9 0.0284; luma axes -0.24.
- J2: white 249, black 18, 0 grey reversals.
- J-TUNG: grey mids 0.0077; daylight stays blue (b -0.091).
- J3: probes hue 45.8..68.6; grid 0/340 outside, grey or green.
- J4: worst ramp 3 reversals (limit 3, denim); hue at clip 3.3.
- J5: blacks 0 %.
- J8: p99.9 2, max 7.
- J9: all ok.
- Result: `judge2_TUN800/judge2_TUN800.json`.

**Distinctness (J5) needs an owner note.** The nearest film is PRONEG at 2.46, which is inside the
2.0..3.0 "pass with an owner note" band. The batch report must mention it.

Full judge v2 distance table (mean dE OKLab x100):

| Film | Distance |
|---|---|
| PRONEG | 2.46 |
| C250X6B | 2.58 |
| CINE250D_PRINT | 2.61 |
| CINE250D | 2.63 |
| PRT400 | 2.85 |
| C250PF | 2.93 |
| SUN200 | 2.96 |
| C250F | 3.11 |
| ETERNA | 3.52 |
| CCHROME | 4.00 |
| CLASNEG | 4.03 |
| CHUNGK | 4.68 |
| PORT160 | 4.68 |
| PRS400 | 10.64 |
| Camera JPEG (information only) | 2.36 |

The table covers:
- the shipped films;
- PORT160;
- the Cine 250D deliver, final and fix candidates (copies of `cine250d/`, read only);
- the batch-1 looks that had T17s at the end: PRS400, PRT400 and SUN200. VSL50 had none yet.

**In-house judge** (`judge_tun800.py`): the Portrait 160 checks plus J1..J7 and J-TUNG, run on two
paths, the shipped kernel and the lut replica. **ALL PASS.** Result: `judge_tun800.json`.

**Review fixes made after reading the sheets:**
- A daylight sun at WB 3200 K turned mint (hue 200 -> 173). The `tl_cool` guard fixed it; the hue
  now stays at 189 through the camera table.
- The scene-range box in the curve diagram overflowed its panel; it is now clipped.

## Sheets

- `sheets/`: before/after scenes for card, scene, lake, sunset, neon, interior, night and daylight at
  3200 K, plus skin 100 %, skin grid, ramps and tone/tint.
- `judge2_TUN800/`: scenes, skin, ramps, distinct, kernel and effect.
- `preview_tun800_curves.png`: grey tone, the datasheet curves and the hue ring.

## Reproduce (WSL, Ubuntu-24.04)

1. Run `work/setup.sh`, then `work/refs2.sh`. This builds the kernel driver and reference copies in
   `/root/ml/ws_tun800`; the repo is used read only.
2. Run `work/fin.sh` to build the CUB and T17.
3. Run `work/run.sh final TUN800 0 1` for the in-house judge with sheets.
4. Run `work/j2fin.sh` for judge v2 with sheets.

`inputs/` holds the two synthetic review scenes made by `make_inputs.py`.
