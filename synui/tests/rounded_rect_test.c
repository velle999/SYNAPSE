/*
 * rounded_rect_test.c — cairo_rounded_rect(), the cairo half of the corner
 * radius.
 *
 * The panels that draw their own frame do it in an overlay buffer that sits on
 * top of a background rect the scene has already rounded, so this path has to
 * agree with wlr_scene_rect_set_corner_radius() or the two layers disagree and
 * the panel keeps a square outline over a curved background. That is the bug
 * this function was extracted for, and it is not what is checked here — a
 * WIRING bug needs a running compositor and a pointer to open a menu with, and
 * nothing can synthesise a pointer (see bar_radius.sh).
 *
 * What IS checked is the arithmetic, which is where the sharp edges are and
 * which needs no display at all:
 *
 *   1. It actually rounds. The filled shape is SMALLER than the rectangle by
 *      roughly what four quarter-circles cut off the corners — measured, not
 *      eyeballed, and compared against the exact area r²(4 - π).
 *   2. r <= 0 and NaN degenerate to exactly cairo_rectangle(). Callers pass
 *      chrome_corner_radius() straight through, and that is 0 for a radius of
 *      zero AND for every retro chrome, so "square" is the common path and not
 *      an edge case.
 *   3. The clamp. Past half the shorter side the arc centres cross over and
 *      cairo joins them the long way round — a bow-tie, an inside-out shape
 *      that is very visibly wrong and would only ever be found by looking. A
 *      huge radius must give the capsule, identical to asking for exactly half.
 *
 * Rasterised and counted rather than asserted on the path, because the path is
 * an implementation detail and the pixels are the claim.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cairo/cairo.h>

#include "cairo_shapes.h"

#define W 120
#define H 80

/* Fill the shape on a fresh surface and return the covered pixel count, and
 * optionally the pixels themselves for an exact comparison. */
static long fill_area(double r, unsigned char *out)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_A8, W, H);
    cairo_t *cr = cairo_create(s);

    /* No antialiasing: a half-covered edge pixel would make every count below
     * a range rather than a number, and the areas being compared here differ by
     * far more than an edge. */
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_rounded_rect(cr, 0, 0, W, H, r);
    cairo_fill(cr);
    cairo_surface_flush(s);

    const unsigned char *d = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);

    long n = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (d[y * stride + x] > 127) n++;

    if (out)
        for (int y = 0; y < H; y++)
            memcpy(out + (size_t)y * W, d + (size_t)y * stride, W);

    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return n;
}

int main(void)
{
    /* ── 1. it rounds, by the right amount ──────────────────── */
    const long square = fill_area(0, NULL);
    assert(square == (long)W * H);          /* r = 0 is the whole box */

    const double r = 20.0;
    const long rounded = fill_area(r, NULL);

    /* Four quarter-circles removed from four corners: r² per corner minus the
     * quarter disc, so r²(4 - π) all told. Tolerance is generous because the
     * rasteriser has to decide a boundary pixel one way or the other, and the
     * claim is "it curves by about this much", not a pixel-exact area. */
    const double cut_expected = r * r * (4.0 - M_PI);
    const double cut_actual   = (double)(square - rounded);
    printf("  radius %g: %ld px of %ld, corners cut %.0f (expected ~%.0f)\n",
           r, rounded, square, cut_actual, cut_expected);
    assert(cut_actual > 0);
    assert(fabs(cut_actual - cut_expected) < 0.15 * cut_expected);

    /* ── 2. square degenerates EXACTLY ──────────────────────── */
    static unsigned char a[W * H], b[W * H];

    fill_area(0, a);
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_A8, W, H);
    cairo_t *cr = cairo_create(s);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);
    cairo_surface_flush(s);
    const unsigned char *d = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < H; y++) memcpy(b + (size_t)y * W, d + (size_t)y * stride, W);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    assert(memcmp(a, b, sizeof a) == 0);
    printf("  r = 0 is pixel-identical to cairo_rectangle()\n");

    /* Negative and NaN take the same door. NaN is the one that would not fail
     * loudly: every comparison against it is false, so a `r < 0` guard lets it
     * through to cairo_arc(), which quietly drops the path and draws NOTHING. */
    assert(fill_area(-5.0, NULL) == square);
    assert(fill_area(NAN, NULL) == square);
    printf("  negative and NaN degenerate to the rectangle too\n");

    /* ── 3. the clamp: a huge radius is the capsule ─────────── */
    const double half = (W < H ? W : H) / 2.0;      /* 40 — H is the shorter */
    const long capsule = fill_area(half, NULL);
    const long clamped = fill_area(10000.0, NULL);
    printf("  clamp: r = half (%g) %ld px, r = 10000 %ld px\n",
           half, capsule, clamped);
    assert(clamped == capsule);

    /* And it is a real capsule, not a collapsed or inside-out shape: two
     * semicircular ends on a rectangle is W*H - H²(4 - π)/4 of area, and a
     * bow-tie would come in far under it. */
    const double cap_expected = (double)W * H - (double)H * H * (4.0 - M_PI) / 4.0;
    assert(fabs((double)capsule - cap_expected) < 0.05 * cap_expected);
    assert(capsule > square / 2);
    printf("  capsule area %ld (expected ~%.0f)\n", capsule, cap_expected);

    printf("PASS\n");
    return 0;
}
