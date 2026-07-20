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
#include <string.h>
#include <time.h>

#include <scenefx/types/wlr_scene.h>

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

/* ── Applying alpha + scenefx glass to a whole window ─────── */
struct view_effect_params {
    float alpha;
    int   corner_radius;   /* 0 = square (maximized/fullscreen) */
    bool  blur;            /* backdrop blur behind this window */
    /* Which corners each buffer rounds. A decorated window is two stacked
     * buffers — titlebar above content — so rounding all four on both curves the
     * two edges that meet in the middle, pinching the window's waist. The
     * titlebar takes the top corners, the content the bottom ones, and the seam
     * between them stays straight. Undecorated (titlebar hidden or height 0):
     * the content is the whole window and rounds all four. */
    struct wlr_scene_buffer *titlebar;
    enum corner_location     content_corners;
};

/* One walk over every buffer under the frame sets opacity AND the scenefx
 * effects together, so the glass tracks the same events the fade already does
 * (map, focus, transparency toggle, config reload) — no separate re-apply path
 * to forget. corner_radius/backdrop_blur persist on the scene_buffer node across
 * the client's buffer swaps, so a repaint can't quietly drop them (the bug that
 * made the plain-opacity 'transparency setting' snap back to solid). */
static void set_buffer_effects(struct wlr_scene_buffer *buffer,
                               int sx, int sy, void *data)
{
    (void)sx; (void)sy;
    const struct view_effect_params *p = data;
    wlr_scene_buffer_set_opacity(buffer, p->alpha);
    wlr_scene_buffer_set_corner_radius(buffer, p->corner_radius,
                                       buffer == p->titlebar
                                           ? CORNER_LOCATION_TOP
                                           : p->content_corners);
    wlr_scene_buffer_set_backdrop_blur(buffer, p->blur);
    /* NOT optimized blur. scenefx's "optimized" path samples a pre-blurred
     * backdrop cached in fx_effect_framebuffers->optimized_blur_buffer, and that
     * buffer is only ever filled by a WLR_SCENE_NODE_OPTIMIZED_BLUR node
     * (wlr_scene_optimized_blur_create) — which synui does not create. Asking for
     * it anyway left the buffer NULL, so every blurred window took the
     * "Warning: Failed to use optimized blur" branch and fell back to live blur:
     * an error log per window per frame for a setting that never did anything.
     * Live blur is the honest description of what we actually render. */
    wlr_scene_buffer_set_backdrop_blur_optimized(buffer, false);
    /* Skip blur under fully-transparent (alpha 0) regions — the rounded-corner
     * cutouts — so corners stay crisp; the semi-transparent glass body still
     * gets blurred. */
    wlr_scene_buffer_set_backdrop_blur_ignore_transparent(buffer, true);
}

/*
 * "App-native glass" windows draw their own translucent background with opaque
 * text (foot's [colors-dark] alpha), so a compositor-wide uniform fade on top of
 * that would drag the *text* down with the background — the exact "everything
 * fades together" the glass look is meant to avoid. Such windows are left fully
 * opaque at the compositor; synui-glass drives their real alpha instead, keeping
 * the glyphs crisp at any transparency. Keyed on app_id:
 *   - foot/footclient: [colors-dark] alpha (synui-glass).
 *   - firefox: glass chrome via userChrome.css (widget.wayland.opaque-region
 *     disabled). Its page content is opaque anyway, so a uniform fade would only
 *     wash out the text it can't make transparent — leave it to draw its own.
 */
static bool view_is_glass_native(syn_view_t *view)
{
    const char *id = view_app_id(view);
    if (!id) return false;
    return strcmp(id, "foot") == 0 || strcmp(id, "footclient") == 0 ||
           strcmp(id, "firefox") == 0 || strcmp(id, "org.mozilla.firefox") == 0;
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

    /*
     * An override-redirect X11 window is chrome — a menu, tooltip or dropdown —
     * belonging to the window that opened it, so it answers with THAT window's
     * translucency instead of computing its own. An OR surface is never the
     * focused view, so left to itself every X11 menu would come out at
     * inactive_opacity and visibly mismatch the window it hangs off. Every
     * xwayland surface carries its view on xs->data (xw_create), so the parent
     * resolves directly.
     *
     * Resolved once, not recursively: a menu whose parent is itself a menu
     * (submenus do this) would otherwise be a cycle if X ever handed back a
     * parent loop. One hop lands on the real toplevel in every case that matters.
     */
    syn_view_t *target = view;
    if (view->override_redirect && view->is_xwayland && view->xsurface &&
        view->xsurface->parent) {
        syn_view_t *pv = view->xsurface->parent->data;
        if (pv && pv != view && pv->mapped)
            target = pv;
    }

    if (view_is_glass_native(target)) return 1.0f;  /* app draws its own glass */
    /*
     * A fullscreen window covers its whole output, so there is nothing behind it
     * that transparency could usefully reveal — only the wallpaper, a stacked
     * menu, or whatever the layout left underneath. Worse, the artifact is
     * INTERMITTENT and so reads as a compositor glitch rather than a setting: a
     * fullscreen client is normally handed to direct scanout, where its buffer
     * goes to the display untouched and per-node opacity is simply not applied.
     * Only when something forces composition for a frame — a click mapping an
     * override-redirect menu, a focus change — does the 0.5 actually render, for
     * exactly that frame. That is the "I can see through the game for a split
     * second on click" report (velle, 2026-07-19, Akane).
     *
     * Squared corners and disabled blur are already gated on fullscreen in
     * anim_apply_alpha; opacity was the one effect that never got the guard.
     * Maximized is deliberately NOT included — a maximized window still has a
     * desktop behind it and translucency there is the wanted feature.
     */
    if (target->fullscreen) return 1.0f;
    float o = (target == s->focused_view) ? s->config.active_opacity
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

    syn_server_t *s = view->server;
    /* A window is glass-eligible when it will actually be translucent: an app that
     * draws its own alpha (foot, transparent Firefox), or any window while the
     * transparency master switch is on. Blur behind an opaque window is invisible
     * and just burns GPU, so it is gated on that. Rounded corners apply to every
     * window (they read as glass even opaque) but are squared off when maximized/
     * fullscreen so nothing pokes past the tile/output. */
    bool boxy       = view->fullscreen || view->maximized;
    bool translucent = view_is_glass_native(view) || (s && s->config.transparency);
    bool decorated = view->titlebar && view->titlebar->node.enabled;
    struct view_effect_params p = {
        .alpha           = a,
        .corner_radius   = (boxy || !s) ? 0 : chrome_corner_radius(&s->config),
        .blur            = s && s->config.blur && !view->fullscreen && translucent,
        .titlebar        = view->titlebar,
        .content_corners = decorated ? CORNER_LOCATION_BOTTOM
                                     : CORNER_LOCATION_ALL,
    };

    wlr_scene_node_for_each_buffer(view_node(view), set_buffer_effects, &p);
    view_update_decorations(view);   /* re-tints the border rects at `a` */
}

static void set_buffer_opacity(struct wlr_scene_buffer *buffer,
                               int sx, int sy, void *data)
{
    (void)sx; (void)sy;
    wlr_scene_buffer_set_opacity(buffer, *(const float *)data);
}

/*
 * Re-push ONLY the opacity, after the client has painted.
 *
 * scenefx re-derives a surface buffer's opacity from wl_alpha_modifier_v1 on
 * EVERY commit (types/scene/surface.c: `float opacity = 1.0;` unless the client
 * set a modifier, then set_opacity(buffer, opacity)). So a window's translucency
 * survived exactly until the client next painted, and anim_apply_alpha's careful
 * one-shot walk was overwritten a frame later. That single fact produced all
 * three symptoms we chased:
 *   - "transparency only works on the window bar": the titlebar is a synui-drawn
 *     buffer that never commits a surface, so it kept its alpha while the client
 *     content next to it snapped straight back to solid.
 *   - "Dolphin pulses on click": a click is a focus change (anim_apply_alpha sets
 *     0.55) immediately followed by the client repainting (reset to 1.0), so the
 *     window renders translucent for about one frame and reverts — too fast to
 *     read as anything but a flicker.
 *   - corner_radius/backdrop_blur never regressed, which is what made this look
 *     like an opacity-specific bug rather than a lost window: they are
 *     scenefx-only fields that commit handler does not touch.
 *
 * Opacity only, and deliberately NOT anim_apply_alpha: this runs on every frame
 * a client paints, so it must not rebuild decorations or re-render the titlebar.
 * wlr_scene_buffer_set_opacity early-returns when the value is unchanged, so the
 * steady-state cost is a float compare per buffer.
 */
void anim_reapply_opacity(syn_view_t *view)
{
    if (!view->mapped) return;

    float a = view->alpha * anim_view_opacity(view);
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;

    wlr_scene_node_for_each_buffer(view_node(view), set_buffer_opacity, &a);
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
