# CHM400 - Chromo 400 (catalogue id 47, type bw) - judge v2.1 PASS

A chromogenic (C-41, dye image) black-and-white ISO 400 negative with a long, gentle shoulder, printed on a very soft grade-00 print and scanned: the softest tonality of the family, open shadows and compressed highlights.

Status: **PASS** on judge v2.1 (default kernel, final set of 2026-11-11). Kept in the release.

## Files

- `CHM400.CUB` (33^3, R = G = B on every line), `CHM400.T17` (17^3 YUV, 4914 words, raw U = V = 0x00): the shipped pair.
- `gen_chm400.py`: the generator (provenance and GUESS list in its docstring); `data/*.csv`: the digitised / public inputs, each with its source header.
- `mono_t17.py` (family copy, md5 9fdaefa3a23f2d08688b33b754e036f7): forces U = V = 0x00 after the module loader; `mono_t17.log` its report.
- `checks_chm400.json`, `provenance_chm400.json`, `bw_model_chm400.json` (spectral model for the J-BW self-check), `preview_chm400_curves.png`.
- `judge2_CHM400/`: final judge v2.1 JSON, stdout and sheets (scenes, skin, ramps, colorchecker, kernel, distinct, effect).
- Rebuild (WSL Ubuntu-24.04, root): `bash tools/final.sh` (generator defaults -> CUB -> `/root/ml/yuvre_work/harness lut` -> mono_t17.py). Deterministic: same md5s as below.

## Sources

- HARMAN technology "TECHNICAL INFORMATION - XP2 SUPER", June 2010, p.2: wedge spectrogram to tungsten light (data/xp2s_spectral_wedge_2856K.csv) and characteristic curve, C-41 (data/xp2s_char_c41.csv; traced vertices data/xp2s_vertices_A.json); "XP2 SUPER Information", Nov 2018 (current).
- Public table (cacreeks.com "Film Characteristics Table", undated, accuracy not guaranteed): RMS "9?" (grain proposal only).
- HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013: p.2 filter-2 characteristic curve (data/mgfb_classic_char_filter2.csv); p.1 ISO range table (grade 2 = R95; other grades by paper_k = R(target) / 95).
- CIE 018:2019 Table 6 (CIE 1931 2-degree CMFs), CIE 015:2018 daylight S0/S1/S2 (D55 = 5503 K), ColorChecker Classic reflectances (Ohta 1997, colour-science BSD-3; J-BW self-check only): data/cie*.csv, data/colorchecker*.csv.

## Look calls (GUESS)

- spectral curve "A" (see docstring)
- paper grade 00 (the softest grade of the paper's ISO range table)
- print anchor "mid"
- scan levels 7 / 251

Family kernel fit (engineering, judge v2.1 per-pixel-E kernel): E soft limit 60 / 120 codes, no flat E, slope scaling below G' 0.8 over a +-16 code window, negative E limited to 40 codes, monotone closure along +R/+G/+B.

## Claims (testable on the sheets; numbers from the shipped tables)

| claim | test | result |
|---|---|---|
| the softest shoulder of the family: lowest highlight slope (next: Fine 100) | CHM400.s_hi < FIN100.s_hi (0.36 vs 0.38) | holds |
| lowest mid contrast of the family (next: Vintage 100) | CHM400.s_mid < VTG100.s_mid (0.7 vs 0.83) | holds |
| open shadows: input 64 lighter than every other film (next: Vintage 100) | CHM400.grey.2 > VTG100.grey.2 (91 vs 79) | holds |

Tone of the shipped table (grey input -> output code, inputs 16/32/64/96/118/160/200/230/245): 8/27/91/147/173/208/230/243/248; log-log slopes shadows (32->64) 1.78, mids (96->160) 0.7, highlights (200->245) 0.36.
ColorChecker output L (OKLab): red 0.620, green 0.725, blue 0.719, yellow 0.879, dark skin 0.600, light skin 0.836; spectral weights (R, G, B) (0.142, 0.395, 0.463).

## Judge v2.1 results

| gate | value |
|---|---|
| verdict | PASS, failed none |
| J1 cube luma axes / d2 max / d2 p99.9 | 0.0 / 0.029 / 0.0179 |
| J2 white / black floor | 251 / 7 |
| J4 ramp reversals (worst) / deepest dip | 0 / {'deepest': -0.003, 'at': 'sodium'} |
| J8 kernel-added p99.9 / max, kernel max | 0 / 2, 5 |
| JBW raw U/V bytes nonzero / output chroma / grey-ramp UV | {'U_nonzero': 0, 'V_nonzero': 0} / 0 / 0 |
| JBW ColorChecker self-check mean / max |dL| | 0.0055 / 0.0196 (worst yellow) |
| J5 nearest / second (mean of 3 strata, x100) | VTG100 4.17 (parts [4.32, 5.02, 3.17]) / FIN100 4.26 |

Distinctness: nearest Vintage 100 4.17, second Fine 100 4.26. PASS.

## Grain row and halation profile (proposal)

- Grain: new row **Chromo 400** (luma-only dye-cloud grain): rms about 9 from the public table (unconfirmed) = 9/17 = **0.53 x the B&W 400 row** (rms 17); the sheet says the dye clouds get finer with more exposure, so a shadow-weighted strength is proposed (GUESS).
- Halation **BW (YH_BW) x 1.0** (GUESS; a C-41 B&W film has a standard anti-halation layer).

Details line: "ISO 400 chromogenic B&W, soft".

## md5

| file | md5 |
|---|---|
| `CHM400.CUB` | a4d85ef5b00fcf65b93692959dddda45 |
| `CHM400.T17` | fe39f261be63a0d1cf2dbc9e74b7e99d |
| `gen_chm400.py` | 79f4b7d4996fbdcb67327d797f1b6121 |
| `mono_t17.py` | 9fdaefa3a23f2d08688b33b754e036f7 |
| `checks_chm400.json` | fc44fdbbe3d556c072f9a87acb40d850 |
| `provenance_chm400.json` | 72654e44d601a2287d42f090cfdcfe59 |
| `bw_model_chm400.json` | 9d00774ec3904ea179f08a7ac6248567 |
| `preview_chm400_curves.png` | 53bbe412e40d37b661f8e26a8b42150d |
| `tools/final.sh` | 1dade49696beccfdf21d70605f3988ae |
| `data/cie1931_2deg_10nm.csv` | cd967fbccba6c1ab974844c267d40c88 |
| `data/cie_daylight_S012_10nm.csv` | 113659faec72f1d238369e30fb8a0b4b |
| `data/colorchecker_ohta1997_10nm.csv` | 3a49c6318ce61af97b9345d8a7685de7 |
| `data/mgfb_classic_char_filter2.csv` | eff4cede552e1f07a08cd20442e0f599 |
| `data/xp2s_char_c41.csv` | 5d770a64cc37da1c552a2bfe7b9e36f1 |
| `data/xp2s_spectral_wedge_2856K.csv` | eecd62f47f375cbcccfbe7f70d6b7261 |
| `data/xp2s_vertices_A.json` | 883cdb263f5755b6331c73747dc7120c |
| `judge2_CHM400/CHM400_colorchecker.png` | 012a1a1291b0c6ecd1a0e0f62c390253 |
| `judge2_CHM400/CHM400_distinct.png` | abd821b1d468e036c6a6f62f74a0ba81 |
| `judge2_CHM400/CHM400_effect.png` | 7dd3cddf3ca7ff280951810c32793180 |
| `judge2_CHM400/CHM400_kernel.png` | 7e0f2a2d0b4869b370bb1e3e00b5cfe1 |
| `judge2_CHM400/CHM400_ramps.png` | ccc0c417a7b1b3f0ce719b65c9566160 |
| `judge2_CHM400/CHM400_scenes.png` | 4918644856177f50380877a439604943 |
| `judge2_CHM400/CHM400_skin.png` | 1bba9dcde4942bd657aa760a4fc60387 |
| `judge2_CHM400/judge2_CHM400.json` | 5c363e8d3e3d67002bb6401ddf1a0a7a |
