/*
 * wpthumb_test — decode every preview image the picker will be asked for.
 *
 * The picker's preview pane fails silently by design: a thumbnail that cannot
 * be decoded just draws "Preview unavailable" in a corner of a panel nobody is
 * staring at. That is the right behaviour at runtime and a terrible one to rely
 * on for correctness, so this walks a directory of real images and asserts each
 * one decodes to a plausible surface.
 *
 * Point it at the Steam Workshop content tree and it covers the actual mix on
 * this machine (77 JPEG, 62 GIF at the time of writing). With no argument it
 * runs a built-in set of synthesised images instead, so the test still means
 * something on a machine with no Wallpaper Engine install — which is every CI
 * runner and the ISO build.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cairo/cairo.h>

#include "synui.h"

static int failures = 0;
static int checked  = 0;

/* A decoded preview has to be something the pane can actually draw: a live
 * surface with non-zero dimensions. A 0x0 surface would sail through a plain
 * NULL check and then draw nothing. */
static void check(const char *path, bool expect_ok)
{
    checked++;
    cairo_surface_t *s = wpthumb_get(path);

    if (!expect_ok) {
        if (s) { printf("  FAIL %s: expected no decode, got one\n", path); failures++; }
        return;
    }
    if (!s) { printf("  FAIL %s: did not decode\n", path); failures++; return; }

    int w = cairo_image_surface_get_width(s);
    int h = cairo_image_surface_get_height(s);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS || w < 1 || h < 1) {
        printf("  FAIL %s: %dx%d status=%d\n", path, w, h,
               cairo_surface_status(s));
        failures++;
    }
}

/* Write a tiny PNG and a deliberately corrupt file, so the no-Workshop path
 * still exercises a real decode and a real failure. */
static void synth_suite(void)
{
    char dir[] = "/tmp/wpthumb_test.XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); exit(2); }

    char png[256], junk[256], missing[256];
    snprintf(png,     sizeof(png),     "%s/a.png", dir);
    snprintf(junk,    sizeof(junk),    "%s/b.png", dir);
    snprintf(missing, sizeof(missing), "%s/nope.png", dir);

    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 32);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgb(cr, 0.2, 0.7, 0.9);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_write_to_png(s, png);
    cairo_surface_destroy(s);

    FILE *f = fopen(junk, "wb");
    if (f) { fputs("this is not an image", f); fclose(f); }

    printf("synthesised suite in %s\n", dir);
    check(png,     true);
    check(junk,    false);   /* a corrupt file must fail, not crash */
    check(missing, false);   /* and so must one that is not there */

    /* Same path twice: the second must come from the cache and still be
     * usable. A cache that hands back a destroyed surface would pass a NULL
     * check and fail here on status. */
    check(png, true);

    unlink(png); unlink(junk); rmdir(dir);
}

/* Every <id>/<preview> under a Workshop content tree. */
static void workshop_suite(const char *root)
{
    DIR *d = opendir(root);
    if (!d) { printf("cannot open %s — skipping\n", root); return; }

    int items = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        /* Take whatever preview.* is there. The compositor reads the filename
         * out of project.json; this only needs the pixels. */
        static const char *exts[] = { "jpg", "gif", "png", "jpeg", NULL };
        for (int i = 0; exts[i]; i++) {
            char p[512];
            struct stat st;
            if (snprintf(p, sizeof(p), "%s/%s/preview.%s", root, e->d_name,
                         exts[i]) >= (int)sizeof(p))
                continue;
            if (stat(p, &st) != 0) continue;
            check(p, true);
            items++;
            break;
        }
    }
    closedir(d);
    printf("workshop previews checked: %d\n", items);

    /* A tree that exists but yielded nothing means the layout assumption is
     * wrong, and a test that silently checks zero files is worse than none. */
    if (items == 0) { printf("  FAIL: %s had no previews\n", root); failures++; }
}

int main(int argc, char **argv)
{
    if (argc > 1) workshop_suite(argv[1]);
    else          synth_suite();

    wpthumb_clear();

    printf("%s: %d checked, %d failed\n",
           failures ? "FAIL" : "PASS", checked, failures);
    return failures ? 1 : 0;
}
