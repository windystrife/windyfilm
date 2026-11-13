# luts/looks: the 30 film looks (WindyStrife's own generators)

Each `<CODE>/gen_*.py` is a parametric look model (numpy + PIL, no scans, no third-party LUTs) that writes one
33^3 .CUB (display-referred sRGB in, sRGB out) and its checks: `python3 -I -B gen_xxx.py <outdir>`.
`data/*.csv` next to a generator are our digitisations of published data sheets and the CIE tables (each file's
header names publication, revision and page). `fuji/` holds the three generators of the shipped looks
(Classic Chrome, Classic Neg, Pro Neg Std, Eterna; Chungking 320 and Cine 250D have their own directories).

Comments in the generators that say "through the module's own loader (harness ...)" are history: the tables are
now made by `tools/mk_wtab.py` from these cubes. Nothing in here reads any table or pack of another module.
