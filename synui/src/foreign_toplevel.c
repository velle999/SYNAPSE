/*
 * foreign_toplevel.c — wlr-foreign-toplevel-management
 *
 * Publishes every mapped, managed window to taskbar/dock clients (waybar's
 * taskbar module, sfwbar, …) and honours their requests: activate raises and
 * focuses (switching workspace if needed), close/fullscreen/maximize map to
 * the same paths the client's own requests use. Minimize has no synui
 * equivalent and is ignored. Title/app-id changes are pushed live.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE

#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>

#include "synui.h"

void foreign_toplevel_update_state(syn_view_t *v)
{
    if (!v || !v->foreign_handle) return;
    wlr_foreign_toplevel_handle_v1_set_activated(
        v->foreign_handle, v->server->focused_view == v);
    wlr_foreign_toplevel_handle_v1_set_maximized(
        v->foreign_handle, v->maximized);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(
        v->foreign_handle, v->fullscreen);
}

static void ft_update_title(syn_view_t *v)
{
    const char *title  = view_title(v);
    const char *app_id = view_app_id(v);
    if (!title)  title  = "";
    if (!app_id) app_id = "";

    if (v->foreign_handle) {
        wlr_foreign_toplevel_handle_v1_set_title(v->foreign_handle, title);
        wlr_foreign_toplevel_handle_v1_set_app_id(v->foreign_handle, app_id);
    }
    if (v->ext_foreign_handle) {
        struct wlr_ext_foreign_toplevel_handle_v1_state state = {
            .title = title, .app_id = app_id,
        };
        wlr_ext_foreign_toplevel_handle_v1_update_state(v->ext_foreign_handle,
                                                        &state);
    }
}

/* ── Taskbar requests ────────────────────────────────────── */
static void ft_handle_activate(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *v = wl_container_of(listener, v, ft_activate);
    syn_server_t *s = v->server;
    if (!v->mapped || s->locked) return;

    if (v->workspace && !workspace_visible(v->workspace))
        workspace_switch(s, v->workspace->index);
    focus_view(s, v, view_surface(v));
}

static void ft_handle_close(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *v = wl_container_of(listener, v, ft_close);
    view_close(v);
}

static void ft_handle_fullscreen(struct wl_listener *listener, void *data)
{
    syn_view_t *v = wl_container_of(listener, v, ft_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
    v->fullscreen = event->fullscreen;
    view_set_fullscreen(v, v->fullscreen);
}

static void ft_handle_maximize(struct wl_listener *listener, void *data)
{
    syn_view_t *v = wl_container_of(listener, v, ft_maximize);
    struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
    v->maximized = event->maximized;
    view_set_maximized(v, v->maximized);
}

/* ── Title / app-id change feeds (per surface type) ──────── */
static void ft_handle_title(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *v = wl_container_of(listener, v, ft_title);
    ft_update_title(v);
}

static void ft_handle_app_id(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_view_t *v = wl_container_of(listener, v, ft_app_id);
    ft_update_title(v);
}

/* ── Map / unmap ─────────────────────────────────────────── */
void foreign_toplevel_map(syn_view_t *v)
{
    syn_server_t *s = v->server;
    /* Menus/tooltips are not windows; don't offer them to taskbars. */
    if (v->override_redirect) return;

    const char *title  = view_title(v);
    const char *app_id = view_app_id(v);

    if (s->foreign_list) {
        struct wlr_ext_foreign_toplevel_handle_v1_state state = {
            .title = title ? title : "", .app_id = app_id ? app_id : "",
        };
        v->ext_foreign_handle =
            wlr_ext_foreign_toplevel_handle_v1_create(s->foreign_list, &state);
        if (v->ext_foreign_handle) v->ext_foreign_handle->data = v;
    }

    if (s->foreign_mgr &&
        (v->foreign_handle =
             wlr_foreign_toplevel_handle_v1_create(s->foreign_mgr))) {
        v->foreign_handle->data = v;
        ft_update_title(v);
        foreign_toplevel_update_state(v);
        syn_output_t *o = server_focused_output(s);
        if (o)
            wlr_foreign_toplevel_handle_v1_output_enter(v->foreign_handle,
                                                        o->wlr_output);

        v->ft_activate.notify = ft_handle_activate;
        wl_signal_add(&v->foreign_handle->events.request_activate, &v->ft_activate);
        v->ft_close.notify = ft_handle_close;
        wl_signal_add(&v->foreign_handle->events.request_close, &v->ft_close);
        v->ft_fullscreen.notify = ft_handle_fullscreen;
        wl_signal_add(&v->foreign_handle->events.request_fullscreen, &v->ft_fullscreen);
        v->ft_maximize.notify = ft_handle_maximize;
        wl_signal_add(&v->foreign_handle->events.request_maximize, &v->ft_maximize);
    }

    if (!v->foreign_handle && !v->ext_foreign_handle) return;

    /* Live title/app-id updates come from the underlying surface. */
    v->ft_title.notify  = ft_handle_title;
    v->ft_app_id.notify = ft_handle_app_id;
    if (v->is_xwayland) {
        wl_signal_add(&v->xsurface->events.set_title, &v->ft_title);
        wl_signal_add(&v->xsurface->events.set_class, &v->ft_app_id);
    } else {
        wl_signal_add(&v->xdg_surface->toplevel->events.set_title, &v->ft_title);
        wl_signal_add(&v->xdg_surface->toplevel->events.set_app_id, &v->ft_app_id);
    }
}

void foreign_toplevel_unmap(syn_view_t *v)
{
    if (!v->foreign_handle && !v->ext_foreign_handle) return;

    wl_list_remove(&v->ft_title.link);
    wl_list_remove(&v->ft_app_id.link);
    if (v->foreign_handle) {
        wl_list_remove(&v->ft_activate.link);
        wl_list_remove(&v->ft_close.link);
        wl_list_remove(&v->ft_fullscreen.link);
        wl_list_remove(&v->ft_maximize.link);
        wlr_foreign_toplevel_handle_v1_destroy(v->foreign_handle);
        v->foreign_handle = NULL;
    }
    if (v->ext_foreign_handle) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(v->ext_foreign_handle);
        v->ext_foreign_handle = NULL;
    }
}

void foreign_toplevel_setup(syn_server_t *s)
{
    s->foreign_mgr  = wlr_foreign_toplevel_manager_v1_create(s->display);
    s->foreign_list = wlr_ext_foreign_toplevel_list_v1_create(s->display, 1);
}
