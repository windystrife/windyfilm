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
| Film development (contrast, gamma, colour separation) | Dev. contrast, Contrast centre, Colour purity; Saturation (gamut-safe) | A | the negative's density scale about a movable grey (Film stage), N curve (Film); purity = a chroma knee after the table (Film stage: Interlayer) |
| Highlight compression (white point, range, colour) | Highlights / Shadows; Shoulder white, Shoulder knee, Highlight colour | H + A | `wf_hs_bend` (3.3); B shoulder `j.roll` as parameters; chroma inside the shoulder |
| Black / white points | Black level, White level, Levels on colour / luma; Print range pinned / natural | A | output levels after the print; the chain's pinned ends (DESIGN 1a) |
| Print stock, print exposure | Print stock (Scan / Cine / Paper; Film stage 15 prints), Print exposure (Exp comp) | H | `WFFILMS.DAT` variants, `wf_stagek.c` print half |
| Print contrast, colour density | Print contrast, Dye density | A | the print stock's curve gamma about its mid density; channel density away from neutral |
| Print paper, target white | Paper Glossy / Matte / Warm / Cool; Printing lamp 5000..6500 K (Film stage prints) | H + A | `wf_paper` (3.10); lamp K -> printer points R / B, computed at run time from the user's `WFSTAGE.DAT` |
| Printer lights (CMY), linked, keep exposure | Printer lights R / G / B (+-12 points), shown as RGB or CMY, Linked, Keep brightness | H + A | `wf_stagek.c` printer timing; CMY = a view of the same numbers; Keep = the mean goes to Print exposure |
| Split toning (shadows / mids / highlights) | Split tone: Shadows / Mids / Highlights warm..cool, Amount | A | temperature offsets in soft tone bands after Levels, our own |
| Grain profiles (gauge x ISO) | Grain Off / Fine / Normal / Strong / Even x3, Gauge 16 / 35 / 65, Grain speed (film's own / 50..3200) | H + A | `yg_grain.c`, Gauge row (3.7), `yg_stocks.c` parameters per speed |
| Grain size, amount, chroma, per-tone | Amount, Size, Colour, Tones | A | the same `yg` parameters, continuous |
| Halation presets, without the anti-halation backing | Halation Off / Wide / Tight / Wrap; Backing on / removed | H + A | `yh_halo.c`; removed = wider, stronger red part |
| Halation amount, threshold, spread, hue | Amount, Threshold, Spread, Hue | A | `yh` hal_q8, knee, gauge reach, colour |
| Bloom profiles and controls | Bloom Off / Low / Normal / Strong + Amount, Threshold | H + A | `yh_halo.c` bloom part |
| - (ours) | Mist White / Black 1/8..1 | H | `wf_mist.c` (3.8) |
| Strength | Strength 0..150 % (camera 30 / 100 / 125) | H | the table build's deviation scale |
| Effect-only view | Effect alone (Grain, Glow tabs) | A | a view switch, never saved |
| Vignette | Vignette Amount, Size, Feather, Roundness, Centre | A | print burn-in in linear light, our own |
| Film rebate | Film rebate: Format (off / 35 mm / half frame / Super 8 / 16 mm / cine 4-perf / 6x4.5 / 6x6 / 6x7 / 6x9 / 4x5), Corners, Sprocket holes, Film runs, Edge light, Rebate shown, Picture size, Position, Edge softness, Mirror, Edge print, Same on every frame | A | our own drawn rebates from public format sizes, edge print in our font (`mk_wffont.py`), never scans |
| Film damage (dust, hair, scratches) | Dust & scratches: Look clean / light / used / old, Amount, Dust, Hairs, Scratches, Vary per frame | A | own procedural marks seeded by roll + frame, white on a print from a negative, dark on a slide |
| Film breath, gate weave (video) | Frame variation (each frame of a roll), Frame drift (the picture in the rebate) | A | stills meanings, seeded by roll + frame; a video meaning only if phone video comes |
| LUT export | look as .cube | L | desktop has it (A) |
| Histogram, undo / redo, hold for before, 200 % | histogram, undo, split + hold, 1:1 grain check | A | app |
| Crop | crop, straighten, 3:2 / 4:3 / 1:1 / 65:24 / 16:9 | A | before the look |
| Batch edit / export, projects | a roll is the project; Copy look to the roll; the Darkroom develops a whole roll | H + A | roll + Darkroom (3.6, 3.15) |
| Print finishes | Frame, Info caption, Date, as the camera | H | `wf_print.c` (3.9) |
| Log outputs, Log + LUT | Output Film / Log / Log + LUT (the user's cubes) | H | `wf_loglut.c` |
| **Camera (ours)** | live film viewfinder, DNG + JPG, manual ISO / shutter / WB / focus / EV, lenses | A | Camera2 + GLES 3 (section 4) |
| **Roll (ours)** | 3..36 frames, film locked, Blind roll, shelf, develop again | H | `wf_roll.c` semantics, `WFROLL.TXT` format |
| **Camera card import** | EOS M / 5D2 CR2 + JPG + rolls over OTG, develop again on the phone | A | `wf_cr2.c` + B, bit-exact with the camera |

**Owner, 21 Nov: every option on the plugin's tool panel goes in, desktop and phone**, each as our own control (option
model section 12; the phone's live-preview split in section 13). Still S: the skin mask (detection, outside
WindyFilm). Still L: micro-contrast, defringe (not on that panel), LUT export and print sizes on the phone.

Film stage keeps its 44 negatives and 15 offered prints and its nine knobs (Film exp, Push / pull, Print exposure,
Printer R / G / B, Interlayer, Bleach bypass, Print bleach, Neutral neg / print) and gains Dev contrast, Contrast
centre, the CMY view of the printer lights with Linked / Keep brightness, Printing lamp, Print contrast, Dye density,
Split tone, Print range, the continuous grain / halation / bloom controls and Exposure / Kelvin / Tint in front (mocks
05, 08).

Develop on the phone has **8 tabs**: Film · Print · Tone · Grain · Glow · Lens · Gate · Finish (mocks 04, 04b, 04c,
08..12). Grain also holds Dust & scratches; Gate = Film rebate and a Per frame group (Same on every frame, Frame drift,
Frame variation).

**Owner, 21 Nov (later): the owner's own film model drives Tone, Grain and Glow.** The owner has a measured model of
five stages in this order: Developer → Compression → Bloom → Halation → Grain. Its numbers and code are private: they
live in the private desktop repository only, never in this repository or in a public build. The app reads the stages
from the shared C core.
- Tone, Grain and Glow each get an **Engine: Lab | Camera** switch. This is a proposal, temporary, and question 14.
  - Lab is the owner's model and the default.
  - Camera is the film's own camera-side tools, bit-exact with the camera: the controls in the rows above.
  - Levels, Print range and Highlight colour show under both engines.
- Tone (Lab) has two groups:
  - **Developer**: Contrast, Midtone lift, Colour purity (0..200 %), Lift & toe.
  - **Shoulder**: Shoulder, Shoulder white, Shoulder width, Colour mute.
  - Saturation leaves Tone, because Colour purity covers it.
- Grain (Lab): **gauge chips 8 / 16 / 35 / 65 mm**, which set Bloom, Halation and Grain to that gauge at once. Then
  Amount, Tones S / M / H, Colour, Size, Clump, Angle, Seed (+ per frame), and Dust & scratches below.
- Glow (Lab): a segmented control picks **Halation · Bloom · Mist**, each with its own switch.
  - Halation: Amount, Hue red↔orange, Source gamma, Tight spread, Wide spread, Threshold, Tone range low / high,
    Blue cut, Softness.
  - Bloom keeps the light's own colour: Amount, Threshold, Hard edge, Brightness, Spread, Falloff, Layers, Size,
    Saturation, Bright roll-off, Edge guard, Edge reach, Edge detail.
  - Mist: White / Black, as before.

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
  - The panel's per-pixel controls (Developer, shoulder, Saturation, Print range, Levels, Printing lamp, Print
    contrast, Dye density, Keep brightness, Split tone) and Frame variation fold into the same cube: exact up to the
    cube. While a slider is dragged a 17^3 cube is baked (<= 8 ms), the 33^3 one on release (<= 60 ms, a worker thread;
    the GPU keeps the old cube until the new one is up).
  - Per-frame draws use the seed of roll + frame; the viewfinder shows the **next** frame's (Frame variation, Frame
    drift, dust): what you see is the frame you get. Free mode seeds with the shot counter.
  - Overlays drawn by the core at preview size when a setting changes (<= 30 ms): the film rebate (exact shapes; Edge
    light and Edge softness blurs at preview size) and the dust marks (exact positions, thin marks at least 1 px). The
    shader places the picture in the rebate (Frame drift = an affine move, bilinear) and computes the vignette with the
    core's formula in float.
  - Approximated (Camera engine): grain = a pre-made noise texture per gauge / size / speed, amplitude per level from
    the `yg` table; halation / bloom = dual-filter blur at 1/4 resolution with the `yh` knee, colours and amounts;
    mist = one more blur level.
  - Lab engine (the owner's model): the shaders are written and measured in a real browser GPU against the model's
    reference code. Both live in the private desktop repository, branch `android-gpu`.
    - Developer and Compression fold into the cube: 33^3 while dragging, 65^3 on release.
    - Bloom and Halation: one blur per layer at a reduced size, plus a full-size edge mask.
    - Grain: the model's noise made in the shader. It is checked by its statistics, because a phone GPU's float maths
      cannot repeat the reference's bits.
    - The preview is approximate. **The saved print always comes from the C core.**
    - The phone needs `EXT_color_buffer_half_float`. All current GLES 3.x phones have it.
  - Tiers: Full (all), Fast (the cube + vignette + rebate: no grain / glow / mist / dust), Off (the phone's picture,
    with a thin frame line when a rebate is on). Film LUT 65^3 cubes are resampled to 33^3 for it. Details per key:
    option model section 13.
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

### 4.1 Foldable phones (owner, 21 Nov: the owner shoots on a Galaxy Z Fold3)

The Fold3:
- Cover screen 6.2", 2268 x 832, about 317 dp wide.
- Inner screen 7.6", 2208 x 1768, with an under-display camera.
- Snapdragon 888. It shipped with Android 11 and now runs One UI 6.1.
- Samsung's own Expert RAW app runs on it, so its main camera gives RAW. What Camera2 offers a third-party app (RAW
  per lens, hardware level) is checked on the phone first, in stage 2.

The app supports foldables from the first build:
- **No orientation lock, resizable.** The app is never letterboxed. The layout follows the window size class:
  - Compact (cover screen, or the phone folded): the phone layout of the mocks. Mock 16 checks the 317 dp width.
  - Medium or Expanded (inner screen: 673 dp wide upright, 841 dp sideways): the viewfinder with its controls in a
    side column, and Develop in two panes (the picture and the roll on the left, the tabs on the right). Mocks 13
    and 14.
- **Tabletop (Flex mode).** Jetpack WindowManager's `FoldingFeature` reports half open with a horizontal hinge. The
  viewfinder moves above the hinge and the shutter, settings and roll counter move below it. Nothing sits on the
  hinge band. Mock 15.
  - Book posture (vertical hinge) keeps the two-pane layout and keeps controls off the hinge.
- **Folding or unfolding mid-shot keeps everything.**
  - The camera keeps running: it feeds our own texture, so only the screen surface is replaced.
  - The roll and its frame counter carry on.
  - Develop keeps its edit, tab and scroll position.
- Each screen's rotation is read on its own. Cutout and under-display-camera insets are respected, so nothing is
  drawn under the inner camera.

## 5. Performance targets (mid-range: Snapdragon 7s Gen 2 / Dimensity 7300 class, 8 GB)

| what | target | note |
|---|---|---|
| viewfinder, Full look | 30 fps steady at 1080 x 1440, GPU <= 10 ms a frame | Fast tier: 60 fps |
| film or knob change in the viewfinder | <= 100 ms (Film stage compose <= 150 ms) | 33^3 bake on the CPU + upload |
| slider drag in Develop | <= 8 ms a step (17^3 bake), 33^3 on release | the picture follows the finger |
| rebate / dust overlay redraw | <= 30 ms at preview size | only when a gate / dust setting or the frame changes |
| extra GPU cost of rebate, dust, vignette, drift | <= 1 ms a frame at 1080p | two texture reads + math |
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

1. **Stage 1 (this):** research, feature map, the shared option model, 18 mockups (14 phone screens, 4 foldable), this plan. Owner approval.
2. **Stage 2, core + preview skeleton:** the core as `libwfcore.so` (arm64 / x86_64) with the wasm kit's shims; host
   test: the ref17 CR2 frames give the camera's prints (bit-exact gate, section 4); a bare Kotlin app with Camera2
   PRIV + RAW + JPEG, the GL preview with the baked cube, the shutter writing DNG. Measure section 5 on the owner's
   phone(s): the **Galaxy Z Fold3** first. Steps on it:
   - a Camera2 probe of every lens;
   - fold and unfold while the camera runs;
   - tabletop;
   - the Lab-engine shaders' frame time on its GPU.
3. **Stage 3, develop + roll:** `wf_dng.c` and B on DNG; Free mode; rolls (film lock, Blind, exposures), the shelf, the
   Darkroom service, the contact sheet, gallery, export, share.
4. **Stage 4, Develop tools:** the 8 tabs; the H rows everywhere, then the A controls in this order: Light K / Tint,
   Exposure; Developer, shoulder, Saturation, Print range, Levels; Print contrast, Dye density, Keep brightness, Split
   tone; grain speed / amount / size, halation / bloom amounts, Effect alone; vignette, crop; then Dust & scratches,
   Film rebate (all formats, edge print), Frame variation, Frame drift. Recipes, sidecar, undo, split / hold, 1:1.
   Each control lands in the core (C, the camera's and desktop's too) and in the preview split of section 4 together.
5. **Stage 5, Film stage + user data + card:** the data folder (SAF), Film stage and its knobs and prints, Printing
   lamp (needs the user's `WFSTAGE.DAT`), the Film LUT cans from the user's cubes, camera card import, develop again.
6. **Stage 6, beta:** a device matrix (the owner's phones + 3-4 common mid-range models), perf / thermal, packaging
   (APK / F-Droid / Play: the owner decides), the README languages (vi / en / ja / ko / zh).

## 8. Mockups

Sources `tools/apps/android_mocks/` (HTML + `wfm.css`, `dev.js` = the shared Develop chrome of 08..12, `mk_assets.py`
gathers the inputs, `render.sh` = Edge headless at 360 x 800 x3, `mk_sheet.py` = the overview). PNGs, 1080 x 2400, in
`F:\MagicLatern\_windyfilm_docs\android_mocks\`: `00_tong_quan.png` (overview), `01_viewfinder`, `02_shelf`,
`03_roll`, `04_develop` (Glow tab), `04b_develop_film` (Film tab), `05_filmstage`, `06_settings`, `07_import`,
`08_develop_print`, `09_develop_tone`, `10_develop_grain` (with Dust & scratches), `11_develop_lens`,
`12_develop_gate` (Film rebate, Per frame). The photos in them are real renders of the WindyFilm core on the owner's
EOS M CR2s (the look-sweep galleries); the Film stage strip uses the private chain renders, so the PNGs stay private.
The rebate in 12 and the dust in 10 are drawn by the mock itself (our own shapes from public format sizes).

Redrawn on 21 Nov (later) for the owner's film model:
- `04_develop` (Glow · Halation), `04c_develop_bloom` (Glow · Bloom), `09_develop_tone` and `10_develop_grain`.
- Their pictures (`assets/lab/*.png`) are the phone preview's own shader output, made by the private repository's
  browser test (`run_edge.mjs --render`). `mk_assets.py` does not make them.
- The slider values are illustrative, not the model's presets.

Foldable screens (`<meta name="wf-size">` sets the window; `render.sh` reads it):
- `13_fold_viewfinder`, `14_fold_develop`, `15_fold_tabletop`: 841 x 673 dp.
- `16_fold_cover`: 317 x 864 dp.
- The overview sheet has them in their own row.

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
9. Everything on the panel goes in (your 21 Nov note). The order inside stage 4 (section 7: tone and print controls
   first, then dust, rebate and per-frame variation): OK, or something first?
10. Ship the 4 spektrafilm Film LUT cans inside the app, or all 23 cans from your folder only?
11. 8 Develop tabs on a phone (Film · Print · Tone · Grain · Glow · Lens · Gate · Finish), or fold Lens (vignette)
    into Gate for 7?
12. Viewfinder with Frame variation / Frame drift / dust on: show the next frame's draw (what you see is what you
    get, mock 01 would change per shot), or keep the viewfinder clean and show them only in Develop?
13. Edge print text on the rebate: "WINDYFILM · film name · frame number" (mock 12), or something else (roll number,
    your name)?
14. Tone, Grain and Glow get an **Engine: Lab | Camera** switch.
    - Lab is your film model: Developer, Shoulder, Bloom, Halation and Grain with your numbers.
    - Camera is the film's own camera-side tools, bit-exact with the camera.
    - Proposed, and taken for now: Lab as the default. Or should Camera be the default for frames shot on the EOS M
      / 5D2?
15. On the Fold3, the viewfinder on the inner screen: sideways with the camera column on the right (mock 13), or
    upright like a big phone?
16. Tabletop (mock 15): only the camera, or Develop too (picture above the hinge, the tool panel below)?
