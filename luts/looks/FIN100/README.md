# FIN100 - Fine 100 (catalogue id 45, type bw) - judge v2.1 PASS

A T-grain panchromatic ISO 100 negative (short toe, long straight line) developed in a D-76-class developer, printed on the published grade-2 paper with the print exposure anchored on the highlights and scanned: open, light mid-tones and clean whites.

Status: **PASS** on judge v2.1 (default kernel, final set of 2026-11-11). Kept in the release. Owner note: distinctness 2.68 vs PRS400 is in the 2.0..3.0 band: tell the owner in the batch report (PLAN 7.1 J5).

## Files

- `FIN100.CUB` (33^3, R = G = B on every line), `FIN100.T17` (17^3 YUV, 4914 words, raw U = V = 0x00): the shipped pair.
- `gen_fin100.py`: the generator (provenance and GUESS list in its docstring); `data/*.csv`: the digitised / public inputs, each with its source header.
- `mono_t17.py` (family copy, md5 9fdaefa3a23f2d08688b33b754e036f7): forces U = V = 0x00 after the module loader; `mono_t17.log` its report.
- `checks_fin100.json`, `provenance_fin100.json`, `bw_model_fin100.json` (spectral model for the J-BW self-check), `preview_fin100_curves.png`.
- `judge2_FIN100/`: final judge v2.1 JSON, stdout and sheets (scenes, skin, ramps, colorchecker, kernel, distinct, effect).
- Rebuild (WSL Ubuntu-24.04, root): `bash tools/final.sh` (generator defaults -> CUB -> `/root/ml/yuvre_work/harness lut` -> mono_t17.py). Deterministic: same md5s as below.

## Sources

- Kodak Alaris F-4016 "KODAK PROFESSIONAL T-MAX 100 Film", June 2018 (Revised 6-18), p.8: characteristic curves D-76 small tank 6 / 7.5 / 10 min (data/f4016_char_d76_small_tank.csv), spectral sensitivity D = Dmin + 1.0 (data/f4016_spectral_sensitivity.csv), image structure: diffuse rms granularity 8.
- HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013: p.2 filter-2 characteristic curve (data/mgfb_classic_char_filter2.csv); p.1 ISO range table (grade 2 = R95; other grades by paper_k = R(target) / 95).
- CIE 018:2019 Table 6 (CIE 1931 2-degree CMFs), CIE 015:2018 daylight S0/S1/S2 (D55 = 5503 K), ColorChecker Classic reflectances (Ohta 1997, colour-science BSD-3; J-BW self-check only): data/cie*.csv, data/colorchecker*.csv.

## Look calls (GUESS)

- development 6.5 min (linear in time between the 6 and 7.5 min vector-digitised curves)
- print anchor "highlight" (the JPEG white prints at the paper's lightest tone)
- scan levels 7 / 251

Family kernel fit (engineering, judge v2.1 per-pixel-E kernel): E soft limit 60 / 120 codes, no flat E, slope scaling below G' 0.8 over a +-16 code window, negative E limited to 40 codes, monotone closure along +R/+G/+B.

## Claims (testable on the sheets; numbers from the shipped tables)

| claim | test | result |
|---|---|---|
| open mid-tones: grey 118 lighter than Press 400 | FIN100.grey.4 > PRS400.grey.4 (157 vs 143) | holds |
| short toe: steepest shadow slope of the family (next: Chromo 400) | FIN100.s_sh > CHM400.s_sh (2.03 vs 1.78) | holds |
| long straight line: mid slope >= 0.9 | FIN100.s_mid >= 0.9 (0.95 vs 0.9) | holds |
| long straight line: mid slope <= 1.1 | FIN100.s_mid <= 1.1 (0.95 vs 1.1) | holds |
| clean highlights: input 230 -> >= 244 | FIN100.grey.7 >= 244 (244 vs 244.0) | holds |

Tone of the shipped table (grey input -> output code, inputs 16/32/64/96/118/160/200/230/245): 7/12/62/126/157/202/230/244/248; log-log slopes shadows (32->64) 2.03, mids (96->160) 0.95, highlights (200->245) 0.38.
ColorChecker output L (OKLab): red 0.555, green 0.680, blue 0.643, yellow 0.873, dark skin 0.521, light skin 0.814; spectral weights (R, G, B) (0.16, 0.431, 0.409).

## Judge v2.1 results

| gate | value |
|---|---|
| verdict | PASS, failed none |
| J1 cube luma axes / d2 max / d2 p99.9 | 0.0 / 0.0289 / 0.0268 |
| J2 white / black floor | 251 / 7 |
| J4 ramp reversals (worst) / deepest dip | 0 / {'deepest': -0.0032, 'at': 'sodium'} |
| J8 kernel-added p99.9 / max, kernel max | 1 / 1, 4 |
| JBW raw U/V bytes nonzero / output chroma / grey-ramp UV | {'U_nonzero': 0, 'V_nonzero': 0} / 0 / 0 |
| JBW ColorChecker self-check mean / max |dL| | 0.0039 / 0.0148 (worst orange yellow) |
| J5 nearest / second (mean of 3 strata, x100) | PRS400 2.68 (parts [2.71, 3.46, 1.89]) / CLS125 3.32 |

Distinctness: nearest Press 400 **2.68 (2..3 warning band: owner note)**; the two share the near-neutral panchromatic response and the grade-2 paper; Fine 100 is lighter in the mids (157 vs 143 at grey 118) and has the shorter toe. Second CLS125 3.32. PASS with warning.

## Grain row and halation profile (proposal)

- Grain row **B&W 100T x 1.0**: F-4016 p.8 diffuse rms granularity 8 (D-76), the basis of the T-grain 100 row.
- Halation **BW (YH_BW) x 1.0** (GUESS, standard anti-halation layer).

Details line: "ISO 100 B&W, T-grain, open mids".

## md5

| file | md5 |
|---|---|
| `FIN100.CUB` | 96c60e4d413953dbca155714e08efd6b |
| `FIN100.T17` | adf2257e142b22f19495366a30cf7c26 |
| `gen_fin100.py` | 10a5a3defae90fc155c6486b32afbdc5 |
| `mono_t17.py` | 9fdaefa3a23f2d08688b33b754e036f7 |
| `checks_fin100.json` | ac85687fdfec03c77cf96770b1133c6a |
| `provenance_fin100.json` | 4fcaed1cf9be09b18eff1be8a855dd55 |
| `bw_model_fin100.json` | 845d130427c02862c3f14728ad3bd9d1 |
| `preview_fin100_curves.png` | 64142ae993c299fb8e4f616594944613 |
| `tools/final.sh` | 46ee592ddabe80090bbf0ee9241232bf |
| `data/cie1931_2deg_10nm.csv` | cd967fbccba6c1ab974844c267d40c88 |
| `data/cie_daylight_S012_10nm.csv` | 113659faec72f1d238369e30fb8a0b4b |
| `data/colorchecker_ohta1997_10nm.csv` | 3a49c6318ce61af97b9345d8a7685de7 |
| `data/f4016_char_d76_small_tank.csv` | 6a3eabade3550a3fbffe184c676ae12d |
| `data/f4016_char_tmax_small_tank.csv` | 5b94ff75bf9edc25c0acbe16c0f48785 |
| `data/f4016_raw_paths.txt` | 76f3bd89de4615591238b985f8c5c46f |
| `data/f4016_spectral_sensitivity.csv` | c6216e1053847c5be7a3d6254b72ff4e |
| `data/mgfb_classic_char_filter2.csv` | eff4cede552e1f07a08cd20442e0f599 |
| `judge2_FIN100/FIN100_colorchecker.png` | 5b203266c630c089e559f202c0608e8c |
| `judge2_FIN100/FIN100_distinct.png` | aec05dac9af9274b8472e00e54be124b |
| `judge2_FIN100/FIN100_effect.png` | d30c29fa0f4df08e74eb1e3a3b276ee2 |
| `judge2_FIN100/FIN100_kernel.png` | d86536d1dc4214e46706b29e46524ce2 |
| `judge2_FIN100/FIN100_ramps.png` | 6de932326d4d0710869bd88132ec8d82 |
| `judge2_FIN100/FIN100_scenes.png` | 78fac30aa79b4def917c1e2e8d2c4ac7 |
| `judge2_FIN100/FIN100_skin.png` | 2263c2f13a40d63b889dbfc7bc1b7337 |
| `judge2_FIN100/judge2_FIN100.json` | 9704b9c040be4135fd9206b3459212ed |
