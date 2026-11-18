/*
 * Automated checks for restore:faded
 *
 * check.c
 * Copyright 2026 by David
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
 * Skeleton checks: a generated scene is faded with Daniell's dye loss
 * model (a power and a scale per channel of the gamma encoded values)
 * for three kinds of fading and restored; the colour cast must be mostly
 * removed, and the Delta E 1976 to the scene and to the restored unfaded
 * scene are printed (see check_fading); gray stays gray, alpha and broken pixels
 * pass through, few colours pass through, Light-Dark darkens, and the
 * result is the same for any tile size, requested region and buffer
 * origin; show=colour-map draws its plot in the lower left corner only.
 *
 *   check <build folder>
 *
 * Prints PASS, FAIL or NOTE per case and exits with 1 if any failed.
 */

#include <gegl.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modules.h"

#define FMT "R'G'B'A float"
/* the format of the input buffers: 16 bit, as most scans, so the result
 * is clipped to 0 to 1 (float input keeps values above 1) */
static const gchar *in_fmt = "R'G'B'A u16";
#define OP  "restore:faded"
#define W   384
#define H   256

static gint n_failed   = 0;
static gint n_passed   = 0;
static gint n_messages = 0;

static void
log_handler (const gchar    *domain,
             GLogLevelFlags  level,
             const gchar    *message,
             gpointer        data)
{
  (void) data;
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
    n_messages++;
  g_log_default_handler (domain, level, message, NULL);
}

static void report (const gchar *name, gboolean ok, const gchar *format, ...) G_GNUC_PRINTF (3, 4);

static void
report (const gchar *name,
        gboolean     ok,
        const gchar *format,
        ...)
{
  printf ("%s  %s", ok ? "PASS" : "FAIL", name);
  if (format)
    {
      va_list args;
      gchar  *detail;

      va_start (args, format);
      detail = g_strdup_vprintf (format, args);
      va_end (args);
      printf (": %s", detail);
      g_free (detail);
    }
  printf ("\n");
  fflush (stdout);
  if (ok)
    n_passed++;
  else
    n_failed++;
}

/* running the operation --------------------------------------------------- */

/* op with props ("name=value ...") on w x h pixels of FMT at origin
 * ox, oy with tiles of tile_w x tile_h (0: GEGL's), the region roi
 * (relative to the origin; NULL: all). g_free the result. */
static gfloat *
run_full (const gchar         *props,
          const gfloat        *in,
          gint                 w,
          gint                 h,
          gint                 ox,
          gint                 oy,
          gint                 tile_w,
          gint                 tile_h,
          const GeglRectangle *roi)
{
  const Babl   *fmt = babl_format (FMT);
  const Babl   *bfmt = babl_format (in_fmt);
  GeglBuffer   *buf;
  GeglNode     *graph, *src, *node;
  GeglRectangle r   = roi ? *roi : *GEGL_RECTANGLE (0, 0, w, h);
  gfloat       *out;
  gchar       **p   = g_strsplit (props ? props : "", " ", -1);
  gint          i;

  if (tile_w > 0)
    buf = g_object_new (GEGL_TYPE_BUFFER, "x", ox, "y", oy, "width", w, "height", h,
                        "format", bfmt, "tile-width", tile_w, "tile-height", tile_h, NULL);
  else
    buf = gegl_buffer_new (GEGL_RECTANGLE (ox, oy, w, h), bfmt);
  gegl_buffer_set (buf, GEGL_RECTANGLE (ox, oy, w, h), 0, fmt, in, GEGL_AUTO_ROWSTRIDE);
  graph = gegl_node_new ();
  src   = gegl_node_new_child (graph, "operation", "gegl:buffer-source", "buffer", buf, NULL);
  node  = gegl_node_new_child (graph, "operation", OP, NULL);
  for (i = 0; p[i]; i++)
    if (p[i][0] && ! node_set_from_string (node, p[i]))
      report ("set_property", FALSE, "%s", p[i]);
  g_strfreev (p);
  gegl_node_link (src, node);
  r.x += ox;
  r.y += oy;
  out = g_new0 (gfloat, (gsize) r.width * r.height * 4);
  gegl_node_blit (node, 1.0, &r, fmt, out, GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
  g_object_unref (graph);
  g_object_unref (buf);
  return out;
}

static gfloat *
run (const gchar  *props,
     const gfloat *in,
     gint          w,
     gint          h)
{
  return run_full (props, in, w, h, 0, 0, 0, 0, NULL);
}

static gdouble
max_diff (const gfloat *a,
          const gfloat *b,
          gsize         n)
{
  gdouble d = 0.0;
  gsize   i;

  for (i = 0; i < n; i++)
    {
      gboolean fa = isfinite (a[i]), fb = isfinite (b[i]);

      if (fa != fb)
        return INFINITY;
      if (fa)
        d = MAX (d, fabs (a[i] - b[i]));
    }
  return d;
}

/* the scene and the fading ------------------------------------------------ */

/* hues across, lightness down, saturation varying, soft blobs of colour
 * and a little noise: a scene whose colours spread over the whole range,
 * as a good photograph does in Daniell's model */
static gfloat *
scene (void)
{
  gfloat *img = g_new (gfloat, (gsize) W * H * 4);
  GRand  *rnd = g_rand_new_with_seed (20260928);
  gint    x, y;

  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++)
      {
        gfloat *p = img + ((gsize) y * W + x) * 4;
        gdouble hh = fmod (x / (gdouble) W * 6.0 + 0.8 * sin (y * 0.05), 6.0);
        gdouble s  = 0.15 + 0.7 * (0.5 + 0.5 * sin (x * 0.031 + y * 0.047));
        gdouble v  = 0.04 + 0.94 * y / (H - 1.0);
        gint    i  = (gint) hh % 6;
        gdouble f  = hh - floor (hh);
        gdouble a  = v * (1 - s), b = v * (1 - s * f), c = v * (1 - s * (1 - f));
        gdouble rgb[6][3] = { { v, c, a }, { b, v, a }, { a, v, c },
                              { a, b, v }, { c, a, v }, { v, a, b } };
        gint    ch;

        for (ch = 0; ch < 3; ch++)
          {
            gdouble n = g_rand_double_range (rnd, -0.01, 0.01);

            p[ch] = CLAMP (rgb[i][ch] + n, 0.0, 1.0);
          }
        p[3] = 1.0f;
      }
  g_rand_free (rnd);
  return img;
}

/* dye loss: each channel C' = F C^alpha (gamma encoded, 0 to 1) */
typedef struct
{
  const gchar *name;
  gdouble      alpha[3];
  gdouble      F[3];
} Fading;

static gfloat *
fade (const gfloat *in,
      const Fading *f)
{
  gfloat *out = g_memdup2 (in, sizeof (gfloat) * W * H * 4);
  gsize   i;
  gint    ch;

  for (i = 0; i < (gsize) W * H; i++)
    for (ch = 0; ch < 3; ch++)
      out[i * 4 + ch] = f->F[ch] * pow (in[i * 4 + ch], f->alpha[ch]);
  return out;
}


static gint
compare_double (const void *a,
                const void *b)
{
  gdouble p = *(const gdouble *) a, q = *(const gdouble *) b;

  return p < q ? -1 : p > q;
}

/* mean and 95th percentile of Delta E 1976 between a and b (w x h) */
static void
delta_e (const gfloat *a,
         const gfloat *b,
         gsize         n,
         gdouble      *mean,
         gdouble      *p95)
{
  const Babl *fish = babl_fish (babl_format (FMT), babl_format ("CIE Lab alpha float"));
  gfloat     *la   = g_new (gfloat, n * 4), *lb = g_new (gfloat, n * 4);
  gdouble    *d    = g_new (gdouble, n), sum = 0.0;
  gsize       i;

  babl_process (fish, a, la, n);
  babl_process (fish, b, lb, n);
  for (i = 0; i < n; i++)
    {
      gdouble dl = la[i * 4] - lb[i * 4], da = la[i * 4 + 1] - lb[i * 4 + 1];
      gdouble db = la[i * 4 + 2] - lb[i * 4 + 2];

      d[i] = sqrt (dl * dl + da * da + db * db);
      sum += d[i];
    }
  qsort (d, n, sizeof (gdouble), compare_double);
  *mean = sum / n;
  *p95  = d[(gsize) (0.95 * (n - 1))];
  g_free (la);
  g_free (lb);
  g_free (d);
}

/* the mean L*, a* and b* */
static void
mean_lab (const gfloat *a,
          gsize         n,
          gdouble      *m)
{
  const Babl *fish = babl_fish (babl_format (FMT), babl_format ("CIE Lab alpha float"));
  gfloat     *la   = g_new (gfloat, n * 4);
  gsize       i;

  babl_process (fish, a, la, n);
  m[0] = m[1] = m[2] = 0.0;
  for (i = 0; i < n; i++)
    {
      m[0] += la[i * 4];
      m[1] += la[i * 4 + 1];
      m[2] += la[i * 4 + 2];
    }
  m[0] /= n;
  m[1] /= n;
  m[2] /= n;
  g_free (la);
}

/* with RESTORE_CHECK_OUT set, the images are saved there as 8 bit PNG
 * (tests/reference.sh runs the original PhotoRestore on them) */
static void
save_png (const gchar  *name,
          const gfloat *img)
{
  const gchar *dir = g_getenv ("RESTORE_CHECK_OUT");
  GeglBuffer  *buf;
  GeglNode    *graph, *src, *save;
  gchar       *path;

  if (! dir)
    return;
  buf = gegl_buffer_new (GEGL_RECTANGLE (0, 0, W, H), babl_format (FMT));
  gegl_buffer_set (buf, NULL, 0, babl_format (FMT), img, GEGL_AUTO_ROWSTRIDE);
  path  = g_strdup_printf ("%s/%s.png", dir, name);
  graph = gegl_node_new ();
  src   = gegl_node_new_child (graph, "operation", "gegl:buffer-source", "buffer", buf, NULL);
  save  = gegl_node_new_child (graph, "operation", "gegl:png-save", "path", path,
                               "bitdepth", 8, NULL);
  gegl_node_link (src, save);
  gegl_node_process (save);
  g_object_unref (graph);
  g_object_unref (buf);
  g_free (path);
}

/* checks -------------------------------------------------------------------- */

static void
check_registration (void)
{
  gboolean ok = gegl_has_operation (OP);

  report ("registration", ok, "%s %s", OP, ok ? "found" : "not found");
  if (ok)
    {
      const gchar *menu = gegl_operation_get_key (OP, "gimp:menu-path");

      report ("menu_path", menu && strcmp (menu, "<Image>/Colors/Auto") == 0, "%s",
              menu ? menu : "(none)");
    }
}

/* Daniell's Table 1 (restore1.pdf, an Agfacolor slide): alpha about
 * 0.46, 0.72, 0.59 and maxima 251, 224, 193 of 255; a blue cast (yellow
 * dye lost, as Kodachrome in the dark); and an even fading of all
 * three */
static const Fading fadings[] = {
  { "cyan_lost_pink",   { 0.46, 0.72, 0.59 }, { 251 / 255.0, 224 / 255.0, 193 / 255.0 } },
  { "yellow_lost_blue", { 0.85, 0.80, 0.50 }, { 0.80, 0.86, 0.98 } },
  { "all_faded",        { 0.60, 0.62, 0.58 }, { 0.92, 0.92, 0.90 } },
};

static void note (const gchar *name, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

static void
note (const gchar *name,
      const gchar *format,
      ...)
{
  va_list args;
  gchar  *detail;

  va_start (args, format);
  detail = g_strdup_vprintf (format, args);
  va_end (args);
  printf ("NOTE  %s: %s\n", name, detail);
  fflush (stdout);
  g_free (detail);
}

/* The cast of the faded scene must at least halve (the scene itself is
 * near neutral, mean a*, b* about 3, 1), for the two fadings with a
 * cast. In Daniell's model a faded image is the scene with a power and a
 * scale per channel, and so is its restoration, so restoring a faded
 * copy should give nearly the image the unfaded scene gives, whatever
 * the fading: the Delta E 1976 between the two is the measure of the
 * method (PLAN.md), printed here for three fadings and checked over a
 * grid of fadings below. How close either comes to the scene itself
 * depends on how "ideal" its colours are, and a generated scene is not
 * a photograph, so that is only printed. */
static void
check_fading (const gfloat *orig,
              const gfloat *same)
{
  gsize   n    = (gsize) W * H;
  gdouble m0, p0, m1, p1, m2, p2, lf[3], lr[3];
  guint   k;

  save_png ("restored-unfaded", same);
  for (k = 0; k < G_N_ELEMENTS (fadings); k++)
    {
      gfloat *faded    = fade (orig, &fadings[k]);
      gfloat *restored = run (NULL, faded, W, H);
      gchar  *name;
      gdouble cf, cr;

      name = g_strconcat ("faded-", fadings[k].name, NULL);
      save_png (name, faded);
      g_free (name);
      name = g_strconcat ("restored-", fadings[k].name, NULL);
      save_png (name, restored);
      g_free (name);

      mean_lab (faded, n, lf);
      mean_lab (restored, n, lr);
      cf = hypot (lf[1], lf[2]);
      cr = hypot (lr[1], lr[2]);
      name = g_strconcat ("cast_removed_", fadings[k].name, NULL);
      if (cf > 10.0)
        report (name, cr < 0.5 * cf,
                "mean a*, b* faded %.1f, %.1f, restored %.1f, %.1f", lf[1], lf[2], lr[1], lr[2]);
      else
        note (name, "no cast to remove: mean a*, b* faded %.1f, %.1f, restored %.1f, %.1f",
              lf[1], lf[2], lr[1], lr[2]);
      g_free (name);

      delta_e (faded, orig, n, &m0, &p0);
      delta_e (restored, orig, n, &m1, &p1);
      delta_e (restored, same, n, &m2, &p2);
      name = g_strconcat ("delta_e_", fadings[k].name, NULL);
      note (name, "to the scene: faded %.1f (95%% %.1f), restored %.1f (95%% %.1f); "
            "to the restored scene: %.1f (95%% %.1f)", m0, p0, m1, p1, m2, p2);
      g_free (name);
      g_free (faded);
      g_free (restored);
    }
  delta_e (same, orig, n, &m1, &p1);
  note ("delta_e_unfaded", "the restored scene to the scene: %.1f (95%% %.1f)", m1, p1);
}

/* The grid of fadings: alpha 0.45, 0.7 and 0.95 for each channel (27),
 * each with no loss of maximum and with maxima of 0.8, 0.9 and 1 in
 * turn (54 fadings). The mean Delta E of restore (fade (scene)) to
 * restore (scene) must stay below GRID_MEAN on average over the grid and
 * below GRID_WORST for every fading: pyrestore4.py's numbers on the same
 * 54 images (tests/reference.sh, measured 2026-11-18): mean 7.0, worst
 * 18.1, and it fails with an error on 4 of them, not counted. */
#define GRID_MEAN  7.0
#define GRID_WORST 18.0

static void
grid_fading (gint    k,
             Fading *f)
{
  static const gdouble a[3] = { 0.45, 0.7, 0.95 };
  gint                 ch, j = k / 2;

  f->name     = NULL;
  f->alpha[0] = a[j / 9];
  f->alpha[1] = a[(j / 3) % 3];
  f->alpha[2] = a[j % 3];
  for (ch = 0; ch < 3; ch++)
    f->F[ch] = (k % 2) ? 0.8 + 0.1 * ((ch + j) % 3) : 1.0;
}

static void
check_grid (const gfloat *orig,
            const gfloat *same)
{
  gsize   n = (gsize) W * H;
  gdouble sum = 0.0, worst = 0.0, worst95 = 0.0;
  gint    k, kw = 0, nk = 54;

  for (k = 0; k < nk; k++)
    {
      Fading  f;
      gfloat *faded, *restored;
      gdouble m, p;

      grid_fading (k, &f);
      faded    = fade (orig, &f);
      restored = run (NULL, faded, W, H);
      if (g_getenv ("RESTORE_CHECK_OUT"))
        {
          gchar *name = g_strdup_printf ("grid%02d-faded", k);

          save_png (name, faded);
          g_free (name);
          name = g_strdup_printf ("grid%02d-restored", k);
          save_png (name, restored);
          g_free (name);
        }
      delta_e (restored, same, n, &m, &p);
      sum += m;
      if (m > worst)
        {
          worst = m;
          kw    = k;
        }
      worst95 = MAX (worst95, p);
      g_free (faded);
      g_free (restored);
    }
  {
    Fading f;

    grid_fading (kw, &f);
    report ("grid_of_fadings", sum / nk < GRID_MEAN && worst < GRID_WORST,
            "%d fadings, Delta E to the restored scene: mean %.2f, worst %.2f "
            "(alpha %.2f %.2f %.2f, maxima %.1f %.1f %.1f), largest 95th percentile %.1f",
            nk, sum / nk, worst, f.alpha[0], f.alpha[1], f.alpha[2], f.F[0], f.F[1], f.F[2],
            worst95);
  }
}

static void
check_gray_alpha_broken (const gfloat *orig)
{
  gsize   n    = (gsize) W * H, i;
  gfloat *gray = g_new (gfloat, n * 4);
  gfloat *out, *in;
  gdouble d = 0.0, m[3];

  for (i = 0; i < n; i++)
    {
      gfloat v = 0.1f + 0.7f * ((i % W) / (gfloat) W);

      gray[i * 4] = gray[i * 4 + 1] = gray[i * 4 + 2] = v * v;
      gray[i * 4 + 3] = 1.0f;
    }
  /* stage 1 alone has nothing to change in a gray image's balance */
  out = run ("neutralize=false enhance-lightness=false saturation=0", gray, W, H);
  for (i = 0; i < n; i++)
    d = MAX (d, MAX (fabs (out[i * 4] - out[i * 4 + 1]), fabs (out[i * 4] - out[i * 4 + 2])));
  report ("gray_stays_gray_in_stage_1", d < 1e-6, "largest channel difference %.2g", d);
  g_free (out);
  /* the whole method makes it Daniell's slightly warm white, not more */
  out = run (NULL, gray, W, H);
  mean_lab (out, n, m);
  report ("gray_ends_near_neutral", hypot (m[1], m[2]) < 4.0,
          "mean a*, b* %.2f, %.2f", m[1], m[2]);
  g_free (out);
  g_free (gray);

  in_fmt = "R'G'B'A float";
  in = g_memdup2 (orig, sizeof (gfloat) * n * 4);
  for (i = 0; i < n; i++)
    in[i * 4 + 3] = (i % 7) / 6.0f;
  in[5 * 4 + 0] = NAN;
  in[9 * 4 + 1] = INFINITY;
  out = run (NULL, in, W, H);
  d = 0.0;
  for (i = 0; i < n; i++)
    d = MAX (d, fabs (out[i * 4 + 3] - in[i * 4 + 3]));
  report ("alpha_passed_through", d == 0.0, "largest alpha difference %.2g", d);
  report ("broken_pixels_passed_through",
          isnan (out[5 * 4]) && isinf (out[9 * 4 + 1]) && isfinite (out[5 * 4 + 1]),
          "NaN and infinity kept, the other channels of those pixels finite");
  g_free (out);
  g_free (in);
  in_fmt = "R'G'B'A u16";
}

/* each property's direction, the identity at amount 0, and clipping by
 * format */
static void
check_properties (const gfloat *orig)
{
  gsize   n     = (gsize) W * H, i;
  gfloat *faded = fade (orig, &fadings[0]);
  gfloat *def   = run (NULL, faded, W, H);
  gfloat *a, *b;
  gdouble ma[3], mb[3], d, ca, cb;
  gint    above;

  in_fmt = "R'G'B'A float";
  a = run ("amount=0", faded, W, H);
  d = max_diff (a, faded, n * 4);
  report ("amount_0_is_identity", d == 0.0, "largest difference %.2g", d);
  g_free (a);
  in_fmt = "R'G'B'A u16";

  a = run ("amount=0.5", faded, W, H);
  {
    gdouble m0, p0, m1, p1;

    delta_e (a, faded, n, &m0, &p0);
    delta_e (def, faded, n, &m1, &p1);
    report ("amount_half_is_between", m0 > 0.2 * m1 && m0 < 0.8 * m1,
            "Delta E to the faded image: amount 0.5 %.1f, 1 %.1f", m0, m1);
  }
  g_free (a);

  a = run ("warmth=-1", faded, W, H);
  b = run ("warmth=1", faded, W, H);
  mean_lab (a, n, ma);
  mean_lab (b, n, mb);
  report ("warmth_yellows", mb[2] > ma[2] + 2.0,
          "mean b* at warmth -1 and 1: %.1f, %.1f", ma[2], mb[2]);
  g_free (a);
  g_free (b);

  a = run ("neutralize=false", faded, W, H);
  mean_lab (a, n, ma);
  mean_lab (def, n, mb);
  report ("neutralize_balances", hypot (mb[1], mb[2]) < hypot (ma[1], ma[2]),
          "mean a*, b* without %.1f, %.1f, with %.1f, %.1f", ma[1], ma[2], mb[1], mb[2]);
  g_free (a);

  a = run ("saturation=0", faded, W, H);
  b = run ("saturation=1", faded, W, H);
  {
    const Babl *fish = babl_fish (babl_format (FMT), babl_format ("CIE LCH(ab) alpha float"));
    gfloat     *la   = g_new (gfloat, n * 4), *lb = g_new (gfloat, n * 4);

    babl_process (fish, a, la, n);
    babl_process (fish, b, lb, n);
    ca = cb = 0.0;
    for (i = 0; i < n; i++)
      {
        ca += la[i * 4 + 1];
        cb += lb[i * 4 + 1];
      }
    report ("saturation_saturates", cb > ca * 1.05, "mean C*ab at 0 and 1: %.1f, %.1f",
            ca / n, cb / n);
    g_free (la);
    g_free (lb);
  }
  g_free (a);
  g_free (b);

  {
    gdouble sa = 0.0, sb = 0.0, mla = 0.0, mlb = 0.0;
    const Babl *fish = babl_fish (babl_format (FMT), babl_format ("CIE Lab alpha float"));
    gfloat     *la   = g_new (gfloat, n * 4), *lb = g_new (gfloat, n * 4);

    a = run ("enhance-lightness=false", faded, W, H);
    babl_process (fish, a, la, n);
    babl_process (fish, def, lb, n);
    for (i = 0; i < n; i++)
      {
        mla += la[i * 4];
        mlb += lb[i * 4];
      }
    mla /= n;
    mlb /= n;
    for (i = 0; i < n; i++)
      {
        sa += (la[i * 4] - mla) * (la[i * 4] - mla);
        sb += (lb[i * 4] - mlb) * (lb[i * 4] - mlb);
      }
    report ("enhance_lightness_stretches", sb > sa,
            "standard deviation of L* without %.1f, with %.1f", sqrt (sa / n), sqrt (sb / n));
    g_free (la);
    g_free (lb);
    g_free (a);
  }

  /* values above 1: only where the input was above 1 already */
  for (i = 0, above = 0; i < n * 4; i++)
    above += (i % 4 != 3) && def[i] > 1.0f;
  report ("clipped_to_1", above == 0, "%d values above 1", above);
  in_fmt = "R'G'B'A float";
  {
    gfloat *hdr = g_memdup2 (faded, sizeof (gfloat) * n * 4);
    gint    kept = 0, other = 0;

    for (i = 0; i < n; i += 97)
      hdr[i * 4] = hdr[i * 4 + 1] = hdr[i * 4 + 2] = 1.5f;
    a = run (NULL, hdr, W, H);
    for (i = 0; i < n; i++)
      {
        gboolean was = hdr[i * 4] > 1.0f;

        if (was)
          kept += a[i * 4] > 1.0f || a[i * 4 + 1] > 1.0f || a[i * 4 + 2] > 1.0f;
        else
          other += a[i * 4] > 1.0f || a[i * 4 + 1] > 1.0f || a[i * 4 + 2] > 1.0f;
      }
    report ("above_1_kept_where_it_was", kept > 0 && other == 0,
            "pixels set to 1.5 still above 1: %d of %d; others above 1: %d",
            kept, (gint) ((n + 96) / 97), other);
    g_free (hdr);
    g_free (a);
  }
  in_fmt = "R'G'B'A u16";
  g_free (def);
  g_free (faded);
}

static void
check_few_colors (void)
{
  gsize   n  = (gsize) W * H, i;
  gfloat *in = g_new (gfloat, n * 4);
  gfloat *out;
  gdouble d;

  for (i = 0; i < n; i++)
    {
      in[i * 4]     = (i % 2) ? 0.7f : 0.3f;
      in[i * 4 + 1] = 0.4f;
      in[i * 4 + 2] = 0.2f;
      in[i * 4 + 3] = 1.0f;
    }
  in_fmt = "R'G'B'A float";
  out = run (NULL, in, W, H);
  in_fmt = "R'G'B'A u16";
  d   = max_diff (out, in, n * 4);
  report ("few_colors_passed_through", d == 0.0, "largest difference %.2g", d);
  g_free (out);
  g_free (in);
}

static void
check_light_dark (const gfloat *orig)
{
  gfloat *faded = fade (orig, &fadings[0]);
  gfloat *l = run ("light-dark=0.5", faded, W, H);
  gfloat *m = run ("light-dark=1.0", faded, W, H);
  gfloat *d = run ("light-dark=2.0", faded, W, H);
  gsize   n = (gsize) W * H;
  gdouble Ll[3], Lm[3], Ld[3];

  mean_lab (l, n, Ll);
  mean_lab (m, n, Lm);
  mean_lab (d, n, Ld);
  report ("light_dark_darkens", Ll[0] > Lm[0] && Lm[0] > Ld[0],
          "mean L* at 0.5, 1 and 2: %.1f, %.1f, %.1f", Ll[0], Lm[0], Ld[0]);
  g_free (faded);
  g_free (l);
  g_free (m);
  g_free (d);
}

static void
check_invariance (const gfloat *orig)
{
  gfloat       *faded = fade (orig, &fadings[0]);
  gfloat       *ref   = run (NULL, faded, W, H);
  gfloat       *t     = run_full (NULL, faded, W, H, 0, 0, 37, 23, NULL);
  gfloat       *o     = run_full (NULL, faded, W, H, 1000, -77, 0, 0, NULL);
  GeglRectangle roi   = { 50, 40, 100, 60 };
  gfloat       *part  = run_full (NULL, faded, W, H, 0, 0, 0, 0, &roi);
  gdouble       dp    = 0.0;
  gint          y;

  report ("same_for_tiles_37x23", max_diff (ref, t, (gsize) W * H * 4) == 0.0,
          "largest difference %.2g", max_diff (ref, t, (gsize) W * H * 4));
  report ("same_for_origin_1000_-77", max_diff (ref, o, (gsize) W * H * 4) == 0.0,
          "largest difference %.2g", max_diff (ref, o, (gsize) W * H * 4));
  for (y = 0; y < roi.height; y++)
    dp = MAX (dp, max_diff (part + (gsize) y * roi.width * 4,
                            ref + ((gsize) (y + roi.y) * W + roi.x) * 4,
                            (gsize) roi.width * 4));
  report ("same_for_a_region", dp == 0.0, "100 x 60 at 50, 40: largest difference %.2g", dp);
  g_free (faded);
  g_free (ref);
  g_free (t);
  g_free (o);
  g_free (part);
}

/* show=colour-map: the plot changes only the lower left corner, enough
 * of it, and it is the same for tiles and origin; outside it the result
 * is that of show=result (and at amount 0 the input) */
static void
check_colour_map (const gfloat *orig)
{
  gsize   n     = (gsize) W * H;
  gfloat *faded = fade (orig, &fadings[0]);
  gfloat *res   = run (NULL, faded, W, H);
  gfloat *map   = run ("show=colour-map", faded, W, H);
  gfloat *t     = run_full ("show=colour-map", faded, W, H, 0, 0, 37, 23, NULL);
  gfloat *o     = run_full ("show=colour-map", faded, W, H, 1000, -77, 0, 0, NULL);
  gint    x0 = W, x1 = -1, y0 = H, y1 = -1, changed = 0, x, y;
  gdouble d;

  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++)
      {
        gsize i = ((gsize) y * W + x) * 4;

        if (max_diff (map + i, res + i, 4) > 0.0)
          {
            changed++;
            x0 = MIN (x0, x); x1 = MAX (x1, x);
            y0 = MIN (y0, y); y1 = MAX (y1, y);
          }
      }
  report ("colour_map_in_the_lower_left", changed > 0 && x1 < W / 2 && y0 > H / 3 && y1 < H,
          "pixels changed %d in x %d to %d, y %d to %d", changed, x0, x1, y0, y1);
  report ("colour_map_large_enough", changed > (x1 - x0 + 1) * (y1 - y0 + 1) / 2
          && (x1 - x0 + 1) > W / 3, "%d by %d pixels", x1 - x0 + 1, y1 - y0 + 1);
  d = MAX (max_diff (map, t, n * 4), max_diff (map, o, n * 4));
  report ("colour_map_same_for_tiles_and_origin", d == 0.0, "largest difference %.2g", d);
  g_free (t);
  g_free (o);

  in_fmt = "R'G'B'A float";
  t = run ("show=colour-map amount=0", faded, W, H);
  for (y = 0, d = 0.0; y < H; y++)
    for (x = 0; x < W; x++)
      if (x < x0 || x > x1 || y < y0 || y > y1)
        d = MAX (d, max_diff (t + ((gsize) y * W + x) * 4, faded + ((gsize) y * W + x) * 4, 4));
  report ("colour_map_amount_0", d == 0.0 && max_diff (t, faded, n * 4) > 0.1,
          "outside the plot largest difference to the input %.2g", d);
  in_fmt = "R'G'B'A u16";
  g_free (t);
  g_free (map);
  g_free (res);
  g_free (faded);
}

int
main (int    argc,
      char **argv)
{
  gchar  *dir;
  gfloat *orig;

  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <build folder>\n", argv[0]);
      return 2;
    }
  setlocale (LC_ALL, "C");
  dir = modules_setup (argv[1]);
  if (! dir)
    return 2;
  gegl_init (&argc, &argv);
  g_log_set_default_handler (log_handler, NULL);

  orig = scene ();
  save_png ("scene", orig);
  check_registration ();
  if (gegl_has_operation (OP))
    {
      gfloat *same = run (NULL, orig, W, H);

      check_fading (orig, same);
      check_grid (orig, same);
      g_free (same);
      check_properties (orig);
      check_gray_alpha_broken (orig);
      check_few_colors ();
      check_light_dark (orig);
      check_invariance (orig);
      check_colour_map (orig);
    }
  g_free (orig);

  report ("no_warnings_or_criticals", n_messages == 0, "%d", n_messages);
  gegl_exit ();
  /* the sanitizer build keeps the module links, for the leak report's
   * stack traces at exit */
  if (! g_getenv ("RESTORE_CHECK_KEEP_MODULE"))
    modules_cleanup (dir);
  else
    g_free (dir);
  printf ("%d passed, %d failed\n", n_passed, n_failed);
  return n_failed ? 1 : 0;
}
