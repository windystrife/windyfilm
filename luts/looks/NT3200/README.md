# NT3200 - Night 3200 (catalogue id 46, type bw) - judge v2.1 PASS

A T-grain multi-speed panchromatic negative rated and push-developed at EI 3200 (the maker's EI 3200 starting-point time in a fine-grain T-grain developer), printed on the published grade-2 paper with the metered grey at net density 0.95 and scanned: blocked shadows, steep pushed mid-tones.

Status: **PASS** on judge v2.1 (default kernel, final set of 2026-11-11). Kept in the release.

## Files

- `NT3200.CUB` (33^3, R = G = B on every line), `NT3200.T17` (17^3 YUV, 4914 words, raw U = V = 0x00): the shipped pair.
- `gen_nt3200.py`: the generator (provenance and GUESS list in its docstring); `data/*.csv`: the digitised / public inputs, each with its source header.
- `mono_t17.py` (family copy, md5 9fdaefa3a23f2d08688b33b754e036f7): forces U = V = 0x00 after the module loader; `mono_t17.log` its report.
- `checks_nt3200.json`, `provenance_nt3200.json`, `bw_model_nt3200.json` (spectral model for the J-BW self-check), `preview_nt3200_curves.png`.
- `judge2_NT3200/`: final judge v2.1 JSON, stdout and sheets (scenes, skin, ramps, colorchecker, kernel, distinct, effect).
- Rebuild (WSL Ubuntu-24.04, root): `bash tools/final.sh` (generator defaults -> CUB -> `/root/ml/yuvre_work/harness lut` -> mono_t17.py). Deterministic: same md5s as below.

## Sources

- Kodak Alaris F-4001 "KODAK PROFESSIONAL T-MAX P3200 Black & White Negative Film", March 2018 (Revised 03-18): characteristic curves in T-MAX developer (data/f4001_tmax_dev_char.csv), contrast index vs time (data/f4001_contrast_index_tmax.csv), spectral sensitivity (data/f4001_spectral_sensitivity.csv), p.7 diffuse rms granularity 18 (D-76); an older edition's curve in data/f4016_2007_p3200_tmax13.csv (see its header).
- HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013: p.2 filter-2 characteristic curve (data/mgfb_classic_char_filter2.csv); p.1 ISO range table (grade 2 = R95; other grades by paper_k = R(target) / 95).
- CIE 018:2019 Table 6 (CIE 1931 2-degree CMFs), CIE 015:2018 daylight S0/S1/S2 (D55 = 5503 K), ColorChecker Classic reflectances (Ohta 1997, colour-science BSD-3; J-BW self-check only): data/cie*.csv, data/colorchecker*.csv.

## Look calls (GUESS)

- development 12 min (EI 3200 starting point)
- spectral curve "D0.3"
- print anchor "grey": metered grey at net density 0.95 (a dark, pushed print)
- scan levels 7 / 251

Family kernel fit (engineering, judge v2.1 per-pixel-E kernel): E soft limit 60 / 120 codes, no flat E, slope scaling below G' 0.8 over a +-16 code window, negative E limited to 40 codes, monotone closure along +R/+G/+B.

## Claims (testable on the sheets; numbers from the shipped tables)

| claim | test | result |
|---|---|---|
| blocked shadows: input 64 darkest of the family (next: Sharp 50) | NT3200.grey.2 < SHP50.grey.2 (14 vs 19) | holds |
| pushed contrast: steepest mid slope (next: Sharp 50) | NT3200.s_mid > SHP50.s_mid (2.09 vs 1.66) | holds |
| dense mids: dark skin darkest of the family (next: Sharp 50) | NT3200.dskin < SHP50.dskin (0.2768 vs 0.3677) | holds |

Tone of the shipped table (grey input -> output code, inputs 16/32/64/96/118/160/200/230/245): 7/7/14/54/97/164/210/236/246; log-log slopes shadows (32->64) 0.53, mids (96->160) 2.09, highlights (200->245) 0.77.
ColorChecker output L (OKLab): red 0.345, green 0.471, blue 0.333, yellow 0.786, dark skin 0.277, light skin 0.693; spectral weights (R, G, B) (0.206, 0.445, 0.349).

## Judge v2.1 results

| gate | value |
|---|---|
| verdict | PASS, failed none |
| J1 cube luma axes / d2 max / d2 p99.9 | 0.0 / 0.0173 / 0.0168 |
| J2 white / black floor | 251 / 7 |
| J4 ramp reversals (worst) / deepest dip | 2 / {'deepest': -0.005, 'at': 'sodium'} |
| J8 kernel-added p99.9 / max, kernel max | 1 / 1, 5 |
| JBW raw U/V bytes nonzero / output chroma / grey-ramp UV | {'U_nonzero': 0, 'V_nonzero': 0} / 0 / 0 |
| JBW ColorChecker self-check mean / max |dL| | 0.0044 / 0.0339 (worst blue) |
| J5 nearest / second (mean of 3 strata, x100) | SHP50 5.94 (parts [5.25, 6.37, 6.19]) / STR400 6.41 |

Distinctness: nearest Sharp 50 5.94, second Street 400 6.41. PASS.

## Grain row and halation profile (proposal)

- Grain: new row **B&W 3200**: F-4001 p.7 rms 18 (D-76, normal development) = 1.06 x B&W 400; the sheet notes more graininess at EI 3200 (p.2), so **1.4 x B&W 400** is proposed for the push (factor GUESS).
- Halation **BW (YH_BW) x 1.0** (GUESS).

Details line: "EI 3200 B&W push, blocked shadows".

## md5

| file | md5 |
|---|---|
| `NT3200.CUB` | f2366d1a4a4ac6105b20669e0a314719 |
| `NT3200.T17` | 60ebf23fece12cb6e5bf8fc7a787aa09 |
| `gen_nt3200.py` | 84e13258c8dd049b360f5b805399e57f |
| `mono_t17.py` | 9fdaefa3a23f2d08688b33b754e036f7 |
| `checks_nt3200.json` | 9787df12e637c4eb995172a97b6af9ad |
| `provenance_nt3200.json` | 696fd16ef3fb9ab1f8c563571ea303cb |
| `bw_model_nt3200.json` | 3c217efbcf5f6c384c4cfba2a7d8a0ff |
| `preview_nt3200_curves.png` | 37ea306f171eded4a25c64781df1a129 |
| `tools/final.sh` | be7ed4de73d533f9c546b4bc0e9d6fc8 |
| `data/cie1931_2deg_10nm.csv` | cd967fbccba6c1ab974844c267d40c88 |
| `data/cie_daylight_S012_10nm.csv` | 113659faec72f1d238369e30fb8a0b4b |
| `data/colorchecker_ohta1997_10nm.csv` | 3a49c6318ce61af97b9345d8a7685de7 |
| `data/f4001_contrast_index_tmax.csv` | cd8ac5a135289e88a28a3ba9631b113e |
| `data/f4001_spectral_sensitivity.csv` | 18536ffd986c14ddfa1495097e9c71c7 |
| `data/f4001_tmax_dev_char.csv` | 90535a4dd87cb27d08cb9dae89e93074 |
| `data/f4016_2007_p3200_tmax13.csv` | 92a248f4292025571d0fe61c99df630a |
| `data/mgfb_classic_char_filter2.csv` | eff4cede552e1f07a08cd20442e0f599 |
| `judge2_NT3200/NT3200_colorchecker.png` | c1836cd0e1f4ebeff9d11afa6fcb1b70 |
| `judge2_NT3200/NT3200_distinct.png` | 7fb4d5b0490de9283905f697cb7e902c |
| `judge2_NT3200/NT3200_effect.png` | 00f1fd2fb0c317d70e34fda7ed9ed557 |
| `judge2_NT3200/NT3200_kernel.png` | 8fbefbf674797c77e710cb684bb6908c |
| `judge2_NT3200/NT3200_ramps.png` | 7e8e791b96e25b64bc1c56fb3b8167fb |
| `judge2_NT3200/NT3200_scenes.png` | 6695d6a9630ead82250ef2a1450c22bb |
| `judge2_NT3200/NT3200_skin.png` | 52fd0abcf6336332619d3314f2b550fe |
| `judge2_NT3200/judge2_NT3200.json` | b4bea632c5904db898e366716e8509a1 |
