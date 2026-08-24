/*
 * contrast.c — how legible a colour is on a surface, and what to do when it
 * is not.
 *
 * synui's panels used to be one near-black navy whatever the theme was, so
 * every accent and every status colour in render.c could be picked once,
 * against that. Panels take the theme's own surface now, which means the two
 * light themes opened PALE panels painted with colours chosen for a dark one.
 * Measured on XP's #ECE9D8: the task manager's meter readings came out at
 * 1.04:1 — beige numbers on beige — and the panel accent at 2.21:1.
 *
 * A per-theme table of replacements was the obvious fix and the wrong one: a
 * rice defines its own accent, and a table cannot carry a colour that does not
 * exist yet. So contrast is measured and the colour is walked until it passes.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <math.h>
#include <stdbool.h>

#include "contrast.h"

static double srgb_to_linear(double c)
{
    return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

/* The same curve over the 256 values an 8-bit channel can hold, which is what
 * every caller measuring PIXELS has. Here rather than beside either of them
 * because both wallpaper.c (a cairo surface) and matrix.c (a GL readback)
 * measure the same strip for the same decision, and a second table would be a
 * second place for the transfer function to be wrong. */
double syn_srgb_lut(int v)
{
    static double lut[256];
    static bool   built = false;
    if (!built) {
        for (int i = 0; i < 256; i++) lut[i] = srgb_to_linear(i / 255.0);
        built = true;
    }
    return lut[v < 0 ? 0 : v > 255 ? 255 : v];
}

double syn_rel_luminance(double r, double g, double b)
{
    return 0.2126 * srgb_to_linear(r) +
           0.7152 * srgb_to_linear(g) +
           0.0722 * srgb_to_linear(b);
}

double syn_contrast(double r, double g, double b, double surface_lum)
{
    double l  = syn_rel_luminance(r, g, b);
    double hi = l > surface_lum ? l : surface_lum;
    double lo = l > surface_lum ? surface_lum : l;
    return (hi + 0.05) / (lo + 0.05);
}

/* Bisection on a monotonic function; 12 rounds is well past the precision of an
 * 8-bit channel, and this runs on a theme change, not per frame.
 *
 * ON A DARK SURFACE THIS IS A NO-OP, deliberately, even where the measured
 * contrast is under target. Gruvbox's red comes out at 3.89:1 on its own #282828
 * and Catppuccin's at 4.33:1 — those are the palettes' values, chosen by their
 * authors, and "correcting" them would repaint four working dark themes to fix a
 * complaint about two light ones. The first draft did exactly that, and
 * tests/panel_contrast_test.c is what caught it. That guard is the reason this
 * file has a test at all.
 */
void syn_contrast_fix(const float in[3], float out[3], double surface_lum)
{
    for (int i = 0; i < 3; i++) out[i] = in[i];
    if (surface_lum <= SURFACE_PALE) return;
    if (syn_contrast(in[0], in[1], in[2], surface_lum) >= CONTRAST_TARGET)
        return;

    /* t = 0 keeps the colour, t = 1 is pure black. Scaling all three channels by
     * the same factor holds hue and saturation and moves only value, which is
     * why XP's brightened Luna blue lands within a hair of the real #316AC5
     * Explorer selection rather than drifting to some other blue. */
    double lo = 0.0, hi = 1.0;
    for (int it = 0; it < 12; it++) {
        double t = (lo + hi) / 2.0;
        if (syn_contrast(in[0] * (1.0 - t), in[1] * (1.0 - t),
                         in[2] * (1.0 - t), surface_lum) >= CONTRAST_TARGET)
            hi = t;
        else
            lo = t;
    }
    out[0] = (float)(in[0] * (1.0 - hi));
    out[1] = (float)(in[1] * (1.0 - hi));
    out[2] = (float)(in[2] * (1.0 - hi));
}

/* Both arguments are already luminances, which syn_contrast's are not: the ink
 * here is one of two fixed colours and the backdrop is a MEAN over thousands of
 * wallpaper pixels, so neither side ever exists as an r,g,b triple. */
double syn_contrast_lum(double a, double b)
{
    double hi = a > b ? a : b;
    double lo = a > b ? b : a;
    return (hi + 0.05) / (lo + 0.05);
}

syn_ink_t syn_ink_for_backdrop(double lum, double target)
{
    if (lum < 0.0) return SYN_INK_NONE;   /* not measured */

    double c_dark  = syn_contrast_lum(SYN_INK_DARK_LUM,  lum);
    double c_light = syn_contrast_lum(SYN_INK_LIGHT_LUM, lum);

    /* At AA there is a band — roughly 0.18 to 0.23 — where a backdrop is too
     * pale for white and too dark for black. Real wallpapers land in it (an
     * evenly-lit photograph does), and that is the case a clear bar must refuse
     * rather than round off: both answers are unreadable, so neither is one. */
    if (c_dark < target && c_light < target) return SYN_INK_NONE;

    return c_dark >= c_light ? SYN_INK_DARK : SYN_INK_LIGHT;
}

/* Same comparison with the target dropped: the answer is always one of the two,
 * because "closer" is always defined even when neither is close enough. Ties go
 * to dark for the same reason the tie above does — a stable side, so the value
 * does not flicker between two frames of a video wallpaper. */
syn_ink_t syn_ink_best(double lum)
{
    if (lum < 0.0) return SYN_INK_NONE;   /* not measured */
    return syn_contrast_lum(SYN_INK_DARK_LUM, lum) >= syn_contrast_lum(SYN_INK_LIGHT_LUM, lum)
               ? SYN_INK_DARK : SYN_INK_LIGHT;
}

syn_ink_t syn_ink_combine(syn_ink_t a, syn_ink_t b)
{
    if (a == SYN_INK_NONE || b == SYN_INK_NONE) return SYN_INK_NONE;
    return a == b ? a : SYN_INK_NONE;
}

const char *syn_ink_name(syn_ink_t ink)
{
    switch (ink) {
    case SYN_INK_DARK:  return "dark";
    case SYN_INK_LIGHT: return "light";
    default:            return "none";
    }
}

/* ── The wallpaper under a surface ───────────────────────── */

/* srgb_to_linear's inverse: a linear-light value back to the encoding the
 * framebuffer and every colour literal in this tree are written in. */
double syn_lum_to_srgb(double c)
{
    if (c <= 0.0) return 0.0;
    if (c >= 1.0) return 1.0;
    return c <= 0.0031308 ? c * 12.92
                          : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

double syn_lum_over(double surface_lum, double alpha, double backdrop_lum)
{
    if (backdrop_lum < 0.0) return surface_lum;   /* not measured */
    if (alpha >= 1.0) return surface_lum;
    if (alpha <= 0.0) return backdrop_lum;

    /* Out and back through the transfer function, so the mix happens on the
     * values the GPU mixes — see the header. Both ends are treated as greys of
     * the given luminance, which is exact for the mix itself: the blend is
     * per-channel and linear in each, so the luminance of the result is the
     * same weighted sum of encoded channels whatever hues the two sides are. */
    double s = syn_lum_to_srgb(surface_lum);
    double b = syn_lum_to_srgb(backdrop_lum);
    return srgb_to_linear(alpha * s + (1.0 - alpha) * b);
}

void syn_backdrop_for_box(const double *grid, double fx, double fy,
                          double fw, double fh, double target,
                          syn_backdrop_t *out)
{
    out->lum     = -1.0;
    out->lum_min = -1.0;
    out->lum_max = -1.0;
    out->ink     = SYN_INK_NONE;
    out->best    = SYN_INK_NONE;
    if (!grid) return;

    /* Clamp into the output. A panel can legitimately hang off the edge (the
     * dock menu at the screen's corner), and the cells it does cover are the
     * honest answer for the part of it that is on screen. */
    if (fw < 0.0) { fx += fw; fw = -fw; }
    if (fh < 0.0) { fy += fh; fh = -fh; }
    double x0 = fx < 0.0 ? 0.0 : fx > 1.0 ? 1.0 : fx;
    double y0 = fy < 0.0 ? 0.0 : fy > 1.0 ? 1.0 : fy;
    double x1 = fx + fw, y1 = fy + fh;
    x1 = x1 < 0.0 ? 0.0 : x1 > 1.0 ? 1.0 : x1;
    y1 = y1 < 0.0 ? 0.0 : y1 > 1.0 ? 1.0 : y1;

    int c0 = (int)(x0 * SYN_LUM_COLS), c1 = (int)(x1 * SYN_LUM_COLS);
    int r0 = (int)(y0 * SYN_LUM_ROWS), r1 = (int)(y1 * SYN_LUM_ROWS);
    if (c0 >= SYN_LUM_COLS) c0 = SYN_LUM_COLS - 1;
    if (r0 >= SYN_LUM_ROWS) r0 = SYN_LUM_ROWS - 1;
    if (c1 >= SYN_LUM_COLS) c1 = SYN_LUM_COLS - 1;
    if (r1 >= SYN_LUM_ROWS) r1 = SYN_LUM_ROWS - 1;
    /* A zero-area box still samples the one cell it lands in — see the header:
     * that is a panel awaiting layout, not a panel with nothing behind it. */
    if (c1 < c0) c1 = c0;
    if (r1 < r0) r1 = r0;

    double sum   = 0.0;
    double lo    = 0.0, hi = 0.0;
    int    n     = 0;
    bool   seen  = false;
    syn_ink_t ink = SYN_INK_NONE, best = SYN_INK_NONE;

    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            double l = grid[r * SYN_LUM_COLS + c];
            if (l < 0.0) {
                /* One unmeasured cell vetoes the box, exactly as one
                 * unmeasured monitor vetoes the clear bar. */
                out->lum = -1.0; out->lum_min = -1.0; out->lum_max = -1.0;
                out->ink = SYN_INK_NONE; out->best = SYN_INK_NONE;
                return;
            }
            sum += l;
            if (!n || l < lo) lo = l;
            if (!n || l > hi) hi = l;
            n++;
            syn_ink_t this_one  = syn_ink_for_backdrop(l, target);
            syn_ink_t this_best = syn_ink_best(l);
            ink  = seen ? syn_ink_combine(ink,  this_one)  : this_one;
            best = seen ? syn_ink_combine(best, this_best) : this_best;
            seen = true;
        }
    }
    if (!n) return;

    out->lum     = sum / n;
    /* The extremes the alpha walk has to survive — see syn_backdrop_t. On a
     * single cell all three are the same number, which is why nothing with a
     * flat wallpaper or a small surface moves at all. */
    out->lum_min = lo;
    out->lum_max = hi;
    out->ink     = ink;
    out->best    = best;
}

/* Windows 95 is the theme with the least room to work in: black on its #C0C0C0
 * silver tops out at 11.54:1 where SYNAPSE's near-white on near-black reaches
 * 17.07:1, and the ladder's sRGB-linear mapping spends that smaller budget
 * unevenly — level 0.44 is 4.06:1 on SYNAPSE and 2.89:1 on 95. That is what
 * "secondary text is grey on a grey background" measures as. */
double syn_ink_floor(const float bg[3], const float ink[3], double target)
{
    double lum = syn_rel_luminance(bg[0], bg[1], bg[2]);
    if (lum <= SURFACE_PALE) return 0.0;

    /* Monotonic in the position, so bisect it. If even the theme's own ink
     * cannot reach the target this converges on 1.0 — the best the palette has,
     * which is the honest answer rather than a colour outside the theme. */
    double lo = 0.0, hi = 1.0;
    for (int it = 0; it < 12; it++) {
        double t = (lo + hi) / 2.0;
        double r = bg[0] + (ink[0] - bg[0]) * t;
        double g = bg[1] + (ink[1] - bg[1]) * t;
        double b = bg[2] + (ink[2] - bg[2]) * t;
        if (syn_contrast(r, g, b, lum) >= target) hi = t;
        else                                      lo = t;
    }
    return hi;
}
