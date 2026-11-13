# STR400 - Street 400 (catalogue id 41, type bw) - judge v2.1 PASS

A softer-printed cubic-grain panchromatic ISO 400 negative with a long gentle toe and a long straight line, printed on a grade-0 paper (paper_k 140/95) and scanned: dense mid-tones, highlights that keep separating, a blue-heavy spectral response (reds print darker, blues lighter than Press 400).

Status: **PASS** on judge v2.1 (default kernel, final set of 2026-11-11). Kept in the release.

## Files

- `STR400.CUB` (33^3, R = G = B on every line), `STR400.T17` (17^3 YUV, 4914 words, raw U = V = 0x00): the shipped pair.
- `gen_str400.py`: the generator (provenance and GUESS list in its docstring); `data/*.csv`: the digitised / public inputs, each with its source header.
- `mono_t17.py` (family copy, md5 9fdaefa3a23f2d08688b33b754e036f7): forces U = V = 0x00 after the module loader; `mono_t17.log` its report.
- `checks_str400.json`, `provenance_str400.json`, `bw_model_str400.json` (spectral model for the J-BW self-check), `preview_str400_curves.png`.
- `judge2_STR400/`: final judge v2.1 JSON, stdout and sheets (scenes, skin, ramps, colorchecker, kernel, distinct, effect).
- Rebuild (WSL Ubuntu-24.04, root): `bash tools/final.sh` (generator defaults -> CUB -> `/root/ml/yuvre_work/harness lut` -> mono_t17.py). Deterministic: same md5s as below.

## Sources

- HARMAN technology "ILFORD PHOTO TECHNICAL INFORMATION: HP5 PLUS", Nov 2018: p.1 wedge spectrogram to tungsten light 2850 K (data/hp5plus_spectrogram_2850K.csv); p.5 characteristic curve, ILFOTEC HC 1+31 (data/hp5plus_char_hc131.csv).
- ILFORD "HP5 Plus" FACT SHEET, July 2004, p.1: ISO 400/27 to daylight.
- HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013: p.2 filter-2 characteristic curve (data/mgfb_classic_char_filter2.csv); p.1 ISO range table (grade 2 = R95; other grades by paper_k = R(target) / 95).
- CIE 018:2019 Table 6 (CIE 1931 2-degree CMFs), CIE 015:2018 daylight S0/S1/S2 (D55 = 5503 K), ColorChecker Classic reflectances (Ohta 1997, colour-science BSD-3; J-BW self-check only): data/cie*.csv, data/colorchecker*.csv.

## Look calls (GUESS)

- spectrogram log-exposure axis read as sensitivity (curve "lin", Planck 2850 K removed)
- paper grade 0 (paper_k 140/95): a softer print for the class
- print anchor "mid" (metered grey at the paper's ISO-range midpoint)
- slope_ref 1.2 in the kernel fit (keeps the toe separation)
- scan levels 7 / 251

Family kernel fit (engineering, judge v2.1 per-pixel-E kernel): E soft limit 60 / 120 codes, no flat E, slope scaling below G' 1.2 over a +-16 code window, negative E limited to 40 codes, monotone closure along +R/+G/+B.

## Claims (testable on the sheets; numbers from the shipped tables)

| claim | test | result |
|---|---|---|
| blue-heavy response: red darker than Press 400 | STR400.red < PRS400.red (0.4276 vs 0.5417) | holds |
| blue lighter than Press 400 | STR400.blue > PRS400.blue (0.5348 vs 0.5138) | holds |
| denser mid-tones than Press 400 (grey 118) | STR400.grey.4 < PRS400.grey.4 (127 vs 143) | holds |
| long straight line: highlight slope above Press 400 | STR400.s_hi > PRS400.s_hi (0.7 vs 0.51) | holds |

Tone of the shipped table (grey input -> output code, inputs 16/32/64/96/118/160/200/230/245): 8/12/44/93/127/176/212/235/245; log-log slopes shadows (32->64) 1.52, mids (96->160) 1.25, highlights (200->245) 0.7.
ColorChecker output L (OKLab): red 0.428, green 0.566, blue 0.535, yellow 0.789, dark skin 0.409, light skin 0.728; spectral weights (R, G, B) (0.139, 0.394, 0.467).

## Judge v2.1 results

| gate | value |
|---|---|
| verdict | PASS, failed none |
| J1 cube luma axes / d2 max / d2 p99.9 | 0.0 / 0.0084 / 0.0077 |
| J2 white / black floor | 251 / 7 |
| J4 ramp reversals (worst) / deepest dip | 0 / {'deepest': -0.0033, 'at': 'sodium'} |
| J8 kernel-added p99.9 / max, kernel max | 0 / 1, 4 |
| JBW raw U/V bytes nonzero / output chroma / grey-ramp UV | {'U_nonzero': 0, 'V_nonzero': 0} / 0 / 0 |
| JBW ColorChecker self-check mean / max |dL| | 0.0098 / 0.0318 (worst yellow) |
| J5 nearest / second (mean of 3 strata, x100) | CRS100 3.14 (parts [2.91, 3.63, 2.87]) / SLK100 4.08 |

Distinctness: nearest Crisp 100 3.14 (same blue-heavy spectral class, different paper and tone); every other film >= 4.0. PASS. The old self-check failure (max 0.083) is gone: 0.0098 / 0.0318.

## Grain row and halation profile (proposal)

- Grain row **B&W 400 x 1.0**: the 2018 sheet publishes no granularity; this is the catalogue assignment for the cubic-grain 400 class (PLAN 5.2), not a measured value.
- Halation **BW (YH_BW) x 1.0**: conventional B&W negative with an anti-halation layer; no published halation figure (GUESS).

Details line: "ISO 400 B&W, cubic grain, dense mids".

## md5

| file | md5 |
|---|---|
| `STR400.CUB` | 03145d3607eeda43bf72d7890f59cf1e |
| `STR400.T17` | bcbc0d66ae12711245ebc551d37c0609 |
| `gen_str400.py` | 64f5151aa50c1586b1972521030a7cd9 |
| `mono_t17.py` | 9fdaefa3a23f2d08688b33b754e036f7 |
| `checks_str400.json` | 9441a379f9161e1e2b6eef86df1f8ee0 |
| `provenance_str400.json` | 8680fafdd44ce06b57a7013f8bab10e3 |
| `bw_model_str400.json` | 305674f391c98cc654437469cb7b84ac |
| `preview_str400_curves.png` | 678e18753b5e40142ff6c60d9d2b994d |
| `tools/final.sh` | 0bd22d3b053d4ef1b7cdedf3daddc9ab |
| `data/cie1931_2deg_10nm.csv` | cd967fbccba6c1ab974844c267d40c88 |
| `data/cie_daylight_S012_10nm.csv` | 113659faec72f1d238369e30fb8a0b4b |
| `data/colorchecker_ohta1997_10nm.csv` | 3a49c6318ce61af97b9345d8a7685de7 |
| `data/hp5plus_char_hc131.csv` | 58178f7813dc8ce3aa9c28b4dbbb15e8 |
| `data/hp5plus_spectrogram_2850K.csv` | 3b5a708583c03197b3cb41fae50ebf15 |
| `data/mgfb_classic_char_filter2.csv` | eff4cede552e1f07a08cd20442e0f599 |
| `judge2_STR400/STR400_colorchecker.png` | 6214772a371d203c46ec2689a6065668 |
| `judge2_STR400/STR400_distinct.png` | 809b95c5276b9080012321aaae60b596 |
| `judge2_STR400/STR400_effect.png` | 540dfd46f485d28dfb3bea2700aa6751 |
| `judge2_STR400/STR400_kernel.png` | 846a4b2931a712e9b1c16778029fffc9 |
| `judge2_STR400/STR400_ramps.png` | a11ebcf34a114f3a1a010c528cf6cf34 |
| `judge2_STR400/STR400_scenes.png` | 4f6a3578baed2f0eb21caca4e9df89fc |
| `judge2_STR400/STR400_skin.png` | 3fbcf54c75e2a1aa1b0710d6a9bda093 |
| `judge2_STR400/judge2_STR400.json` | ea5b992662fb652d66efe8814cb6d174 |
