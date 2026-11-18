# Real faded slides (EPA DOCUMERICA, 1972 to 1977, public domain; the
# "faded-slide" entries of tests/images/manifest.json) restored by
# restore:faded and by Daniell's pyrestore4.py. There is no ground truth:
# prints the cast that remains (the mean a*, b* of the whole image, and
# of its near neutral pixels, chroma below 12 in the result) and writes a
# sheet per slide (the scan, restore:faded, pyrestore4.py) to look at.
# Run by tests/slides.sh.
#
#   slides.py <output folder> [--size N] [--props "name=value ..."]
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from photos import arg, images, lab, load, run_method  # noqa: E402


def cast(c):
    l = lab(c)
    chroma = np.hypot(l[..., 1], l[..., 2])
    near = chroma < 12
    a, b = l[..., 1].mean(), l[..., 2].mean()
    if near.sum() > 0.01 * near.size:
        return a, b, l[..., 1][near].mean(), l[..., 2][near].mean(), near.mean()
    return a, b, float('nan'), float('nan'), near.mean()


def main():
    out = sys.argv[1]
    size = int(arg('--size', 1200))
    props = arg('--props', '').split()
    os.makedirs(out, exist_ok=True)
    print('%-26s %-14s %15s %22s' % ('slide', 'method', 'mean a*, b*', 'near neutral a*, b*'))
    for photo in json.load(open(os.path.join(images, 'manifest.json'))):
        if photo.get('kind') != 'faded-slide':
            continue
        path = os.path.join(images, photo['file'])
        if not os.path.exists(path):
            sys.exit('no %s: run tests/images/fetch.py' % path)
        name = os.path.splitext(photo['file'])[0]
        scan = os.path.join(out, name + '.png')
        if not os.path.exists(scan):
            im = Image.open(path).convert('RGB')
            s = size / max(im.size)
            im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS).save(scan)
        results = [('scan', scan)]
        for m in ('restore:faded', 'pyrestore4'):
            dst = os.path.join(out, '%s-%s.png' % ('faded' if m == 'restore:faded' else m, name))
            run_method(m, scan, dst, props)
            results.append((m, dst))
        for m, p in results:
            a, b, na, nb, frac = cast(load(p))
            print('%-26s %-14s %6.1f, %6.1f %10.1f, %6.1f (%2.0f%%)' % (name[:26], m, a, b, na, nb, 100 * frac))
        ims = [Image.open(p).convert('RGB') for _, p in results]
        w = 600
        ims = [im.resize((w, round(im.height * w / im.width))) for im in ims]
        sheet = Image.new('RGB', (w * len(ims), max(i.height for i in ims)), 'white')
        for k, im in enumerate(ims):
            sheet.paste(im, (k * w, 0))
        sheet.save(os.path.join(out, 'sheet-%s.jpg' % name), quality=90)


if __name__ == '__main__':
    main()
