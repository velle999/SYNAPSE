/*
 * xwayland.c — X11 application support via wlroots XWayland
 *
 * Runs an Xwayland server and manages its surfaces alongside native Wayland
 * (xdg-shell) windows. A syn_view_t can wrap either kind of surface; the
 * accessors at the top of this file paper over the difference so layout, focus
 * and the security feed treat them uniformly.
 *
 * Two flavours of X11 window are handled:
 *   - managed windows      → tiled/floated in a workspace like xdg toplevels,
 *                            with borders and focus/activation.
 *   - override-redirect    → menus, tooltips, dropdowns; positioned at their
 *                            own absolute coordinates in the overlay layer,
 *                            never tiled, focused only if they ask for it.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>

#include <wlr/types/wlr_xcursor_manager.h>

#include "synui.h"
#include "effects.h"

/* border width comes from s->config (live-reloadable via SIGHUP) */

/* ── View accessors (xdg / xwayland agnostic) ────────────── */
struct wlr_surface *view_surface(syn_view_t *v)
{
    if (!v) return NULL;
    if (v->is_xwayland)
        return v->xsurface ? v->xsurface->surface : NULL;
    return v->xdg_surface ? v->xdg_surface->surface : NULL;
}

const char *view_app_id(syn_view_t *v)
{
    if (v->is_xwayland) return v->xsurface->class;
    return v->xdg_surface->toplevel->app_id;
}

const char *view_title(syn_view_t *v)
{
    if (v->is_xwayland) return v->xsurface->title;
    return v->xdg_surface->toplevel->title;
}

pid_t view_pid(syn_view_t *v)
{
    if (v->is_xwayland) return v->xsurface->pid;
    struct wlr_surface *surf = view_surface(v);
    if (!surf) return 0;
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(surf->resource),
                              &pid, NULL, NULL);
    return pid;
}

void view_close(syn_view_t *v)
{
    if (v->is_xwayland) wlr_xwayland_surface_close(v->xsurface);
    else                wlr_xdg_toplevel_send_close(v->xdg_surface->toplevel);
}

void view_set_activated(syn_view_t *v, int activated)
{
    if (v->is_xwayland)
        wlr_xwayland_surface_activate(v->xsurface, activated);
    else
        wlr_xdg_toplevel_set_activated(v->xdg_surface->toplevel, activated);
}

void view_set_maximized(syn_view_t *v, int maximized)
{
    if (v->is_xwayland)
        wlr_xwayland_surface_set_maximized(v->xsurface, maximized, maximized);
    else
        wlr_xdg_toplevel_set_maximized(v->xdg_surface->toplevel, maximized);
    foreign_toplevel_update_state(v);
}

void view_set_fullscreen(syn_view_t *v, int fullscreen)
{
    if (v->is_xwayland)
        wlr_xwayland_surface_set_fullscreen(v->xsurface, fullscreen);
    else
        wlr_xdg_toplevel_set_fullscreen(v->xdg_surface->toplevel, fullscreen);
    foreign_toplevel_update_state(v);
}

/* xdg-shell has no minimize request — a native client is never told; only
 * X11 (ICCCM WM_STATE) and foreign-toplevel (taskbar) observers care. */
void view_set_minimized(syn_view_t *v, int minimized)
{
    if (v->is_xwayland)
        wlr_xwayland_surface_set_minimized(v->xsurface, minimized);
    foreign_toplevel_update_state(v);
}

/* ── Managed-window mapping ──────────────────────────────── */
static void xw_map(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, map);
    struct wlr_xwayland_surface *xs = view->xsurface;
    syn_server_t *s = view->server;

    view->override_redirect = xs->override_redirect;
    view->mapped = 1;
    wlr_log(WLR_INFO, "synui: X11 window mapped: '%s' (%s)",
            xs->title ? xs->title : "?",
            view->override_redirect ? "override-redirect" : "managed");

    if (view->override_redirect) {
        /* Unmanaged surface (menu/tooltip): overlay layer, absolute position. */
        view->scene_tree = wlr_scene_subsurface_tree_create(
            s->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], xs->surface);
        view->x = xs->x; view->y = xs->y;
        view->w = xs->width; view->h = xs->height;
        wlr_scene_node_set_position(&view->scene_tree->node, xs->x, xs->y);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);

        /* Some OR windows (rofi/dmenu) grab the keyboard themselves. */
        if (wlr_xwayland_surface_override_redirect_wants_focus(xs)) {
            struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
            if (kb)
                wlr_seat_keyboard_notify_enter(s->seat, xs->surface,
                    kb->keycodes, kb->num_keycodes, &kb->modifiers);
        }
        return;
    }

    /* Managed toplevel: join the active workspace and tile/float it. */
    view->scene_tree = wlr_scene_subsurface_tree_create(s->window_tree, xs->surface);
    view->scene_tree->node.data = view;   /* so view_at() finds it */

    view->workspace = server_active_workspace(s);
    if (xs->modal || xs->parent)
        view->floating = 1;
    wl_list_insert(&view->workspace->windows, &view->link);

    layout_apply(s, view->workspace);
    if (view->floating) {
        layout_float_place(s, view);
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }
    focus_view(s, view, xs->surface);
    foreign_toplevel_map(view);

    /* An X11 client that set _NET_WM_STATE_FULLSCREEN before it ever mapped
     * (SDL does, so Chibi does) never got fullscreen geometry: the request
     * arrived while view->mapped was 0, and layout_apply above skips
     * fullscreen views, leaving the surface at the scene origin at its own
     * size — straddling monitors. Apply it now that the view is mapped and
     * has a taskbar handle, the way xdg_surface_map does. Read the state off
     * the surface: wlroots sets it from the property without necessarily
     * emitting request_fullscreen. */
    if (xs->fullscreen)
        view_apply_fullscreen(s, view, 1);

    synui_welcome_hide(s);
}

static void xw_unmap(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, unmap);
    syn_server_t *s = view->server;

    /* L2 (interim): a closing window fires a brief screen glitch. */
    effects_notify_close(s);

    int was_focused = (s->focused_view == view);
    view->mapped = 0;
    foreign_toplevel_unmap(view);
    if (s->focused_view == view) s->focused_view = NULL;
    if (s->grabbed_view == view) {
        s->grabbed_view = NULL;
        s->cursor_mode  = SYNUI_CURSOR_PASSTHROUGH;
    }

    /* Drop borders */
    if (view->border_top)    { wlr_scene_node_destroy(&view->border_top->node);    view->border_top    = NULL; }
    if (view->border_bottom) { wlr_scene_node_destroy(&view->border_bottom->node); view->border_bottom = NULL; }
    if (view->border_left)   { wlr_scene_node_destroy(&view->border_left->node);   view->border_left   = NULL; }
    if (view->border_right)  { wlr_scene_node_destroy(&view->border_right->node);  view->border_right  = NULL; }

    if (!view->override_redirect) {
        wl_list_remove(&view->link);
        wl_list_init(&view->link);
    }
    if (view->scene_tree) {
        wlr_scene_node_destroy(&view->scene_tree->node);
        view->scene_tree = NULL;
    }
    if (!view->override_redirect && !s->shutting_down) {
        layout_apply(s, view->workspace);
        if (was_focused)
            workspace_focus_first(s, view->workspace);
    }
}

/* ── Fullscreen upscale for sub-native X11 clients ───────────
 * Old games (and other X11 clients) often keep rendering a fixed buffer —
 * classically 1920x1080 — even after we configure them to a larger fullscreen
 * box, leaving them unscaled in a corner of a higher-res monitor. When such a
 * managed view is fullscreen and its surface is smaller than its fullscreen
 * box, scale its scene buffer up to fill, aspect-preserving and centred (a
 * non-matching aspect letterboxes rather than stretches). wlr_scene derives
 * the matching input hit-test and damage for a scaled buffer, so no extra
 * pointer bookkeeping is needed.
 *
 * Only single-buffer surfaces are scaled: a uniform buffer scale would
 * misplace the sub-surfaces of a multi-surface client, so those are left as
 * they are (gamescope remains the tool for games that need more). The scene
 * subsurface-tree helper resets dest_size to the surface's logical size on
 * every commit, so this is re-applied from the X11 surface commit handler. */
struct fs_scale_probe { int count; struct wlr_scene_buffer *buf; };

static void fs_scale_count(struct wlr_scene_buffer *b, int sx, int sy, void *data)
{
    (void)sx; (void)sy;
    struct fs_scale_probe *p = data;
    p->count++;
    p->buf = b;
}

void view_fullscreen_rescale(syn_view_t *v)
{
    if (!v || !v->is_xwayland || v->override_redirect || !v->mapped ||
        !v->scene_tree)
        return;
    struct wlr_surface *surf = v->xsurface ? v->xsurface->surface : NULL;
    if (!surf) return;
    int sw = surf->current.width, sh = surf->current.height;
    if (sw <= 0 || sh <= 0) return;

    struct fs_scale_probe p = {0};
    wlr_scene_node_for_each_buffer(&v->scene_tree->node, fs_scale_count, &p);
    if (p.count != 1 || !p.buf) return;   /* leave multi-surface clients alone */

    if (v->fullscreen && v->w > 0 && v->h > 0 && (sw < v->w || sh < v->h)) {
        double fx = (double)v->w / sw, fy = (double)v->h / sh;
        double scale = fx < fy ? fx : fy;
        int dw = (int)(sw * scale + 0.5), dh = (int)(sh * scale + 0.5);
        wlr_scene_buffer_set_dest_size(p.buf, dw, dh);
        wlr_scene_node_set_position(&v->scene_tree->node,
                                    v->x + (v->w - dw) / 2,
                                    v->y + (v->h - dh) / 2);
    } else {
        /* Not fullscreen, or the client already fills the box: hand the buffer
         * back to its natural size at the view origin. (The helper also resets
         * dest_size on the next commit, but do it now so an un-fullscreened or
         * grown window isn't left scaled/offset for a frame.) */
        wlr_scene_buffer_set_dest_size(p.buf, sw, sh);
        wlr_scene_node_set_position(&v->scene_tree->node, v->x, v->y);
    }
}

static void xw_surface_commit(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, commit);
    /* Only fullscreen managed X11 windows need the per-frame re-apply; the
     * un-fullscreen reset is driven once from view_apply_fullscreen. */
    if (view->fullscreen)
        view_fullscreen_rescale(view);
}

static void xw_associate(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, associate);
    view->map.notify = xw_map;
    wl_signal_add(&view->xsurface->surface->events.map, &view->map);
    view->unmap.notify = xw_unmap;
    wl_signal_add(&view->xsurface->surface->events.unmap, &view->unmap);
    view->commit.notify = xw_surface_commit;
    wl_signal_add(&view->xsurface->surface->events.commit, &view->commit);
}

static void xw_dissociate(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, dissociate);
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
}

static void xw_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, destroy);
    syn_server_t *s = view->server;

    foreign_toplevel_unmap(view);   /* no-op if unmap already retracted it */
    if (s->focused_view == view) s->focused_view = NULL;
    if (s->grabbed_view == view) {
        s->grabbed_view = NULL;
        s->cursor_mode  = SYNUI_CURSOR_PASSTHROUGH;
    }

    wl_list_remove(&view->associate.link);
    wl_list_remove(&view->dissociate.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_configure.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->request_activate.link);
    wl_list_remove(&view->request_minimize.link);
    free(view);
}

static void xw_request_configure(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, request_configure);
    struct wlr_xwayland_surface_configure_event *ev = data;
    struct wlr_xwayland_surface *xs = view->xsurface;

    /* Honour the client's geometry before it is mapped, and for floating /
     * override-redirect windows; for tiled windows we own the geometry and
     * simply re-assert it so the client gets its configure-notify. */
    if (!view->mapped || view->override_redirect || view->floating) {
        wlr_xwayland_surface_configure(xs, ev->x, ev->y, ev->width, ev->height);
        if (view->mapped && view->scene_tree) {
            view->x = ev->x; view->y = ev->y;
            view->w = ev->width; view->h = ev->height;
            wlr_scene_node_set_position(&view->scene_tree->node, ev->x, ev->y);
        }
    } else {
        int bw = view->server->config.border_width;
        wlr_xwayland_surface_configure(xs, view->x, view->y,
            view->w - 2 * bw, view->h - 2 * bw);
    }
}

static void xw_request_maximize(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, request_maximize);
    struct wlr_xwayland_surface *xs = view->xsurface;
    /* Ack the request; tiling still governs the actual size. */
    wlr_xwayland_surface_set_maximized(xs, xs->maximized_horz, xs->maximized_vert);
}

static void xw_request_fullscreen(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, request_fullscreen);
    /* Shared path: fullscreen on the workspace's own output, borders hidden;
     * or back to the layout. */
    view_apply_fullscreen(view->server, view, view->xsurface->fullscreen);
}

static void xw_request_activate(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *view = wl_container_of(listener, view, request_activate);
    if (view->mapped && !view->override_redirect)
        focus_view(view->server, view, view->xsurface->surface);
}

/* ICCCM iconify: X11 apps (and some window managers' pagers) toggle this via
 * WM_CHANGE_STATE / _NET_WM_STATE_HIDDEN rather than a dedicated protocol
 * request the way maximize/fullscreen do. */
static void xw_request_minimize(struct wl_listener *listener, void *data)
{
    syn_view_t *view = wl_container_of(listener, view, request_minimize);
    struct wlr_xwayland_minimize_event *event = data;
    if (!view->mapped || view->override_redirect) return;
    view_apply_minimized(view->server, view, event->minimize);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_xwayland_surface);
    struct wlr_xwayland_surface *xs = data;

    syn_view_t *view = calloc(1, sizeof(*view));
    view->server = s;
    view->is_xwayland = 1;
    view->xsurface = xs;
    view->override_redirect = xs->override_redirect;
    xs->data = view;
    wl_list_init(&view->link);

    view->associate.notify = xw_associate;
    wl_signal_add(&xs->events.associate, &view->associate);
    view->dissociate.notify = xw_dissociate;
    wl_signal_add(&xs->events.dissociate, &view->dissociate);
    view->destroy.notify = xw_destroy;
    wl_signal_add(&xs->events.destroy, &view->destroy);
    view->request_configure.notify = xw_request_configure;
    wl_signal_add(&xs->events.request_configure, &view->request_configure);
    view->request_maximize.notify = xw_request_maximize;
    wl_signal_add(&xs->events.request_maximize, &view->request_maximize);
    view->request_fullscreen.notify = xw_request_fullscreen;
    wl_signal_add(&xs->events.request_fullscreen, &view->request_fullscreen);
    view->request_activate.notify = xw_request_activate;
    wl_signal_add(&xs->events.request_activate, &view->request_activate);
    view->request_minimize.notify = xw_request_minimize;
    wl_signal_add(&xs->events.request_minimize, &view->request_minimize);
}

/* ── Server ready: publish DISPLAY, set the X cursor ─────── */
static void xwayland_ready(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_server_t *s = wl_container_of(listener, s, xwayland_ready);
    setenv("DISPLAY", s->xwayland->display_name, 1);
    wlr_log(WLR_INFO, "synui: Xwayland ready on DISPLAY=%s",
            s->xwayland->display_name);

    struct wlr_xcursor *xc =
        wlr_xcursor_manager_get_xcursor(s->cursor_mgr, "default", 1);
    if (xc && xc->image_count > 0) {
        struct wlr_xcursor_image *img = xc->images[0];
        wlr_xwayland_set_cursor(s->xwayland, img->buffer, img->width * 4,
                                img->width, img->height,
                                img->hotspot_x, img->hotspot_y);
    }
}

/* ── Public entry point ──────────────────────────────────── */
void xwayland_setup(syn_server_t *s)
{
    s->xwayland = wlr_xwayland_create(s->display, s->compositor, true /*lazy*/);
    if (!s->xwayland) {
        wlr_log(WLR_ERROR, "synui: failed to start Xwayland — X11 apps disabled");
        return;
    }

    s->new_xwayland_surface.notify = server_new_xwayland_surface;
    wl_signal_add(&s->xwayland->events.new_surface, &s->new_xwayland_surface);
    s->xwayland_ready.notify = xwayland_ready;
    wl_signal_add(&s->xwayland->events.ready, &s->xwayland_ready);

    wlr_xwayland_set_seat(s->xwayland, s->seat);

    /* display_name is assigned at create even in lazy mode. */
    if (s->xwayland->display_name) {
        setenv("DISPLAY", s->xwayland->display_name, 1);
        wlr_log(WLR_INFO, "synui: Xwayland X11 DISPLAY=%s (lazy start)",
                s->xwayland->display_name);
    }
}
