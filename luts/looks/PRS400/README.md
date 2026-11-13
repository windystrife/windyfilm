# PRS400 - Press 400 (catalogue id 37, type bw)

A WindyStrife own film: a classic cubic-grain panchromatic black-and-white ISO 400 negative, developed in a
D-76-class developer to the data sheet's time and printed on a measured grade-2 paper, as a scanned print.
Rich mid-tones, open shadows, a gentle shoulder. The table is luma only: R = G = B on every .cube line, and
raw U = V = 0x00 in every T17 word.

Shipped strings stay generic ("Press 400", "PRS400"). Maker and product names appear only in this file, in
the generator's comments and in the CSV headers, where they cite sources.

## Files

| file | what |
|---|---|
| `gen_prs400.py` | the generator (family module `bw_spectral`, PLAN 6.2). It writes the cube, checks, provenance, the J-BW model and the curves preview. |
| `data/*.csv` | digitised or copied source data. Each file has a header with publication, revision/date, page and figure. |
| `PRS400.CUB` | 33^3 .cube with red fastest and R = G = B. |
| `PRS400.T17` | the cube through the module's own .cube loader (`/root/ml/yuvre_work/harness lut`, as Portrait 160 was converted), then `mono_t17.py`. |
| `mono_t17.py`, `mono_t17.log` | the documented B&W post-step (see "T17 conversion"). |
| `checks_prs400.json`, `provenance_prs400.json`, `bw_model_prs400.json` | generator outputs. `bw_model` is the `--bw-model` input for judge v2. |
| `preview_prs400_curves.png` | generator diagnostics: grey curve, spectral fit, negative curve, paper curve. |
| `judge2_PRS400/` | judge v2 (1.1) result JSON and its seven sheets. |
| `sheets/` | my review sheets (shipped kernel) and `claims_prs400.json` (J7). |
| `tools/final.sh` | generator -> loader -> mono step -> copy finals (WSL). |
| `tools/review_prs400.py`, `tools/klib.sh` | review sheets and claim tests. `klib.sh` builds the same host kernel library the judge builds. |
| `tools/trace_spec.py`, `tools/trace_hd.py`, `tools/trace_paper.py` | the digitisers named in the CSV headers. |

Rebuild (WSL Ubuntu-24.04, root): `bash tools/final.sh`. The output is deterministic: same md5s as below.

## Model (steps as in the generator docstring)

1. Input: the camera Neutral JPEG, treated as sRGB-encoded scene light (GUESS), converted to linear.
2. Spectral filter: film exposure F = w . (R, G, B)_lin. Here w = (0.2153, 0.4164, 0.3683) is a regularised
   least-squares fit of the data-sheet sensitivity x D55 by the linear-sRGB CMFs x D55. Both sides have unit area
   (D55 grey -> F = grey), sum w = 1, and the ridge is alpha 1e-3 toward Rec.709. The relative residual is 0.41,
   because a three-primary basis cannot follow the sheet's two-hump curve. Varying alpha from 0 to 1e-2 moves w
   by < 0.02.
3. Exposure: the meter equation (K 12.5, q 0.65, ISO 400). The JPEG mid-grey (118) is the metered grey,
   1.04 log H above the ISO speed point.
4. Negative: the sheet's D-76 large-tank curves at 6 and 8 min, as smooth fits (rms 0.004 / 0.010 D),
   interpolated to the recommended 7 3/4 min.
5. Print: 7 % enlarger stray light (ISO 6846 annex A gives 5-10 %), then the measured grade-2 paper curve
   (monotone P-spline, rms 0.005 D). From the curve, LER is 0.94 = R94; the sheet's own table says R95. ISO 6846
   annex A match: the negative's effective range from the speed point to JPEG white is 0.90, against LER 0.94,
   an exact match for a medium paper. Print exposure (GUESS rule): the metered grey sits at the midpoint of the
   paper's ISO range in log E.
6. Scan: levels from the print's darkest to lightest tone, mapped to output codes 7 / 251 (GUESS).
7. Kernel fit (engineering, not film data). The shipped 2.5D kernel computes Y' = N[y] + E(pair-mean luma, u, v).
   E is one signed byte per (65 luma x 17 x 17) node, and the camera chroma is integer. The pure model gives
   luma steps there (J4 5-9 reversals, J8 max 9-12). The table therefore stores G(y601) + E', where G is the grey
   curve and E' is the spectral shift with five changes:
   - a soft limit (identity to 32 codes, tanh to 64)
   - E taken at the mid-gamut luma of its camera chroma ("flat" E)
   - E scaled by smooth-min(1, G'/0.8) where the grey slope is low
   - the negative side (colours rendered darker than their camera luma) soft-limited to 6 codes
   - a monotone closure along +R, +G, +B

   These were tested and rejected, each failing J4 or J8:
   - shadow fades
   - chroma coring
   - a two-sided flat E
   - negative limits of 8, 10 and 12
   - knee/limit 64/112

   Cost: the table differs from the pure model by mean 0.014 / max 0.062 OKLab L on the ColorChecker (worst:
   blue), and the yellow/green darkening is about 2/3 of the model's.

Tone result through the shipped kernel (grey in -> out):

| in | 0 | 16 | 32 | 48 | 64 | 96 | 118 | 140 | 160 | 200 | 230 | 245 | 255 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| out | 7 | 9 | 16 | 32 | 55 | 112 | 143 | 170 | 190 | 223 | 241 | 247 | 251 |

Exposure wedge from grey 118: -3 EV 41->25, -2 EV 60->49, -1 EV 85->93, +1 EV 162->192, +2 EV 221->236.

## Sources (read through WebFetch; numbers digitised from 600 dpi page renders; the downloaded files were deleted)

- **Kodak Professional F-4017**, "KODAK PROFESSIONAL TRI-X 320 and 400 Films", December 2016:
  - p.7 "Spectral Sensitivity Curves", TRI-X 400 / 400TX. The D = 1.0 > gross fog curve is used and D = 0.3 is
    reported. -> `data/f4017_spectral_sensitivity.csv`
  - p.8 "Characteristic Curves, TRI-X 400 / 400TX, 35 mm", D-76 large tank, 20 C, 6/8/10/12 min.
    -> `data/f4017_char_d76.csv`
  - p.3 development table: D-76, large tank, 68 F, 7 3/4 min.
  - p.2: the starting-point times "produce a contrast index of 0.56".
  - p.6: diffuse rms granularity 17.
- **HARMAN technology**, "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013 (MGFB_CLASSIC_1013):
  - p.2 "Characteristic curves", glossy, filter 2 curve. -> `data/mgfb_classic_char_filter2.csv`
  - p.1 ISO range table: filter 2 = R95.
- **CIE 018:2019** Table 6, CIE 1931 2-degree CMFs (DOI 10.25039/CIE.DS.xvudnb9b). -> `data/cie1931_2deg_10nm.csv`
- **CIE daylight components S0/S1/S2** (CIE 015:2018 model). -> `data/cie_daylight_S012_10nm.csv` (D55 = 5503 K)
- **ColorChecker Classic reflectances**, Ohta 1997, via colour-science (BSD-3). -> `data/colorchecker_ohta1997_10nm.csv`
  (used only by the generator's own J-BW check)
- **ISO 6846:1983** (paper ISO range, annexes A/B), **ISO 6:1993** (speed point), **ISO 2720** (K = 12.5).

## Claims (J7, inequalities, through the shipped kernel) - all pass

Each claim is tested against the Rec.709 luminance of the same ColorChecker patch (judge2 D55 data), sent through
the same table as a grey: a luminance monochrome with the film's own tone. dL is in OKLab L.
Results are in `sheets/claims_prs400.json`.

| claim | basis (cited) | test | result |
|---|---|---|---|
| C1a blue lighter | p.7: log S 2.71 at 380-450 nm vs 2.40-2.45 at 550-600 nm (about 2x, equal energy) | blue >= +0.05; mean(blue, purplish blue, blue flower, blue sky) >= +0.03 | +0.135; +0.090 |
| C1b yellow/green darker | same | yellow, orange yellow, yellow green each <= -0.03; green <= -0.05 | -0.046, -0.034, -0.050; -0.082 |
| C2 red near luminance | p.7: D = 1.0 curve still log S 2.41 at 620 nm, falls to 0.1 at 665 nm (panchromatic, no extended red) | abs(red) <= 0.03 | -0.007 |
| C3 medium contrast, long straight line | p.2 CI 0.56; p.8: 8 min curve has no shoulder up to D 2.03 | out(255) - out(230) >= 8; mid-grey L slope (108..128) in 1.1..1.4 | 10; 1.30 |
| C4 grain (effects row) | p.6 rms 17, coarser than the T-grain 100 class (rms 8) | grain row B&W 400 x 1.0 | proposal |

C1b was restated. The first draft said "yellow <= -0.05" and gets -0.046: the kernel fit's 6-code negative limit
holds it back, while the pure spectral model gives -0.065. Negative limits 8 and 10 would reach -0.049 and -0.055,
but they fail J4 and J8, and so does 12. The record is in `claims_prs400.json` under `first_draft_test`.

For the SLK100 red-order rule (PLAN J-BW: SLK100's red darker than PRS400's by >= 0.05), the PRS400 red patch is
L 0.5417 (judge info `JBW_red_patch_L`).

## Guesses (GUESS in the code)

- The Neutral JPEG is sRGB-encoded scene light, and camera WB is diagonal in linear sRGB.
- ev_place 0 (box speed).
- Stray light 7 %.
- Print exposure: grey at the midpoint of the paper ISO range. Rejected anchors are in the docstring:
  - JPEG white at T: grey 150, lighter mids
  - grey at print density 0.70: grey 133, but -2.6 EV goes to 24 (blocked shadows)
- Scan levels 7/251.
- Linear-in-time interpolation from 6 to 8 min, and the fitted negative toe below log H -3.4.
- Paper Dmin 0.020, and the paper toe below log E 0.30 (hidden in the figure; mean of the filter 1 and 3 traces).
- Sensitivity 0 beyond 665 nm.
- All of model step 7 (engineering).

Trade-off: mid-grey 118 prints at 143, brighter than the catalogue's other films (about 116-129). A darker print
exposure blocks the shadows on this paper. The physically consistent "mid" anchor was kept.

## T17 conversion and the mono post-step

`harness lut PRS400.CUB` (the module's .cube loader) gives the loader T17. `mono_t17.py` then does the following:
- asserts 4914 words, flag 1, top byte 0 and a non-decreasing neutral-node Y
- refuses any U/V byte other than 0x00 / 0x01 / 0xFF
- forces U/V to 0x00

On this cube the loader left no +-1 bytes (0 U, 0 V changed), so the loader T17 and PRS400.T17 are identical.
The generator itself never touches the T17.

## Results (judge v2 1.1, shipped kernel fr_film.c md5 362ac3f5c27a175454b65b1ba2d92211): PASS

Command:
```
judge2.sh PRS400.T17 --type bw --code PRS400 --others catalogue/looks --bw-model bw_model_prs400.json --white 251 --black 7
```

| gate | value | limit |
|---|---|---|
| J6 size / flag / top byte / neutral Y rising | 4914 / 1 / 0 / [7 .. 251] | |
| JBW raw U/V bytes | 0 / 0 non-zero | == 0 |
| J1 cube grey monotonic / luma axes | yes / 0.0 | drop <= 1 level |
| J1 d2 max / p99.9 | 0.0144 / 0.0113 | <= 0.06 / 0.04 (PORT160 0.0354 / 0.0135) |
| J2 white / black / grey reversals | 251 / 7 / 0 | white >= 248 (batch bar; judge bw floor 230), declared +-3 |
| J4 L reversals per ramp | [0,0,2,0,3,2,2,2,3,2], worst 3 | <= 3 |
| J5 crushed blacks (card, chart, scene) | 0 / 0 / 0 % | == 0 % at max <= 2 |
| J8 kernel vs tet, per image and channel | p99.9 worst 2 (card Y), max worst 7 (night Y); U, V 0 | <= 2 / <= 7 |
| J9 Effect subtle / strong grey reversals, B&W chroma | 0 / 0, 0 / 0 | == 0 |
| JBW output chroma (all images, grey ramp) | 0 | == 0 |
| JBW ColorChecker self-consistency | mean 0.0136, max 0.0623 (blue) | <= 0.03 / <= 0.07 |
| J5 distinct nearest | see "Distinctness" | >= 2.0 |

J-BW is a self-consistency check: the table against the film's own spectral model. It is not a camera check
(that is T6c). PLAN says these thresholds are set first on PRS400 and then frozen; this film passes them
without changing them.

Info only: J9 Strong has a ramp L-reversal worst of 8 (the judge does not gate it), white 250, black 9.
`J1k_kernel_luma_axes` is reported by the judge.

## Distinctness (mean OKLab dE x100, kernel outputs; lake, sunset, scene, card, chart)

Final re-check: 2026-11-10 13:32, after cleanup. The run used `--others catalogue/looks` plus the Cine 250D
tables, which were read only.

| film | dE |
|---|---|
| CCHROME (nearest) | 7.58 |
| CLASNEG | 8.00 |
| PORT160 | 8.22 |
| PRT400 | 8.64 |
| PRONEG | 9.15 |
| CHUNGK | 9.37 |
| ETERNA | 9.75 |
| CINE250D print | 9.84 |
| CINE250D | 10.68 |

Every film is far above 2.0, as expected for a luma-only film against colour films. SUN200, TUN800 and VSL50
had no T17 in `catalogue/looks` at the time of the check, so they are not compared. As colour films, they will
also be far from a B&W table. No other B&W own film exists yet.

## Grain / halation proposal

- Grain row **"B&W 400" x 1.0**. Basis: F-4017 p.6 diffuse rms granularity 17 (at net D 1.0, 48 um, 12x). That is
  the catalogue's cubic-grain 400 class, coarser than the T-grain 100 row (rms 8).
- rho_max, derived by the PLAN 3.2.4 rule from this T17: log10(L(Y255 = 251) / L(Y0 = 7)) = 2.66, capped by the
  paper's published net Dmax. The MG FB Classic p.2 plateau gives net 2.05, so rho_max = 2.05. print_gamma: take
  it from the table's mid-grey slope (OKLab-L slope 1.30 at grey 118) if no public source is found.
- Halation profile **BW (YH_BW)** at scale 1.0. Basis: a conventional B&W negative with an anti-halation layer;
  F-4017 gives no halation figure, so the strength is a GUESS at the standard B&W profile.
- B&W order: grain and halation are applied after the colour stage on a luma-only image (PLAN 7.5
  `bw_after_colour`), so the output stays exactly neutral.

## Final md5

| file | md5 |
|---|---|
| PRS400.T17 | 10ea6e861ef02395639fa3e220e2366f |
| PRS400.CUB | 1b9ecd952985c522dcba82c14f1812c6 |
| judge2_PRS400/judge2_PRS400.json (final run) | 2e7ea671ab0310fec72cc5df18547480 |
| gen_prs400.py | c58da45267a13af523405f8f99126f4b |
| mono_t17.py | 9fdaefa3a23f2d08688b33b754e036f7 |
| tools/review_prs400.py | 0897bea6cd9f0edb4ce379af20f03c41 |
| data/f4017_spectral_sensitivity.csv | 80a31014601f0596417c1a16a34662f2 |
| data/f4017_char_d76.csv | bf831a1abc3eab679136c262ca11fdd9 |
| data/mgfb_classic_char_filter2.csv | eff4cede552e1f07a08cd20442e0f599 |
| data/cie1931_2deg_10nm.csv | cd967fbccba6c1ab974844c267d40c88 |
| data/cie_daylight_S012_10nm.csv | 113659faec72f1d238369e30fb8a0b4b |
| data/colorchecker_ohta1997_10nm.csv | 3a49c6318ce61af97b9345d8a7685de7 |

## Judge v2.1 re-run (2026-11-11)

Table unchanged (PRS400.CUB 1b9ecd952985c522dcba82c14f1812c6, PRS400.T17 10ea6e861ef02395639fa3e220e2366f). Final judge v2.1 with sheets in `judge2_PRS400/` (judge2_PRS400.json e4289f511f6fb74f3a2ef54917cfcd88; the v2 1.1 folder was replaced).
- Verdict **FAIL**, failed ['J5_distinct_nearest']. Self-check mean / max |dL| 0.0136 / 0.0623 (worst blue); J1 d2 0.0144; J4 worst 0; J8 kernel max 5; red patch L 0.5417.
- J5: nearest **CLS125 1.35** (FAIL < 2.0), second SLK100 2.56. CLS125 (Classic 125) is a film another agent added to catalogue/looks on 2026-11-11 20:57, built on the same F-4017 sheet lines as this film; it is not part of this batch, so Press 400 was NOT changed. Owner decision: move CLS125 (the newer film) or re-anchor one of the two. Against the films of this batch: Silk 100 2.56 and Fine 100 2.68 (2..3 warning band).
