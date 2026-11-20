# Attributions

WindyFilm (`windyflm.mo`, its tools and its card files) is written by **WindyStrife**. It is licensed under the
GNU GPL version 3 or (at your option) any later version (`LICENSE`), since 18/11/2026; earlier commits, up to and
including tag RAWB27, were released under GPL version 2. Magic Lantern, which it is built against and runs inside, is
GPL version 2 or any later version, so the two combine under GPL version 3.

The sources below are credited. Unless a line says "data" or stands under "Derived from Genesis Pro", no
third-party code is included: those sources gave ideas, published formulas or published numbers, implemented
independently here.

## Code it builds against

- **Magic Lantern**: GPL-2.0-or-later (its source files say "version 2 of the License, or (at your option) any later
  version"), the Magic Lantern team (magiclantern.fm). The module includes ML headers and calls the ML API at run time;
  ML core patches 0002 / 0003 / 0004 are WindyStrife's and keep Magic Lantern's own licence, GPL-2.0-or-later.

## Data included (with its licence)

- **spektrafilm profiles**: © Andrea Volpato, **CC BY-SA 4.0** (github.com/andreavolpato/spektrafilm). These files are
  computed from those profiles and are CC BY-SA 4.0 as well:
  - the Film LUT cubes made by `luts/`;
  - the film tables (`films/tables/*.T17`) whose source is that model;
  - their print variants.

  Licence text: `licenses/SPEKTRAFILM_LICENSE.txt`. What we changed from the original data is recorded in each card's
  `CHANGELOG_FILMLUT.txt`. spektrafilm's code (GPLv3) is not used or copied, only ideas from it.
- **CIE 1931 colour matching functions and CIE daylight components**: CIE datasets (`luts/data/*.csv`).
- **Ubuntu Sans / Ubuntu fonts**: © Canonical Ltd., **Ubuntu Font Licence 1.0** (`licenses/UBUNTU_FONT_LICENCE.txt`).
  The browser's bitmap fonts (`WFFONTS.DAT`) and the text drawn on the cans are rendered from these fonts. The
  rendered bitmaps are not called "Ubuntu".

## Derived from Genesis Pro (published with permission)

- **Genesis Pro 1.6.6**, Cullen Kelly Color: a commercial OFX film-emulation plugin. WindyStrife states that
  permission to publish what is derived from it was obtained (20/11/2026). Credit lines are added by WindyStrife.
  These parts follow it (docs/DESIGN.md 1a):
  - the film-stage chain numbers (negative DLogE, StMtoPD, interlayer IIE, Print DLogE, DyeAmountLUT,
    Lin_to_LogFN), sampled by the host tools `tools/mk_genesis_*` and `tools/mk_wfstage.*` into card data:
    `ML/DATA/WFSTAGE.DAT` and the chain film tables. That data is only in the card zips on the release page, not in
    this repo;
  - the WindyGrain model and its three 64-point quantile tables (`src/yg_grain.*`);
  - the light-wrap halation, `YH_GEN` (`src/yh_halo.*`, `src/wf_fx.*`).

## Published specifications (formulas implemented independently)

- **Sony S-Log3 / S-Gamut3.Cine**: Sony technical summary.
- **Panasonic V-Log / V-Gamut**: Panasonic reference document.
- **ARRI LogC3 (EI 800) / ARRI Wide Gamut 3**: ARRI white paper.
- **Canon Log 2 / Canon Log 3 / Cinema Gamut**: Canon input transform v1.2 (2020-07).
- **ITU-R BT.601 / BT.709, sRGB (IEC 61966-2-1)**: the YCbCr and display encodings.

## Ideas

- **Film grain**: the approach follows Steve Yedlin's public talks and writing on emulating film grain (no code).
- **Halation and bloom**: the model follows ideas published with spektrafilm and our own measurements (no code).
- **Stock facts** come from the manufacturers' public technical data sheets, digitised by WindyStrife: box ISO,
  white balance, characteristic curves, spectral sensitivity, dye density and published granularity. Examples:
  Kodak E-4050 (Portra 400), Kodak H-1 (Vision3). Each file under `luts/looks/*/data/` names its publication, page
  and figure.

## Trademarks

These names are trademarks of their owners:
- Kodak, Portra, Ektar, Gold, ColorPlus, UltraMax, Vision3, Ektachrome;
- Fujifilm, Superia, Pro 400H, Eterna, Classic Chrome;
- Ilford, Lomography, CineStill;
- Canon, Sony, Panasonic, ARRI.

WindyFilm uses them only to name the look it emulates. It is not affiliated with or endorsed by any of these
companies. The source code in this repo draws no logos; the card zips carry the maker wordmark for the print frame
(below).

## In the release zips only (not in this repo)

The full card zips on the release page (RAWB27 onwards) also carry third-party data. WindyStrife publishes it and
states that permission was obtained; credit lines are added by WindyStrife:
- third-party LUT cubes under `ML/DATA/*.CUB`: the CinePrint cubes (Canon Log 2), the Leica look LUT, the V-Log
  grades and the EKTA P export (the owner's cubes of docs/DESIGN.md 1);
- the maker wordmark drawn in the print frame (`ML/DATA/WFLOGO.DAT`);
- the film-stage data derived from Genesis Pro (above).

## Not included

- The "Film Roll" module by florinkiritescu.ro and every part of it: its binary, film data, can art, palette, fonts
  and strings. WindyFilm does not need it, does not load it and must not run together with it.
- The third-party cubes, the maker wordmark and the Genesis-derived card data are never put in this repo's history.
