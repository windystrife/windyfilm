# VTG100 - Vintage 100 (catalogue id 56, type bw) - judge v2.1 PASS

A traditional cubic-grain panchromatic ISO 100 negative with a weak anti-halation layer, rendered as an old-school low-contrast print: raised fog, long soft toe, a softer paper (paper_k 110/95, grade 1), metered grey at net density 0.72, scanned. Lifted blacks and gentle mid-tones; the glow around highlights belongs to the halation profile (BW_WEAK), not to this table.

Status: **PASS** on judge v2.1 (default kernel, final set of 2026-11-11). Kept in the release.

## Files

- `VTG100.CUB` (33^3, R = G = B on every line), `VTG100.T17` (17^3 YUV, 4914 words, raw U = V = 0x00): the shipped pair.
- `gen_vtg100.py`: the generator (provenance and GUESS list in its docstring); `data/*.csv`: the digitised / public inputs, each with its source header.
- `mono_t17.py` (family copy, md5 9fdaefa3a23f2d08688b33b754e036f7): forces U = V = 0x00 after the module loader; `mono_t17.log` its report.
- `checks_vtg100.json`, `provenance_vtg100.json`, `bw_model_vtg100.json` (spectral model for the J-BW self-check), `preview_vtg100_curves.png`.
- `judge2_VTG100/`: final judge v2.1 JSON, stdout and sheets (scenes, skin, ramps, colorchecker, kernel, distinct, effect).
- Rebuild (WSL Ubuntu-24.04, root): `bash tools/final.sh` (generator defaults -> CUB -> `/root/ml/yuvre_work/harness lut` -> mono_t17.py). Deterministic: same md5s as below.

## Sources

- PARAMETRIC (clean room, no downloads in this batch): the maker's sheet was not digitised. Spectral sensitivity = blue Gaussian + green-red logistic plateau + red logistic cut-off; negative = fog + gamma x softplus toe, placed by the ISO 6 speed point at box speed. Every value is a LOOK CALL set from the published descriptions summarised in catalogue/inventory.md (B&W list). Sheet named there: the maker's traditional ISO 100 data sheet; documented traits: traditional cubic grain, high contrast in sunlight, real speed often EI 50-80, a weak anti-halation layer that gives visible halos.
- HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013: p.2 filter-2 characteristic curve (data/mgfb_classic_char_filter2.csv); p.1 ISO range table (grade 2 = R95; other grades by paper_k = R(target) / 95).
- CIE 018:2019 Table 6 (CIE 1931 2-degree CMFs), CIE 015:2018 daylight S0/S1/S2 (D55 = 5503 K), ColorChecker Classic reflectances (Ohta 1997, colour-science BSD-3; J-BW self-check only): data/cie*.csv, data/colorchecker*.csv.

## Look calls (GUESS)

- sensitivity: blue 440 nm / 35 nm / 0.9, plateau rise 495 nm / 12 nm, plateau 1.0, red cut 672 nm / 8 nm (an extended red, the lightest reds of the family)
- negative: ISO 100 box speed (the described EI 50-80 is NOT modelled), fog 0.22, gamma 0.50, toe softness 0.35 log H: a low-contrast vintage rendering; the described high contrast in sunlight is NOT modelled (look call for the catalogue slot)
- paper_k 110/95 (grade 1), print anchor "grey" 0.72
- scan levels 7 / 251

Family kernel fit (engineering, judge v2.1 per-pixel-E kernel): E soft limit 60 / 120 codes, no flat E, slope scaling below G' 0.8 over a +-16 code window, negative E limited to 40 codes, monotone closure along +R/+G/+B.

## Claims (testable on the sheets; numbers from the shipped tables)

| claim | test | result |
|---|---|---|
| lifted blacks: input 32 lightest of the family (next: Chromo 400) | VTG100.grey.1 > CHM400.grey.1 (34 vs 27) | holds |
| low contrast: mid slope below every panchromatic silver film (next: Fine 100) | VTG100.s_mid < FIN100.s_mid (0.83 vs 0.95) | holds |
| extended red: red patch lightest of the family (next: Chromo 400) | VTG100.red > CHM400.red (0.6268 vs 0.6201) | holds |

Tone of the shipped table (grey input -> output code, inputs 16/32/64/96/118/160/200/230/245): 17/34/79/126/152/191/220/238/246; log-log slopes shadows (32->64) 1.26, mids (96->160) 0.83, highlights (200->245) 0.56.
ColorChecker output L (OKLab): red 0.627, green 0.676, blue 0.580, yellow 0.864, dark skin 0.562, light skin 0.798; spectral weights (R, G, B) (0.25, 0.475, 0.276).

## Judge v2.1 results

| gate | value |
|---|---|
| verdict | PASS, failed none |
| J1 cube luma axes / d2 max / d2 p99.9 | 0.0 / 0.0051 / 0.0035 |
| J2 white / black floor | 251 / 7 |
| J4 ramp reversals (worst) / deepest dip | 0 / {'deepest': -0.0014, 'at': 'lips'} |
| J8 kernel-added p99.9 / max, kernel max | 1 / 1, 3 |
| JBW raw U/V bytes nonzero / output chroma / grey-ramp UV | {'U_nonzero': 0, 'V_nonzero': 0} / 0 / 0 |
| JBW ColorChecker self-check mean / max |dL| | 0.0022 / 0.0063 (worst yellow green) |
| J5 nearest / second (mean of 3 strata, x100) | CHM400 4.17 (parts [4.32, 5.02, 3.17]) / FIN100 4.22 |

Distinctness: nearest Chromo 400 4.17, second Fine 100 4.22. PASS.

## Grain row and halation profile (proposal)

- Grain: new row **B&W 100C** (traditional cubic 100): no RMS from the maker found offline; **1.3 x B&W 100T** (about rms 10-11, between the T-grain 100 row, rms 8, and the cubic 400 row, rms 17) is proposed (GUESS).
- Halation: new profile **BW_WEAK** = the YH_BW shape at **scale 2.5, radius x 1.5** (GUESS: the maker describes a weak anti-halation layer and visible halos around bright highlights; no numeric halation or MTF data are published). In the effects code this is the stronger YH_BW variant (fx_hvar) named in the inventory.

Details line: "ISO 100 B&W, vintage, low contrast".

## md5

| file | md5 |
|---|---|
| `VTG100.CUB` | 837dd7dcc4b5c10b0c7b37996b497e54 |
| `VTG100.T17` | d9300092f9507e5c38fcd91dfd7b6fed |
| `gen_vtg100.py` | e728c3a9b0fef68c71fca1918f85aac4 |
| `mono_t17.py` | 9fdaefa3a23f2d08688b33b754e036f7 |
| `checks_vtg100.json` | 4443cce48c8ad91643f18d9d878a7d7d |
| `provenance_vtg100.json` | 1b72420f9f9e8fa33859261d904a35b1 |
| `bw_model_vtg100.json` | 550adfe9a0f125aab5097d936efe5742 |
| `preview_vtg100_curves.png` | 337b85381debbad89c0ae8de25053e16 |
| `tools/final.sh` | 89163c0ed853baced58b4ddd6099050e |
| `data/cie1931_2deg_10nm.csv` | cd967fbccba6c1ab974844c267d40c88 |
| `data/cie_daylight_S012_10nm.csv` | 113659faec72f1d238369e30fb8a0b4b |
| `data/colorchecker_ohta1997_10nm.csv` | 3a49c6318ce61af97b9345d8a7685de7 |
| `data/mgfb_classic_char_filter2.csv` | eff4cede552e1f07a08cd20442e0f599 |
| `judge2_VTG100/VTG100_colorchecker.png` | 36e93fd4151c5ec5b7b82c892af8c3c0 |
| `judge2_VTG100/VTG100_distinct.png` | dcdbbc27bca1c05248469f7569a6e675 |
| `judge2_VTG100/VTG100_effect.png` | 75896d4babc11d83d6f10d8b2e94d996 |
| `judge2_VTG100/VTG100_kernel.png` | 4c05f3ad368765be2ff21cd20316403b |
| `judge2_VTG100/VTG100_ramps.png` | 0c654a42776f795acbb1eeecd8fa1ae0 |
| `judge2_VTG100/VTG100_scenes.png` | 3a30fe41d63f1bd4c2921ff6ff6b74b7 |
| `judge2_VTG100/VTG100_skin.png` | bdf9120e0c4f7a456c568c70023334d5 |
| `judge2_VTG100/judge2_VTG100.json` | 127b8678242d4167c69e691108e0d614 |
