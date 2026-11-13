# SUN200 (colour negative; judge v2.1 final: FAIL)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`, `--tint declared` (declared grey-axis tint)): **FAIL**, failing: J5_distinct_nearest (nearest film DLY200 belongs to another agent and appeared after this look was finished; see STATUS_NEG.md). J5 nearest DLY200 1.12 (parts scenes/colour/skin [1.14, 1.12, 1.11]); second TRV400 2.02.

## Colour-negative separation round (neg_separation.md)
Moves: W 2.9/0.8 (golden), S 1.12, K 1.04, B 25, shadow tint warm-brown; tint declared (J2).

Measured axes through the v2.1 kernel: W_b/W_a 2.90/0.80, S 1.118, K 1.037, B 25, T_a/T_b 0.09/1.66, R 0.936.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- SUN200.T17 (harness `lut SUN200.CUB SUN200.T17`): 5838d2ba3dc5bfe6be0b9cebba6245cc
- SUN200.CUB: 6492416be567a91728447abe344d7188
- gen_sun200.py: 935a081ffe92025c311f1e2f30cc2c1a (`python3 -I gen_sun200.py <outdir>` writes the CUB)
- judge2_SUN200\ : judge2_SUN200.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: NEW row Neg 200 (E-7022 PGI, from public granularity only)
- Halation: negative profile

## Earlier README (before the separation round)
# SUN200 - Sunny 200 (catalogue id 34, type neg, family "consumer neg")

WindyStrife's own look of a warm ISO 200 consumer colour negative, rendered as a warm minilab scan. It has a
golden-yellow cast, saturated warm colours, peach-golden skin, moderate contrast (a print a touch denser and firmer
than the camera JPEG) and brown-olive deep shadows. Pale skies stay blue. Grain and halation come from the module's
effects (proposal below), not from this LUT. All shipped strings are generic. The CUB title is
"SUN200 - Sunny 200 (WindyStrife original LUT)".

## Files
| file | what |
|---|---|
| `gen_sun200.py` | Generator: the OKLab model (verbatim from gen_chungking / gen_port160) plus the `neg_scan` family module. Provenance block in the header. Run as `python3 gen_sun200.py [--preset SUN200] <outdir>`. |
| `SUN200.CUB` | 33^3 cube from the generator. |
| `SUN200.T17` | 17^3 YUV table: SUN200.CUB through the module's own .cube loader (`/root/ml/yuvre_work/harness lut`). |
| `checks_sun200.json`, `preview_sun200_curves.png` | The generator's self-checks and curve preview. |
| `judge2/judge2_SUN200.json` + 6 PNG | Judge v2 (v1.1). This verdict is authoritative: PASS. |
| `judge_sun200.json`, `sheets/*.png` | Own judge (J1-J9, J7 claims, distinctness vs batch 1) and review sheets. |
| `data/*.csv` | Digitised sources. Each CSV header gives the publication, revision, page and figure. |
| `tools/` | `final.sh` (generator -> CUB -> harness -> T17 -> both judges), `judge_sun200.py`, `kern.py` (numpy model of the shipped kernel + D12), `rho.py` (grain-row numbers from the T17), `setup.sh` / `cam_main.c` (WSL camera-path build), `q.sh`, `trace_char.py` / `axes.py` (digitising aids). |

To reproduce in WSL (Ubuntu-24.04, root), run `bash tools/setup.sh` and then `bash tools/final.sh`. setup.sh rebuilds the work dir /root/ml/ws_sun200, which was deleted after delivery.

## Sources
- **S1** Kodak Publication No. E-7022, KODAK GOLD 200 Film technical data, Kodak Alaris, Revised 06-23.
  - p.4 Characteristic Curves (E7022B; Log H ref -1.14, Status M) -> `data/E7022_characteristic_curves.csv`.
  - p.4 Spectral-Sensitivity (E7022C) -> `data/E7022_spectral_sensitivity.csv`.
  - p.4 Spectral-Dye-Density -> `data/E7022_spectral_dye_density.csv`.
  - p.3 Print Grain Index -> `data/E7022_print_grain_index.csv`.
  - pp.1-2 text facts (ISO 200, latitude -2/+3, "saturated colors", 0.10 mm ESTAR base, grey-card red density) -> `data/E7022_facts.csv`.
- **S2** (secondary, comparison only) cacreeks.com/films.htm, Kodak PGI table and max-density table.
- **S3** Kodak Publication No. E-4021, PORTRA ENDURA / SUPRA ENDURA Paper technical data, October 2003, p.9, Characteristic Curves of the Supra Endura paper (F002_1197AC, Status A) -> `data/E4021_paper_characteristic.csv`. Used only for the grain row's rho_max cap (display-medium Dmax).
- **D1** analog.cafe, "Kodak Gold 200 Film Review" (2022, upd. 2024): warm, yellow tint in light greys, saturated greens, lighter skies.
- **D2** 50mmf2.com, "Kodak Gold 200 Review" (2021): yellow-green / brown cast, contrasty, saturated.
- **D3** decafjournal.com, "A Review of Kodak Gold 200 Film" (2025): warmth on skin, strong yellow / orange / red, Noritsu scans with subdued contrast.

## Claims (relative, cited) and results
| # | claim | source | result |
|---|---|---|---|
| C1 | Grainier than the portrait-160 class | PGI 44 vs 28-32 at 4x6 from 135 (S1 p.3, S2) | Grain row Neg 200 sigma scale 0.95 vs Neg 160 0.51 (proposal below) |
| C2 | More saturated than Portrait 160 and Soft Neg 160 | S1 p.1 "saturated colors" vs the portrait class | Chroma factor 1.092 vs PORT160 0.846 / PRONEG 0.76: pass |
| C3 | Mid-grey contrast between Portrait 160 and Hard Neg 400 | S1 slopes 0.54-0.61; D1 "neutral", D2 "contrasty", D3 "subdued" | Slope 1.12 (codes 98..138), between PORT160 0.954 and CLASNEG 1.145: pass |
| C4 | Grey mids warm | D1, D3 | OKLab b at grey 118 = +0.0245 (> 0): pass |

## Guesses (GUESS in the code)
- **neg_scan scan parameters:**
  - `ns_floor` -5.2: the JPEG black sits that many stops under mid grey.
  - `ns_ev` 0: box speed.
  - `ns_mid` 0.172: a print 0.07 stop denser than the camera.
  - `ns_white` 0.975: display 252.
  - `ns_contrast` 0.12, tapered out over display 0.60..0.95.
  - `ns_tint` 0.5: the minilab auto balance removes about half of the toe tint.
  - `tint_top` 0.90..0.97.
- **Declared golden-amber grey tint:** `tintL_lab`, b up to 0.025 in the mids, hue about 72 deg. It is brown in the shadows and neutral at paper white. Judge v2 runs with `--tint declared`.
- **Colour model:**
  - Saturation: sat 1.10, cknee 0.14, cmax 0.25, satL.
  - Per-hue dH / S / dL: reds and yellows warmer and stronger, greens slightly yellow, sky lighter, purples a little stronger.
  - veil 0.0008: output black 3.
  - skin_sat 0.10.
- **Guards:**
  - Skin guard for the tint: `skin_tl` 1.0, kappa 2.0.
  - Tint fades: `tl_cfade` and `tl_toe` 0.1.
  - New sky guard `sky_tl` (0.85, 220 deg, kappa 1.2, C1 0.05). It keeps the warm tint off pale blue and cyan, which would otherwise turn sage-green.
- **OPT gamut knee** 0.84 instead of 0.88, so the shipped 2.5D kernel stays within J8 at the gamut edge.
- **G4 (INFERRED):** the red record's sensitivity at 580-600 nm (S1 E7022C) supports the red and yellow turns toward orange. The amounts are still GUESS.

## Grain / halation proposal (new row "Neg 200", PLAN 5.2 batch 1)
The parameters below come from public granularity only (S1 PGI, H-1-5219t rms shape). Nothing is copied from the existing rows' rho_max.

```
{   /* ISO 200 colour negative, consumer (Sunny 200; shared with SNP200 x1.15, TRV200 x1.0) */
    "Neg 200", YG_NEG, 200, 0.0f, 1.0f, { 1.0f, 0.9f, 1.73f }, 6.5f, 15.0f, 0.25f,
    { 10, 10, 10 },
    { S5219_R, S5219_G, S5219_B },
    { G5219_R(0.95f), G5219_G(0.95f), G5219_B(0.95f) },
    { 0.67f, 0.73f, 0.76f }, 0.565f, 1.99f, 2.48f, 3, 1
},
```
- **iso_grain 200, ei_stops 0:** S1 p.1 box speed. The dye clouds scale by (200/200)^0.25 = 1, i.e. the 6.5 / 15 um reference size.
- **sig shape:** the H-1-5219t p.4 rms curves (public; as in the existing colour-neg rows). E-7022 publishes PGI only, no rms curves.
- **sigma scale 0.95 (GUESS):** 0.8 x 44/37. That is the Neg 400 row's 0.8 at PGI 37 (E-4050 p.3, as digitised for PRT400), scaled by this film's PGI 44 (S1 p.3, 135 at 4x6). PGI is a perceptual scale (4 units = 1 JND), so mapping it proportionally to rms is a GUESS.
  - The bracket is 0.80 (PGI saturating) to 1.03 (linear in PGI through the Neg 160 row 0.51 @ PGI 28 and the Neg 400 row 0.8 @ PGI 37).
  - PGI 44 sits between the 400 portrait class (<40) and the 400 consumer class (46, S2), so it should be grainier than Neg 400 and not above it by much.
  - Catalogue scale for SUN200: Q8 256 (x1.0).
- **dmid (net density of an 18 % grey at log H -1.14):** R 0.67, G 0.73, B 0.76, from S1 p.4. Total R 0.92, G 1.38, B 1.72 minus D-min 0.25 / 0.66 / 0.96. The red value agrees with p.2's grey-card red density 0.85-1.05.
- **neg_gamma 0.565:** the green-record mid-scale slope of the S1 p.4 trace (R 0.544, B 0.617).
- **rho_max 2.48 (per film, catalogue Q8 635), by the PLAN 3.2.4 (b) rule:**
  - From this film's own T17, through the shipped kernel (`tools/rho.py`): log10(L(Y255 = 252) / L(Y0 = 3)) = 3.03.
  - Capped by the display medium's published Dmax: S3 p.9 Supra Endura paper, neutral net Dmax = lowest record at the plot end 2.58 minus D-min 0.10 = 2.48.
  - So rho_max = min(3.03, 2.48) = 2.48.
- **print_gamma 1.99 (DERIVED, no public source):** the T17's log-log mid slope at grey 118 +-0.5 EV (1.123, `tools/rho.py`) divided by neg_gamma 0.565.
- **rho 0.25, size_scale {1, 0.9, 1.73}, d_fine 6.5 / d_clus 15, chroma_shift 1:** unchanged from the colour-neg rows. Their own provenance lines apply.
- **Halation:** profile YH_NEG (negative) at Q8 256 (x1.0), a GUESS. S1 gives no halation data. The film is a conventional 135 colour negative on a 0.10 mm ESTAR base (S1 p.1) with an anti-halation layer, so it gets the type default.

## Judge results
- **Judge v2 (v1.1, `judge2/judge2_SUN200.json`): PASS, exit 0.**
  - **J6:** 4914 words, flag 1, top byte 0, neutral Y rising 3..252.
  - **J1:** grey monotonic; luma axes -0.49; d2 max 0.0557, p99.9 0.0315.
  - **J2:** 0 reversals, white 252, black 3.
  - **J3:** probes hue 46.6..67.6 (C ratio min 1.03); grid 0 / 0 / 0.
  - **J4:** worst 3 L reversals; hue at clip 2.2.
  - **J5:** blacks 0 %; nearest PRT400 2.33 (WARNING, 2-3 band).
  - **J8:** p99.9 2 (card Y), max 7 (chart Y).
  - **J9:** bit-exact model; subtle grey 0 rev / hue 1.5; strong grey 0 rev / hue 5.5.
- **Own judge (`judge_sun200.json`): ALL true.**
  - Grey 118 -> RGB 126/114/100.
  - Grid hue 42.6..67.3.
  - Chroma factors: scene 1.027, lake 1.056, sunset 1.213, card 1.072.
  - J7 C2-C4 pass.

## Distinctness (mean dE OKLab x100, kernel outputs; lake/sunset/scene/card/chart)
| film | dE | |
|---|---|---|
| PRT400 | 2.33 | **owner note (2-3)** |
| CCHROME | 2.62 | **owner note** |
| CLASNEG | 2.69 | **owner note** |
| TUN800 (batch 1, judged as present at 13:45) | 2.96 | **owner note** |
| CINE250D_PRINT (other workflow, info) | 2.95 | **owner note** |
| CHUNGK | 3.05 | |
| PRONEG | 3.70 | |
| PORT160 | 4.44 | |
| CINE250D scan (info) | 4.83 | |
| ETERNA | 5.44 | |
| PRS400 | 8.94 | |

Every distance is >= 2.0. The first five fall in the 2.0-3.0 band, so the owner must be told.
- **PRT400** is the nearest. Both are colour-negative scans, and SUN200 differs from it in four ways (judge v2 metrics):
  - Golden greys: OKLab b at grey 118 is 0.0245 vs 0.0.
  - More chroma: synthetic chroma factor 1.045 vs 1.006.
  - Firmer mids: slope 1.12 vs 1.00.
  - Deeper black: 3 vs 6.
- **VSL50** had no T17 when this was checked.

## Final md5
- `gen_sun200.py` 679a0c7ac9164c972c2c8fdcc1c1f59c
- `SUN200.CUB` 1fc630f0b15814b59a6bce8ce0638996
- `SUN200.T17` 304b2b1915da496af28ea1b80be9dcee
