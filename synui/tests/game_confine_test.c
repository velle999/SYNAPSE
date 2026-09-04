/*
 * game_confine_test.c — game mode holds the pointer on the game's screen.
 *
 * The bug this exists for was not a broken confinement, it was the ABSENCE of
 * one. Measured 2026-08-26 on Cyberpunk 2077 (steam_app_1091500) under
 * proton-cachyos across three outputs: with the game holding focus for 59
 * seconds, the cursor ranged over x 0..3639, y 75..2602 — all three monitors.
 * Wine/Xwayland never asks for a zwp_locked_pointer, so constraints.c has
 * nothing to honour, and game mode answers for itself instead.
 *
 * Two things can be wrong here and neither shows up as a crash:
 *
 *   1. THE GATE. Confining when we should not is a mouse the user cannot get
 *      off one monitor. Every "no" below is a real escape hatch, and the one
 *      that matters most is focus: Alt-Tab has to free the pointer.
 *   2. THE EDGE. A cursor clamped to box.x + box.width is already on the
 *      NEXT output — the first column of an output is its origin, so the last
 *      one it owns is width - 1. Off by one here and the confinement leaks
 *      one pixel a time, which is exactly what it was built to stop.
 *
 * Drives the real game_pointer_box()/game_confine_cursor() out of game.c; the
 * dozen compositor symbols it reaches for are stubbed below.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "synui.h"

static int fails;

/* ── The compositor half ───────────────────────────────────── */

static struct wlr_box stub_out_box = { .x = 1080, .y = 1080,
                                       .width = 2560, .height = 1440 };
static const char *stub_app_id = "steam_app_1091500";

void output_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box)
{
    (void)s; (void)o; *box = stub_out_box;
}
const char *view_app_id(syn_view_t *v) { (void)v; return stub_app_id; }

/* The cursor never really moves here; warping IS the observable effect. */
void wlr_cursor_warp_closest(struct wlr_cursor *cur, struct wlr_input_device *dev,
                             double x, double y)
{
    (void)dev; cur->x = x; cur->y = y;
}

syn_output_t *server_focused_output(syn_server_t *s) { (void)s; return NULL; }
syn_output_t *server_primary_output(syn_server_t *s) { (void)s; return NULL; }
void power_notify_activity(syn_server_t *s) { (void)s; }
void synmon_start(syn_server_t *s) { (void)s; }
void synmon_stop(syn_server_t *s)  { (void)s; }
void synui_spawn(const char *cmd)  { (void)cmd; }
/* Game mode tells synapd the GPU is wanted instead of stopping the daemon.
 * -1 is the "no synapd answered" answer, which is the right one for a test rig
 * with no socket — and it keeps this suite from depending on whether a real
 * synapd happens to be running on the machine building synui. */
int  ai_release_model(void)        { return -1; }
int  ai_resume_model(void)         { return -1; }
void wlr_output_schedule_frame(struct wlr_output *o) { (void)o; }

/* The rectangle the game is DRAWN in. Stubbed so a test can state a letterbox
 * outright rather than building a scene graph to imply one; a zero box is the
 * "nothing measurable" answer the real one gives for a client whose tree
 * carries more than one buffer. */
static struct wlr_box stub_content;

int view_scaled_content_box(syn_view_t *v, struct wlr_box *out)
{
    (void)v;
    if (stub_content.width <= 0 || stub_content.height <= 0) return 0;
    *out = stub_content;
    return 1;
}

/* ── Harness ───────────────────────────────────────────────── */

static syn_server_t   srv;
static syn_view_t     game;
static syn_output_t   out;
static struct wlr_cursor cursor;

/* A server with one mapped fullscreen X11 game, focused, game mode engaged —
 * i.e. every gate open. Each test then closes exactly one. */
static void reset(void)
{
    memset(&srv, 0, sizeof srv);
    memset(&game, 0, sizeof game);
    memset(&out, 0, sizeof out);
    memset(&cursor, 0, sizeof cursor);
    stub_content = (struct wlr_box){ 0, 0, 0, 0 };

    for (int i = 0; i < WORKSPACE_MAX; i++)
        wl_list_init(&srv.workspaces[i].windows);
    wl_list_init(&srv.xw_views);

    game.mapped      = 1;
    game.fullscreen  = 1;
    game.is_xwayland = 1;
    game.output      = &out;
    wl_list_insert(&srv.workspaces[0].windows, &game.link);

    srv.cursor       = &cursor;
    srv.focused_view = &game;
    srv.game.active  = 1;
    srv.config.game_mode            = 1;
    srv.config.game_confine_pointer = 1;
    stub_app_id = "steam_app_1091500";
}

/* The self-minimise lookup: is a game that unmapped itself still a game?
 * `srv.xw_views` is the list an unmapped X11 view is still on, so the harness
 * puts the view on it the way xw_map() would. */
static void mini(const char *what, int want)
{
    syn_view_t *got = game_minimized_view(&srv);
    if (!!got == !!want) {
        printf("  ok    %s (%s)\n", what, got ? "still a game" : "gone");
    } else {
        printf("  FAIL  %s — expected %s, got %s\n", what,
               want ? "still a game" : "gone", got ? "still a game" : "gone");
        fails++;
    }
}

static void gate(const char *what, int want)
{
    struct wlr_box b;
    int got = game_pointer_box(&srv, &b);
    if (!!got == !!want) {
        printf("  ok    %s (%s)\n", what, got ? "confined" : "free");
    } else {
        printf("  FAIL  %s — expected %s, got %s\n", what,
               want ? "confined" : "free", got ? "confined" : "free");
        fails++;
    }
}

/* game_owns_output() asks a DIFFERENT question to game_pointer_box() and the
 * difference is focus — see the comment on it in game.c. Worth its own check
 * because the two read almost identically at the call site. */
static void owns(const char *what, syn_output_t *o, int want)
{
    int got = game_owns_output(&srv, o);
    if (!!got == !!want) {
        printf("  ok    %s (%s)\n", what, got ? "covered" : "not covered");
    } else {
        printf("  FAIL  %s — expected %s, got %s\n", what,
               want ? "covered" : "not covered", got ? "covered" : "not covered");
        fails++;
    }
}

static void lands(const char *what, double from_x, double from_y,
                  double want_x, double want_y)
{
    cursor.x = from_x; cursor.y = from_y;
    game_confine_cursor(&srv);
    if (fabs(cursor.x - want_x) < 0.01 && fabs(cursor.y - want_y) < 0.01) {
        printf("  ok    %s (%.1f,%.1f)\n", what, cursor.x, cursor.y);
    } else {
        printf("  FAIL  %s — expected (%.1f,%.1f), got (%.1f,%.1f)\n",
               what, want_x, want_y, cursor.x, cursor.y);
        fails++;
    }
}

/* game_confine_rect() is the geometry on its own: which rectangle holds the
 * pointer, given the screen and whatever the game is actually drawn in. */
static void rect(const char *what, struct wlr_box content,
                 int wx, int wy, int ww, int wh)
{
    struct wlr_box o = { 1080, 1080, 2560, 1440 };
    struct wlr_box got;
    if (!game_confine_rect(&o, &content, &got)) {
        printf("  FAIL  %s — declined outright\n", what);
        fails++;
        return;
    }
    if (got.x == wx && got.y == wy && got.width == ww && got.height == wh) {
        printf("  ok    %s (%d,%d %dx%d)\n", what,
               got.x, got.y, got.width, got.height);
    } else {
        printf("  FAIL  %s — expected (%d,%d %dx%d), got (%d,%d %dx%d)\n",
               what, wx, wy, ww, wh, got.x, got.y, got.width, got.height);
        fails++;
    }
}


/* Which point of the picture answers for a point in the box — and whether the
 * window answers for it AT ALL, which is the whole regression below. */
static void owns_pt(const char *what, struct wlr_box box, struct wlr_box content,
                    double lx, double ly, int want, double wx, double wy)
{
    double cx = -1, cy = -1;
    int got = game_fullscreen_owns_point(&box, &content, lx, ly, &cx, &cy);
    int ok = (got == want) && (!want || (cx == wx && cy == wy));
    printf("  %s %-58s", ok ? "ok  " : "FAIL", what);
    if (ok) printf("\n");
    else    printf(" got %d (%.0f,%.0f) want %d (%.0f,%.0f)\n",
                   got, cx, cy, want, wx, wy);
    if (!ok) fails++;
}

int main(void)
{
    printf("game_confine_test\n\n THE GATE\n");

    reset();                                  gate("a focused fullscreen game", 1);
    reset(); srv.focused_view = NULL;         gate("game lost focus (Alt-Tab)", 0);
    reset(); srv.game.active = 0;             gate("game mode not engaged", 0);
    reset(); srv.config.game_confine_pointer = 0;
                                              gate("setting turned off", 0);
    reset(); srv.config.game_mode = 0;        gate("game mode master switch off", 0);
    reset(); game.fullscreen = 0;             gate("game left fullscreen", 0);
    reset(); game.mapped = 0;                 gate("game unmapped", 0);
    reset(); game.output = NULL;              gate("game has no output yet", 0);
    reset(); stub_app_id = "firefox";
             srv.config.game_exclude_count = 1;
             snprintf(srv.config.game_exclude[0],
                      sizeof srv.config.game_exclude[0], "firefox");
                                              gate("an excluded app_id", 0);

    printf("\n THE EDGE  (output x 1080..3639, y 1080..2519)\n");

    reset();
    lands("inside is left alone",        2000, 1800, 2000, 1800);
    lands("off the LEFT edge comes back", 900, 1800, 1080, 1800);
    lands("off the RIGHT edge comes back", 4000, 1800, 3639, 1800);
    lands("off the TOP edge comes back",  2000,  200, 2000, 1080);
    lands("off the BOTTOM edge comes back", 2000, 3000, 2000, 2519);
    lands("a corner clamps both axes",     0,     0,  1080, 1080);
    /* The off-by-one that would leak the pointer one pixel at a time. */
    lands("the last column it owns holds", 3639, 2519, 3639, 2519);
    lands("one past the last column does not",
                                           3640, 2520, 3639, 2519);

    /* An unfocused game must not have its cursor moved at all. */
    reset(); srv.focused_view = NULL;
    lands("unfocused: cursor untouched",   9999, 9999, 9999, 9999);

    printf("\n A GAME THAT MINIMISED ITSELF  (Alt-Tab out of an X11 game)\n");

    /* An exclusive-fullscreen X11 title unmaps its window on every focus loss.
     * The view leaves its workspace list with it, so without this the grace
     * timer runs out and game mode tears the desktop down and rebuilds it —
     * on every Alt-Tab. */
    reset(); game.mapped = 0;
             game.xsurface = (struct wlr_xwayland_surface *)&game;
             wl_list_insert(&srv.xw_views, &game.xw_link);
                                              mini("unmapped, client alive", 1);

    /* ⚠ Never an ENTRY condition. A window nobody can see must not turn game
     * mode on — only keep it on. */
    reset(); game.mapped = 0; srv.game.active = 0;
             game.xsurface = (struct wlr_xwayland_surface *)&game;
             wl_list_insert(&srv.xw_views, &game.xw_link);
                                              mini("game mode not engaged", 0);

    /* A game that really QUIT: xw_destroy takes the view off xw_views and asks
     * again, and the surface is gone before that. */
    reset(); game.mapped = 0; game.xsurface = NULL;
             wl_list_insert(&srv.xw_views, &game.xw_link);
                                              mini("window destroyed", 0);

    reset(); game.mapped = 0; game.fullscreen = 0;
             game.xsurface = (struct wlr_xwayland_surface *)&game;
             wl_list_insert(&srv.xw_views, &game.xw_link);
                                              mini("not fullscreen", 0);

    reset(); game.mapped = 0;
             game.xsurface = (struct wlr_xwayland_surface *)&game;
             game.override_redirect = 1;
             wl_list_insert(&srv.xw_views, &game.xw_link);
                                              mini("override-redirect", 0);

    reset(); game.mapped = 0;
             game.xsurface = (struct wlr_xwayland_surface *)&game;
             stub_app_id = "firefox";
             srv.config.game_exclude_count = 1;
             snprintf(srv.config.game_exclude[0],
                      sizeof srv.config.game_exclude[0], "firefox");
             wl_list_insert(&srv.xw_views, &game.xw_link);
                                              mini("an excluded app_id", 0);

    /* A mapped game is the normal path's business, not this one's. */
    reset(); game.xsurface = (struct wlr_xwayland_surface *)&game;
             wl_list_insert(&srv.xw_views, &game.xw_link);
                                              mini("still mapped", 0);

    printf("\n A LETTERBOX HOLDS THE POINTER, NOT THE SCREEN\n");

    /* Measured on Cyberpunk 2077, 2026-08-26: the fit produced 2560x1438
     * inside a 2560x1440 screen — a ONE PIXEL bar top and bottom. The cursor
     * sits on the bottom edge of the picture constantly, because that is what
     * looking down does, and one step onto the bar costs pointer focus. */
    reset();
    stub_content = (struct wlr_box){ 1080, 1081, 2560, 1438 };
    lands("the bottom bar is out of reach", 2000, 3000, 2000, 2518);
    lands("the top bar is out of reach",    2000,  200, 2000, 1081);
    lands("the sides are still the screen's", 4000, 1800, 3639, 1800);
    lands("inside the picture is left alone", 2000, 1800, 2000, 1800);

    /* A picture wider than the screen is the client's own dest size, not ours;
     * the confine may never follow it off the monitor. */
    reset();
    stub_content = (struct wlr_box){ 1000, 1000, 3000, 1600 };
    lands("a picture overhanging the screen still clamps to it",
                                            4000, 3000, 3639, 2519);

    printf("\n WHICH SCREEN A GAME COVERS  (barscan skips it)\n");

    static syn_output_t other;
    reset();
    owns("the game's own output", &out, 1);
    owns("another output", &other, 0);
    owns("no output at all", NULL, 0);

    /* THE distinction: a tabbed-away game still COVERS its screen, even though
     * it has released the pointer. barscan must keep skipping it; the pointer
     * must come back. Getting these two the same way round is the bug this
     * pair of assertions exists to catch. */
    reset(); srv.focused_view = NULL;
    owns("unfocused game still covers its screen", &out, 1);
    gate("unfocused game has released the pointer", 0);

    reset(); srv.game.active = 0;
    owns("game mode not engaged", &out, 0);
    reset(); srv.config.game_mode = 0;
    owns("master switch off", &out, 0);
    /* The confine SETTING must not reach barscan: turning off "keep the mouse
     * on the game's screen" is about the pointer, not about what is on screen. */
    reset(); srv.config.game_confine_pointer = 0;
    owns("confine setting off still covers the screen", &out, 1);

    printf("\n THE LETTERBOX  (the surface, not the screen)\n");

    /* Nothing measurable: the old behaviour, and the only safe default. */
    rect("no content box falls back to the screen",
         (struct wlr_box){ 0, 0, 0, 0 },            1080, 1080, 2560, 1440);
    /* A game that fills its output is the case where both answers agree. */
    rect("a game filling its screen is unchanged",
         (struct wlr_box){ 1080, 1080, 2560, 1440 }, 1080, 1080, 2560, 1440);

    /* THE BUG. A sub-native game centred by view_fullscreen_rescale leaves a
     * bar top and bottom; the pointer must not be able to reach one, because
     * there is no surface there and losing pointer focus destroys a oneshot
     * lock outright — mouse-look dead for the session. */
    rect("a letterboxed game confines to its own picture",
         (struct wlr_box){ 1080, 1092, 2560, 1416 }, 1080, 1092, 2560, 1416);
    rect("pillarboxed the same way",
         (struct wlr_box){ 1160, 1080, 2400, 1440 }, 1160, 1080, 2400, 1440);

    /* A client picks its own buffer dest size. The escape hatch is worth
     * nothing if the confine can follow one off the screen. */
    rect("content larger than the screen is clipped to it",
         (struct wlr_box){ 1000, 1000, 3000, 1600 }, 1080, 1080, 2560, 1440);
    rect("content entirely off the screen falls back",
         (struct wlr_box){ 100, 100, 200, 200 },     1080, 1080, 2560, 1440);

    printf("\n WHO OWNS THE PIXEL  (the box, not the picture)\n");

    /* THE REGRESSION, measured 2026-08-26 on Cyberpunk 2077 filling DP-3.
     * The picture covers the frame exactly, so the old code declined the point
     * on the reasoning that the scene walk must already have answered for it.
     * It had not: pointer focus dropped to NULL on x 3639 and y 2519 — the
     * output's last column and last row, which are the two lines the confine
     * parks the cursor on — and each drop cost the game its pointer lock. */
    {
        struct wlr_box full = { 1080, 1080, 2560, 1440 };
        owns_pt("the last column of a filling picture is the window's",
                full, full, 3639, 1700, 1, 3639, 1700);
        owns_pt("the last row of a filling picture is the window's",
                full, full, 2000, 2519, 1, 2000, 2519);
        owns_pt("so is the far corner",
                full, full, 3639, 2519, 1, 3639, 2519);
        owns_pt("and the near corner, which never broke",
                full, full, 1080, 1080, 1, 1080, 1080);
        /* Ownership stops at the box: the next output is not ours to answer for. */
        owns_pt("one past the last column belongs to nobody here",
                full, full, 3640, 1700, 0, 0, 0);
        owns_pt("one past the last row likewise",
                full, full, 2000, 2520, 0, 0, 0);

        /* A letterboxed picture still answers with the nearest painted point —
         * the behaviour 513 added, which this must not have cost. */
        struct wlr_box bar = { 1080, 1092, 2560, 1416 };
        owns_pt("a point in the top bar answers at the picture's top row",
                full, bar, 2000, 1080, 1, 2000, 1092);
        owns_pt("a point in the bottom bar answers at its last row",
                full, bar, 2000, 2519, 1, 2000, 2507);
        owns_pt("a pillarbox answers at the picture's edge column",
                full, (struct wlr_box){ 1160, 1080, 2400, 1440 },
                1080, 1700, 1, 1160, 1700);
    }

    printf("\n%s (%d failed)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
