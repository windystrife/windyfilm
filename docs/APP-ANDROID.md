# WindyFilm for phones (Android first): stage 1 plan (21 Nov 2026, branch `app-android`)

**Status: research, feature map, mockups and plan for the owner's approval. No app code yet.** The only code on this
branch is what draws the mockups (`tools/apps/android_mocks/`).

Owner's words (21 Nov, paraphrased): the phone app shoots RAW and adds film; learn the features and choices of the
owner's reference, a commercial desktop film plugin, built on what WindyFilm already has; keep Film stage, with many
options; desktop and Android first.

The option model is shared with the desktop app (session `app-desktop`): `apps-handoff/option-model.md` in the
coordinator's notes folder; section 10 there holds this app's additions. The research notes with every source
are in the private desktop repository (`docs/android-research.md`).

## 1. Rules carried over

- `docs/DESIGN.md` section 1 holds: our own code. Other film tools, commercial or GPL, are idea sources only: no
  code, art, film data, tool or preset names, no layout.
- **Private data never ships.** The owner's purchased / unlicensed cubes (the CinePrint looks, the six V-Log cubes,
  Leica Vivid) and the Film stage tables (`WFSTAGE.DAT`, `WFLOG.DAT`, `WFSTLOG.DAT`) are read at run time from the
  user's own card or folder (Storage Access Framework). They are never in the APK, the repo, a backup or an export.
- Never in the repo: `.mo` / `.sym` / `.DAT` / `.CUB` / `.T17`, CR2s, the owner's photos. The mockups' inputs and PNGs
  stay in `_windyfilm_docs/android_mocks/`.
- The owner approves the images before any app code.

## 2. Research summary

### 2.1 What is new

- No film app we found ships **a stills camera with a live film viewfinder, RAW and a roll**. That part is ours. The
  depth of the editing options (section 3) follows the owner's reference, a commercial desktop film plugin.

### 2.2 RAW capture on Android

- Camera2 `RAW_SENSOR` = 16-bit Bayer; `DngCreator` (API 21) writes DNG 1.4 from CameraCharacteristics + CaptureResult
  (orientation, location, thumbnail <= 256 px); the docs ask for the lens shading map, which AOSP writes as GainMap
  opcodes. Uncompressed: 12 MP is about 24 MB.
  [DngCreator](https://developer.android.com/reference/android/hardware/camera2/DngCreator),
  [DngUtils.cpp](https://android.googlesource.com/platform/frameworks/av/+/refs/heads/main/media/img_utils/src/DngUtils.cpp)
- **Guaranteed on RAW-capable devices:** PRIV preview + JPEG max + RAW max (also YUV preview + RAW max). Exactly the
  streams this app needs. [CameraDevice](https://developer.android.com/reference/android/hardware/camera2/CameraDevice)
- Manual: MANUAL_SENSOR (ISO, exposure time, frame duration), MANUAL_POST_PROCESSING (WB gains / transform, tonemap,
  shading). Guaranteed ranges are small (ISO <= 100 .. >= 800; exposure > 100 ms only on FULL); ISO only with AE off;
  the fps range caps exposure. [CameraCharacteristics](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics)
- Who has it: no public percentages. Media Performance Class 31+ phones must expose RAW on the main back camera, 30+
  must be FULL. Quad / nona Bayer sensors give binned RAW unless MAXIMUM_RESOLUTION mode (own stream map). RAW from
  other lenses needs physical sub-camera streams; some Samsung A models expose only the main back camera.
  [MPC](https://source.android.com/docs/compatibility/17/mpc),
  [multi-camera](https://developer.android.com/media/camera/camera2/multi-camera)
- CameraX 1.5 has RAW and RAW + JPEG in ImageCapture and manual controls through Camera2Interop; RAW together with a
  GPU CameraEffect is undocumented. [ImageCapture](https://developer.android.com/reference/androidx/camera/core/ImageCapture)

### 2.3 Live film preview

- The camera frame arrives as an OES texture (SurfaceTexture); GLES 3.0 has 3D textures, so a film table is one 3D
  texture. Halation / bloom: a dual-filter down / up blur is the cheapest at 1080p on Mali (Arm, SIGGRAPH 2015).
  [SurfaceTexture](https://developer.android.com/reference/android/graphics/SurfaceTexture),
  [glTexImage3D](https://registry.khronos.org/OpenGL-Refpages/es3.0/html/glTexImage3D.xhtml),
  [Arm slides](https://community.arm.com:443/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_slides.pdf)
- A GPU effect on CameraX's ImageCapture is capped near the GL surface size and breaks zero shutter lag: stills must
  go through our own develop, not the preview shader.
  [camerax-developers](https://groups.google.com/a/android.com/g/camerax-developers/c/kBIZX8scR_g)
- No film app (Dazz, NOMO, Huji, VSCO, Moment, Filmborn) publishes its pipeline and no GPU costs are published:
  stage 2 measures on the owner's phones.

### 2.4 Storage and the camera card

- MediaStore needs no permission for the app's own media (Android 10+). SAF `ACTION_OPEN_DOCUMENT_TREE` with a
  persisted permission reaches a card's `DCIM`, `ROLLS`, `ML` on an OTG reader (Android 11+ blocks only volume roots,
  Download and Android/data). exFAT needs kernel support (Pixels since Android 13): some phones will not mount an SDXC
  card. [documents](https://developer.android.com/training/data-storage/shared/documents-files),
  [exFAT](https://www.androidpolice.com/pixel-android-13-exfat-support-history/)

## 3. Feature map

Status as in the option model: **H** have it (the engine part), **A** add, our own clean-room design, **L** later,
**S** skip. Our names only; the left column is bookkeeping and never shown in the app.

| Option area | ours | st | engine part / why |
|---|---|---|---|
| Film presets (categories, strip) | 97 films: Film (Colour 15, Cine 4, B&W 11, Film LUT 23) + Film stage 44 | H | catalogue, 17^3 tables, Log + LUT cans, `wf_stagek.c` |
| Favourites, saved presets, import / export | favourites; **Recipes** = a look saved as the sidecar, also as a camera `WINDYFLM.CFG` snippet | A | option model `recipe` |
| Source temperature / tint | Light Auto / Day / Cloudy / Shade / Tungsten + Kelvin / Tint | H + A | Light (3.13); B's channel scales |
| Source exposure | Exposure -3..+3 EV (RAW: before B's shoulder) | A | B scene scale |
| Push / pull | Film stage: Film exp, Push / pull; Film: Film exp | H / A | `wf_stagek.c` X axis; Film: linear scale before the table |
| Film developer (gamma, contrast, colour gain) | Dev contrast; Saturation | A | scale of the negative's density curves about grey (Film stage), N curve (Film) |
| Film compression (highlight roll-off) | Highlights / Shadows; RAW develop's film shoulder | H | `wf_hs_bend` (3.3); B shoulder |
| Expand (black / white points) | Print range: pinned / natural | A | the chain's pinned ends (DESIGN 1a) |
| Print stock, print exposure | Print stock (Scan / Cine / Paper; Film stage 15 prints), Print exposure (Exp comp) | H | `WFFILMS.DAT` variants, `wf_stagek.c` print half |
| Print paper, target white | Paper Glossy / Matte / Warm / Cool | H | `wf_paper` (3.10); printing lamp: S (needs new chain data) |
| CMY colour head + gang | Printer lights R / G / B (+-12 points), shown as RGB or CMY | H + A | `wf_stagek.c` printer timing; CMY = a view of the same numbers |
| Split toning (shadows / mids / highlights) | - | L | split toning after the print, our own |
| Grain profiles (gauge x ISO) | Grain Off / Fine / Normal / Strong / Even x3, Gauge 16 / 35 / 65 | H | `yg_grain.c`, Gauge row (3.7) |
| Grain size, amount, chroma, per-tone | Amount, Size, Colour, Tones | A | the same `yg` parameters, continuous |
| Halation presets, without the anti-halation backing | Halation Off / Wide / Tight / Wrap; Backing on / removed | H + A | `yh_halo.c`; removed = wider, stronger red part |
| Halation amount, threshold, spread, hue | Amount, Threshold, Spread, Hue | A | `yh` hal_q8, knee, gauge reach, colour |
| Bloom profiles and controls | Bloom Off / Low / Normal / Strong + Amount, Threshold | H + A | `yh_halo.c` bloom part |
| - (ours) | Mist White / Black 1/8..1 | H | `wf_mist.c` (3.8) |
| Strength | Strength 0..150 % (camera 30 / 100 / 125) | H | the table build's deviation scale |
| Vignette | Vignette amount, size, feather | A | print burn-in in linear light, our own |
| Film rebate | Film rebate with our own edge text | L | own art; the contact sheet already draws one |
| Film damage (dust, hair, scratches) | - | L | own procedural, later if wanted |
| Film breath, gate weave | - | S | video only |
| LUT export | look as .cube | L | desktop has it (A) |
| Histogram, undo / redo, hold for before, 200 % | histogram, undo, split + hold, 1:1 grain check | A | app |
| Crop | crop, straighten, 3:2 / 4:3 / 1:1 / 65:24 / 16:9 | A | before the look |
| Batch edit / export, projects | a roll is the project; Copy look to the roll; the Darkroom develops a whole roll | H + A | roll + Darkroom (3.6, 3.15) |
| Print finishes | Frame, Info caption, Date, as the camera | H | `wf_print.c` (3.9) |
| Log outputs, Log + LUT | Output Film / Log / Log + LUT (the user's cubes) | H | `wf_loglut.c` |
| **Camera (ours)** | live film viewfinder, DNG + JPG, manual ISO / shutter / WB / focus / EV, lenses | A | Camera2 + GLES 3 (section 4) |
| **Roll (ours)** | 3..36 frames, film locked, Blind roll, shelf, develop again | H | `wf_roll.c` semantics, `WFROLL.TXT` format |
| **Camera card import** | EOS M / 5D2 CR2 + JPG + rolls over OTG, develop again on the phone | A | `wf_cr2.c` + B, bit-exact with the camera |

Film stage keeps its 44 negatives and 15 offered prints and its nine knobs (Film exp, Push / pull, Print exposure,
Printer R / G / B, Interlayer, Bleach bypass, Print bleach, Neutral neg / print) and gains Dev contrast, the CMY view
of the printer lights, Print range, the continuous grain / halation / bloom controls and Exposure / Kelvin / Tint in
front (mock 05).

## 4. Architecture

```
Camera2 (PRIV preview <= 1080p + RAW_SENSOR max + JPEG max)
   |                     \                          \
   | OES texture          \ RAW at the shutter       \ the phone's JPEG (kept as the "camera JPEG")
   v                       v
GLES 3 preview         DngCreator -> DCIM/WindyFilm/WF_nnnn.DNG (async)
(baked film cube,          |
 grain, glow)              v
   |                  libwfcore (NDK, our C core) <- CR2 + JPG imported from the EOS M / 5D2 card
   |                  read DNG / CR2 -> develop B -> look stage (table, grain, halation, bloom, mist)
   v                  -> print finish -> wf_jpe -> Pictures/WindyFilm/ROLLnnn/Fnn_xxxx.JPG
viewfinder                 ^
                      roll / shelf / Darkroom (rolls, frames, passes; WFROLL.TXT semantics)
```

- **Stack: Kotlin + Jetpack Compose (UI), Camera2 directly, GLES 3.0 on its own EGL thread, the C core through the
  NDK (CMake, clang, arm64-v8a; x86_64 for the emulator and host tests), a thin JNI layer.**
  - Why native Kotlin: the camera, the GL preview and the NDK are Android APIs; a cross-platform UI layer (Flutter,
    React Native) would still need all three natively and adds a second runtime. What the apps share is the C core,
    the option model and the sidecar format, not UI code.
  - Why Camera2 and not CameraX: we need RAW + PRIV + JPEG in one session, physical lenses, manual sensor control
    and our own preview surface; CameraX 1.5 can do RAW but RAW beside a GPU effect is undocumented. CameraX can come
    back later for the device quirks it hides.
  - minSdk 29 (Android 10: scoped storage, mature Camera2), target the current SDK.
- **The core on the NDK.** The same `src/` files the wasm Darkroom already builds (`darkroom_web_src` kit:
  `wfdark.c` + shims for the camera services, bit-exact with the camera: ref17 59/59 MATCH): `wf_cr2.c`, `wf_dev.c`
  (B), `wf_film.c`, `wf_fx.c`, `yg_grain.c`, `yh_halo.c`, `wf_mist.c`, `wf_loglut.c`, `wf_stagek.c`, `wf_print.c`,
  `wf_jpe.c`, `wf_roll.c` logic. The armv5t `.S` kernels are replaced by their C paths (as in wasm). New:
  - `wf_dng.c` (A): reader for our own DNGs (TIFF IFDs, uncompressed 16-bit CFA, black / white, ColorMatrix /
    ForwardMatrix / AsShotNeutral, the GainMap opcode for lens shading; WarpRectilinear later).
  - B with the DNG's matrices instead of Canon's; the rest of the develop and the look stage unchanged.
  - Threads: bands of the frame across 4 big cores (the camera runs one band at a time; the band code is reentrant
    per frame, checked in stage 2).
- **Pixels: the same as the camera and the desktop, within a stated tolerance.**
  - A CR2 from the EOS M / 5D2 developed on the phone = the camera's Darkroom print for the same film and settings:
    **bit-exact target (0 codes), accepted at max 1 code on any channel, mean < 0.01**, checked by FNV / per-pixel
    compare against the qemu / wasm references on the ref17 frames (stage 2 gate).
  - A phone DNG has no camera print to compare with; phone = desktop core on the same DNG, bit-exact.
  - The live preview is **not** the print (the phone's ISP picture, a baked cube, approximated grain / glow): target
    mean dE00 <= 2.5, p95 <= 5 against the print of the same frame, measured in stage 2.
- **Live preview on the GPU.**
  - Exact part: the core's look kernel is run on the CPU over a 33^3 RGB grid whenever the film or a knob changes
    (Strength, Highlights / Shadows, Paper, Print, Film stage compose by `wf_stagek.c`, Light gains), and the result is
    uploaded as one 3D texture; the shader samples it (tetrahedral). The preview's colour is then the core's kernel up
    to interpolation, with no second colour implementation to keep in step.
  - Approximated: grain = a pre-made noise texture per gauge / size, amplitude per level from the `yg` table; halation /
    bloom = dual-filter blur at 1/4 resolution with the `yh` knee and colours; mist = one more blur level.
  - Tiers: Full (all), Fast (cube only), Off (the phone's picture). Film LUT 65^3 cubes are resampled to 33^3 for it.
- **Roll, shelf, Darkroom** (DESIGN 3.6, 3.14, 3.15): one roll in the camera, 3 / 6 / 12 / 24 / 36 frames, its film
  locked, Blind roll hides the frames until developed, a finished roll goes on the shelf, the Darkroom develops a roll
  (a foreground service with progress, safelight red on the run screen only), develop again with another film
  (passes B..Z). Modes on the phone: **Free** (no roll: each frame developed right after the shot) and **Roll** (its
  frames develop at the shot or later in the Darkroom: the camera's RAW develop / Darkroom choice). The app keeps its
  rolls in its own database; a camera roll file `WFROLL.TXT` is read on import with the camera's semantics.
- **Camera card import** (mock 07): SAF tree on the card; finds `DCIM/*CANON`, `ROLLS/ROLLnnn*/WFROLL.TXT`, `ML/DATA`;
  copies (or reads in place) the CR2 + JPG of the chosen rolls; never writes to the card; offers the card's `ML/DATA`
  as the user's film data folder.
- **Gallery, export, share:** prints go to `Pictures/WindyFilm/ROLLnnn/Fnn_xxxx.JPG` (the camera's names), q95, the
  camera's UserComment text ("WindyFilm Darkroom: <look> | ROLL 004 F13"), EXIF from the DNG / CR2; the DNG stays in
  `DCIM/WindyFilm` (MediaStore Images may refuse `image/x-adobe-dng` on some phones: Documents as the fall-back);
  Android share sheet; a contact sheet as one JPEG; the sidecar `.wfx` per picture.

## 5. Performance targets (mid-range: Snapdragon 7s Gen 2 / Dimensity 7300 class, 8 GB)

| what | target | note |
|---|---|---|
| viewfinder, Full look | 30 fps steady at 1080 x 1440, GPU <= 10 ms a frame | Fast tier: 60 fps |
| film or knob change in the viewfinder | <= 100 ms (Film stage compose <= 150 ms) | 33^3 bake on the CPU + upload |
| camera start | <= 700 ms | MPC figure |
| shot to shot, DNG + JPG | <= 1 s | DNG written on an IO thread |
| develop one frame (B + look + print + JPEG) | <= 2 s for 12 MP DNG, <= 3 s for an 18 MP CR2 | 4 threads; the camera takes 50-60 s |
| Darkroom, 36 frames | <= 90 s, screen may sleep | foreground service; pauses at thermal SEVERE |
| memory while developing | <= 400 MB | raw 33 MB + frame 36 MB + buffers per frame in flight |
| APK | <= 30 MB | core, the public film tables, 97 cans, fonts |

All of these are targets to measure in stage 2, not measurements.

## 6. Risks

- **Device RAW support** varies: RAW only on the main lens on many phones, binned RAW on quad Bayer sensors, lens
  shading maps missing on some, UBWC-compressed buffers on Adreno. The app probes each camera at start (Settings
  "This phone") and falls back to the phone's JPEG with the film on it (the camera's "JPEG 8-bit" path).
- **exFAT cards** may not mount on some phones (the EOS M uses exFAT on SDXC): import says so and points to a card
  reader app or a FAT32 card.
- **Preview != print**: shown honestly (Develop has the true print; "Hold for before" there), the dE target above.
- **Private data**: read through SAF only; derived caches (composed tables, cube caches) in app-private no-backup
  storage; `allowBackup=false` and data-extraction rules exclude them; never in a share, an export or a log. A build
  gate (like `tools/provenance_gate.py`) scans the APK for `.DAT` / `.CUB` names and the private files' hashes.
- **Licences**: the app is GPL-3.0-or-later like the core (AndroidX Apache-2.0 is compatible); Ubuntu Sans (UFL 1.0)
  ships with its licence; the spektrafilm-derived tables and the 4 spektrafilm Film LUT cans (CC BY-SA 4.0) ship with
  attribution in About; the other 19 cans' cubes are the user's own files. The Even grain and Wrap halation constants
  in `src/yg_grain.*` / `src/yh_halo.*` are Genesis-derived (DESIGN 1a, published with permission): whether that
  permission covers a store-distributed app is the owner's call; a build flag can leave those two choices out.
- **Names**: the cans carry real stock names, as on the camera; in a store listing that is a trademark question for
  the owner.
- **Thermal and battery**: a 30 fps shader plus long Darkroom runs; Fast tier and a thermal-status listener.
- **Two places to keep in step**: the C core is the single source; the preview bakes from it, so a core change needs
  no shader change (except grain / glow approximations, checked against prints in a host test).

## 7. Staged plan

1. **Stage 1 (this):** research, feature map, the shared option model, 8 mockups, this plan. Owner approval.
2. **Stage 2, core + preview skeleton:** the core as `libwfcore.so` (arm64 / x86_64) with the wasm kit's shims; host
   test: the ref17 CR2 frames give the camera's prints (bit-exact gate, section 4); a bare Kotlin app with Camera2
   PRIV + RAW + JPEG, the GL preview with the baked cube, the shutter writing DNG. Measure section 5 on the owner's
   phone(s).
3. **Stage 3, develop + roll:** `wf_dng.c` and B on DNG; Free mode; rolls (film lock, Blind, exposures), the shelf, the
   Darkroom service, the contact sheet, gallery, export, share.
4. **Stage 4, Develop tools:** the H rows everywhere, then the A controls in the option model's order (Light K / Tint,
   Exposure, grain / halation / bloom continuous, Dev contrast, vignette, crop), recipes, sidecar, undo, split / hold,
   1:1.
5. **Stage 5, Film stage + user data + card:** the data folder (SAF), Film stage and its knobs and prints, the Film
   LUT cans from the user's cubes, camera card import, develop again.
6. **Stage 6, beta:** a device matrix (the owner's phones + 3-4 common mid-range models), perf / thermal, packaging
   (APK / F-Droid / Play: the owner decides), the README languages (vi / en / ja / ko / zh).

## 8. Mockups

Sources `tools/apps/android_mocks/` (HTML + `wfm.css`, `mk_assets.py` gathers the inputs, `render.sh` = Edge headless
at 360 x 800 x3, `mk_sheet.py` = the overview). PNGs, 1080 x 2400, in `F:\MagicLatern\_windyfilm_docs\android_mocks\`:
`00_tong_quan.png` (overview), `01_viewfinder`, `02_shelf`, `03_roll`, `04_develop` (Glow tab), `04b_develop_film`
(Film tab), `05_filmstage`, `06_settings`, `07_import`. The photos in them are real renders of the WindyFilm core on
the owner's EOS M CR2s (the look-sweep galleries); the Film stage strip uses the private chain renders, so the PNGs
stay private.

## 9. Open questions for the owner

1. OK that the camera (live film viewfinder, RAW, roll) is our own design and the reference plugin gives only the
   depth of the editing options?
2. "Film shelf" (the cans, mock 02) vs the camera's "shelf" (rolls waiting, 3.15): keep both words, or rename the cans
   screen?
3. Default mode: Roll with 36 frames and Blind on (as mocked), or Free?
4. Live preview default: Full (30 fps, more battery) or Fast?
5. Develop screens stay neutral grey for colour judgement; the safelight red only on the Darkroom run screen. OK?
6. The Genesis-derived Even grain / Wrap halation in a store-distributed app: covered by the permission, or leave
   them out of app builds?
7. Distribution: APK only, F-Droid, or Play Store (and the stock names on the cans there)?
8. Which phones first (your own models), and minSdk 29?
9. Which NEW options for v1 (Kelvin / Tint / Exposure, Dev contrast, Halation amount / threshold, grain amount / size,
   vignette, crop) and which later?
10. Ship the 4 spektrafilm Film LUT cans inside the app, or all 23 cans from your folder only?
