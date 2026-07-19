/*
 * anim.c — window animations
 *
 * The Hyprland-style polish, on a wlr_scene compositor. wlr_scene gives us
 * exactly one lever — per-buffer opacity (wlr_scene_buffer_set_opacity) — and
 * no per-node shader hook, so what's achievable here is *fades*, done properly:
 *
 *   - a window fades in when it opens, instead of snapping into existence;
 *   - switching desktop cross-fades: the outgoing windows fade out (and are
 *     only disabled once they're actually invisible), the incoming ones fade in.
 *
 * Geometry animation (a window gliding to its tiled slot) is deliberately NOT
 * here. Animating a window's *size* means re-configuring the client every frame
 * — a resize storm the client has to keep up with, which is exactly why
 * Hyprland animates a scaled snapshot of the buffer instead. That needs render
 * control wlr_scene doesn't expose. Position-only animation would work, but a
 * tiling reflow almost always changes size too, so it would only ever apply to
 * the cases nobody notices. Fades are the honest subset.
 *
 * Everything runs off the per-output frame tick (like dock_tick / cat_tick):
 * anim_tick() advances every running fade and returns true while any is still
 * going, so the frame loop keeps pumping until the animation settles.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <time.h>

#include <wlr/types/wlr_scene.h>

#include "synui.h"

/* Ease-out cubic: fast at the start, settling at the end. Movement that decays
 * reads as physical; a linear fade reads as a slideshow. */
static float ease_out(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

static double now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ── Applying alpha to a whole window ────────────────────── */
static void set_buffer_opacity(struct wlr_scene_buffer *buffer,
                               int sx, int sy, void *data)
{
    (void)sx; (void)sy;
    const float *alpha = data;
    wlr_scene_buffer_set_opacity(buffer, *alpha);
}

/*
 * The window's *settled* translucency, driven by focus. This is the theme's
 * transparency lever, orthogonal to the fade in flight (view->alpha): the two
 * are multiplied at apply time so a translucent window still fades cleanly.
 * 1.0 (opaque) whenever transparency is off, so the whole feature costs nothing
 * until someone turns it on.
 */
float anim_view_opacity(syn_view_t *view)
{
    syn_server_t *s = view->server;
    if (!s || !s->config.transparency) return 1.0f;
    float o = (view == s->focused_view) ? s->config.active_opacity
                                        : s->config.inactive_opacity;
    if (o < 0.1f) o = 0.1f;   /* never let a window vanish entirely */
    if (o > 1.0f) o = 1.0f;
    return o;
}

/*
 * A window is a frame tree: the client's surface(s) plus the titlebar buffer
 * plus four border rects. for_each_buffer covers the surfaces and the titlebar;
 * the borders are scene *rects*, which carry their alpha in the colour, so
 * view_update_decorations() multiplies them by the effective alpha itself.
 *
 * Effective alpha = the fade value × the focus-driven base translucency, so one
 * pass handles both a window that is fading in AND one made see-through.
 */
void anim_apply_alpha(syn_view_t *view)
{
    if (!view->mapped) return;

    float a = view->alpha * anim_view_opacity(view);
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;

    wlr_scene_node_for_each_buffer(view_node(view), set_buffer_opacity, &a);
    view_update_decorations(view);   /* re-tints the border rects at `a` */
}

/* Re-push opacity to every mapped window. Called after the transparency master
 * switch flips or a theme moves the active/inactive levels — nothing else would
 * repaint the windows that are not the focused one. */
void anim_apply_alpha_all(syn_server_t *s)
{
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link)
            if (v->mapped)
                anim_apply_alpha(v);
    }
}

/* ── Starting a fade ─────────────────────────────────────── */
static void fade_to(syn_view_t *view, float to, bool hide_when_done)
{
    syn_server_t *s = view->server;

    if (!view->mapped) return;

    /* Animations off, or a zero duration: jump straight to the end state. The
     * rest of the compositor must not have to care which mode it's in. */
    if (s->config.animation_ms <= 0) {
        view->alpha = to;
        view->fade_active = 0;
        anim_apply_alpha(view);
        if (hide_when_done && to <= 0.0f)
            wlr_scene_node_set_enabled(view_node(view), false);
        return;
    }

    view->fade_from      = view->alpha;
    view->fade_to        = to;
    view->fade_start     = now_secs();
    view->fade_active    = 1;
    view->fade_hide_done = hide_when_done ? 1 : 0;
}

void anim_fade_in(syn_view_t *view)
{
    if (!view->mapped || view->minimized) return;
    view->alpha = 0.0f;
    anim_apply_alpha(view);
    fade_to(view, 1.0f, false);
}

/* Fade the window out and only then disable its node — a window that vanishes
 * the instant the desktop changes is the snap we're trying to get rid of. */
void anim_fade_out_and_hide(syn_view_t *view)
{
    if (!view->mapped) {
        wlr_scene_node_set_enabled(view_node(view), false);
        return;
    }
    fade_to(view, 0.0f, true);
}

/* A window that was faded out and is being shown again (desktop switch back,
 * un-minimize) must start from wherever its alpha was left, not from full. */
void anim_reset(syn_view_t *view)
{
    view->fade_active = 0;
    view->alpha = 1.0f;
    if (view->mapped)
        anim_apply_alpha(view);
}

/* ── Per-frame tick ──────────────────────────────────────── */
/* Advance every running fade. Returns true while at least one is still going,
 * so output_frame keeps scheduling frames until things settle. */
bool anim_tick(syn_server_t *s, double now)
{
    bool running = false;
    double dur = s->config.animation_ms / 1000.0;
    if (dur <= 0.0) dur = 0.001;

    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link) {
            if (!v->fade_active) continue;
            if (!v->mapped) { v->fade_active = 0; continue; }

            float t = (float)((now - v->fade_start) / dur);
            if (t >= 1.0f) {
                v->alpha       = v->fade_to;
                v->fade_active = 0;
                anim_apply_alpha(v);
                if (v->fade_hide_done && v->fade_to <= 0.0f) {
                    wlr_scene_node_set_enabled(view_node(v), false);
                    /* Left at 0 it would be invisible when re-enabled; the show
                     * path fades it back in from 0 on purpose, but a plain
                     * enable (minimize restore) must not get a ghost. */
                    v->alpha = 1.0f;
                    anim_apply_alpha(v);
                }
                v->fade_hide_done = 0;
                continue;
            }
            if (t < 0.0f) t = 0.0f;

            float e = ease_out(t);
            v->alpha = v->fade_from + (v->fade_to - v->fade_from) * e;
            anim_apply_alpha(v);
            running = true;
        }
    }
    return running;
}
