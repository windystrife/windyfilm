# Attributions

WindyFilm (`windyflm.mo`, its tools and its card files) is written by **WindyStrife**. It is licensed under the
GNU GPL version 2 (`LICENSE`), the licence of Magic Lantern, which it is built against and runs inside.

The sources below are credited. Unless a line says "data", no third-party code is included: those sources gave
ideas, published formulas or published numbers, implemented independently here.

## Code it builds against

- **Magic Lantern**: GPL-2.0, the Magic Lantern team (magiclantern.fm). The module includes ML headers and calls the ML
  API at run time; ML core patches 0002 / 0003 / 0004 are WindyStrife's.

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
companies. No logos are drawn.

## Not included

- The "Film Roll" module by florinkiritescu.ro and every part of it: its binary, film data, can art, palette, fonts
  and strings. WindyFilm does not need it, does not load it and must not run together with it.
- Third-party cubes the owner uses privately (for example cine print or camera-maker LUTs). They stay on the owner's
  own card and are never put in this repo or in a release.
