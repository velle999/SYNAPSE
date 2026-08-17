/*
 * anim.c — window animations
 *
 * The Hyprland-style polish, on a wlr_scene compositor. wlr_scene gives us two
 * free levers and no third: per-buffer opacity
 * (wlr_scene_buffer_set_opacity), and the POSITION of a scene node — moving one
 * configures nobody, which is the whole reason a slide is affordable here. What
 * it does not give us is a per-node shader hook or any way to draw a buffer at
 * a size the client did not agree to.
 *
 * So the two events that animate are:
 *
 *   - a window OPENING: it fades in, and on `rise` glides up into place
 *     instead of snapping into existence (config.anim_window);
 *   - switching DESKTOP: the outgoing windows either fade out or slide off the
 *     way you switched, and are only disabled once they have actually gone;
 *     the incoming ones arrive the same way (config.anim_workspace).
 *
 * Each has its own length, and they share one curve — see syn_anim_*_t in
 * synui.h for why those are the divisions.
 *
 * A window CLOSING is not in that list and cannot be: the client's buffer is
 * gone the moment it unmaps, and fading what is left would mean holding a
 * snapshot texture wlr_scene does not hand us.
 *
 * SIZE animation (a window growing into its tiled slot) is deliberately NOT
 * here either. Animating a window's *size* means re-configuring the client
 * every frame — a resize storm the client has to keep up with, which is exactly
 * why Hyprland animates a scaled snapshot of the buffer instead. Position-only
 * animation is what is left, and it is enough for the two events above because
 * neither of them changes any window's size: an opening window rises at its
 * final size, and a whole desk slides without a single window resizing. A
 * tiling reflow does change sizes, which is why gliding to a tiled slot is
 * still not offered.
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
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <scenefx/types/wlr_scene.h>

#include "synui.h"
#include "kde_blur.h"

/* Ease-out cubic: fast at the start, settling at the end. Movement that decays
 * reads as physical; a linear fade reads as a slideshow.
 *
 * Public (anim_ease_out) because the niri strip slide in layout.c has to decay
 * on the SAME curve. A strip that glides linearly next to windows that fade on
 * a cubic does not read as one animation system, it reads as two — and the two
 * routinely run in the same frame, since switching desktop cross-fades AND
 * re-scrolls every strip on it. */
float anim_ease_out(float t)
{
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}
#define ease_out(t) anim_ease_out(t)

/* The other three curves synuirc's `anim_curve` can name. Cubic throughout, so
 * switching curve changes the *shape* of the decay and not how far anything
 * travels — every one of these maps 0→0 and 1→1. */
float anim_curve_apply(int curve, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    switch (curve) {
    case ANIM_CURVE_LINEAR:
        return t;
    case ANIM_CURVE_EASE_IN:
        return t * t * t;
    case ANIM_CURVE_EASE_IN_OUT:
        /* Standard cubic in-out: the first half is ease-in scaled into [0,0.5],
         * the second is its mirror. */
        return t < 0.5f ? 4.0f * t * t * t
                        : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case ANIM_CURVE_EASE_OUT:
    default:
        return anim_ease_out(t);
    }
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
                               struct wlr_scene_buffer *buffer)
{
    if (!bb->blur) return;

    int w, h;
    blur_buffer_size(buffer, &w, &h);
    if (w <= 0 || h <= 0) {
        wlr_scene_node_set_enabled(&bb->blur->node, false);
        return;
    }

    wlr_scene_blur_set_size(bb->blur, w, h);
    /* Sibling of its buffer, so the buffer's own offset in their shared parent
     * is the whole answer. */
    wlr_scene_node_set_position(&bb->blur->node, buffer->node.x, buffer->node.y);
    /* Directly under its own buffer: a titlebar's blur belongs under the
     * titlebar, not under the window. The ring OUTSIDE the window is a separate
     * node owned by the frame — see view_halo_update(). */
    wlr_scene_node_place_below(&bb->blur->node, &buffer->node);
    wlr_scene_node_set_enabled(&bb->blur->node, true);
}

/* Geometry plus the corner radii, which only the effects walk knows. */
static void blur_sync(struct buffer_blur *bb, struct wlr_scene_buffer *buffer,
                      struct fx_corner_radii corners)
{
    if (!bb->blur) return;

    blur_sync_geometry(bb, buffer);
    wlr_scene_blur_set_corner_radii(bb->blur, corners);
}

/* Turn blur on or off for one buffer, creating or destroying the companion. */
static void blur_set(struct wlr_scene_buffer *buffer, bool want,
                     struct fx_corner_radii corners)
{
    struct buffer_blur *bb = blur_find(buffer);

    if (!want) {
        if (bb) blur_addon_destroy(&bb->addon);
        return;
    }

    if (!bb) {
        int w, h;
        blur_buffer_size(buffer, &w, &h);
        if (w <= 0 || h <= 0) return;   /* nothing painted yet; try next frame */

        struct wlr_scene_tree *parent = buffer->node.parent;
        if (!parent) return;

        bb = calloc(1, sizeof(*bb));
        if (!bb) return;
        bb->blur = wlr_scene_blur_create(parent, w, h);
        if (!bb->blur) { free(bb); return; }

        /* Blur only where the buffer paints — the 0.4 "ignore transparent"
         * behaviour, without which the blur would fill the square corners the
         * client left transparent. Never cleared: scenefx's setter dereferences
         * the source before its NULL check (wlr_scene.c:1153
         * `linked_node_destroy(&source->blur)`), so passing NULL to DROP a mask
         * is a segfault, not a no-op. Set once at creation and left alone. */
        wlr_scene_blur_set_transparency_mask_source(bb->blur, buffer);

        bb->blur_destroy.notify = blur_node_destroyed;
        wl_signal_add(&bb->blur->node.events.destroy, &bb->blur_destroy);
        wlr_addon_init(&bb->addon, &buffer->node.addons, buffer,
                       &blur_addon_impl);
    }

    blur_sync(bb, buffer, corners);
}

/* The same machinery, for a buffer synui painted itself — the dock's glass bar.
 * See the header for why this is exported rather than reimplemented there. */
void syn_buffer_backdrop_blur(struct wlr_scene_buffer *buffer, bool want,
                              int radius)
{
    if (!buffer) return;
    blur_set(buffer, want, corner_radii_all(radius > 0 ? radius : 0));
}

/* ── The same companion, behind a RECT synui coloured itself ──
 *
 * Every compositor-drawn panel is a coloured wlr_scene_rect with a cairo buffer
 * of ink sitting on top of it — see panel_bg_color() and panel_chrome_sync().
 * So the surface a panel IS made of is the rect, and a panel that is glass
 * needs the blur behind THAT. Behind the text buffer would frost the ink's own
 * bounding box and leave the panel body a flat slab, which is the picture you
 * get from blurring the thing you can see instead of the thing you see through.
 *
 * Two differences from the buffer path above, both because a rect is a uniform
 * fill rather than a picture:
 *
 *   - No transparency mask source. The mask exists to stop the blur leaking
 *     into the parts a CLIENT left transparent; a rect has no such parts, and
 *     its rounded corners come from the radii it is handed. There is also no
 *     honest thing to pass — the mask wants the buffer being blurred behind,
 *     and here that is a rect, which is not a wlr_scene_buffer at all.
 *   - The size comes from the rect's own width/height. A client's buffer lands
 *     its new size a frame after the resize it was configured for, which is
 *     what blur_sync_geometry() has to chase; a panel sets its rect's size in
 *     the same render that decides it, so there is nothing to lag behind.
 *
 * Shares the addon type and the destroy listener with the buffer path, so the
 * "the scene tore the blur node down under us" case is handled in one place
 * rather than in a copy that could learn it later.
 */
void syn_rect_backdrop_blur(struct wlr_scene_rect *rect, bool want, int radius)
{
    if (!rect) return;

    struct wlr_addon *a = wlr_addon_find(&rect->node.addons, rect,
                                         &blur_addon_impl);
    struct buffer_blur *bb = NULL;
    if (a) bb = wl_container_of(a, bb, addon);

    if (!want) {
        if (bb) blur_addon_destroy(&bb->addon);
        return;
    }

    /* Nothing on screen yet — a panel whose rect has never been sized. Keep the
     * companion (the panel is about to be rendered) and just stop drawing it. */
    if (rect->width <= 0 || rect->height <= 0) {
        if (bb && bb->blur) wlr_scene_node_set_enabled(&bb->blur->node, false);
        return;
    }

    if (!bb) {
        struct wlr_scene_tree *parent = rect->node.parent;
        if (!parent) return;

        bb = calloc(1, sizeof(*bb));
        if (!bb) return;
        bb->blur = wlr_scene_blur_create(parent, rect->width, rect->height);
        if (!bb->blur) { free(bb); return; }

        bb->blur_destroy.notify = blur_node_destroyed;
        wl_signal_add(&bb->blur->node.events.destroy, &bb->blur_destroy);
        wlr_addon_init(&bb->addon, &rect->node.addons, rect, &blur_addon_impl);
    }
    if (!bb->blur) return;

    /* Every setter early-returns when the value is unchanged, so this is cheap
     * enough for panel_chrome_sync() to run over all of them every frame. */
    wlr_scene_blur_set_size(bb->blur, rect->width, rect->height);
    wlr_scene_blur_set_corner_radius(bb->blur, radius > 0 ? radius : 0);
    /* Sibling of its rect, so the rect's offset in their shared parent is the
     * whole answer — same argument as blur_sync_geometry(). */
    wlr_scene_node_set_position(&bb->blur->node, rect->node.x, rect->node.y);
    wlr_scene_node_place_below(&bb->blur->node, &rect->node);
    wlr_scene_node_set_enabled(&bb->blur->node, true);
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
    blur_set(buffer, blur, corners);
}

/*
 * "App-native glass" windows draw their own translucent background with opaque
 * text (kitty's background_opacity, foot's [colors-dark] alpha), so a
 * compositor-wide uniform fade on top of that would drag the *text* down with
 * the background — the exact "everything fades together" the glass look is meant
 * to avoid. Such windows are left fully opaque at the compositor; synui-glass
 * drives their real alpha instead, keeping the glyphs crisp at any transparency.
 * Keyed on app_id:
 *   - kitty: background_opacity (synui-glass). The default terminal.
 *   - foot/footclient: [colors-dark] alpha (synui-glass). Still supported —
 *     `terminal = foot` remains valid and upgraded systems keep their foot.ini.
 *   - firefox: glass chrome via userChrome.css (widget.wayland.opaque-region
 *     disabled). Its page content is opaque anyway, so a uniform fade would only
 *     wash out the text it can't make transparent — leave it to draw its own.
 *
 * A terminal MISSING from this list is a silent fault, not a loud one: it keeps
 * drawing its own alpha and then gets the compositor's fade stacked on top, so
 * the text washes out exactly as if glass were broken. Nothing logs it.
 *
 * ⚠ syntty IS THE DEFAULT TERMINAL AND IS DELIBERATELY NOT HERE. The rule is
 * "does the window draw its own alpha", not "is it the default": syntty's
 * config has foreground, background and cursor colours and NO opacity key, so
 * it paints an opaque background and takes the compositor's uniform fade like
 * every other opaque window. Adding it would pin it fully opaque and it would
 * be the one window that ignores the transparency slider. Revisit only if
 * syntty grows a real background-alpha setting — and then teach synui-glass to
 * write it in the same change, or it will be listed here and driven by nobody.
 */
static bool view_is_glass_native(syn_view_t *view)
{
    const char *id = view_app_id(view);
    if (!id) return false;
    return strcmp(id, "kitty") == 0 ||
           strcmp(id, "foot") == 0 || strcmp(id, "footclient") == 0 ||
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
    struct view_effect_params p = {
        .alpha            = a,
        .corner_radius    = radius,
        .blur             = s && s->config.blur && !view->fullscreen &&
                            translucent,
        .kde_blur_ok      = s && s->config.blur && !view->fullscreen,
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
    if (bb) blur_sync_geometry(bb, buffer);
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

/* ── Starting one ────────────────────────────────────────── */
/*
 * The one entry point. Everything an animation can do is here: opacity from →
 * to, offset from → to, over `ms` milliseconds, and whether the node is
 * disabled at the end.
 *
 * `ms <= 0` is not a special case anybody else has to handle — the end state
 * is applied on the spot and no animation is armed, which is what makes
 * "animations off" a duration of zero rather than a mode.
 */
static void anim_start(syn_view_t *view, int ms,
                       float alpha_to,
                       int dx_from, int dx_to, int dy_from, int dy_to,
                       bool hide_when_done)
{
    syn_server_t *s = view->server;

    if (!view->mapped) return;

    if (ms <= 0) {
        view->alpha       = alpha_to;
        view->fade_active = 0;
        view->anim_dx     = dx_to;
        view->anim_dy     = dy_to;
        view_place_node(view);
        anim_apply_alpha(view);
        if (hide_when_done && alpha_to <= 0.0f)
            wlr_scene_node_set_enabled(view_node(view), false);
        return;
    }

    view->fade_from      = view->alpha;
    view->fade_to        = alpha_to;
    view->fade_start     = now_secs();
    view->fade_dur       = ms / 1000.0;
    view->fade_curve     = s->config.anim_curve;
    view->fade_active    = 1;
    view->fade_hide_done = hide_when_done ? 1 : 0;

    view->anim_dx_from = dx_from;  view->anim_dx_to = dx_to;
    view->anim_dy_from = dy_from;  view->anim_dy_to = dy_to;

    /* Start displaced, so the first frame of the animation is the start of it
     * rather than one frame of the end state. */
    if (view->anim_dx != dx_from || view->anim_dy != dy_from) {
        view->anim_dx = dx_from;
        view->anim_dy = dy_from;
        view_place_node(view);
    }
}

/* How far a desktop slides: the width of the monitor the window is on, so two
 * monitors of different widths each carry their own windows off their own edge
 * rather than sharing one distance that is wrong for both. */
static int slide_distance(syn_view_t *view)
{
    syn_server_t *s = view->server;
    struct wlr_box b;
    syn_output_t *o = view->output ? view->output : server_focused_output(s);

    output_box_of(s, o, &b);
    return b.width > 0 ? b.width : 1920;
}

/* ── A window has just mapped ────────────────────────────── */
void anim_window_open(syn_view_t *view)
{
    syn_server_t *s = view->server;
    if (!view->mapped || view->minimized) return;

    int  ms   = s->config.anim_window_ms;
    int  rise = 0;

    switch (s->config.anim_window) {
    case ANIM_WINDOW_NONE:
        ms = 0;
        break;
    case ANIM_WINDOW_RISE:
        rise = s->config.anim_rise_px;
        break;
    case ANIM_WINDOW_FADE:
    default:
        break;
    }

    view->alpha = (ms > 0) ? 0.0f : 1.0f;
    anim_apply_alpha(view);
    /* Rise travels UP into place: it starts +rise BELOW where it belongs. */
    anim_start(view, ms, 1.0f, 0, 0, rise, 0, false);
}

/* ── The two halves of a desktop switch ──────────────────── */
/*
 * hide() only disables the node once the window is actually gone — a window
 * that vanishes the instant the desktop changes is the snap all of this exists
 * to remove. show() enables it first, for the same reason in reverse.
 */
void anim_workspace_hide(syn_view_t *view, int dir)
{
    syn_server_t *s = view->server;

    if (!view->mapped) {
        wlr_scene_node_set_enabled(view_node(view), false);
        return;
    }

    int ms = s->config.anim_workspace_ms;
    int dx = 0;
    float to = 0.0f;

    switch (s->config.anim_workspace) {
    case ANIM_WS_NONE:
        ms = 0;
        break;
    case ANIM_WS_SLIDE:
        /* Switching UP (dir > 0) sends this desk off to the LEFT, the way a
         * pager reads. The windows keep full opacity: they are leaving, not
         * dissolving, and a slide that also fades reads as neither. */
        dx = (dir >= 0) ? -slide_distance(view) : slide_distance(view);
        to = 1.0f;
        break;
    case ANIM_WS_FADE:
    default:
        break;
    }

    anim_start(view, ms, to, 0, dx, 0, 0, true);
}

void anim_workspace_show(syn_view_t *view, int dir)
{
    syn_server_t *s = view->server;
    if (!view->mapped || view->minimized) return;

    wlr_scene_node_set_enabled(view_node(view), true);

    int ms = s->config.anim_workspace_ms;
    int dx = 0;

    switch (s->config.anim_workspace) {
    case ANIM_WS_NONE:
        ms = 0;
        break;
    case ANIM_WS_SLIDE:
        /* The mirror of hide(): the incoming desk comes from the side the
         * outgoing one left towards. */
        dx = (dir >= 0) ? slide_distance(view) : -slide_distance(view);
        view->alpha = 1.0f;
        break;
    case ANIM_WS_FADE:
    default:
        view->alpha = 0.0f;
        break;
    }

    anim_apply_alpha(view);
    anim_start(view, ms, 1.0f, dx, 0, 0, 0, false);
}

/* A window that was animated out and is being shown again (desktop switch back,
 * un-minimize) must start from wherever it was left, not from full — and it
 * must not keep an offset from an animation nobody finished. */
void anim_reset(syn_view_t *view)
{
    view->fade_active = 0;
    view->alpha = 1.0f;
    if (view->anim_dx || view->anim_dy) {
        view->anim_dx = view->anim_dy = 0;
        view_place_node(view);
    }
    if (view->mapped)
        anim_apply_alpha(view);
}

/* ── Per-frame tick ──────────────────────────────────────── */
/* Advance every running fade. Returns true while at least one is still going,
 * so output_frame keeps scheduling frames until things settle. */
bool anim_tick(syn_server_t *s, double now)
{
    bool running = false;

    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link) {
            if (!v->fade_active) continue;
            if (!v->mapped) { v->fade_active = 0; continue; }

            /* Per-run, so a window opening beside a sliding desk keeps its own
             * length even if the config changed under both. */
            double dur = v->fade_dur > 0.0 ? v->fade_dur : 0.001;

            float t = (float)((now - v->fade_start) / dur);
            if (t >= 1.0f) {
                v->alpha       = v->fade_to;
                v->fade_active = 0;
                if (v->anim_dx != v->anim_dx_to || v->anim_dy != v->anim_dy_to) {
                    v->anim_dx = v->anim_dx_to;
                    v->anim_dy = v->anim_dy_to;
                    view_place_node(v);
                }
                anim_apply_alpha(v);
                if (v->fade_hide_done) {
                    wlr_scene_node_set_enabled(view_node(v), false);
                    /* Left mid-animation it would come back displaced or
                     * invisible: the show path starts it from 0 (or from off
                     * the edge) on purpose, but a plain enable — a minimize
                     * restore — must not get a ghost or an offset window. */
                    v->alpha = 1.0f;
                    if (v->anim_dx || v->anim_dy) {
                        v->anim_dx = v->anim_dy = 0;
                        view_place_node(v);
                    }
                    anim_apply_alpha(v);
                }
                v->fade_hide_done = 0;
                continue;
            }
            if (t < 0.0f) t = 0.0f;

            float e = anim_curve_apply(v->fade_curve, t);
            v->alpha = v->fade_from + (v->fade_to - v->fade_from) * e;
            anim_apply_alpha(v);

            int dx = v->anim_dx_from + (int)lroundf((v->anim_dx_to - v->anim_dx_from) * e);
            int dy = v->anim_dy_from + (int)lroundf((v->anim_dy_to - v->anim_dy_from) * e);
            if (dx != v->anim_dx || dy != v->anim_dy) {
                v->anim_dx = dx;
                v->anim_dy = dy;
                view_place_node(v);
            }
            running = true;
        }
    }
    return running;
}
