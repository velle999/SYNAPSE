/*
 * synui.h — SynapseOS Wayland Compositor internal header
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#pragma once
#define _GNU_SOURCE

#include <stdatomic.h>
#include <pthread.h>
#include <sys/types.h>
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
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include <xkbcommon/xkbcommon.h>

/* ── Version ─────────────────────────────────────────────── */
#define SYNUI_VERSION "0.1.0-synapse"

/* ── Constants ───────────────────────────────────────────── */
#define WORKSPACE_MAX       9
#define WORKSPACE_NAME_LEN  32
#define CMDBAR_MAX_INPUT    256
/* Defaults for synuirc `border_width` / `gap`; the live values come from
 * s->config so a SIGHUP reload can change them at runtime. */
#define BORDER_WIDTH_DEFAULT 2
#define GAP_DEFAULT          8

/* ── synapd IPC ──────────────────────────────────────────── */
#define SYNAPD_SOCKET       "/run/synapd/synapd.sock"

/* ── synapse_kmod syscall numbers ────────────────────────── */
#define NR_AI_CTX_SET       451
#define NR_AI_CTX_GET       452
#define NR_AI_CTX_QUERY     453

/* ── Colors (RGBA float) ─────────────────────────────────── */
/* Default border palette ("night drive"): overridable per-role from
 * synuirc via border_color_norm/focus/ai/warn = #rrggbb. */
#define COLOR_BORDER_NORM   { 0.16f, 0.16f, 0.25f, 1.0f }  /* #2a2a40 dim indigo   */
#define COLOR_BORDER_FOCUS  { 1.00f, 0.16f, 0.43f, 1.0f }  /* #ff296d neon magenta */
#define COLOR_BORDER_AI     { 0.02f, 0.85f, 0.91f, 1.0f }  /* #05d9e8 neon cyan    */
#define COLOR_BORDER_WARN   { 1.00f, 0.21f, 0.14f, 1.0f }  /* #ff3524 alarm red    */
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

struct wlr_swapchain;   /* render/swapchain.h — only referenced by pointer */
struct syn_effects;     /* effects.c private state */

/* ── AI request / response ───────────────────────────────── */
typedef struct {
    syn_ai_msg_type_t type;
    uint64_t          id;
    char              prompt[1024];
} syn_ai_request_t;

typedef struct {
    uint64_t          request_id;   /* echoes request id; for LAYOUT = ws index */
    syn_ai_msg_type_t type;         /* echoes request type, for dispatch */
    int               ok;
    char              response[4096];
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

/* ── Welcome menu (render.c owns the table, input.c executes) ── */
typedef struct {
    const char *label;    /* shown in the menu */
    const char *hint;     /* keybinding hint column */
    const char *action;   /* bind action executed on Enter */
} syn_welcome_entry_t;

extern const syn_welcome_entry_t synui_welcome_menu[];
extern const int                 synui_welcome_menu_len;

/* ── Display settings panel (dispcfg.c) ──────────────────── */
#define DISPCFG_MAX_OUTPUTS 8

typedef struct {
    int visible;
    int selected;   /* index into order[] */
    int column;     /* 0 = row (left→right), 1 = column (top→bottom) */
    int count;
    syn_output_t *order[DISPCFG_MAX_OUTPUTS];  /* arrangement order */
    char status[96];   /* last action / error, shown in the panel */
} syn_dispcfg_t;

/* ── Keybinding (table-driven; syntax in config.c) ───────── */
#define SYN_BINDS_MAX        96
#define SYN_BIND_ACTION_LEN  24
#define SYN_BIND_ARG_LEN     104

typedef struct {
    uint32_t     mods;      /* WLR_MODIFIER_* mask (LOGO/SHIFT/CTRL/ALT) */
    xkb_keysym_t sym;       /* stored lower-cased */
    char         action[SYN_BIND_ACTION_LEN];
    char         arg[SYN_BIND_ARG_LEN];
} syn_bind_t;

/* ── Configuration ───────────────────────────────────────── */
#define SYN_AUTOSTART_MAX 8

typedef struct {
    char  terminal[64];
    char  autostart[SYN_AUTOSTART_MAX][128];
    int   autostart_count;
    int   border_width;
    int   gap;
    float master_factor;
    int   ai_layout;
    int   ai_ctx_decor;
    int   start_overlay;

    /* Border colors (RGBA 0..1) by window role; defaults COLOR_BORDER_*. */
    float border_color_norm[4];
    float border_color_focus[4];
    float border_color_ai[4];
    float border_color_warn[4];

    /* GLES post-process effects (effects.c). `effects` gates the pass;
     * it silently stays off on non-GLES2 renderers (pixman VMs).
     * Strengths are 0..1; 0 disables the individual effect. */
    int   effects;
    float effect_scanline;
    float effect_curvature;
    float effect_aberration;
    float effect_glitch;     /* strength of the alert/close glitch; 0 = off */

    /* Keyboard: XKB keymap (empty = XKB_DEFAULT_* env / system default). */
    char  xkb_rules[64];
    char  xkb_model[64];
    char  xkb_layout[64];
    char  xkb_variant[64];
    char  xkb_options[256];
    int   repeat_rate;       /* key repeats per second */
    int   repeat_delay;      /* ms before repeat starts */

    /* libinput device options; tri-states are -1 = leave device default. */
    int   tap_to_click;
    int   natural_scroll;
    int   left_handed;
    float accel_speed;       /* -1.0 .. 1.0 */
    int   accel_speed_set;

    syn_bind_t binds[SYN_BINDS_MAX];
    int        bind_count;
} syn_config_t;

/* ── Workspace ───────────────────────────────────────────── */
struct syn_workspace {
    int              index;
    char             name[WORKSPACE_NAME_LEN];
    char             intent[256];
    syn_layout_t     layout;
    int              visible;        /* shown on its output right now */
    syn_output_t    *output;         /* output this workspace lives on; NULL =
                                        unassigned (never shown / its output
                                        was unplugged) */
    float            master_factor;  /* master column width, 0.10–0.90 */
    struct wl_list   windows;   /* syn_view_t::link */
};

/* ── View (window) ───────────────────────────────────────── */
struct syn_view {
    struct wl_list          link;       /* in workspace->windows */
    syn_server_t           *server;
    syn_workspace_t        *workspace;

    /* A view wraps either an xdg_toplevel (Wayland) or an xwayland_surface
     * (X11). Exactly one of these is set; is_xwayland selects which. */
    int                          is_xwayland;
    int                          override_redirect;  /* X11 OR: menus/tooltips */
    struct wlr_xdg_surface      *xdg_surface;
    struct wlr_xwayland_surface *xsurface;
    struct wlr_scene_tree       *scene_tree;

    int mapped;
    int floating;
    int fullscreen;
    int maximized;
    int x, y, w, h;

    win_security_t   security;
    syn_ai_ctx_t     ai_ctx;

    /* foreign-toplevel handles (taskbars/docks); NULL while unmapped.
     * zwlr carries state + requests; ext is the newer list-only protocol. */
    struct wlr_foreign_toplevel_handle_v1     *foreign_handle;
    struct wlr_ext_foreign_toplevel_handle_v1 *ext_foreign_handle;
    struct wl_listener ft_activate;
    struct wl_listener ft_close;
    struct wl_listener ft_fullscreen;
    struct wl_listener ft_maximize;
    struct wl_listener ft_title;
    struct wl_listener ft_app_id;

    /* Border scene rects */
    struct wlr_scene_rect *border_top;
    struct wlr_scene_rect *border_bottom;
    struct wlr_scene_rect *border_left;
    struct wlr_scene_rect *border_right;

    /* Listeners (shared: xdg + xwayland reuse map/unmap/destroy/request_*) */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    /* xwayland-only */
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener request_configure;
    struct wl_listener request_activate;
};

/* ── Layer-shell surface (panels, bars, wallpaper, launchers) ── */
typedef struct syn_layer_surface {
    struct wl_list                     link;    /* in syn_output::layer_surfaces */
    syn_server_t                      *server;
    syn_output_t                      *output;
    struct wlr_layer_surface_v1       *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene;   /* wlroots geometry helper */
    enum zwlr_layer_shell_v1_layer     layer;   /* cached; may change on commit */

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener new_popup;
} syn_layer_surface_t;

/* ── Output ──────────────────────────────────────────────── */
struct syn_output {
    struct wl_list           link;
    syn_server_t            *server;
    struct wlr_output       *wlr_output;
    struct wlr_scene_output *scene_output;

    int                      active_workspace; /* workspace shown on this output */

    struct wl_list           layer_surfaces;  /* syn_layer_surface_t::link */
    struct wlr_box           usable_area;     /* full box minus exclusive zones */

    /* effects.c: offscreen swapchain the scene renders into when the GLES
     * post-process pass is active (NULL until first effects frame). */
    struct wlr_swapchain    *fx_swapchain;

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

/* Non-keyboard input device (pointer/touch/tablet), tracked so a SIGHUP
 * config reload can reapply libinput options to it. */
typedef struct syn_input_dev {
    struct wl_list           link;
    struct wlr_input_device *dev;
    struct wl_listener       destroy;
} syn_input_dev_t;

/* ── Server (compositor state) ───────────────────────────── */
struct syn_server {
    struct wl_display          *display;
    /* SIGINT/SIGTERM/SIGHUP sources; the event loop doesn't free sources it
     * still holds at destroy, so we remove them ourselves in synui_destroy. */
    struct wl_event_source     *sigint_src;
    struct wl_event_source     *sigterm_src;
    struct wl_event_source     *sighup_src;
    struct wlr_backend         *backend;
    struct wlr_renderer        *renderer;
    struct wlr_allocator       *allocator;
    struct syn_effects         *effects;   /* GLES post-process (effects.c); NULL = unavailable */
    struct wlr_compositor      *compositor;
    struct wlr_scene           *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_scene_rect      *bg_rect;

    struct wlr_xdg_shell      *xdg_shell;
    struct wlr_layer_shell_v1  *layer_shell;
    struct wlr_xwayland        *xwayland;
    struct wlr_seat            *seat;

    /* Phase F: output & session management. */
    struct wlr_idle_notifier_v1        *idle_notifier;
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit;
    struct wlr_output_manager_v1        *output_mgr;
    struct wlr_output_power_manager_v1  *power_mgr;
    struct wlr_session_lock_manager_v1  *lock_mgr;
    struct wlr_session_lock_v1          *cur_lock;   /* active lock, if any */
    struct wlr_scene_tree               *lock_tree;  /* lock surfaces, top-most */
    int                                  locked;     /* session is locked */
    int                                  idle_inhibitors;  /* active inhibitor count */

    /* Phase G: input completeness. */
    struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
    struct wlr_pointer_constraints_v1      *pointer_constraints;
    struct wlr_pointer_constraint_v1       *active_constraint;  /* on the pointer-focused surface */
    struct wlr_pointer_gestures_v1         *pointer_gestures;
    int                                     touch_devices;

    /* Phase H: ecosystem protocols. */
    struct wlr_foreign_toplevel_manager_v1  *foreign_mgr;
    struct wlr_ext_foreign_toplevel_list_v1 *foreign_list;
    struct wlr_gamma_control_manager_v1     *gamma_mgr;
    struct wlr_scene_tree                   *drag_icon_tree;  /* topmost; follows cursor */

    /* Scene-graph z-order (bottom→top): bg_rect, layer[BACKGROUND],
     * layer[BOTTOM], window_tree, layer[TOP], layer[OVERLAY], then UI. */
    struct wlr_scene_tree     *window_tree;    /* xdg toplevels + borders */
    struct wlr_scene_tree     *layer_tree[4];  /* zwlr_layer_shell_v1_layer */
    struct wlr_cursor          *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_output_layout   *output_layout;

    double cursor_x, cursor_y;

    struct wl_list  outputs;     /* syn_output_t::link */
    struct wl_list  keyboards;   /* syn_keyboard_t::link */
    struct wl_list  input_devs;  /* syn_input_dev_t::link — non-keyboard
                                    devices, so a config reload can reapply
                                    libinput options */

    /* Workspaces are global; each output shows one of them
     * (syn_output_t::active_workspace). "The" active workspace — what
     * keybinds, new windows and the overlay act on — is the one on the
     * focused output: server_active_workspace(). */
    syn_workspace_t workspaces[WORKSPACE_MAX];
    syn_view_t     *focused_view;

    syn_cmdbar_t    cmdbar;
    syn_overlay_t   overlay;
    syn_config_t    config;

    /* Interactive move/resize grab state (Super + mouse drag). */
    syn_cursor_mode_t cursor_mode;      /* PASSTHROUGH / MOVE / RESIZE */
    syn_view_t       *grabbed_view;
    double            grab_x, grab_y;   /* MOVE: cursor→view offset; RESIZE: cursor anchor */
    struct wlr_box    grab_geobox;      /* RESIZE: view geometry at grab start */
    uint32_t          resize_edges;     /* RESIZE: WLR_EDGE_* being dragged */

    /* UI scene nodes (render.c) */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } cmdbar_ui;

    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } overlay_ui;

    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
        int shown;
        int selected;   /* highlighted synui_welcome_menu entry */
    } welcome_ui;

    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } dispcfg_ui;

    syn_dispcfg_t   dispcfg;

    /* AI thread communication */
    atomic_int      ai_connected;
    int             ai_disabled;        /* --no-ai: AI thread never starts */
    int             ai_pipe_req[2];
    int             ai_pipe_resp[2];
    pthread_t       ai_thread;
    int             ai_running;         /* thread created; join on shutdown */
    /*
     * Reassembly buffer for the response pipe. A syn_ai_response_t exceeds
     * PIPE_BUF, so a write is not atomic and the non-blocking reader in the
     * frame loop may see it in fragments — accumulate here across frames.
     */
    struct {
        char   buf[sizeof(syn_ai_response_t)];
        size_t have;
    } ai_resp_rx;

    /* synguard security-verdict feed: a thread subscribes to the synguard
     * broadcast socket and forwards records over sec_pipe; the frame loop
     * drains it and colours the matching window's border. */
    int        sec_pipe[2];
    pthread_t  sec_thread;
    int        sec_disabled;
    int        sec_running;   /* thread created; join on shutdown */
    atomic_int sec_stop;      /* tells the feed thread to exit */
    atomic_int sec_fd;        /* feed socket, so stop can shutdown() it */

    /* Set once teardown begins so output_destroy (fired by the backend during
     * shutdown, after the scene graph is gone) skips its re-layout path. */
    int       shutting_down;

    /* Listeners */
    struct wl_listener new_output;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_listener new_layer_surface;
    struct wl_listener new_xwayland_surface;
    struct wl_listener xwayland_ready;
    struct wl_listener new_decoration;
    struct wl_listener new_idle_inhibitor;
    struct wl_listener output_mgr_apply;
    struct wl_listener output_mgr_test;
    struct wl_listener output_power_set_mode;
    struct wl_listener new_session_lock;
    struct wl_listener lock_new_surface;
    struct wl_listener lock_unlock;
    struct wl_listener lock_destroy;
    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    struct wl_listener new_constraint;
    struct wl_listener touch_down;
    struct wl_listener touch_up;
    struct wl_listener touch_motion;
    struct wl_listener touch_frame;
    struct wl_listener touch_cancel;
    struct wl_listener tablet_axis;
    struct wl_listener tablet_proximity;
    struct wl_listener tablet_tip;
    struct wl_listener tablet_button;
    struct wl_listener swipe_begin;
    struct wl_listener swipe_update;
    struct wl_listener swipe_end;
    struct wl_listener pinch_begin;
    struct wl_listener pinch_update;
    struct wl_listener pinch_end;
    struct wl_listener hold_begin;
    struct wl_listener hold_end;
    struct wl_listener request_set_primary_selection;
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener drag_destroy;   /* linked only while a drag is live */
    struct wl_listener gamma_set;
};

/* ── synui_main.c ────────────────────────────────────────── */
int  synui_init(syn_server_t *s);
int  synui_run(syn_server_t *s);
void synui_destroy(syn_server_t *s);
void synui_config_reload(syn_server_t *s);   /* SIGHUP: reparse + reapply */
/* The output the user is currently working on: the one under the cursor,
 * else the one holding the focused window, else the first connected output. */
syn_output_t *server_focused_output(syn_server_t *s);
/* The workspace on the focused output (never NULL; falls back to ws 0). */
syn_workspace_t *server_active_workspace(syn_server_t *s);
/* Is this workspace currently shown on its output? */
int workspace_visible(syn_workspace_t *ws);
/* Layout/usable box of a specific output (1920x1080 fallback). */
void output_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box);
void output_usable_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box);
/* Layout-space box of the focused output (falls back to 1920x1080 @ 0,0). */
void server_output_box(syn_server_t *s, struct wlr_box *box);
/* Like server_output_box but minus layer-shell exclusive zones (for tiling). */
void server_usable_box(syn_server_t *s, struct wlr_box *box);

/* ── layer.c ─────────────────────────────────────────────── */
void layer_shell_init(syn_server_t *s);            /* create global + wire signal */
void layer_arrange_output(syn_output_t *output);   /* place layers, update usable */
void layer_output_destroy(syn_output_t *output);   /* close surfaces on a dead output */

/* ── view accessors (xdg / xwayland agnostic) ────────────── */
struct wlr_surface *view_surface(syn_view_t *v);
const char *view_app_id(syn_view_t *v);   /* xdg app_id / X11 class */
const char *view_title(syn_view_t *v);
pid_t       view_pid(syn_view_t *v);
void        view_close(syn_view_t *v);
void        view_set_activated(syn_view_t *v, int activated);
void        view_set_maximized(syn_view_t *v, int maximized);
void        view_set_fullscreen(syn_view_t *v, int fullscreen);

/* ── xwayland.c ──────────────────────────────────────────── */
void xwayland_setup(syn_server_t *s);   /* create server; no-op if unavailable */

/* ── output_mgmt.c ───────────────────────────────────────── */
void output_mgmt_setup(syn_server_t *s);        /* output-management + DPMS */
void output_mgmt_update(syn_server_t *s);        /* push current config to clients */
/* Reflow everything after output geometry changed: layer surfaces (which
 * re-tile each output's workspace), lock surfaces, compositor UI, and
 * broadcast the new config to management clients. */
void output_layout_changed(syn_server_t *s);

/* ── dispcfg.c ───────────────────────────────────────────── */
void dispcfg_show(syn_server_t *s);
void dispcfg_hide(syn_server_t *s);
void dispcfg_toggle(syn_server_t *s);
/* Modal key handling while the panel is open. Unmodified keys are absorbed
 * (navigation/rotate/reorder); modified combos fall through to the bind
 * table. Returns 1 if the key was consumed. */
int  dispcfg_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* Output hotplug while the panel is open: reseed the arrangement order
 * (dropping dangling pointers) and re-render. No-op when hidden. */
void dispcfg_outputs_changed(syn_server_t *s);

/* ── session.c ───────────────────────────────────────────── */
void session_lock_setup(syn_server_t *s);        /* ext-session-lock */
void session_lock_arrange(syn_server_t *s);      /* re-place lock surfaces */

/* ── foreign_toplevel.c ──────────────────────────────────── */
void foreign_toplevel_setup(syn_server_t *s);        /* create the manager */
void foreign_toplevel_map(syn_view_t *v);            /* publish a mapped view */
void foreign_toplevel_unmap(syn_view_t *v);          /* retract (sends closed) */
void foreign_toplevel_update_state(syn_view_t *v);   /* activated/max/fullscreen */

/* ── constraints.c ───────────────────────────────────────── */
void constraints_setup(syn_server_t *s);  /* pointer-constraints + relative-pointer */
/* (De)activate the constraint owned by the surface now holding pointer focus
 * (surface may be NULL — deactivates any active constraint). */
void constraints_focus_surface(syn_server_t *s, struct wlr_surface *surface);
/* Clamp a relative motion against the active constraint (confined) or absorb
 * it entirely — returns 1 if the cursor must not move (locked pointer). */
int  constraints_apply_motion(syn_server_t *s, double *dx, double *dy);

/* ── input.c ─────────────────────────────────────────────── */
void input_setup(syn_server_t *s);
void input_reload_config(syn_server_t *s);   /* reapply keymap/repeat/libinput */
void pointer_update_focus(syn_server_t *s, uint32_t time_msec);
void focus_view(syn_server_t *s, syn_view_t *view,
                struct wlr_surface *surface);
syn_view_t *view_at(syn_server_t *s, double lx, double ly,
                    struct wlr_surface **surface, double *sx, double *sy);
struct wlr_surface *surface_at(syn_server_t *s, double lx, double ly,
                               syn_view_t **view_out, double *sx, double *sy);
void view_set_security(syn_view_t *view, win_security_t state);
void view_update_borders(syn_view_t *view);

/* ── layout.c ────────────────────────────────────────────── */
void layout_apply(syn_server_t *s, syn_workspace_t *ws);
void view_apply_fullscreen(syn_server_t *s, syn_view_t *view, int fs);
void workspace_focus_first(syn_server_t *s, syn_workspace_t *ws);
void layout_tile(syn_server_t *s, syn_workspace_t *ws);
void layout_monocle(syn_server_t *s, syn_workspace_t *ws);
void view_resize(syn_view_t *view, int x, int y, int w, int h);
void layout_float_place(syn_server_t *s, syn_view_t *view);
void layout_move_in_stack(syn_server_t *s, syn_view_t *view, int dir);
void layout_adjust_master(syn_server_t *s, syn_workspace_t *ws, float delta);
void layout_request_ai(syn_server_t *s, syn_workspace_t *ws);
void layout_apply_ai_response(syn_server_t *s, syn_workspace_t *ws,
                               const char *json_response);
int  parse_ai_layout_line(const char *line, char *app_id, size_t app_len,
                          float *x, float *y, float *w, float *h);
void workspace_switch(syn_server_t *s, int index);
void workspace_move_view(syn_server_t *s, syn_view_t *view, int ws_index);

/* ── ai_interface.c ──────────────────────────────────────── */
int  ai_thread_start(syn_server_t *s);
void ai_thread_stop(syn_server_t *s);    /* join the thread, close the pipes */
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
void execute_ai_action(syn_server_t *s, const char *response);

/* ── secfeed.c ───────────────────────────────────────────── */
void secfeed_start(syn_server_t *s);     /* subscribe to synguard verdicts */
void secfeed_stop(syn_server_t *s);      /* join the thread, close the pipe */
void secfeed_dispatch(syn_server_t *s);  /* drain feed, colour windows (frame loop) */

/* ── config.c ────────────────────────────────────────────── */
void synui_config_load(syn_config_t *cfg);

/* ── render.c ────────────────────────────────────────────── */
void synui_ui_init(syn_server_t *s);
void synui_render_welcome(syn_server_t *s);
void synui_welcome_hide(syn_server_t *s);
void synui_render_cmdbar(syn_server_t *s);
void synui_render_overlay(syn_server_t *s);
void synui_render_dispcfg(syn_server_t *s);
