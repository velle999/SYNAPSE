/*
 * cairo_shapes.c — path helpers shared by everything that draws its own chrome
 * into a cairo overlay.
 *
 * A synui panel is TWO layers: a background scene rect underneath, and a cairo
 * buffer of text, rules and highlights on top. The scene side has a corner
 * radius of its own (wlr_scene_rect_set_corner_radius, driven by
 * panel_chrome_sync), and the cairo side needs a matching one or the two
 * disagree — which is not theoretical: rounding only the rect left both
 * right-click menus drawing a square 1px border across a curved background, so
 * the corner radius appeared to do nothing to the menus however high it went.
 *
 * ITS OWN FILE, and small on purpose. It began as a static in dock.c, and the
 * obvious home when the menus needed it was render.c beside create_cairo_buf().
 * But render.c is the compositor — linking it into a test drags in wlroots,
 * scenefx and a running scene graph — and the interesting half of this is the
 * arithmetic, the clamp especially, which is exactly the sort of thing that
 * should be pinned by a test that runs in a millisecond with no display. Pure
 * cairo, no synui types, so tests/rounded_rect_test.c links this file alone.
 * Same reason contrast.c is a file rather than a corner of render.c.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <math.h>

#include <cairo/cairo.h>

#include "cairo_shapes.h"

void cairo_rounded_rect(cairo_t *cr, double x, double y,
                        double w, double h, double r)
{
    /* Square is not a special case worth making the caller handle: passing
     * chrome_corner_radius() through means the retro chromes and a radius of 0
     * arrive here as 0, and they want the plain rectangle they always drew.
     * NaN lands here too — every comparison against it is false, so it fails
     * this test and the clamp below, and would reach cairo_arc() as a poisoned
     * radius that silently drops the whole path. */
    if (!(r > 0.0)) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }

    /* A radius past half the shorter side turns the arcs inside out — the arc
     * centres cross over and cairo joins them the long way round, which draws a
     * bow-tie. Clamped rather than rejected: a 14px radius on a 20px-tall row is
     * a capsule, which is the sane reading of it, and the bar's own pill asks
     * for exactly that. */
    const double lim = (w < h ? w : h) / 2.0;
    if (r > lim) r = lim;

    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r,     y + r,     r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
}
