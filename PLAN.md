# Plan

Status: **steps 1 to 3 done, 4 and 5 in part** (2026-11-18, while
David was away; see "Defaults chosen" below). The method, the tests and
the measured numbers are in the README; the research behind this plan is
in docs/research.md.

## Goal

Automatic restoration of scanned faded colour slides and prints in GIMP
3.2, as a non-destructive filter, after Geoff Daniell's PhotoRestore
(GPL-3.0-or-later, GIMP 2 and Python 2 only, no GIMP 3 port anywhere).
Nothing in GIMP, GEGL, G'MIC, darktable or RawTherapee does this; their
automatic tools estimate one illuminant (gains), not a separate dye loss
curve per channel.

## Done

- `restore:faded` (operations/faded.c): all three stages of Daniell's
  newest method (pyrestore4.py), with the deviations the tests measured
  as better (README, "Deviations"), and every property below. Whole
  image estimate, deterministic; the per pixel part in parallel. 0.29 s
  on 24 megapixels (tests/bench.sh).
- `show`, colour map (2026-11-18 evening): a plot over the lower left
  corner (README, "Why: the colour map"). Show: Result gives the same
  bytes as before it existed (checked against the previous build on two
  slides and a photo, float TIFF, byte identical).
- tests/check.sh: 27 checks, also under ASan and UBSan, among them the
  grid of 54 fadings (mean Delta E 6.53, worst 11.3).
- tests/gimp-check.sh: the filter in headless GIMP 3.2 (every precision,
  grayscale, XCF, offsets, the gegl command line), all pass.
- tests/reference.sh: pyrestore4.py on the same images and the grid.
- tests/images/fetch.py and manifest.json: 10 CC0 photos and 6 faded
  DOCUMERICA slides from Commons, licence read per file, SHA-256 checked.
- tests/photos.sh: the synthetic fading of the photos against
  pyrestore4.py and three baselines; `--slides`: the real slides.
- tests/gui/shots.sh: written, **not run** (the GUI pass is David's).

## The operation

One operation, `restore:faded`, single input. Everything PhotoRestore
computes comes from the image alone, so a GEGL filter can do all of it;
no plug-in is needed for the method (docs/research.md section 4). A
second operation is not planned; open question 5 is about a picked
neutral point inside this one.

**Menu**: `<Image>/Colors/Auto`, label "Restore Faded Colors...", next
to White Balance and Stretch Contrast. That submenu exists in GIMP 3.2.6
(`menus/image-menu.ui.in.in`, label `_Auto` under `_Colors`) and GIMP
matches `gimp:menu-path` against labels without mnemonics
(`app/menus/filters-menu.c`, `app/widgets/gimpmenumodel.c`); the item's
place is confirmed on screen in the GUI test (GIMP 3.2.8, 2026-11-18:
last in Colors > Auto, docs/colors-menu.png). Open question 1.

**Properties** (all done):

| Property | Type, range, default | What |
|---|---|---|
| `light-dark` | double 0.5 to 2.0, 1.5 (Daniell: 1.0; measured, README) | Daniell's Light-Dark: the ideal ramp `(i/n)^gamma`; larger is darker |
| `neutralize` | boolean, on | stage 2: the grey balance in L\*u\*v\* (pyrestore4.py) |
| `warmth` | double -1 to 1, 0 | shifts the neutral target towards yellow or blue, at 1 by half the way from the D65 to the D50 white in u'v'; 0 is Daniell's empirical white point (`U0 = 0.2005, V0 = 0.4665`); his Restore1 "Make less blue" and Restore2 `u_off, v_off = 2` did the same by other means |
| `enhance-lightness` | boolean, on | stage 3: the 2.5% and 97.5% lightness ends moved half way to L\* 10 and 95 |
| `saturation` | double 0 to 1, 0.4 (Daniell: 0.6; measured) | stage 3: the mean saturation is raised to this if below, by at most 1.5 times (0 = off); pyrestore4.py's `sat_targ` |
| `amount` | double 0 to 1, 1 | how much of the correction: the exponents and scales blended towards 1 (Restore1's "Degree of Restoration") |
| `show` | enum: result, colour-map; result | "Colour map" draws a plot over the lower left corner of the result: before and after, the colour list with each channel sorted on its own (a monotone curve per channel, added as light so agreement is white), the ideal ramp dashed, and the combined power per channel as text (a built in 5 x 7 font, no fonts needed). Drawn from the fit and the image size only, so tiles and regions give the same plot. Daniell plotted the channels along the luminance order (scattered points); sorting each on its own shows the same cast as smooth curves |

No "Save parameters": the filter's settings are saved in the XCF, and
GIMP presets exist for the dialog. The estimated numbers could be shown
read-only if GEGL allows it (it does not have output properties that GIMP
displays; unverified whether any way exists).

## The method

In the gamma encoded R'G'B' of the image's own space (babl), as Daniell
works on 8 bit sRGB values; the model is a power law on those values,
and a power law on encoded values is a power law on light for a pure
gamma encoding (sRGB's linear toe makes it nearly so).

1. **Colour list** (whole image, deterministic). The input's whole
   bounding box is required (`get_required_for_output`,
   `get_cached_region` and `get_invalidated_by_change` return it, as in
   gegl-presence), read in bands of rows in a fixed order into a box
   averaged copy of at most about 100 pixels on the long side (measured
   better than 512, README; Daniell:
   1/8 nearest neighbour, or GIMP's scale to 1/10); pixels with alpha 0
   or not finite are left out. The copy's colours are binned 16 levels per
   channel (4096 bins, pyrestore4.py's top 4 bits); each non-empty bin
   gives one colour (the mean of its members; Daniell keeps the first
   pixel seen, open question 3), sorted by luminance with the bin index
   breaking ties. All sums in double, in a fixed order, no threads in
   the estimate: the same numbers for any tiles, threads, region or
   origin (checked).
2. **Stage 1, the dye loss** (done): per channel `lambda, sigma`
   minimising `sum (sigma c_i^lambda - (i/n)^gamma)^2`. For a given
   lambda the best sigma is closed form, so the search is one
   dimensional (a grid over log lambda from 0.1 to 10, then golden
   section): reproducible and without Nelder-Mead's start dependence.
3. **Stage 2, grey balance**: on the colour list after stage 1, six
   numbers (a power and a scale per channel) minimising pyrestore4.py's
   `Loff^2 + u0^2 + u1^2/3 + u2^2/5 + v0^2 + v1^2/3 + v2^2/5` (mean L\* at
   50, and `u*, v*` neutral on average and not drifting with lightness,
   the first three Legendre moments). **Done** as Levenberg-Marquardt on
   the seven residuals plus six that hold the numbers towards no change
   (weight 1 per log; README), from no change, steps kept within 0.01
   to 100, deterministic. L\*u\*v\* from GEGL's XYZ adapted to D65
   (open question 4, default chosen).
4. **Combine** stages 1 and 2 into one power and scale per channel (the
   family is closed under composition), then `amount`.
5. **Stage 3, enhancement**: on the colour list after 4, the 2.5% and
   97.5% L\* percentiles, the two piece linear L\* map and the `u, v`
   scale of pyrestore4.py. Per pixel in L\*u\*v\* (L\* scaled, u\*, v\*
   scaled with it), then back.
6. **Apply** per pixel: the per channel power and scale, then stage 3
   (done, in parallel). Values above 1 are clipped as Daniell does (8
   bit) except where the input was above 1 (open question 6, default
   chosen). Alpha as it is; pixels that are not finite unchanged.
7. **Quality** (pyrestore4.py labels good, poor, bad, fail from the
   residual): no way to show it in a GIMP filter dialog; logged with
   `g_debug`; the stage 2 steps cannot leave 0.01 to 100, so the
   negative exponents of pyrestore4.py's "fail" cannot occur.

The estimate runs once per render of the whole image (the cached region
is all of it). GIMP's preview re-renders on every slider move; the
estimate is cheap (a pass over the pixels plus a few thousand colours),
the per pixel part is a power per channel and a conversion to
L\*u\*v\* and back. Measured: 0.29 s on 24 megapixels (tests/bench.sh).

Not planned: side absorption correction (Restore1 had it; Daniell
dropped it; needs narrow band scans to mean anything, Gschwind);
correction of light fading, which is subtractive in density and not a
power law (Wilhelm, ch. 2), beyond what the fit happens to absorb; yellow
stain as its own term (open question 7).

## Tests

- **tests/check.sh** (generated images, no network): the fading model on a
  generated scene, three fadings now (cyan lost, pink, from Daniell's
  Table 1; yellow lost, blue; all faded); to add: a grid of fadings
  (`alpha` 0.4 to 0.95 per channel, scales 0.75 to 1). Measured: Delta E
  1976 of `restore(fade(x))` to `restore(x)` (the method's own result on
  the unfaded image): the model says it should not depend on the fading.
  **Target for the full method**: no worse than pyrestore4.py on the same
  images, now mean 2.9 to 5.5 (95th percentile 6.5 to 11.5) in
  tests/reference.sh (CIE Lab D65 from 8 bit PNGs); the skeleton's stage
  1 alone gives 8.8 to 16.3 there.
  Also Delta E to `x` itself, printed. Kept: gray stays gray, alpha,
  broken pixels, few colours pass through, Light-Dark monotonic, the
  same result for tiles, origin and region; to add: every property's
  direction, `amount = 0` is the identity, other buffer formats and RGB
  spaces, huge images (memory of the averaged copy only).
- **Real unfaded photos, faded synthetically**: CC0 and public domain
  photos from Wikimedia Commons (licence read on each file page, as in
  gegl-presence's tests/images/manifest.json; a fetch script, the photos
  not in git). Same numbers as above, and against baselines on the same
  images: GIMP's White Balance, `gegl:stretch-contrast` per channel, gray
  world. Target: better than every baseline on mean Delta E to `x`, and
  within 1 of pyrestore4.py.
- **Real faded slides**: the EPA DOCUMERICA series (1972 to 1977, public
  domain, `{{PD-USGov-EPA}}`, many visibly faded; one checked,
  docs/research.md section 5). No ground truth: the remaining cast (mean
  a\*, b\* of near neutral colours) is printed, and the results are looked
  at, side by side with pyrestore4.py, before and after pictures in the
  README.
- **Reference**: tests/reference.sh runs Daniell's own code (Python 3 port
  in tests/reference, needs NumPy and Pillow on the machine) on the same
  images. A second reference, Restore2.py in GIMP 2.10, would need a GIMP
  2 installation; not planned unless wanted.
- **tests/gimp-check.sh** (done): as a Gimp.DrawableFilter at every
  precision, grayscale, XCF round trip, offsets, the gegl command line.
- **tests/gui** (to add, from gegl-presence): the dialog on Broadway,
  screenshots looked at, the menu place confirmed; GUI GIMP started with
  `--new-instance` (as gegl-presence's tests/gui/start.sh) so it never
  talks to a GIMP the user has open.
- Everything through tests/isolate.sh: a throwaway home in tests/output,
  the build against the native GIMP (gimp-devtools/gimp-build.sh with
  GIMP_RUN_HOME), the user's folders listed before and after.

## Steps

1. Stage 2 and the combination; the invariance target met on the
   generated grid. **Done**: grid mean 6.69 against pyrestore4.py's 7.04
   (worst 11.5 against 18.1; tests/reference.sh). On the three named
   fadings pyrestore4.py is still closer (2.9 to 5.5 against 6.4 to 9.1).
2. Stage 3 and the remaining properties. **Done** (`show` on
   2026-11-18 evening).
3. The photo fetch script and manifest; the synthetic fading of real
   photos with the baselines; the DOCUMERICA slides. **Done**: mean
   Delta E 15.22, better than every baseline (gray world 16.22) and
   than pyrestore4.py (20.02); gray world is better on the 95th
   percentile (23.2 against 24.5) and on the worst photo.
4. GUI test and screenshots; bench on 24 megapixels. **Bench done**
   (0.29 s); the GUI test is written, not run.
5. README with method, numbers and examples; version 0.1. **README
   done**; version 0.1 after the GUI pass.
6. Tell the PhotoRestore issue #2 (the GIMP 3 question) where this is,
   once released.

## Defaults chosen 2026-11-18 while David was away; easy to change

- Q1 menu: kept Colors > Auto; the GUI pass is to confirm it on screen.
- Q3 colour list: decided by measurement: bin means of a box averaged
  copy (15.39) and Daniell's first pixel per bin (15.35) measure the
  same; means kept (less dependent on noise and pixel order).
- Q4 colour science: decided by measurement: GEGL's XYZ adapted to D65
  with Bradford (Daniell's white point as it is) and the white point
  re-derived around D50 measure the same (15.23 against 15.17); D65
  kept. Images in other RGB spaces are converted through their own
  space to XYZ, not treated as sRGB.
- Q5 picked neutral: later.
- Q6 clipping: the result is clipped at 1 except where the input was
  above 1 (high dynamic range float images keep those values). Decided
  by the values, not by the buffer format.
- Q7 yellow stain: later.
- Q8 batch: none.
- Q9 upstream: kept here for now.
- Q10 name: "Restore Faded Colors"; Daniell credited in the operation's
  description and the README.
- Light-Dark default 1.5 and Saturation 0.4 instead of Daniell's 1 and
  0.6, by measurement on the photos (README). For David to judge on his
  own faded slides: 1.5 is a little darker than Daniell's; 1.75 measured
  slightly better still (14.94) but looked dark on the DOCUMERICA
  slides. The ten test photos are modern, often dark and colour graded
  (Unsplash), so they favour a darker default more than family slides
  might.

## Open questions for the user

1. **Menu place**: Colors > Auto (next to White Balance; the skeleton), or
   Colors, or Filters > Enhance? Colors > Auto's other items are one-shot
   commands without dialogs.
2. **Faithful or improved**: decided 2026-11-18: improved. Start from
   pyrestore4.py's numbers (the empirical white point, `L0 = 50`,
   `L_top = 95`, `L_bot = 10`, `sat_targ = 0.6`, the 1/8 copy) and deviate
   where the tests measure a better result; the README lists each
   deviation and its numbers.
3. **Colour list**: Daniell keeps the first pixel seen per bin on a
   nearest neighbour copy; the skeleton averages (box copy, bin means),
   which is less noise dependent but gives stage 1 exponents up to about
   0.1 away from his. Match his, or keep averaging?
4. **Colour science**: L\*u\*v\* with babl's D50 (consistent with GEGL, the
   white point constants re-derived) or Daniell's D65 sRGB formulas (his
   results reproduced)? And in images that are not sRGB: convert to sRGB
   for the estimate, or work in the image's own space?
5. **A picked neutral** (optional): one or a few "this is grey" points
   with GIMP's coordinate picker (as gegl-control-points), overriding
   stage 2 when set. Useful for sunsets and other images the automatic
   balance gets wrong (Daniell: "attempts to process sunsets will always
   fail"). In the first version or later?
6. **Clipping**: clip the result to 0 to 1 as Daniell (8 bit), or keep
   values above 1 for float images?
7. **Yellow stain**: a separate Dmin (whites) term, since stain and not
   dye loss is the main problem of 1980s and later prints (Wilhelm,
   ch. 5)? It is not in Daniell's model.
8. **Batch**: is a batch mode wanted (Daniell had batch versions)? GIMP's
   own batch via BIMP or a small Python plug-in that applies the filter to
   a folder could do it; not planned otherwise.
9. **Upstream**: offer it to GEGL later (as `gegl:restore-faded`, GPL-3+
   operations), or keep it here?
10. **Name**: `restore:faded` and "Restore Faded Colors", or name
    Daniell in the title ("Restore Faded Colors (PhotoRestore)")?

## Found along the way

- pyrestore4.py stops with an error ("zero to a negative power") on 4 of
  the 54 fadings of the grid: its stage 2 simplex returns a negative
  exponent (its own "fail" label) and the power of a zero channel then
  raises.
- XCF keeps a filter's double settings as single precision floats (0.7
  comes back as 0.699999988; checked in headless GIMP), so a filter
  loaded from XCF can differ a little from the one saved;
  tests/gimp-check.py compares it at the settings it came back with.
- Wikimedia's media server answers Python's own HTTP client with 429 at
  once and curl with the same User-Agent with 200; tests/images/fetch.py
  uses curl and waits when asked. Originals of large files were refused
  repeatedly; the 2560 pixel renditions came through.

- GIMP's manual says White Balance discards 0.05% of pixels at each end
  (docs.gimp.org/3.0/en/gimp-layer-white-balance.html); GIMP 3.2.6's
  code uses 0.6% (`bias = 0.006`, `app/operations/gimplevelsconfig.c`).
  Report upstream.
- Daniell's Restore3.py opens a debug file at a hard-coded path of his own
  (not commented out), so it cannot run elsewhere.
- The `gegl` program warns of one leaked GeglBuffer after this
  operation, as after any that caches its whole result (gegl-presence's
  PLAN.md); in tests/check.c nothing leaks.
