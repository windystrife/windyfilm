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
| | PLAY | Prints |
| | half-press | back to shooting |
| Film shelf | LEFT / RIGHT, ring | film |
| | UP / DOWN | shelf row |
| | one-finger tap | group |
| | SET | load (animation, then LiveView) |
| | INFO | about the film |
| | MENU | Home |
| Prints | LEFT / RIGHT, ring | print |
| | SET | contact sheet |
| | INFO | details |
| | PLAY | Canon's PLAY of the camera JPEG |
| | MENU | Home |
| | half-press | shoot |
| Contact sheet | arrows | frame |
| | SET | that print |
| | INFO | roll |
| | MENU | Home |
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
| | INFO | Top plate on / off (Canon's own screen) |
| | MENU | Home |
| | TRASH | Film shelf (today) |
| | PLAY | Prints / Canon PLAY (option) |
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
| Prints / sheet | joystick, dial | print / frame |
| | SET | sheet / print |
| | INFO | details |
| | PLAY | Canon's PLAY |
| | MENU | Home |
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
- **LiveView HUD.** Drawn from wf_task like today's roll HUD (`wfdr_hud`, into ML's bitmap), only when `lv`, no ML
  menu, no UI, not recording, and the display is on.
  - Canon's own LV info is hidden by Canon's clean INFO view, or by ML's own lvinfo bars (which already replace
    Canon's bottom bar on both cameras).
  - Values: `lens_info` (raw_shutter, raw_aperture, raw_iso, ae) and `shooting_mode`, as `wf_expsim.c` reads them; the
    count from `wf_roll`.
  - Colours, two choices (R3):
    - (a) ML's palette indices, as the roll HUD does now: safe, opaque chips;
    - (b) our palette while the HUD is up: the mock's look, translucent scrims if the opacity byte works.
  - Fallback HUD: items in ML's own lvinfo bars (`lvinfo_add_items`), which need no palette.
- **5D2 Top plate (no LiveView).** Shown when `!lv`, gui_state IDLE, the display is on, and Canon is on its
  shooting-settings screen (power on, INFO, after a shot).
  - It takes the screen with GUI mode 2, as the browser does without LV, and follows Canon's display: at half-press it
    gives the screen back and Canon turns the LCD off.
  - After a shot (the roll count changes; CBR_SHOOT_TASK) it shows the new count for 2 s, then lets Canon's power
    saving run.
  - Values: `lens_info`. Without a half-press the 5D2 does not meter, so EXPOSURE shows the exposure compensation in
    P / Tv / Av and the last metered value in M.
- **PLAY: Prints.**
  - **Pictures.** The Darkroom already holds each print in memory when it writes `ROLLS/ROLLnnn/Fnn_nnnn.JPG`. Stage 5
    makes it also write:
    - a screen-size UYVY preview (720 x 480, about 675 KB);
    - one contact-sheet strip file per roll (80 x 53 thumbnails).
    Neither needs a JPEG decoder on the camera.
  - **The viewer** streams the preview in 16 KB chunks into the display's YUV buffer:
    - in LiveView through the module's display filter (`CBR_DISPLAY_FILTER`, `wf_dispf`, already the LV preview: it
      outputs the stored frame instead of the sensor);
    - or in Canon's PLAY into ML's `get_yuv422_vram()` buffer, with Canon's redraws held off by the front-buffer take.
      This is how ML's own viewers draw.
    Our bitmap draws the chrome around it.
  - **Zero-code alternative (today).** Camera JPEG "Over + print" or "Print + over" already put the print in DCIM, so
    Canon's own PLAY shows prints.
  - The option "PLAY shows: Camera JPEG" leaves Canon's PLAY alone (the owner likes it). That is the default (Q2).
- **Settings and the switch back.**
  - Config keys in `WINDYFLM.CFG`: `os=on|off` (Camera interface), `play=prints|canon`, `hud=full|min|canon`,
    `plate=on|off`.
  - The same "Camera interface" item sits in ML's menu (Shoot > WindyFilm), so the film OS can be turned off without
    it.
  - Movie mode: off (Canon + ML only).

## 5. Risks

- **R1. EOS M memory.**
  - Budget: the browser peaks at 122 KB of UI heap. The film OS adds no frame buffers: the print previews stream into
    the display buffer in 16 KB chunks, the sheet reads one strip at a time.
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
  - LV HUD: Canon redraws its LV info on events, so the HUD redraws after each key and every 1 s, as the roll HUD does.
- **R3. Palette in LiveView.**
  - Our entries 0x50..0xFE are safe while we own the screen. In LV, Canon still draws its AF frame / focus box with its
    own entries.
  - Translucent scrims need the palette's opacity byte to work per entry (DIGIC 4 / 5), which is unchecked.
  - Plan: a probe card that writes a test HUD with opacity 1 / 2 / 3, photographed by the owner. If it fails, opaque
    dark chips (today's roll HUD look).
- **R4. 5D2 viewfinder shooting, no LiveView.**
  - The back LCD on while shooting costs battery, so it follows Canon's display: off at half-press, 2 s after a shot.
  - No metering without a half-press.
  - The display filter does not run without LV: Prints on the 5D2 use Canon's PLAY buffer.
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
2. **M2: Film shelf.** The new shelf layout over the browser's list. The browser stays as the INFO look editor.
3. **M3: LiveView HUD.**
   - The palette probe card (R3), then the HUD in the chosen colours: EOS M stills LV, then 5D2 LV.
   - INFO cycles Full / Minimal / Canon.
4. **M4: 5D2 Top plate.** Display following, values, after-shot count, battery check on the camera.
5. **M5: Prints.**
   - The Darkroom writes the screen previews and the sheet strip. The Prints / Contact sheet viewer (display filter
     in LV, Canon's PLAY buffer otherwise).
   - "PLAY shows" option, default Camera JPEG.
6. **M6: Polish.**
   - The Darkroom's top bar in the film OS style (safelight theme kept).
   - Boot straight to the HUD with the roll toast.
   - HUONGDAN / README text, the full EOS M and 5D2 check lists, DESIGN section 3.19.

## 7. Open questions for the owner

1. **5D2 ML menu key:** TRASH on Home (mock), or keep TRASH = Film shelf everywhere and reach the ML menu only from
   Settings > System? *M1 default: TRASH on Home opens the ML menu (5D2 only; the EOS M keeps the two-finger tap).*
2. **PLAY default:** Camera JPEG (Canon's PLAY, as the owner likes) with Prints as the option (proposed), or Prints by
   default? *M1 default: PLAY on Home is Canon's playback (Prints comes in M5).*
3. **LiveView HUD default:** Full or Minimal? Is the FILM tag next to ISO (ISO set by the film) wanted?
4. **Home look strip:** keep it (Light / Print / Paper / Grain / Halation / Strength), or show the last 6 frames
   instead?
5. **Film shelf:** the new shelves (mock 03), or keep the browser's strip as the shelf and use the new look only for
   Home / HUD / Prints?
6. **Top plate:** also show it after power-on in LV mode on the 5D2, or only for viewfinder shooting?
7. **The mark:** the spool-and-gust tile, or the browser's can icon?
