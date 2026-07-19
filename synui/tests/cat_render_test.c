/*
 * cat_render_test.c — render the kitty to a PNG so a human can look at it.
 *
 * cat.c draws a cat from nothing but sines and beziers. The failure mode is not
 * a crash or a bad return code, it is "that does not read as a cat" — which no
 * assertion catches. So this renders each pose (and a walk cycle) to a contact
 * sheet and leaves the judging to whoever runs it.
 *
 * As a test it only asserts the weak, mechanical things: that we drew SOMETHING,
 * inside the canvas, and that the poses differ from one another. Run it as:
 *     ninja -C build && ./build/cat_render_test /tmp/cat.png
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>

#include "synui.h"

/* Count non-transparent pixels — the cheap "did we draw anything" check.
 *
 * Must run on a surface holding ONLY the cat: measuring the contact sheet
 * instead counts its opaque backdrop as ink, so every pose reads as 100% full
 * and the "solid blob" guard fires on a perfectly good cat. */
static int ink(cairo_surface_t *s)
{
    cairo_surface_flush(s);
    unsigned char *d = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    int w = cairo_image_surface_get_width(s);
    int h = cairo_image_surface_get_height(s);
    int n = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (d[y * stride + x * 4 + 3] > 24) n++;   /* alpha */
    return n;
}

int main(int argc, char **argv)
{
    const char *out   = argc > 1 ? argv[1] : "cat.png";
    const int   scale = argc > 2 ? atoi(argv[2]) : 1;   /* blow it up to inspect */

    /* Contact sheet: sit / sleep on the top row, a walk cycle below. */
    const int cols = 6, rows = 2;
    const int W = CAT_W * cols * scale, H = CAT_H * rows * scale;

    cairo_surface_t *sheet =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t *cr = cairo_create(sheet);

    /* Dark backdrop, so the neon rim reads the way it does on a desktop. */
    cairo_set_source_rgb(cr, 0.02, 0.03, 0.05);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);

    cat_pose_t poses[12];
    int n = 0;
    poses[n++] = (cat_pose_t){ .state = CAT_SIT,   .now = 0.0 };
    poses[n++] = (cat_pose_t){ .state = CAT_SIT,   .now = 0.0, .blinking = true };
    poses[n++] = (cat_pose_t){ .state = CAT_SLEEP, .now = 0.3 };
    poses[n++] = (cat_pose_t){ .state = CAT_SLEEP, .now = 0.9 };
    poses[n++] = (cat_pose_t){ .state = CAT_WALK,  .now = 0.0, .phase = 0.0 };
    poses[n++] = (cat_pose_t){ .state = CAT_WALK,  .now = 0.4, .phase = 0.8 };
    for (int i = 0; i < 6; i++)   /* one full walk cycle */
        poses[n++] = (cat_pose_t){ .state = CAT_WALK,
                                   .now = i * 0.25,
                                   .phase = i * (2 * 3.14159 / 6) };

    int counts[12];
    for (int i = 0; i < n; i++) {
        /* Draw each pose alone on a transparent canvas: that is both what the
         * compositor really does (the cat is an ARGB buffer over the desktop)
         * and the only surface on which "how much did we draw" is meaningful. */
        cairo_surface_t *one =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, CAT_W, CAT_H);
        cairo_t *ocr = cairo_create(one);
        cat_paint(ocr, &poses[i]);
        cairo_destroy(ocr);

        counts[i] = ink(one);
        printf("pose %2d: state=%d phase=%.2f  ink=%d px (%.0f%% of canvas)\n",
               i, poses[i].state, poses[i].phase, counts[i],
               100.0 * counts[i] / (CAT_W * CAT_H));
        fflush(stdout);

        cairo_set_source_surface(cr, one, (i % cols) * CAT_W, (i / cols) * CAT_H);
        cairo_paint(cr);
        cairo_surface_destroy(one);
    }

    cairo_destroy(cr);
    cairo_surface_write_to_png(sheet, out);
    printf("wrote %s (%dx%d)\n", out, W, H);

    /* Weak but real: every pose drew something, and nothing drew a solid block
     * (which is what a runaway fill or a NaN coordinate looks like). */
    for (int i = 0; i < n; i++) {
        assert(counts[i] > 120 && "pose drew (almost) nothing");
        assert(counts[i] < CAT_W * CAT_H * 0.75 && "pose is a solid blob");
    }
    /* Sitting, sleeping and walking must not render identically. */
    assert(counts[0] != counts[2] && "sit and sleep are the same drawing");

    cairo_surface_destroy(sheet);
    printf("OK\n");
    return 0;
}
