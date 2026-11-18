# Delta E 1976 (CIE Lab, D65, sRGB) of each faded, restored (restore:faded)
# and PhotoRestore restored (pyrestore4.py) image to the scene, and of
# each restoration to the same method's restoration of the unfaded scene
# (how well the fading is undone, whatever the method makes of the
# scene), from the PNGs in the given folder (tests/reference.sh writes
# them).
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import sys

import numpy as np
from PIL import Image


def lab(path):
    c = np.asarray(Image.open(path).convert('RGB'), dtype=np.float64) / 255.0
    lin = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805],
                  [0.2126, 0.7152, 0.0722],
                  [0.0193, 0.1192, 0.9505]])
    xyz = lin @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 216 / 24389, np.cbrt(xyz), (24389 / 27 * xyz + 16) / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]),
                     200 * (f[..., 1] - f[..., 2])], -1)


def de(a, b):
    d = np.sqrt(((a - b) ** 2).sum(-1)).ravel()
    return d.mean(), np.percentile(d, 95)


out = sys.argv[1]
ref = lab(os.path.join(out, 'scene.png'))
names = sorted(f[len('faded-'):-4] for f in os.listdir(out) if f.startswith('faded-'))
def row(n, target):
    cells = []
    for kind in ('faded', 'restored', 'pyrestore4'):
        p = os.path.join(out, 'scene.png' if n == 'unfaded' and kind == 'faded'
                         else '%s-%s.png' % (kind, n))
        t = target(kind)
        cells.append('%5.1f (95%% %4.1f)' % de(lab(p), t) if os.path.exists(p) and t is not None
                     else '%16s' % '-')
    print('%-18s %s %s %s' % (n, cells[0], cells[1], cells[2]))


head = '%-18s %16s %16s %16s' % ('fading', 'faded', 'restore:faded', 'pyrestore4')
print('Delta E 1976 to the scene, mean (95th percentile)')
print(head)
for n in names + ['unfaded']:
    row(n, lambda kind: ref)
restored_unfaded = {k: lab(os.path.join(out, '%s-unfaded.png' % k))
                    for k in ('restored', 'pyrestore4')
                    if os.path.exists(os.path.join(out, '%s-unfaded.png' % k))}
print()
print('Delta E 1976 to the same method on the unfaded scene (faded: to the scene)')
print(head)
for n in names:
    row(n, lambda kind: ref if kind == 'faded' else restored_unfaded.get(kind))

# the grid of fadings of tests/check.c: each restoration to the same
# method's restoration of the unfaded scene
grid = sorted(f[:6] for f in os.listdir(out) if f.startswith('grid') and f.endswith('-faded.png'))
if grid:
    print()
    print('The grid of %d fadings: Delta E to the same method on the unfaded scene' % len(grid))
    print('%-18s %16s %16s' % ('', 'restore:faded', 'pyrestore4'))
    stats = {}
    for kind, name in (('restored', '%s-restored.png'), ('pyrestore4', 'pyrestore4-%s.png')):
        ms = [de(lab(os.path.join(out, name % g)), restored_unfaded[kind])[0]
              for g in grid if os.path.exists(os.path.join(out, name % g))]
        stats[kind] = ms
    print('%-18s %16.2f %16.2f' % ('mean', np.mean(stats['restored']), np.mean(stats['pyrestore4'])))
    print('%-18s %16.2f %16.2f' % ('worst', np.max(stats['restored']), np.max(stats['pyrestore4'])))
