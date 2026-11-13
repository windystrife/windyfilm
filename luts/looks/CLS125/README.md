# CLS125 - Classic 125 (catalogue id 61, type bw)

WindyStrife own film. Research reference (never shipped): Ilford FP4 Plus (ISO 125). Shipped name "Classic 125", code CLS125. Status: **kept**.

## Result (judge v2.1, run against every folder in catalogue/looks at the end of the session)
- verdict **PASS**; failed gates: none
- J5 nearest CRS100 at 2.48 dE100 (gate 2.0; 2.0..3.0 = warning)
- failed-gate numbers: none
- T17 md5 `c9e06f2a9619e7e2b1344b317742a245`, CUB md5 `52fc94f7601c2a2c1e87b355425504a5`
- judge output: `judge2_CLS125\` (JSON + scenes, skin, ramps, distinct, kernel, effect sheets)

## Files
`gen_cls125.py` generator (python3 + numpy + PIL; --set / LOOK_OVR overrides are dev only), `final.sh` build (generator, CUB, `/root/ml/yuvre_work/harness lut`, T17, mono_t17.py (raw U/V 0x00), judge), `data/` digitised sources with headers, `src/` tracers, `CLS125.CUB`, `CLS125.T17`.

## Sources and claims (clean room: public maker and review material only; nothing from the Film Roll author)
- HARMAN FP4 Plus technical information Nov 2018: p.1 ISO 125/22, usable 6 stops over / 2 under; p.5 characteristic curve (ILFOTEC HC 1+31, 8 min) -> data/char_cls125.csv (digitised from the raster figure at 500 dpi).
- ILFORD FP4 Plus fact sheet April 2004 p.1 spectral sensitivity, wedge spectrogram to tungsten 2850 K -> data/spec_cls125.csv (vector curve, 2850 K Planck source divided out; the linear reading of the axis is a GUESS).
- HARMAN ILFORD MULTIGRADE FB CLASSIC information Oct 2013 p.2 paper curve (as PRS400).

Claims implemented:
- C1 ISO 6 check: average gradient from the speed point over 1.30 log E is 0.66 (criterion 0.615), so the x axis is log10
- C2 development scaled to 0.95 of the sheet curve so the negative range sits near the paper range (ISO 6846 annex A); open shadows, long straight line
- C3 spectral weights R 0.155 G 0.40 B 0.446: blue-green leaning after removing the tungsten source

## Rounds and decision
R1 g 1.0 (E 32/64): only the J-BW colour-checker failed (0.0838). R2 g 0.85: d2_max 0.067, colour-checker 0.073, 1.35 from PRS400. R3 g 0.95 with E 40/72: all gates; its only open item was the twin BAS200 at g 1.0, solved by moving BAS200. Final: PASS.

## Shipped strings
Name "Classic 125", code CLS125, details line: "Classic 125: fine-grain panchromatic B&W, long straight tones, clean highlights". No brand or product names in the CUB TITLE or generator output strings (citations only in comments, data headers and this README).

## Grain and halation proposal
- grain: NEW row "B&W 125" or the existing finest cubic row at x1.0 (the sheet says "exceptionally fine grain" and prints no number; no RMS is claimed)
- halation: BW (roll film has an anti-halation backing that clears in development, sheet p.1)
