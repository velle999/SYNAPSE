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
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xcb/randr.h>
#include <xcb/xcb.h>

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
    wlr_log(WLR_INFO, "synui: X11 window mapped: '%s' (0x%x, %s)",
            xs->title ? xs->title : "?", xs->window_id,
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

    /* Managed toplevel: join the active workspace and tile/float it. The client
     * surface lives inside a per-view frame alongside its borders + titlebar. */
    struct wlr_scene_tree *frame = view_frame_create(view, s->window_tree);
    view->scene_tree = wlr_scene_subsurface_tree_create(frame, xs->surface);
    view->scene_tree->node.data = view;   /* so view_at() finds it */

    view->workspace = server_active_workspace(s);
    view->output    = server_focused_output(s);
    if (xs->modal || xs->parent)
        view->floating = 1;
    wl_list_insert(&view->workspace->windows, &view->link);

    layout_apply(s, view->workspace);
    if (view->floating) {
        layout_float_place(s, view);
        wlr_scene_node_raise_to_top(view_node(view));
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

    anim_fade_in(view);          /* windows arrive, they don't just appear */
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
    /* A game exiting is an unmap, not an un-fullscreen — without this, game
     * mode would stay engaged (and synapd stopped) after the game quit, and
     * the bar this view was covering would never come back. */
    layer_update_occlusion_all(s);
    game_reevaluate(s);
    if (s->focused_view == view) s->focused_view = NULL;
    if (s->grabbed_view == view) {
        s->grabbed_view = NULL;
        s->cursor_mode  = SYNUI_CURSOR_PASSTHROUGH;
    }

    /* Drop the whole frame tree — chrome, surface tree, and the frame itself.
     * xw_map builds a fresh frame on every map, so an unmap that dropped only
     * the surface tree left the frame behind, and the next map orphaned it for
     * good: one leaked tree per close/restore cycle, which is what Steam does
     * every time it closes to the tray. */
    view_frame_destroy(view);

    if (!view->override_redirect) {
        wl_list_remove(&view->link);
        wl_list_init(&view->link);
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

/* Place the client surface at an offset *inside* the view's frame.
 *
 * The surface tree is a child of the frame (view_frame_create), so its position
 * is relative to the frame's origin — exactly as view_resize places it. Passing
 * an absolute layout coordinate here displaces the surface by the frame's own
 * position, which is invisible on a monitor at the layout origin and ruinous on
 * any other: a fullscreen window on the portrait monitor (layout y=1080) had its
 * top 1080px pushed off the bottom of the screen. */
static void fs_place_surface(syn_view_t *v, int off_x, int off_y)
{
    int base_x = v->frame ? 0 : v->x;
    int base_y = v->frame ? 0 : v->y;
    wlr_scene_node_set_position(&v->scene_tree->node,
                                base_x + off_x, base_y + off_y);
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
        fs_place_surface(v, (v->w - dw) / 2, (v->h - dh) / 2);
    } else {
        /* Not fullscreen, or the client already fills the box: hand the buffer
         * back to its natural size at the content offset. (The helper also resets
         * dest_size on the next commit, but do it now so an un-fullscreened or
         * grown window isn't left scaled/offset for a frame.) A window that is
         * back in the layout has its border and titlebar again, so the content
         * offset is not the frame origin — view_content_box is the same answer
         * view_resize gives, and this runs after it. */
        struct wlr_box c;
        view_content_box(v, &c);
        wlr_scene_buffer_set_dest_size(p.buf, sw, sh);
        fs_place_surface(v, c.x - v->x, c.y - v->y);
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

    /* wlroots maps an Xwayland surface from ONE place: its commit handler
     * (xwm.c xwayland_surface_handle_commit). Association itself never maps,
     * however much buffer the surface already carries. That is fine for a
     * fresh surface — a commit is always still to come — but a surface can be
     * associated with a buffer already committed on it:
     *
     *   X unmap  → xwm dissociates → wlr_surface_unmap(). This does NOT clear
     *              the buffer; wlr_surface_has_buffer() reads current.buffer_
     *              width/height, which stay set. Xwayland keeps the surface.
     *   X map    → new WL_SURFACE_SERIAL for that same, still-buffered surface
     *              → associate → no commit follows, because nothing about the
     *              surface's content changed. It stays unmapped forever.
     *
     * That is the Steam tray wedge: viewable in X, associated, buffered, and
     * invisible. Map it here — a buffered surface is by definition mappable,
     * and it is what the commit that never comes would have done. */
    struct wlr_surface *surf = view->xsurface->surface;
    if (!surf->mapped && wlr_surface_has_buffer(surf)) {
        wlr_log(WLR_INFO, "synui: ASSOCIATE-BUFFERED %s (0x%x): surface %p "
                "already has a buffer and is unmapped; mapping it",
                view_title(view) ? view_title(view) : "(no title)",
                view->xsurface->window_id, (void *)surf);
        wlr_surface_map(surf);
    }
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
    /* No-op if xw_unmap already ran, but a surface can be destroyed while
     * unmapped (or never map at all), and the frame carries node.data = view:
     * left behind, it is a dangling view pointer that surface_at() will walk
     * up to and dereference. */
    view_frame_destroy(view);

    wl_list_remove(&view->xw_link);
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
    wl_list_insert(&s->xw_views, &view->xw_link);

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

/* ── X11 primary output ──────────────────────────────────────
 *
 * Wayland has no "primary monitor"; X11 does, and X11 apps lean on it hard.
 * SDL sorts the primary output to the front of its display list, so a game
 * opening on "display 0" — which is most of them, and everything Steam runs
 * — opens on whatever X calls primary. Xwayland marks *nothing* primary by
 * default, which leaves SDL falling back to RandR enumeration order: an
 * arbitrary connector ordering with no relation to how the desk is actually
 * laid out. That is how a fullscreen game ends up on a portrait side monitor.
 *
 * wlroots exposes no API for this and keeps its xwm xcb connection private,
 * so we open a short-lived xcb connection of our own and set it directly.
 * Called on Xwayland ready, on hotplug, and whenever the display panel moves
 * the flag.
 *
 * This talks to X on a worker thread, and never before Xwayland is ready.
 * Both rules exist because breaking either one deadlocks the compositor:
 * Xwayland is one of our own Wayland clients, so any X round-trip made from
 * the main thread blocks the event loop that Xwayland itself needs serviced
 * to answer us. It hangs on both ends, with a black screen and dead input —
 * no crash, no core, nothing in the log. See the ready-flag note below. */
static void primary_push(const char *display, const char *want)
{
    xcb_connection_t *xcb = xcb_connect(display, NULL);
    if (!xcb || xcb_connection_has_error(xcb)) {
        /* Xwayland exited, or never came up — nothing to mark. */
        if (xcb) xcb_disconnect(xcb);
        return;
    }

    const xcb_query_extension_reply_t *ext =
        xcb_get_extension_data(xcb, &xcb_randr_id);
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(xcb)).data;
    if (!ext || !ext->present || !screen) {
        wlr_log(WLR_ERROR, "synui: Xwayland has no RandR; can't set primary");
        xcb_disconnect(xcb);
        return;
    }

    /* Negotiate RandR >= 1.3 before using it. SetOutputPrimary is a 1.3
     * request, and a client that never announces its version is treated as
     * RandR 1.0 — the server then *silently discards* the request. No X
     * error, no reply, the primary just never changes. This call is not
     * optional politeness; without it the code below is a no-op. */
    xcb_randr_query_version_reply_t *ver = xcb_randr_query_version_reply(xcb,
        xcb_randr_query_version(xcb, 1, 3), NULL);
    if (!ver || ver->major_version < 1 ||
        (ver->major_version == 1 && ver->minor_version < 3)) {
        wlr_log(WLR_ERROR, "synui: Xwayland RandR too old for a primary output");
        free(ver);
        xcb_disconnect(xcb);
        return;
    }
    free(ver);

    xcb_randr_get_screen_resources_current_reply_t *res =
        xcb_randr_get_screen_resources_current_reply(xcb,
            xcb_randr_get_screen_resources_current(xcb, screen->root), NULL);
    if (!res) {
        xcb_disconnect(xcb);
        return;
    }

    xcb_randr_output_t *outs =
        xcb_randr_get_screen_resources_current_outputs(res);
    int n = xcb_randr_get_screen_resources_current_outputs_length(res);

    xcb_randr_output_t match = XCB_NONE;
    for (int i = 0; i < n && match == XCB_NONE; i++) {
        xcb_randr_get_output_info_reply_t *info =
            xcb_randr_get_output_info_reply(xcb,
                xcb_randr_get_output_info(xcb, outs[i], res->config_timestamp),
                NULL);
        if (!info) continue;

        /* The RandR name is not NUL-terminated. */
        int len = xcb_randr_get_output_info_name_length(info);
        const char *name = (const char *)xcb_randr_get_output_info_name(info);
        if (len == (int)strlen(want) && !strncmp(name, want, (size_t)len))
            match = outs[i];

        free(info);
    }
    free(res);

    if (match == XCB_NONE) {
        /* Xwayland hadn't mirrored this wl_output yet (hotplug races the X
         * server). Harmless: the next call — ready, or the next panel edit —
         * picks it up. */
        wlr_log(WLR_INFO,
                "synui: no X11 RandR output named %s (yet); primary not set",
                want);
    } else {
        /* Checked: this request fails quietly by design, so ask for the error
         * rather than assuming it landed. */
        xcb_generic_error_t *err = xcb_request_check(xcb,
            xcb_randr_set_output_primary_checked(xcb, screen->root, match));
        if (err) {
            wlr_log(WLR_ERROR,
                    "synui: setting X11 primary to %s failed (X error %u)",
                    want, err->error_code);
            free(err);
        } else {
            wlr_log(WLR_INFO, "synui: X11 primary output = %s", want);
        }
    }

    xcb_disconnect(xcb);
}

/* Hotplug and the display panel can queue several of these in a row, so the
 * pushes are serialised and the newest wins: an older worker that loses the
 * race for the lock must not land its stale primary on top of a newer one. */
static pthread_mutex_t primary_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t primary_gen;    /* requests handed out */
static uint64_t primary_done;   /* newest request already pushed to X */

struct primary_job {
    char    *display;   /* owned */
    char    *want;      /* owned; wl_output name, e.g. "DP-3" */
    uint64_t gen;
};

static void *primary_worker(void *data)
{
    struct primary_job *job = data;

    pthread_mutex_lock(&primary_lock);
    if (job->gen > primary_done) {   /* else: overtaken while we queued */
        primary_done = job->gen;
        primary_push(job->display, job->want);
    }
    pthread_mutex_unlock(&primary_lock);

    free(job->display);
    free(job->want);
    free(job);
    return NULL;
}

void xwayland_apply_primary(syn_server_t *s)
{
    /* Not merely "not up yet": with lazy Xwayland, display_name is set as
     * soon as the socket is *listening*, so this is reachable long before
     * the X server exists — and connecting is what starts it. Do that from
     * the output handler at startup and the compositor wedges: we block on
     * the X handshake, Xwayland blocks on the Wayland handshake we are no
     * longer dispatching. Wait for ready; it fires an apply of its own. */
    if (!s->xwayland_up || !s->xwayland->display_name) return;

    syn_output_t *primary = server_primary_output(s);
    if (!primary) return;

    struct primary_job *job = calloc(1, sizeof *job);
    if (!job) return;

    job->display = strdup(s->xwayland->display_name);
    job->want    = strdup(primary->wlr_output->name);
    if (!job->display || !job->want) goto fail;

    /* Snapshot above, thread below: the worker touches no server state, so it
     * needs no lock on ours and cannot be tripped by a hotplug mid-flight. */
    pthread_mutex_lock(&primary_lock);
    job->gen = ++primary_gen;
    pthread_mutex_unlock(&primary_lock);

    pthread_t tid;
    if (pthread_create(&tid, NULL, primary_worker, job) != 0) {
        wlr_log(WLR_ERROR, "synui: can't spawn X11 primary-output worker");
        goto fail;
    }
    pthread_detach(tid);
    return;

fail:
    free(job->display);
    free(job->want);
    free(job);
}

/* ── Unwedging a window that mapped in X but never associated ─
 *
 * Steam can reach a state where its main X window is IsViewable and fully
 * sized, but no wl_surface is ever associated with it: wlroots therefore never
 * emits map, there is no view in any workspace list, and _NET_CLIENT_LIST is
 * empty. Nothing the compositor draws can bring it back, and steam://open/main
 * cannot either — the client believes its window is already up, so it has
 * nothing to do. Caught live 2026-07-16; measured on the wedge:
 *
 *     XResizeWindow ±1 and back (ConfigureNotify) → no
 *     XClearArea exposures=True (full Expose)     → no
 *     XUnmapWindow + XMapWindow                   → window back in ~1s
 *
 * Only teardown works, and it works because it makes Xwayland destroy the
 * failed surface and build a fresh one. Confirmed in isolation — resize and
 * expose were not run first.
 *
 * This is a workaround, not the fix: the association is being lost somewhere we
 * have not yet found (it is NOT wlroots' xwm.c serial guard — a patched wlroots
 * logging that drop stayed silent across a whole natural wedge). Treat this as
 * a floor under the bug, not a reason to stop looking for it.
 */

struct unwedge_job {
    char        *display;   /* owned */
    xcb_window_t window;
    char        *what;      /* owned; for logging only */
};

/* XSync equivalent: a round trip forces everything queued to actually land.
 * The unmap must be complete before the map is sent, or Xwayland can coalesce
 * the pair and the surface is never torn down — which is the whole point. */
static void unwedge_sync(xcb_connection_t *xcb)
{
    free(xcb_get_input_focus_reply(xcb, xcb_get_input_focus(xcb), NULL));
}

static void *unwedge_worker(void *data)
{
    struct unwedge_job *job = data;

    xcb_connection_t *xcb = xcb_connect(job->display, NULL);
    if (xcb_connection_has_error(xcb)) goto out;

    /* Act only on a window X still calls viewable. A Steam that is genuinely
     * tray-resident has its main window UNMAPPED, and both states look
     * identical from here (no view either way) — so without this check the
     * timer would force open a window the user deliberately closed. */
    xcb_get_window_attributes_reply_t *attr =
        xcb_get_window_attributes_reply(xcb,
            xcb_get_window_attributes(xcb, job->window), NULL);
    if (!attr) goto out_disconnect;   /* window died under us; nothing to fix */
    int viewable = attr->map_state == XCB_MAP_STATE_VIEWABLE;
    free(attr);

    if (!viewable) {
        wlr_log(WLR_INFO,
                "synui: %s (0x%x) is unmapped in X — really in the tray, "
                "not wedged; leaving it alone", job->what, job->window);
        goto out_disconnect;
    }

    wlr_log(WLR_INFO,
            "synui: %s (0x%x) is viewable in X but has no wl_surface — "
            "wedged; forcing an X unmap/map to rebuild it", job->what,
            job->window);

    xcb_unmap_window(xcb, job->window);
    unwedge_sync(xcb);
    sleep(2);                 /* mirrors the recipe proven on the live wedge */
    xcb_map_window(xcb, job->window);
    unwedge_sync(xcb);

out_disconnect:
    xcb_disconnect(xcb);
out:
    free(job->display);
    free(job->what);
    free(job);
    return NULL;
}

void xwayland_unwedge(syn_server_t *s, const char *app_id, const char *title)
{
    if (!s->xwayland_up || !s->xwayland->display_name) return;

    /* Find the one managed X11 window that we have never mapped and that
     * matches both class and title. Class alone is far too broad: every Steam
     * child window (and there are a dozen) reports class "steam" too, and all
     * of them are legitimately unmapped. The title is what separates the main
     * window from its own popups; the worker's IsViewable test is the second
     * gate. */
    xcb_window_t win = XCB_WINDOW_NONE;
    syn_view_t *v, *found = NULL;
    wl_list_for_each(v, &s->xw_views, xw_link) {
        if (!v->is_xwayland || v->mapped || v->override_redirect) continue;
        if (!v->xsurface || !v->xsurface->window_id) continue;
        const char *c = view_app_id(v), *t = view_title(v);
        if (!c || !t || strcmp(c, app_id) || strcmp(t, title)) continue;
        win = v->xsurface->window_id;
        found = v;
        break;
    }
    if (win == XCB_WINDOW_NONE) return;   /* nothing wedged-looking */

    /* Which half of the bug are we in? !mapped has two causes that look
     * identical from everywhere else in the compositor, and nothing has ever
     * distinguished them: the surface was never associated, or it associated
     * fine and no buffer ever arrived. Every hypothesis so far has assumed the
     * former on the strength of an empty _NET_CLIENT_LIST. Measure it. */
    {
        struct wlr_xwayland_surface *xs = found->xsurface;
        struct wlr_surface *surf = xs->surface;
        if (!surf) {
            wlr_log(WLR_INFO, "synui: WEDGE-STATE %s (0x%x): NOT associated "
                    "(xsurface->surface == NULL, serial %" PRIu64 ")",
                    title, win, xs->serial);
        } else {
            wlr_log(WLR_INFO, "synui: WEDGE-STATE %s (0x%x): ASSOCIATED "
                    "(surface %p, mapped %d, has_buffer %d, serial %" PRIu64 ")",
                    title, win, (void *)surf, surf->mapped,
                    wlr_surface_has_buffer(surf), xs->serial);
        }
    }

    struct unwedge_job *job = calloc(1, sizeof *job);
    if (!job) return;
    job->display = strdup(s->xwayland->display_name);
    job->what    = strdup(title);
    job->window  = win;
    if (!job->display || !job->what) goto fail;

    /* Snapshot above, thread below. The worker gets a window id and a string —
     * never a view pointer, which could be freed while it sleeps. It must not
     * run inline either: it does X round trips and a 2s sleep, and blocking the
     * wl_event_loop on X is what deadlocks us (see xwayland_apply_primary). */
    pthread_t tid;
    if (pthread_create(&tid, NULL, unwedge_worker, job) != 0) {
        wlr_log(WLR_ERROR, "synui: can't spawn X11 unwedge worker");
        goto fail;
    }
    pthread_detach(tid);
    return;

fail:
    free(job->display);
    free(job->what);
    free(job);
}

/* ── Server ready: publish DISPLAY, set the X cursor ─────── */
static void xwayland_ready(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_server_t *s = wl_container_of(listener, s, xwayland_ready);
    setenv("DISPLAY", s->xwayland->display_name, 1);
    wlr_log(WLR_INFO, "synui: Xwayland ready on DISPLAY=%s",
            s->xwayland->display_name);

    /* Only now is it safe to speak X: an earlier connect would have *started*
     * lazy Xwayland from inside our own event loop and deadlocked us. */
    s->xwayland_up = 1;
    xwayland_apply_primary(s);

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
