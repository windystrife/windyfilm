# BAS200 - Basic 200 (catalogue id 60, type bw)

WindyStrife own film. Research reference (never shipped): Kentmere Pan 200 (budget panchromatic B&W 200). Shipped name "Basic 200", code BAS200. Status: **kept**.

## Result (judge v2.1, run against every folder in catalogue/looks at the end of the session)
- verdict **PASS**; failed gates: none
- J5 nearest CLS125 at 2.51 dE100 (gate 2.0; 2.0..3.0 = warning)
- failed-gate numbers: none
- T17 md5 `de23520f30534ab6ef22ab73e32dd6c0`, CUB md5 `e6885ed7c5f6a18a5e8f914118ac8902`
- judge output: `judge2_BAS200\` (JSON + scenes, skin, ramps, distinct, kernel, effect sheets)

## Files
`gen_bas200.py` generator (python3 + numpy + PIL; --set / LOOK_OVR overrides are dev only), `final.sh` build (generator, CUB, `/root/ml/yuvre_work/harness lut`, T17, mono_t17.py (raw U/V 0x00), judge), `data/` digitised sources with headers, `src/` tracers, `BAS200.CUB`, `BAS200.T17`.

## Sources and claims (clean room: public maker and review material only; nothing from the Film Roll author)
- HARMAN Kentmere Pan 200 technical information, March 2025 (4 pp.): medium speed, fine grain, good sharpness, "mid-high pictoral contrast at the given development times", ISO 200/24 in ID-11, 0.125 mm low-density acetate base, ILFOTEC HC 1+31 6:00 at EI 200. No curve, no spectral figure, no granularity number.
- Reviews and forums 2025 (Parallax Photographic, analoguewonderland, photrio, l-camera-forum): contrastier than the other Kentmere films, clear base raises contrast, more halation, tonal separation disputed.
- Shape donor: HARMAN FP4 Plus technical info Nov 2018 p.5 curve and ILFORD FP4 Plus fact sheet 04/2004 p.1 spectrogram (data/char_bas200.csv, data/spec_bas200.csv; GUESS: not the same film).

Claims implemented:
- C1 higher print contrast than CLS125: curve gradient x1.15 of the donor (CLS125 x0.95), base+fog 0.075 vs 0.095
- C2 spectral response is a LOOK CALL: half FP4 spectrogram, half flat panchromatic to 660 nm (nothing published for this film)

## Rounds and decision
R1 (g 1.0): twin of CLS125 (0.82). R2 g 1.25: distinct 3.68 but J-BW colour-checker 0.0796 (cyan). R3 g 1.15: 0.0731. R4 g 1.15 with E soft limit 40/72: PASS, 2.51 from CLS125 (warning band).

## Shipped strings
Name "Basic 200", code BAS200, details line: "Basic 200: fairly punchy everyday B&W, fine grain, bright base". No brand or product names in the CUB TITLE or generator output strings (citations only in comments, data headers and this README).

## Grain and halation proposal
- grain: B&W 400 x0.75 (existing row; maker text "fine grain", users place it between FP4 Plus and HP5 Plus)
- halation: NEW profile BW_WEAK (as proposed by VTG100): users report no anti-halation layer and more halation than FP4 / HP5; clear low-density base
