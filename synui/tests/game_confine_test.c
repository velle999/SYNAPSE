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
void wlr_output_schedule_frame(struct wlr_output *o) { (void)o; }

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

    for (int i = 0; i < WORKSPACE_MAX; i++)
        wl_list_init(&srv.workspaces[i].windows);

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

    printf("\n%s (%d failed)\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
