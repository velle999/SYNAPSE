#pragma once
#define _GNU_SOURCE
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#define SYNUI_VERSION "0.1.0-synapse"

struct synui_server {
    struct wl_display          *display;
    struct wlr_backend         *backend;
    struct wlr_renderer        *renderer;
    struct wlr_allocator       *allocator;
    struct wlr_scene           *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_xdg_shell       *xdg_shell;
    struct wl_listener          new_xdg_toplevel;
    struct wl_list              toplevels;

    struct wlr_cursor          *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener          cursor_motion;
    struct wl_listener          cursor_motion_absolute;
    struct wl_listener          cursor_button;
    struct wl_listener          cursor_axis;
    struct wl_listener          cursor_frame;

    struct wlr_seat            *seat;
    struct wl_listener          request_cursor;
    struct wl_listener          request_set_selection;

    struct wlr_output_layout   *output_layout;
    struct wl_list              outputs;
    struct wl_listener          new_output;

    struct wl_listener          new_input;
    struct wl_list              keyboards;
};

struct synui_output {
    struct wl_list              link;
    struct synui_server        *server;
    struct wlr_output          *wlr_output;
    struct wlr_scene_output    *scene_output;
    struct wl_listener          frame;
    struct wl_listener          request_state;
    struct wl_listener          destroy;
};

struct synui_toplevel {
    struct wl_list              link;
    struct synui_server        *server;
    struct wlr_xdg_toplevel    *xdg_toplevel;
    struct wlr_scene_tree      *scene_tree;
    struct wl_listener          map;
    struct wl_listener          unmap;
    struct wl_listener          commit;
    struct wl_listener          destroy;
    struct wl_listener          request_move;
    struct wl_listener          request_resize;
    struct wl_listener          request_maximize;
};

struct synui_keyboard {
    struct wl_list              link;
    struct synui_server        *server;
    struct wlr_keyboard        *wlr_keyboard;
    struct wl_listener          modifiers;
    struct wl_listener          key;
    struct wl_listener          destroy;
};

/* cursor modes */
enum synui_cursor_mode {
    SYNUI_CURSOR_PASSTHROUGH,
    SYNUI_CURSOR_MOVE,
    SYNUI_CURSOR_RESIZE,
};

int  synui_server_init(struct synui_server *server);
void synui_server_run(struct synui_server *server);
void synui_server_destroy(struct synui_server *server);
