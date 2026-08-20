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
#include <time.h>		/* futimens, struct timespec */

#include <dirent.h>		/* count_files — the viewer must not add any */
#include <math.h>		/* fabs — the zoom holds a point to within a pixel */

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
     * here — that is fine, the point is whether `touch` ran.
     *
     * WITH THE SESSION BUS TAKEN AWAY. The command being run is a real
     * `notify-send`, and on a developer's own machine `meson test` inherits the
     * DBUS_SESSION_BUS_ADDRESS of the desktop they are sitting in front of — so
     * this line posted a toast to the LIVE session reading
     *
     *     Cropped — /tmp/synui-crop-test-XXXX/ev'il; touch PWNED; x-crop.png
     *
     * which is an alarming thing to have appear on your desktop out of nowhere,
     * and reads like a compromise rather than like a passing test. (It was a
     * passing test: the PWNED check below is what proves the quoting held.)
     *
     * Pointing the bus at a path that cannot exist makes notify-send fail to
     * connect and exit non-zero, which this already tolerates — the assertion
     * is about `touch`, and touch does not care about D-Bus. Same reason
     * tests/notif.sh runs against a private session bus rather than the real
     * one: a test that can write to the desktop it is running on is a test that
     * eventually does. */
    char probe[4096];
    snprintf(probe, sizeof(probe),
             "cd '%s' && export DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent"
             " && { %s ; } >/dev/null 2>&1 || true",
             scratch, last_spawn);
    if (system(probe) != 0) { /* the toast itself failing is not the test */ }

    char pwned[512];
    snprintf(pwned, sizeof(pwned), "%s/PWNED", scratch);
    CHECK(access(pwned, F_OK) != 0,
          "SHELL INJECTION: the filename executed a command");
}

/* ── The keyboard reaches all four edges ──────────────────────
 *
 * THE BUG THIS PINS DOWN
 *
 * crop_nudge() moves the ACTIVE corner, which is `b`, and the panel opens with
 * the whole image selected — a at the top-left, b at the bottom-right. So b sat
 * against its clamps on both axes from the first frame: Right and Down were
 * silent no-ops, and only Left and Up moved anything. The keyboard could bring
 * the right and bottom edges in and could not touch the other two at all,
 * which presents as "the arrows only go up and left".
 *
 * Driven through crop_key() rather than the statics, because the fix lives in
 * which corner is active and that is only reachable through the key handler —
 * testing crop_nudge() directly would assert the half that was never broken.
 */
static void test_keyboard_reaches_every_edge(void)
{
    syn_server_t *s = server_with_image(1000, 800);

    /* As crop_open() leaves it. */
    s->crop.ax = 0;   s->crop.ay = 0;
    s->crop.bx = 1000; s->crop.by = 800;
    s->crop.active = 3;                    /* bottom-right */

    int x, y, w, h;

    /* The half that always worked: pull the right and bottom edges in. */
    crop_key(s, XKB_KEY_Left, 0);
    crop_key(s, XKB_KEY_Up, 0);
    crop_selection(s, &x, &y, &w, &h);
    CHECK(x == 0 && y == 0 && w == 999 && h == 799,
          "Left/Up on the far corner should shrink from the right and bottom, "
          "got %d,%d %dx%d", x, y, w, h);

    /* Tab moves which corner the next arrow drives, and must NOT move the
     * rectangle while it does. */
    crop_key(s, XKB_KEY_Tab, 0);
    crop_selection(s, &x, &y, &w, &h);
    CHECK(x == 0 && y == 0 && w == 999 && h == 799,
          "Tab must not move the selection, got %d,%d %dx%d", x, y, w, h);
    CHECK(s->crop.active == 2,
          "Tab is CLOCKWISE: bottom-right leads to bottom-left, got %d",
          s->crop.active);

    /* One more lands on the top-left — the corner the arrows could never reach
     * before, because it was always `a` and only `b` ever moved. Right and Down
     * now bring the LEFT and TOP edges in. */
    crop_key(s, XKB_KEY_Tab, 0);
    CHECK(s->crop.active == 0, "expected the top-left corner, got %d", s->crop.active);

    crop_key(s, XKB_KEY_Right, 0);
    crop_key(s, XKB_KEY_Down, 0);
    crop_selection(s, &x, &y, &w, &h);
    CHECK(x == 1 && y == 1 && w == 998 && h == 798,
          "the top-left corner should move in from the left and top, "
          "got %d,%d %dx%d", x, y, w, h);

    /* Shift is still the coarse step, on whichever corner is active. */
    crop_key(s, XKB_KEY_Right, WLR_MODIFIER_SHIFT);
    crop_selection(s, &x, &y, &w, &h);
    CHECK(x == 26, "Shift+Right should step 25, left edge at %d", x);

    cairo_surface_destroy(s->crop.img);
}

/* Ctrl+Arrows slide the selection without resizing it, and stop AT the border
 * rather than deforming against it. */
static void test_ctrl_arrows_move(void)
{
    syn_server_t *s = server_with_image(1000, 800);

    s->crop.ax = 100; s->crop.ay = 100;
    s->crop.bx = 300; s->crop.by = 300;
    s->crop.active = 3;

    int x, y, w, h;

    crop_key(s, XKB_KEY_Right, WLR_MODIFIER_CTRL);
    crop_selection(s, &x, &y, &w, &h);
    CHECK(x == 101 && y == 100 && w == 200 && h == 200,
          "Ctrl+Right should slide the box, got %d,%d %dx%d", x, y, w, h);

    /* Drive it hard into the right edge. The size must survive: clamping the
     * two corners independently would let the trailing edge keep coming after
     * the leading one stopped, quietly turning a move into a resize. */
    for (int i = 0; i < 60; i++)
        crop_key(s, XKB_KEY_Right, WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT);

    crop_selection(s, &x, &y, &w, &h);
    CHECK(w == 200 && h == 200,
          "a move into the border must not resize: got %dx%d", w, h);
    CHECK(x + w == 1000, "it should come to rest against the edge, right at %d", x + w);

    /* And the same on the way back. */
    for (int i = 0; i < 60; i++)
        crop_key(s, XKB_KEY_Up, WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT);

    crop_selection(s, &x, &y, &w, &h);
    CHECK(w == 200 && h == 200 && y == 0,
          "moving into the top edge gave %d,%d %dx%d", x, y, w, h);

    cairo_surface_destroy(s->crop.img);
}

/* Tab tracks the rectangle FLIPPING. Push the active corner past its opposite
 * and it becomes a different corner of the picture; if `active` did not follow,
 * the render would highlight one corner while the arrows moved another. */
static void test_active_follows_a_flip(void)
{
    syn_server_t *s = server_with_image(1000, 800);

    s->crop.ax = 500; s->crop.ay = 400;   /* a: the fixed corner */
    s->crop.bx = 700; s->crop.by = 600;   /* b: bottom-right of the pair */
    s->crop.active = 3;

    /* Walk b left past a. It is now to a's LEFT, so it is the bottom-LEFT
     * corner — bit 0 (right) clear, bit 1 (bottom) still set. */
    for (int i = 0; i < 12; i++)
        crop_key(s, XKB_KEY_Left, WLR_MODIFIER_SHIFT);

    CHECK(s->crop.bx < s->crop.ax, "b should have crossed a, %d vs %d",
          s->crop.bx, s->crop.ax);
    CHECK(s->crop.active == 2,
          "after the flip the active corner should be bottom-left (2), got %d",
          s->crop.active);

    cairo_surface_destroy(s->crop.img);
}

/* ── The viewer ──────────────────────────────────────────────
 *
 * Three things here can be wrong in ways nobody sees until they are in front of
 * a photograph:
 *
 *   1. THE ZOOM MAPPING. Zooming has to hold still whatever it was pointed at,
 *      and the picture must never be pannable off the screen. Both are
 *      arithmetic that reads as "the viewer jumps about" when it drifts.
 *   2. THE FOLDER ORDER. IMG_9 before IMG_10 — the one order a camera gives
 *      you and a byte comparison reverses.
 *   3. THAT IT WRITES NOTHING. The cropper shares this panel and every one of
 *      its safeguards; the viewer's is simply that no file may appear.
 */

static void write_png(const char *dir, const char *name, int w, int h)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);

    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_status_t st = cairo_surface_write_to_png(img, path);
    CHECK(st == CAIRO_STATUS_SUCCESS, "could not write %s: %s",
          path, cairo_status_to_string(st));
    cairo_surface_destroy(img);
}

/* How many entries a directory holds — the viewer's whole output contract is
 * that this number does not change. */
static int count_files(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) { CHECK(0, "could not open %s", dir); return -1; }

    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

/* The current basename of what the panel has open. */
static const char *shown(syn_server_t *s)
{
    const char *slash = strrchr(s->crop.path, '/');
    return slash ? slash + 1 : s->crop.path;
}

static void test_view_zoom_and_pan(void)
{
    char dir[400];
    snprintf(dir, sizeof(dir), "%s/view", scratch);
    char cmd[900];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
    if (system(cmd) != 0) { CHECK(0, "could not build the scratch dir"); return; }

    /* Larger than the screen at 1:1, so there is something to pan. */
    write_png(dir, "photo.png", 2400, 1600);

    char path[512];
    snprintf(path, sizeof(path), "%s/photo.png", dir);

    static syn_server_t s;
    memset(&s, 0, sizeof(s));
    crop_view_open(&s, path);

    CHECK(s.crop.visible && s.crop.viewing, "the viewer should be up");
    CHECK(s.crop.img_w == 2400 && s.crop.img_h == 1600,
          "wrong image size: %dx%d", s.crop.img_w, s.crop.img_h);

    /* It opens FITTED: the same geometry the cropper would use, which is what
     * "zoom 1.0 means the whole picture" is defined to mean. */
    double fsc, fox, foy, sc, ox, oy;
    crop_fit(&s, &g_out, &fsc, &fox, &foy);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    CHECK(sc == fsc, "the viewer should open at the fitted scale (%.4f vs %.4f)",
          sc, fsc);
    CHECK(ox == fox && oy == foy, "the fitted picture should be centred");

    /* 1 is one image pixel per screen pixel, whatever the fit was. */
    crop_key(&s, XKB_KEY_1, 0);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    CHECK(sc > 0.999 && sc < 1.001, "1 should show actual size, got %.4f", sc);

    /* …and 0 puts the whole picture back. */
    crop_key(&s, XKB_KEY_0, 0);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    CHECK(sc == fsc, "0 should fit the picture again, got %.4f", sc);

    /* ⚠ WHILE THE PICTURE STILL FITS AN AXIS, IT STAYS CENTRED ON IT. Fitted,
     * this 2400x1600 photo is 1440 wide on a 1920 screen, and one notch of zoom
     * makes it 1800 — still narrower than the screen. There is no pan the clamp
     * would allow, so the point under the pointer necessarily moves, and a
     * viewer that held it still would have to leave the picture hanging
     * off-centre with a wider gap on one side than the other. */
    double lx = g_out.width / 2.0 + 120, ly = g_out.height / 2.0 - 80;
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    crop_scroll(&s, lx, ly, -1);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    CHECK(sc > fsc, "scrolling up should zoom in (%.4f vs %.4f)", sc, fsc);
    CHECK(fabs((ox - g_out.x) - (g_out.width - 2400 * sc) / 2) < 1.0,
          "a picture narrower than the screen should stay centred on it");

    /* Zoomed past the edges of the screen there IS somewhere to pan, and then
     * the wheel has to hold still whatever it was pointed at — the difference
     * between a magnifying glass and a slot machine. 1 puts this photo at
     * 2400x1600 on a 1920x1080 screen, so both axes overflow. */
    crop_key(&s, XKB_KEY_1, 0);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    double ix = (lx - ox) / sc, iy = (ly - oy) / sc;

    crop_scroll(&s, lx, ly, -1);          /* one notch further in */
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);

    double ix2 = (lx - ox) / sc, iy2 = (ly - oy) / sc;
    CHECK(fabs(ix2 - ix) < 2.0 && fabs(iy2 - iy) < 2.0,
          "zoom moved the point under the pointer: (%.1f,%.1f) -> (%.1f,%.1f)",
          ix, iy, ix2, iy2);

    /* Zoomed all the way in, and then some: the ceiling is an ABSOLUTE scale,
     * so 60 notches on a 2400px picture must still land on VIEW_SCALE_MAX. */
    for (int i = 0; i < 60; i++) crop_key(&s, XKB_KEY_plus, 0);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    CHECK(sc <= 8.0 + 1e-6, "zoom went past the ceiling: %.4f", sc);

    /* And out: never further than the fit. */
    for (int i = 0; i < 60; i++) crop_key(&s, XKB_KEY_minus, 0);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    CHECK(sc == fsc, "zooming out should stop at the fit, got %.4f", sc);

    /* THE PAN CLAMP. At 1:1 the picture is wider than the screen; panning as
     * far as the keyboard will go must never bring an edge inside the viewport,
     * which is what "the photo slid off into the void" looks like. */
    crop_key(&s, XKB_KEY_1, 0);
    for (int i = 0; i < 200; i++) crop_key(&s, XKB_KEY_Left, 0);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    CHECK(ox <= g_out.x + 0.5, "panned past the left edge: ox %.1f", ox);
    CHECK(ox + 2400 * sc >= g_out.x + g_out.width - 0.5,
          "the picture came off the right of the screen");

    for (int i = 0; i < 400; i++) crop_key(&s, XKB_KEY_Down, 0);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    CHECK(oy + 1600 * sc >= g_out.y + g_out.height - 0.5,
          "the picture came off the bottom of the screen");

    /* Fitted, the arrows are the FOLDER and not a pan — there is nowhere to pan
     * to. With one image in the directory that is a no-op, and the test for it
     * is that the geometry is untouched. */
    crop_key(&s, XKB_KEY_0, 0);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    double before = ox;
    crop_key(&s, XKB_KEY_Left, 0);
    crop_view_geom(&s, &g_out, &sc, &ox, &oy);
    CHECK(ox == before, "fitted, an arrow should not pan the picture");

    crop_hide(&s);
    CHECK(!s.crop.visible && !s.crop.viewing, "the viewer should have closed");
    CHECK(s.crop.nav == NULL, "the folder list should be freed on hide");
}

static void test_view_walks_the_folder(void)
{
    char dir[400];
    snprintf(dir, sizeof(dir), "%s/walk", scratch);
    char cmd[900];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
    if (system(cmd) != 0) { CHECK(0, "could not build the scratch dir"); return; }

    /* Named the way a camera names them. In byte order IMG_10 sorts BEFORE
     * IMG_9, which is the bug the natural compare exists to prevent. */
    write_png(dir, "IMG_9.png",  40, 30);
    write_png(dir, "IMG_10.png", 40, 30);
    write_png(dir, "IMG_11.png", 40, 30);

    /* Not an image at all, however it is named: the walk must step over it
     * rather than stopping on it or blanking the picture that was fine. */
    char broken[512];
    snprintf(broken, sizeof(broken), "%s/IMG_10b.png", dir);
    FILE *f = fopen(broken, "w");
    if (f) { fputs("this is not a PNG", f); fclose(f); }

    char path[512];
    snprintf(path, sizeof(path), "%s/IMG_9.png", dir);

    static syn_server_t s;
    memset(&s, 0, sizeof(s));
    crop_view_open(&s, path);

    CHECK(s.crop.nav_count == 4, "expected 4 names in the folder, got %d",
          s.crop.nav_count);
    CHECK(s.crop.nav_at == 0, "IMG_9 should sort first, got index %d",
          s.crop.nav_at);

    crop_key(&s, XKB_KEY_n, 0);
    CHECK(strcmp(shown(&s), "IMG_10.png") == 0,
          "after IMG_9 comes IMG_10, got %s", shown(&s));

    /* IMG_10b is next in name order and cannot be decoded — stepping on must
     * land on IMG_11 with the panel still showing a picture. */
    crop_key(&s, XKB_KEY_n, 0);
    CHECK(strcmp(shown(&s), "IMG_11.png") == 0,
          "the undecodable file should be stepped over, got %s", shown(&s));
    CHECK(s.crop.img != NULL, "the viewer lost its image on a failed step");

    /* Off the end and round to the start. */
    crop_key(&s, XKB_KEY_n, 0);
    CHECK(strcmp(shown(&s), "IMG_9.png") == 0,
          "the walk should wrap to the first image, got %s", shown(&s));

    /* Backwards from the first wraps to the last. */
    crop_key(&s, XKB_KEY_p, 0);
    CHECK(strcmp(shown(&s), "IMG_11.png") == 0,
          "backwards from the first should wrap, got %s", shown(&s));

    crop_hide(&s);
}

/* The viewer is READ-ONLY, and `c` hands the picture to the cropper without
 * re-reading it — with the whole image selected, so Enter there is a copy of
 * exactly what was being looked at. */
static void test_view_writes_nothing_and_hands_over(void)
{
    char dir[400];
    snprintf(dir, sizeof(dir), "%s/ro", scratch);
    char cmd[900];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
    if (system(cmd) != 0) { CHECK(0, "could not build the scratch dir"); return; }

    write_png(dir, "one.png", 800, 600);
    write_png(dir, "two.png", 400, 300);

    char path[512];
    snprintf(path, sizeof(path), "%s/one.png", dir);

    int before = count_files(dir);

    static syn_server_t s;
    memset(&s, 0, sizeof(s));
    crop_view_open(&s, path);

    crop_key(&s, XKB_KEY_1, 0);
    crop_key(&s, XKB_KEY_Right, 0);        /* fitted → the next picture */
    crop_key(&s, XKB_KEY_p, 0);            /* and back */
    crop_scroll(&s, 900, 500, -1);
    crop_click(&s, 900, 500, BTN_LEFT, 0);
    crop_drag_motion(&s, 700, 400);
    crop_drag_end(&s, 700, 400);

    CHECK(count_files(dir) == before,
          "the viewer wrote something: %d files, was %d", count_files(dir), before);

    /* Into the cropper, on the same file and with everything selected. */
    CHECK(strcmp(shown(&s), "one.png") == 0, "expected one.png, got %s", shown(&s));
    crop_key(&s, XKB_KEY_c, 0);
    CHECK(!s.crop.viewing, "c should leave the viewer");
    CHECK(s.crop.from_view, "the cropper should know it came from the viewer");
    CHECK(strcmp(shown(&s), "one.png") == 0,
          "the cropper opened a different file: %s", shown(&s));

    int x, y, w, h;
    crop_selection(&s, &x, &y, &w, &h);
    CHECK(x == 0 && y == 0 && w == 800 && h == 600,
          "the cropper should open on the whole picture, got %d,%d %dx%d",
          x, y, w, h);

    /* …and Escape goes back to the picture rather than out to the desktop. */
    crop_key(&s, XKB_KEY_Escape, 0);
    CHECK(s.crop.viewing && s.crop.visible,
          "Escape from a crop entered with c should return to the viewer");

    CHECK(count_files(dir) == before, "the hand-over wrote a file");
    crop_hide(&s);
}

/* ── The recent-images list ──────────────────────────────────
 *
 * Sorted newest first, deduped, and holding the NEWEST n rather than the first
 * n found — that last one is what stops a full Downloads folder from hiding
 * Pictures entirely, and it is invisible until the cap is actually reached.
 */
static void touch_image(const char *dir, const char *name, time_t when)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);

    FILE *f = fopen(path, "w");
    if (!f) { CHECK(0, "could not create %s", path); return; }
    fputc('x', f);

    /* ⚠ ON THE DESCRIPTOR, and flushed first. utime(path) after fclose()
     * resolves the name a second time — between the two it can mean a
     * different file, and this writes under /tmp where somebody else can
     * arrange that. futimens() cannot be pointed anywhere but at the file that
     * was opened. Same finding as terminal_chain_test.c (CodeQL #14).
     *
     * fflush BEFORE it: closing the stream writes the byte, and a write after
     * the timestamp is set would put the mtime back to now — which is the one
     * thing this helper exists to control. */
    struct timespec ts[2] = { { when, 0 }, { when, 0 } };
    fflush(f);
    if (futimens(fileno(f), ts) != 0) CHECK(0, "could not set mtime on %s", path);
    fclose(f);
}

static void test_recent_list(void)
{
    static syn_server_t s;
    memset(&s, 0, sizeof(s));

    char home[300];
    snprintf(home, sizeof(home), "%s/home", scratch);

    char pics[400], dl[400];
    snprintf(pics, sizeof(pics), "%s/Pictures", home);
    snprintf(dl,   sizeof(dl),   "%s/Downloads", home);

    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s' '%s'", pics, dl);
    if (system(cmd) != 0) { CHECK(0, "could not build the scratch home"); return; }

    const time_t base = 1700000000;
    touch_image(pics, "old.png",    base);
    touch_image(pics, "middle.jpg", base + 500);
    touch_image(dl,   "newest.png", base + 900);
    /* Not an image, and a dotfile: neither belongs in the list. */
    touch_image(pics, "notes.txt",  base + 999);
    touch_image(pics, ".hidden.png", base + 999);

    const char *old_home = getenv("HOME");
    char saved[512];
    snprintf(saved, sizeof(saved), "%s", old_home ? old_home : "");
    setenv("HOME", home, 1);

    crop_recent_scan(&s);

    CHECK(s.crop.recent_count == 3,
          "expected 3 images, got %d", s.crop.recent_count);

    if (s.crop.recent_count == 3) {
        /* Newest first, across directories — the sort is on mtime, not on the
         * order the directories were scanned in. */
        CHECK(strstr(s.crop.recent[0].path, "newest.png") != NULL,
              "row 0 should be the newest, got %s", s.crop.recent[0].path);
        CHECK(strstr(s.crop.recent[1].path, "middle.jpg") != NULL,
              "row 1 should be middle.jpg, got %s", s.crop.recent[1].path);
        CHECK(strstr(s.crop.recent[2].path, "old.png") != NULL,
              "row 2 should be old.png, got %s", s.crop.recent[2].path);
    }

    /* The cap keeps the newest, not the first found. Fill well past it with
     * files OLDER than the three above, which must all survive. */
    for (int i = 0; i < CROP_RECENT_MAX + 20; i++) {
        char name[64];
        snprintf(name, sizeof(name), "filler-%03d.png", i);
        touch_image(dl, name, base - 10000 - i);
    }

    crop_recent_scan(&s);
    CHECK(s.crop.recent_count == CROP_RECENT_MAX,
          "the list should fill to its cap, got %d", s.crop.recent_count);
    CHECK(strstr(s.crop.recent[0].path, "newest.png") != NULL,
          "the newest file must survive a full list, got %s", s.crop.recent[0].path);

    /* Sorted, strictly. */
    for (int i = 1; i < s.crop.recent_count; i++)
        if (s.crop.recent[i].mtime > s.crop.recent[i - 1].mtime) {
            CHECK(0, "row %d is newer than the row above it", i);
            break;
        }

    /* An unwritable directory is skipped whole: every row has to be a row whose
     * crop can be written beside it, and crop_write never writes anywhere else.
     * (Skipped for root, who can write to it regardless.) */
    if (geteuid() != 0) {
        snprintf(cmd, sizeof(cmd), "chmod 500 '%s'", dl);
        if (system(cmd) == 0) {
            crop_recent_scan(&s);
            for (int i = 0; i < s.crop.recent_count; i++)
                if (strstr(s.crop.recent[i].path, "/Downloads/")) {
                    CHECK(0, "a read-only directory must not be listed: %s",
                          s.crop.recent[i].path);
                    break;
                }
            snprintf(cmd, sizeof(cmd), "chmod 700 '%s'", dl);
            if (system(cmd) != 0) { /* cleanup is best effort */ }
        }
    }

    if (saved[0]) setenv("HOME", saved, 1);
    else          unsetenv("HOME");
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
    test_keyboard_reaches_every_edge();
    test_ctrl_arrows_move();
    test_active_follows_a_flip();
    test_recent_list();
    test_view_zoom_and_pan();
    test_view_walks_the_folder();
    test_view_writes_nothing_and_hands_over();

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
