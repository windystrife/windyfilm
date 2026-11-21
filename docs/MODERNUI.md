# WindyFilm modernUI: the film OS (stage 1 + M1, 21 Nov 2026, branch `modernUI`)

M1 (21 Nov 2026, chủ dự án duyệt mock): đã có Home, Cài đặt và công tắc "Camera interface". Khi đang chụp, phím MENU mở
Home thay cho menu Canon; MENU trên Home mở menu Canon; menu ML vẫn mở bằng chạm hai ngón (EOS M) hoặc TRASH trên Home
(5D2). Chưa thử trên máy: chưa build thẻ. Trạng thái, cách thử và các quyết định ở mục 6 (M1).

Tóm tắt stage 1 (bản mock): WindyFilm thay menu ML và phần lớn màn hình Canon bằng giao diện riêng: Home, Kệ phim, HUD
LiveView, Top plate (5D2 chụp qua ống ngắm), PLAY xem ảnh in và tờ tiếp xúc, Cài đặt; Darkroom giữ nguyên RAWB28. Menu
ML luôn mở được, và có một nút tắt để về giao diện Canon + ML như cũ. Ảnh mock:
`F:\MagicLatern\_windyfilm_docs\modernui_mocks\` (`modernui_eosm.png`, `modernui_5d2.png`, `index.html`). Các câu hỏi
cần chủ dự án chọn ở mục 7.

The owner saw M-Film (HNGSeries, GPL-2.0), which replaces the Canon / ML interface with a film-camera interface, and
asked for a WindyFilm variant in the same spirit on its own branch. The concept (a film-camera interface over ML) is
generic; nothing of M-Film is used: no code, art, fonts, layout or drawings (DESIGN 1 rules apply). How ours differs
is in section 2.

## 1. The mockups

- **How they are made.** They are real UI code on the PC harness, not drawings.
  - `tests/ui/modernui/os_mock.c` draws every screen with the browser's own code: `src/ui/wf_gfx.c` (canvas,
    primitives, gradient / glow / floor shadow, the hardware palette), `wf_font.c` (Ubuntu Sans small text) and
    `wf_cans.c` (the WFCANS.DAT cans).
  - The display fonts come from `tests/ui/modernui/mk_osfont.py` (WFOSFONT.DAT, the WFFONTS.DAT format).
  - Output: 720 x 480 in ML's 8-bit palette range 0x50..0xFE.
  - `os_comp.py` puts the photo layer under the bitmap: the stand-in LiveView frame, the print, the contact-sheet
    thumbnails. On the camera that layer is LiveView or the display's YUV buffer, and palette index 0 is transparent.
    The two scrim entries are blended at 50 % / 68 %; on the camera they are palette entries with partial opacity
    (risk R3).
- **Run.** `bash tests/ui/modernui/os_run.sh` in WSL. The Darkroom screens come from `tests/ui/dr_run.sh`, run first
  with `W=/root/ml/wf_modern/dr` and `CAM=5D2 W=/root/ml/wf_modern/dr5d2`.
  The photos are the owner's and stay outside the repo: `PHOTOS` = the apps hand-off assets (`mk_app_assets.py`
  output), `LV` / `LV5` = camera JPEGs.
- **Not in the module build.** `build.sh` compiles `src/ui/*.c`; the mock lives in `tests/ui/modernui/` until the owner
  approves it.

| # | Screen | EOS M | 5D2 | What it shows |
|---|---|---|---|---|
| 01 | Home | yes | yes | the loaded roll (can, ROLL nnn, film, chips, frames shot / 36, frames left, the 36-tick gauge), four paths (Film shelf, Darkroom, Prints, Settings), the roll's look strip |
| 02 | Home, Darkroom focused | yes | yes | the focus style (amber tint + outline) |
| 03 | Film shelf | yes | yes | the picked can big at the left with name, chips, description; three shelves of four cans at the right; section pill Film / Film stage; group tabs; green dot = the film in the camera |
| 04 | LiveView HUD, Full | yes | yes (LV) | film pill + roll, battery + RAW, 3:2 frame corners, shutter / aperture / ISO (FILM tag when the film set it), the exposure scale, the next frame number + gauge |
| 05 | LiveView HUD, Minimal | yes | yes (LV) | the film pill and the frame number only |
| 06 | Top plate | - | yes | the 5D2 shooting through the viewfinder: the back screen in place of Canon's shooting-settings screen (shutter, aperture, ISO from film, exposure, mode, the look, next frame, card, the roll with its can and gauge) |
| 07 | PLAY: print | yes | yes | the developed print from ROLLS (frame + caption as written), the roll, count, gauge, file, shot data |
| 08 | PLAY: contact sheet | yes | yes | the roll on film strips with frame numbers in the rebate, the picked frame bigger with its data |
| 09 | Settings | yes | yes | Interface section: Camera interface WindyFilm / Canon + ML, MENU key, PLAY shows, LiveView HUD, Quick keys (EOS M) / Top plate (5D2), Blind roll, the ML menu key (fixed) |
| 10 | Darkroom | yes | yes | RAWB28 unchanged (photos on the easel, Camera JPEG 4 choices, safelight theme) |
| 11 | Darkroom developing | yes | yes | RAWB28 unchanged |

## 2. Visual identity

- **Colours.** The browser's palette (DESIGN 5) is the base, so the film OS, the browser and the Darkroom read as one
  product.
  - Ink: background gradient (24, 27, 33) -> (10, 11, 14). Surfaces C_FRAME / C_SEP / C_TRACK. Hairlines C_LINE /
    C_SEP.
  - Text: (236, 238, 240), (146, 155, 168), (98, 106, 118).
  - One accent, WindyFilm amber (244, 166, 52): focus, the next frame, the roll number, the FILM tag. Teal = Film
    LUT, green = loaded / developed, red = warning.
  - The film's own colour appears only as the glow behind its can, and on the HUD as one recoloured dot.
  - The Darkroom keeps its red safelight theme (RAWB10..28).
- **Type.** Ubuntu Sans (UFL 1.0) everywhere, as the browser.
  - The existing 13 / 15 px pack for small text.
  - New display sizes (mk_osfont.py): 20 px / 600 titles, 30 px / 500 film names, 46 px / 400 exposure values, 80 px
    / 300 the frame counter, 26 px / 400 HUD values, 12 px / 700 letter-spaced small caps labels.
  - Large light numerals, not segment digits.
- **Motifs.**
  - The **36-tick roll gauge**: one tick per frame, the exposed ones light, the next frame amber and taller. It
    appears on Home, the HUD, the top plate and the prints.
  - Our own **cans** (`cans/gen_cans.py`).
  - **Small caps labels** over values ("SHUTTER", "ROLL 024").
  - Rounded cards (radius 8-14) on the gradient, one-pixel hairlines.
  - The **mark**: an amber tile with a spool whose leader runs out as a gust, and two shorter gusts (our drawing,
    primitives only).
  - Key caps in the footer, with the cameras' real key names.
- **Not M-Film.**
  - Numerals: Ubuntu Sans light, not red seven-segment readouts.
  - Exposure: a tick scale with an amber marker, not a needle.
  - Viewfinder: frame corners only, not an aperture scale.
  - Film shelf: static shelves with the picked can raised and shown big, not cans that turn.
  - Top plate: card tiles, not a segment LCD.
  - Prints: the existing WindyFilm print (frame + caption) and the contact sheet in the rebate style of our Darkroom
    sheet.

## 3. Screen map and keys

```
            power on / half-press anywhere
                        |
        +--------- SHOOTING --------+          EOS M: LiveView HUD (Full / Minimal / Canon's own: INFO)
        |  (HUD in LV; 5D2 OVF:      |          5D2: LV HUD, or the Top plate when shooting through the viewfinder
        |   Top plate)               |
   MENU |              PLAY          | open key (EOS M long DOWN, 5D2 TRASH)
        v                v           v
      HOME ---------> PRINTS     FILM SHELF ---SET load---> load animation -> SHOOTING
       |  \             | SET         ^
       |   \            v             |
       |    \       CONTACT SHEET     |
       |     +--> DARKROOM (RAWB28) --+  (Film row: pick a film for develop again)
       +--> SETTINGS
  MENU on Home: Canon's menu.  The ML menu: EOS M two-finger tap, 5D2 TRASH on Home (Q1). Never taken away.
```

**EOS M (2.0.2)**

Keys: shutter, REC, MENU, INFO, PLAY, the ring (SET in the centre, UP / DOWN / LEFT / RIGHT on it, turning = wheel),
the touch screen. Patch 0002: long DOWN = TRASH. Patch 0003: the module screen take, the two-finger tap.

| where | key | action |
|---|---|---|
| shooting (stills LV) | MENU | Home (instead of Canon's menu) |
| | INFO | HUD Full -> Minimal -> Canon's own |
| | PLAY | Prints when "PLAY shows: Prints", else Canon's PLAY (default: Canon, Q2) |
| | long DOWN | Film shelf (today's open key) |
| | DOWN, SET, ring | unchanged: Quick keys (DOWN ring), Magic Zoom, Canon Q (long SET) |
| | two-finger tap | ML menu (unchanged) |
| Home | UP / DOWN / LEFT / RIGHT | move between the tiles and the look strip |
| | SET | open |
| | ring | step the focused look value |
| | INFO | the look editor (the browser's rows) |
| | MENU | Canon's menu |
| | PLAY | Prints when "PLAY shows: Prints", else Canon's PLAY |
| | half-press | back to shooting |
| Film shelf | LEFT / RIGHT, ring | film |
| | UP / DOWN | shelf row |
| | one-finger tap | group |
| | SET | load (animation, then LiveView) |
| | INFO | about the film |
| | MENU | Home |
| Prints | LEFT / RIGHT, ring | print (past a roll's end: the next roll) |
| | UP / DOWN | newer / older roll |
| | SET | contact sheet |
| | INFO | the whole screen (INFO / MENU back) |
| | PLAY | Canon's PLAY |
| | MENU | Home |
| | half-press | shoot |
| Contact sheet | LEFT / RIGHT, ring | frame |
| | UP / DOWN | the strip above / below |
| | SET, INFO, MENU | that print |
| | PLAY | Canon's PLAY |
| Settings | UP / DOWN | row |
| | ring | value |
| | LEFT / RIGHT | section |
| | MENU | Home |

**5D Mark II (2.1.2)**

Keys: shutter, main dial, rear wheel (quick control dial), joystick (+ press), SET, MENU, INFO, JUMP, PLAY, TRASH,
Picture Style, LV.

| where | key | action |
|---|---|---|
| shooting, viewfinder | (none) | Top plate on the back screen while Canon would show its shooting-settings screen |
| | INFO | Canon's own screen (the plate closes, Canon gets INFO) |
| | MENU | Home |
| | TRASH | Film shelf |
| | PLAY | Prints when "PLAY shows: Prints", else Canon's PLAY (a blind roll says why not) |
| | main dial | Tv / M: shutter, Av: aperture (1/3 stop) |
| | rear wheel | M: aperture, P / Tv / Av: compensation (1/3 stop, -2 .. +2) |
| | half-press | closes it (Canon meters and shoots as ever) |
| shooting, LV | INFO | HUD Full -> Minimal -> Canon's own |
| | MENU / TRASH / PLAY | as above |
| Home | joystick | move |
| | SET | open |
| | main dial / wheel | step the focused look value |
| | MENU | Canon's menu |
| | TRASH | ML menu |
| | half-press | shoot |
| Film shelf | joystick | film / row |
| | dial | film |
| | Picture Style | group |
| | SET | load |
| | INFO | about |
| | MENU | Home |
| Prints | main dial, rear wheel, joystick left / right | print |
| | joystick up / down, Picture Style | roll |
| | SET, joystick press | contact sheet |
| | INFO | the whole screen |
| | PLAY | Canon's PLAY |
| | MENU | Home |
| Contact sheet | joystick, dials | frame |
| | Picture Style | the next roll's sheet |
| | SET, joystick press, INFO, MENU | that print |
| Settings | joystick | row |
| | dial | value |
| | Picture Style | section |
| | MENU | Home |

Why "MENU on Home = Canon's menu" and not "hold MENU": on both cameras MENU arrives as one press event (no release),
so a long press cannot be told apart. MENU twice is the same reach as Canon's MENU, and Home is one key away.

## 4. Where it hooks into ML and Canon

Everything runs in the module's existing paths. No new ML core patch is planned for stage 2; one is needed only if R6
says so.

- **Keys (CBR_KEYPRESS, GUI task).** `wf_main.c`'s handler sees the keys before Canon. While the film OS is on, it
  takes:
  - MENU (EOS M: only the real MENU key, never the two-finger tap; see R5);
  - INFO in LiveView;
  - PLAY when "PLAY shows: Prints";
  - TRASH / long DOWN, as today.
  Everything else passes. The "never a dead camera" rule stays: no UI beat for 2 s means every key goes to Canon / ML
  (DESIGN 5).
  - Canon's menu from Home: close our screen (front buffer back, palette back), then send Canon its MENU with ML's
    `fake_simple_button(BGMT_MENU)` and a pass-through flag. The symbol must be in both card .sym files (checksyms);
    the fallback is `SetGUIRequestMode` with Canon's menu mode.
- **Full screens (Home, Film shelf, Prints, Contact sheet, Settings, Top plate).** These are new `WFUI_SCR_*` screens in
  the browser's UI task (`wf_ui.c`, prio 0x1B), beside BROWSER and DARK, with the same open / close / heartbeat /
  palette code.
  - EOS M: `canon_gui_disable_front_buffer` (patch 0003: Canon's LV Set dialog under us).
  - 5D2: Canon's GUI mode 2 without LV and 38 in LV, as the browser asks today (DESIGN 5), and
    `canon_gui_disable_front_buffer`.
  - The Film shelf can reuse the browser's data and rows. The browser itself stays as the "look editor" behind INFO.
- **LiveView finder (M3, built).** `src/wf_osvf.c` (core side, ML headers) and `src/ui/wf_os_hud.c` (drawing).
  - **When it is up:** Camera interface WindyFilm, the finder not set to "Canon + ML", photo LiveView with ML's
    overlays on (`get_global_draw`), the display on. Also: no ML menu, no WindyFilm screen, no PLAY, no Darkroom run,
    and the HUD font pack held.
  - **INFO stays Canon's.** Canon's INFO switches between its own info view and the clean view, and ML draws (global
    draw) only on the clean view. So INFO switches between Canon's info and our finder, with no key of ours.
  - **ML's two bars are ours** through two lvinfo items (`lvinfo_add_items`), each 720 px wide at priority 127.
    - ML's layout hides every other item of that bar, clears our box and calls us to paint. So the bands change
      exactly when ML redraws its bars and never flicker against them.
    - A value of ours that changes (the count, the meter) asks for a redraw with `lens_display_set_dirty`.
    - Down, the items measure 0 px and ML's own items come back.
  - **The middle** (frame corners, bright lines) is painted from wf_task every 500 ms, at once on a mode change, and
    given back (0) when the finder goes down in plain LiveView.
  - **One lock** (`wf_hud_lock`) covers every painter that points the drawing canvas at ML's bitmap: ML's task (the
    bands), wf_task (middle, roll HUD, notices, font pack) and the UI's open.
  - **Values:**
    - `lens_info` shutter / aperture / ISO numbers. We format them ourselves: ML's `lens_format_*` use its symbol font.
    - The meter is `get_ae_value()` in M (AE_VALUE, 1/8 EV) and the exposure compensation in P / Tv / Av; scene modes
      have none.
    - `battery_level_bars`, `avail_shot` (the card's shots left), and the roll from `wf_roll`.
  - **Colours:** ML's own palette indices only (white, greys, orange, red), with `COLOR_TRANSPARENT_BLACK` for the
    scrims. Nothing writes the palette (R3 avoided).
  - **Modes** are set in Settings > LiveView finder (`finder=` in the config):
    - **Full:** film pill + roll, battery + card count; SHUTTER / APERTURE / ISO (+ FILM tag), a -2..+2 meter scale,
      the frame counter (WIND ON amber for 1 s, END / REWOUND red); four frame corners.
    - **Minimal:** film pill, battery + counter.
    - **Bright lines:** a 3:2 bright-line frame with a focus patch, and one strip (shutter, aperture, three meter
      lamps, frames).
    - **Canon + ML:** ML's own bars as before.
- **5D2 Top plate (no LiveView), built in M4.** `wf_osvf.c` `wf_plate_poll` (wf_task, after the roll HUD) opens the UI
  on `WFUI_SCR_PLATE` when Canon's shooting screen has been up for 300 ms: `!lv`, the display on, gui_state 0
  (GUISTATE_IDLE), `get_gui_mode() == 0`, no ML menu, no PLAY, no Darkroom run, no shot being saved
  (`lens_info.job_state`). With Camera interface WindyFilm and Settings > Top plate On (`plate=on|off`, default on).
  - **Once per screen.** It opens, then waits until that screen goes (display off, LiveView, a menu, PLAY, a review,
    Canon busy) before it can open again. So it comes back after power-on, after the display wakes, after a shot's
    review and after a Canon screen, but never over a screen the owner chose to keep (INFO, or the plate's own idle
    close). For 1.5 s after any close of our UI it does not re-arm (the 5D2's GUI-mode hand-back settles first).
    Turning the option on does not open it over the screen that is up; the next one gets it.
  - It takes the screen as the browser does without LV (GUI mode 2, the 5D2 open path in `wf_ui.c`) and gives it
    back at half-press (`wf_ui_key`). In Canon's MENU mode the dials come to us, not to Canon's exposure (passed on,
    they would only move Canon's hidden menu), so the plate sets the exposure itself (`wf_osvf.c wf_plate_dial`, ML's
    `lens_set_rawshutter` / `lens_set_rawaperture` / `lens_set_ae`, one 1/3 stop a click, a half-stop body 1/2). It
    redraws when a value changes (`plate_tick` sums the values).
  - With no key and no change for 20 s it closes with nothing sent: Canon's screen and Canon's LCD timer again.
  - Values: `wf_osvf_values` (the finder's `lens_info` values plus `mode` and `drive`). Without a half-press the 5D2
    does not meter, so EXPOSURE shows the exposure compensation in P / Tv / Av and the last metered value in M.
  - The roll count after a shot: the half-press closed the plate, the review and the display-on bring it back with the
    new count (no separate 2 s view).
- **PLAY: Prints (M5, built).**
  - **Pictures.** The Darkroom holds each print's rows while it encodes `ROLLS/ROLLnnn/Fnn_nnnn.JPG`, so it also
    writes `Fnn_nnnn.WFP` beside it (`wf_dark.h` `WF_PRV_*`, 156 KB): a 28-byte head (`WFP1`, sizes, where the print
    sits, film, frame, Exif orientation), the contact sheet picture (84 x 56) and the screen picture (336 x 224), both
    UYVY as the encoder's rows, the print fitted in with black bars. `wf_darkp.c wf_dk_vth_*` gathers them as the
    Exif thumbnail is gathered (box means of every second row); they sit in the decoder's free input memory (`M.in`
    after the Exif thumbnail, to 252 KB of 256). A failed write is logged; the print stays.
  - **The viewer** (`src/ui/wf_prints_ui.c`, the UI task) needs no JPEG decoder and no YUV layer: it draws the
    pictures through our palette, in the 103 entries the cans use on the other screens. Each picture gets its own
    colours (median cut over a 4-4-4 histogram), each 5-5-5 colour's nearest entry is found once (a 32 KB table), and
    a 4 x 4 ordered dither smooths the steps. Prints scales the screen picture up (Y bilinear) into 576 x 384, or the
    whole 720 x 480 on INFO. The same on both cameras, with or without LiveView, and the host harness sees all of it.
  - **Orientation.** The print is stored as Canon stores the shot (a portrait shot landscape, Exif orientation 6 / 8),
    so Prints turns it upright. The contact sheet shows the frames as on the film, sideways where the camera was.
  - **The contact sheet** is a roll's frames six to a strip with the frame numbers before them, all in one set of
    colours: every frame is read twice (the colours of all, then each frame through them), a few per tick, so the
    strips show at once and the pictures fill in. It stays in memory while Prints is up.
  - **Memory** (malloc, freed on leave): the picture 147 KB and the table 32 KB while Prints is up; the sheet's pixels
    165 KB, a read buffer and a 16 KB histogram while it is built. Not enough memory for the sheet: the picture goes
    first, then a word.
  - **The card:** `wf_prints_scan` lists `ROLLS/ROLLnnn[B..]/Fnn_nnnn.WFP` (the right size only), newest roll first,
    16 rolls, each in frame order. Prints opens on the newest print; no prints: "No prints yet".
  - **Ways in:** Home's Prints tile always. PLAY (shooting, Home, the Top plate) only with "PLAY shows: Prints"
    (`play=prints`; default `canon`, Q2): `wf_ui_play_key` takes it in the same places MENU takes Home, a blind roll
    too (the prints are the Darkroom's). PLAY on Prints closes and sends Canon its PLAY, with a pass flag so that PLAY
    does not open Prints again; a blind roll keeps it with a word.
- **Settings and the switch back.**
  - Config keys in `WINDYFLM.CFG`: `os=on|off` (Camera interface), `finder=full|min|lines|off` (M3),
    `plate=on|off` (M4), `play=canon|prints` (M5).
  - The same "Camera interface" item sits in ML's menu (Shoot > WindyFilm), so the film OS can be turned off without
    it.
  - Movie mode: off (Canon + ML only).

## 5. Risks

- **R1. EOS M memory.**
  - Budget: the browser peaks at 122 KB of UI heap. Prints (M5) adds up to 370 KB while it is up with its contact
    sheet (the picture 147 KB, the colour table 32 KB, the sheet 165 KB, its build's 25 KB), all freed on leave; short
    of memory the sheet frees the picture and tries again, then says so.
  - M1: the display sizes went into WFFONTS.DAT itself (no WFOSFONT.DAT): 22,111 B before, 53,757 B now. The host
    harness's peak UI heap is 154 KB (153,776 B), the growth being the larger font pack. To check on the EOS M with
    the first M1 card (free memory in the log at open).
  - Home and the Top plate need only one can image (the big can's blob, at most a few KB in the cache).
  - The Darkroom keeps its own budget (dark-nomem). The film OS screens never run during a develop: the Darkroom is a
    screen of the same task.
  - Stage 2: trim the display font to the characters it draws (digits, "/", "f", ".", caps) if 78 KB is too much.
- **R2. Canon redraws.**
  - EOS M: patch 0003 skips `_redraw_do` while a module owns the screen.
  - 5D2: no such patch. Canon or ML (global draw) can draw for up to 1 s until our next full render (the browser's
    heal timer); the Top plate on the 5D2 also has Canon's own screen under it.
  - LiveView finder (M3): the bands are drawn by ML's own bar redraw (lvinfo), so they do not fight it. The middle is
    painted again every 500 ms. On the camera, still to see: Canon's AF frame and focus box over the corners, and
    whether ML's bars sit at rows 0 / 448 on both cameras (logged: "osvf: LiveView finder up (mode, bars at y / y)").
- **R3. Palette in LiveView.**
  - Our entries 0x50..0xFE are safe while we own the screen. In LV, Canon still draws its AF frame / focus box with its
    own entries.
  - Translucent scrims need the palette's opacity byte to work per entry (DIGIC 4 / 5), which is unchecked.
  - Plan: a probe card that writes a test HUD with opacity 1 / 2 / 3, photographed by the owner. If it fails, opaque
    dark chips (today's roll HUD look).
  - M3: the finder needs no probe. It uses ML's own indices, and ML's `COLOR_TRANSPARENT_BLACK` (0x03) is already
    translucent on both cameras.
- **R4. 5D2 viewfinder shooting, no LiveView.**
  - The back LCD on while shooting costs battery, so it follows Canon's display: off at half-press, 2 s after a shot.
  - No metering without a half-press.
  - The joystick press and the dials in LV need GUI mode 38 (DESIGN 5).
- **R5. Taking MENU on the EOS M.** The two-finger tap reaches ML as its own event, and patch 0003 turns it into MENU
  only while a module owns the screen, to close it.
  - Read in ML's code (M1): with no module UI open the tap is its own key code (37), so the takeover in shooting
    (`wf_ui_home_key`, MENU = 22 only) never sees it: the tap opens the ML menu, never Home.
  - With our UI open the tap arrives as MENU, the same code as the real key. So MENU on Home closes the UI at once and
    looks after the close: if ML opened its menu (it does that for a tap, `module_ui_after_release`), that was the tap;
    if not, it was the MENU key and Canon gets its MENU. In a sub-screen (shelf, Darkroom, Settings) MENU goes to Home
    first: from there the tap takes two taps to reach the ML menu.
  - Still to see on the camera: KEYTRACE on both, and no flash of Canon's menu after a tap.
- **R6. Canon's menu after Home.** Settled in M1: `module_send_keypress` and `gui_open_menu` are exported on both card
  cores (EOS M 2.0.2, 5D2 2.1.2; `checksyms` passes). Home sends MENU back with `module_send_keypress` once our UI has
  closed, and lets that one MENU through (1.5 s) so it does not open Home again. No `fake_simple_button` or
  `SetGUIRequestMode`.
- **R7. Falling back to the ML menu.** It is never taken:
  - EOS M: the two-finger tap. 5D2: TRASH from Home (Q1).
  - "Camera interface" sits in ML's menu too.
  - A UI task without a beat for 2 s passes every key.
  - M1: a font pack from before M1 (three fonts) still loads: the display sizes fall back to ML's fonts, the open log
    says "film OS sizes missing from the font pack (older pack)", and Settings > System shows "older pack". A missing
    or bad pack draws with ML's fonts, as the browser does.
- **R8. Auto power-off.** Our screens tell Canon "busy" as the Darkroom does (`powersave_prohibit` while open,
  `powersave_permit` on close), so Canon's own timers still run while shooting.

## 6. Staged plan (after the owner approves the mocks)

Each stage has the same checks as the browser:
- host harness shots and checks (palette range, everything given back, 0 leaks under ASan);
- `armcheck` and `checksyms` on both cameras;
- the provenance gate;
- EOS M and 5D2 suites, never at the same time.
Cards are built only on the owner's word.

1. **M1: Home + Settings + the switch.** Done on 21 Nov 2026 (host checks, both builds, both suites); not yet on a
   camera.
   - **Code:**
     - `src/ui/wf_os_ui.c` (new): Home and Settings, drawn in the browser's UI task; `wf_dark_ui.c` passes the two
       screens on.
     - `wf_font.c` / `tools/mk_wffont.py`: four sizes appended to WFFONTS.DAT (D20, D30, D80 light, L12 bold caps) with
       letter spacing for the caps. The first three fonts stay byte-equal.
     - `wf_ui.c`: `wf_ui_home_key`, `wfui_goto`, the close-then-act step (`after_close`: Canon MENU, ML menu, PLAY).
     - `wf_main.c`: the MENU takeover in `wf_keypress`. `wf_menu.c`: `os=` in WINDYFLM.CFG (default WindyFilm) and the
       ML menu entry "Camera interface" under "Open film browser".
   - **The way in:** MENU in shooting, Camera interface = WindyFilm, no ML menu, Canon idle (GUISTATE_IDLE), not
     recording; on the EOS M only in photo LiveView (movie mode keeps Canon's MENU). Home also opens from MENU in the
     browser and in the Darkroom.
   - **Home keys:**
     - UP / DOWN between the four tiles and the look strip; LEFT / RIGHT in the strip; the ring / main dial steps the
       look value (or moves between tiles).
     - SET / joystick / Q opens; INFO opens the browser.
     - MENU: Canon's menu. PLAY: Canon's playback (Prints). LV or a half-press closes.
     - TRASH: on the 5D2 the ML menu (Q1), on the EOS M the shelf.
     - A blind roll eats PLAY with "Blind roll: the prints come after the Darkroom".
   - **Settings:**
     - Interface: Camera interface, Quick keys (EOS M), Blind roll, plus the fixed ML menu and MENU key rows.
     - System: films, font pack, can pack.
     - LEFT / RIGHT, PICSTYLE or Q switch the section. MENU / TRASH go back to Home.
   - **Checked:** `tests/ui/run.sh` (ui_test and the new os_test, 0 failures under ASan, shots in
     `F:\MagicLatern\_windyfilm_docs\modernui_mocks\m1\`); `build.sh CAM=EOSM` and `CAM=5D2` (checksyms, TCC link,
     provenance gate PASS); `test.sh` EOS M 2331 / 0 then 5D2 1849 / 0 (CFG checks `os=`).
   - **Cards (21 Nov 2026):** `F:\MagicLatern\EOSM_WindyFilm_M1` and `5D2_WindyFilm_M1` (and on the share), the
     RAWB28 recipe. They differ from RAWB28 only in `windyflm.mo`, `WFFONTS.DAT` and the owner's step list
     `card/HUONGDAN_FILMOS_<CAM>.txt` (Vietnamese).
   - **On the camera (first M1 card):**
     - MENU in LV gives Home, MENU on Home gives Canon's menu with no Home in between;
     - the two-finger tap opens the ML menu from shooting and from Home;
     - 5D2 TRASH on Home opens the ML menu;
     - PLAY on Home gives Canon's playback;
     - free memory at open;
     - Camera interface = Canon + ML gives back the old MENU.
2. **M2: Film shelf (built 21 Nov 2026).** The browser opens on the shelf with Camera interface WindyFilm.
   - **Opens from:** Home's Film shelf tile, the EOS M's long DOWN, or any open of the browser.
   - **Layout (mock 03):**
     - Top bar: the film OS top bar with the section pill, and the mode chip where the camera has rolls.
     - Left: the film under the cursor (big can, name, kind / ISO / format chips, its two lines, why it cannot load).
     - Right: the section's tabs and the tab's films in a 4-column grid (3 rows in view, a scroll bar past that).
       The cursor's can is larger in an outline; the loaded film has a green dot.
   - **Behind it:** the browser's own list, tabs, selection, loads, questions and toasts.
   - **Keys:**
     - LEFT / RIGHT and the ring / dial step the film (across the section's tabs).
     - UP / DOWN move a row. UP from the first row goes to the pill / chip.
     - TAP / PICT / Q go to the next tab; SET loads.
     - INFO opens the browser's own screen ("Details": look rows, Exposures, Blind roll, INFO page); MENU there
       comes back to the shelf.
     - MENU on the shelf goes to Home.
   - **Still the browser's own screen:** Camera interface Canon + ML, Home's look strip (a row), the Darkroom's
     film pick, a new roll.
   - **Every film OS screen's top bar** now has the battery (the 5D2's in % too), the camera's clock and the
     camera.
   - **Checked:** os_test shelf keys and shots (`m2_*`, both cameras); ui_test and dr_run unchanged; build.sh both,
     gate PASS.
3. **M3: LiveView finder (built 21 Nov 2026, see section 4).**
   - Full / Minimal / Bright lines / Canon + ML in Settings. INFO stays Canon's: it switches Canon's info view and the
     finder.
   - Takes ML's bars through lvinfo; the middle from wf_task. ML's colours only, so no palette probe.
   - **Checked:**
     - `tests/ui/run.sh`: os_test has 13 finder shots (`m3_*`) over a stand-in LiveView frame (`WF_LVBG` = a 720 x 480
       PPM of a real LiveView photo, or a gradient). Each band is checked to stay in its 32 rows and the middle to
       clear back to 0. Every pixel is one of ML's colours. Settings has the new row.
     - `test.sh` OSVF (both cameras): the items as ML calls them, every way up and down, redraw requests, the middle,
       the config.
   - **On the camera:** AE_VALUE's sign (+ = over), the bar rows, Canon's AF box over the corners.
4. **M4: 5D2 Top plate (built 21 Nov 2026, see section 4).**
   - **Layout (mock 06):**
     - Film OS top bar "Top plate".
     - Left: SHUTTER and APERTURE in the large display size (BULB and long times smaller), ISO with FROM FILM when the
       film set it, the EXPOSURE scale (-2 .. +2 in thirds, red past either end), MODE in amber.
     - The roll's look in a box (Light, Print, Grain, Paper); NEXT FRAME, CARD (shots left), DRIVE, BLIND ROLL.
     - Right: the film's card (can, name, ROLL nnn, frames shot / exposures and the gauge; no film: an empty can's
       place and "TRASH: a film from the shelf").
   - **Keys:** MENU Home, TRASH the Film shelf, INFO Canon's screen (close + Canon's INFO), PLAY Prints, the main
     dial the shutter (Tv / M) or aperture (Av), the rear wheel the aperture (M) or compensation (P / Tv / Av),
     half-press closes.
   - **Settings:** Interface > Top plate On / Off on the 5D2, in the row the EOS M uses for Quick keys.
   - **Checked:**
     - `tests/ui/run.sh`: os_test opens the plate six times (shots `m4_5d2_plate`, `_dial`, `_end`, `_canon`,
       `m4_5d2_settings_plate_off`). Each dial steps its value; a change at 8 s keeps it up past 20 s; TRASH, MENU, INFO
       (Canon gets INFO), PLAY (Canon gets PLAY; a blind roll keeps it with a word); idle for 20 s closes with nothing
       sent. Everything given back each time.
     - `test.sh` PLATE (both cameras): the auto-open and its re-arming (display, gui mode, gui_state, the ML menu,
       PLAY, a Canon screen, a shot being saved), the dials' steps and ends, plate off, on again over an open screen,
       Camera interface Canon + ML,
       never in LiveView, never on the EOS M; `plate=` saved. CFG reads and saves `plate=`.
   - **On the camera:**
     - Canon's shooting screen really reads gui mode 0 with the display on (the 5D2's INFO screens);
     - which dial sends WHEEL LEFT / RIGHT and which UP / DOWN (ML 5D2: main dial assumed LEFT / RIGHT);
     - what INFO does after the close;
     - the battery % and the clock.
5. **M5: Prints + contact sheet (built 21 Nov 2026, see section 4).**
   - **Design change:** the plan was a 720 x 480 UYVY preview per print (675 KB, 24 MB a roll) drawn into the YUV
     layer (display filter in LV, Canon's PLAY buffer without). Built instead: a 156 KB `.WFP` per print drawn through
     our palette. No probe card needed (no YUV path, no Canon PLAY under us), the same on both cameras with or without
     LiveView, and the host harness checks every pixel. The price: 336 x 224 scaled up and 103 colours a picture.
   - **Layout (mocks 07 / 08, our own look):** Prints: the film OS top bar "Prints" with ROLL nnn, frame, n of m and
     the film; the print in a 576 x 384 box; the footer keys. Contact sheet: strips of six with amber frame numbers,
     the picked frame in an amber outline, a frame that has no picture "none".
   - **Settings:** Interface > PLAY shows Canon / Prints (both cameras).
   - **Checked:**
     - `tests/ui/run.sh`: os_test writes stand-in `.WFP` rolls (one roll developed twice, a portrait print in bars, one
       stored turned (orientation 6), a head that is not ours, a file of the wrong size) and walks them: frames across
       rolls and their ends, rolls, INFO's whole screen, the sheet (built, its keys, kept when opened again with no
       reads, the short roll's "none"), Home's tile, PLAY in and out (Canon gets PLAY once, the pass flag, a blind
       roll), the empty card, the 5D2 from the Top plate with Picture Style; every picture pixel counted in its box, the
       palette checked, every allocation freed. Shots `m5_*`; with `WF_PRVREAL` set, shots of real prints from a host
       Darkroom run (local only).
     - `test.sh` ROLL:jpeg: the Darkroom writes a valid `.WFP` beside each print (size, head, film, frame,
       orientation, black bars, the picture not flat, the sheet the 4 x 4 means of the picture) and `wf_prints_scan`
       lists them. CFG reads and saves `play=` (default canon).
   - **On the camera:** the time to open a print and to build a 36-frame sheet (two reads of 9 KB a frame); the
     pictures' look on the LCD (103 colours, the dither); free memory with the sheet up; PLAY in shooting on both
     (and that Canon's PLAY after ours does not bounce back); the 5D2's joystick up / down codes on Prints.
   - **Cards (21 Nov 2026, on the owner's word):** `F:\MagicLatern\EOSM_WindyFilm_M5` and `5D2_WindyFilm_M5` (and on
     the share), the M1 / RAWB28 recipe, `windyflm.mo` from cb503d4 (code = 7f7a019). Against the M1 cards only
     `windyflm.mo` and `card/HUONGDAN_FILMOS_<CAM>.txt` differ: the step lists now walk M1 - M5 (M2 shelf, M3 finder,
     M4 Top plate on the 5D2, M5 Prints and the contact sheet; a roll must be developed with the M5 card first, older
     prints have no `.WFP`). Logs come back to share Anh `log_m5_eosm` / `log_m5_5d2`.
6. **M6: Polish.**
   - The Darkroom's top bar in the film OS style (safelight theme kept).
   - Boot straight to the HUD with the roll toast.
   - HUONGDAN / README text, the full EOS M and 5D2 check lists, DESIGN section 3.19.

## 7. Open questions for the owner

1. **5D2 ML menu key:** TRASH on Home (mock), or keep TRASH = Film shelf everywhere and reach the ML menu only from
   Settings > System? *M1 default: TRASH on Home opens the ML menu (5D2 only; the EOS M keeps the two-finger tap).*
2. **PLAY default:** Camera JPEG (Canon's PLAY, as the owner likes) with Prints as the option (proposed), or Prints by
   default? *M5 default: Canon (`play=canon`); Settings > PLAY shows = Prints turns PLAY to our Prints. Home's
   Prints tile always opens Prints.*
3. **LiveView HUD default:** Full or Minimal? Is the FILM tag next to ISO (ISO set by the film) wanted?
4. **Home look strip:** keep it (Light / Print / Paper / Grain / Halation / Strength), or show the last 6 frames
   instead?
5. **Film shelf:** the new shelves (mock 03), or keep the browser's strip as the shelf and use the new look only for
   Home / HUD / Prints?
6. **Top plate:** also show it after power-on in LV mode on the 5D2, or only for viewfinder shooting?
7. **The mark:** the spool-and-gust tile, or the browser's can icon?
