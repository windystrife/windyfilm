# PRT800 (colour negative; judge v2.1 final: PASS)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`): **PASS**, no failing gate. J5 nearest POP800 2.21 (parts scenes/colour/skin [2.19, 2.48, 1.97]); second SUN200 2.36.  J5 is in the 2.0..3.0 band: owner note.

## Colour-negative separation round (neg_separation.md)
Moves: warmer and more saturated than the 400 (W 1.7/0.4, S 1.19 > PRT400 1.11), K 1.17 -> 1.0, B 27, softer shoulder. Default skin window 40..70 (the earlier 30..71 and white/black declarations are no longer needed).

Measured axes through the v2.1 kernel: W_b/W_a 1.72/0.40, S 1.185, K 1.000, B 27, T_a/T_b 0.40/0.68, R 0.863.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- PRT800.T17 (harness `lut PRT800.CUB PRT800.T17`): 7345cd4f4e8558d37428671e810aec74
- PRT800.CUB: 2a587a207ca9869317e939df111d540c
- gen_prt800.py: fdedaf1cc0aa2219694d20e88b269432 (`python3 -I gen_prt800.py <outdir>` writes the CUB)
- judge2_PRT800\ : judge2_PRT800.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: NEW row Neg 800 (E-4040 PGI)
- Halation: negative profile

## Earlier README (before the separation round)
# PRT800 Portrait 800 (neg, batch 3)
Model: OKLab family generator (gen_prt800.py, from gen_prt400.py) + neg_scan on digitised Kodak E-4040 (Jan 2025) p.4 curves and spectral sensitivities (data/*.csv; E-4050 only for the relative claim). Build: build.sh (gen -> harness lut -> judge v2).
Result: judge v2 PASS with declared --skin-window 30,71 (deep skin hue 35.4..70.4; the extremes did not move with any dH/S/spec/tone knob tried, they come from the E-4040 toe cross-channel model) and --white 250 --black 4. Distinctness 2.55 vs SUN200 (warning band), CIN50D ~2.5.
GUESSES: scan contrast k 1.08, toe_tint 0.3, per-hue S/dH/satL, tone knots.
Details line: "Pro portrait 800: warm, rich colour, wide latitude, grainy"
Grain/halation proposal: row Neg 800 (E-4040 print grain index from data/e4040_print_grain_index.csv), halation neg.
md5: T17 dfa21aef9d3ac32fbac7c3a72c9c5c74, CUB ac8642ca063407bf36b7a7325d7d24d0
