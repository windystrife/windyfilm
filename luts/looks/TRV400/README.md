# TRV400 (colour negative; judge v2.1 final: FAIL)

Final judge v2.1 (2026-11-11, `--others catalogue/looks`, `--tint declared` (declared grey-axis tint)): **FAIL**, failing: J5_distinct_nearest (nearest film DLY200 belongs to another agent and appeared after this look was finished; see STATUS_NEG.md). J5 nearest DLY200 1.73 (parts scenes/colour/skin [1.95, 1.69, 1.57]); second SUN200 2.02.

## Colour-negative separation round (neg_separation.md)
Moves: green lean W a -1.4, cyan-green shadows (T -1.25/-0.2), S 1.01, K 1.21, B 26; tint declared (J2).

Measured axes through the v2.1 kernel: W_b/W_a 0.20/-1.43, S 1.010, K 1.213, B 26, T_a/T_b -1.25/-0.18, R 0.828.
The move is a thin layer on the resolved preset (`RETUNE` in the generator: global chroma gain, tone power about mid, black lift, shoulder, grey-axis tint offsets, skin-hue gather limited to chroma < 0.09 so reds keep their hue, cube smoothing of nodes with a large second difference, luma-monotone repair). All amounts are look calls (GUESS) fitted by measuring the table through the camera kernel; sources and claims of the look itself are unchanged and listed in the earlier README below.

## Files and md5
- TRV400.T17 (harness `lut TRV400.CUB TRV400.T17`): 91e844c06313d7ea0cba2dd9a937b0e9
- TRV400.CUB: 2369621fe62c06700854e9116a92da5e
- gen_trv400.py: 7e5d0793d41b7357c2ff40d4e217dfa4 (`python3 -I gen_trv400.py <outdir>` writes the CUB)
- judge2_TRV400\ : judge2_TRV400.json, judge_stdout.txt, scenes / skin / ramps / distinct / kernel / effect sheets

## Grain row and halation profile (module effects, not this table)
- Grain: Neg 400 x1.1
- Halation: negative profile

## Earlier README (before the separation round)
# TRV400 (batch 2) - status FAIL on judge v2 (see judge2_TRV400/judge_stdout or judge2_TRV400.json)
Generator gen_trv400.py (provenance + GUESS list in its docstring), data/*.csv (cited), TRV400.CUB, TRV400.T17, judge2_TRV400/.
Honest state: tuned to trade J1/J3/J4/J8 near-misses against J5 distinctness; remaining failing gates are listed in the final batch report.
Final tuning is baked into the generator preset (tools/ holds the sweep scripts). Grain/halation proposals: see batch report.
