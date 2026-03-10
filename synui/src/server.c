#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include "../include/synui.h"

/* ── Output handling ─────────────────────────────────────── */
static void output_frame(struct wl_listener *listener, void *data) {
    struct synui_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene *scene = output->server->scene;
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(scene, output->wlr_output);
    wlr_scene_output_commit(scene_output, NULL);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
    struct synui_output *output =
        wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    struct synui_output *output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct synui_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct synui_output *output = calloc(1, sizeof(*output));
    output->server     = server;
    output->wlr_output = wlr_output;

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    struct wlr_output_layout_output *layout_output =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);
    output->scene_output =
        wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout,
                                       layout_output, output->scene_output);
}

/* ── Toplevel (window) handling ──────────────────────────── */
static void toplevel_map(struct wl_listener *listener, void *data) {
    struct synui_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
    /* focus the new window */
    struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
    struct wlr_seat *seat = toplevel->server->seat;
    wlr_seat_keyboard_notify_enter(seat, surface,
        NULL, 0, NULL);
}

static void toplevel_unmap(struct wl_listener *listener, void *data) {
    struct synui_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    wl_list_remove(&toplevel->link);
}

static void toplevel_destroy(struct wl_listener *listener, void *data) {
    struct synui_toplevel *toplevel =
        wl_container_of(listener, toplevel, destroy);
    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->destroy.link);
    free(toplevel);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct synui_server *server =
        wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;

    struct synui_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    toplevel->server      = server;
    toplevel->xdg_toplevel = xdg_toplevel;
    toplevel->scene_tree  =
        wlr_scene_xdg_surface_create(&server->scene->tree,
                                     xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;
    xdg_toplevel->base->data = toplevel->scene_tree;

    toplevel->map.notify    = toplevel_map;
    toplevel->unmap.notify  = toplevel_unmap;
    toplevel->destroy.notify = toplevel_destroy;
    wl_signal_add(&xdg_toplevel->base->surface->events.map,    &toplevel->map);
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap,  &toplevel->unmap);
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);
}

/* ── Cursor handling ─────────────────────────────────────── */
static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct synui_server *server =
        wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base,
                    event->delta_x, event->delta_y);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct synui_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
                             event->x, event->y);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct synui_server *server =
        wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wlr_seat_pointer_notify_button(server->seat,
        event->time_msec, event->button, event->state);
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct synui_server *server =
        wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    struct synui_server *server =
        wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void server_request_cursor(struct wl_listener *listener, void *data) {
    struct synui_server *server =
        wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client =
        server->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client)
        wlr_cursor_set_surface(server->cursor, event->surface,
                               event->hotspot_x, event->hotspot_y);
}

static void server_request_set_selection(struct wl_listener *listener, void *data) {
    struct synui_server *server =
        wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source,
                           event->serial);
}

/* ── Init / Run / Destroy ────────────────────────────────── */
int synui_server_init(struct synui_server *server) {
    server->display = wl_display_create();
    if (!server->display) return -1;

    server->backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server->display), NULL);
    if (!server->backend) return -1;

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) return -1;
    wlr_renderer_init_wl_display(server->renderer, server->display);

    server->allocator = wlr_allocator_autocreate(server->backend,
                                                  server->renderer);
    if (!server->allocator) return -1;

    wlr_compositor_create(server->display, 5, server->renderer);
    wlr_subcompositor_create(server->display);
    wlr_data_device_manager_create(server->display);

    server->output_layout = wlr_output_layout_create(server->display);
    wl_list_init(&server->outputs);
    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);

    server->scene = wlr_scene_create();
    server->scene_layout = wlr_scene_attach_output_layout(server->scene,
                                                           server->output_layout);

    server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
    wl_list_init(&server->toplevels);
    server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel,
                  &server->new_xdg_toplevel);

    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
    server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

    server->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
    server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute,
                  &server->cursor_motion_absolute);
    server->cursor_button.notify = server_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);
    server->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
    server->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

    server->seat = wlr_seat_create(server->display, "seat0");
    server->request_cursor.notify = server_request_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor,
                  &server->request_cursor);
    server->request_set_selection.notify = server_request_set_selection;
    wl_signal_add(&server->seat->events.request_set_selection,
                  &server->request_set_selection);

    const char *socket = wl_display_add_socket_auto(server->display);
    if (!socket) return -1;
    setenv("WAYLAND_DISPLAY", socket, 1);

    if (!wlr_backend_start(server->backend)) return -1;

    wlr_log(WLR_INFO, "synui: compositor ready on %s", socket);
    return 0;
}

void synui_server_run(struct synui_server *server) {
    wl_display_run(server->display);
}

void synui_server_destroy(struct synui_server *server) {
    wl_display_destroy_clients(server->display);
    wlr_scene_node_destroy(&server->scene->tree.node);
    wlr_xcursor_manager_destroy(server->cursor_mgr);
    wlr_cursor_destroy(server->cursor);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    wlr_backend_destroy(server->backend);
    wl_display_destroy(server->display);
}
