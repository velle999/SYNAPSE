/*
 * clipbox_test.c — a rect's clipped_region as a HIT test, not just a paint one.
 *
 * The failure this guards is silent and was live for four releases: barscan.c
 * asks the scene graph what is under the bar, a window's border is ONE rect the
 * size of the whole frame with the content clipped out (deco.c), and a walk
 * that stops at the node's box therefore answered "the border" for every point
 * inside every window. Nothing crashed. The bar simply took its ink from its
 * own chrome colour instead of the window, chose dark text over a dark window,
 * and read as a theming bug rather than a geometry one — measured 2026-08-18,
 * a whole session of strip values with not one window buffer among them.
 *
 * The numbers below are the real frame geometry: border_width 2,
 * titlebar_height 26, a 1920x1080 maximized window, and the bar strip's probe
 * row at the middle of a 34px band. That row is the case the compositor got
 * wrong, and it is the first assertion here.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* deco.c's border: a w x h rect with the content box (inset by the border
 * width) clipped out, leaving a ring bw thick. */
static struct clipped_region border_ring(int w, int h, int bw, int radius)
{
    struct clipped_region cr = {
        .area    = { .x = bw, .y = bw, .width = w - 2 * bw, .height = h - 2 * bw },
        .corners = corner_radii_all(radius),
    };
    return cr;
}

/*
 * THE BUG. A 1920x1080 window at the top of the screen, and the strip's probe
 * row 17px down: inside the frame, inside the border rect's box, and painted by
 * nothing but the titlebar. The border must not answer for it.
 */
static void test_the_strip_row(void)
{
    struct clipped_region cr = border_ring(1920, 1080, 2, 0);

    CHECK(syn_clip_hides(&cr, 960, 17),
          "the bar's probe row reads as border paint (it is titlebar)");
    CHECK(syn_clip_hides(&cr, 0, 17) == false,
          "the left border column is paint and must answer");
    CHECK(syn_clip_hides(&cr, 1919, 17) == false,
          "the right border column is paint and must answer");
    CHECK(syn_clip_hides(&cr, 960, 1) == false,
          "the top border row is paint and must answer");
    CHECK(syn_clip_hides(&cr, 960, 1078) == false,
          "the bottom border row is paint and must answer");
    CHECK(syn_clip_hides(&cr, 960, 540),
          "the middle of the window is not the border");
}

/* Half-open, like every other box in this tree: the first clipped pixel is
 * hidden, the last one before the ring is not. Off by one here and the scan
 * either loses the innermost border column or claims one it does not paint. */
static void test_edges(void)
{
    struct clipped_region cr = border_ring(100, 100, 2, 0);

    CHECK(syn_clip_hides(&cr, 1, 50) == false, "x=bw-1 is border paint");
    CHECK(syn_clip_hides(&cr, 2, 50),          "x=bw is clipped out");
    CHECK(syn_clip_hides(&cr, 97, 50),         "x=w-bw-1 is clipped out");
    CHECK(syn_clip_hides(&cr, 98, 50) == false, "x=w-bw is border paint");
}

/*
 * The ring is thicker at a rounded corner, and that extra IS painted. A point
 * in the corner's square but outside its quarter-circle must read as paint, or
 * the scan falls back to the wallpaper exactly where the border is widest.
 */
static void test_rounded_corner(void)
{
    struct clipped_region cr = border_ring(200, 200, 2, 20);

    CHECK(syn_clip_hides(&cr, 3, 3) == false,
          "the outside of a rounded corner is paint, not cutout");
    CHECK(syn_clip_hides(&cr, 22, 22),
          "the centre of the corner's arc is inside the cutout");
    CHECK(syn_clip_hides(&cr, 196, 3) == false,  "top-right corner");
    CHECK(syn_clip_hides(&cr, 3, 196) == false,  "bottom-left corner");
    CHECK(syn_clip_hides(&cr, 196, 196) == false, "bottom-right corner");
    CHECK(syn_clip_hides(&cr, 100, 3),
          "a straight edge between two corners is still cut out");
}

/*
 * No clip at all is the common case — every rect in the tree that is not a
 * border — and it must never hide anything, or the scan starts declining solid
 * colour it can read perfectly well.
 */
static void test_no_clip(void)
{
    struct clipped_region none = clipped_region_get_default();
    CHECK(syn_clip_hides(&none, 0, 0) == false,   "a default clip hid a point");
    CHECK(syn_clip_hides(&none, 500, 500) == false, "a default clip hid a point");

    struct clipped_region empty = { .area = { 10, 10, 0, 0 } };
    CHECK(syn_clip_hides(&empty, 10, 10) == false,
          "a zero-sized cutout hid a point");
}

int main(void)
{
    test_the_strip_row();
    test_edges();
    test_rounded_corner();
    test_no_clip();

    if (failures) {
        fprintf(stderr, "clipbox_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("clipbox_test: ok\n");
    return 0;
}
