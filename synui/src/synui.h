/*
 * synui.h — SynapseOS Wayland Compositor internal header
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#pragma once
#define _GNU_SOURCE

#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

/* ── Version ─────────────────────────────────────────────── */
#define SYNUI_VERSION "0.1.0-synapse"

/* ── Constants ───────────────────────────────────────────── */
#define WORKSPACE_MAX       9
#define WORKSPACE_NAME_LEN  32
#define CMDBAR_MAX_INPUT    256
#define BORDER_WIDTH        2

/* ── synapd IPC ──────────────────────────────────────────── */
#define SYNAPD_SOCKET       "/run/synapd/synapd.sock"

/* ── synapse_kmod syscall numbers ────────────────────────── */
#define NR_AI_CTX_SET       451
#define NR_AI_CTX_GET       452
#define NR_AI_CTX_QUERY     453

/* ── Colors (RGBA float) ─────────────────────────────────── */
#define COLOR_BORDER_NORM   { 0.30f, 0.30f, 0.40f, 1.0f }
#define COLOR_BORDER_FOCUS  { 0.40f, 0.70f, 1.00f, 1.0f }
#define COLOR_BORDER_AI     { 0.00f, 0.90f, 0.80f, 1.0f }
#define COLOR_BORDER_WARN   { 1.00f, 0.30f, 0.20f, 1.0f }
#define COLOR_OVERLAY_BG    { 0.05f, 0.05f, 0.10f, 0.85f }
#define COLOR_BRAND         { 0.00f, 0.85f, 0.75f, 1.0f }

/* ── Enums ───────────────────────────────────────────────── */
typedef enum {
    LAYOUT_TILING = 0,
    LAYOUT_FLOATING,
    LAYOUT_MONOCLE,
    LAYOUT_AI,
} syn_layout_t;

typedef enum {
    WIN_SECURE_NORMAL = 0,
    WIN_SECURE_TRUSTED,
    WIN_SECURE_ALERT,
    WIN_SECURE_DENIED,
} win_security_t;

typedef enum {
    AI_MSG_QUERY_LAYOUT = 1,
    AI_MSG_QUERY_CMD,
    AI_MSG_STATUS_UPDATE,
} syn_ai_msg_type_t;

typedef enum {
    SYNUI_CURSOR_PASSTHROUGH = 0,
    SYNUI_CURSOR_MOVE,
    SYNUI_CURSOR_RESIZE,
} syn_cursor_mode_t;

/* ── Forward declarations ────────────────────────────────── */
typedef struct syn_server   syn_server_t;
typedef struct syn_view     syn_view_t;
typedef struct syn_workspace syn_workspace_t;
typedef struct syn_output   syn_output_t;
typedef struct syn_keyboard syn_keyboard_t;

/* ── AI request / response ───────────────────────────────── */
typedef struct {
    syn_ai_msg_type_t type;
    uint64_t          id;
    char              prompt[1024];
} syn_ai_request_t;

typedef struct {
    uint64_t request_id;
    int      ok;
    char     response[4096];
} syn_ai_response_t;

/* ── Command bar ─────────────────────────────────────────── */
typedef struct {
    int   visible;
    char  input[CMDBAR_MAX_INPUT];
    int   input_len;
    char  response[512];
    int   waiting;
} syn_cmdbar_t;

/* ── Neural overlay ──────────────────────────────────────── */
typedef struct {
    int    visible;
    char   synapd_status[64];
    char   ai_context[256];
    time_t last_update;
} syn_overlay_t;

/* ── AI context attached to a window ─────────────────────── */
typedef struct {
    int  has_ctx;
    char intent[128];
} syn_ai_ctx_t;

/* ── Workspace ───────────────────────────────────────────── */
struct syn_workspace {
    int              index;
    char             name[WORKSPACE_NAME_LEN];
    char             intent[256];
    syn_layout_t     layout;
    int              visible;
    struct wl_list   windows;   /* syn_view_t::link */
};

/* ── View (window) ───────────────────────────────────────── */
struct syn_view {
    struct wl_list          link;       /* in workspace->windows */
    syn_server_t           *server;
    syn_workspace_t        *workspace;

    struct wlr_xdg_surface *xdg_surface;
    struct wlr_scene_tree  *scene_tree;

    int mapped;
    int floating;
    int fullscreen;
    int maximized;
    int x, y, w, h;

    win_security_t   security;
    syn_ai_ctx_t     ai_ctx;

    /* Border scene rects */
    struct wlr_scene_rect *border_top;
    struct wlr_scene_rect *border_bottom;
    struct wlr_scene_rect *border_left;
    struct wlr_scene_rect *border_right;

    /* Listeners */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};

/* ── Output ──────────────────────────────────────────────── */
struct syn_output {
    struct wl_list           link;
    syn_server_t            *server;
    struct wlr_output       *wlr_output;
    struct wlr_scene_output *scene_output;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

/* ── Keyboard ────────────────────────────────────────────── */
struct syn_keyboard {
    struct wl_list       link;
    syn_server_t        *server;
    struct wlr_keyboard *wlr_keyboard;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

/* ── Server (compositor state) ───────────────────────────── */
struct syn_server {
    struct wl_display          *display;
    struct wlr_backend         *backend;
    struct wlr_renderer        *renderer;
    struct wlr_allocator       *allocator;
    struct wlr_compositor      *compositor;
    struct wlr_scene           *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_scene_rect      *bg_rect;

    struct wlr_xdg_shell      *xdg_shell;
    struct wlr_layer_shell_v1  *layer_shell;
    struct wlr_seat            *seat;
    struct wlr_cursor          *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_output_layout   *output_layout;

    double cursor_x, cursor_y;

    struct wl_list  outputs;     /* syn_output_t::link */
    struct wl_list  keyboards;   /* syn_keyboard_t::link */

    syn_workspace_t workspaces[WORKSPACE_MAX];
    int             active_workspace;
    syn_view_t     *focused_view;

    syn_cmdbar_t    cmdbar;
    syn_overlay_t   overlay;

    /* AI thread communication */
    atomic_int      ai_connected;
    int             ai_pipe_req[2];
    int             ai_pipe_resp[2];
    pthread_t       ai_thread;

    /* Listeners */
    struct wl_listener new_output;
    struct wl_listener new_xdg_surface;
    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
};

/* ── synui_main.c ────────────────────────────────────────── */
int  synui_init(syn_server_t *s);
int  synui_run(syn_server_t *s);
void synui_destroy(syn_server_t *s);

/* ── input.c ─────────────────────────────────────────────── */
void input_setup(syn_server_t *s);
void focus_view(syn_server_t *s, syn_view_t *view,
                struct wlr_surface *surface);
syn_view_t *view_at(syn_server_t *s, double lx, double ly,
                    struct wlr_surface **surface, double *sx, double *sy);
void view_set_security(syn_view_t *view, win_security_t state);
void view_update_borders(syn_view_t *view);

/* ── layout.c ────────────────────────────────────────────── */
void layout_apply(syn_server_t *s, syn_workspace_t *ws);
void layout_tile(syn_server_t *s, syn_workspace_t *ws);
void layout_monocle(syn_server_t *s, syn_workspace_t *ws);
void layout_request_ai(syn_server_t *s, syn_workspace_t *ws);
void layout_apply_ai_response(syn_server_t *s, syn_workspace_t *ws,
                               const char *json_response);
void workspace_switch(syn_server_t *s, int index);
void workspace_move_view(syn_server_t *s, syn_view_t *view, int ws_index);

/* ── ai_interface.c ──────────────────────────────────────── */
int  synui_synapd_connect(syn_server_t *s);
int  ai_thread_start(syn_server_t *s);
void ai_thread_send(syn_server_t *s, const syn_ai_request_t *req);
int  ai_thread_poll(syn_server_t *s, syn_ai_response_t *resp);
void cmdbar_show(syn_server_t *s);
void cmdbar_hide(syn_server_t *s);
void cmdbar_key(syn_server_t *s, uint32_t keysym);
void cmdbar_submit(syn_server_t *s);
void overlay_toggle(syn_server_t *s);
void overlay_update(syn_server_t *s);
void overlay_render(syn_server_t *s, struct wlr_renderer *renderer,
                    int width, int height);
