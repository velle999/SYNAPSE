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
#include "kde_blur.h"

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

/* ── Backdrop blur: the companion node (scenefx 0.5) ─────── */
/*
 * scenefx 0.4 carried backdrop blur as a flag on the buffer itself —
 * wlr_scene_buffer_set_backdrop_blur(buffer, true) — and the renderer blurred
 * whatever lay behind that buffer. 0.5 removed the flag and made blur a node
 * type of its own (wlr_scene_blur), which the compositor has to create, size,
 * position and z-order.
 *
 * So a blurred buffer now needs a companion node kept in lockstep with it:
 * same parent, same position, same size, same corner radii, placed directly
 * below it, with the buffer set as the transparency mask source — that last
 * part is what the old ..._ignore_transparent flag did, blurring only where the
 * buffer actually paints so the rounded-corner cutouts stay crisp.
 *
 * The companion hangs off the buffer's addon set, so it is destroyed with the
 * buffer and nothing else has to remember it exists. It also listens for its
 * own node's destroy, because tearing down a frame tree destroys children in
 * list order — the blur node can go first, and the addon must not then free it
 * a second time.
 *
 * Creating and destroying these from inside wlr_scene_node_for_each_buffer is
 * safe by construction: the companion is inserted *below* the buffer being
 * visited, and wl_list_for_each has already taken its next pointer off that
 * buffer, so a node added or removed before it cannot disturb the walk. Blur
 * nodes are neither BUFFER nor TREE, so the walk skips them entirely and never
 * revisits one.
 */
struct buffer_blur {
    struct wlr_scene_blur *blur;
    struct wlr_addon       addon;
    struct wl_listener     blur_destroy;
    /* The halo the node was BUILT for, not the current setting. Only whether it
     * was zero matters: a masked node and a haloed one differ in a property that
     * cannot be changed afterwards (see blur_set), so crossing that line has to
     * rebuild the node rather than re-sync it. */
    int                    halo;
};

static void blur_addon_destroy(struct wlr_addon *addon);

static const struct wlr_addon_interface blur_addon_impl = {
    .name    = "synui_backdrop_blur",
    .destroy = blur_addon_destroy,
};

static void blur_addon_destroy(struct wlr_addon *addon)
{
    struct buffer_blur *bb = wl_container_of(addon, bb, addon);
    wl_list_remove(&bb->blur_destroy.link);
    wlr_addon_finish(&bb->addon);
    if (bb->blur)
        wlr_scene_node_destroy(&bb->blur->node);
    free(bb);
}

/* The scene destroyed the blur node under us (its parent tree went away).
 * Forget it, so the addon teardown does not touch freed memory. */
static void blur_node_destroyed(struct wl_listener *listener, void *data)
{
    (void)data;
    struct buffer_blur *bb = wl_container_of(listener, bb, blur_destroy);
    wl_list_remove(&bb->blur_destroy.link);
    wl_list_init(&bb->blur_destroy.link);
    bb->blur = NULL;
}

static struct buffer_blur *blur_find(struct wlr_scene_buffer *buffer)
{
    struct wlr_addon *a = wlr_addon_find(&buffer->node.addons, buffer,
                                         &blur_addon_impl);
    if (!a) return NULL;
    struct buffer_blur *bb = wl_container_of(a, bb, addon);
    return bb;
}

/* The buffer's painted size. dst_width/height is the scaled destination when
 * the scene has been told one; otherwise the buffer's own dimensions. Zero
 * means there is nothing on screen yet, and so nothing to blur behind. */
static void blur_buffer_size(struct wlr_scene_buffer *buffer, int *w, int *h)
{
    if (buffer->dst_width > 0 && buffer->dst_height > 0) {
        *w = buffer->dst_width;
        *h = buffer->dst_height;
    } else if (buffer->buffer) {
        *w = buffer->buffer->width;
        *h = buffer->buffer->height;
    } else {
        *w = *h = 0;
    }
}

/*
 * The frame tree a buffer belongs to — the node view_frame_create() stamped with
 * the view, which is the same "walk up until something carries a view" rule
 * surface_at() uses. A haloed blur node is parented HERE rather than beside its
 * buffer: it now covers the ring where synui's border rect lives, and inside the
 * surface subtree there is no way to get underneath that rect (the subtree as a
 * whole sits above it), so the first cut of the halo swallowed the focus border.
 */
static struct wlr_scene_tree *buffer_frame_tree(struct wlr_scene_buffer *buffer)
{
    for (struct wlr_scene_tree *t = buffer->node.parent; t; t = t->node.parent) {
        /* Ask the view for its frame rather than taking the first tree that
         * carries one. More than one ancestor does — the surface tree holds the
         * view too, so surface_at() can answer from any node it hits — and
         * stopping there lands back INSIDE the subtree that sits above the
         * border, which is exactly the stacking this is trying to escape. */
        syn_view_t *v = t->node.data;
        if (v && v->frame) return v->frame;
    }
    return buffer->node.parent;   /* no frame (layer surface, drag icon): as before */
}

/* Where the buffer sits relative to `ancestor`. Node coordinates are relative to
 * the immediate parent, so a companion hung off the frame instead of the buffer's
 * own tree has to add every intermediate tree's offset — otherwise a window whose
 * surface tree is offset by the border/titlebar (all of them) gets its halo
 * parked at the frame origin. */
static void buffer_offset_in(struct wlr_scene_buffer *buffer,
                             struct wlr_scene_tree *ancestor, int *ox, int *oy)
{
    int x = 0, y = 0;
    struct wlr_scene_node *n = &buffer->node;
    while (n) {
        x += n->x;
        y += n->y;
        struct wlr_scene_tree *p = n->parent;
        if (!p || p == ancestor) break;
        n = &p->node;
    }
    *ox = x;
    *oy = y;
}

/* Match the companion's *geometry* to its buffer. Cheap enough to call on every
 * client paint: each setter early-returns when the value is unchanged.
 *
 * This has to run on every paint, not only when the window's effects are
 * recomputed. The blur node is sized from the buffer, and the buffer changes
 * size a frame *after* the resize the client was configured for — so anything
 * that resizes a window (a drag on its edge, maximize, a layout reflow) leaves
 * the companion at the old size until something else re-syncs it. That is the
 * "the shadow keeps the window's old size while I drag, and snaps right when I
 * click" report: the leftover is a blur node still covering the box the window
 * used to fill, and the click was a focus change, which does re-sync.
 */
static void blur_sync_geometry(struct buffer_blur *bb,
                               struct wlr_scene_buffer *buffer, int halo)
{
    if (!bb->blur) return;

    int w, h;
    blur_buffer_size(buffer, &w, &h);
    if (w <= 0 || h <= 0) {
        wlr_scene_node_set_enabled(&bb->blur->node, false);
        return;
    }

    wlr_scene_blur_set_size(bb->blur, w + 2 * halo, h + 2 * halo);

    int bx, by;
    buffer_offset_in(buffer, bb->blur->node.parent, &bx, &by);
    wlr_scene_node_set_position(&bb->blur->node, bx - halo, by - halo);

    /* Directly under its own buffer normally — a titlebar's blur belongs under
     * the titlebar, not under the window. A haloed node instead goes to the
     * bottom of the frame, under the shadow and the border, so the stack reads
     * backdrop → halo → shadow → border → client. That is the order Firefox's
     * accidental version already has, and it is re-asserted by anim_lower_halos()
     * because the decoration pass lowers those rects on its own schedule. */
    if (halo > 0)
        wlr_scene_node_lower_to_bottom(&bb->blur->node);
    else
        wlr_scene_node_place_below(&bb->blur->node, &buffer->node);
    wlr_scene_node_set_enabled(&bb->blur->node, true);
}

/* Geometry plus the corner radii, which only the effects walk knows. */
static void blur_sync(struct buffer_blur *bb, struct wlr_scene_buffer *buffer,
                      struct fx_corner_radii corners, int halo)
{
    if (!bb->blur) return;

    blur_sync_geometry(bb, buffer, halo);
    /* The halo grows the node outwards on every side, so each corner's arc has
     * to grow with it or the ring would square off around a rounded window. A
     * corner the window does not round (radius 0 — the seam between titlebar and
     * content) stays square, which is what keeps that seam straight. */
    if (halo > 0)
        corners = corner_radii_new(
            corners.top_left     ? corners.top_left     + halo : 0,
            corners.top_right    ? corners.top_right    + halo : 0,
            corners.bottom_right ? corners.bottom_right + halo : 0,
            corners.bottom_left  ? corners.bottom_left  + halo : 0);
    wlr_scene_blur_set_corner_radii(bb->blur, corners);
}

/* Turn blur on or off for one buffer, creating or destroying the companion. */
static void blur_set(struct wlr_scene_buffer *buffer, bool want,
                     struct fx_corner_radii corners, int halo)
{
    struct buffer_blur *bb = blur_find(buffer);

    if (!want) {
        if (bb) blur_addon_destroy(&bb->addon);
        return;
    }

    /* Turning the halo on or off at runtime (a SIGHUP config reload) means the
     * mask has to come or go, and scenefx cannot CLEAR one: its setter
     * dereferences the source before the NULL check
     * (wlr_scene.c:1153 `linked_node_destroy(&source->blur)`), so passing NULL
     * to drop a mask is a segfault, not a no-op. Rebuild instead. */
    if (bb && (bb->halo > 0) != (halo > 0)) {
        blur_addon_destroy(&bb->addon);
        bb = NULL;
    }

    if (!bb) {
        int w, h;
        blur_buffer_size(buffer, &w, &h);
        if (w <= 0 || h <= 0) return;   /* nothing painted yet; try next frame */

        struct wlr_scene_tree *parent = halo > 0 ? buffer_frame_tree(buffer)
                                                 : buffer->node.parent;
        if (!parent) return;

        bb = calloc(1, sizeof(*bb));
        if (!bb) return;
        bb->blur = wlr_scene_blur_create(parent, w + 2 * halo, h + 2 * halo);
        if (!bb->blur) { free(bb); return; }

        /* Blur only where the buffer paints — the 0.4 "ignore transparent"
         * behaviour, without which the blur would fill the square corners the
         * client left transparent.
         *
         * The halo is the deliberate exception. The mask blurs only where its
         * source RENDERS, so a node grown past its buffer would draw nothing in
         * the ring — the whole point of the halo. Dropping the mask is what
         * gives the ring its blur; the grown corner radii above take over the
         * job of keeping the corners round, which is the other thing the mask
         * was doing for us.
         *
         * This is the effect Firefox already gets for free: it never binds
         * xdg-decoration, so its surface keeps a GTK shadow margin, the blur
         * node is sized from that whole surface, and the margin renders as
         * blurred backdrop under GTK's shadow. Everything that honours
         * server-side decorations stops at the frame and so had no halo at all.
         */
        if (halo <= 0)
            wlr_scene_blur_set_transparency_mask_source(bb->blur, buffer);
        bb->halo = halo;

        bb->blur_destroy.notify = blur_node_destroyed;
        wl_signal_add(&bb->blur->node.events.destroy, &bb->blur_destroy);
        wlr_addon_init(&bb->addon, &buffer->node.addons, buffer,
                       &blur_addon_impl);
    }

    blur_sync(bb, buffer, corners, halo);
}

/* Push every haloed companion back under the frame's chrome.
 *
 * The decoration pass lowers the border rect and then the shadow to the BOTTOM of
 * the frame, in that order, whenever anything about the chrome changes — a focus
 * change, a resize, a theme reload. Each of those calls puts them back underneath
 * a halo that was lowered earlier, and the halo covers the ring the border draws
 * in, so the border would blink out until the client's next paint re-lowered it.
 * Re-asserting right after the chrome moves is what makes the stacking a rule
 * rather than a race between two schedules. A no-op when nothing is haloed.
 */
static void lower_halo(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
    (void)sx; (void)sy; (void)data;
    struct buffer_blur *bb = blur_find(buffer);
    if (bb && bb->halo > 0 && bb->blur)
        wlr_scene_node_lower_to_bottom(&bb->blur->node);
}

void anim_lower_halos(syn_view_t *view)
{
    if (!view || !view->mapped || !view->server) return;
    if (view->server->config.glass_halo <= 0) return;
    wlr_scene_node_for_each_buffer(view_node(view), lower_halo, NULL);
}

/* ── Applying alpha + scenefx glass to a whole window ─────── */
struct view_effect_params {
    float alpha;
    int   corner_radius;   /* 0 = square (maximized/fullscreen) */
    bool  blur;            /* backdrop blur behind this window */
    /* Blur is allowed for a buffer that ASKS for it via org_kde_kwin_blur, even
     * when the heuristic above said no. `blur` is a guess that a window will be
     * translucent (app_id allow-list, or the global transparency switch); a KDE
     * client sending a blur request is not a guess, it is the client stating it
     * paints a translucent background. So the guess is bypassed — but the
     * master switch and the fullscreen guard are not, since those are the
     * user's call and the compositor's, not the client's. */
    bool  kde_blur_ok;
    /* How far the backdrop blur reaches PAST the window, in px. 0 = the old
     * behaviour (blur stops at the frame). See blur_set. */
    int   halo;
    /* Which corners each buffer rounds. A decorated window is two stacked
     * buffers — titlebar above content — so rounding all four on both curves the
     * two edges that meet in the middle, pinching the window's waist. The
     * titlebar takes the top corners, the content the bottom ones, and the seam
     * between them stays straight. Undecorated (titlebar hidden or height 0):
     * the content is the whole window and rounds all four.
     *
     * scenefx 0.5 fused "how round" with "which corners" into one
     * fx_corner_radii, so these are the finished radii rather than a location
     * to be combined with corner_radius at apply time. */
    struct wlr_scene_buffer *titlebar;
    struct fx_corner_radii   titlebar_corners;
    struct fx_corner_radii   content_corners;
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
    struct fx_corner_radii corners = buffer == p->titlebar ? p->titlebar_corners
                                                           : p->content_corners;
    wlr_scene_buffer_set_corner_radii(buffer, corners);
    bool blur = p->blur;
    if (!blur && p->kde_blur_ok) {
        /* Per BUFFER, not per view: a window is a frame tree, and only the
         * client's own surfaces can carry a blur request — the titlebar and the
         * border rects are synui's, and blurring behind an opaque titlebar
         * would just cost a pass. try_from_buffer returns NULL for those. */
        struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
        blur = ss && syn_kde_blur_wants(ss->surface);
    }
    /* Live blur, via a companion node (see blur_set). Still NOT the optimized
     * path: scenefx's "optimized" blur samples a pre-blurred backdrop cached in
     * fx_effect_framebuffers->optimized_blur_buffer, and that buffer is only
     * ever filled by a WLR_SCENE_NODE_OPTIMIZED_BLUR node, which synui does not
     * create. Under 0.4 asking for it anyway left the buffer NULL, so every
     * blurred window took the "Failed to use optimized blur" branch and fell
     * back to live blur — an error log per window per frame for a setting that
     * never did anything. 0.5 makes that an explicit node type we simply do not
     * create; live blur is the honest description of what we render. */
    blur_set(buffer, blur, corners, p->halo);
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
    int  radius    = (boxy || !s) ? 0 : chrome_corner_radius(&s->config);
    int  halo      = (boxy || !s) ? 0 : s->config.glass_halo;
    struct view_effect_params p = {
        .alpha            = a,
        .corner_radius    = radius,
        /* The halo is the exception to "blur behind an opaque window is
         * invisible": it grows the companion PAST the window, and that ring is
         * over the desktop, not over the client. Gating on translucency alone
         * meant every server-side-decorated window (Dolphin, foot) computed a
         * halo, handed it to blur_set, and had the companion refused — the
         * halo could only ever appear on a glass-native app or with the
         * transparency master switch on. */
        .blur             = s && s->config.blur && !view->fullscreen &&
                            (translucent || halo > 0),
        .kde_blur_ok      = s && s->config.blur && !view->fullscreen,
        /* Same edge-to-edge rule the shadow and the corner radii follow: a
         * maximized or fullscreen window has no desktop beside it to blur, so
         * the halo would only reach onto the neighbouring tile or off the
         * output. */
        .halo             = halo,
        .titlebar         = view->titlebar,
        .titlebar_corners = corner_radii_top(radius),
        .content_corners  = decorated ? corner_radii_bottom(radius)
                                      : corner_radii_all(radius),
    };

    wlr_scene_node_for_each_buffer(view_node(view), set_buffer_effects, &p);
    view_update_decorations(view);   /* re-tints the border rects at `a` */
}

static void set_buffer_opacity(struct wlr_scene_buffer *buffer,
                               int sx, int sy, void *data)
{
    (void)sx; (void)sy;
    wlr_scene_buffer_set_opacity(buffer, *(const float *)data);

    /* Under scenefx 0.4 backdrop blur was a flag on this buffer and needed no
     * upkeep. Under 0.5 it is a separate node that only covers what it is told
     * to, so the buffer having just painted at a new size is exactly the event
     * that has to move it — see blur_sync_geometry. Existing companions only:
     * whether a window *gets* blur is the effects walk's call, not this one's. */
    struct buffer_blur *bb = blur_find(buffer);
    /* bb->halo, not the live config: this path re-seats an EXISTING node, and a
     * node built without a halo has a transparency mask that a halo would have
     * to remove — which is the effects walk's job (it rebuilds), not a resize's. */
    if (bb) blur_sync_geometry(bb, buffer, bb->halo);
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
 * That last point stopped being true at scenefx 0.5, which turned backdrop blur
 * from a field on the buffer into a node beside it. A node has a size, and the
 * size it needs is the one the client just painted — so this walk now also
 * re-seats each buffer's blur companion (set_buffer_opacity), which is what
 * keeps the blur from lagging a resize.
 *
 * Still deliberately NOT anim_apply_alpha: this runs on every frame a client
 * paints, so it must not rebuild decorations or re-render the titlebar. Every
 * setter it does call early-returns when the value is unchanged, so the
 * steady-state cost is a compare per buffer.
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
