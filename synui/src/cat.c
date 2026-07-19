/*
 * cat.c — cat mode
 *
 * A small procedurally drawn kitty that wanders across the outputs, on top of
 * whatever happens to be in the way. Super+Shift+C toggles it.
 *
 * Everything here is cairo: there is no sprite sheet, no asset to ship, and no
 * image to decode. The cat is redrawn every frame from its current pose (walk
 * phase, tail sway, ear twitch, blink), so the "animation" is just a handful of
 * sines evaluated against the clock.
 *
 * Two properties matter and are easy to get wrong:
 *
 *   - It must never eat input. The scene buffer sets point_accepts_input to a
 *     function that always says no, so wlr_scene_node_at() looks straight
 *     through the cat to the window underneath. Without it the kitty would be a
 *     roaming dead zone that swallows clicks.
 *
 *   - It lives in its own top-level scene tree in LAYOUT coordinates, not under
 *     any output's tree, which is what lets it walk from one monitor to the next
 *     without anything special happening at the seam.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <cairo.h>

#include <wlr/types/wlr_scene.h>

#include "synui.h"

#define CAT_SPEED      70.0    /* px/sec while walking */
#define CAT_ARRIVE     6.0     /* px; close enough to the target */
#define CAT_EDGE_PAD   24      /* keep this far inside an output's edges */

static double cat_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static double frand(void) { return (double)rand() / (double)RAND_MAX; }

/* ── Wandering ───────────────────────────────────────────── */

/* Pick a point on a randomly chosen output. Sampling an output first (rather
 * than a point in the layout's bounding box) means a gap between differently
 * sized monitors can never be chosen as a destination — the cat would walk into
 * the void and appear stuck. */
static bool cat_pick_target(syn_server_t *s, double *tx, double *ty)
{
    int n = wl_list_length(&s->outputs);
    if (n <= 0) return false;

    int pick = rand() % n;
    syn_output_t *o, *chosen = NULL;
    int i = 0;
    wl_list_for_each(o, &s->outputs, link) {
        if (i++ == pick) { chosen = o; break; }
    }
    if (!chosen) return false;

    struct wlr_box b;
    output_box_of(s, chosen, &b);

    int pad = CAT_EDGE_PAD;
    if (b.width  < 4 * pad || b.height < 4 * pad) pad = 0;   /* tiny output */

    *tx = b.x + pad + frand() * (b.width  - 2 * pad);
    *ty = b.y + pad + frand() * (b.height - 2 * pad);
    return true;
}

static void cat_choose_next(syn_server_t *s, double now)
{
    /* Cats do not march from waypoint to waypoint. Most arrivals are followed by
     * a sit, some by a nap, and now and then it just carries on walking. */
    double r = frand();
    if (r < 0.55) {
        s->cat.state = CAT_SIT;
        s->cat.state_until = now + 1.5 + frand() * 4.0;
    } else if (r < 0.70) {
        s->cat.state = CAT_SLEEP;
        s->cat.state_until = now + 5.0 + frand() * 10.0;
    } else {
        s->cat.state = CAT_WALK;
        s->cat.state_until = 0;
        cat_pick_target(s, &s->cat.tx, &s->cat.ty);
    }
}

static void cat_advance(syn_server_t *s, double dt, double now)
{
    switch (s->cat.state) {
    case CAT_SIT:
    case CAT_SLEEP:
        if (now >= s->cat.state_until) {
            s->cat.state = CAT_WALK;
            cat_pick_target(s, &s->cat.tx, &s->cat.ty);
        }
        break;

    case CAT_WALK: {
        double dx = s->cat.tx - s->cat.x;
        double dy = s->cat.ty - s->cat.y;
        double d  = sqrt(dx * dx + dy * dy);

        if (d < CAT_ARRIVE) {
            cat_choose_next(s, now);
            break;
        }

        double step = CAT_SPEED * dt;
        if (step > d) step = d;
        s->cat.x += dx / d * step;
        s->cat.y += dy / d * step;

        /* Only commit to a new facing on real horizontal movement, or the cat
         * flickers back and forth while walking almost straight up or down. */
        if (fabs(dx) > 2.0)
            s->cat.facing = dx > 0 ? 1 : -1;

        s->cat.phase += step / 9.0;   /* stride length, in px per half-cycle */
        break;
    }
    }

    /* Blink on a slow random schedule, in any state except sleeping. */
    if (s->cat.state != CAT_SLEEP && now >= s->cat.blink_until) {
        if (frand() < 0.008) s->cat.blink_until = now + 0.12;
        else                 s->cat.blink_until = 0;
    }
}

/* The cat is decoration: it must never be the thing a click lands on. */
static bool cat_no_input(struct wlr_scene_buffer *buf, double *sx, double *sy)
{
    (void)buf; (void)sx; (void)sy;
    return false;
}

static void cat_render(syn_server_t *s, double now)
{
    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(CAT_W, CAT_H, &cr);
    if (!buf) return;

    cairo_begin(cr);

    /* Mirror about the canvas centre when heading left, so one drawing serves
     * both directions. */
    if (s->cat.facing < 0) {
        cairo_translate(cr, CAT_W, 0);
        cairo_scale(cr, -1.0, 1.0);
    }
    cat_pose_t pose = {
        .state    = s->cat.state,
        .phase    = s->cat.phase,
        .now      = now,
        .blinking = s->cat.blink_until > now,
    };
    cat_paint(cr, &pose);
    cairo_destroy(cr);

    set_scene_buffer(&s->cat.buf, s->cat.tree, buf);
    if (s->cat.buf)
        s->cat.buf->point_accepts_input = cat_no_input;
}

/* ── Public ──────────────────────────────────────────────── */

bool cat_tick(syn_output_t *o, double now)
{
    syn_server_t *s = o->server;
    if (!s->cat.enabled || !s->cat.tree) return false;

    /* Stand down while the screen is blanked or locked. Not cosmetic: cat_tick
     * asks for a frame every frame, so an unattended machine would otherwise sit
     * at the refresh rate redrawing a kitty nobody can see — behind the lock
     * screen, no less. The idle timers are unaffected either way (we never call
     * power_notify_activity), so this only stops the pointless repainting. */
    if (s->power.blanked || s->power.locked) {
        wlr_scene_node_set_enabled(&s->cat.tree->node, false);
        return false;
    }
    if (!s->cat.tree->node.enabled)
        wlr_scene_node_set_enabled(&s->cat.tree->node, true);

    /* output_frame runs once per output, but the cat is one animal in one
     * layout. Let whichever output gets the first frame of this instant own the
     * simulation step; the others just draw what it produced. */
    if (now > s->cat.last_t) {
        double dt = now - s->cat.last_t;
        if (dt > 0.05) dt = 0.05;        /* a stall must not teleport the cat */
        s->cat.last_t = now;

        cat_advance(s, dt, now);
        cat_render(s, now);

        wlr_scene_node_set_position(&s->cat.tree->node,
                                    (int)(s->cat.x - CAT_W / 2.0),
                                    (int)(s->cat.y - CAT_H + 6));

        /* Stay above whatever has been raised since the last frame — a window
         * that just mapped, the dock popping up, a panel opening. */
        wlr_scene_node_raise_to_top(&s->cat.tree->node);
    }

    /* Keep frames coming only on the outputs the cat is actually over. If it is
     * over none of them (an output vanished mid-stride), fall back to asking
     * everyone, or the animation would stall with no one left to drive it. */
    struct wlr_box ob;
    output_box_of(s, o, &ob);
    struct wlr_box cb = {
        .x = (int)(s->cat.x - CAT_W / 2.0),
        .y = (int)(s->cat.y - CAT_H + 6),
        .width = CAT_W, .height = CAT_H,
    };
    bool over_this = wlr_box_intersection(&(struct wlr_box){0}, &ob, &cb);

    if (over_this) return true;

    syn_output_t *other;
    wl_list_for_each(other, &s->outputs, link) {
        struct wlr_box b;
        output_box_of(s, other, &b);
        if (wlr_box_intersection(&(struct wlr_box){0}, &b, &cb))
            return false;      /* someone else has it — they will drive us */
    }
    return true;               /* nobody does; keep this output ticking */
}

void cat_toggle(syn_server_t *s)
{
    s->cat.enabled = !s->cat.enabled;

    if (!s->cat.enabled) {
        if (s->cat.tree)
            wlr_scene_node_set_enabled(&s->cat.tree->node, false);
        wlr_log(WLR_INFO, "synui: cat mode off");
        return;
    }

    if (!s->cat.tree) {
        s->cat.tree = wlr_scene_tree_create(&s->scene->tree);
        if (!s->cat.tree) { s->cat.enabled = 0; return; }
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
    }

    /* Start on the focused output rather than at the layout origin, which on a
     * multi-head setup is often a monitor you are not looking at. */
    struct wlr_box b;
    output_box_of(s, server_focused_output(s), &b);
    s->cat.x = b.x + b.width  / 2.0;
    s->cat.y = b.y + b.height / 2.0;
    s->cat.facing = 1;
    s->cat.state = CAT_WALK;
    s->cat.phase = 0;
    s->cat.blink_until = 0;
    s->cat.last_t = cat_now();
    cat_pick_target(s, &s->cat.tx, &s->cat.ty);

    wlr_scene_node_set_enabled(&s->cat.tree->node, true);
    wlr_scene_node_raise_to_top(&s->cat.tree->node);
    wlr_log(WLR_INFO, "synui: cat mode on");

    /* Kick the first frame; after that cat_tick keeps them coming. */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        wlr_output_schedule_frame(o->wlr_output);
}
