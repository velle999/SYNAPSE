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
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdlib.h>

#include "synui.h"

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
    if (s->focused_view && s->focused_view->mapped)
        focus_view(s, s->focused_view, s->focused_view->xdg_surface->surface);
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

    output->usable_area = usable;
    wlr_log(WLR_DEBUG, "synui: %s usable area %dx%d+%d+%d (full %dx%d)",
            output->wlr_output->name, usable.width, usable.height,
            usable.x, usable.y, full.width, full.height);

    /* Re-tile this output's workspace so windows fit the new usable area. */
    if (!s->shutting_down)
        layout_apply(s, &s->workspaces[output->active_workspace]);
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

static void layer_popup_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_layer_popup_t *lp = wl_container_of(listener, lp, commit);
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
