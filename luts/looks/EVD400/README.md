# EVD400 (colour negative; judge v2.1 final: PASS)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`): **PASS**, no failing gate. J5 nearest TRV400 2.18 (parts scenes/colour/skin [2.16, 2.31, 2.06]); second SUN200 2.34.  J5 is in the 2.0..3.0 band: owner note.

## Colour-negative separation round (neg_separation.md)
Moves: W 1.4, S 1.17, K 1.23, B 30 (punchy, warm-leaning, less warm than SUN200).

Measured axes through the v2.1 kernel: W_b/W_a 1.40/0.48, S 1.172, K 1.226, B 30, T_a/T_b 0.04/0.38, R 0.743.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- EVD400.T17 (harness `lut EVD400.CUB EVD400.T17`): 98c0192a88efa918fbf45fa7f578ad20
- EVD400.CUB: 3ca8ed946663379a7b500a07b6219d4a
- gen_evd400.py: a8744ef9628bfd740058f98fee372ff2 (`python3 -I gen_evd400.py <outdir>` writes the CUB)
- judge2_EVD400\ : judge2_EVD400.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: Neg 400 x1.15
- Halation: negative profile

## Earlier README (before the separation round)
# EVD400 (batch 2) - status FAIL on judge v2 (see judge2_EVD400/judge_stdout or judge2_EVD400.json)
Generator gen_evd400.py (provenance + GUESS list in its docstring), data/*.csv (cited), EVD400.CUB, EVD400.T17, judge2_EVD400/.
Honest state: tuned to trade J1/J3/J4/J8 near-misses against J5 distinctness; remaining failing gates are listed in the final batch report.
Final tuning is baked into the generator preset (tools/ holds the sweep scripts). Grain/halation proposals: see batch report.
