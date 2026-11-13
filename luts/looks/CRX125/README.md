# CRX125 - Crossover 125 (catalogue id 59, type neg (creative))

WindyStrife own film. Research reference (never shipped): Harman Switch Azure (ISO 125 creative C-41). Shipped name "Crossover 125", code CRX125. Status: **kept as id 59, effect film (owner-approved 2026-11-11; J3 skin gates waived)**.

## Result (judge v2.1, run against every folder in catalogue/looks at the end of the session)
- verdict **FAIL**; failed gates: J3_skin_probes, J3_skin_grid_hue, J3_skin_grid_green, J8_kernel_max
- J5 nearest STR400 at 8.01 dE100 (gate 2.0; 2.0..3.0 = warning)
- failed-gate numbers: J3_skin_probes {"hue_min": 245.5, "hue_max": 277.1, "C_ratio_min": 0.41, "failing": ["pale_porcelain"]}; J3_skin_grid_hue 7; J3_skin_grid_green 340; J8_kernel_max {"worst": 8, "at": "night V"}
- T17 md5 `5b34c1773664c52ecf9877144c4f01df`, CUB md5 `4a485ceafe216bd37ecef23cd415ae6b`
- judge output: `judge2_CRX125\` (JSON + scenes, skin, ramps, distinct, kernel, effect sheets)

## Files
`gen_crx125.py` generator (python3 + numpy + PIL; --set / LOOK_OVR overrides are dev only), `build.sh` build (generator, CUB, `/root/ml/yuvre_work/harness lut`, T17, judge), `data/` digitised sources with headers, `src/` tracers, `CRX125.CUB`, `CRX125.T17`.

## Sources and claims (clean room: public maker and review material only; nothing from the Film Roll author)
- Retailer product text (Roberts Camera, ProCam, Retro Camera Web, 2026): ISO 125, C-41, two of three colour couplers swapped, blue light recorded in the red channel, red in the blue channel, green stays; best at box speed.
- Reviews (Digital Camera World 2026 x2, analog.cafe "Blue Halation", Evan Hutchings 2026-08, Lexi Gerdts 2026, PetaPixel 2026-03-05 via search summary): blues orange, reds purple, skin cyan/blue, yellows azure, greens least affected, high contrast, limited range, blue halation, frame-to-frame tint scatter.
- Kodak E-7022 p.4 curves (data/E7022_*.csv) for the tone / grey axis (donor).

Claims implemented:
- C1 hue map of a record crossover R<->B of strength 0.9 (retailer text): orange/skin to cyan-blue, blue sky to orange, red to purple, yellow to azure, green nearly unchanged (scenes sheet)
- C2 higher contrast and a shorter range than DLY200: neg_scan contrast 0.16, k 1.12 (reviews)
- C3 (declared skin window 200..285 deg) documented behaviour: skin renders cyan-blue (retailer and review accounts above)

## Rounds and decision
Three rounds (hm_edge 0.5, 0.85 = worse, 0.2 = best). The look is right by eye (scenes sheet). J3_skin_grid_green demands 0 of 340 skin samples above hue 75; this film puts skin at hue 245-280, so that gate fails for every honest version. Remaining fails besides it: pale_porcelain chroma ratio 0.41, 7 grid samples outside the window, J8_kernel_max 8 (limit 7, steep hue map). If the owner waives J3_skin_grid_green and accepts the other three it can ship as id 59.

## Shipped strings
Name "Crossover 125", code CRX125, details line: "Crossover 125: crossed colour records, orange to cyan-blue, blue to orange, vivid and contrasty". No brand or product names in the CUB TITLE or generator output strings (citations only in comments, data headers and this README).

## Grain and halation proposal
- grain: Neg 160 x0.9 (existing row; reviews: fairly fine grain for ISO 125)
- halation: NEW profile proposal "negative, blue-biased": negative halation x1.3 with the halo tinted toward blue (reviews: strong blue halation)

## Owner decision and waiver (2026-11-11)
J3 skin gates waived by owner 2026-11-11: effect film, skin intentionally recoloured. Waived: J3_skin_probes (including the porcelain chroma ratio), J3_skin_grid_hue, J3_skin_grid_green. The declared skin window 200..285 deg stays (documented behaviour: skin renders cyan-blue, claim C3). Status: id 59, effect film, approved for the release; one open gate, J8_kernel_max.

## J8_kernel_max (8, limit 7): diagnosis and two rounds
- Final judge (release-only `--others`: copies of the release T17s incl. BAS200 and CLS125; VSL50, SLD100, HER64, CSL100, VSL100, the DLY200 preset and TRV400_L excluded): FAIL only on the waived J3 gates plus J8_kernel_max 8 at "night V" (kernel against the exact tetrahedral lookup of the same T17, camera chroma V). Everything else passes: J1 (d2_max 0.0464, p999 0.0359), J2, J4 (dip -0.002, hue at clip 4.8), J5 (nearest STR400 8.01), J6, J8_kernel_added_p999 (worst 1) and J8_kernel_added_max (worst 2), J9.
- Where it peaks: the T17 itself has V steps of 50-67 codes between adjacent V nodes (node V index 11 to 12) at U nodes 0..3 (U about -128..-80) and luma nodes 7..11, plus similar a-axis steps (49) near luma nodes 5-6; in input terms that is the saturated yellow/orange-red edge of the YUV cube at mid-bright luma, partly outside the sRGB gamut. The night scene's saturated lights sample across that edge, so the 2.5D kernel (luma offset per chroma pair) and the tet lookup disagree by up to 8 codes in V.
- Why: the crossover turns orange/red to blue/purple, so across that edge the output chroma vector flips between the two ends of the hue ring (V +60 to -10); the loader additionally clips out-of-gamut nodes per channel before the lookup, which sharpens the step (same artefact the slides show).
- Round 1: hue-turn function smoothed (circular Gaussian, 10 deg) and the hue turn faded in over chroma 0.04 instead of 0.02: J8_kernel_max 8, T17 steps unchanged (67 codes). Round 2: smoothing 24 deg: J8_kernel_max 8, J1 luma axes -0.6 (ok), steps unchanged, hue of the porcelain probe moved 4 deg. Smoothing the turn does not touch the step because the step is the fold of the hue ring itself, and removing the fold (partial swap < 0.6 or hue turns under about 100 deg) would end blue skin / orange sky / purple red. Stopped after 2 rounds as agreed; the shipped table is the unsmoothed one (hm_smooth 0, kept in the generator as an option).
- If the owner wants J8 fully green: a local fix would be a chroma-dependent fade of the hue turn toward the saturated yellow/orange-red edge only (loses the saturated orange to blue mapping at the gamut edge), or J8 limit waiver of 1 code for this effect film.

## Final files
CRX125.T17 md5 5b34c1773664c52ecf9877144c4f01df, CRX125.CUB md5 4a485ceafe216bd37ecef23cd415ae6b; judge2_CRX125\ (JSON + 6 sheets, run with the release-only others). Grain / halation proposal unchanged: Neg 160 x0.9, negative halation x1.3 with a blue-biased halo (new profile).

CRX125 J8 (main thread, 2026-11-11 21:50): J8_kernel_max 8 vs 7 accepted as a 1-code waiver. The v2.1 kernel-added gates pass (added p999 1, added max 2), so the 8 comes from the table's own hue-ring fold at the saturated orange-red edge, not from the kernel. The owner can still pick a local fade or a drop before release.
