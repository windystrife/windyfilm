# PRT400 (colour negative; judge v2.1 final: PASS)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`): **PASS**, no failing gate. J5 nearest EVD400 2.45 (parts scenes/colour/skin [2.45, 2.61, 2.29]); second SUN200 2.85.  J5 is in the 2.0..3.0 band: owner note.

## Colour-negative separation round (neg_separation.md)
Kept unchanged on its axes (S 1.11, K 0.95, B 25, R 0.72, neutral); only the skin hue rendering (J3 grid) was repaired.

Measured axes through the v2.1 kernel: W_b/W_a 0.00/0.00, S 1.112, K 0.945, B 25, T_a/T_b 0.00/0.00, R 0.724.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- PRT400.T17 (harness `lut PRT400.CUB PRT400.T17`): c4d1b64a4ade5532c0e690c7a8d038dc
- PRT400.CUB: 6cd7a098399b15524404d55922704596
- gen_prt400.py: 0badfb11d00ae625bd6d53ac765bbb1e (`python3 -I gen_prt400.py <outdir>` writes the CUB)
- judge2_PRT400\ : judge2_PRT400.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: Neg 400 x1.0
- Halation: negative profile

## Earlier README (before the separation round)
# PRT400 - Portrait 400 (catalogue id 33, type neg)

An ISO 400 professional portrait C-41 negative, rendered as a lab scan. The film has low contrast, a long
overexposure latitude with a soft shoulder (white 251, no hard clip), warm-neutral skin, neutral greys and
more colour than Portrait 160 (PRT160). Grain and halation come from the module's effects, not from this LUT.

**Fix round 1 (11-10).** Judge v2 failed the first version for four reasons:
- it was a near-duplicate of the camera (mean dE 0.92);
- it sat 2.33-2.85 from SUN200, PRONEG, PORT160 and TUN800;
- only one T17 was delivered;
- the C4 metric did not match the judge's.

The look is rebuilt as two candidates for the judge to pick from (PLAN 7.2). Both pass every judge v2 gate.

**Fix round 2 (11-11).** Round 1 left A at 2.80 from PORT160, under the >= 3.0 the judge asked for where
reachable. A now gets +25 % colour on the soft colours of the mid tones (`vib` in the generator, claim C3). That
moves A to **3.02 from PORT160**, 3.39 from SUN200, 4.37 from PRONEG and 2.64 from the camera, and every judge v2
gate still passes. B is unchanged (bit-exact T17). Greys, the toe, the shoulder and the C2 / C4 metrics of A are
unchanged too, because the gain acts only on colours.

| candidate | file | what it is |
|---|---|---|
| **A (default)** | `PRT400.T17` / `PRT400.CUB` | **warm-neutral lab scan**: dense film toe (black 4, input 8/16 -> 8/12), mids a little open (118 -> 125), a long rising upper mid that rolls off only over the last stop, pastel highlights (colour thins to 62 % at white, pale skin kept peach), neutral greys, warm skin and mid-tone colours with clearly more colour (C3: chroma factor 1.15, skin pixels 1.38) |
| **B** | `PRT400B.T17` / `PRT400B.CUB` | **high-key pastel scan**: the same negative printed bright, the way a lab renders a generously exposed portrait negative. Mid grey 118 -> 131, clean but open shadows (black 4, no milky lift), the same long shoulder to 251, pastel colour (still more than PRT160), warm-neutral skin |

Neither candidate has a golden grey cast (both are 0.0 grey tint, unlike SUN200) or a lifted milky black
(black 4 against PRONEG's 13).

## Files
| file | what |
|---|---|
| `gen_prt400.py` | Generator: neg_scan family module plus the parametric OKLab model, with provenance in the header. `python3 gen_prt400.py [--preset all\|PRT400\|PRT400B] <outdir>` writes both cubes by default. |
| `data/*.csv` | Digitised source data. Every file starts with the publication, revision/date, page and figure. |
| `PRT400.CUB`, `PRT400B.CUB` | 33^3 cubes, candidates A and B |
| `PRT400.T17`, `PRT400B.T17` | The cubes converted by the module's own loader (`cd /root/ml/yuvre_work; ./harness lut X.CUB X.T17`), the same route Portrait 160 used |
| `checks_prt400.json` | Generator self-checks for both presets, plus provenance numbers: toe fits, spectral matrix and effective per-hue edits |
| `claims_prt400.json` | Claim metrics C2-C4 and the batch 0 rho_max for A, B and PRT160 (`src/claims.py`, judge v2 functions on the shipped kernel) |
| `judge2_PRT400/`, `judge2_PRT400B/` | **judge v2** JSON and sheets for each candidate: both PASS |
| `preview_prt400_curves.png`, `sheets/*.png` | Review sheets: A, B, the camera and the nearest films side by side |
| `src/` | `setup.sh` (shipped kernel as libfr.so), `build.sh` (both cubes and T17s), `verify.sh` (rebuilds both into the WSL work dir and compares md5), `j2.sh` (judge v2 on both), `claims.py`, `sheets.py`, `tune2.py` (variant scorer), `r2_sweep.py` (round-2 batch of tune2 variants), `r2_decomp.py` (splits a distance into lightness / colour / tint), `r2_j8.py` (locates kernel-vs-tetrahedral errors of 3 codes or more), `w.sh`, digitisers |

## Model
0. **neg_scan** works on the grey axis and is driven by the datasheet.
   - The display sRGB value is linearised and treated as scene light relative to 18 % grey. A camera toe
     f = 0.004 means JPEG black is not scene black. This gives log H around Log H Ref -1.44.
   - Each layer follows a softplus toe model fitted to the E-4050 characteristic curves (rms 0.003-0.007 D).
   - The scanner model divides by each layer's mid gamma and renders 10^(k*e) with k = 0.94. It then
     subtracts 97 % of the code-0 density, sets white at the JPEG white and grey-balances at the grey card.
   - This curve becomes the first part of the tone. Its tiny residual tint is added as a guarded
     grey-axis tint.
1. **OKLab model** of gen_port160.py.
   - Per-hue hue, chroma and lightness edits, plus a spectral hint from the E-4050 spectral sensitivities
     and the CIE CMF fit.
   - Then the tone knots, chroma vs lightness, skin guards, soft gamut and veil.
2. **New in the fix round**, all GUESSES in the code:
   - Per-preset tone knots that place the toe, the mids and the long shoulder.
   - satL with pastel highlights (C2).
   - `skin_hi_dh`: pale skin turned -4 deg toward red from input L 0.72 to 0.92, on skin chroma only. Without
     it, lifting pale warm colours toward white runs red into 255 first, and they drift toward lemon.
   - `skin_pastel_keep`: skin hues keep 60 % (A) or 50 % (B) of what the pastel highlights would take.
   - A per-preset gamut knee (`opt.knee`): 0.75 for A and 0.8 for B, against 0.88 in the family.
   - kappa 0.3: chroma follows the lightness change only partly.
3. **New in fix round 2 (11-11)**, a GUESS in the code: `vib` = (0.25, (0.45, 0.58, 0.66, 0.80), (0.06, 0.16),
   0.5, 2), A only. It is a chroma gain of 1 + 0.25, shaped by four weights:
   - it grows in over output OKLab L 0.45..0.58 and fades out over 0.66..0.80;
   - it fades out over input chroma 0.06..0.16;
   - it is halved on skin hues, with a wide hue weight (kappa 2).

   Every weight is a smooth function of OKLab L, C and h. Things tried in round 2 that failed:
   - A global gain, a per-hue gain or a stronger mid satL bent the cube at its saturated top (d2 0.060-0.070 at
     green 0,215,96 and cyan 0,191,239).
   - A weight on the RGB saturation (q_rgb), or a window reaching the deep tones, changed too fast between the
     17^3 nodes. That gave J8 kernel p99.9 3 (one deep skin grid node, 117,57,25, and scene/lake U).
   - Gains above 0.25 also gave J8 p99.9 3 (lake U).
   - Less colour on skin (a fuller skin fade, or skin S 1.16 -> 1.04-1.08) put one red-side skin grid sample at
     hue 39.2-39.7 and dropped PORT160 below 3.0.

## Sources (read with WebFetch; numbers digitised, files not kept)
- [1] Kodak Alaris E-4050, Portra 400 technical data, January 2025 (Revised 1-25):
  - p.4 Characteristic Curves (graph E4040C, Daylight, Status M, Log H Ref -1.44)
  - p.4 Spectral-Sensitivity Curves
  - p.4 Spectral-Dye-Density Curves
  - p.3 Print Grain Index
  - p.3 negative-density table
  - p.1 description text
- [2] Kodak Alaris E-4051, Portra 160 technical data, February 2016 (Revised 2-16): p.4 Characteristic
  Curves, p.3 Print Grain Index and p.1 text. Used only for the relative claims.
- [3] Wyman, Sloan, Shirley, "Simple Analytic Approximations to the CIE XYZ Color Matching Functions",
  JCGT 2(2), 2013.
- [4] Kodak Publication E-4021, Portra Endura / Supra Endura Paper technical data, October 2003, p.9,
  Characteristic Curves of the Supra Endura paper (F002_1197AC, Status A). Used only for the rho_max cap,
  digitised in `catalogue/looks/SUN200/data/E4021_paper_characteristic.csv`.

## Claims (relative, cited; metrics stated)
All metrics are judge v2 functions on the shipped kernel (`claims_prt400.json`). PRT160 = `PORT160.T17`.

| # | claim | source | metric | A | B | PRT160 |
|---|---|---|---|---|---|---|
| C1 | coarser grain than PRT160 | PGI 37 vs 28 at 4x6 from 135 ([1], [2] p.3) | grain row | Neg 400 x1.0 | Neg 400 x1.0 | Neg 160 |
| C2 | longer straight line above normal, soft highlights | E-4050 slopes hold to Ref +2; E-4051 red bends to ~0.4 ([1], [2] p.4) | luma slope over input grey 160..230 (display units) | **0.986** | **0.914** | 0.871 |
| C3 | more colour than PRT160 | "exceptional color saturation" ([1] p.1) vs no saturation claim ([2] p.1) | J5 synthetic chroma factor / skin-pixel chroma factor | **1.154 / 1.376** | **0.947 / 1.061** | 0.844 / 0.974 |
| C4 | same low mid contrast as PRT160 | G gamma 0.554 vs 0.549 ([1], [2] p.4) | **judge v2 mid slope**: log2 linear output luma / log2 linear input, grey codes 100 -> 139 (-0.5 / +0.5 EV around 118, `judge2.grey_code`) | **0.928 (+5.3 %)** | **0.910 (+3.3 %)** | 0.881 (window 0.793..0.969) |

The first version's C4 used another slope (the own judge's): 1.000 vs 0.951. It is replaced by the judge
v2 mid slope above, and both candidates stay within +-10 % of PRT160.

## Guesses (GUESS in the code)
Several inputs are GUESSES:
- the scan parameters: contrast 0.94, camera toe 0.004, black point 0.97, toe tint 0.3
- each preset's tone knots, satL, per-hue base edits, kappa, skin guards, veil, gamut knee, spectral-hint strengths and A's `vib`
- the 5500 K illuminant for the hint
- no inverse of the picture-style curve (as in PLAN 6.1)

The spectral ring's mean chroma loss of 0.77 is dropped. It is taken as compensated by the interlayer
effects and the scanner, neither of which is published.

## Judge v2 results (v1.1, shipped kernel, `--others catalogue/looks`, re-run 11-11 on the round-2 files)
| check | A `PRT400` | B `PRT400B` |
|---|---|---|
| verdict | **PASS** (no warning) | **PASS** (warning: 2.24 vs PORT160, owner band) |
| J6 T17 | 4914 words, flag 1, top byte 0, neutral Y 4..251 | same, neutral Y 4..251 |
| J1 cube | luma axes -0.71, d2 max 0.0564 / p99.9 0.0384 | luma axes -0.96, d2 0.0449 / 0.0291 |
| J2 grey | 0 reversals, white 251, black 4, tint 0.0 | 0 reversals, white 251, black 4, tint 0.0 |
| J3 skin | probes hue 42.9-66.8 (C ratio min 0.73); grid 40.8-68.1, 0/340 outside, 0 grey, 0 green | probes 42.7-66.0 (C ratio min 0.60); grid 41.4-66.8, 0/0/0 |
| J4 ramps | worst 2 L reversals, hue at clip 2.7 deg | worst 3, 5.2 deg |
| J5 blacks | 0 % crushed | 0 % crushed |
| J8 kernel | p99.9 2 (card Y), max 7 (chart Y) | p99.9 2, max 7 |
| J9 Effect | grey 0 reversals at subtle and strong; hue at clip 2.1 / 9.4 deg | 0 / 0; 1.6 / 1.8 deg |
| grey luma 0/8/16/32/64/96/118/160/200/230/250/255 | 4/8/12/29/68/103/125/168/211/237/249/251 | 4/9/14/33/72/109/131/176/218/240/249/251 |

Generator self-check note: `checks_prt400.json` counts every luma dip greater than 0.001 (0.26 of a code)
along the cube lines. A has 94 + 36 such dips and B has 51 + 62. They are below one 8-bit level; the worst
is -0.71 (A) and -0.96 (B) against judge v2's bound of -1.0.

Margins are thin on A, because the 17^3 table is near its resolution:
- the skin grid spans 40.8-68.1 inside 40-70;
- J8 chart Y has 0.083 % of pixels at 3 codes or more, against the 0.1 % that would make p99.9 3;
- J9 strong hue at clip is 9.4 of 10 (unchanged since round 1);
- d2 is 0.0564 of 0.06.

## Distinctness
Mean OKLab dE x100 over lake, sunset, scene, card and chart, from judge v2:

| film | A `PRT400` | B `PRT400B` |
|---|---|---|
| ORIGINAL (camera) | **2.64** | **3.91** |
| the other candidate | 2.43 | 2.43 |
| PORT160 (PRT160) | **3.02** | 2.24 (band) |
| SUN200 (batch 1) | **3.39** | 5.21 |
| C250D_branch (judge's built-in Cine 250D) | 4.23 | 4.35 |
| PRONEG | **4.37** | 5.01 |
| CLASNEG | 4.39 | 5.73 |
| TUN800 (batch 1) | 4.40 | 5.97 |
| CCHROME | 4.51 | 5.65 |
| CHUNGK | 5.42 | 7.10 |
| ETERNA | 6.16 | 6.60 |
| VSL50 (batch 1) | 6.78 | 7.21 |
| PRS400 (batch 1) | 8.28 | 6.97 |

Round 1 for comparison: A was 2.50 from the camera, 2.80 from PORT160, 3.31 from SUN200, 4.20 from PRONEG
and 2.27 from B.

**Where A and B are measured against each other.** A's run skips B as a variant, because B's name starts
with the code PRT400. B's run (code PRT400B) includes A, which gives the A-B distance of 2.43. The distance
is symmetric.

**What A changes, against the camera and the two nearest films.** These are per-image means of the kernel
outputs from `src/r2_decomp.py` (round-1 A; round 2 adds about +0.3 chroma in the mid tones):

| film | lightness vs camera | colour vs camera | tint |
|---|---|---|---|
| A | +1.8 | +0.6..0.9 | neutral |
| PORT160 | +2.7 | -0.75 | cream (warm magenta) |
| SUN200 | -1.2 | +0.7 | golden (b +1.1) |
| PRONEG | about 0 | -1.2 | milky lifted black |

A is therefore separated from PORT160 by colour and tint, and from SUN200 by lightness and tint.

**Owner note (J5 2-3 band):**
- **A is now at 3.02 from PORT160.** The >= 3.0 target is met, and SUN200 (3.39) and PRONEG (4.37) are >= 3
  too. The margin is small, and the judge v2 gates set the ceiling (see Model item 3).
- **B vs PORT160 is 2.24, and A vs B is 2.43.** B is the bright alternative and passes J5 against everything
  (SUN200 and PRONEG are >= 5).
  - B is boxed in between PORT160 (bright, cream, less colour) and A (more colour). Every round-2 attempt to
    move it failed, so the B T17 is unchanged from round 1:
    - More colour on B (vib, S, satL) raised PORT160 to 2.38-2.70 but brought A down to 2.08-2.30, and broke
      J4 (4 reversals), J1 luma axes (-1.13) or J8 (3).
    - A cool, cyan-leaning scan tint (the other common lab-scanner rendering) gained only 0.1 (2.32-2.41) and
      pushed skin past hue 70 (70.0-72.0).
    - In round 1, a brighter B top broke monotonic luma along the cube's red lines.
- **A's skin is rich.** Skin pixels carry 1.38x the camera's chroma (round 1: 1.29; PRT160 0.97). The hues stay
  at 43-67 (warm, not red, not yellow), but the face crop reads tan-orange next to the camera. Less skin
  colour failed the skin grid (see Model item 3).

**For the pick:**
- A is the more recognisable portrait-400 scan: dense toe, rich warm skin and mid-tone colours, a pastel
  shoulder, and about 35 % more colour than PRT160.
- B is the overexposed, pastel high-key rendering. Its skin is more neutral, but it sits nearer PRT160.

## Grain / halation proposal (batch 0, PLAN 3.2.4 b)
- Grain row **Neg 400 x1.0**. This is C1: PGI 37 is coarser than the ISO 160 member's 28, and 37 at 4x6 is
  a fine grain for ISO 400.
- **rho_max (per film) = 2.48, Q8 635**, by the PLAN 3.2.4 (b) rule. The T17's own range is
  log10(L(Y255) / L(max(Y0, 1))), where L is the sRGB decoding of the shipped kernel's neutral-axis output:
  - A: Y0 = 4, Y255 = 251, so log10(0.965 / 0.00121) = 2.90.
  - B: the same codes, 2.90.
  - This is capped by the colour paper's published net neutral Dmax: [4] p.9, 2.58 at the plot end minus
    D-min 0.10 = 2.48. That is the same display medium and cap as SUN200.
  - So rho_max = min(2.90, 2.48) = 2.48 for either candidate.
  - The row's current value 3.31 is the one PLAN C4 replaces. It was not used.
- **print_gamma check:** its origin is not stated in `yg_stocks.c`, so it is re-derived from the T17 and
  marked DERIVED: judge v2 mid slope / neg_gamma.
  - With the row's neg_gamma 0.60: A 1.55, B 1.52.
  - With E-4050's own green mid slope 0.554: A 1.68, B 1.64.
  - Either way it is well below the row's 2.6. This is a finding for the batch 0 report; the row is not
    changed here.
- Halation: **neg** profile, x1.0. The datasheet gives no halation data, so this is the type default for a
  colour negative.

## Final md5
- `PRT400.CUB` 64534250614440011ecbeeb2c8f55e06
- `PRT400.T17` e9fd65c7ac00c4aa75c7be65b5a55b4d (candidate A, default; round 2)
- `PRT400B.CUB` 3b18e8e4b8ad61161a195e0b7b82040e
- `PRT400B.T17` 965eed3246c3bbc5c1eb8c213ff200fe (candidate B)
