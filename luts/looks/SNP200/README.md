# SNP200 (colour negative; judge v2.1 final: PASS)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`, `--tint declared` (declared grey-axis tint)): **PASS**, no failing gate. J5 nearest PORT160 2.55 (parts scenes/colour/skin [2.78, 2.44, 2.43]); second CIN50D 2.71.  J5 is in the 2.0..3.0 band: owner note.

## Colour-negative separation round (neg_separation.md)
Moves: W 2.0/2.4 (magenta-red lean), S 0.92, K 0.81, B 50 (lifted black), shadow tint warm-magenta, reds/yellows x1.15 and greens x0.8; tint declared (J2). Not a fallback: vs SUN200 and CIN50D it is above 3.0.

Measured axes through the v2.1 kernel: W_b/W_a 2.03/2.41, S 0.919, K 0.810, B 50, T_a/T_b 1.90/2.00, R 0.863.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- SNP200.T17 (harness `lut SNP200.CUB SNP200.T17`): 23b30224074bdfe2b1df0a4d89951439
- SNP200.CUB: 69efa9797e058b7c6db854423efc22e1
- gen_snp200.py: 27d7a01f1d329072d46cd5c9341521e4 (`python3 -I gen_snp200.py <outdir>` writes the CUB)
- judge2_SNP200\ : judge2_SNP200.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: Neg 200 x1.15
- Halation: negative profile

## Earlier README (before the separation round)
# SNP200 - Snapshot 200 (batch 4) - judge v2 FAIL (2 gates, near miss)
Look call, no datasheet (retail latitude only; see data/SNP200_retail_facts.csv, E7022/E4021 curves for tone). Shipped variant A "rosy warm"; variant B "amber soft" kept as preset SNP200B.
A: T17 md5 581a6e6f8dac8587641e5f0b4b076a8c. Fails J3_skin_probes (pale_1 hue 70.5 vs 70 limit) and J3_skin_grid_hue (1 of 340). All else passes incl. J5: nearest CIN50D 2.01 (warning band), SUN200 3.16, TRV400 3.16.
B: T17 md5 0f7692cb1ede4315188b14c627785858. Passes everything except J5 (CIN50D 1.52, SUN200 1.77) so it is a preset, not a film.
Pale-skin hue shift grows with global sat (1.04 passes skin but falls to J4 foliage reversals; 1.07 chosen for distinctness). Pre-tuning generator: tools/gen_snp200_pre_v9a.py.
Details line: "ISO 200 consumer neg, warm soft print". Grain Neg 200 x1.15, halation neg.
Sheets: sheets/SNP200_scenes.png, SNP200_skin.png.
