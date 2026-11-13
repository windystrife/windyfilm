# BLD100 (colour negative; judge v2.1 final: PASS)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`): **PASS**, no failing gate. J5 nearest POP800 2.14 (parts scenes/colour/skin [1.99, 2.24, 2.2]); second TUN800 2.66.  J5 is in the 2.0..3.0 band: owner note.

## Colour-negative separation round (neg_separation.md)
Moves: S 1.08 -> 1.21 (target 1.30 relaxed: the cube cannot keep J1 smoothness above ~1.22; PLAN J7 S >= 1.15 and > EVD400 1.17 holds), K 1.33 -> 1.25, B 16 -> 21 (the darkest low-chroma skin sample goes red through the table below B ~20), no grey tint.

Measured axes through the v2.1 kernel: W_b/W_a 0.00/0.00, S 1.211, K 1.248, B 21, T_a/T_b 0.00/0.00, R 1.009.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- BLD100.T17 (harness `lut BLD100.CUB BLD100.T17`): 7ad4be11830b61f8e368961422ae6da6
- BLD100.CUB: 0340570bc45cc72630e2e71f6a43fb8f
- gen_bld100.py: ad7f557072bc64014b90716d66517958 (`python3 -I gen_bld100.py <outdir>` writes the CUB)
- judge2_BLD100\ : judge2_BLD100.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: NEW row Neg 100 (parameters from the public granularity, E-4046 PGI)
- Halation: negative profile

## Earlier README (before the separation round)
# BLD100 (batch 2) - status FAIL on judge v2 (see judge2_BLD100/judge_stdout or judge2_BLD100.json)
Generator gen_bld100.py (provenance + GUESS list in its docstring), data/*.csv (cited), BLD100.CUB, BLD100.T17, judge2_BLD100/.
Honest state: tuned to trade J1/J3/J4/J8 near-misses against J5 distinctness; remaining failing gates are listed in the final batch report.
Final tuning is baked into the generator preset (tools/ holds the sweep scripts). Grain/halation proposals: see batch report.
