# The defaults on real faded slides (the "faded-slide" entries of
# tests/images/manifest.json): restore:faded at Light-Dark 1, 1.25 and
# 1.5 with Saturation 0.4 and 0.6 on every slide. There is no ground
# truth, so it prints numbers to read together with pictures to look at:
#
#   cast    the cast left on near neutral colours: |mean a*, b*| of the
#           pixels with chroma below 12 in the result (as tests/slides.py;
#           empty if fewer than 1% are near neutral)
#   L*      the mean lightness
#   C*      the mean chroma
#   clip    the share of pixels with a channel at 0 or 255
#
# and writes a sheet per slide (the scan, then the six settings, labelled)
# and one sheet of all slides into the output folder. Run by
# tests/photos.sh --study.
#
#   slide-study.py <output folder> [--size N]
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from photos import arg, gegl, images, lab, load  # noqa: E402

SETTINGS = [(ld, sat) for sat in (0.4, 0.6) for ld in (1.0, 1.25, 1.5)]


def label(ld, sat):
    return 'LD %g, Sat %g' % (ld, sat)


def measure(path):
    c = load(path)
    l = lab(c)
    chroma = np.hypot(l[..., 1], l[..., 2])
    near = chroma < 12
    cast = float('nan')
    if near.sum() > 0.01 * near.size:
        cast = float(np.hypot(l[..., 1][near].mean(), l[..., 2][near].mean()))
    u8 = np.round(c * 255)
    clip = ((u8 == 0) | (u8 == 255)).any(-1).mean()
    return cast, l[..., 0].mean(), chroma.mean(), 100 * clip


def tile(path, w, text):
    im = Image.open(path).convert('RGB')
    im = im.resize((w, round(im.height * w / im.width)), Image.LANCZOS)
    out = Image.new('RGB', (w, im.height + 16), 'white')
    out.paste(im, (0, 16))
    ImageDraw.Draw(out).text((3, 2), text, fill='black')
    return out


def row(paths, texts, w):
    tiles = [tile(p, w, t) for p, t in zip(paths, texts)]
    sheet = Image.new('RGB', (w * len(tiles) + 2 * (len(tiles) - 1), max(t.height for t in tiles)),
                      'white')
    for k, t in enumerate(tiles):
        sheet.paste(t, (k * (w + 2), 0))
    return sheet


def main():
    out = sys.argv[1]
    size = int(arg('--size', 800))
    os.makedirs(out, exist_ok=True)
    slides = [p for p in json.load(open(os.path.join(images, 'manifest.json')))
              if p.get('kind') == 'faded-slide']
    jobs, names = [], []
    for photo in slides:
        path = os.path.join(images, photo['file'])
        if not os.path.exists(path):
            sys.exit('no %s: run tests/images/fetch.py' % path)
        name = os.path.splitext(photo['file'])[0]
        names.append(name)
        scan = os.path.join(out, name + '.png')
        if not os.path.exists(scan):
            im = Image.open(path).convert('RGB')
            s = size / max(im.size)
            im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS).save(scan)
        for ld, sat in SETTINGS:
            dst = os.path.join(out, '%s-ld%g-sat%g.png' % (name, ld, sat))
            jobs.append((scan, dst, ['light-dark=%g' % ld, 'saturation=%g' % sat]))
    with ThreadPoolExecutor(os.cpu_count()) as ex:
        list(ex.map(lambda j: gegl(j[0], j[1], 'restore:faded', j[2]), jobs))

    results = {}
    print('%-28s %-18s %6s %6s %6s %6s' % ('slide', 'setting', 'cast', 'L*', 'C*', 'clip%'))
    rows = []
    for name in names:
        scan = os.path.join(out, name + '.png')
        paths, texts = [scan], ['scan']
        for k, m in enumerate([('scan', scan)] + [
                (label(ld, sat), os.path.join(out, '%s-ld%g-sat%g.png' % (name, ld, sat)))
                for ld, sat in SETTINGS]):
            v = measure(m[1])
            results.setdefault(m[0], []).append(v)
            print('%-28s %-18s %6.1f %6.1f %6.1f %6.1f'
                  % (((name[:28] if k == 0 else ''), m[0]) + v))
            if k:
                paths.append(m[1])
                texts.append(m[0])
        r = row(paths, texts, 240)
        r.save(os.path.join(out, 'sheet-%s.jpg' % name), quality=88)
        rows.append(row(paths, texts, 160))
    sheet = Image.new('RGB', (rows[0].width, sum(r.height + 4 for r in rows)), 'white')
    y = 0
    for r in rows:
        sheet.paste(r, (0, y))
        y += r.height + 4
    sheet.save(os.path.join(out, 'sheet-all.jpg'), quality=85)

    print()
    print('%-18s %10s %10s %8s %6s %6s %6s' % ('over all slides', 'mean cast', 'median', 'worst',
                                               'L*', 'C*', 'clip%'))
    for key, vals in results.items():
        a = np.array(vals)
        casts = a[:, 0][~np.isnan(a[:, 0])]
        print('%-18s %10.2f %10.2f %8.1f %6.1f %6.1f %6.1f' % (
            key, casts.mean(), np.median(casts), casts.max(), a[:, 1].mean(), a[:, 2].mean(),
            a[:, 3].mean()))


if __name__ == '__main__':
    main()
