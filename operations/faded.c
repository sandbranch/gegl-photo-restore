/*
 * Restore faded colour photographs, after Geoff Daniell's PhotoRestore,
 * as a GEGL operation
 *
 * faded.c
 * Copyright 2026 by David
 * The method is Geoff Daniell's (PhotoRestore, Copyright 2008 Geoff
 * Daniell, GPL-3.0-or-later, https://github.com/stongey/PhotoRestore);
 * this file is a new implementation of it in C.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * The three stages of Daniell's newest algorithm (pyrestore4.py), with
 * the deviations the tests measured as better listed in the README.
 *
 * The model (Daniell, "Restoration of Faded Slides", 2008): losing dye
 * turns each channel value C of the gamma encoded scan into
 * C' = C^alpha (values 0 to 1), so a restoration is a power and a scale
 * per channel, out = sigma * in^lambda. The numbers are estimated from
 * the colours that occur in the image, not from how many pixels have
 * them. A box averaged copy of at most about 100 pixels on the long side
 * is made; its colours are binned 16 levels per channel (4096 bins, as
 * pyrestore4.py does with the top 4 bits), the mean colour of each bin
 * that has pixels is one colour, and the colours are sorted by
 * luminance.
 *
 *   1. Dye loss: for each channel, lambda and sigma so that its values
 *      along that order come closest (least squares) to the ideal ramp
 *      (i / n)^gamma, gamma being "Light-Dark".
 *   2. Grey balance ("Neutralize"): on the colours of the copy after
 *      stage 1, a second power and scale per channel so that the mean
 *      L* is 50 and u*, v* (CIE L*u*v*) are neutral on average and do
 *      not drift with lightness (the first three Legendre moments),
 *      pyrestore4.py's measure, minimised by Levenberg-Marquardt from no
 *      change, with a term that holds the six numbers towards no change
 *      (FADED_RIDGE). The neutral point is Daniell's empirical white
 *      (u' 0.2005, v' 0.4665 under D65), moved by "Warmth".
 *   3. Enhancement: the 2.5% and 97.5% lightness ends of the copy are
 *      moved half way to L* 10 and 95 by a two piece linear map around
 *      L* 50 ("Enhance lightness"), and u*, v* are scaled up (at most
 *      1.5 times) so that the mean saturation of the colours is at
 *      least "Saturation".
 *
 * Stages 1 and 2 combine into one power and scale per channel. "Amount"
 * blends every correction towards no change. The result is clipped to 0
 * to 1 as Daniell's (8 bit), except where the input was above 1 already
 * (high dynamic range float images keep those values). This is decided
 * by the values, not by the format of the input buffer, so the same
 * pixels give the same result whatever the precision.
 *
 * The estimate is made once from the whole input (get_required_for_output
 * asks for all of it) in a fixed order, so the result does not depend on
 * tiles, threads or the region asked for. Alpha is passed through, and
 * pixels that are not finite are passed through unchanged.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

property_double (light_dark, _("Light-Dark"), 1.5)
  description (_("The shape of the ideal spread of colours the image is "
                 "fitted to: 1 is an even spread (PhotoRestore's default), "
                 "larger values give a darker restoration, smaller values "
                 "a lighter one (PhotoRestore's Light-Dark)."))
  value_range (0.5, 2.0)
  ui_digits (2)

property_boolean (neutralize, _("Neutralize"), TRUE)
  description (_("Balance the colours so that they are neutral on average "
                 "at every lightness (the second stage)."))

property_double (warmth, _("Warmth"), 0.0)
  description (_("Moves the neutral point towards yellow (positive) or "
                 "blue (negative); 0 is PhotoRestore's own, slightly warm "
                 "white."))
  value_range (-1.0, 1.0)
  ui_digits (2)

property_boolean (enhance_lightness, _("Enhance lightness"), TRUE)
  description (_("Stretch the lightness: the darkest and lightest 2.5% "
                 "of the image are moved half way to L* 10 and 95."))

property_double (saturation, _("Saturation"), 0.4)
  description (_("If the mean saturation of the colours (CIE L*u*v*) is "
                 "below this, it is raised to it; 0 leaves the saturation "
                 "as it is."))
  value_range (0.0, 1.0)
  ui_digits (2)

property_double (amount, _("Amount"), 1.0)
  description (_("How much of the correction to apply: 0 is none, 1 all."))
  value_range (0.0, 1.0)
  ui_digits (2)

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     faded
#define GEGL_OP_C_SOURCE faded.c

#include "gegl-op.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FADED_STATS_SIZE 100   /* the long side of the averaged copy, at most about */
#define FADED_LEVELS     16    /* bins per channel */
#define FADED_BINS       (FADED_LEVELS * FADED_LEVELS * FADED_LEVELS)
#define FADED_MIN_COLORS 8     /* fewer distinct colours: nothing to fit, pass through */

/* pyrestore4.py's constants */
#define FADED_L0    50.0       /* the mean L* after stage 2 */
#define FADED_L_TOP 95.0
#define FADED_L_BOT 10.0
#define FADED_U0    0.2005     /* the empirical white, u' and v' under D65 */
#define FADED_V0    0.4665
/* Warmth 1 moves the white by half the way from D65 (u' 0.1978, v' 0.4683)
 * to D50 (u' 0.2092, v' 0.4881) */
#define FADED_WARM_U 0.00567
#define FADED_WARM_V 0.00989
/* stage 2 is held towards no change: a residual of this weight per log
 * of each of its six numbers (README: without it the full minimum
 * flattens photographs) */
#define FADED_RIDGE  1.0
/* the saturation factor at most (Daniell's Restore2 limited it to 1.5) */
#define FADED_SAT_CAP 1.5

typedef struct
{
  gdouble  lambda[3];    /* the combined power and scale per channel */
  gdouble  sigma[3];
  gdouble  p[2], q[2];   /* stage 3: L* > L0: p[0] L + q[0], else p[1] L + q[1] */
  gdouble  uvfac;        /* stage 3: u*, v* scale */
  gdouble  U0, V0;       /* the neutral point */
  gboolean stage3;
  gint     n_colors;
  gboolean ok;
} FadedFit;

typedef struct
{
  gdouble rgb[3];
  gdouble Y;
  gint    index;
} FadedColor;

/* conversions of the image's space, made once per estimate */
typedef struct
{
  const Babl *to_Y;      /* R'G'B' double -> Y double */
  const Babl *to_xyz;    /* R'G'B' double -> CIE XYZ double (D50) */
  gdouble     U0, V0;
} FadedSpace;

static void
prepare (GeglOperation *operation)
{
  const Babl *space  = gegl_operation_get_source_space (operation, "input");
  const Babl *format = babl_format_with_space ("R'G'B'A float", space);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

/* every result needs all of the input; an input without bounds (such as
 * gegl:color) is passed through */
static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation, "input");

  (void) input_pad;
  return in && ! gegl_rectangle_is_infinite_plane (in) ? *in : *roi;
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation, "input");

  return in && ! gegl_rectangle_is_infinite_plane (in) ? *in : *roi;
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *roi)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation, "input");

  (void) input_pad;
  return in && ! gegl_rectangle_is_infinite_plane (in) ? *in : *roi;
}

/* CIE L*u*v* ------------------------------------------------------------- */

/* GEGL's XYZ is adapted to D50 (as ICC profiles are); Daniell's numbers
 * are for D65, so XYZ is adapted back with Bradford's matrix */
static const gdouble d50_to_d65[9] = {  0.9555766, -0.0230393,  0.0631636,
                                       -0.0282895,  1.0099416,  0.0210077,
                                        0.0122982, -0.0204830,  1.3299098 };

static inline void
mat3 (const gdouble *m,
      const gdouble *in,
      gdouble       *out)
{
  gdouble x = in[0], y = in[1], z = in[2];

  out[0] = m[0] * x + m[1] * y + m[2] * z;
  out[1] = m[3] * x + m[4] * y + m[5] * z;
  out[2] = m[6] * x + m[7] * y + m[8] * z;
}

/* XYZ under D65 (Y of white 1) to L*, u*, v* around the neutral U0, V0 */
static inline void
xyz_to_luv (const gdouble *xyz,
            gdouble        U0,
            gdouble        V0,
            gdouble       *luv)
{
  gdouble Y = xyz[1], d = xyz[0] + 15.0 * Y + 3.0 * xyz[2];
  gdouble L = Y > 216.0 / 24389.0 ? 116.0 * cbrt (Y) - 16.0 : 24389.0 / 27.0 * Y;

  luv[0] = L;
  if (d > 1e-12 && L > 0.0)
    {
      luv[1] = 13.0 * L * (4.0 * xyz[0] / d - U0);
      luv[2] = 13.0 * L * (9.0 * Y / d - V0);
    }
  else
    luv[1] = luv[2] = 0.0;
}

static inline void
luv_to_xyz (const gdouble *luv,
            gdouble        U0,
            gdouble        V0,
            gdouble       *xyz)
{
  gdouble L = luv[0], u, v, Y;

  if (L <= 0.0)
    {
      xyz[0] = xyz[1] = xyz[2] = 0.0;
      return;
    }
  u = luv[1] / (13.0 * L) + U0;
  v = luv[2] / (13.0 * L) + V0;
  Y = L > 8.0 ? pow ((L + 16.0) / 116.0, 3.0) : L * 27.0 / 24389.0;
  if (v <= 1e-12)
    {
      xyz[0] = xyz[1] = xyz[2] = 0.0;
      return;
    }
  xyz[0] = Y * 9.0 * u / (4.0 * v);
  xyz[1] = Y;
  xyz[2] = Y * (12.0 - 3.0 * u - 20.0 * v) / (4.0 * v);
}

/* n colours of R'G'B' (3 doubles each) to L*u*v* (3 doubles each, may be
 * the same array) */
static void
rgb_to_luv (const FadedSpace *fs,
            const gdouble    *rgb,
            gdouble          *luv,
            gint              n)
{
  gint i;

  babl_process (fs->to_xyz, rgb, luv, n);
  for (i = 0; i < n; i++)
    {
      gdouble x[3];

      mat3 (d50_to_d65, luv + i * 3, x);
      xyz_to_luv (x, fs->U0, fs->V0, luv + i * 3);
    }
}

/* the colour list ---------------------------------------------------------- */

static gint
compare_colors (const void *a,
                const void *b)
{
  const FadedColor *p = a, *q = b;

  if (p->Y < q->Y)
    return -1;
  if (p->Y > q->Y)
    return 1;
  return p->index - q->index;
}

static inline gdouble
power (gdouble v,
       gdouble lambda,
       gdouble sigma)
{
  return v > 0.0 ? sigma * pow (v, lambda) : 0.0;
}

/* the distinct colours of the copy (n pixels, 3 floats each) after the
 * power and scale per channel (clipped to 0 to 1), sorted by luminance;
 * colors needs room for FADED_BINS; returns how many */
static gint
make_colors (const FadedSpace *fs,
             const gfloat     *copy,
             gint              n,
             const gdouble    *lambda,
             const gdouble    *sigma,
             FadedColor       *colors)
{
  gdouble *bins = g_new0 (gdouble, (gsize) FADED_BINS * 4);
  gint     i, k, ch;

  for (i = 0; i < n; i++)
    {
      gdouble m[3];
      gint    b = 0;

      for (ch = 0; ch < 3; ch++)
        {
          m[ch] = CLAMP (power (copy[i * 3 + ch], lambda[ch], sigma[ch]), 0.0, 1.0);
          b = b * FADED_LEVELS + MIN ((gint) (m[ch] * FADED_LEVELS), FADED_LEVELS - 1);
        }
      for (ch = 0; ch < 3; ch++)
        bins[b * 4 + ch] += m[ch];
      bins[b * 4 + 3] += 1.0;
    }
  for (i = 0, k = 0; i < FADED_BINS; i++)
    if (bins[i * 4 + 3] > 0.0)
      {
        for (ch = 0; ch < 3; ch++)
          colors[k].rgb[ch] = bins[i * 4 + ch] / bins[i * 4 + 3];
        babl_process (fs->to_Y, colors[k].rgb, &colors[k].Y, 1);
        colors[k].index = i;
        k++;
      }
  g_free (bins);
  qsort (colors, k, sizeof (FadedColor), compare_colors);
  return k;
}

/* stage 1 ------------------------------------------------------------------ */

/* sum over the colours of (sigma c^lambda - t)^2 with the best sigma for
 * this lambda (sigma = sum t p / sum p p), which it returns in *sigma */
static gdouble
misfit (const gdouble *c,
        const gdouble *t,
        gint           n,
        gdouble        lambda,
        gdouble       *sigma)
{
  gdouble tp = 0.0, pp = 0.0, tt = 0.0, s;
  gint    i;

  for (i = 0; i < n; i++)
    {
      gdouble p = c[i] > 0.0 ? pow (c[i], lambda) : 0.0;

      tp += t[i] * p;
      pp += p * p;
      tt += t[i] * t[i];
    }
  s = pp > 0.0 ? tp / pp : 1.0;
  *sigma = s;
  /* sum (s p - t)^2 = s^2 pp - 2 s tp + tt */
  return s * s * pp - 2.0 * s * tp + tt;
}

/* the lambda (and sigma) of one channel: a grid over log lambda from
 * 1/10 to 10, then golden section search around the best grid point */
static void
fit_channel (const gdouble *c,
             const gdouble *t,
             gint           n,
             gdouble       *lambda,
             gdouble       *sigma)
{
  const gint    steps = 200;
  const gdouble lo = log (0.1), hi = log (10.0), h = (hi - lo) / steps;
  const gdouble g = 0.5 * (sqrt (5.0) - 1.0);
  gdouble       best = G_MAXDOUBLE, a, b, x1, x2, f1, f2, s;
  gint          i, k = 0;

  for (i = 0; i <= steps; i++)
    {
      gdouble f = misfit (c, t, n, exp (lo + i * h), &s);

      if (f < best)
        {
          best = f;
          k = i;
        }
    }
  a  = lo + MAX (k - 1, 0) * h;
  b  = lo + MIN (k + 1, steps) * h;
  x1 = b - g * (b - a);
  x2 = a + g * (b - a);
  f1 = misfit (c, t, n, exp (x1), &s);
  f2 = misfit (c, t, n, exp (x2), &s);
  for (i = 0; i < 60; i++)
    {
      if (f1 < f2)
        {
          b  = x2;
          x2 = x1;
          f2 = f1;
          x1 = b - g * (b - a);
          f1 = misfit (c, t, n, exp (x1), &s);
        }
      else
        {
          a  = x1;
          x1 = x2;
          f1 = f2;
          x2 = a + g * (b - a);
          f2 = misfit (c, t, n, exp (x2), &s);
        }
    }
  *lambda = exp (0.5 * (a + b));
  misfit (c, t, n, *lambda, sigma);
}

static void
stage1 (const FadedColor *colors,
        gint              n,
        gdouble           light_dark,
        gdouble          *lambda,
        gdouble          *sigma)
{
  gdouble *c = g_new (gdouble, n), *t = g_new (gdouble, n);
  gint     i, ch;

  for (i = 0; i < n; i++)
    t[i] = pow ((gdouble) i / n, light_dark);
  for (ch = 0; ch < 3; ch++)
    {
      for (i = 0; i < n; i++)
        c[i] = colors[i].rgb[ch];
      fit_channel (c, t, n, &lambda[ch], &sigma[ch]);
    }
  g_free (c);
  g_free (t);
}

/* stage 2 ------------------------------------------------------------------ */

#define FADED_N_RES 13

typedef struct
{
  const FadedSpace *fs;
  const FadedColor *colors;
  gint              n;
  gdouble          *work;   /* 3 n doubles */
} Stage2;

/* pyrestore4.py's seven terms (as residuals, their squares summed is his
 * measure) for x = lambda r, g, b, sigma r, g, b applied to the colours */
static gdouble
stage2_residuals (Stage2        *s,
                  const gdouble *x,
                  gdouble       *r)
{
  gdouble sL = 0, u0 = 0, u1 = 0, u2 = 0, v0 = 0, v1 = 0, v2 = 0, sum = 0;
  gint    i, ch;

  for (i = 0; i < s->n; i++)
    for (ch = 0; ch < 3; ch++)
      s->work[i * 3 + ch] = CLAMP (power (s->colors[i].rgb[ch], x[ch], x[3 + ch]), 0.0, 1.0);
  rgb_to_luv (s->fs, s->work, s->work, s->n);
  for (i = 0; i < s->n; i++)
    {
      const gdouble *luv = s->work + i * 3;
      gdouble        l   = luv[0] / FADED_L0 - 1.0;

      sL += luv[0];
      u0 += luv[1];
      u1 += luv[1] * l;
      u2 += luv[1] * l * l;
      v0 += luv[2];
      v1 += luv[2] * l;
      v2 += luv[2] * l * l;
    }
  sL /= s->n; u0 /= s->n; u1 /= s->n; u2 /= s->n; v0 /= s->n; v1 /= s->n; v2 /= s->n;
  r[0] = sL - FADED_L0;
  r[1] = u0;
  r[2] = 3.0 * u1 / sqrt (3.0);
  r[3] = 2.5 * (3.0 * u2 - u0) / sqrt (5.0);
  r[4] = v0;
  r[5] = 3.0 * v1 / sqrt (3.0);
  r[6] = 2.5 * (3.0 * v2 - v0) / sqrt (5.0);
  for (ch = 0; ch < 3; ch++)
    {
      r[7 + ch]  = FADED_RIDGE * log (x[ch]);
      r[10 + ch] = FADED_RIDGE * log (x[3 + ch]);
    }
  for (i = 0; i < FADED_N_RES; i++)
    sum += r[i] * r[i];
  return sum;
}

/* solves the 6 x 6 system a x = b (a is overwritten); FALSE if singular */
static gboolean
solve6 (gdouble *a,
        gdouble *b)
{
  gint i, j, k;

  for (k = 0; k < 6; k++)
    {
      gint    piv = k;
      gdouble m;

      for (i = k + 1; i < 6; i++)
        if (fabs (a[i * 6 + k]) > fabs (a[piv * 6 + k]))
          piv = i;
      if (fabs (a[piv * 6 + k]) < 1e-300)
        return FALSE;
      if (piv != k)
        {
          for (j = 0; j < 6; j++)
            {
              gdouble t = a[k * 6 + j];

              a[k * 6 + j]   = a[piv * 6 + j];
              a[piv * 6 + j] = t;
            }
          m = b[k]; b[k] = b[piv]; b[piv] = m;
        }
      for (i = k + 1; i < 6; i++)
        {
          m = a[i * 6 + k] / a[k * 6 + k];
          for (j = k; j < 6; j++)
            a[i * 6 + j] -= m * a[k * 6 + j];
          b[i] -= m * b[k];
        }
    }
  for (k = 5; k >= 0; k--)
    {
      for (j = k + 1; j < 6; j++)
        b[k] -= a[k * 6 + j] * b[j];
      b[k] /= a[k * 6 + k];
    }
  return TRUE;
}

/* Levenberg-Marquardt from no change (all six 1), with forward
 * difference derivatives; deterministic. Returns the final measure. */
static gdouble
stage2 (Stage2  *s,
        gdouble *x)
{
  const gdouble h = 1e-6;
  gdouble       r[FADED_N_RES], rh[FADED_N_RES], J[FADED_N_RES * 6];
  gdouble       cost, mu = 1e-3;
  gint          iter, i, j, k;

  for (k = 0; k < 6; k++)
    x[k] = 1.0;
  cost = stage2_residuals (s, x, r);
  for (iter = 0; iter < 100; iter++)
    {
      gdouble  A[36], g[6];
      gboolean better = FALSE;

      for (k = 0; k < 6; k++)
        {
          gdouble xh[6];

          memcpy (xh, x, sizeof xh);
          xh[k] += h;
          stage2_residuals (s, xh, rh);
          for (i = 0; i < FADED_N_RES; i++)
            J[i * 6 + k] = (rh[i] - r[i]) / h;
        }
      for (j = 0; j < 6; j++)
        {
          g[j] = 0.0;
          for (i = 0; i < FADED_N_RES; i++)
            g[j] += J[i * 6 + j] * r[i];
          for (k = 0; k < 6; k++)
            {
              A[j * 6 + k] = 0.0;
              for (i = 0; i < FADED_N_RES; i++)
                A[j * 6 + k] += J[i * 6 + j] * J[i * 6 + k];
            }
        }
      while (mu < 1e12)
        {
          gdouble M[36], d[6], xn[6], rn[FADED_N_RES], cn;
          gboolean valid = TRUE;

          memcpy (M, A, sizeof M);
          for (k = 0; k < 6; k++)
            {
              M[k * 6 + k] += mu * (A[k * 6 + k] + 1e-9);
              d[k] = -g[k];
            }
          if (solve6 (M, d))
            {
              for (k = 0; k < 6; k++)
                {
                  xn[k] = x[k] + d[k];
                  if (! (xn[k] > 0.01 && xn[k] < 100.0))
                    valid = FALSE;
                }
              if (valid)
                {
                  cn = stage2_residuals (s, xn, rn);
                  if (cn < cost)
                    {
                      gdouble gain = cost - cn;

                      memcpy (x, xn, sizeof xn);
                      memcpy (r, rn, sizeof rn);
                      cost = cn;
                      mu = MAX (mu * 0.1, 1e-12);
                      better = gain > 1e-12 * (1.0 + cost);
                      break;
                    }
                }
            }
          mu *= 10.0;
        }
      if (! better)
        break;
    }
  return cost;
}

/* stage 3 ------------------------------------------------------------------ */

static gint
compare_double (const void *a,
                const void *b)
{
  gdouble p = *(const gdouble *) a, q = *(const gdouble *) b;

  return p < q ? -1 : p > q;
}

/* the lightness map and saturation factor from the copy after the
 * combined correction (as pyrestore4.py's get_enhance_params and the
 * code after it) */
static void
stage3 (const FadedSpace *fs,
        const gfloat     *copy,
        gint              n,
        const gdouble    *lambda,
        const gdouble    *sigma,
        gboolean          lightness,
        gdouble           sat_target,
        FadedFit         *fit)
{
  gdouble    *L      = g_new (gdouble, (gsize) n * 3);
  FadedColor *colors = g_new (FadedColor, FADED_BINS);
  gdouble    *luv;
  gint        i, ch, k, nc, m;
  gdouble     sat = 0.0;

  fit->p[0] = fit->p[1] = 1.0;
  fit->q[0] = fit->q[1] = 0.0;
  fit->uvfac = 1.0;

  if (lightness)
    {
      gdouble Lmin, Lmax, L_hi, L_lo;

      for (i = 0; i < n; i++)
        for (ch = 0; ch < 3; ch++)
          L[i * 3 + ch] = CLAMP (power (copy[i * 3 + ch], lambda[ch], sigma[ch]), 0.0, 1.0);
      rgb_to_luv (fs, L, L, n);
      for (i = 0; i < n; i++)
        L[i] = L[i * 3];
      qsort (L, n, sizeof (gdouble), compare_double);
      k    = (gint) (0.025 * n);
      Lmin = L[MIN (k, n - 1)];
      Lmax = L[MAX (n - 1 - k, 0)];
      L_hi = 0.5 * (FADED_L_TOP + Lmax);
      L_lo = MAX (0.5 * (FADED_L_BOT + Lmin), FADED_L_BOT);
      if (Lmax > FADED_L0 + 1.0)
        {
          fit->p[0] = (L_hi - FADED_L0) / (Lmax - FADED_L0);
          fit->q[0] = FADED_L0 * (Lmax - L_hi) / (Lmax - FADED_L0);
        }
      if (Lmin < FADED_L0 - 1.0)
        {
          fit->p[1] = (L_lo - FADED_L0) / (Lmin - FADED_L0);
          fit->q[1] = FADED_L0 * (Lmin - L_lo) / (Lmin - FADED_L0);
        }
    }

  if (sat_target > 0.0)
    {
      nc  = make_colors (fs, copy, n, lambda, sigma, colors);
      luv = g_new (gdouble, (gsize) nc * 3);
      for (i = 0; i < nc; i++)
        for (ch = 0; ch < 3; ch++)
          luv[i * 3 + ch] = colors[i].rgb[ch];
      rgb_to_luv (fs, luv, luv, nc);
      for (i = 0, m = 0; i < nc; i++)
        if (luv[i * 3] > 1.0)
          {
            sat += hypot (luv[i * 3 + 1], luv[i * 3 + 2]) / luv[i * 3];
            m++;
          }
      g_free (luv);
      if (m > 0)
        {
          sat /= m;
          if (sat > 1e-6 && sat < sat_target)
            fit->uvfac = sat_target / sat;
          fit->uvfac = MIN (fit->uvfac, FADED_SAT_CAP);
        }
    }
  g_free (colors);
  g_free (L);
}

/* the estimate ------------------------------------------------------------- */

/* the box averaged copy of the whole input, read in bands of rows in a
 * fixed order; returns it (3 floats per pixel, *n pixels) */
static gfloat *
make_copy (GeglBuffer          *input,
           const GeglRectangle *whole,
           const Babl          *rgba,
           gint                *n)
{
  gint    w     = whole->width, h = whole->height;
  gint    block = MAX (1, (MAX (w, h) + FADED_STATS_SIZE - 1) / FADED_STATS_SIZE);
  gint    gw    = (w + block - 1) / block, gh = (h + block - 1) / block;
  gfloat *copy  = g_new (gfloat, (gsize) gw * gh * 3);
  gdouble *cells = g_new (gdouble, (gsize) gw * 4);
  gfloat *band  = g_new (gfloat, (gsize) w * block * 4);
  gint    gx, gy, k = 0;

  for (gy = 0; gy < gh; gy++)
    {
      GeglRectangle r = { whole->x, whole->y + gy * block, w, MIN (block, h - gy * block) };
      gint          x, y;

      gegl_buffer_get (input, &r, 1.0, rgba, band, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
      memset (cells, 0, sizeof (gdouble) * gw * 4);
      for (y = 0; y < r.height; y++)
        for (x = 0; x < w; x++)
          {
            const gfloat *p    = band + ((gsize) y * w + x) * 4;
            gdouble      *cell = cells + (x / block) * 4;

            if (! (isfinite (p[0]) && isfinite (p[1]) && isfinite (p[2]) && p[3] > 0.0f))
              continue;
            cell[0] += CLAMP (p[0], 0.0f, 1.0f);
            cell[1] += CLAMP (p[1], 0.0f, 1.0f);
            cell[2] += CLAMP (p[2], 0.0f, 1.0f);
            cell[3] += 1.0;
          }
      for (gx = 0; gx < gw; gx++)
        {
          const gdouble *cell = cells + gx * 4;
          gint           ch;

          if (cell[3] == 0.0)
            continue;
          for (ch = 0; ch < 3; ch++)
            copy[k * 3 + ch] = cell[ch] / cell[3];
          k++;
        }
    }
  g_free (band);
  g_free (cells);
  *n = k;
  return copy;
}

static FadedFit
estimate (GeglOperation       *operation,
          GeglBuffer          *input,
          const GeglRectangle *whole,
          GeglProperties      *o)
{
  const Babl *space = gegl_operation_get_source_space (operation, "input");
  const Babl *rgb   = babl_format_with_space ("R'G'B' double", space);
  FadedFit    fit   = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1 }, { 0, 0 }, 1.0,
                        FADED_U0, FADED_V0, FALSE, 0, FALSE };
  FadedSpace  fs;
  FadedColor *colors;
  gfloat     *copy;
  gdouble     lam1[3], sig1[3], x[6] = { 1, 1, 1, 1, 1, 1 }, a = o->amount;
  gint        n, nc, ch;

  fs.to_Y   = babl_fish (rgb, babl_format_with_space ("Y double", space));
  fs.to_xyz = babl_fish (rgb, babl_format ("CIE XYZ double"));
  fs.U0     = fit.U0 = FADED_U0 + o->warmth * FADED_WARM_U;
  fs.V0     = fit.V0 = FADED_V0 + o->warmth * FADED_WARM_V;

  copy   = make_copy (input, whole, babl_format_with_space ("R'G'B'A float", space), &n);
  colors = g_new (FadedColor, FADED_BINS);
  nc     = make_colors (&fs, copy, n, fit.lambda, fit.sigma, colors);
  fit.n_colors = nc;
  if (nc < FADED_MIN_COLORS)
    {
      g_free (colors);
      g_free (copy);
      return fit;
    }

  stage1 (colors, nc, o->light_dark, lam1, sig1);
  if (o->neutralize)
    {
      Stage2  s;
      gdouble cost;

      s.fs     = &fs;
      s.colors = colors;
      s.n      = make_colors (&fs, copy, n, lam1, sig1, colors);
      s.work   = g_new (gdouble, (gsize) s.n * 3);
      if (s.n >= FADED_MIN_COLORS)
        {
          cost = stage2 (&s, x);
          g_debug ("restore:faded stage 2: measure %.3g (pyrestore4.py: good below 2, "
                   "bad above 5), lambda %.3f %.3f %.3f, sigma %.3f %.3f %.3f",
                   cost, x[0], x[1], x[2], x[3], x[4], x[5]);
        }
      g_free (s.work);
    }
  /* stage 2 after stage 1: sigma2 (sigma1 c^lambda1)^lambda2 */
  for (ch = 0; ch < 3; ch++)
    {
      fit.lambda[ch] = lam1[ch] * x[ch];
      fit.sigma[ch]  = x[3 + ch] * pow (sig1[ch], x[ch]);
    }
  g_free (colors);

  stage3 (&fs, copy, n, fit.lambda, fit.sigma, o->enhance_lightness, o->saturation, &fit);
  g_free (copy);

  /* Amount: everything towards no change */
  for (ch = 0; ch < 3; ch++)
    {
      fit.lambda[ch] = 1.0 + a * (fit.lambda[ch] - 1.0);
      fit.sigma[ch]  = 1.0 + a * (fit.sigma[ch] - 1.0);
    }
  for (ch = 0; ch < 2; ch++)
    {
      fit.p[ch] = 1.0 + a * (fit.p[ch] - 1.0);
      fit.q[ch] = a * fit.q[ch];
    }
  fit.uvfac  = 1.0 + a * (fit.uvfac - 1.0);
  fit.stage3 = fit.p[0] != 1.0 || fit.p[1] != 1.0 || fit.q[0] != 0.0 || fit.q[1] != 0.0
               || fit.uvfac != 1.0;
  fit.ok     = TRUE;
  return fit;
}

/* the result --------------------------------------------------------------- */


typedef struct
{
  GeglBuffer     *input, *output;
  const Babl     *fmt, *to_xyz, *from_xyz;   /* R'G'B'A float <-> RGBA float */
  gdouble         m[9], minv[9];             /* linear RGB <-> XYZ under D65 */
  const FadedFit *fit;
} FadedApply;

/* the inverse of a 3 x 3 matrix */
static void
invert3 (const gdouble *m,
         gdouble       *r)
{
  gdouble det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6])
                + m[2] * (m[3] * m[7] - m[4] * m[6]);

  r[0] = (m[4] * m[8] - m[5] * m[7]) / det;
  r[1] = (m[2] * m[7] - m[1] * m[8]) / det;
  r[2] = (m[1] * m[5] - m[2] * m[4]) / det;
  r[3] = (m[5] * m[6] - m[3] * m[8]) / det;
  r[4] = (m[0] * m[8] - m[2] * m[6]) / det;
  r[5] = (m[2] * m[3] - m[0] * m[5]) / det;
  r[6] = (m[3] * m[7] - m[4] * m[6]) / det;
  r[7] = (m[1] * m[6] - m[0] * m[7]) / det;
  r[8] = (m[0] * m[4] - m[1] * m[3]) / det;
}

/* the correction of one part of the result (run in parallel) */
static void
apply_area (const GeglRectangle *area,
            gpointer             data)
{
  FadedApply         *d   = data, *dd = data;
  const FadedFit     *fit = d->fit;
  GeglBufferIterator *it;
  gfloat             *xyz = NULL;

  it = gegl_buffer_iterator_new (d->output, area, 0, d->fmt,
                                 GEGL_ACCESS_WRITE, GEGL_ABYSS_NONE, 2);
  gegl_buffer_iterator_add (it, d->input, area, 0, d->fmt, GEGL_ACCESS_READ, GEGL_ABYSS_NONE);
  while (gegl_buffer_iterator_next (it))
    {
      gfloat       *out = it->items[0].data;
      const gfloat *src = it->items[1].data;
      gint          i, ch, len = it->length;

      for (i = 0; i < len; i++)
        {
          for (ch = 0; ch < 3; ch++)
            {
              gfloat v = src[i * 4 + ch];

              if (! isfinite (v))
                out[i * 4 + ch] = v;
              else
                {
                  gdouble r = power (v, fit->lambda[ch], fit->sigma[ch]);

                  /* clipped at 1 unless the input was above 1 already */
                  out[i * 4 + ch] = v > 1.0f ? r : MIN (r, 1.0);
                }
            }
          out[i * 4 + 3] = src[i * 4 + 3];
        }

      if (! fit->stage3)
        continue;

      xyz = g_renew (gfloat, xyz, (gsize) len * 4);
      babl_process (d->to_xyz, out, xyz, len);
      for (i = 0; i < len; i++)
        {
          gfloat *p = xyz + i * 4;
          gdouble x[3], d[3], luv[3];

          if (! (isfinite (src[i * 4]) && isfinite (src[i * 4 + 1]) && isfinite (src[i * 4 + 2])))
            continue;
          d[0] = p[0]; d[1] = p[1]; d[2] = p[2];
          mat3 (dd->m, d, x);
          xyz_to_luv (x, fit->U0, fit->V0, luv);
          luv[0] = luv[0] > FADED_L0 ? fit->p[0] * luv[0] + fit->q[0]
                                     : fit->p[1] * luv[0] + fit->q[1];
          luv[1] *= fit->uvfac;
          luv[2] *= fit->uvfac;
          luv_to_xyz (luv, fit->U0, fit->V0, x);
          mat3 (dd->minv, x, d);
          p[0] = d[0]; p[1] = d[1]; p[2] = d[2];
        }
      babl_process (d->from_xyz, xyz, out, len);
      for (i = 0; i < len; i++)
        {
          gboolean finite = isfinite (src[i * 4]) && isfinite (src[i * 4 + 1])
                            && isfinite (src[i * 4 + 2]);
          gboolean above  = src[i * 4] > 1.0f || src[i * 4 + 1] > 1.0f || src[i * 4 + 2] > 1.0f;

          for (ch = 0; ch < 3; ch++)
            {
              gfloat v = out[i * 4 + ch];

              if (! finite)
                {
                  gfloat s = src[i * 4 + ch];

                  v = isfinite (s) ? power (s, fit->lambda[ch], fit->sigma[ch]) : s;
                  if (isfinite (v) && s <= 1.0f)
                    v = MIN (v, 1.0f);
                  out[i * 4 + ch] = v;
                  continue;
                }
              v = MAX (v, 0.0f);
              out[i * 4 + ch] = above ? v : MIN (v, 1.0f);
            }
          out[i * 4 + 3] = src[i * 4 + 3];
        }
    }
  g_free (xyz);
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties      *o   = GEGL_PROPERTIES (operation);
  const Babl          *fmt = gegl_operation_get_format (operation, "output");
  const GeglRectangle *in  = gegl_operation_source_get_bounding_box (operation, "input");
  GeglRectangle        whole;
  FadedFit             fit;
  FadedApply           d;

  (void) level;
  if (! in || gegl_rectangle_is_infinite_plane (in) || in->width <= 0 || in->height <= 0
      || o->amount <= 0.0)
    {
      gegl_buffer_copy (input, result, GEGL_ABYSS_NONE, output, result);
      return TRUE;
    }
  whole = *in;
  fit   = estimate (operation, input, &whole, o);
  if (! fit.ok)
    {
      gegl_buffer_copy (input, result, GEGL_ABYSS_NONE, output, result);
      return TRUE;
    }

  d.input    = input;
  d.output   = output;
  d.fmt      = fmt;
  {
    /* the space's linear RGB to XYZ (D50), from three conversions, then
     * adapted to D65 */
    const Babl *space = gegl_operation_get_source_space (operation, "input");
    const Babl *lin   = babl_format_with_space ("RGBA float", space);
    gdouble     unit[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, col[3], m50[9], t[3];
    gint        i, j;

    for (j = 0; j < 3; j++)
      {
        babl_process (babl_fish (babl_format_with_space ("RGB double", space),
                                 babl_format ("CIE XYZ double")), unit[j], col, 1);
        for (i = 0; i < 3; i++)
          m50[i * 3 + j] = col[i];
      }
    for (j = 0; j < 3; j++)
      {
        gdouble c[3] = { m50[j], m50[3 + j], m50[6 + j] };

        mat3 (d50_to_d65, c, t);
        for (i = 0; i < 3; i++)
          d.m[i * 3 + j] = t[i];
      }
    invert3 (d.m, d.minv);
    d.to_xyz   = babl_fish (fmt, lin);
    d.from_xyz = babl_fish (lin, fmt);
  }
  d.fit      = &fit;
  gegl_parallel_distribute_area (result, 64.0, GEGL_SPLIT_STRATEGY_AUTO, apply_area, &d);
  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationFilterClass *filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  operation_class->prepare                   = prepare;
  operation_class->get_required_for_output   = get_required_for_output;
  operation_class->get_cached_region         = get_cached_region;
  operation_class->get_invalidated_by_change = get_invalidated_by_change;
  operation_class->threaded                  = FALSE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "restore:faded",
    "title",           _("Restore Faded Colors"),
    "categories",      "color",
    "description",     _("Estimates how much of each dye of an old colour "
                         "photograph has faded and restores it: a power and a "
                         "scale per channel, a grey balance and a lightness and "
                         "saturation stretch, after Geoff Daniell's "
                         "PhotoRestore."),
    "gimp:menu-path",  "<Image>/Colors/Auto",
    "gimp:menu-label", _("Restore Faded Colors..."),
    NULL);
}

#endif
