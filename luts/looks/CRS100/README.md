# CRS100 - Crisp 100 (catalogue id 53, type bw) - judge v2.1 PASS

A core-shell tabular-grain panchromatic ISO 100 negative in a stock fine-grain developer, printed on the published grade-2 paper (mid-anchored) and scanned: crisper and firmer than Fine 100 (higher mid contrast, deeper shadows), brighter highlights than Street 400, a blue-heavy spectral response.

Status: **PASS** on judge v2.1 (default kernel, final set of 2026-11-11). Kept in the release.

## Files

- `CRS100.CUB` (33^3, R = G = B on every line), `CRS100.T17` (17^3 YUV, 4914 words, raw U = V = 0x00): the shipped pair.
- `gen_crs100.py`: the generator (provenance and GUESS list in its docstring); `data/*.csv`: the digitised / public inputs, each with its source header.
- `mono_t17.py` (family copy, md5 9fdaefa3a23f2d08688b33b754e036f7): forces U = V = 0x00 after the module loader; `mono_t17.log` its report.
- `checks_crs100.json`, `provenance_crs100.json`, `bw_model_crs100.json` (spectral model for the J-BW self-check), `preview_crs100_curves.png`.
- `judge2_CRS100/`: final judge v2.1 JSON, stdout and sheets (scenes, skin, ramps, colorchecker, kernel, distinct, effect).
- Rebuild (WSL Ubuntu-24.04, root): `bash tools/final.sh` (generator defaults -> CUB -> `/root/ml/yuvre_work/harness lut` -> mono_t17.py). Deterministic: same md5s as below.

## Sources

- ILFORD (HARMAN technology) "100 DELTA PROFESSIONAL" technical information, Sept 2002 (figures unchanged in the Nov 2018 edition): p.4 characteristic curve ID-11 stock 8 1/2 min 20 C (data/delta100_char_id11_8.5min.csv); p.1 wedge spectrogram 2850 K (data/delta100_spectrogram_2850K.csv).
- HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013: p.2 filter-2 characteristic curve (data/mgfb_classic_char_filter2.csv); p.1 ISO range table (grade 2 = R95; other grades by paper_k = R(target) / 95).
- CIE 018:2019 Table 6 (CIE 1931 2-degree CMFs), CIE 015:2018 daylight S0/S1/S2 (D55 = 5503 K), ColorChecker Classic reflectances (Ohta 1997, colour-science BSD-3; J-BW self-check only): data/cie*.csv, data/colorchecker*.csv.

## Look calls (GUESS)

- spectrogram log-exposure axis read as sensitivity (curve "lin")
- paper_k 1.0 (plain grade 2), print anchor "mid", levels clip
- scan levels 7 / 251

Family kernel fit (engineering, judge v2.1 per-pixel-E kernel): E soft limit 60 / 120 codes, no flat E, slope scaling below G' 0.8 over a +-16 code window, negative E limited to 40 codes, monotone closure along +R/+G/+B.

## Claims (testable on the sheets; numbers from the shipped tables)

| claim | test | result |
|---|---|---|
| crisper than Fine 100: mid contrast above it | CRS100.s_mid > FIN100.s_mid (1.25 vs 0.95) | holds |
| deep blacks: input 64 darker than Fine 100 | CRS100.grey.2 < FIN100.grey.2 (40 vs 62) | holds |
| brighter highlights than Street 400 (input 230) | CRS100.grey.7 > STR400.grey.7 (244 vs 235) | holds |
| blue-heavy: red darker than Fine 100 | CRS100.red < FIN100.red (0.4459 vs 0.5555) | holds |

Tone of the shipped table (grey input -> output code, inputs 16/32/64/96/118/160/200/230/245): 7/9/40/101/137/191/228/244/249; log-log slopes shadows (32->64) 1.61, mids (96->160) 1.25, highlights (200->245) 0.44.
ColorChecker output L (OKLab): red 0.446, green 0.597, blue 0.586, yellow 0.817, dark skin 0.416, light skin 0.764; spectral weights (R, G, B) (0.138, 0.395, 0.468).

## Judge v2.1 results

| gate | value |
|---|---|
| verdict | PASS, failed none |
| J1 cube luma axes / d2 max / d2 p99.9 | 0.0 / 0.022 / 0.0215 |
| J2 white / black floor | 251 / 7 |
| J4 ramp reversals (worst) / deepest dip | 2 / {'deepest': -0.0073, 'at': 'sodium'} |
| J8 kernel-added p99.9 / max, kernel max | 0 / 0, 4 |
| JBW raw U/V bytes nonzero / output chroma / grey-ramp UV | {'U_nonzero': 0, 'V_nonzero': 0} / 0 / 0 |
| JBW ColorChecker self-check mean / max |dL| | 0.0046 / 0.0143 (worst orange yellow) |
| J5 nearest / second (mean of 3 strata, x100) | CLS125 3.04 (parts [2.26, 3.0, 3.86]) / STR400 3.14 |

Distinctness: nearest in the final set CLS125 3.04 (a film another agent added on 2026-11-11 20:57); among the films of this batch Street 400 3.14. PASS. The old self-check failure (0.031 / 0.094) is gone: 0.0046 / 0.0143.

## Grain row and halation profile (proposal)

- Grain row **B&W 100T x 1.05**: core-shell tabular 100, described as very fine but slightly crisper than the T-grain 100 class; the sheet gives no RMS value (scale is a GUESS).
- Halation **BW (YH_BW) x 1.0** (GUESS, standard anti-halation layer).

Details line: "ISO 100 B&W, tabular grain, crisp".

## md5

| file | md5 |
|---|---|
| `CRS100.CUB` | 27f0e2392423184d4610369a654296c4 |
| `CRS100.T17` | d0a70bff02938fde10e485b8982c0110 |
| `gen_crs100.py` | ba716ed281ff40d2dc9771efe287d823 |
| `mono_t17.py` | 9fdaefa3a23f2d08688b33b754e036f7 |
| `checks_crs100.json` | 5023533882da0f3d003e6cc67ca359d4 |
| `provenance_crs100.json` | 0bdd7c56b1dac27cc0557a859966dce2 |
| `bw_model_crs100.json` | 61651a3a4aab787e8acf884441d90704 |
| `preview_crs100_curves.png` | ab6aa4d72ac6fa586f8bfd7db76aa641 |
| `tools/final.sh` | 5aaebf4ab8fce3f26b8168fbf306c823 |
| `data/cie1931_2deg_10nm.csv` | cd967fbccba6c1ab974844c267d40c88 |
| `data/cie_daylight_S012_10nm.csv` | 113659faec72f1d238369e30fb8a0b4b |
| `data/colorchecker_ohta1997_10nm.csv` | 3a49c6318ce61af97b9345d8a7685de7 |
| `data/delta100_char_id11_8.5min.csv` | 0b11fb2a315dbea1bb86da34886651ff |
| `data/delta100_spectrogram_2850K.csv` | 3ee373020b366cc67a98834c579e976c |
| `data/mgfb_classic_char_filter2.csv` | eff4cede552e1f07a08cd20442e0f599 |
| `judge2_CRS100/CRS100_colorchecker.png` | 5e46eea991569425eda20708531106c8 |
| `judge2_CRS100/CRS100_distinct.png` | a135d323e9ac04d0767f5f917196d45d |
| `judge2_CRS100/CRS100_effect.png` | fbf481c5001700551414ed1d1e83bfbb |
| `judge2_CRS100/CRS100_kernel.png` | aae2b3ac9424d350831c32dab7e365a2 |
| `judge2_CRS100/CRS100_ramps.png` | 703da8b2fc3b84ac4d1c29cdd15a1acb |
| `judge2_CRS100/CRS100_scenes.png` | 4ba85e799bc8cabbd5b5f0f6a1180772 |
| `judge2_CRS100/CRS100_skin.png` | 0e8503b3b492146a7694c0b1cd64a42f |
| `judge2_CRS100/judge2_CRS100.json` | be56e74662a33971f5ec85ea8c236490 |
