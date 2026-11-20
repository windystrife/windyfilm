#!/usr/bin/env python3
"""os_sheet.py OUTDIR - contact sheets (one per camera) and index.html of the modernUI mock PNGs, with the owner's
captions in Vietnamese. DejaVu Sans for the captions (Ubuntu Sans has no full Vietnamese set)."""
import os, sys, html
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONTB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
CAP = {
    "01_home": ("Home thay menu ML", "Cuộn đang nạp, số kiểu còn lại, đường tới Kệ phim / Darkroom / Ảnh in / Cài đặt"),
    "02_home_darkroom": ("Home: chọn Darkroom", "Ô đang chọn viền hổ phách; dải LOOK là look của cuộn, INFO để sửa"),
    "03_shelf": ("Kệ phim (nạp phim)", "Lon to bên trái, ba kệ lon nhỏ bên phải; Film | Film stage và các nhóm ở trên"),
    "04_hud_full": ("HUD LiveView đầy đủ", "Phim + cuộn, góc khung 3:2, tốc / khẩu / ISO (từ phim), thang đo sáng, số khung"),
    "05_hud_minimal": ("HUD LiveView tối giản", "Chỉ phim và số khung; INFO đổi Full / Minimal / của Canon"),
    "06_topplate": ("Top plate (5D2 chụp qua ống ngắm)", "Màn sau thay màn thông số của Canon: tốc, khẩu, ISO, bù sáng, cuộn"),
    "07_play_print": ("PLAY: ảnh in", "Bản in trong ROLLS thay ảnh chưa tráng; PLAY lần nữa = JPEG của máy (tùy chọn)"),
    "08_play_sheet": ("PLAY: tờ tiếp xúc", "Cả cuộn trên dải phim, kiểu đang chọn phóng to với thông số"),
    "09_settings": ("Cài đặt", "Bật / tắt giao diện WindyFilm, PLAY hiện gì, HUD, Quick keys / Top plate, Blind roll"),
    "10_darkroom": ("Darkroom (giữ RAWB28)", "Ảnh trên giá phơi, Camera JPEG 4 lựa chọn; theme đèn đỏ giữ nguyên"),
    "11_darkroom_develop": ("Darkroom đang tráng", "Màn tráng của RAWB28, không đổi"),
}
CAMS = (("eosm", "EOS M"), ("5d2", "5D Mark II"))

def font(px, wght):
    return ImageFont.truetype(FONTB if wght >= 600 else FONT, px)

def main():
    od = sys.argv[1]
    t1, t2, tt = font(20, 600), font(14, 400), font(26, 600)
    rows = []
    for cam, cname in CAMS:
        shots = [(k, os.path.join(od, "%s_%s.png" % (cam, k))) for k in CAP if os.path.exists(os.path.join(od, "%s_%s.png" % (cam, k)))]
        cw, ch, pad, top = 720, 480 + 64, 24, 90
        cols = 2
        nr = (len(shots) + cols - 1) // cols
        S = Image.new("RGB", (pad + cols * (cw + pad), top + nr * (ch + pad)), (22, 23, 26))
        d = ImageDraw.Draw(S)
        d.text((pad, 26), "WindyFilm modernUI: film OS trên %s (mock, code UI thật trên PC)" % cname, font=tt, fill=(236, 238, 240))
        for i, (k, p) in enumerate(shots):
            x, y = pad + (i % cols) * (cw + pad), top + (i // cols) * (ch + pad)
            d.text((x, y), CAP[k][0], font=t1, fill=(244, 166, 52))
            d.text((x, y + 30), CAP[k][1], font=t2, fill=(160, 168, 180))
            S.paste(Image.open(p).convert("RGB"), (x, y + 64))
        out = os.path.join(od, "modernui_%s.png" % cam)
        S.save(out)
        print(os.path.basename(out))
        rows.append((cname, shots))
    h = ["<!doctype html><meta charset=utf-8><title>WindyFilm modernUI</title>",
         "<style>body{background:#16171a;color:#eceef0;font-family:'Ubuntu Sans',sans-serif;margin:24px}"
         "h2{color:#f4a634}figure{display:inline-block;margin:0 16px 24px 0;vertical-align:top}"
         "figcaption{color:#a0a8b4;font-size:14px;max-width:720px}b{color:#eceef0}img{display:block;width:720px;margin-top:6px}</style>",
         "<h1>WindyFilm modernUI: film OS (stage 1 mock)</h1>"]
    for cname, shots in rows:
        h.append("<h2>%s</h2>" % cname)
        for k, p in shots:
            h.append("<figure><figcaption><b>%s</b> - %s</figcaption><img src='%s'></figure>" % (
                html.escape(CAP[k][0]), html.escape(CAP[k][1]), os.path.basename(p)))
    open(os.path.join(od, "index.html"), "w").write("\n".join(h))
    print("index.html")

main()
