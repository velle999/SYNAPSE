/*
 * session.c — ext-session-lock (swaylock-style screen locking)
 *
 * When a lock client (e.g. swaylock) binds, it becomes the sole recipient of
 * input and its per-output surfaces are drawn above everything else. If the
 * lock client dies without unlocking, the session stays blanked and locked —
 * the secure behaviour mandated by the protocol.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdlib.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_session_lock_v1.h>

#include "synui.h"

/* Per-output lock surface. */
struct syn_lock_surface {
    syn_server_t                        *server;
    struct wlr_session_lock_surface_v1  *lock_surface;
    struct wlr_scene_tree               *tree;
    struct wl_listener                   destroy;
    struct wl_listener                   map;
};

static void lock_keyboard_focus(syn_server_t *s, struct wlr_surface *surface)
{
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
    if (kb)
        wlr_seat_keyboard_notify_enter(s->seat, surface,
                                       kb->keycodes, kb->num_keycodes,
                                       &kb->modifiers);
    else
        wlr_seat_keyboard_notify_enter(s->seat, surface, NULL, 0, NULL);
}

static void lock_surface_map(struct wl_listener *listener, void *data)
{
    (void)data;
    struct syn_lock_surface *ls = wl_container_of(listener, ls, map);
    lock_keyboard_focus(ls->server, ls->lock_surface->surface);
}

static void lock_surface_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct syn_lock_surface *ls = wl_container_of(listener, ls, destroy);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->map.link);
    free(ls);
}

static void lock_new_surface(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, lock_new_surface);
    struct wlr_session_lock_surface_v1 *lock_surface = data;
    struct wlr_output *output = lock_surface->output;

    struct syn_lock_surface *ls = calloc(1, sizeof(*ls));
    ls->server = s;
    ls->lock_surface = lock_surface;
    lock_surface->data = ls;

    ls->tree = wlr_scene_subsurface_tree_create(s->lock_tree,
                                                lock_surface->surface);

    struct wlr_box box;
    wlr_output_layout_get_box(s->output_layout, output, &box);
    wlr_scene_node_set_position(&ls->tree->node, box.x, box.y);
    wlr_session_lock_surface_v1_configure(lock_surface, box.width, box.height);

    ls->destroy.notify = lock_surface_destroy;
    wl_signal_add(&lock_surface->events.destroy, &ls->destroy);
    ls->map.notify = lock_surface_map;
    wl_signal_add(&lock_surface->surface->events.map, &ls->map);
}

/* Re-place lock surfaces after an output geometry change. */
void session_lock_arrange(syn_server_t *s)
{
    if (!s->cur_lock) return;
    struct wlr_session_lock_surface_v1 *lsurf;
    wl_list_for_each(lsurf, &s->cur_lock->surfaces, link) {
        struct syn_lock_surface *ls = lsurf->data;
        if (!ls) continue;
        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, lsurf->output, &box);
        wlr_scene_node_set_position(&ls->tree->node, box.x, box.y);
        wlr_session_lock_surface_v1_configure(lsurf, box.width, box.height);
    }
}

static void session_unlock(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_server_t *s = wl_container_of(listener, s, lock_unlock);
    /* Client authenticated. Clear the locked flag; the black scene is torn
     * down in the lock-destroy handler that follows. */
    s->locked = 0;

    /* Restore focus to a window. */
    if (s->focused_view && s->focused_view->mapped)
        focus_view(s, s->focused_view, view_surface(s->focused_view));
    else
        wlr_seat_keyboard_notify_clear_focus(s->seat);
}

static void session_lock_destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    syn_server_t *s = wl_container_of(listener, s, lock_destroy);

    wl_list_remove(&s->lock_new_surface.link);
    wl_list_remove(&s->lock_unlock.link);
    wl_list_remove(&s->lock_destroy.link);
    s->cur_lock = NULL;

    /* If unlock ran, tear down the black overlay; otherwise the client died
     * without unlocking, so keep the screen blanked (stay secure). */
    if (!s->locked && s->lock_tree) {
        wlr_scene_node_destroy(&s->lock_tree->node);
        s->lock_tree = NULL;
    }
}

static void server_new_session_lock(struct wl_listener *listener, void *data)
{
    syn_server_t *s = wl_container_of(listener, s, new_session_lock);
    struct wlr_session_lock_v1 *lock = data;

    /* Only one lock at a time; reject a second client. */
    if (s->cur_lock) {
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    s->cur_lock = lock;
    s->locked = 1;

    /* A scene tree above everything, with a black backstop covering the whole
     * layout so nothing shows through between lock surfaces. */
    s->lock_tree = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_raise_to_top(&s->lock_tree->node);
    float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    struct wlr_scene_rect *bg =
        wlr_scene_rect_create(s->lock_tree, 32768, 32768, black);
    wlr_scene_node_set_position(&bg->node, -16384, -16384);

    s->lock_new_surface.notify = lock_new_surface;
    wl_signal_add(&lock->events.new_surface, &s->lock_new_surface);
    s->lock_unlock.notify = session_unlock;
    wl_signal_add(&lock->events.unlock, &s->lock_unlock);
    s->lock_destroy.notify = session_lock_destroy;
    wl_signal_add(&lock->events.destroy, &s->lock_destroy);

    /* Drop focus from normal windows and tell the client it may show up. */
    wlr_seat_keyboard_notify_clear_focus(s->seat);
    wlr_session_lock_v1_send_locked(lock);
    wlr_log(WLR_INFO, "synui: session locked");
}

void session_lock_setup(syn_server_t *s)
{
    s->lock_mgr = wlr_session_lock_manager_v1_create(s->display);
    s->new_session_lock.notify = server_new_session_lock;
    wl_signal_add(&s->lock_mgr->events.new_lock, &s->new_session_lock);
}
