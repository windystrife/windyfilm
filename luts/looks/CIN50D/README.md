# CIN50D (colour negative; judge v2.1 final: PASS)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`): **PASS**, no failing gate. J5 nearest CINE250D 2.05 (parts scenes/colour/skin [2.08, 2.2, 1.88]); second DLY200 2.17.  J5 is in the 2.0..3.0 band: owner note.

## Colour-negative separation round (neg_separation.md)
Moves: neutral, S 0.98, K 0.86, B 29; not a fallback (J5 vs Cine 250D 2.05 >= 2.0).

Measured axes through the v2.1 kernel: W_b/W_a 0.00/0.00, S 0.981, K 0.855, B 29, T_a/T_b 0.00/0.00, R 0.970.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- CIN50D.T17 (harness `lut CIN50D.CUB CIN50D.T17`): 731db92ecc2d9caa354d799c97dfb947
- CIN50D.CUB: 8bc850e20fa2b9895b7009d50fa73870
- gen_cin50d.py: 5d969bbcfa247e61e40f31c6be8482a2 (`python3 -I gen_cin50d.py <outdir>` writes the CUB)
- judge2_CIN50D\ : judge2_CIN50D.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: NEW row Neg 50
- Halation: C800T x0.6

## Earlier README (before the separation round)
# CIN50D - Cine 50D (batch 4)
Models: 50D-class daylight ECN-2 negative, lab scan (neg_scan tone from H-1-5203 p.4 sensitometry, spectral/dye p.4-5; Cine 250D model reused). Clean neutral, low contrast, wide latitude.
Sources/claims/guesses: see provenance block in gen_cin50d.py and data/*.csv. Final tune: skin_gather 0.35 (skin grid hue window), default otherwise.
Judge v2: PASS. T17 md5 7ef93c90b74656b840dcc485cf1da779. Nearest PRT400 mean dE 2.27 (2.0-3.0 band: owner note), then PRONEG 2.44.
Details line proposal: "ISO 50 daylight cine neg, lab scan". Grain: NEW row Neg 50 (H-1-5203 p.4 RMS); halation C800T x0.6.
Sheets: sheets/CIN50D_scenes.png, CIN50D_skin.png.
