# PAS400 (colour negative; judge v2.1 final: PASS)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`): **PASS**, no failing gate. J5 nearest PORT160 4.06 (parts scenes/colour/skin [3.81, 4.23, 4.13]); second SNP200 5.08.

## Colour-negative separation round (neg_separation.md)
Unchanged film (PASS before and after).

Measured axes through the v2.1 kernel: W_b/W_a 0.00/0.00, S 0.758, K 0.817, B 39, T_a/T_b 0.00/0.00, R 0.676.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- PAS400.T17 (harness `lut PAS400.CUB PAS400.T17`): 473f9e536ab107dd0a7fdfac56363223
- PAS400.CUB: 8bad48570f765376d95c7a5c6d7c09f6
- gen_pas400.py: 8aec87feb89de392bf9b98efaf3f747b (`python3 -I gen_pas400.py <outdir>` writes the CUB)
- judge2_PAS400\ : judge2_PAS400.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: Neg 400 x0.85
- Halation: negative profile

## Earlier README (before the separation round)
# PAS400 Pastel 400 (neg, batch 3)
Model: OKLab family generator (gen_pas400.py) + neg_scan on digitised Fujifilm AF3-176E (Pro 400H-class) curves (data/*.csv: p7 granularity, p8 fig18 characteristic, fig19 spectral, fig20 MTF, fig21 dye density), +1 EV (ei 0.301), bright soft scan. Build: build.sh.
Fourth layer: the extra cyan-green sensitivity (fig19) only enters as a hue/sat hint (cool-green foliage); its mixed-light benefit cannot be reproduced from a JPEG.
Result: judge v2 PASS, no declared exceptions. Distinctness 3.55 vs PRT400B (3.8 vs PORT160).
GUESSES: tone knots (lifted shadows/mids), per-hue S/dH/dL, kappa 0.5 (also reduced ramp L-reversals; reversals are 8-bit noise sensitive).
Details line: "Pastel 400: soft airy colour, bright highlights, cool greens, gentle skin"
Grain/halation proposal: row Neg 400 x0.85 (AF3-176E RMS 4, p7), halation neg.
md5: T17 473f9e536ab107dd0a7fdfac56363223, CUB 8bad48570f765376d95c7a5c6d7c09f6
