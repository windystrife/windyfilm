# SHP50 - Sharp 50 (catalogue id 54, type bw) - judge v2.1 PASS

A slow, high-contrast, very fine grain panchromatic ISO 50 negative developed to a high contrast index, printed on a hard paper (paper_k 80/95, grade 3) with the metered grey at net density 0.70 and scanned: deep blacks, bright clean whites, steep mid-tones. Shown ISO 50, camera ISO 100 (catalogue row).

Status: **PASS** on judge v2.1 (default kernel, final set of 2026-11-11). Kept in the release.

## Files

- `SHP50.CUB` (33^3, R = G = B on every line), `SHP50.T17` (17^3 YUV, 4914 words, raw U = V = 0x00): the shipped pair.
- `gen_shp50.py`: the generator (provenance and GUESS list in its docstring); `data/*.csv`: the digitised / public inputs, each with its source header.
- `mono_t17.py` (family copy, md5 9fdaefa3a23f2d08688b33b754e036f7): forces U = V = 0x00 after the module loader; `mono_t17.log` its report.
- `checks_shp50.json`, `provenance_shp50.json`, `bw_model_shp50.json` (spectral model for the J-BW self-check), `preview_shp50_curves.png`.
- `judge2_SHP50/`: final judge v2.1 JSON, stdout and sheets (scenes, skin, ramps, colorchecker, kernel, distinct, effect).
- Rebuild (WSL Ubuntu-24.04, root): `bash tools/final.sh` (generator defaults -> CUB -> `/root/ml/yuvre_work/harness lut` -> mono_t17.py). Deterministic: same md5s as below.

## Sources

- PARAMETRIC (clean room, no downloads in this batch): the maker's sheet was not digitised. Spectral sensitivity = blue Gaussian + green-red logistic plateau + red logistic cut-off; negative = fog + gamma x softplus toe, placed by the ISO 6 speed point at box speed. Every value is a LOOK CALL set from the published descriptions summarised in catalogue/inventory.md (B&W list). Sheet named there: the maker's ISO 50 fine-grain data sheet (curves, spectral sensitivity); documented traits: extremely fine grain, high contrast, deep blacks, tight latitude.
- HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013: p.2 filter-2 characteristic curve (data/mgfb_classic_char_filter2.csv); p.1 ISO range table (grade 2 = R95; other grades by paper_k = R(target) / 95).
- CIE 018:2019 Table 6 (CIE 1931 2-degree CMFs), CIE 015:2018 daylight S0/S1/S2 (D55 = 5503 K), ColorChecker Classic reflectances (Ohta 1997, colour-science BSD-3; J-BW self-check only): data/cie*.csv, data/colorchecker*.csv.

## Look calls (GUESS)

- sensitivity: blue 440 nm / 35 nm / 1.0, plateau rise 495 nm / 12 nm, plateau 0.85, red cut 655 nm / 8 nm
- negative: ISO 50, fog 0.20, gamma 0.72, toe softness 0.14 log H
- paper_k 80/95 (grade 3), print anchor "grey" 0.70
- scan levels 7 / 251

Family kernel fit (engineering, judge v2.1 per-pixel-E kernel): E soft limit 60 / 120 codes, no flat E, slope scaling below G' 0.8 over a +-16 code window, negative E limited to 40 codes, monotone closure along +R/+G/+B.

## Claims (testable on the sheets; numbers from the shipped tables)

| claim | test | result |
|---|---|---|
| high contrast: mid slope above every 100-400 film (next: Street 400 / Crisp 100) | SHP50.s_mid > STR400.s_mid (1.66 vs 1.25) | holds |
| deep blacks: input 64 darker than every film except Night 3200 (next: Crisp 100) | SHP50.grey.2 < CRS100.grey.2 (19 vs 40) | holds |
| bright clean whites: input 245 -> >= 249 | SHP50.grey.8 >= 249 (249 vs 249.0) | holds |

Tone of the shipped table (grey input -> output code, inputs 16/32/64/96/118/160/200/230/245): 7/7/19/79/123/184/226/244/249; log-log slopes shadows (32->64) 0.81, mids (96->160) 1.66, highlights (200->245) 0.48.
ColorChecker output L (OKLab): red 0.460, green 0.569, blue 0.420, yellow 0.861, dark skin 0.368, light skin 0.767; spectral weights (R, G, B) (0.226, 0.459, 0.314).

## Judge v2.1 results

| gate | value |
|---|---|
| verdict | PASS, failed none |
| J1 cube luma axes / d2 max / d2 p99.9 | 0.0 / 0.0312 / 0.0299 |
| J2 white / black floor | 251 / 7 |
| J4 ramp reversals (worst) / deepest dip | 0 / {'deepest': -0.002, 'at': 'foliage'} |
| J8 kernel-added p99.9 / max, kernel max | 1 / 0, 5 |
| JBW raw U/V bytes nonzero / output chroma / grey-ramp UV | {'U_nonzero': 0, 'V_nonzero': 0} / 0 / 0 |
| JBW ColorChecker self-check mean / max |dL| | 0.0036 / 0.0143 (worst blue) |
| J5 nearest / second (mean of 3 strata, x100) | CRS100 3.45 (parts [3.66, 4.71, 1.97]) / STR400 4.28 |

Distinctness: nearest Crisp 100 3.45, second Street 400 4.28. PASS.

## Grain row and halation profile (proposal)

- Grain: new row **B&W 50**: the maker publishes no RMS; extremely fine grain is described, so **0.6 x B&W 100T** (about rms 5) is proposed (GUESS).
- Halation **BW (YH_BW) x 1.0** (GUESS).

Details line: "ISO 50 B&W, high contrast, fine grain".

## md5

| file | md5 |
|---|---|
| `SHP50.CUB` | 989b6f6abd41d595f4ff47761732ce9b |
| `SHP50.T17` | eeb57b882dc9dee0cd93c1e21781e5de |
| `gen_shp50.py` | 0e87cd09733d0b2171bc87339c5b6647 |
| `mono_t17.py` | 9fdaefa3a23f2d08688b33b754e036f7 |
| `checks_shp50.json` | 02972d65f9d3802967cefe29a65b6489 |
| `provenance_shp50.json` | b5dfb55d6c6cf779221888799108c833 |
| `bw_model_shp50.json` | 2061ac54310252d1f1f77929e759ac95 |
| `preview_shp50_curves.png` | 7287781497024316a7534f18ff59b5cd |
| `tools/final.sh` | e410da82f33a24c1bdb56d2433f396cf |
| `data/cie1931_2deg_10nm.csv` | cd967fbccba6c1ab974844c267d40c88 |
| `data/cie_daylight_S012_10nm.csv` | 113659faec72f1d238369e30fb8a0b4b |
| `data/colorchecker_ohta1997_10nm.csv` | 3a49c6318ce61af97b9345d8a7685de7 |
| `data/mgfb_classic_char_filter2.csv` | eff4cede552e1f07a08cd20442e0f599 |
| `judge2_SHP50/SHP50_colorchecker.png` | 3b54b96ada8d3c285ff2dff530d108a5 |
| `judge2_SHP50/SHP50_distinct.png` | ac3aef6d7f3f171a55d384ceee148758 |
| `judge2_SHP50/SHP50_effect.png` | 0179901e56a183709a71fa6619b345c2 |
| `judge2_SHP50/SHP50_kernel.png` | d01a3df90412c43de4c0ca0521b20be2 |
| `judge2_SHP50/SHP50_ramps.png` | 7a9b1d0f13d9e520646c3991e37721a5 |
| `judge2_SHP50/SHP50_scenes.png` | 743539a1de92bf82ca78c4bd64e9dbc7 |
| `judge2_SHP50/SHP50_skin.png` | 05a27a5ec4cc1989ea2c3eaf63466324 |
| `judge2_SHP50/judge2_SHP50.json` | 36bfcea6021af3e93683493c6b75a765 |
