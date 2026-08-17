/*
 * layer.c — wlr-layer-shell support (panels, bars, wallpaper, launchers)
 *
 * Implements the four layer-shell layers (background / bottom / top / overlay)
 * with exclusive-zone accounting, so clients like waybar, swaybg, mako and wofi
 * can position themselves on the desktop. The heavy geometry lifting (anchors,
 * margins, exclusive zones) is done by wlroots' scene helper,
 * wlr_scene_layer_surface_v1_configure(), which also sends the configure and
 * shrinks the remaining usable area we feed back into the tiling layout.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdlib.h>

#include "synui.h"

/* ── Glass behind the shell's own layer surfaces ──────────── */
/*
 * The start menu, the mixer, the bar's right-click menu, the widgets and the
 * OSD are quickshell layer surfaces, and synui applied NO effects to layer
 * surfaces at all — so on a glass theme they were the one family of system
 * chrome still drawing as a solid slab while the windows behind them frosted.
 * That is what "the glass should reach the system menus too" was about; the
 * compositor-drawn half is panel_chrome_sync()'s.
 *
 * ⚠ THE BAR IS DELIBERATELY NOT IN THIS. It is a quickshell PanelWindow like
 * the rest, so the namespace is what separates them — the shell marks the
 * surfaces it wants frosted as SYN_GLASS_NAMESPACE and the bar keeps the plain
 * one. The bar already has a whole tuned opacity system of its own (bar_opacity,
 * the clear bar, the scrim and the backdrop ink), all of it measured WITHOUT a
 * blur underneath, and a clear bar paints nothing but its glyphs — so blurring
 * it by namespace would put a frosted halo behind each glyph and quietly
 * invalidate the contrast those numbers were chosen for. Frosting the bar is a
 * change to that system, not to this one.
 *
 * The mask source inside syn_buffer_backdrop_blur() is what makes this safe on
 * a surface like the start menu, whose layer surface is the WHOLE SCREEN with a
 * transparent click-catcher and the menu drawn as a rectangle inside it: the
 * blur lands only where the client actually paints, so the menu frosts and the
 * catcher stays clear. Without it this would frost the entire output.
 *
 * No corner radius is passed for the same reason — the shape comes from what
 * the client painted, and quickshell has already rounded it.
 */
#define SYN_GLASS_NAMESPACE "synui-glass"

static void layer_blur_buffer(struct wlr_scene_buffer *buffer,
                              int sx, int sy, void *data)
{
    (void)sx; (void)sy;
    syn_buffer_backdrop_blur(buffer, *(const bool *)data, 0);
}

/* Is this one of the shell's own surfaces at all? Popups are keyed off this
 * rather than off their own namespace, which they do not have: an xdg_popup
 * inherits the identity of the layer surface that opened it, so the bar's menus
 * and the mixer are reached through the BAR — which is why they get glass while
 * the bar itself does not. */
static bool layer_is_shell(const syn_layer_surface_t *ls)
{
    const char *ns = ls->layer_surface->namespace;
    return ns && (strcmp(ns, "quickshell") == 0 ||
                  strcmp(ns, SYN_GLASS_NAMESPACE) == 0);
}

static bool layer_wants_glass(const syn_layer_surface_t *ls)
{
    const char *ns = ls->layer_surface->namespace;
    return ns && strcmp(ns, SYN_GLASS_NAMESPACE) == 0;
}

/* Idempotent, and cheap for the same reason the panel walk is: every setter
 * underneath early-returns when nothing moved, and `want = false` on a buffer
 * with no companion does nothing. So this can run on every commit, which is
 * what keeps the blur the same size as the surface it sits behind. */
void layer_glass_apply(syn_layer_surface_t *ls)
{
    if (!ls || !ls->scene) return;

    /* ⚠ ONLY THE SURFACES THAT ASKED, and this must be an early return rather
     * than a walk with want=false. A layer surface's xdg_popups are scene trees
     * created UNDER its own (see layer_surface_new_popup), so walking the bar to
     * clear blur it never had would also walk the mixer and the bar menu and
     * clear theirs — which layer_popup_glass() had just set. The two would then
     * fight on every commit and the mixer would flicker between frosted and
     * flat. Surfaces that never opt in are simply not touched here. */
    if (!layer_wants_glass(ls)) return;

    bool want = syn_glass_active(&ls->server->config);
    wlr_scene_node_for_each_buffer(&ls->scene->tree->node,
                                   layer_blur_buffer, &want);
}

/* Re-assert glass over every layer surface on the desktop. For the events that
 * change the ANSWER rather than the geometry — a theme switch, the transparency
 * or blur toggles, a config reload — none of which commit anything. */
void layer_glass_all(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        syn_layer_surface_t *ls;
        wl_list_for_each(ls, &o->layer_surfaces, link)
            layer_glass_apply(ls);
    }
}

/* ── Keyboard focus helpers ──────────────────────────────── */
static void layer_keyboard_enter(syn_server_t *s, struct wlr_surface *surface)
{
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(s->seat, surface,
                                       kb->keycodes, kb->num_keycodes,
                                       &kb->modifiers);
    else
        wlr_seat_keyboard_notify_enter(s->seat, surface, NULL, 0, NULL);
}

/* Hand keyboard focus back to the focused toplevel (e.g. after a launcher
 * closes), or clear it if there is none. */
static void restore_toplevel_focus(syn_server_t *s)
{
    /* view_surface(), not ->xdg_surface->surface: an X11 view has a NULL
     * xdg_surface (the union is selected by is_xwayland), so the raw deref
     * crashed whenever a keyboard-grabbing layer surface — the start menu —
     * unmapped while an XWayland window held the focus. */
    if (s->focused_view && s->focused_view->mapped)
        focus_view(s, s->focused_view, view_surface(s->focused_view));
    else
        wlr_seat_keyboard_notify_clear_focus(s->seat);
}

/* ── Arrangement ─────────────────────────────────────────── */
void layer_arrange_output(syn_output_t *output)
{
    syn_server_t *s = output->server;

    struct wlr_box full;
    wlr_output_layout_get_box(s->output_layout, output->wlr_output, &full);
    if (full.width <= 0 || full.height <= 0) {
        /* Output not (yet) in the layout — nothing to arrange. */
        output->usable_area = (struct wlr_box){0};
        return;
    }

    struct wlr_box usable = full;

    /* Reserve exclusive zones first, top layers before bottom, then position
     * the non-exclusive surfaces in what's left. */
    static const enum zwlr_layer_shell_v1_layer order[4] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    };
    for (int exclusive = 1; exclusive >= 0; exclusive--) {
        for (int li = 0; li < 4; li++) {
            syn_layer_surface_t *ls;
            wl_list_for_each(ls, &output->layer_surfaces, link) {
                if (ls->layer != order[li]) continue;
                if (!ls->layer_surface->initialized) continue;
                bool has_exclusive =
                    ls->layer_surface->current.exclusive_zone > 0;
                if ((bool)exclusive != has_exclusive) continue;
                wlr_scene_layer_surface_v1_configure(ls->scene, &full, &usable);
            }
        }
    }

    bool usable_changed = !wlr_box_equal(&output->usable_area, &usable);
    output->usable_area = usable;
    wlr_log(WLR_DEBUG, "synui: %s usable area %dx%d+%d+%d (full %dx%d)",
            output->wlr_output->name, usable.width, usable.height,
            usable.x, usable.y, full.width, full.height);

    /* Re-tile the visible desktop so windows fit the new usable area — but only
     * when that box actually moved.
     *
     * This function runs on every layer-shell map, unmap and geometry commit,
     * and most of those change nothing: the volume/brightness OSD, the start
     * menu and the launcher all set exclusionMode=Ignore, so they reserve no
     * space and the desktop underneath has no reason to reflow at all.
     *
     * A redundant reflow is not a free no-op. layout_cascade's arrangement IS
     * its stacking order, so it raises every window in list order — and the
     * focused window sits wherever it happens to be in that list, usually not
     * last. So a volume keypress buried the window you were working in behind
     * the others: once when the OSD mapped, and again 1.6s later when it
     * unmapped. Reported 2026-08-12 as "windows in background resetting to
     * foreground when I change volume". */
    if (usable_changed && !s->shutting_down)
        layout_apply(s, server_active_workspace(s));

    /* The desktop icon grid is sized against that same box, and the bar reserves
     * its strip *after* synui has started — quickshell is a client, so the very
     * first layout, at the end of init, ran against the whole output. Without
     * this the top row stays under the bar until something else re-grids the
     * desktop (a drag, a monitor change), and until then the icons are a bar's
     * height above where the placement they were saved from puts them. Only on a
     * real change: this runs on every layer commit, and a repaint rebuilds a
     * screen-sized cairo surface. */
    if (usable_changed && !s->shutting_down) {
        deskicons_layout(s);
        synui_render_deskicons(s);
    }

    /* A bar that (un)mapped or re-anchored must re-check the fullscreen rule. */
    layer_update_occlusion(s, output);
}

/* ── Fullscreen occlusion ────────────────────────────────── */
/* The scene z-order is fixed at startup — layer[BOTTOM] < window_tree <
 * layer[TOP] — so a fullscreen window can never be raised over a panel:
 * view_apply_fullscreen's raise_to_top only reorders within window_tree. The
 * only way to let a fullscreen window cover the bar is to take this output's
 * TOP-layer surfaces out of the scene while it shows one.
 *
 * OVERLAY is deliberately left alone: the lock screen and the OSDs must stay
 * visible over a game. The bar keeps its exclusive zone either way, so the
 * usable area (and therefore the tiling of every non-fullscreen window) is
 * unchanged — fullscreen views are laid out from the full output box, not the
 * usable one, so they cover the vacated strip. */
void layer_update_occlusion(syn_server_t *s, syn_output_t *o)
{
    if (!o) return;

    int hide = 0;
    syn_view_t *v;
    wl_list_for_each(v, &server_active_workspace(s)->windows, link) {
        if (v->output != o) continue;   /* only this monitor's windows cover it */
        if (v->mapped && v->fullscreen && !v->minimized) { hide = 1; break; }
    }

    syn_layer_surface_t *ls;
    wl_list_for_each(ls, &o->layer_surfaces, link) {
        if (ls->layer != ZWLR_LAYER_SHELL_V1_LAYER_TOP) continue;
        if (ls->scene && ls->scene->tree)
            wlr_scene_node_set_enabled(&ls->scene->tree->node, !hide);
    }
}

void layer_update_occlusion_all(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        layer_update_occlusion(s, o);

    /* A fullscreen change also decides whether the always-visible dock sits over
     * or under the fullscreen window (dock_apply_position reads the same rule),
     * so restack the docks whenever occlusion is recomputed. */
    dock_relayout(s);
}

/* ── Layer surface events ────────────────────────────────── */
static void layer_surface_map(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_layer_surface_t *ls = wl_container_of(listener, ls, map);
    layer_arrange_output(ls->output);

    /* Launchers and lock-style surfaces ask for the keyboard. */
    if (ls->layer_surface->current.keyboard_interactive !=
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
        layer_keyboard_enter(ls->server, ls->layer_surface->surface);

    /* A panel/launcher can map right under a cursor that never moved; it needs
     * wl_pointer.enter now, not on the next physical nudge. */
    pointer_rebase(ls->server);
}

static void layer_surface_unmap(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_layer_surface_t *ls = wl_container_of(listener, ls, unmap);
    syn_server_t *s = ls->server;

    /* If this surface held the keyboard, return focus to a toplevel. */
    if (s->seat->keyboard_state.focused_surface == ls->layer_surface->surface)
        restore_toplevel_focus(s);

    if (!s->shutting_down)
        layer_arrange_output(ls->output);
    /* A dismissed launcher/menu uncovers whatever was beneath it. */
    pointer_rebase(s);
}

static void layer_surface_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_layer_surface_t *ls = wl_container_of(listener, ls, commit);
    struct wlr_layer_surface_v1 *lsurf = ls->layer_surface;
    syn_server_t *s = ls->server;

    /* Follow a set_layer request by moving the scene node between layer trees. */
    if (lsurf->current.layer != ls->layer) {
        ls->layer = lsurf->current.layer;
        wlr_scene_node_reparent(&ls->scene->tree->node,
                                s->layer_tree[ls->layer]);
    }

    /* On EVERY commit, plain buffer ones included: the blur companion is sized
     * from the buffer, so a menu that changes height — a search that filters its
     * rows down — would otherwise keep the frosted patch it opened at. Same
     * staleness the window blur has to chase in blur_sync_geometry(). */
    layer_glass_apply(ls);

    /* (Re)arrange on the initial commit (to send the first configure) and
     * whenever geometry-affecting state changed — but not on plain buffer
     * commits, which would loop configure/ack forever. */
    if (lsurf->initial_commit || lsurf->current.committed != 0)
        layer_arrange_output(ls->output);
}

/* An xdg_popup attached to a layer surface via get_popup (waybar menus,
 * wofi submenus). It reached the xdg-shell new_popup signal parentless, so
 * this is the only place its scene tree can be created. Unconstraining and
 * the initial configure both have to wait for the initial commit — the
 * popup surface isn't initialized before then and calling either trips a
 * wlroots assert. */
typedef struct {
    struct wlr_xdg_popup *popup;
    syn_layer_surface_t  *ls;
    struct wl_listener    commit;
    struct wl_listener    destroy;
} syn_layer_popup_t;

/* Glass for a menu the shell opened off one of its own surfaces — the bar's
 * right-click menu and the mixer, which are xdg_popups parented to the BAR.
 *
 * Keyed on the parent being a shell surface rather than on a namespace of its
 * own, because a popup has none. That is also what lets the bar's menus be
 * frosted while the bar is not: the menu is reached through this path and the
 * bar is only ever reached through layer_glass_apply(), which skips it.
 */
static void layer_popup_glass(syn_layer_popup_t *lp)
{
    struct wlr_scene_tree *tree = lp->popup->base->data;
    if (!tree) return;
    bool want = syn_glass_active(&lp->ls->server->config) &&
                layer_is_shell(lp->ls);
    wlr_scene_node_for_each_buffer(&tree->node, layer_blur_buffer, &want);
}

static void layer_popup_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_layer_popup_t *lp = wl_container_of(listener, lp, commit);

    /* Before the initial-commit gate: a popup's blur has to be re-synced on
     * every commit, because the companion node is sized from the buffer and a
     * menu that grows a submenu or reflows its rows would otherwise keep the
     * frosted patch it had at the size it opened at. */
    layer_popup_glass(lp);

    if (!lp->popup->base->initial_commit)
        return;

    /* Constrain to the output (in layer-surface-local coordinates) so a menu
     * spawned at a screen edge flips/slides into view instead of clipping. */
    syn_layer_surface_t *ls = lp->ls;
    struct wlr_box out_box;
    wlr_output_layout_get_box(ls->server->output_layout,
                              ls->output->wlr_output, &out_box);
    struct wlr_box constraint = {
        .x = out_box.x - ls->scene->tree->node.x,
        .y = out_box.y - ls->scene->tree->node.y,
        .width = out_box.width,
        .height = out_box.height,
    };
    wlr_xdg_popup_unconstrain_from_box(lp->popup, &constraint);

    /* wlroots 0.19 requires the compositor to answer the initial commit with
     * a configure (see synui_main.c's popup_watch_commit) or the client waits
     * forever and never maps/accepts input. Safe here because we're already
     * gated on initial_commit being true — the earlier SIGABRT this file's
     * comment warns about came from calling this before that point. */
    wlr_xdg_surface_schedule_configure(lp->popup->base);
}

static void layer_popup_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_layer_popup_t *lp = wl_container_of(listener, lp, destroy);
    wl_list_remove(&lp->commit.link);
    wl_list_remove(&lp->destroy.link);
    free(lp);
}

static void layer_surface_new_popup(struct wl_listener *listener, void *data)
{
    syn_layer_surface_t *ls = wl_container_of(listener, ls, new_popup);
    struct wlr_xdg_popup *popup = data;

    popup->base->data =
        wlr_scene_xdg_surface_create(ls->scene->tree, popup->base);

    syn_layer_popup_t *lp = calloc(1, sizeof(*lp));
    lp->popup = popup;
    lp->ls = ls;
    lp->commit.notify = layer_popup_commit;
    wl_signal_add(&popup->base->surface->events.commit, &lp->commit);
    lp->destroy.notify = layer_popup_destroy;
    wl_signal_add(&popup->events.destroy, &lp->destroy);
}

static void layer_surface_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_layer_surface_t *ls = wl_container_of(listener, ls, destroy);
    syn_output_t *output = ls->output;
    syn_server_t *s = ls->server;

    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->new_popup.link);
    wl_list_remove(&ls->link);
    free(ls);

    /* Reclaim the exclusive zone it held (unless the output is going away). */
    if (!s->shutting_down && output->wlr_output)
        layer_arrange_output(output);
}

static void server_new_layer_surface(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_layer_surface);
    struct wlr_layer_surface_v1 *lsurf = data;

    /* The client may not have picked an output — assign the focused one. */
    if (!lsurf->output) {
        syn_output_t *o = server_focused_output(s);
        if (!o) {
            wlr_log(WLR_ERROR,
                    "synui: no output for layer surface '%s' — closing",
                    lsurf->namespace ? lsurf->namespace : "?");
            wlr_layer_surface_v1_destroy(lsurf);
            return;
        }
        lsurf->output = o->wlr_output;
    }

    syn_output_t *output = lsurf->output->data;
    if (!output) {
        wlr_layer_surface_v1_destroy(lsurf);
        return;
    }

    syn_layer_surface_t *ls = calloc(1, sizeof(*ls));
    ls->server = s;
    ls->output = output;
    ls->layer_surface = lsurf;
    ls->layer = lsurf->pending.layer;   /* set in the get_layer_surface request */
    if (ls->layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)
        ls->layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    lsurf->data = ls;   /* so xdg popups can find our scene tree */

    ls->scene = wlr_scene_layer_surface_v1_create(s->layer_tree[ls->layer], lsurf);

    ls->map.notify = layer_surface_map;
    wl_signal_add(&lsurf->surface->events.map, &ls->map);
    ls->unmap.notify = layer_surface_unmap;
    wl_signal_add(&lsurf->surface->events.unmap, &ls->unmap);
    ls->commit.notify = layer_surface_commit;
    wl_signal_add(&lsurf->surface->events.commit, &ls->commit);
    ls->destroy.notify = layer_surface_destroy;
    wl_signal_add(&lsurf->events.destroy, &ls->destroy);
    ls->new_popup.notify = layer_surface_new_popup;
    wl_signal_add(&lsurf->events.new_popup, &ls->new_popup);

    wl_list_insert(&output->layer_surfaces, &ls->link);

    wlr_log(WLR_INFO, "synui: layer surface '%s' on %s (layer %d)",
            lsurf->namespace ? lsurf->namespace : "?",
            output->wlr_output->name, ls->layer);
}

/* ── Public entry points ─────────────────────────────────── */
void layer_shell_init(syn_server_t *s)
{
    s->layer_shell = wlr_layer_shell_v1_create(s->display, 4);
    s->new_layer_surface.notify = server_new_layer_surface;
    wl_signal_add(&s->layer_shell->events.new_surface, &s->new_layer_surface);
}

void layer_output_destroy(syn_output_t *output)
{
    /* Closing each surface fires layer_surface_destroy, which unlinks it. */
    syn_layer_surface_t *ls, *tmp;
    wl_list_for_each_safe(ls, tmp, &output->layer_surfaces, link)
        wlr_layer_surface_v1_destroy(ls->layer_surface);
}
