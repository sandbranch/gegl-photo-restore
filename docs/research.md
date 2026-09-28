# Research: restoring faded colour photographs

Checked 2026-09-28. Every claim below was read in the source named; what
was not read is marked **unverified**.

## 1. PhotoRestore (Geoff Daniell)

### Where the code is

- **github.com/stongey/PhotoRestore**, published by Marc St. Onge
  ("stongey") with Daniell's permission (its README). GitHub API: GPL-3.0,
  70 stars, 5 forks, last push 2020-05-10; the last commit is 509036a
  (2019-07-23). The newest algorithm is commit 44ccae8 (2019-07-03,
  "first commit of Geoff's newest algorithm", `Python/pyrestore4.py`).
  Contents: `GIMP Plugins/Restore1.py`, `Restore2.py`, `Restore3.py` and
  their `batch_` versions (GIMP 2.8, Python 2, `gimpfu`);
  `Python/pyrestore3.py` and `pyrestore4.py` (stand-alone, Python 2, NumPy
  and PIL); `White Papers/Restore1.pdf` (2008-12-11), `Restore2.pdf`
  (2016-04-18), `stand_alone.pdf` (2016-06-03); a Windows executable
  `restore4.0A.exe` and an Android APK (not run).
- Issues: #1 is a pull request by deburau (2020-05-10, "Migration to
  Python3 and Executable for Windows 10 64") and #2 by ToddAndMargo
  (2025-01-02, "Gimp 3.0"); both open, no replies.
- Forks: justimm, Shadoward and trecyme are identical to upstream;
  deburau is one commit ahead (the Python 3 standalone, not a GIMP
  plug-in). ImageProcessing-ElectronicPublications/gimp-plugin-photorestore
  is unrelated history, GIMP 2 (`from gimpfu import *`), last commit
  2019-09-09. **No GIMP 3 port exists** (GitHub repository and code search
  for photorestore, python-fu_Restore1, pyrestore3, lionhouse.plus.com).
- Daniell's own site, www.lionhouse.plus.com: refused connections on
  2026-09-28. The Wayback Machine lists the root last captured 2023-06-07
  and directories `/photosoftware/gimp_plugins` (2009 to 2012: Restore.py,
  batch_restore.py, brighten.py, remove-spots.py), `/photosoftware/restore`
  (2016 to 2017: Restore1/2/3.py, android, documents, stand_alone,
  NEWS.txt) and `/photorestore` (2019). Its "GitHub" link pointed to
  github.com/roger-/restore (an "authorized GitHub mirror", Python 3,
  per a 2018 capture), which is now 404. (These archive facts are from a
  CDX listing made during this research; the Archive was offline when
  re-checked, so the file contents there are **unverified**.)

### Licence

Every Python file has the GPL "version 3 of the License, or (at your
option) any later version" notice, `__copyright__ = "Copyright 2008,
Geoff Daniell"` and `__license__ = "GPLv3"`; `LICENSE` is the GPL-3.0
text. So GPL-3.0-or-later. This repository is GPL-3.0-or-later too, which
allows porting his code; `tests/reference/pyrestore4.py` is his file,
modified (listed in its header).

### The model (Restore1.pdf, sections 2 to 4)

A reversal film's dye amount is proportional to the unexposed emulsion,
so transmitted light is `Pt = Pi (E/Emax)^(a beta gamma_p)` and the
manufacturer makes the exponent about 1. Fading reduces the dye amount
`beta` to `alpha beta`, so `Pt' ~ Pt^alpha`. With the scanner's fixed
`Pmax` and encoding `Pt = Pmax (C/255)^gamma_d`, the scanned value becomes

    C' = 255 (C/255)^alpha_c,   restored C = 255 (C'/255)^(1/alpha_c)

per channel, `alpha_c < 1`. Section 4 adds side absorptions (the magenta
and yellow dyes absorb some red, and so on, coefficients `delta`), extra
dye formed later from left-over couplers in E-4 and E-6 (either constant
or proportional, both giving "a power and a scaling"), and an overall
absorption `F_c`. His conclusion: "It should be possible to accomplish
the restoration using only scaling and power law operations", which GIMP
Levels does (gamma and input/output ends).

### The three GIMP plug-ins and every option

- **Restore1.py** (`<Image>/Restore/Restore1`; Restore1.pdf section 5):
  the image scaled to 1/10; its 256 colour palette (GIMP's indexed
  conversion); per channel the maximum (`top = 1.0`) and lower decile
  (`bot = 0.1`) of the palette values set `m` and `alpha` so the decile
  goes to 0.1 of the range; iterated by a Newton-like step (at most 10
  times, "deteriorated too far" if `alpha` leaves 0.1 to 10); then a
  refinement `sigma, lambda` per channel towards the palette's mean grey
  `(R+G+B)/3` (Newton, 20 steps); then side absorption correction as a
  Channel Mixer layer in Divide mode (`delta` 0.15, 0.07, 0.05, 0.18,
  0.05, 0.03). Options: **Make less blue** (toggle, default on: Color
  Balance midtones yellow -20, "an overall shift from blue to yellow...
  improves many pictures"), **Degree of Restoration** (0 to 1.2, default
  1: `alpha = 1 - contrast (1 - alpha)`, `m = 255 - contrast (255 - m)`),
  **Combine all Layers** (flatten, default on).
- **Restore2.py** (`<Image>/Restore/Restore2`; Restore2.pdf): (1) on the
  1/10 copy's 256 palette colours, per channel `lambda, sigma` minimising
  `sum_i (255 sigma (C_i/255)^lambda - 255 (i/255)^gamma)^2` (Nelder-Mead
  simplex from 1, 1); the "ideal photograph" has its palette colours
  spread along `(i/255)^gamma`. Scaling: `C_hi = (C_max + C_ext)/2` (the
  maximum and a linear extrapolation), `s_c = sigma_c (C_hi/255)^lambda_c`,
  all channels scaled by `255 / max s_c`, some highlights allowed to burn
  out. (2) Colour balance: on the palette of the stage 1 result, a
  6 parameter simplex so that the weighted mean `u*, v*` (CIE L\*u\*v\*,
  D65 2 degrees, weight `w = g / (g + u*^2 + v*^2)`, `g = 50`) sits at
  `(u_off, v_off) = (2, 2)`, slightly warm. (3) Optional saturation: HSV
  decompose, `S` divided by `fac/255` where `fac = 255 / min(1.5, 150 /
  (mean + 2 sd))`. Options: **Light-Dark** (`gammatarg`, 0.5 to 2.0 step
  0.25, default 1.0, "a larger value of gamma produces a darker restored
  image"), **Saturation adjustment** (Off/On, default On), **Save
  parameters** (writes a GIMP Levels preset to a hard-coded
  `/home/gjd/.gimp-2.8/levels/restore`). The result is a new image.
- **Restore3.py**: Restore2 with its own octree colour quantiser (the
  appendix of Restore2.pdf), in case GIMP drops indexed conversion; "the
  results are not identical but are very similar". As published it opens
  a debug file at a hard-coded `/home/gjd/gimp/NewRestore/debug.txt`
  (line 533, not commented out), so it fails on any other machine.
- The `batch_` versions add a folder argument (default `/home/gjd/restore`).

### pyrestore4.py, the newest method (read in full)

The file says "pyrestore4.py seems to perform better than any earlier
versions in the vast majority of cases." Its steps:

1. The image resized to 1/8 with nearest neighbour.
2. "Colours": the distinct colours by their top 4 bits per channel (4096
   bins), the first pixel seen in each bin kept; sorted by luminance `Y`
   (sRGB decoded, Rec. 709 weights).
3. Stage 1 as Restore2, ideal `(i/n)^1` (`gammatarg` fixed at 1), no
   `C_hi` scaling; values above 255 clip.
4. Stage 2 on the colours of the stage 1 result: a 6 parameter simplex
   minimising `Loff^2 + u0^2 + u1^2/3 + u2^2/5 + v0^2 + v1^2/3 + v2^2/5`:
   `Loff` is mean L\* minus `L0 = 50`, and `u0, u1, u2` (`v0...`) are the
   first three Legendre moments of `u*` (`v*`) against `L/L0 - 1`, that is
   the colour must be neutral on average, and not drift with lightness to
   first and second order. The white point is an "empirical" `U0 = 0.2005,
   V0 = 0.4665` (vs the CIE 0.2009, 0.4610; several alternatives are left
   in comments: "these values are extremely critical"). A quality label
   from the residual: good under 2, poor from 2, bad over 5, fail if any
   parameter is negative.
5. The two fits combined into one power and scale per channel.
6. Enhancement in L\*u\*v\*: the 2.5% and 97.5% lightness percentiles
   `Lmin, Lmax` are moved half way to `L_bot = 10`, `L_top = 95` by a
   two piece linear map around `L0 = 50`; `u, v` scaled by `sat_targ /
   sat` if the mean saturation `sqrt(u*^2 + v*^2)/L*` is below
   `sat_targ = 0.6`.

No user options (the command line parsing is commented out).

### Checked here

- `tests/reference/pyrestore4.py` runs Daniell's file under Python 3 with
  mechanical changes only. On the same colour list, a closed form
  (sigma for a given lambda, then a search over lambda) reproduces his
  simplex's stage 1 lambda and sigma within about 0.01 (a scratch
  script, 2026-09-28): the optimiser is not the source of differences.
- `restore:faded`'s stage 1 differs from pyrestore4.py's stage 1 by up to
  about 0.1 in lambda on the test scenes, because it builds the colour
  list differently (box average and bin means, not nearest neighbour and
  the first pixel per bin). Stage 1 is sensitive to how the colour list is
  made; PLAN.md open question 3.
- On the generated scenes (tests/check.c, tests/reference.sh) the full
  pyrestore4.py gives nearly the same image whatever the fading: mean
  Delta E 1976 of 2.9 to 5.5 between its restoration of a faded copy and
  of the unfaded scene; stage 1 alone gives 8.8 to 16. Stages 2 and 3
  matter.
- On a public domain faded 1972 slide (section 5), stage 1 alone removes
  the pink cast; pyrestore4.py's result is a little warmer
  (docs/example-documerica-546220.jpg).

## 2. The physics of dye fading

Main source: Henry Wilhelm and Carol Brower, *The Permanence and Care of
Color Photographs* (1993), chapters as free PDFs listed at
http://www.wilhelm-research.com/book_toc.html (files
`pdf/HW_Book_NN_of_20_HiRes_v1c.pdf`; the site's TLS certificate does not
verify). Page numbers are the book's.

**Dark storage versus light.**

- "In dark storage, the cyan dyes in chromogenic papers traditionally have
  been the least stable of the three dyes" (ch. 5, p. 166, Fig. 5.1).
- Magenta is "the most stable of the three dyes in the dark, but is the
  least stable in light under typical indoor display conditions"
  (Ektacolor, ch. 5, p. 165; generally ch. 6, p. 223).
- Graphics Atlas (https://new.graphicsatlas.org/chromogenic/object-view):
  "Cyan dyes tend to fade in dark leaving prints overall magenta while
  magenta dyes tend to light fade leaving prints overall blue or
  blue-green."

**By material** (Wilhelm, 20% dye loss from density 1.0 in his
62 °C / 45% RH test, and Kodak's Arrhenius predictions for 24 °C / 40% RH;
Tables 5.5c, 5.9 and 5.13):

- **Agfacolor PE paper Type 4** (1974 to 1982): cyan lost more than 60% in
  under 4 years at room temperature in some portraits (ch. 5, p. 166).
  All known prints "had faded to an ugly reddish shadow" (ch. 3, p. 101).
- **Ektacolor 37 RC, 74 RC and 78**: cyan the least stable dye (20% loss in
  27 to 60 test days). Ektacolor 37 RC: 20 years at 24 °C. Wilhelm: "a
  serious color shift toward red-yellow because of cyan dye fading; the
  whites in the prints will have significantly yellowed" (ch. 1, p. 35).
- **Eastman Color motion picture prints, 1950 to about 1970**: most of the
  cyan lost, usually much of the yellow too, "a ghastly reddish-magenta
  reminder" (ch. 1, p. 26).
- **Ektachrome E-3** (1959 to 1977): cyan the least stable, 8 years at
  24 °C, with a yellowish stain. **E-4**: cyan, 30 years. **E-6**: Kodak
  predicts yellow first (105 years), Wilhelm's test cyan first; stain is
  likely very high by the time of 20% loss (ch. 5, p. 176).
- **Kodachrome** (K-12, K-14) in the dark: yellow the least stable dye;
  no stain, because no couplers remain in the film (ch. 5, p. 168).
  (That this gives a blue cast is inference; Daniell observes that K-14
  slides "ultimately acquire a blue cast", Restore1.pdf section 1.)
- **In projection** (light): Ektachrome's cyan fades most in continuous
  projection (red shift) and magenta in normal intermittent projection
  (green shift) (ch. 6, p. 231); magenta is the least stable dye in nearly
  all films tested, Agfachrome included (ch. 6, p. 214).

**Yellow stain.** From the late 1980s, "it is no longer dye fading but
rather yellowish stain formation that is the main image stability
problem" in the dark (ch. 5, p. 165); the main cause is left-over magenta
couplers (ch. 5, p. 169). Stain shows as a rise of blue density in Dmin,
the whites, and adds to (or masks) a yellow imbalance.

**How the density changes** (ch. 2, pp. 77 to 79, Tables 2.3 and 2.4),
which decides the model:

- Dark fading "usually involves an approximately equal percentage loss of
  density throughout the density range" (Table 2.3: 45 to 48% at every
  density from 0.2 to 1.5). A constant fraction of density is a power law
  on transmitted light: exactly Daniell's model.
- Light fading is "more or less equal loss in density units" (Table 2.4:
  about 0.13 to 0.15 at every density): subtractive, so the highlights
  wash out first, and a power law cannot undo it without harming the
  denser parts (ch. 6, p. 223).
- Pure colours and neutrals fade at about the same rate in the dark, not
  in light (ch. 2, p. 79).

**Standards.** ISO 18909:2022 measures change at Dmin and at 1.0 above
Dmin, in neutral and pure C, M, Y patches; its dark test adapts the
Arrhenius method of Bard et al. (preview read at
https://cdn.standards.iteh.ai/samples/83333/59da29805f8e4f77a0c7bb791d14b70e/ISO-18909-2022.pdf).

**Linear bleach model.** Gschwind, "Restoration of Movie Films by Digital
Image Processing" (Danish Film Institute, *Preserve Then Show*, 2002,
pp. 168 to 171, https://www.dfi.dk/files/docs/2018-01/RudolfGschwind.pdf):
faded densities `[Y' M' C'] = A [Y M C] + offsets`; the diagonal is
proportional fading, the off diagonal terms light fading through other
layers, the offsets stain. It needs narrow band scans (about 450, 550,
650 nm) for Beer-Lambert to hold; with broadband RGB the red channel can
be "up to half" magenta side absorption, and when cyan has faded most "a
correct reconstruction is no longer possible."

## 3. Other methods

### Published methods for faded colour

- Chambah and Besserer, "Digital Restoration of Faded Color Movies: A
  Four-Step Method", IS&T/SID CIC 8 (2000) pp. 161 to 166,
  doi:10.2352/cic.2000.8.1.art00030 (read): side absorption matrix,
  reference frame, colour adjustment with offset, affine or non-linear
  bleaching models fitted by least squares to 4 to 6 **user chosen key
  colours**, then histogram equalisation. Their later unsupervised ACE
  based work (SPIE 5008, 2003): **not read**.
- Gschwind and Frey, "Electronic Imaging, a Tool for the Reconstruction of
  Faded Color Photographs", JIST 38(6) (1994) pp. 520 to 525, and Frey and
  Gschwind, "Mathematical Bleaching Models for Photographic Three-Color
  Materials", JIST 38(6) pp. 513 to 519: citations checked, **texts not
  read**.
- Trumpy and Chatterjee, "A Spectral Approach to Digitally Restore a Faded
  Agfacolor Print from 1945", IS&T Archiving,
  doi:10.2352/issn.2168-3204.2022.19.1.20 (read): narrow band capture at
  415, 530 and 657 nm, dye unmixing, per layer density scaling so that the
  unexposed frame lines become neutral black. Needs a spectral capture.
- Kodak US 5,796,874, "Restoration of faded images" (Woolfe, Pillman,
  Barry; filed 1996-04-30, granted 1998-08-18; read from the USPTO PDF):
  a fade model linear in the initial densities with time dependent
  coefficients, inverted for a user chosen fade time. By the 20 year term
  from filing it ended about 2016-04 (legal status page **not checked**).
- ACE: Rizzi, Gatta, Marini, Pattern Recognition Letters 24(11) (2003)
  pp. 1663 to 1677, doi:10.1016/S0167-8655(02)00323-9; open fast version
  by Getreuer, IPOL 2 (2012), doi:10.5201/ipol.2012.g-ace (the IPOL page
  says BSD-2-Clause). Patent status **unverified**. GEGL already has a
  relative: `gegl:stress` (STRESS, Kolås, Farup and Rizzi, LGPL-3+).
- Applied Science Fiction / Kodak Digital ROC: "proprietary, patented";
  no patent number found, **unverified**.

### Colour constancy (automatic white balance), primary references

Gray world: Buchsbaum, J. Franklin Inst. 310 (1980) 1 to 26,
doi:10.1016/0016-0032(80)90058-7. White patch (max-RGB): Land and McCann,
JOSA 61 (1971) 1, doi:10.1364/josa.61.000001. Shades of gray: Finlayson
and Trezzi, CIC 12 (2004) 37 to 41, doi:10.2352/cic.2004.12.1.art00008.
Gray edge: van de Weijer, Gevers, Gijsenij, IEEE TIP 16 (2007) 2207 to
2214, doi:10.1109/tip.2007.901808. Multiscale Retinex: Jobson, Rahman,
Woodell, IEEE TIP 6 (1997) 965, doi:10.1109/83.597272. These estimate one
illuminant (three gains, or a gamma per channel at most); none models a
different curve per channel, which is what dye loss needs.

### What open tools do (read in their source)

- **GIMP 3.2.6** (tag GIMP_3_2_6): Colors > Auto has Equalize, White
  Balance, Stretch Contrast, Stretch Contrast HSV and Color Enhance
  (`menus/image-menu.ui.in.in`). **White Balance**
  (`app/core/gimpdrawable-levels.c`, `app/operations/gimplevelsconfig.c`
  line 537) sets per channel Levels ends from a linear light histogram
  where the cumulative fraction is closest to `bias = 0.006`: 0.6% per
  end (GIMP's manual says 0.05%). No gamma, so a dye loss curve stays.
- **GEGL**: `gegl:stretch-contrast` (min/max; "Keep colors" default on,
  one range for all channels; off gives a per channel stretch),
  `gegl:stretch-contrast-hsv`, `gegl:color-enhance` (chroma stretch),
  `gegl:color-temperature` (manual). No gray world, white patch or per
  channel automatic curves.
- **G'MIC** (gmic_stdlib.gmic, CeCILL-C; gmic-community): Colors/Color
  Balance (`fx_balance_gamma`: per channel gamma mapping a **picked**
  neutral to 128), Colors/Auto Balance (`gcd_auto_balance`,
  andy_kelday.gmic line 1719: per channel mean shift towards neutral plus a
  local tone map), LMS Adjustment with "Auto Balance",
  Testing/Garagecoder/Temperature Balance and Geometric Color Balance,
  Colors/Retinex, the equalizers, Transfer Colors (needs a reference
  image). Testing/Iain Fergusson "Auto WB by hue variance" and "Subtract
  Cast" are close in spirit but CC BY-NC-SA (non-free). Colors/Retro Fade
  and Frames/Old Photograph do the opposite (they age an image). **No
  faded dye restoration.**
- **darktable**: color calibration's automatic illuminant detection
  ("surfaces", a weighted gray world; "edges", gray edge like) is compiled
  out (`// #define AI_ACTIVATED`, `src/iop/channelmixerrgb.c` line 36,
  "AI feature not good enough so disabled for now"); rgb levels "auto" is a
  per channel min/max; the legacy color balance "neutralize colors" fits
  lift, gamma and gain per channel from three patches. Nothing for faded
  film.
- **RawTherapee**: automatic white balance "RGB grey" (clipped gray world)
  and "Temperature correlation" (`rtengine/procparams.cc` lines 1465 and
  1466); Auto Levels on luminance only. Nothing for faded film.

**Conclusion**: automatic restoration of faded dyes without a reference
(Daniell's niche) is not covered by GIMP, GEGL, G'MIC, darktable or
RawTherapee. Of the classical estimators, gray world and white patch fit
the power law model only partly (gains without gammas); per channel
histogram matching (Daniell's stage 1 is a robust version of it, on
colours not pixels) is the closest fit.

## 4. Which parts fit a GEGL operation

GIMP 3.2 runs a GEGL operation as a non-destructive filter with a dialog
built from its properties (the house repositories do this).

- Everything in pyrestore4.py fits **one single input GEGL op**: its
  estimate reads only the image, and a filter can ask for the whole input
  (`get_required_for_output` and `get_cached_region` the input's bounds,
  as gegl-presence does) and compute the statistics once, deterministically,
  on a reduced copy. The correction is a point operation.
- A **user picked neutral** (Chambah and Besserer's key colours,
  G'MIC's Color Balance, darktable's neutralize) fits too: GIMP 3.2.6
  gives a property pair with `unit` `relative-coordinate` and `axis` `x`
  and `y` the "Pick coordinates from the image" button
  (`app/propgui/gimppropgui-generic.c`, used by gegl-control-points'
  eight points). A few neutral points are enough; more points means more
  properties, fixed at compile time.
- A **plug-in** is only needed for what a filter cannot do: batch runs
  over folders (Daniell's batch versions; BIMP or a small Python plug-in
  can apply the filter to files), or showing several candidate results
  side by side (Kodak's preview variants).
- Side absorption correction needs densities from a narrow band scan to
  be meaningful (Gschwind); Daniell removed it in Restore2 because "it was
  not necessarily an improvement". Not planned.

## 5. Test images

- Generated: `tests/check.c` makes a scene and fades it with the model.
- Public domain faded slides: the EPA **DOCUMERICA** series (1972 to
  1977) at the US National Archives, on Wikimedia Commons, many visibly
  faded. Checked: "CHEMICAL PLANTS ON SHORE ARE CONSIDERED PRIME SOURCE OF
  POLLUTION" (Marc St. Gil, June 1972, NARA 546220), Commons page
  https://commons.wikimedia.org/wiki/File:CHEMICAL_PLANTS_ON_SHORE_ARE_CONSIDERED_PRIME_SOURCE_OF_POLLUTION_-_NARA_-_546220.jpg,
  licence templates `{{PD-USGov-EPA}}` and `{{NARA-cooperation}}` (read
  in the page's wikitext), a strong pink cast. Used for
  docs/example-documerica-546220.jpg (the 960 pixel wide Commons
  thumbnail; left the scan, middle `restore:faded` stage 1, right
  pyrestore4.py).
- The user's own photos are never used.
