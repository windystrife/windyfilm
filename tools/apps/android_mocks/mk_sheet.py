#!/usr/bin/env python3
"""mk_sheet.py - one overview sheet of the phone mockups with Vietnamese captions (for the owner's approval).

    python3 -I -B mk_sheet.py PNG_DIR FONT_REGULAR.ttf FONT_BOLD.ttf OUT.png

The caption font needs Vietnamese glyphs (e.g. Windows segoeui.ttf / segoeuib.ttf); the hand-off Ubuntu Sans lacks some.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

D, FR, FB, OUT = sys.argv[1:5]
SHOTS = [
    ('01_viewfinder', '01 · Ngắm chụp', 'Film live 30 fps trong khung ngắm · ROLL 004 12/36 · ISO / tốc / WB / focus / EV'),
    ('02_shelf', '02 · Tủ phim (Film fridge, các lon)', 'Film | Film stage · Colour / Cine / B&W / Film LUT · nạp cuộn 3-36 kiểu'),
    ('03_roll', '03 · Cuộn + contact sheet', 'Cuộn trong máy (blind: ẩn ảnh) · cuộn đã tráng · cuộn chờ trên kệ'),
    ('04b_develop_film', '04b · Tráng: tab Film', 'Light + Kelvin / Tint / Exposure (mới) · Strength · Print · Paper'),
    ('08_develop_print', '08 · Tráng: tab Print', 'Đèn in (K) · Print contrast · Dye density · đầu màu C/M/Y, Linked, Keep brightness · Split tone'),
    ('09_develop_tone', '09 · Tráng: tab Tone', 'Engine Lab (mô hình của chủ) | Camera · Negative 4 núm · Shoulder 4 núm · Levels'),
    ('10_develop_grain', '10 · Tráng: tab Grain', 'Gauge 8/16/35/65 mm · Amount · Tones S/M/H · Colour noise · Grain size · Clump · Angle · Seed · bụi, tóc, xước'),
    ('04_develop', '04 · Tráng: Glow · Halation', 'Chọn Halation / Bloom / Mist · preset gauge · 10 núm của chủ · trước / sau là shader thật'),
    ('04c_develop_bloom', '04c · Tráng: Glow · Bloom', 'Bloom giữ màu nguồn sáng · 13 núm của chủ (Edge guard gộp 1 hàng) · trước / sau là shader thật'),
    ('11_develop_lens', '11 · Tráng: tab Lens', 'Vignette: Amount, Size, Feather, Roundness, kéo tâm ngang / dọc'),
    ('12_develop_gate', '12 · Tráng: tab Gate', 'Viền film tự vẽ: 11 khổ, góc, lỗ răng cưa, chữ viền · Per frame: drift, variation'),
    ('05_filmstage', '05 · Film stage', '44 negative x 15 print · Film exp, Push, Dev contrast (mới), Printer R/G/B'),
    ('06_settings', '06 · Cài đặt', 'DNG + JPG · preview · dữ liệu film của bạn (không kèm trong app) · thẻ máy'),
    ('07_import', '07 · Nhập thẻ EOS M / 5D2', 'Đầu đọc OTG · ROLLS + WFROLL.TXT · tráng lại · không ghi gì vào thẻ'),
]
# the owner's Galaxy Z Fold3: inner screen (841 x 673 dp) four times, the cover screen (317 x 864 dp) once;
# the row wraps when it is full (open flat + cover first, then the two tabletop screens)
FOLD = [
    ('13_fold_viewfinder', '13 · Fold3 mở: ngắm chụp', 'Màn trong nằm ngang: khung ngắm trái, cột máy phải · gập lại là màn ngoài chụp tiếp'),
    ('14_fold_develop', '14 · Fold3 mở: tráng 2 cột', 'Ảnh + cuộn + histogram bên trái · 8 tab và núm bên phải (cùng hàng như điện thoại)'),
    ('16_fold_cover', '16 · Màn ngoài', '317 dp: bố cục điện thoại vừa khít'),
    ('15_fold_tabletop', '15 · Fold3 gập nửa (Flex): chụp', 'Đặt bàn: ảnh nửa trên, nút nửa dưới · không vẽ gì lên bản lề · hẹn giờ 10 s'),
    ('15b_fold_tabletop_develop', '15b · Fold3 gập nửa: tráng', 'Chủ chọn 21/11: tabletop có cả Develop · ảnh nửa trên, 8 tab và núm nửa dưới (2 cột)'),
]
FW, FH, CW = 552, 442, 162           # inner screens at width 552; the cover screen at the same height
W, H = 360, 800                      # each screen at 1/3 of 1080 x 2400
COLS, PAD, TOP, CAP = 5, 28, 140, 78
rows = (len(SHOTS) + COLS - 1) // COLS
FTOP = TOP + rows * (H + CAP + PAD) + 20
SW = COLS * W + (COLS + 1) * PAD
FIMS, fpos, fx, fy = {}, [], PAD, 0
for name, title, cap in FOLD:
    im = Image.open(os.path.join(D, name + '.png')).convert('RGB')
    w = FW if im.width > im.height else CW
    if fx + w > SW - PAD:
        fx, fy = PAD, fy + 1
    FIMS[name] = (im, w)
    fpos.append((fx, fy))
    fx += w + PAD
FROWH = CAP + 24 + FH + PAD
sheet = Image.new('RGB', (SW, FTOP + 48 + (fy + 1) * FROWH), (20, 21, 25))
d = ImageDraw.Draw(sheet)
fb = lambda s: ImageFont.truetype(FB, s)
fr = lambda s: ImageFont.truetype(FR, s)


def wrap(text, f, width):
    out, line = [], ''
    for w in text.split(' '):
        t = (line + ' ' + w).strip()
        if d.textlength(t, font=f) > width and line:
            out.append(line); line = w
        else:
            line = t
    return out + [line]


d.text((PAD, 24), 'WindyFilm cho điện thoại (Android) · mockup giai đoạn 1 · chờ chủ duyệt', font=fb(32), fill=(236, 238, 240))
LEGEND = ('Ảnh trong khung là render thật của engine WindyFilm trên CR2 của chủ (Darkroom develop). '
          'Chữ NEW xanh = tuỳ chọn mới, máy ảnh chưa có. Chữ cam = núm Film stage. Tráng có 8 tab: '
          'Film · Print · Tone · Grain · Glow · Lens · Gate · Finish (đủ mọi núm chủ yêu cầu 21/11).')
for k, ln in enumerate(wrap(LEGEND, fr(18), sheet.width - 2 * PAD)):
    d.text((PAD, 70 + k * 24), ln, font=fr(18), fill=(160, 168, 180))
for i, (name, title, cap) in enumerate(SHOTS):
    x = PAD + (i % COLS) * (W + PAD)
    y = TOP + (i // COLS) * (H + CAP + PAD)
    im = Image.open(os.path.join(D, name + '.png')).convert('RGB').resize((W, H), Image.LANCZOS)
    sheet.paste(im, (x, y + CAP))
    d.rectangle((x - 1, y + CAP - 1, x + W, y + CAP + H), outline=(42, 47, 56))
    d.text((x, y + 2), title, font=fb(22), fill=(244, 166, 52))
    for k, ln in enumerate(wrap(cap, fr(15), W)):
        d.text((x, y + 34 + k * 19), ln, font=fr(15), fill=(160, 168, 180))
d.text((PAD, FTOP), 'Điện thoại gập · Galaxy Z Fold3 của chủ (không khoá xoay, không viền đen)', font=fb(26), fill=(236, 238, 240))
for (name, title, cap), (x, row) in zip(FOLD, fpos):
    im, w = FIMS[name]
    im = im.resize((w, FH), Image.LANCZOS)
    y = FTOP + 48 + row * FROWH
    capw = w
    d.text((x, y + 2), title, font=fb(20 if w == CW else 22), fill=(244, 166, 52))
    for k, ln in enumerate(wrap(cap, fr(15), capw)):
        d.text((x, y + 32 + k * 19), ln, font=fr(15), fill=(160, 168, 180))
    sheet.paste(im, (x, y + CAP + 24))
    d.rectangle((x - 1, y + CAP + 23, x + w, y + CAP + 24 + FH), outline=(42, 47, 56))
sheet.save(OUT)
print(OUT, sheet.size)
