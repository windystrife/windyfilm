# POP800 (colour negative; judge v2.1 final: PASS)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`, `--tint declared` (declared grey-axis tint)): **PASS**, no failing gate. J5 nearest BLD100 2.14 (parts scenes/colour/skin [1.99, 2.24, 2.2]); second PRT800 2.21.  J5 is in the 2.0..3.0 band: owner note.

## Colour-negative separation round (neg_separation.md)
Moves: now the canonical POP800.T17 (variant POP800B kept as preset files). W 2.2/0.9, S 1.35 (highest of the family), K 1.16, B 26, warm shadow tint; tint declared (J2).

Measured axes through the v2.1 kernel: W_b/W_a 2.17/0.91, S 1.345, K 1.155, B 26, T_a/T_b 0.68/1.33, R 0.956.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- POP800.T17 (harness `lut POP800.CUB POP800.T17`): 9342ccc9312fa77b3b30405b4e8dabf9
- POP800.CUB: 7315d690da0c2d5c90454331b235f2e0
- gen_pop800.py: fd6c7718565a545fb512323afdf14079 (`python3 -I gen_pop800.py <outdir>` writes the CUB)
- judge2_POP800\ : judge2_POP800.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: Neg 800 x1.2
- Halation: negative profile

## Earlier README (before the separation round)
# POP800 - Pop 800 (catalogue id 57, type neg) - STATUS: FAIL judge v2 (unfinished), best variant POP800B

Look call (no published curves). Built from SUN200's neg_scan + OKLab module (tone class shape from a published 200-class
consumer negative sheet, E-7022; contrast, tint, hue/saturation edits are GUESS). Variants in gen_pop800.py: POP800 (A, warm, fails
J1/J4/J8, nearest SUN200 1.67) and POP800B (cool shadows over warm highlights, darker mids ns_mid 0.145, ns_contrast 0.14).
Shipped candidate files: POP800B.CUB / POP800B.T17 (md5 4181b8853195868d9ecca68dfdd9b767), judge2/ (JSON + sheets).
Judge v2 (neg, --tint declared): only J8_kernel_p999 (3 > 2, card U) and J5 (nearest PRT800 1.56 < 2.0) fail.
Findings: ns_contrast above ~0.2 breaks J1 d2 (cube smoothness); hue/saturation boosts barely move J5 (test content is mostly
neutrals/skin, so the nearest film is set by tone); lighter mids (0.205) break J1 and sit near PRT400, mid 0.172 sits on SUN200.
Grain Neg 800 x1.2, halation neg. Details line: "Vivid saturated 800 negative".
Rebuild: tools/run.sh POP800B 1 (WSL), tools/fin.sh.
