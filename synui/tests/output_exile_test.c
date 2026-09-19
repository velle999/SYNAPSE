/*
 * output_exile_test.c — a window goes back to the monitor it was rescued from.
 *
 * ⛔ THE BUG. A monitor in standby is not a connected monitor: the panel drops
 * its DP link, wlroots destroys the wlr_output, and output_destroy() sweeps
 * every window that was on it onto a screen that still exists. Two seconds
 * later the panel wakes, the connector re-enumerates under the same name — and
 * until output_exile.c the windows stayed where the rescue had put them. From
 * velle's journal, an ordinary wake:
 *
 *     15:34:27  2 window(s) re-homed from DP-3 onto DP-2
 *     15:34:29  new output DP-3 2560x1440 — showing workspace 1
 *
 * ⚠ WHY THIS IS A UNIT TEST AND NOT A RIG. There is no way to make a connector
 * come back under its own name headless. wlroots numbers headless outputs from
 * a counter that only ever goes up — measured here: a virtual display removed
 * as HEADLESS-2 comes back as HEADLESS-3 — so `synctl virtual add/remove`
 * cannot reproduce the one thing the fix keys on, which is DP-3 returning as
 * DP-3. And the real event needs a monitor to physically sleep. So the two
 * halves are driven directly, exactly as the compositor calls them:
 * output_exile_take() from output_destroy()/dispcfg_detach(), and
 * output_exile_return() from server_new_output()/dispcfg_rechain().
 *
 * What that pins, none of which the build catches:
 *
 *   - the window comes back AT ALL, onto the connector it was taken off
 *   - it comes back to its BOX — and the box is relative, so a monitor that
 *     returns at a different origin does not hand the window coordinates that
 *     now land on its neighbour
 *   - a CASCADE (screen A onto B, then B onto nothing) sends each window back
 *     to the screen it started on, not the one it was passing through
 *   - a TILED window gets the screen and no box: the layout owns its geometry
 *     and a box handed back here would be a resize the tiler overwrites
 *   - an EXPLICIT move forgets, so a monitor re-appearing cannot undo it
 *   - a window nobody displaced is never touched by a returning screen
 *
 * Run as:
 *     ninja -C build && ./build/output_exile_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_output.h>

#include "synui.h"

static int failures;
static int checks;

#define CHECK(cond, ...) do {                                    \
        checks++;                                                \
        if (cond) { } else {                                     \
            failures++;                                          \
            printf("  FAIL — ");                                 \
            printf(__VA_ARGS__); printf("\n");                   \
        }                                                        \
    } while (0)

/* ── The compositor, stubbed ─────────────────────────────────
 *
 * output_exile.c is linked alone. Placement belongs to layout.c and is already
 * whatever it is — what this file is about is which window, which screen and
 * which box, so the placement calls are recorded rather than performed.
 */

#define FAKE_MAX 8
static struct { syn_output_t *o; struct wlr_box b; } fake_box[FAKE_MAX];
static int fake_boxes;

void output_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box)
{
    (void)s;
    for (int i = 0; i < fake_boxes; i++)
        if (fake_box[i].o == o) { *box = fake_box[i].b; return; }
    *box = (struct wlr_box){ 0, 0, 1920, 1080 };   /* what the real one does */
}

/* The last box handed to the placement path, and how many times. */
static syn_view_t   *placed_view;
static struct wlr_box placed_box;
static int            placements;

void view_place_saved_box(syn_server_t *s, syn_view_t *view,
                          struct wlr_box saved)
{
    (void)s;
    placed_view = view;
    placed_box  = saved;
    placements++;
    view->x = saved.x; view->y = saved.y;
    view->w = saved.width; view->h = saved.height;
}

static int resizes;
void view_resize(syn_view_t *view, int x, int y, int w, int h)
{
    resizes++;
    view->x = x; view->y = y; view->w = w; view->h = h;
}

void view_fullscreen_rescale(syn_view_t *view)   { (void)view; }

static int refreshes, applies;
void view_refresh_visibility(syn_server_t *s) { (void)s; refreshes++; }
void layout_apply_visible(syn_server_t *s)    { (void)s; applies++;   }

/* ── A desk, made by hand ────────────────────────────────── */

/* Every phase gets a fresh desk and fresh counters — the output table is
 * per-desk, and a counter left over from the phase before reads as a call this
 * one made. */
static syn_server_t *mk_server(void)
{
    fake_boxes = 0;
    placements = resizes = refreshes = applies = 0;
    placed_view = NULL;
    placed_box  = (struct wlr_box){ 0, 0, 0, 0 };

    syn_server_t *s = calloc(1, sizeof(*s));
    if (!s) { perror("calloc"); exit(1); }
    wl_list_init(&s->outputs);
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        s->workspaces[i].index  = i;
        s->workspaces[i].layout = LAYOUT_TILING;
        wl_list_init(&s->workspaces[i].windows);
    }
    return s;
}

static syn_output_t *mk_output(syn_server_t *s, const char *name,
                               int x, int y, int w, int h)
{
    syn_output_t *o = calloc(1, sizeof(*o));
    struct wlr_output *wo = calloc(1, sizeof(*wo));
    if (!o || !wo) { perror("calloc"); exit(1); }
    wo->name = strdup(name);
    o->wlr_output = wo;
    o->server = s;
    wl_list_insert(s->outputs.prev, &o->link);

    /* ⚠ LOUD. A table that silently stopped recording would hand every later
     * output the 1920x1080 fallback, and the relative-box assertions would
     * then be measuring the fallback rather than the screen — a green run
     * proving nothing. */
    if (fake_boxes >= FAKE_MAX) {
        printf("  FAIL — the test's output table is full; raise FAKE_MAX\n");
        exit(1);
    }
    fake_box[fake_boxes].o = o;
    fake_box[fake_boxes].b = (struct wlr_box){ x, y, w, h };
    fake_boxes++;
    return o;
}

/* The connector comes back: same name, a fresh syn_output_t, which is exactly
 * what server_new_output() builds. Its origin is an argument because a monitor
 * does not have to return where it was. */
static syn_output_t *reconnect(syn_server_t *s, syn_output_t *gone,
                               const char *name, int x, int y, int w, int h)
{
    wl_list_remove(&gone->link);   /* output_destroy() unlinks before freeing */
    return mk_output(s, name, x, y, w, h);
}

static syn_view_t *mk_view(syn_server_t *s, int ws, syn_output_t *o,
                           int x, int y, int w, int h, int floating)
{
    syn_view_t *v = calloc(1, sizeof(*v));
    if (!v) { perror("calloc"); exit(1); }
    v->server    = s;
    v->workspace = &s->workspaces[ws];
    v->output    = o;
    v->mapped    = 1;
    v->floating  = floating;
    v->x = x; v->y = y; v->w = w; v->h = h;
    wl_list_insert(s->workspaces[ws].windows.prev, &v->link);
    return v;
}

static const char *out_name(syn_view_t *v)
{
    return v->output ? v->output->wlr_output->name : "(none)";
}

/* ── 1. standby: the screen goes, the screen comes back ──── */
static void test_standby_round_trip(void)
{
    printf("exile: a monitor goes to standby and comes back\n");

    syn_server_t *s = mk_server();
    syn_output_t *dp2 = mk_output(s, "DP-2", 0,    0, 1920, 1080);
    syn_output_t *dp3 = mk_output(s, "DP-3", 1920, 0, 2560, 1440);

    /* A hand-placed window 680px into DP-3. */
    syn_view_t *v = mk_view(s, 0, dp3, 1920 + 680, 100, 800, 600, 1);

    int moved = output_exile_take(s, dp3, dp2);
    CHECK(moved == 1, "one window was re-homed, got %d", moved);
    CHECK(v->output == dp2, "it landed on DP-2, not %s", out_name(v));
    CHECK(strcmp(v->exile_from, "DP-3") == 0,
          "it remembers DP-3, remembers \"%s\"", v->exile_from);
    CHECK(v->exile_geo.x == 680 && v->exile_geo.y == 100,
          "the box is relative to DP-3's origin (680,100), got (%d,%d)",
          v->exile_geo.x, v->exile_geo.y);

    /* …and two seconds later. */
    placements = 0;
    syn_output_t *dp3b = reconnect(s, dp3, "DP-3", 1920, 0, 2560, 1440);
    int came = output_exile_return(s, dp3b);

    CHECK(came == 1, "one window came back, got %d", came);
    CHECK(v->output == dp3b, "it is on the new DP-3, not %s", out_name(v));
    CHECK(v->exile_from[0] == '\0',
          "the record is spent, still holds \"%s\"", v->exile_from);
    CHECK(placements == 1, "it was placed once, %d times", placements);
    CHECK(placed_view == v && placed_box.x == 1920 + 680 && placed_box.y == 100,
          "back at its own box (2600,100), got (%d,%d)",
          placed_box.x, placed_box.y);
    CHECK(refreshes > 0 && applies > 0,
          "the desk was re-flowed after the move (%d refresh, %d apply)",
          refreshes, applies);

    /* A second return finds nothing left to do — the record was spent, and a
     * wake that fires the sweep twice must not move anything a second time. */
    placements = 0;
    CHECK(output_exile_return(s, dp3b) == 0, "the second return is a no-op");
    CHECK(placements == 0, "…and places nothing");
}

/* ── 2. it comes back somewhere else ─────────────────────── */
static void test_returns_at_a_new_origin(void)
{
    printf("exile: the monitor comes back at a different place in the desk\n");

    syn_server_t *s = mk_server();
    syn_output_t *dp2 = mk_output(s, "DP-2", 0,    0, 1920, 1080);
    syn_output_t *dp3 = mk_output(s, "DP-3", 1920, 0, 2560, 1440);
    syn_view_t   *v   = mk_view(s, 0, dp3, 1920 + 680, 100, 800, 600, 1);

    output_exile_take(s, dp3, dp2);

    /* Same connector, now to the LEFT of DP-2 — a grid the display panel
     * re-packed while it was away. The absolute box it had is over DP-2 now;
     * only the relative one is still true. */
    placements = 0;
    syn_output_t *dp3b = reconnect(s, dp3, "DP-3", -2560, 0, 2560, 1440);
    CHECK(output_exile_return(s, dp3b) == 1, "the window came back");
    CHECK(v->output == dp3b, "onto DP-3, not %s", out_name(v));
    CHECK(placed_box.x == -2560 + 680 && placed_box.y == 100,
          "placed at the same spot ON DP-3 (-1880,100), got (%d,%d)",
          placed_box.x, placed_box.y);
}

/* ── 3. the cascade: A onto B, then B onto nothing ───────── */
static void test_cascade_keeps_the_first_home(void)
{
    printf("exile: screens going one after another\n");

    /* This is 2026-09-19 07:03 in the journal, exactly:
     *     3 window(s) re-homed from HDMI-A-1 onto DP-3
     *     3 window(s) re-homed from DP-3 onto (no output left)          */
    syn_server_t *s   = mk_server();
    syn_output_t *hdmi = mk_output(s, "HDMI-A-1", 0,    0, 1920, 1080);
    syn_output_t *dp3  = mk_output(s, "DP-3",     1920, 0, 2560, 1440);

    syn_view_t *from_hdmi = mk_view(s, 0, hdmi, 40,        60, 640, 480, 1);
    syn_view_t *from_dp3  = mk_view(s, 0, dp3,  1920 + 40, 60, 640, 480, 1);

    output_exile_take(s, hdmi, dp3);
    CHECK(from_hdmi->output == dp3, "the HDMI window went to DP-3");

    output_exile_take(s, dp3, NULL);
    CHECK(from_hdmi->output == NULL && from_dp3->output == NULL,
          "the last screen leaves both windows homeless");
    CHECK(strcmp(from_hdmi->exile_from, "HDMI-A-1") == 0,
          "the HDMI window still names HDMI-A-1, names \"%s\" — the second hop "
          "must not overwrite the first", from_hdmi->exile_from);
    CHECK(strcmp(from_dp3->exile_from, "DP-3") == 0,
          "the DP-3 window names DP-3, names \"%s\"", from_dp3->exile_from);

    /* HDMI-A-1 comes back first; it takes its own window and leaves the other
     * one alone even though that one has nowhere to be. */
    syn_output_t *hdmi_b = reconnect(s, hdmi, "HDMI-A-1", 0, 0, 1920, 1080);
    CHECK(output_exile_return(s, hdmi_b) == 1, "HDMI-A-1 took one window back");
    CHECK(from_hdmi->output == hdmi_b, "…and it is the one that lived there");
    CHECK(from_dp3->output == NULL,
          "the DP-3 window is still waiting, not on %s", out_name(from_dp3));

    syn_output_t *dp3_b = reconnect(s, dp3, "DP-3", 1920, 0, 2560, 1440);
    CHECK(output_exile_return(s, dp3_b) == 1, "DP-3 took its own back");
    CHECK(from_dp3->output == dp3_b, "…onto the screen it started on");
}

/* ── 4. a tiled window gets the screen, not a box ────────── */
static void test_tiled_window_gets_no_box(void)
{
    printf("exile: a tiled window — the layout owns its geometry\n");

    syn_server_t *s = mk_server();
    syn_output_t *dp2 = mk_output(s, "DP-2", 0,    0, 1920, 1080);
    syn_output_t *dp3 = mk_output(s, "DP-3", 1920, 0, 2560, 1440);
    syn_view_t   *v   = mk_view(s, 0, dp3, 1920, 0, 2560, 1440, 0);  /* tiled */

    output_exile_take(s, dp3, dp2);
    placements = 0; resizes = 0;
    syn_output_t *dp3b = reconnect(s, dp3, "DP-3", 1920, 0, 2560, 1440);

    CHECK(output_exile_return(s, dp3b) == 1, "it came back");
    CHECK(v->output == dp3b, "onto DP-3, not %s", out_name(v));
    CHECK(placements == 0 && resizes == 0,
          "and was not placed by hand (%d place, %d resize) — layout_apply "
          "re-tiles it", placements, resizes);

    /* A maximized window is in the same class: it is marked floating, but its
     * box is the output's usable area and layout_apply re-fits it. */
    syn_view_t *m = mk_view(s, 0, dp3b, 1920, 0, 2560, 1440, 1);
    m->maximized = 1;
    output_exile_take(s, dp3b, dp2);
    placements = 0;
    syn_output_t *dp3c = reconnect(s, dp3b, "DP-3", 1920, 0, 2560, 1440);
    output_exile_return(s, dp3c);
    CHECK(m->output == dp3c, "the maximized window came back too");
    CHECK(placements == 0, "…without a box of its own (%d)", placements);
}

/* ── 5. a fullscreen window covers the screen it returns to ── */
static void test_fullscreen_refits(void)
{
    printf("exile: a fullscreen window\n");

    syn_server_t *s = mk_server();
    syn_output_t *dp2 = mk_output(s, "DP-2", 0,    0, 1920, 1080);
    syn_output_t *dp3 = mk_output(s, "DP-3", 1920, 0, 2560, 1440);
    syn_view_t   *v   = mk_view(s, 0, dp3, 1920, 0, 2560, 1440, 1);
    v->fullscreen = 1;

    output_exile_take(s, dp3, dp2);
    resizes = 0;
    /* Back at a DIFFERENT mode — a 1080p mode the panel came up in. The window
     * has to take the box the screen has NOW, or a game comes back covering
     * two thirds of it. */
    syn_output_t *dp3b = reconnect(s, dp3, "DP-3", 1920, 0, 1920, 1080);
    CHECK(output_exile_return(s, dp3b) == 1, "it came back");
    CHECK(resizes == 1, "it was resized once, %d times", resizes);
    CHECK(v->x == 1920 && v->y == 0 && v->w == 1920 && v->h == 1080,
          "to the whole of DP-3 as it is now, got %dx%d at (%d,%d)",
          v->w, v->h, v->x, v->y);
}

/* ── 6. an explicit move overrules the record ────────────── */
static void test_a_deliberate_move_forgets(void)
{
    printf("exile: the user moves the window themselves\n");

    syn_server_t *s = mk_server();
    syn_output_t *dp2 = mk_output(s, "DP-2", 0,    0, 1920, 1080);
    syn_output_t *dp3 = mk_output(s, "DP-3", 1920, 0, 2560, 1440);
    syn_view_t   *v   = mk_view(s, 0, dp3, 1920 + 40, 60, 800, 600, 1);

    output_exile_take(s, dp3, dp2);
    /* Super+O, a drag onto another monitor — everything goes through
     * view_set_output(), and this is the line it calls. */
    view_exile_forget(v);

    placements = 0;
    syn_output_t *dp3b = reconnect(s, dp3, "DP-3", 1920, 0, 2560, 1440);
    CHECK(output_exile_return(s, dp3b) == 0,
          "DP-3 coming back takes nothing");
    CHECK(v->output == dp2, "the window stays where it was put, on %s",
          out_name(v));
    CHECK(placements == 0, "and nothing was re-placed (%d)", placements);
}

/* ── 7. a window nobody displaced is not touched ─────────── */
static void test_untouched_windows(void)
{
    printf("exile: windows that were never displaced\n");

    syn_server_t *s = mk_server();
    syn_output_t *dp2 = mk_output(s, "DP-2", 0,    0, 1920, 1080);
    syn_output_t *dp3 = mk_output(s, "DP-3", 1920, 0, 2560, 1440);

    syn_view_t *native = mk_view(s, 0, dp2, 100, 100, 800, 600, 1);
    /* On another desktop, so the sweep has to walk more than the visible one —
     * a screen going away orphans windows on every workspace. */
    syn_view_t *other_ws = mk_view(s, 4, dp3, 1920 + 10, 10, 400, 300, 1);

    CHECK(output_exile_take(s, dp3, dp2) == 1,
          "the window on desktop 5 was swept too");

    placements = 0;
    syn_output_t *dp3b = reconnect(s, dp3, "DP-3", 1920, 0, 2560, 1440);
    CHECK(output_exile_return(s, dp3b) == 1, "and it came back");
    CHECK(other_ws->output == dp3b, "onto DP-3");
    CHECK(native->output == dp2 && native->x == 100 && native->y == 100,
          "the DP-2 window was never touched (on %s at %d,%d)",
          out_name(native), native->x, native->y);
    CHECK(placements == 1, "exactly one window was placed, %d", placements);

    /* A returning screen nobody was exiled from is a no-op, which is what every
     * ordinary hotplug and every session start is. */
    syn_output_t *hdmi = mk_output(s, "HDMI-A-1", 4480, 0, 1920, 1080);
    CHECK(output_exile_return(s, hdmi) == 0,
          "a screen nobody was taken off claims nothing");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    test_standby_round_trip();
    test_returns_at_a_new_origin();
    test_cascade_keeps_the_first_home();
    test_tiled_window_gets_no_box();
    test_fullscreen_refits();
    test_a_deliberate_move_forgets();
    test_untouched_windows();

    if (failures) {
        printf("FAIL: %d checked, %d failed\n", checks, failures);
        return 1;
    }
    printf("PASS: %d checked, 0 failed\n", checks);
    return 0;
}
