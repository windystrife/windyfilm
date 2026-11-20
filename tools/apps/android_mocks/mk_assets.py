#!/usr/bin/env python3
"""mk_assets.py - gather the inputs of the WindyFilm phone mockups into WORK/assets (stage 1, approval images only).

    python3 -I -B mk_assets.py APPS GALLERY GALLERY_STAGE WORK

APPS           the apps hand-off assets (modernUI session): cans/<code>.png, fonts/UbuntuSans.ttf, photos/t_NN.jpg
GALLERY        tools/look_sweep.sh films output: film/<id>-<name>__IMG_nnnn__...jpg, neutral/000-neutral__IMG_nnnn__neutral.jpg
GALLERY_STAGE  tools/look_sweep.sh stage output: matrix/<id>-<name>__IMG_nnnn__pNN_...jpg (Film stage: private renders)

Everything written is the owner's photos or private renders: WORK stays outside the repository, never committed.
"""
import glob, os, shutil, sys
from PIL import Image

APPS, GAL, GALS, WORK = sys.argv[1:5]
A = os.path.join(WORK, 'assets')
for d in ('cans', 'f', 'n', 'st', 't'):
    os.makedirs(os.path.join(A, d), exist_ok=True)

shutil.copy(os.path.join(APPS, 'fonts', 'UbuntuSans.ttf'), os.path.join(A, 'UbuntuSans.ttf'))
for p in glob.glob(os.path.join(APPS, 'cans', '*.png')):
    shutil.copy(p, os.path.join(A, 'cans', os.path.basename(p)))
for p in glob.glob(os.path.join(APPS, 'photos', 't_*.jpg')):
    if os.path.basename(p) != 't_14.jpg':            # README: avoid t_14
        shutil.copy(p, os.path.join(A, 't', os.path.basename(p)[2:]))


def one(pat):
    hits = sorted(glob.glob(pat))
    if not hits:
        sys.exit('missing: ' + pat)
    return hits[0]


def save(im, path, w=None, q=88):
    im = im.convert('RGB')
    if w and im.width > w:
        im = im.resize((w, round(im.height * w / im.width)), Image.LANCZOS)
    im.save(path, quality=q)


FRAMES = ['6839', '6844', '6847', '6849', '6852', '6856']
# film looks used on the screens (Film section; Darkroom develop of the owner's CR2s, the camera's default finish)
FILMS = ['026', '027', '029', '030', '031', '032', '033', '034', '035', '037', '039', '041', '044', '046', '055', '064',
         '066', '084']
for fid in FILMS:
    for fr in FRAMES:
        src = one(os.path.join(GAL, 'film', f'{fid}-*__IMG_{fr}__*.jpg'))
        save(Image.open(src), os.path.join(A, 'f', f'{fid}_{fr}.jpg'), 1280 if fr in ('6847', '6849', '6844') else 640)
for fr in FRAMES:
    save(Image.open(one(os.path.join(GAL, 'neutral', f'000-neutral__IMG_{fr}__*.jpg'))), os.path.join(A, 'n', f'{fr}.jpg'), 1280)

# viewfinder: Portra 400 on IMG_6844, a 3:4 portrait crop around the subject (1080 x 1440 = 360 x 480 css at x3)
im = Image.open(one(os.path.join(GAL, 'film', '033-*__IMG_6844__*.jpg')))
h = im.height; w = h * 3 // 4; x0 = min(max(int(im.width * 0.565) - w // 2, 0), im.width - w)
save(im.crop((x0, 0, x0 + w, h)).resize((1080, 1440), Image.LANCZOS), os.path.join(A, 'vf.jpg'), q=90)
# the same crop, neutral (the "look off" state / live-preview comparison)
im = Image.open(one(os.path.join(GAL, 'neutral', '000-neutral__IMG_6844__*.jpg')))
save(im.crop((x0, 0, x0 + w, h)).resize((1080, 1440), Image.LANCZOS), os.path.join(A, 'vf_n.jpg'), q=90)

# Film stage: Vision3 5207 250D (id 92) through each of its print stocks, IMG_6844 and IMG_6856
for fr in ('6844', '6856'):
    for p in sorted(glob.glob(os.path.join(GALS, 'matrix', f'092-*__IMG_{fr}__p*.jpg'))):
        pv = os.path.basename(p).split('__')[2].split('_')[0]          # p00 .. p15
        save(Image.open(p), os.path.join(A, 'st', f'092_{fr}_{pv}.jpg'), 1280 if pv == 'p01' else 480)
print('assets in', A)
