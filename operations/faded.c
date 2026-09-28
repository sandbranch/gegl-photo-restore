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
 * SKELETON: this is only the first of the three stages of Daniell's
 * newest algorithm (pyrestore4.py): the per channel power law fit to an
 * "ideal" spread of colours. The grey balance in CIE L*u*v* and the
 * lightness and saturation enhancement are not done yet (PLAN.md).
 *
 * The model (Daniell, "Restoration of Faded Slides", 2008): losing dye
 * turns each channel value C of the gamma encoded scan into
 * C' = C^alpha (values 0 to 1), so a restoration is a power and a scale
 * per channel, out = sigma * in^lambda. The six numbers are estimated
 * from the colours that occur in the image, not from how many pixels
 * have them: on a box averaged copy of at most about 512 pixels on the
 * long side, the colours are binned 16 levels per channel (4096 bins,
 * as pyrestore4.py does with the top 4 bits), the mean colour of each
 * bin that has pixels is one colour, the colours are sorted by
 * luminance, and for each channel lambda and sigma are chosen so that
 * its values along that order come closest (least squares) to the ideal
 * ramp (i / n)^gamma, gamma being "Light-Dark".
 *
 * The estimate is made once from the whole input (get_required_for_output
 * asks for all of it) in a fixed order, so the result does not depend on
 * tiles, threads or the region asked for. Alpha is passed through, and
 * pixels that are not finite are passed through unchanged.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

property_double (light_dark, _("Light-Dark"), 1.0)
  description (_("The shape of the ideal spread of colours the image is "
                 "fitted to: 1 is an even spread, larger values give a "
                 "darker restoration, smaller values a lighter one "
                 "(PhotoRestore's Light-Dark)."))
  value_range (0.5, 2.0)
  ui_digits (2)

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     faded
#define GEGL_OP_C_SOURCE faded.c

#include "gegl-op.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FADED_STATS_SIZE 512   /* the long side of the averaged copy, at most about */
#define FADED_LEVELS     16    /* bins per channel */
#define FADED_MIN_COLORS 8     /* fewer distinct colours: nothing to fit, pass through */

typedef struct
{
  gdouble lambda[3];
  gdouble sigma[3];
  gint    n_colors;
  gboolean ok;
} FadedFit;

typedef struct
{
  gdouble rgb[3];
  gdouble Y;
  gint    index;
} FadedColor;

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

/* the estimate from the whole input, read in bands of rows in a fixed
 * order */
static FadedFit
estimate (GeglOperation       *operation,
          GeglBuffer          *input,
          const GeglRectangle *whole,
          gdouble              light_dark)
{
  const Babl *space   = gegl_operation_get_source_space (operation, "input");
  const Babl *rgba    = babl_format_with_space ("R'G'B'A float", space);
  const Babl *to_Y    = babl_fish (babl_format_with_space ("R'G'B' double", space),
                                   babl_format_with_space ("Y double", space));
  FadedFit    fit     = { { 1, 1, 1 }, { 1, 1, 1 }, 0, FALSE };
  gint        w       = whole->width, h = whole->height;
  gint        block   = MAX (1, (MAX (w, h) + FADED_STATS_SIZE - 1) / FADED_STATS_SIZE);
  gint        gw      = (w + block - 1) / block, gh = (h + block - 1) / block;
  gint        n_bins  = FADED_LEVELS * FADED_LEVELS * FADED_LEVELS;
  gdouble    *bins    = g_new0 (gdouble, (gsize) n_bins * 4);
  gdouble    *cells   = g_new (gdouble, (gsize) gw * 4);
  gfloat     *band    = g_new (gfloat, (gsize) w * block * 4);
  FadedColor *colors;
  gdouble    *c, *t;
  gint        gx, gy, i, n, ch;

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
          gdouble        m[3];
          gint           b = 0;

          if (cell[3] == 0.0)
            continue;
          for (ch = 0; ch < 3; ch++)
            {
              m[ch] = cell[ch] / cell[3];
              b = b * FADED_LEVELS + MIN ((gint) (m[ch] * FADED_LEVELS), FADED_LEVELS - 1);
            }
          for (ch = 0; ch < 3; ch++)
            bins[b * 4 + ch] += m[ch];
          bins[b * 4 + 3] += 1.0;
        }
    }
  g_free (band);
  g_free (cells);

  colors = g_new (FadedColor, n_bins);
  for (i = 0, n = 0; i < n_bins; i++)
    if (bins[i * 4 + 3] > 0.0)
      {
        for (ch = 0; ch < 3; ch++)
          colors[n].rgb[ch] = bins[i * 4 + ch] / bins[i * 4 + 3];
        babl_process (to_Y, colors[n].rgb, &colors[n].Y, 1);
        colors[n].index = i;
        n++;
      }
  g_free (bins);
  fit.n_colors = n;

  if (n >= FADED_MIN_COLORS)
    {
      qsort (colors, n, sizeof (FadedColor), compare_colors);
      c = g_new (gdouble, n);
      t = g_new (gdouble, n);
      for (i = 0; i < n; i++)
        t[i] = pow ((gdouble) i / n, light_dark);
      for (ch = 0; ch < 3; ch++)
        {
          for (i = 0; i < n; i++)
            c[i] = colors[i].rgb[ch];
          fit_channel (c, t, n, &fit.lambda[ch], &fit.sigma[ch]);
        }
      g_free (c);
      g_free (t);
      fit.ok = TRUE;
    }
  g_free (colors);
  return fit;
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties      *o     = GEGL_PROPERTIES (operation);
  const Babl          *fmt   = gegl_operation_get_format (operation, "output");
  const GeglRectangle *in    = gegl_operation_source_get_bounding_box (operation, "input");
  GeglRectangle        whole;
  GeglBufferIterator  *it;
  FadedFit             fit;

  (void) level;
  if (! in || gegl_rectangle_is_infinite_plane (in) || in->width <= 0 || in->height <= 0)
    {
      gegl_buffer_copy (input, result, GEGL_ABYSS_NONE, output, result);
      return TRUE;
    }
  whole = *in;
  fit   = estimate (operation, input, &whole, o->light_dark);
  if (! fit.ok)
    {
      gegl_buffer_copy (input, result, GEGL_ABYSS_NONE, output, result);
      return TRUE;
    }

  it = gegl_buffer_iterator_new (output, result, 0, fmt,
                                 GEGL_ACCESS_WRITE, GEGL_ABYSS_NONE, 2);
  gegl_buffer_iterator_add (it, input, result, 0, fmt, GEGL_ACCESS_READ, GEGL_ABYSS_NONE);
  while (gegl_buffer_iterator_next (it))
    {
      gfloat       *out = it->items[0].data;
      const gfloat *src = it->items[1].data;
      gint          i, ch;

      for (i = 0; i < it->length; i++, out += 4, src += 4)
        {
          for (ch = 0; ch < 3; ch++)
            {
              gfloat v = src[ch];

              if (! isfinite (v))
                out[ch] = v;
              else
                out[ch] = CLAMP (fit.sigma[ch] * pow (MAX (v, 0.0f), fit.lambda[ch]), 0.0, 1.0);
            }
          out[3] = src[3];
        }
    }
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
                         "photograph has faded and restores it (after Geoff "
                         "Daniell's PhotoRestore). Skeleton: only the first "
                         "stage, the per channel power law, is done."),
    "gimp:menu-path",  "<Image>/Colors/Auto",
    "gimp:menu-label", _("Restore Faded Colors..."),
    NULL);
}

#endif
