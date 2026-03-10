/*
 * synui.h — SynapseOS Wayland Compositor
 *
 * synui is a Wayland compositor built on wlroots that integrates
 * directly with the SynapseOS AI stack. It is NOT a traditional
 * desktop environment — it is a tiling compositor where the AI
 * is a first-class layout and interaction participant.
 *
 * Key capabilities:
 *
 *   AI LAYOUT ENGINE
 *     The compositor can ask synapd to suggest window arrangements
 *     based on what the user is doing. "I'm writing code and running
 *     tests" → editor left, terminal right, monitor bottom.
 *
 *   NEURAL OVERLAY
 *     A persistent semi-transparent overlay rendered by the compositor
 *     displays AI context: what synguard is watching, what synapd knows,
 *     system health. Toggled with Super+A.
 *
 *   INTENT WORKSPACE
 *     Workspaces can be named by natural language intent.
 *     'syn workspace "writing a paper"' creates a workspace,
 *     synapd suggests which apps to open and arranges them.
 *
 *   SECURE FOCUS INDICATOR
 *     When synguard flags a process as suspicious, synui draws
 *     a red border around its window. When an app has AI_CTX set,
 *     synui shows the intent as a subtle window decoration.
 *
 *   VOICE/TEXT COMMAND BAR
 *     Super+Space opens a command bar. Natural language input is
 *     sent to synapd. Response can open apps, move windows, or
 *     answer questions inline.
 *
 * Architecture:
 *   Built on wlroots + tinywl pattern.
 *   Single-process compositor; AI calls are async (dedicated thread).
 *   Renders with OpenGL ES 2.0 via wlroots renderer.
 *   Extended Wayland protocols: xdg-shell, layer-shell, xdg-decoration.
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */
#pragma once

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include <pixman.h>
#include <pthread.h>
#include <stdatomic.h>

/* ── Version ─────────────────────────────────────────────── */
#define SYNUI_VERSION  "0.1.0-synapse"

/* ── synapd IPC ──────────────────────────────────────────── */
#define SYNAPD_SOCKET  "/run/synapd/synapd.sock"

/* ── AI_CTX syscall numbers ──────────────────────────────── */
#define NR_AI_CTX_SET    451
#define NR_AI_CTX_QUERY  453

/* ── Colors (Synapse brand palette) ─────────────────────── */
/* RGBA floats for wlroots renderer */
#define COLOR_BRAND        { 0.12f, 0.87f, 0.99f, 1.0f }  /* electric cyan */
#define COLOR_BORDER_FOCUS { 0.12f, 0.87f, 0.99f, 1.0f }  /* focused window */
#define COLOR_BORDER_NORM  { 0.18f, 0.20f, 0.25f, 1.0f }  /* normal window  */
#define COLOR_BORDER_WARN  { 0.98f, 0.36f, 0.12f, 1.0f }  /* synguard alert */
#define COLOR_BORDER_AI    { 0.54f, 0.33f, 0.99f, 1.0f }  /* AI_CTX active  */
#define COLOR_OVERLAY_BG   { 0.05f, 0.07f, 0.10f, 0.82f } /* neural overlay */
#define COLOR_TEXT_BRAND   { 0.12f, 0.87f, 0.99f, 1.0f }
#define COLOR_TEXT_DIM     { 0.45f, 0.50f, 0.55f, 1.0f }
#define COLOR_TRANSPARENT  { 0.0f,  0.0f,  0.0f,  0.0f }

/* ── Layout modes ────────────────────────────────────────── */
typedef enum {
    LAYOUT_TILING   = 0,   /* automatic tiling (default) */
    LAYOUT_FLOATING = 1,   /* traditional floating WM */
    LAYOUT_MONOCLE  = 2,   /* one window maximized */
    LAYOUT_AI       = 3,   /* AI-suggested layout */
} syn_layout_mode_t;

/* ── Window security state ───────────────────────────────── */
typedef enum {
    WIN_SECURE_NORMAL    = 0,
    WIN_SECURE_MONITORED = 1,  /* synguard is watching this process */
    WIN_SECURE_ALERT     = 2,  /* synguard flagged this process */
    WIN_SECURE_DENIED    = 3,  /* synguard killed this process */
} win_security_t;

/* ── AI context state of a window ───────────────────────── */
typedef struct {
    int      has_ctx;          /* process called AI_CTX_SET */
    char     intent[256];      /* what the process says it's doing */
    int      sched_class;      /* AI_SCHED_* */
} win_ai_ctx_t;

/* ── Workspace ───────────────────────────────────────────── */
#define WORKSPACE_MAX  9
#define WORKSPACE_NAME_LEN 64

typedef struct syn_workspace {
    int             index;
    char            name[WORKSPACE_NAME_LEN];
    char            intent[256];      /* natural language intent */
    syn_layout_mode_t layout;
    struct wl_list  windows;          /* list of syn_view_t */
    int             visible;
} syn_workspace_t;

/* ── View (a mapped window) ──────────────────────────────── */
typedef struct syn_view {
    struct wl_list       link;         /* workspace->windows */
    struct wlr_xdg_surface *xdg_surface;
    struct wlr_scene_tree  *scene_tree;

    /* Geometry */
    int x, y, w, h;
    int floating;

    /* Workspace */
    syn_workspace_t *workspace;

    /* State */
    int              mapped;
    int              maximized;
    int              fullscreen;

    /* Security decoration */
    win_security_t   security;
    win_ai_ctx_t     ai_ctx;

    /* Event listeners */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;

    /* Border scene nodes */
    struct wlr_scene_rect *border_top;
    struct wlr_scene_rect *border_bottom;
    struct wlr_scene_rect *border_left;
    struct wlr_scene_rect *border_right;

} syn_view_t;

/* ── Output (monitor) ────────────────────────────────────── */
typedef struct syn_output {
    struct wl_list        link;
    struct wlr_output    *wlr_output;
    struct syn_server    *server;
    struct wlr_scene_output *scene_output;

    struct wl_listener    frame;
    struct wl_listener    request_state;
    struct wl_listener    destroy;
} syn_output_t;

/* ── Keyboard ────────────────────────────────────────────── */
typedef struct syn_keyboard {
    struct wl_list           link;
    struct syn_server       *server;
    struct wlr_keyboard     *wlr_keyboard;
    struct wl_listener       modifiers;
    struct wl_listener       key;
    struct wl_listener       destroy;
} syn_keyboard_t;

/* ── Command bar state ───────────────────────────────────── */
#define CMDBAR_MAX_INPUT  512

typedef struct {
    int    visible;
    char   input[CMDBAR_MAX_INPUT];
    int    input_len;
    char   response[2048];
    int    response_len;
    int    waiting;           /* waiting for AI response */
    int    cursor_pos;
} syn_cmdbar_t;

/* ── Neural overlay state ────────────────────────────────── */
typedef struct {
    int    visible;
    char   synapd_status[256];
    char   synguard_status[256];
    char   ai_context[512];
    char   last_alert[256];
    int    request_total;
    float  load_pct;
    time_t last_update;
} syn_overlay_t;

/* ── AI thread message types ─────────────────────────────── */
typedef enum {
    AI_MSG_QUERY_LAYOUT   = 1,
    AI_MSG_QUERY_CMD      = 2,
    AI_MSG_STATUS_UPDATE  = 3,
    AI_MSG_WORKSPACE_HINT = 4,
} syn_ai_msg_type_t;

typedef struct {
    syn_ai_msg_type_t  type;
    char               prompt[512];
    uint64_t           id;
} syn_ai_request_t;

typedef struct {
    uint64_t           request_id;
    char               response[2048];
    int                ok;
} syn_ai_response_t;

/* ── Main server state ───────────────────────────────────── */
typedef struct syn_server {
    struct wl_display            *display;
    struct wlr_backend           *backend;
    struct wlr_renderer          *renderer;
    struct wlr_allocator         *allocator;
    struct wlr_scene             *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_output_layout     *output_layout;
    struct wlr_compositor        *compositor;
    struct wlr_xdg_shell         *xdg_shell;
    struct wlr_seat              *seat;
    struct wlr_cursor            *cursor;
    struct wlr_xcursor_manager   *cursor_mgr;
    struct wlr_layer_shell_v1    *layer_shell;

    /* Collections */
    struct wl_list   outputs;    /* syn_output_t */
    struct wl_list   keyboards;  /* syn_keyboard_t */

    /* Workspaces */
    syn_workspace_t  workspaces[WORKSPACE_MAX];
    int              active_workspace;

    /* Focus */
    syn_view_t      *focused_view;

    /* Cursor state */
    double           cursor_x, cursor_y;
    syn_view_t      *grabbed_view;
    double           grab_x, grab_y;
    struct wlr_box   grab_geobox;
    enum { GRAB_NONE, GRAB_MOVE, GRAB_RESIZE } grab_mode;
    uint32_t         resize_edges;

    /* UI state */
    syn_cmdbar_t     cmdbar;
    syn_overlay_t    overlay;

    /* AI thread */
    pthread_t        ai_thread;
    int              ai_pipe_req[2];   /* compositor → AI thread */
    int              ai_pipe_resp[2];  /* AI thread → compositor */
    atomic_int       ai_connected;

    /* Event listeners */
    struct wl_listener new_output;
    struct wl_listener new_xdg_surface;
    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_abs;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;

} syn_server_t;

/* ── Function declarations ───────────────────────────────── */

/* Server init/run */
int  synui_init(syn_server_t *s);
int  synui_run(syn_server_t *s);
void synui_destroy(syn_server_t *s);

/* Layout engine */
void layout_apply(syn_server_t *s, syn_workspace_t *ws);
void layout_tile(syn_server_t *s, syn_workspace_t *ws);
void layout_monocle(syn_server_t *s, syn_workspace_t *ws);
void layout_request_ai(syn_server_t *s, syn_workspace_t *ws);

/* Focus */
void focus_view(syn_server_t *s, syn_view_t *view, struct wlr_surface *surface);
syn_view_t *view_at(syn_server_t *s, double lx, double ly,
                    struct wlr_surface **surface, double *sx, double *sy);

/* Workspace */
void workspace_switch(syn_server_t *s, int index);
void workspace_move_view(syn_server_t *s, syn_view_t *view, int ws_index);

/* View decorations */
void view_update_borders(syn_view_t *view);
void view_set_security(syn_view_t *view, win_security_t state);

/* Command bar */
void cmdbar_show(syn_server_t *s);
void cmdbar_hide(syn_server_t *s);
void cmdbar_key(syn_server_t *s, uint32_t keysym);
void cmdbar_submit(syn_server_t *s);

/* Neural overlay */
void overlay_toggle(syn_server_t *s);
void overlay_update(syn_server_t *s);
void overlay_render(syn_server_t *s, struct wlr_renderer *renderer,
                    int width, int height);

/* AI thread */
int  ai_thread_start(syn_server_t *s);
void ai_thread_send(syn_server_t *s, const syn_ai_request_t *req);
int  ai_thread_poll(syn_server_t *s, syn_ai_response_t *resp);

/* IPC */
int  synui_synapd_connect(syn_server_t *s);
int  synui_synapd_query(syn_server_t *s, const char *prompt,
                        char *out, size_t out_len);
