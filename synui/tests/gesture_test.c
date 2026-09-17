/*
 * gesture_test.c — the claim and the direction, driven by hand-written event
 * streams in the order wlroots delivers them from libinput.
 *
 * The two ways this goes wrong on a real touchpad are both silent:
 *
 *   - A half-claimed gesture. If synui takes the begin and the client gets the
 *     update or the end (or the reverse), the app under the pointer is left
 *     holding a gesture that never finishes. Nothing crashes; a browser just
 *     stops responding to its own swipes. So every call's claim is asserted,
 *     not only the action that comes out.
 *   - A direction read backwards. libinput's dx is positive when the fingers
 *     move RIGHT, and a swipe left has to reach `ws next`. A sign flipped
 *     anywhere between here and the table is a feature that works, the wrong
 *     way round.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* syn_config_t is far too big for the stack. */
static syn_config_t *cfg;

static void add(int kind, int fingers, int dir, const char *action, const char *arg)
{
    syn_gesture_bind_t *g = &cfg->gesture_binds[cfg->gesture_count++];
    g->kind = kind;
    g->fingers = fingers;
    g->dir = dir;
    snprintf(g->action, sizeof(g->action), "%s", action);
    snprintf(g->arg, sizeof(g->arg), "%s", arg);
}

/* The table synui ships (config.c's seed_default_gestures), rebuilt by hand
 * so this test does not need config.c. settings_test asserts the real seed. */
static void reset_defaults(void)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->gestures = 1;
    add(SYN_GESTURE_SWIPE, 4, SYN_GESTURE_LEFT,  "ws", "next");
    add(SYN_GESTURE_SWIPE, 4, SYN_GESTURE_RIGHT, "ws", "prev");
    add(SYN_GESTURE_SWIPE, 4, SYN_GESTURE_UP,    "overview", "open");
    add(SYN_GESTURE_SWIPE, 4, SYN_GESTURE_DOWN,  "overview", "close");
}

static const char *bind_str(const syn_gesture_bind_t *g)
{
    static char buf[160];
    if (!g) return "(none)";
    snprintf(buf, sizeof(buf), "%s %s", g->action, g->arg);
    return buf;
}

/* A whole swipe, as a stream of small steps — a real one arrives as dozens of
 * updates a few units each, not one big delta. Returns the bind it ran, and
 * says through *claimed_all whether EVERY event was synui's. */
static const syn_gesture_bind_t *swipe(syn_gesture_tracker_t *t, int fingers,
                                       double dx, double dy, int steps,
                                       bool cancelled, bool *claimed_all)
{
    bool c = gesture_begin(t, cfg, SYN_GESTURE_SWIPE, (uint32_t)fingers, true);
    bool all = c, any = c;
    for (int i = 0; i < steps; i++) {
        bool u = gesture_update(t, SYN_GESTURE_SWIPE, dx / steps, dy / steps, 1.0);
        all = all && u;
        any = any || u;
    }
    const syn_gesture_bind_t *g = NULL;
    bool e = gesture_end(t, cfg, SYN_GESTURE_SWIPE, cancelled, &g);
    all = all && e;
    any = any || e;
    /* All or nothing — the half-claimed gesture is the bug. */
    CHECK(all == any, "a %d-finger swipe was claimed for some events and "
          "forwarded for others", fingers);
    if (claimed_all) *claimed_all = all;
    return g;
}

static bool is(const syn_gesture_bind_t *g, const char *action, const char *arg)
{
    return g && strcmp(g->action, action) == 0 && strcmp(g->arg, arg) == 0;
}

/* ── 1. The direction rule ───────────────────────────────── */
static void test_direction(void)
{
    CHECK(gesture_direction(SYN_GESTURE_SWIPE, -300, 0, 1) == SYN_GESTURE_LEFT,
          "fingers moving toward -x are a swipe LEFT");
    CHECK(gesture_direction(SYN_GESTURE_SWIPE, 300, 0, 1) == SYN_GESTURE_RIGHT,
          "fingers moving toward +x are a swipe RIGHT");
    CHECK(gesture_direction(SYN_GESTURE_SWIPE, 0, -300, 1) == SYN_GESTURE_UP,
          "libinput's y grows downward: -y is UP");
    CHECK(gesture_direction(SYN_GESTURE_SWIPE, 0, 300, 1) == SYN_GESTURE_DOWN,
          "+y is DOWN");

    /* Nobody swipes along an axis. The larger component wins. */
    CHECK(gesture_direction(SYN_GESTURE_SWIPE, -300, 180, 1) == SYN_GESTURE_LEFT,
          "a mostly-left diagonal is LEFT");
    CHECK(gesture_direction(SYN_GESTURE_SWIPE, 120, -250, 1) == SYN_GESTURE_UP,
          "a mostly-up diagonal is UP");
    /* sway's tie goes vertical; keep its rule rather than inventing one. */
    CHECK(gesture_direction(SYN_GESTURE_SWIPE, 100, 100, 1) == SYN_GESTURE_DOWN,
          "an exact diagonal falls to the vertical axis, as in sway");
    CHECK(gesture_direction(SYN_GESTURE_SWIPE, 0, 0, 1) == SYN_GESTURE_DIR_NONE,
          "a swipe that summed to nothing has no direction");

    CHECK(gesture_direction(SYN_GESTURE_PINCH, 0, 0, 0.5) == SYN_GESTURE_IN,
          "scale below 0.9 is a pinch IN");
    CHECK(gesture_direction(SYN_GESTURE_PINCH, 0, 0, 1.6) == SYN_GESTURE_OUT,
          "scale above 1.1 is a pinch OUT");
    CHECK(gesture_direction(SYN_GESTURE_PINCH, 0, 0, 1.05) == SYN_GESTURE_DIR_NONE,
          "a pinch that drifted 5%% is not a pinch");
    CHECK(gesture_direction(SYN_GESTURE_PINCH, 300, 0, 1.0) == SYN_GESTURE_DIR_NONE,
          "a pinch has no swipe directions");
}

/* ── 2. The defaults do what the docs say ────────────────── */
static void test_default_swipes(void)
{
    syn_gesture_tracker_t t = { 0 };
    bool claimed;
    reset_defaults();

    const syn_gesture_bind_t *g = swipe(&t, 4, -400, 35, 40, false, &claimed);
    CHECK(claimed && is(g, "ws", "next"),
          "four fingers left should be ws next, got %s", bind_str(g));

    g = swipe(&t, 4, 400, -20, 40, false, &claimed);
    CHECK(claimed && is(g, "ws", "prev"),
          "four fingers right should be ws prev, got %s", bind_str(g));

    g = swipe(&t, 4, 30, -350, 40, false, &claimed);
    CHECK(claimed && is(g, "overview", "open"),
          "four fingers up should open the overview, got %s", bind_str(g));

    g = swipe(&t, 4, -10, 350, 40, false, &claimed);
    CHECK(claimed && is(g, "overview", "close"),
          "four fingers down should close the overview, got %s", bind_str(g));

    CHECK(!t.active, "the tracker must be idle after a finished swipe");
}

/* ── 3. What synui does not bind is not synui's ──────────── */
static void test_unbound_reaches_the_app(void)
{
    syn_gesture_tracker_t t = { 0 };
    bool claimed;
    reset_defaults();

    const syn_gesture_bind_t *g = swipe(&t, 3, -400, 0, 40, false, &claimed);
    CHECK(!claimed && !g, "a three-finger swipe must go to the app when only "
          "four is bound");

    /* A pinch with nothing bound: apps zoom with it. */
    CHECK(!gesture_begin(&t, cfg, SYN_GESTURE_PINCH, 2, true),
          "a two-finger pinch must go to the app");
    CHECK(!gesture_update(&t, SYN_GESTURE_PINCH, 0, 0, 1.8),
          "…and so must its update");
    g = NULL;
    CHECK(!gesture_end(&t, cfg, SYN_GESTURE_PINCH, false, &g) && !g,
          "…and its end");
}

/* ── 4. The refusals ─────────────────────────────────────── */
static void test_may_not_claim(void)
{
    syn_gesture_tracker_t t = { 0 };
    const syn_gesture_bind_t *g = NULL;
    reset_defaults();

    /* input.c passes false on the lock screen, over the screensaver and
     * during a move/resize. The gesture then belongs to the client whole. */
    CHECK(!gesture_begin(&t, cfg, SYN_GESTURE_SWIPE, 4, false),
          "may_claim=false must forward the begin");
    CHECK(!gesture_update(&t, SYN_GESTURE_SWIPE, -400, 0, 1),
          "…and the update");
    CHECK(!gesture_end(&t, cfg, SYN_GESTURE_SWIPE, false, &g) && !g,
          "…and the end, running nothing");

    cfg->gestures = 0;
    bool claimed;
    g = swipe(&t, 4, -400, 0, 10, false, &claimed);
    CHECK(!claimed && !g, "gestures = off must hand four-finger swipes back "
          "to apps");
    cfg->gestures = 1;
}

/* ── 5. Cancelled, unbound direction, flipped off mid-swipe ─ */
static void test_claimed_but_nothing_runs(void)
{
    syn_gesture_tracker_t t = { 0 };
    bool claimed;
    reset_defaults();

    /* libinput cancels when a finger is added or lifted part-way. */
    const syn_gesture_bind_t *g = swipe(&t, 4, -400, 0, 20, true, &claimed);
    CHECK(claimed && !g, "a cancelled swipe stays synui's and runs nothing, "
          "got %s", bind_str(g));

    /* Only LEFT bound: the swipe is still taken whole, since the begin had to
     * be decided before anyone knew the direction. Right runs nothing. */
    memset(cfg, 0, sizeof(*cfg));
    cfg->gestures = 1;
    add(SYN_GESTURE_SWIPE, 4, SYN_GESTURE_LEFT, "ws", "next");
    g = swipe(&t, 4, 400, 0, 20, false, &claimed);
    CHECK(claimed && !g, "an unbound direction of a bound finger count is "
          "claimed and runs nothing, got %s", bind_str(g));

    /* The switch turned off between the fingers landing and lifting. */
    reset_defaults();
    CHECK(gesture_begin(&t, cfg, SYN_GESTURE_SWIPE, 4, true), "claimed");
    gesture_update(&t, SYN_GESTURE_SWIPE, -400, 0, 1);
    cfg->gestures = 0;
    g = NULL;
    CHECK(gesture_end(&t, cfg, SYN_GESTURE_SWIPE, false, &g) && !g,
          "a swipe finished after gestures went off must end claimed and run "
          "nothing");
}

/* ── 6. Nothing leaks from one gesture into the next ─────── */
static void test_no_leaks(void)
{
    syn_gesture_tracker_t t = { 0 };
    const syn_gesture_bind_t *g = NULL;
    reset_defaults();

    /* A begin with no end (touchpad unplugged mid-swipe), then a fresh short
     * swipe right. The 900 units of LEFT from the orphan must not count. */
    CHECK(gesture_begin(&t, cfg, SYN_GESTURE_SWIPE, 4, true), "claimed");
    gesture_update(&t, SYN_GESTURE_SWIPE, -900, 0, 1);
    CHECK(gesture_begin(&t, cfg, SYN_GESTURE_SWIPE, 4, true), "claimed again");
    gesture_update(&t, SYN_GESTURE_SWIPE, 40, 3, 1);
    CHECK(gesture_end(&t, cfg, SYN_GESTURE_SWIPE, false, &g) && is(g, "ws", "prev"),
          "a new begin must start from zero, got %s", bind_str(g));

    /* A claimed swipe followed by an unclaimed three-finger one: the second
     * must not inherit the first's claim. */
    CHECK(gesture_begin(&t, cfg, SYN_GESTURE_SWIPE, 4, true), "claimed");
    CHECK(!gesture_begin(&t, cfg, SYN_GESTURE_SWIPE, 3, true),
          "a three-finger begin replaces the orphaned claim and is forwarded");
    CHECK(!gesture_update(&t, SYN_GESTURE_SWIPE, -400, 0, 1),
          "…so its updates are forwarded too");

    /* A pinch event while a swipe is claimed is not the swipe's. */
    reset_defaults();
    CHECK(gesture_begin(&t, cfg, SYN_GESTURE_SWIPE, 4, true), "claimed");
    CHECK(!gesture_update(&t, SYN_GESTURE_PINCH, 0, 0, 0.5),
          "a pinch update must not feed a claimed swipe");
    CHECK(!gesture_end(&t, cfg, SYN_GESTURE_PINCH, false, &g),
          "a pinch end must not end a claimed swipe");
    CHECK(t.active, "the swipe is still in flight");
    gesture_update(&t, SYN_GESTURE_SWIPE, 0, -300, 1);
    CHECK(gesture_end(&t, cfg, SYN_GESTURE_SWIPE, false, &g) &&
          is(g, "overview", "open"), "the swipe still ends as itself, got %s",
          bind_str(g));

    /* After the end, stray updates belong to nobody synui knows about. */
    CHECK(!gesture_update(&t, SYN_GESTURE_SWIPE, -400, 0, 1),
          "an update after the end is not claimed");
}

/* ── 7. A pinch reads libinput's ABSOLUTE scale ──────────── */
static void test_pinch(void)
{
    syn_gesture_tracker_t t = { 0 };
    const syn_gesture_bind_t *g = NULL;
    memset(cfg, 0, sizeof(*cfg));
    cfg->gestures = 1;
    add(SYN_GESTURE_PINCH, 4, SYN_GESTURE_IN,  "overview", "open");
    add(SYN_GESTURE_PINCH, 4, SYN_GESTURE_OUT, "overview", "close");

    /* 0.95, 0.9, 0.85, 0.8 — each is the scale since the begin. Multiplying
     * them (0.58) would still read IN, so the check that matters is the one
     * below: a pinch that comes back to 0.97 is NOT a pinch. */
    CHECK(gesture_begin(&t, cfg, SYN_GESTURE_PINCH, 4, true), "claimed");
    for (double sc = 0.95; sc > 0.79; sc -= 0.05)
        CHECK(gesture_update(&t, SYN_GESTURE_PINCH, 0, 0, sc), "claimed update");
    CHECK(gesture_end(&t, cfg, SYN_GESTURE_PINCH, false, &g) &&
          is(g, "overview", "open"), "four fingers pinched in, got %s", bind_str(g));

    CHECK(gesture_begin(&t, cfg, SYN_GESTURE_PINCH, 4, true), "claimed");
    gesture_update(&t, SYN_GESTURE_PINCH, 0, 0, 0.8);
    gesture_update(&t, SYN_GESTURE_PINCH, 0, 0, 0.9);
    gesture_update(&t, SYN_GESTURE_PINCH, 0, 0, 0.97);
    g = NULL;
    CHECK(gesture_end(&t, cfg, SYN_GESTURE_PINCH, false, &g) && !g,
          "a pinch that returned to 0.97 has no direction — the scale is "
          "absolute, not a product of steps; got %s", bind_str(g));

    CHECK(!gesture_begin(&t, cfg, SYN_GESTURE_SWIPE, 4, true),
          "binding pinches must not take four-finger swipes");
}

int main(void)
{
    cfg = calloc(1, sizeof(*cfg));
    if (!cfg) return 2;

    test_direction();
    test_default_swipes();
    test_unbound_reaches_the_app();
    test_may_not_claim();
    test_claimed_but_nothing_runs();
    test_no_leaks();
    test_pinch();

    free(cfg);
    if (failures) {
        fprintf(stderr, "gesture_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("gesture_test: ok\n");
    return 0;
}
