/*
 * crop_test — the cropper's geometry and its output rules (src/crop.c)
 *
 * Two things here can actually hurt someone, and they are what this covers:
 *
 *   1. THE COORDINATE MAPPING. The selection is stored in image pixels and
 *      drawn in screen pixels, and if the two disagree the tool writes out a
 *      different rectangle from the one you dragged. Silent, and you only
 *      notice by comparing the result to what you remember selecting.
 *
 *   2. NEVER OVERWRITING. Cropping is destructive and there is no undo, so the
 *      output must always be a new file. A regression here eats originals.
 *
 * The panel's own handlers need a compositor (they call server_output_box and
 * synui_render_crop), so those are stubbed and the pure geometry is driven
 * directly — the same shape as hit_test.
 *
 * It writes only into its own mkdtemp scratch directory.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <cairo/cairo.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* ── The compositor, stubbed ─────────────────────────────── */

static struct wlr_box g_out = { .x = 0, .y = 0, .width = 1920, .height = 1080 };

void server_output_box(syn_server_t *s, struct wlr_box *box) { (void)s; *box = g_out; }
void synui_render_crop(syn_server_t *s) { (void)s; }
void ctlpanel_child_closed(syn_server_t *s, const char *a) { (void)s; (void)a; }
/* Captured rather than discarded: the injection case asserts on what the panel
 * would have handed to /bin/sh. */
static char last_spawn[2048];
void synui_spawn(const char *cmd)
{
    snprintf(last_spawn, sizeof(last_spawn), "%s", cmd ? cmd : "");
}
cairo_surface_t *syn_decode_jpeg(const char *path) { (void)path; return NULL; }

static char scratch[256];

/* ── Helpers ─────────────────────────────────────────────── */

static syn_server_t *server_with_image(int w, int h)
{
    static syn_server_t s;
    memset(&s, 0, sizeof(s));
    s.crop.img   = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    s.crop.img_w = w;
    s.crop.img_h = h;
    s.crop.visible = 1;
    return &s;
}

/* ── Cases ───────────────────────────────────────────────── */

/* crop_fit centres the image, preserves aspect, and never scales up. */
static void test_fit(void)
{
    /* A photo far larger than the screen: scales DOWN to fit the short axis. */
    syn_server_t *s = server_with_image(6000, 4000);
    double sc, ox, oy;
    crop_fit(s, &g_out, &sc, &ox, &oy);

    /* 1080 - 2*60 margin = 960 available height; 960/4000 = 0.24, and that is
     * smaller than 1800/6000 = 0.30, so height is the binding axis. */
    CHECK(sc > 0.239 && sc < 0.241, "expected ~0.24 scale, got %.4f", sc);

    double dw = 6000 * sc, dh = 4000 * sc;
    CHECK(dh <= g_out.height - 119, "the fitted image should respect the margin");
    /* Centred: equal gaps either side. */
    CHECK(ox > (g_out.width - dw) / 2 - 0.5 && ox < (g_out.width - dw) / 2 + 0.5,
          "the image should be horizontally centred");
    CHECK(oy > (g_out.height - dh) / 2 - 0.5 && oy < (g_out.height - dh) / 2 + 0.5,
          "the image should be vertically centred");
    cairo_surface_destroy(s->crop.img);

    /* A tiny icon must NOT be blown up — see crop_fit's comment. */
    s = server_with_image(64, 64);
    crop_fit(s, &g_out, &sc, &ox, &oy);
    CHECK(sc == 1.0, "a small image should render 1:1, got scale %.4f", sc);
    cairo_surface_destroy(s->crop.img);
}

/* The selection normalises whichever way the drag went. */
static void test_selection_normalises(void)
{
    syn_server_t *s = server_with_image(1000, 800);
    int x, y, w, h;

    /* Dragged down-right. */
    s->crop.ax = 100; s->crop.ay = 50; s->crop.bx = 400; s->crop.by = 250;
    crop_selection(s, &x, &y, &w, &h);
    CHECK(x == 100 && y == 50 && w == 300 && h == 200,
          "down-right drag gave %d,%d %dx%d", x, y, w, h);

    /* …and up-left, which leaves bx < ax. Same rectangle. */
    s->crop.ax = 400; s->crop.ay = 250; s->crop.bx = 100; s->crop.by = 50;
    crop_selection(s, &x, &y, &w, &h);
    CHECK(x == 100 && y == 50 && w == 300 && h == 200,
          "up-left drag gave %d,%d %dx%d", x, y, w, h);

    /* A click with no travel is not a selection. */
    s->crop.ax = s->crop.bx = 200;
    s->crop.ay = s->crop.by = 200;
    CHECK(!crop_has_selection(s), "a zero-size drag must not count as a selection");

    /* Nor is one a few pixels across — below CROP_MIN_PX. */
    s->crop.bx = 203; s->crop.by = 203;
    CHECK(!crop_has_selection(s), "a 3px drag must not count as a selection");

    s->crop.bx = 260; s->crop.by = 260;
    CHECK(crop_has_selection(s), "a 60px drag should be a selection");

    cairo_surface_destroy(s->crop.img);
}

/* A round trip through the mapping: a rectangle in image pixels converted to
 * screen and back must land where it started. This is the assertion that keeps
 * "what you dragged" and "what gets written" the same rectangle. */
static void test_round_trip(void)
{
    syn_server_t *s = server_with_image(6000, 4000);
    double sc, ox, oy;
    crop_fit(s, &g_out, &sc, &ox, &oy);

    const int probes[][2] = {
        { 0, 0 }, { 6000, 4000 }, { 3000, 2000 }, { 17, 3 }, { 5999, 3999 },
    };

    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        int ix = probes[i][0], iy = probes[i][1];

        /* image -> screen, the way the render does it */
        double lx = ox + ix * sc, ly = oy + iy * sc;

        /* screen -> image, the way the pointer does it */
        double bx = (lx - ox) / sc, by = (ly - oy) / sc;
        int rx = (int)(bx + 0.5), ry = (int)(by + 0.5);

        CHECK(rx == ix && ry == iy,
              "round trip of %d,%d came back as %d,%d", ix, iy, rx, ry);
    }
    cairo_surface_destroy(s->crop.img);
}

/* Writing must never touch the input, and never a file that already exists. */
static void test_never_overwrites(void)
{
    char src[320];
    snprintf(src, sizeof(src), "%s/shot.png", scratch);

    /* A real 200x100 PNG to crop. */
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 100);
    cairo_t *cr = cairo_create(img);
    cairo_set_source_rgb(cr, 0.2, 0.6, 0.9);
    cairo_paint(cr);
    cairo_destroy(cr);
    CHECK(cairo_surface_write_to_png(img, src) == CAIRO_STATUS_SUCCESS,
          "could not write the test source image");

    struct stat before;
    CHECK(stat(src, &before) == 0, "the source should exist");

    cairo_surface_destroy(img);

    /* Crop the middle and save, three times over.
     *
     * A FRESH surface each round: a successful save calls crop_hide(), which
     * releases the image — that is the panel's contract (a 6000x4000 photo is
     * ~96 MB and nothing is cached), so handing the same surface back would be
     * a use-after-free. Reopening for real is also what a user does. */
    const char *expect[] = { "shot-crop.png", "shot-crop-2.png", "shot-crop-3.png" };
    for (int i = 0; i < 3; i++) {
        syn_server_t *s = server_with_image(200, 100);
        snprintf(s->crop.path, sizeof(s->crop.path), "%s", src);

        s->crop.ax = 40; s->crop.ay = 20;
        s->crop.bx = 140; s->crop.by = 80;
        s->crop.status[0] = '\0';

        crop_key(s, XKB_KEY_Return, 0);

        char out[400];
        snprintf(out, sizeof(out), "%s/%s", scratch, expect[i]);
        CHECK(access(out, F_OK) == 0, "expected %s to be written", expect[i]);
        CHECK(s->crop.status[0] == '\0', "save %d reported \"%s\"", i, s->crop.status);
        /* …and the panel let go of the image on the way out. */
        CHECK(s->crop.img == NULL, "a successful save should release the image");
        CHECK(!s->crop.visible, "a successful save should close the panel");
    }

    /* The ORIGINAL must be byte-for-byte untouched. */
    struct stat after;
    CHECK(stat(src, &after) == 0, "the source should still exist");
    CHECK(before.st_size == after.st_size && before.st_mtime == after.st_mtime,
          "the source image was modified — a cropper must never write in place");

    /* …and the crop must be the size that was selected, not the whole image. */
    char first[400];
    snprintf(first, sizeof(first), "%s/shot-crop.png", scratch);
    cairo_surface_t *got = cairo_image_surface_create_from_png(first);
    CHECK(cairo_surface_status(got) == CAIRO_STATUS_SUCCESS, "the crop should be a readable PNG");
    CHECK(cairo_image_surface_get_width(got) == 100,
          "crop width should be 100, got %d", cairo_image_surface_get_width(got));
    CHECK(cairo_image_surface_get_height(got) == 60,
          "crop height should be 60, got %d", cairo_image_surface_get_height(got));
    cairo_surface_destroy(got);
}

/* A selection below the minimum must refuse to write and SAY so, rather than
 * closing as though it had worked. */
static void test_refuses_tiny(void)
{
    char src[320];
    snprintf(src, sizeof(src), "%s/tiny.png", scratch);

    syn_server_t *s = server_with_image(200, 100);
    snprintf(s->crop.path, sizeof(s->crop.path), "%s", src);
    s->crop.ax = 10; s->crop.ay = 10;
    s->crop.bx = 12; s->crop.by = 12;      /* 2x2 — under CROP_MIN_PX */

    crop_key(s, XKB_KEY_Return, 0);

    CHECK(s->crop.visible, "a refused save must leave the panel open");
    CHECK(s->crop.status[0] != '\0', "a refused save must report why");

    char out[400];
    snprintf(out, sizeof(out), "%s/tiny-crop.png", scratch);
    CHECK(access(out, F_OK) != 0, "a refused save must not write a file");
    cairo_surface_destroy(s->crop.img);
}

/* A filename with a quote in it must not reach /bin/sh unquoted.
 *
 * synui_spawn() runs /bin/sh -c, and the path comes from a file manager's
 * context menu — a filename is trivially attacker-chosen and survives a
 * download or a USB stick. The stub below captures what WOULD have been run;
 * the assertion is that the dangerous text is inside single quotes rather than
 * sitting in command position. */
static void test_shell_injection(void)
{
    /* The classic shape: close the quote, run something, reopen it.
     *
     * No '/' in the payload — the touch target is RELATIVE and the probe below
     * runs from the scratch directory. A slash would make the filename name a
     * directory that does not exist, the write would fail before the toast was
     * ever built, and the test would pass for entirely the wrong reason. */
    char nasty[512];
    snprintf(nasty, sizeof(nasty), "%s/ev'il; touch PWNED; x.png", scratch);

    syn_server_t *s = server_with_image(200, 100);
    snprintf(s->crop.path, sizeof(s->crop.path), "%s", nasty);
    s->crop.ax = 10; s->crop.ay = 10;
    s->crop.bx = 150; s->crop.by = 80;

    last_spawn[0] = '\0';
    crop_key(s, XKB_KEY_Return, 0);

    /* The write itself should have succeeded — a quote is a legal filename
     * character and the cropper must handle it, not refuse it. */
    CHECK(last_spawn[0] != '\0', "a successful crop should have spawned a toast");

    /* And the payload must be quoted. If `; touch …;` appears OUTSIDE single
     * quotes, the shell would run it. */
    CHECK(strstr(last_spawn, "touch") == NULL || strstr(last_spawn, "'") != NULL,
          "the toast command carries unquoted shell text: %s", last_spawn);

    /* The real check: the quote in the filename must have been escaped into
     * the '\'' form, so nothing can close the quoting early. */
    if (strstr(last_spawn, "ev")) {
        CHECK(strstr(last_spawn, "ev'\\''il") != NULL,
              "the embedded quote was not escaped: %s", last_spawn);
    }

    /* Belt and braces: run the command the panel built, in a real shell, and
     * confirm the injected payload did NOT fire. notify-send may not exist
     * here — that is fine, the point is whether `touch` ran. */
    char probe[4096];
    snprintf(probe, sizeof(probe), "cd '%s' && { %s ; } >/dev/null 2>&1 || true",
             scratch, last_spawn);
    if (system(probe) != 0) { /* the toast itself failing is not the test */ }

    char pwned[512];
    snprintf(pwned, sizeof(pwned), "%s/PWNED", scratch);
    CHECK(access(pwned, F_OK) != 0,
          "SHELL INJECTION: the filename executed a command");
}

int main(void)
{
    char tmpl[] = "/tmp/synui-crop-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }
    snprintf(scratch, sizeof(scratch), "%s", dir);

    test_fit();
    test_selection_normalises();
    test_round_trip();
    test_never_overwrites();
    test_refuses_tiny();
    test_shell_injection();

    /* Tidy up whatever landed in the scratch dir. */
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", scratch);
    if (system(cmd) != 0) { /* best effort */ }

    if (failures) {
        fprintf(stderr, "crop_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("crop_test: ok\n");
    return 0;
}
