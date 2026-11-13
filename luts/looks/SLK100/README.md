# SLK100 - Silk 100 (catalogue id 55, type bw) - judge v2.1 PASS

A fine-grain orthopanchromatic ISO 100 negative (reduced deep-red response: reds, lips and skin print darker than on a full panchromatic film), long smooth toe, printed on the published grade-2 paper with the metered grey at net density 0.68 and scanned: silky tonality, bright clean highlights.

Status: **PASS** on judge v2.1 (default kernel, final set of 2026-11-11). Kept in the release. Owner note: distinctness 2.56 vs PRS400 is in the 2.0..3.0 band: tell the owner in the batch report (PLAN 7.1 J5).

## Files

- `SLK100.CUB` (33^3, R = G = B on every line), `SLK100.T17` (17^3 YUV, 4914 words, raw U = V = 0x00): the shipped pair.
- `gen_slk100.py`: the generator (provenance and GUESS list in its docstring); `data/*.csv`: the digitised / public inputs, each with its source header.
- `mono_t17.py` (family copy, md5 9fdaefa3a23f2d08688b33b754e036f7): forces U = V = 0x00 after the module loader; `mono_t17.log` its report.
- `checks_slk100.json`, `provenance_slk100.json`, `bw_model_slk100.json` (spectral model for the J-BW self-check), `preview_slk100_curves.png`.
- `judge2_SLK100/`: final judge v2.1 JSON, stdout and sheets (scenes, skin, ramps, colorchecker, kernel, distinct, effect).
- Rebuild (WSL Ubuntu-24.04, root): `bash tools/final.sh` (generator defaults -> CUB -> `/root/ml/yuvre_work/harness lut` -> mono_t17.py). Deterministic: same md5s as below.

## Sources

- PARAMETRIC (clean room, no downloads in this batch): the maker's sheet was not digitised. Spectral sensitivity = blue Gaussian + green-red logistic plateau + red logistic cut-off; negative = fog + gamma x softplus toe, placed by the ISO 6 speed point at box speed. Every value is a LOOK CALL set from the published descriptions summarised in catalogue/inventory.md (B&W list). Sheet named there: the maker's orthopanchromatic ISO 100 data sheet; documented traits: reduced red sensitivity with a cut around 640-650 nm, very fine grain, rich mid-tone separation, bright clean highlights, darker reds.
- HARMAN technology "ILFORD MULTIGRADE FB CLASSIC" technical information, Oct 2013: p.2 filter-2 characteristic curve (data/mgfb_classic_char_filter2.csv); p.1 ISO range table (grade 2 = R95; other grades by paper_k = R(target) / 95).
- CIE 018:2019 Table 6 (CIE 1931 2-degree CMFs), CIE 015:2018 daylight S0/S1/S2 (D55 = 5503 K), ColorChecker Classic reflectances (Ohta 1997, colour-science BSD-3; J-BW self-check only): data/cie*.csv, data/colorchecker*.csv.

## Look calls (GUESS)

- sensitivity: blue 440 nm / 35 nm / 1.2, plateau rise 490 nm / 12 nm, plateau 0.95, red cut at **605 nm** / 6 nm (half height): earlier than the described 640-650 nm, a look call that darkens reds and keeps the film >= 2.0 from Press 400
- negative: ISO 100, fog 0.18, gamma 0.55, toe softness 0.30 log H
- paper_k 1.0 (grade 2), print anchor "grey" 0.68
- scan levels 7 / 251

Family kernel fit (engineering, judge v2.1 per-pixel-E kernel): E soft limit 60 / 120 codes, no flat E, slope scaling below G' 0.8 over a +-16 code window, negative E limited to 40 codes, monotone closure along +R/+G/+B.

## Claims (testable on the sheets; numbers from the shipped tables)

| claim | test | result |
|---|---|---|
| orthopanchromatic: red patch darker than Press 400 | SLK100.red < PRS400.red (0.4604 vs 0.5417) | holds |
| green patch lighter than Press 400 | SLK100.green > PRS400.green (0.6534 vs 0.6301) | holds |
| skin prints darker than Press 400 (dark skin) | SLK100.dskin < PRS400.dskin (0.4748 vs 0.4997) | holds |

Tone of the shipped table (grey input -> output code, inputs 16/32/64/96/118/160/200/230/245): 10/19/60/112/142/186/218/238/246; log-log slopes shadows (32->64) 1.53, mids (96->160) 1.0, highlights (200->245) 0.59.
ColorChecker output L (OKLab): red 0.460, green 0.653, blue 0.580, yellow 0.823, dark skin 0.475, light skin 0.754; spectral weights (R, G, B) (0.113, 0.514, 0.373).

## Judge v2.1 results

| gate | value |
|---|---|
| verdict | PASS, failed none |
| J1 cube luma axes / d2 max / d2 p99.9 | 0.0 / 0.0137 / 0.012 |
| J2 white / black floor | 251 / 7 |
| J4 ramp reversals (worst) / deepest dip | 0 / {'deepest': -0.0032, 'at': 'neonred'} |
| J8 kernel-added p99.9 / max, kernel max | 0 / 1, 4 |
| JBW raw U/V bytes nonzero / output chroma / grey-ramp UV | {'U_nonzero': 0, 'V_nonzero': 0} / 0 / 0 |
| JBW ColorChecker self-check mean / max |dL| | 0.0036 / 0.0116 (worst orange yellow) |
| J5 nearest / second (mean of 3 strata, x100) | PRS400 2.56 (parts [2.04, 3.07, 2.57]) / CLS125 2.63 |

Distinctness: nearest Press 400 **2.56 (2..3 warning band: owner note)**, second CLS125 2.63 (another agent's new film, also in the band). The difference is the spectral response (red 0.460 vs 0.542, green 0.653 vs 0.630) at a similar tone curve; darker or brighter curves were tried and moved it into Crisp 100 / Press 400 or broke the red claim. PASS with warning.

## Grain row and halation profile (proposal)

- Grain row **B&W 100T x 0.9**: the maker claims very fine grain; no RMS value found (scale GUESS, catalogue inventory).
- Halation **BW (YH_BW) x 1.0** (GUESS).

Details line: "ISO 100 ortho-pan B&W, dark reds".

## md5

| file | md5 |
|---|---|
| `SLK100.CUB` | afecc05539898fc4e983967a44d16e99 |
| `SLK100.T17` | e541e9c0ab861b01dde0091f6abf43a7 |
| `gen_slk100.py` | 20b836921e5b02b78440ea2b12fc3049 |
| `mono_t17.py` | 9fdaefa3a23f2d08688b33b754e036f7 |
| `checks_slk100.json` | e91d886f9ad4df9404a165112b6e271d |
| `provenance_slk100.json` | c93c67e0cc99a033e3917a4a16ddf1f3 |
| `bw_model_slk100.json` | 1bee93d7c9c9294d2440f8388b6fb134 |
| `preview_slk100_curves.png` | 9d62961b0f810bc073b9328fa54a81d7 |
| `tools/final.sh` | d039e8392e2cdc06ff80875b348fec06 |
| `data/cie1931_2deg_10nm.csv` | cd967fbccba6c1ab974844c267d40c88 |
| `data/cie_daylight_S012_10nm.csv` | 113659faec72f1d238369e30fb8a0b4b |
| `data/colorchecker_ohta1997_10nm.csv` | 3a49c6318ce61af97b9345d8a7685de7 |
| `data/mgfb_classic_char_filter2.csv` | eff4cede552e1f07a08cd20442e0f599 |
| `judge2_SLK100/SLK100_colorchecker.png` | 481068554e2518f17d3dca33cd270d07 |
| `judge2_SLK100/SLK100_distinct.png` | c1682c5dae0223a2b219eb1c47b0f7b8 |
| `judge2_SLK100/SLK100_effect.png` | d643cfc1d5b77b25f2c3e4b68c53ee32 |
| `judge2_SLK100/SLK100_kernel.png` | 0be0b96b03eba9d0deefc3263aff0f66 |
| `judge2_SLK100/SLK100_ramps.png` | 7923e647a06a857fe8ce7493050ec3c3 |
| `judge2_SLK100/SLK100_scenes.png` | a14272b94588698dd4e16c01fc38dc22 |
| `judge2_SLK100/SLK100_skin.png` | fcea120ba5ec3dd569a14ab9bc62187d |
| `judge2_SLK100/judge2_SLK100.json` | 3370dada58ce4fa0b0bc0524f2f28d57 |
