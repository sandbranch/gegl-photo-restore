# Real photos, faded synthetically with Daniell's model, restored by
# restore:faded, by Daniell's own pyrestore4.py and by three simple
# baselines; prints the Delta E 1976 (CIE Lab, D65) of each result to the
# unfaded photo, and of each restoration to the same method's result on
# the unfaded photo. Run by tests/photos.sh, which sets GEGL_PATH to the
# build and isolates the run.
#
#   photos.py <output folder> [--size N] [--only restore:faded,...]
#             [--props "name=value ..."] [--label NAME]
#
# The photos are those of tests/images/manifest.json (tests/images/fetch.py
# downloads them), scaled to N pixels on the long side (default 1200)
# and saved as 8 bit PNG, as a scan would be. The baselines:
#   white-balance   GIMP's Colors > Auto > White Balance, re-implemented
#                   from GIMP 3.2.6's code (gimplevelsconfig.c: per
#                   channel Levels on a linear light histogram of 256
#                   bins, the ends where 0.6% of the pixels are cut)
#   stretch         gegl:stretch-contrast with keep-colors off (a per
#                   channel stretch from the minimum to the maximum)
#   gray-world      per channel gains in linear light that make the
#                   three channel means equal (Buchsbaum 1980)
# pyrestore4.py's results are kept between runs (they do not change).
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
images = os.path.join(here, 'images')

# Daniell's Table 1 (an Agfacolor slide), the three of tests/check.c and
# a magenta loss (light fading of Ektachrome in projection, green cast)
FADINGS = {
    'cyan_lost_pink':     ((0.46, 0.72, 0.59), (251 / 255, 224 / 255, 193 / 255)),
    'yellow_lost_blue':   ((0.85, 0.80, 0.50), (0.80, 0.86, 0.98)),
    'all_faded':          ((0.60, 0.62, 0.58), (0.92, 0.92, 0.90)),
    'magenta_lost_green': ((0.80, 0.50, 0.75), (0.95, 0.97, 0.88)),
}
METHODS = ['restore:faded', 'pyrestore4', 'white-balance', 'stretch', 'gray-world']


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def load(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.float64) / 255.0


def save(path, c):
    Image.fromarray((np.clip(c, 0, 1) * 255 + 0.5).astype(np.uint8)).save(path)


def to_linear(c):
    return np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)


def from_linear(l):
    l = np.clip(l, 0, 1)
    return np.where(l > 0.0031308, 1.055 * l ** (1 / 2.4) - 0.055, 12.92 * l)


def lab(c):
    m = np.array([[0.4124, 0.3576, 0.1805],
                  [0.2126, 0.7152, 0.0722],
                  [0.0193, 0.1192, 0.9505]])
    xyz = to_linear(c) @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 216 / 24389, np.cbrt(xyz), (24389 / 27 * xyz + 16) / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]),
                     200 * (f[..., 1] - f[..., 2])], -1)


def delta_e(a, b):
    d = np.sqrt(((lab(a) - lab(b)) ** 2).sum(-1)).ravel()
    return d.mean(), np.percentile(d, 95)


def white_balance(c):
    lin = to_linear(c)
    out = np.empty_like(lin)
    n_bins, bias = 256, 0.006
    for ch in range(3):
        hist = np.bincount(np.clip(np.rint(lin[..., ch] * (n_bins - 1)), 0, n_bins - 1).astype(int).ravel(),
                           minlength=n_bins).astype(np.float64)
        count = hist.sum()
        lo, hi = 0.0, 1.0
        new = 0.0
        for i in range(n_bins - 1):
            new += hist[i]
            if abs(new / count - bias) < abs((new + hist[i + 1]) / count - bias):
                lo = (i + 1) / (n_bins - 1)
                break
        new = 0.0
        for i in range(n_bins - 1, 0, -1):
            new += hist[i]
            if abs(new / count - bias) < abs((new + hist[i - 1]) / count - bias):
                hi = (i - 1) / (n_bins - 1)
                break
        out[..., ch] = (lin[..., ch] - lo) / max(hi - lo, 1e-6)
    return from_linear(out)


def gray_world(c):
    lin = to_linear(c)
    means = lin.reshape(-1, 3).mean(0)
    return from_linear(lin * (means.mean() / np.maximum(means, 1e-6)))


def gegl(src, dst, op, props):
    subprocess.run(['gegl', '-i', src, '-o', dst, '--', op] + props, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run_method(method, src, dst, props):
    if method == 'restore:faded':
        gegl(src, dst, 'restore:faded', props)
    elif method == 'pyrestore4':
        if not (os.path.exists(dst) and os.path.getmtime(dst) > os.path.getmtime(src)):
            subprocess.run([sys.executable, os.path.join(here, 'reference', 'pyrestore4.py'), src, dst],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           env=dict(os.environ, PYRESTORE_DEBUG='0'))
    elif method == 'stretch':
        gegl(src, dst, 'gegl:stretch-contrast', ['keep-colors=false'])
    elif method == 'white-balance':
        save(dst, white_balance(load(src)))
    elif method == 'gray-world':
        save(dst, gray_world(load(src)))


def prepare(out, size):
    """the scaled photos and their faded copies (made once)"""
    names = []
    for photo in json.load(open(os.path.join(images, 'manifest.json'))):
        if photo.get('kind') == 'faded-slide':
            continue
        path = os.path.join(images, photo['file'])
        if not os.path.exists(path):
            if '--allow-missing' in sys.argv:
                continue
            sys.exit('no %s: run tests/images/fetch.py' % path)
        name = os.path.splitext(photo['file'])[0]
        orig = os.path.join(out, '%s.png' % name)
        if not os.path.exists(orig):
            im = Image.open(path).convert('RGB')
            s = size / max(im.size)
            im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS).save(orig)
        x = load(orig)
        for f, (alpha, F) in FADINGS.items():
            p = os.path.join(out, '%s-%s.png' % (name, f))
            if not os.path.exists(p):
                save(p, np.stack([F[ch] * x[..., ch] ** alpha[ch] for ch in range(3)], -1))
        names.append(name)
    return names


def main():
    out = sys.argv[1]
    size = int(arg('--size', 1200))
    only = arg('--only', ','.join(METHODS)).split(',')
    props = arg('--props', '').split()
    label = arg('--label', 'restore:faded')
    # the results of restore:faded are named after the label, so that runs
    # with other settings can go side by side
    rtag = 'faded' if label == 'restore:faded' else 'faded_' + ''.join(c if c.isalnum() else '_' for c in label)
    os.makedirs(out, exist_ok=True)
    names = prepare(out, size)
    jobs = []
    for name in names:
        for f in ['unfaded'] + list(FADINGS):
            src = os.path.join(out, '%s.png' % name if f == 'unfaded' else '%s-%s.png' % (name, f))
            for m in only:
                tag = m if m != 'restore:faded' else rtag
                jobs.append((m, src, os.path.join(out, 'r', '%s-%s-%s.png' % (tag, name, f)), props))
    os.makedirs(os.path.join(out, 'r'), exist_ok=True)
    with ThreadPoolExecutor(max_workers=min(12, os.cpu_count() or 4)) as ex:
        list(ex.map(lambda j: run_method(*j), jobs))

    # to the photo (mean, 95th) and to the same method on the photo (mean)
    res = {m: {'to_photo': [], 'p95': [], 'invariance': [], 'per': {}} for m in ['faded'] + only}
    for name in names:
        x = load(os.path.join(out, '%s.png' % name))
        for f in FADINGS:
            faded = load(os.path.join(out, '%s-%s.png' % (name, f)))
            m0, p0 = delta_e(faded, x)
            res['faded']['to_photo'].append(m0)
            res['faded']['p95'].append(p0)
            for m in only:
                tag = m if m != 'restore:faded' else rtag
                r = load(os.path.join(out, 'r', '%s-%s-%s.png' % (tag, name, f)))
                ru = load(os.path.join(out, 'r', '%s-%s-unfaded.png' % (tag, name)))
                m1, p1 = delta_e(r, x)
                m2, _ = delta_e(r, ru)
                res[m]['to_photo'].append(m1)
                res[m]['p95'].append(p1)
                res[m]['invariance'].append(m2)
                res[m]['per'].setdefault(name, []).append(m1)
    print('Delta E 1976 to the unfaded photo: mean over %d photos x %d fadings (mean of the '
          '95th percentiles); worst photo; and to the same method on the unfaded photo'
          % (len(names), len(FADINGS)))
    print('%-16s %16s %8s %12s' % ('method', 'to the photo', 'worst', 'to itself'))
    for m in ['faded'] + only:
        r = res[m]
        worst = max(np.mean(v) for v in r['per'].values()) if r['per'] else float('nan')
        inv = '%12.2f' % np.mean(r['invariance']) if r['invariance'] else '%12s' % '-'
        print('%-16s %6.2f (95%% %5.1f) %8.2f %s' % (label if m == 'restore:faded' else m,
                                                   np.mean(r['to_photo']), np.mean(r['p95']), worst, inv))
    if '--per-photo' in sys.argv:
        print()
        print('%-16s' % 'photo' + ''.join('%14s' % m[:13] for m in only))
        for name in names:
            print('%-16s' % name[:16] + ''.join('%14.2f' % np.mean(res[m]['per'][name]) for m in only))
    json.dump({m: {k: v for k, v in r.items()} for m, r in res.items()},
              open(os.path.join(out, 'results-%s.json' % label.replace(':', '_')), 'w'))


if __name__ == '__main__':
    main()
