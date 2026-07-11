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
#include <cairo.h>

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
/* One line of synapd's recent-activity feed (from SYN_MSG_CONTEXT_GET). */
#define OVERLAY_ACTIVITY_MAX  8

typedef struct {
    int    visible;
    char   synapd_status[64];
    char   ai_context[256];
    time_t last_update;

    /* Live snapshot of what synapd is doing, refreshed by synapd_mon.c while
     * the overlay is visible. mon_online reflects the last poll, not just
     * socket connectivity (synapd_status above). */
    int           mon_online;
    char          model[16];        /* "loaded" | "loading" | "none" */
    unsigned long requests;         /* total served since start */
    unsigned long active;           /* queries in flight right now */
    unsigned      ctx_used;         /* context tokens used */
    unsigned      ctx_window;       /* context window size (tokens) */
    char          activity[OVERLAY_ACTIVITY_MAX][100];  /* recent events */
    int           activity_n;
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

/* ── Wallpaper picker (wppick.c) ─────────────────────────── */
#define WPPICK_FOUND_MAX 64   /* images the browse scan will list */
#define WPPICK_ROWS      10   /* rows visible at once; the rest scroll */

/* ── Display settings panel (dispcfg.c) ──────────────────── */
#define DISPCFG_MAX_OUTPUTS 8

typedef struct {
    int visible;
    int selected;   /* index into order[] */
    int count;
    syn_output_t *order[DISPCFG_MAX_OUTPUTS];  /* panel list, reading order
                                                 * (grid_y then grid_x) */
    char status[96];   /* last action / error, shown in the panel */
} syn_dispcfg_t;

/* ── CRT filter panel (filters.c) ────────────────────────── */
/* Panel rows, in display order. FILTER_ROW_ENABLED toggles the master switch;
 * the rest each map to one syn_config_t effect_* strength, in the same order. */
typedef enum {
    FILTER_ROW_ENABLED = 0,
    FILTER_ROW_SCANLINE,
    FILTER_ROW_CURVATURE,
    FILTER_ROW_ABERRATION,
    FILTER_ROW_GLITCH,
    FILTER_ROW_COUNT,
} syn_filter_row_t;

typedef struct {
    int  visible;
    int  selected;     /* syn_filter_row_t */
    int  dirty;        /* edited since the last save — drives the panel hint */
    char status[96];
} syn_filters_t;

/* ── Power saving panel + idle state machine (power.c) ───── */
/* Panel rows, in display order. POWER_ROW_ENABLED toggles the master switch;
 * the rest each map to one syn_config_t timeout, in the same order. */
typedef enum {
    POWER_ROW_ENABLED = 0,
    POWER_ROW_DIM,
    POWER_ROW_BLANK,
    POWER_ROW_LOCK,
    POWER_ROW_SUSPEND,
    POWER_ROW_COUNT,
} syn_power_row_t;

typedef struct {
    int visible;
    int selected;      /* syn_power_row_t */
    int dirty;         /* edited since the last save — drives the panel hint */
    char status[96];

    /* Live stage state. Each timer is armed from the last input event with
     * its own timeout and fires at most once per idle period; activity
     * disarms, reverses what fired, and rearms. */
    struct wl_event_source *t_dim, *t_blank, *t_lock, *t_suspend;
    int dimmed;
    int blanked;
    int locked;        /* we spawned the locker and have seen no activity since */
    uint32_t last_arm_ms;  /* rearm throttle — see power_notify_activity */
} syn_power_t;

/* ── Game mode (game.c) ───────────────────────────────────── */
typedef struct {
    int  active;        /* engaged right now */
    int  forced;        /* Super+G: -1 forced off, 0 auto, +1 forced on */
    int  ai_suspended;  /* we stopped synapd and owe it a restart */
    char app[64];       /* app_id that triggered it (for the log) */
} syn_game_t;

/* ── GPU telemetry (gpu.c) ───────────────────────────────── */
#define SYN_GPU_MAX       4    /* devices reported in the panel */
#define SYN_GPU_PROC_MAX  256  /* pids in the per-process VRAM table */

/* One device's last sample. util/temp_c/power_w are -1 when the back end
 * cannot report that figure (common on amdgpu); vram_total_kb 0 means the
 * whole VRAM reading is unavailable, not that the card has no memory. */
typedef struct {
    char          name[64];
    int           util;            /* percent, or -1 */
    int           temp_c;          /* or -1 */
    int           power_w;         /* or -1 */
    unsigned long vram_used_kb;
    unsigned long vram_total_kb;
    char          sysfs[256];      /* amdgpu back end only; empty under NVML */
} syn_gpu_t;

/* ── Task manager (taskmgr.c) ────────────────────────────── */
#define TASKMGR_MAX_PROCS 512  /* sampled; the table shows the top rows */
#define TASKMGR_ROWS      14   /* rows visible at once */

typedef enum {
    TM_SORT_CPU = 0,
    TM_SORT_MEM,
    TM_SORT_GPU,
    TM_SORT_PID,
} syn_tm_sort_t;

/* A kill is armed, not sent: the panel shows a confirmation line and only
 * signals once the user answers it. */
typedef enum {
    TM_CONFIRM_NONE = 0,
    TM_CONFIRM_TERM,
    TM_CONFIRM_KILL,
} syn_tm_confirm_t;

typedef struct {
    pid_t              pid;
    uid_t              uid;
    char               name[40];
    double             cpu;        /* percent of one core, top-style */
    unsigned long      rss_kb;
    unsigned long      vram_kb;    /* 0 = none, or the back end cannot say */
    unsigned long long jiffies;    /* utime+stime; next poll's CPU% baseline */
    int                has_window; /* owns a window synui is managing */
} syn_tm_proc_t;

typedef struct {
    int              visible;
    int              selected;     /* index into procs[] */
    pid_t            sel_pid;      /* what selected *means* across a re-sort */
    int              scroll;       /* first row drawn */
    syn_tm_sort_t    sort;
    int              own_only;     /* 'u': hide other users' processes */
    char             status[96];

    syn_tm_confirm_t confirm;
    pid_t            confirm_pid;  /* pinned when the confirmation is armed, so
                                    * a re-sort under it cannot redirect the
                                    * signal at a different process */
    char             confirm_name[40];

    syn_tm_proc_t    procs[TASKMGR_MAX_PROCS];
    int              n;

    /* Previous poll, for the CPU% deltas. */
    struct { pid_t pid; unsigned long long jiffies; } prev[TASKMGR_MAX_PROCS];
    int                prev_n;
    unsigned long long prev_total, prev_busy;

    /* System totals, refreshed each poll. */
    double        cpu_pct;
    unsigned long mem_used_kb, mem_total_kb;
    unsigned long swap_used_kb, swap_total_kb;

    struct wl_event_source *timer;  /* 1 Hz, armed only while visible */
} syn_taskmgr_t;

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

/* Install prefix for bundled assets (wallpaper.png, kanji_atlas.png).
 * Normally injected by meson (-DSYNUI_DATADIR); fall back to the default. */
#ifndef SYNUI_DATADIR
#define SYNUI_DATADIR "/usr/share/synui"
#endif

typedef enum {
    SYN_WALLPAPER_FILL = 0,   /* cover, cropped (default) */
    SYN_WALLPAPER_FIT,        /* contain, letterboxed */
    SYN_WALLPAPER_STRETCH,    /* non-uniform scale to exact size */
    SYN_WALLPAPER_CENTER,     /* 1:1, cropped/padded */
} syn_wallpaper_mode_t;

/* Which wallpaper backend paints the background. IMAGE is the static
 * wallpaper.c path (config `wallpaper` = a file path, or empty = solid
 * bg_color); MATRIX is the animated GLES2 rain (matrix.c). Selected in
 * synuirc (`wallpaper = matrix`) or live via the wppick.c picker. */
typedef enum {
    SYN_WP_SRC_IMAGE = 0,     /* static image / solid (wallpaper.c) */
    SYN_WP_SRC_MATRIX,        /* animated kanji rain (matrix.c) */
} syn_wallpaper_src_t;

/* Which screen edge the dock lives on (dock.c). BOTTOM/TOP render a
 * horizontal bar; LEFT/RIGHT render a vertical column. Set in synuirc
 * (`dock_edge`) or by dragging the dock to another edge. */
typedef enum {
    SYN_DOCK_EDGE_BOTTOM = 0,
    SYN_DOCK_EDGE_TOP,
    SYN_DOCK_EDGE_LEFT,
    SYN_DOCK_EDGE_RIGHT,
} syn_dock_edge_t;

/* Dock right-click context-menu actions (dock.c / render.c). */
typedef enum {
    SYN_DOCKACT_PIN = 0,   /* add app_id to the pinned set */
    SYN_DOCKACT_UNPIN,     /* remove it from the pinned set */
    SYN_DOCKACT_OPEN,      /* launch (.desktop Exec) — not currently running */
    SYN_DOCKACT_NEWWIN,    /* launch another instance — already running */
    SYN_DOCKACT_QUIT,      /* close every mapped window of this app_id */
} syn_dockact_t;
#define SYN_DOCKMENU_MAX 6

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

    /* Background image (wallpaper.c); empty path = no wallpaper (solid
     * bg_color shows instead). Ignored when wallpaper_src == MATRIX. */
    char                  wallpaper[256];
    syn_wallpaper_mode_t  wallpaper_mode;
    syn_wallpaper_src_t   wallpaper_src;   /* IMAGE (default) or MATRIX */

    /* macOS-style auto-hide dock (dock.c). Mirrored on every output; never
     * reserves an exclusive zone (see syn_output::dock's comment) — hidden
     * it takes zero layout space, shown it floats above window content. */
    int   dock_enabled;         /* default 1 */
    int   dock_height;          /* px thickness, default 64 */
    int   dock_hover_margin;    /* px trigger strip at the dock's edge, default 4 */
    syn_dock_edge_t dock_edge;  /* which screen edge, default BOTTOM */
#define DOCK_PIN_MAX 16
#define GAME_EXCLUDE_MAX 16
    /* Runtime-mutable pinned set: seeded from synuirc `dock_pin`, then
     * overridden by ~/.config/synui/dock.state and edited live via the dock
     * context menu (dock_pin_toggle). */
    char  dock_pin[DOCK_PIN_MAX][128];
    int   dock_pin_count;

    /* Idle power saving (power.c). Each stage is an idle timeout in seconds,
     * measured from the last input event; 0 disables that stage. They are
     * independent, not cumulative — a stage fires when the seat has been idle
     * that long, whatever the other stages did. Runtime-mutable from the
     * Super+P panel and persisted to ~/.config/synui/power.state. */
    int   power_enabled;        /* master switch, default 1 */
    int   power_dim;            /* fade a dim overlay over the scene */
    int   power_blank;          /* DPMS the outputs off */
    int   power_lock;           /* run power_lock_cmd */
    int   power_suspend;        /* run power_suspend_cmd; default 0 (never) */
    char  power_lock_cmd[192];

    /* Wi-Fi / network configuration UI. nmtui in a terminal by default: synui
     * has no text entry to type a passphrase into, so there is nothing native
     * to point this at yet. Overridable for non-NetworkManager setups. */
    char  network_cmd[192];
    char  power_suspend_cmd[192];

    /* Game mode (game.c). A fullscreen XWayland client is taken to be a game
     * unless its app_id matches game_exclude — that list is what keeps a
     * fullscreen Firefox video from suspending the AI. */
    int   game_mode;            /* master switch, default 1 */
    int   game_suspend_ai;      /* stop synapd while a game runs, default 1 */
    int   game_inhibit_idle;    /* hold off dim/blank/lock, default 1 */
    char  game_exclude[GAME_EXCLUDE_MAX][64];
    int   game_exclude_count;
    char  game_ai_stop_cmd[192];
    char  game_ai_start_cmd[192];

    syn_bind_t binds[SYN_BINDS_MAX];
    int        bind_count;
} syn_config_t;

/* ── Dock entry (dock.c) ──────────────────────────────────── */
/* One pinned and/or running app, shared across every output's mirrored
 * dock — rendering differs only by which output's box the tree sits in
 * (see syn_output::dock), so hit-box coordinates here are dock-canvas-local
 * and valid for every output alike. */
#define DOCK_MAX_ENTRIES 32
typedef struct {
    char app_id[128];
    int  pinned;             /* came from synuirc dock_pin */
    int  running;            /* >=1 mapped view with this app_id */
    syn_view_t *primary_view;   /* most-recently-focused running view; NULL if not running */
    int  x, y, w, h;          /* icon hit-box, dock-canvas-local; set by dock_render() */
    double anim_start;        /* CLOCK_MONOTONIC secs of last click; 0 = idle. Drives
                               * the press-pop scale in dock_render (see DOCK_CLICK_ANIM_SECS) */
} syn_dock_entry_t;

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
    int minimized;
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
    struct wl_listener ft_minimize;
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
    struct wl_listener request_minimize;   /* ICCCM iconify; xdg-shell has no equivalent */
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

    /* Logical cell in the dispcfg arrangement grid (not pixels — see
     * dispcfg_rechain). Seeded from connection order in server_new_output;
     * moved with Shift+arrows in the display panel. */
    int                      grid_x, grid_y;

    /* The "primary" monitor, in the X11 RandR sense: the one Xwayland
     * reports with the primary flag. Wayland has no such concept, but X11
     * toolkits do — SDL puts the primary output first in its display list,
     * so a game that opens on "display 0" lands here. With no primary set,
     * SDL falls back to RandR enumeration order, which is arbitrary (it
     * followed connector order, not desk layout) and is why fullscreen
     * games could open on whichever monitor happened to be listed first.
     * At most one output has this set; xwayland_apply_primary() pushes it
     * to the X server. Persisted in outputs.conf as primary=1. */
    int                      primary;

    struct wl_list           layer_surfaces;  /* syn_layer_surface_t::link */
    struct wlr_box           usable_area;     /* full box minus exclusive zones */

    /* effects.c: offscreen swapchain the scene renders into when the GLES
     * post-process pass is active (NULL until first effects frame). */
    struct wlr_swapchain    *fx_swapchain;

    /* wallpaper.c: this output's painted background, parented under
     * server->wallpaper_tree; NULL if no wallpaper is configured/decoded. */
    struct wlr_scene_buffer *wallpaper_buf;

    /* matrix.c: the animated wallpaper's per-frame GPU buffer + swapchain,
     * a sibling of wallpaper_buf under wallpaper_tree. Only one of the two
     * is ever populated (chosen by config.wallpaper_src). NULL when the
     * matrix wallpaper isn't active on this output. */
    struct wlr_scene_buffer *matrix_buf;
    struct wlr_swapchain    *matrix_swapchain;

    /* dock.c: this output's own mirror of the auto-hide dock. A top-level
     * scene tree (sibling of the welcome/overlay/dispcfg UI trees, not
     * parented under window_tree/layer_tree) so a shown dock always floats
     * above window content without needing an exclusive-zone reservation —
     * hidden, it reserves no layout space at all. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_buffer *icons_buf;
        int      shown;            /* currently slid in on this output */
        double   slide_progress;   /* 0 = fully hidden, 1 = fully shown */
        double   hover_since;      /* CLOCK_MONOTONIC secs cursor entered the
                                     * trigger strip; 0 = not hovering */
        double   unhover_since;    /* secs cursor left the dock area; 0 = still
                                     * over it. Debounces the slide-out. */
        double   last_tick;        /* CLOCK_MONOTONIC secs of the last anim
                                     * step; 0 while settled (no slide). */
    } dock;

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
    struct wlr_scene_tree      *wallpaper_tree;  /* wallpaper.c; above bg_rect,
                                                   below layer[BACKGROUND] */
    struct {
        cairo_surface_t *src;   /* decoded source image; NULL = none/failed */
    } wallpaper;

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

    /* Idle inhibits held over D-Bus (org.freedesktop.ScreenSaver — screensaver.c).
     * Counted separately from idle_inhibitors, which belongs to the wlr
     * idle-inhibit protocol: mixing them would make the Wayland counter lie
     * about how many protocol objects exist. Use idle_inhibited() to ask the
     * only question anyone actually has, which is whether *anything* is
     * holding the screen on. */
    int                                  screensaver_inhibitors;

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

    /* virtual-keyboard-v1: lets a client (wtype) synthesize key events that
     * flow through the exact same server_new_keyboard()/keyboard_handle_key
     * path a physical keyboard takes — used by the waybar menu to trigger
     * the activity overview without a bespoke IPC channel. */
    struct wlr_virtual_keyboard_manager_v1  *virtual_keyboard_mgr;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener vkb_mgr_destroy;

    /* dock.c: shared entry model (pinned + running apps), rendered into
     * every output's own syn_output::dock tree. */
    syn_dock_entry_t dock_entries[DOCK_MAX_ENTRIES];
    int              dock_entry_count;

    /* dock.c: drag-to-reposition state. While active on an output, that
     * output's dock floats under the cursor and stays shown; on release it
     * snaps to the nearest screen edge. */
    struct {
        int           active;   /* a press landed on a dock bar background */
        int           moved;    /* passed the start threshold → really dragging */
        syn_output_t *output;   /* output the drag started on */
        double        start_x, start_y;   /* press point (layout coords) */
        double        float_x, float_y;   /* current bar top-left while floating */
    } dock_drag;

    /* dock.c / render.c: right-click context menu for a dock icon. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } dockmenu_ui;
    struct {
        int  visible;
        char app_id[128];                 /* snapshot (entries rebuild live) */
        syn_dockact_t actions[SYN_DOCKMENU_MAX];
        int  action_count;
        int  selected;                    /* hovered item, -1 = none */
        int  x, y, w, h;                  /* menu rect, layout coords */
    } dockmenu;

    /* Scene-graph z-order (bottom→top): bg_rect, wallpaper_tree,
     * layer[BACKGROUND], layer[BOTTOM], window_tree, layer[TOP],
     * layer[OVERLAY], then UI. */
    struct wlr_scene_tree     *window_tree;    /* xdg toplevels + borders */
    struct wlr_scene_tree     *layer_tree[4];  /* zwlr_layer_shell_v1_layer */
    struct wlr_cursor          *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_output_layout   *output_layout;

    double cursor_x, cursor_y;

    /* Implicit pointer grab: the cursor→surface-local offset of the surface
     * that took the button-down, captured while it still had pointer focus.
     * Lets motion during the grab stay in that surface's coordinate space
     * even once the cursor wanders off it. See pointer_update_focus(). */
    double ptr_grab_off_x, ptr_grab_off_y;

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

    /* Power saving panel (Super+P) plus the full-layout dim overlay the
     * dim stage fades in. `dim` lives in its own tree so it can be raised
     * above every window without disturbing the panel's own stacking. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
        struct wlr_scene_tree   *dim_tree;
        struct wlr_scene_rect   *dim;
    } power_ui;

    syn_power_t     power;
    syn_game_t      game;

    /* CRT filter panel (Super+E) — sliders for the effects.c strengths. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } filters_ui;

    syn_filters_t   filters;

    /* Task manager panel (Super+T) — process table + resource overview. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } taskmgr_ui;

    syn_taskmgr_t   taskmgr;

    /* GPU telemetry (gpu.c), refreshed by the task manager's poll. gpu_n is 0
     * when no supported GPU was found — every consumer must handle that. */
    syn_gpu_t       gpu[SYN_GPU_MAX];
    int             gpu_n;
    struct { pid_t pid; unsigned long vram_kb; } gpu_proc[SYN_GPU_PROC_MAX];
    int             gpu_proc_n;

    /* Wallpaper selector panel (wppick.c) — a compositor-drawn modal picker
     * (Super+W) for switching between the built-in wallpapers live. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } wppick_ui;
    struct {
        int visible;
        int selected;   /* row index: built-ins first, then found[] */
        int scroll;     /* first row drawn (the found list can be long) */

        /* Images found on disk by the "browse" scan (wppick_scan), shown below
         * the built-ins so you can pick your own wallpaper without editing
         * synuirc. Rescanned every time the panel opens, so an image dropped
         * into ~/Pictures shows up without restarting the compositor. */
        char found[WPPICK_FOUND_MAX][256];
        int  found_count;
    } wppick;

    /* matrix.c: animated-wallpaper GLES2 state; NULL when unavailable
     * (non-GLES2 renderer) or never initialized. */
    struct syn_matrix *matrix;

    /* AI thread communication */
    atomic_int      ai_connected;
    atomic_int      ai_synapd_fd;       /* live synapd socket, so stop can
                                          * shutdown() it and unblock a
                                          * mid-query recv() (see sec_fd) */
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

    /* synapd activity monitor: a thread polls synapd's STATUS + CONTEXT_GET
     * while the neural overlay is open and forwards a snapshot over
     * synmon_pipe; an event-loop fd source drains it and refreshes the
     * overlay. Same shutdown-fd discipline as the feed above. */
    int        synmon_pipe[2];
    pthread_t  synmon_thread;
    int        synmon_running;
    atomic_int synmon_stop;    /* tells the monitor thread to exit */
    atomic_int synmon_fd;      /* poll socket, so stop can shutdown() it */
    atomic_int synmon_want;    /* 1 while the overlay is visible → poll fast */
    struct wl_event_source *synmon_src;  /* pipe read-end in the event loop */

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
    int xwayland_up;    /* the X server is actually running (ready has fired),
                         * not merely socket-listening — see the lazy-start
                         * deadlock note in xwayland_apply_primary() */
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

/* The effective X11 primary: the output flagged ->primary, else the largest.
 * NULL only when no outputs are connected. */
syn_output_t *server_primary_output(syn_server_t *s);
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
/* Hide/show an output's TOP-layer panels so a fullscreen view can cover them. */
void layer_update_occlusion(syn_server_t *s, syn_output_t *o);
void layer_update_occlusion_all(syn_server_t *s);

/* ── view accessors (xdg / xwayland agnostic) ────────────── */
struct wlr_surface *view_surface(syn_view_t *v);
const char *view_app_id(syn_view_t *v);   /* xdg app_id / X11 class */
const char *view_title(syn_view_t *v);
pid_t       view_pid(syn_view_t *v);
void        view_close(syn_view_t *v);
void        view_set_activated(syn_view_t *v, int activated);
void        view_set_maximized(syn_view_t *v, int maximized);
void        view_set_fullscreen(syn_view_t *v, int fullscreen);
void        view_set_minimized(syn_view_t *v, int minimized);

/* ── xwayland.c ──────────────────────────────────────────── */
void xwayland_setup(syn_server_t *s);   /* create server; no-op if unavailable */

/* Push the syn_output_t marked ->primary to the X server as the RandR primary
 * output. Safe to call from the event loop: a no-op until Xwayland is ready,
 * and the X round-trips themselves run on a worker thread — doing them inline
 * deadlocks the compositor against Xwayland. Call whenever the primary changes
 * (display panel) or a monitor is hotplugged. */
void xwayland_apply_primary(syn_server_t *s);

/* ── output_mgmt.c ───────────────────────────────────────── */
void output_mgmt_setup(syn_server_t *s);        /* output-management + DPMS */
void output_mgmt_update(syn_server_t *s);        /* push current config to clients */
/* Reflow everything after output geometry changed: layer surfaces (which
 * re-tile each output's workspace), lock surfaces, compositor UI, and
 * broadcast the new config to management clients. */
void output_layout_changed(syn_server_t *s);

/* ── output_persist.c ────────────────────────────────────── */
/* Restore this connector's saved mode/transform/scale/position (from
 * ~/.config/synui/outputs.conf) if one exists. Returns the layout entry on
 * success (as wlr_output_layout_add_auto() would), or NULL if there's
 * nothing saved / the backend rejected it — caller should fall back to
 * auto-placement. */
struct wlr_output_layout_output *output_persist_apply(syn_server_t *s,
                                                       syn_output_t *output);
/* Snapshot every connected output's current mode/transform/scale/position
 * to disk, merged with saved entries for disconnected outputs. Called from
 * output_layout_changed() after any real apply. */
void output_persist_save(syn_server_t *s);

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
void view_apply_minimized(syn_server_t *s, syn_view_t *view, int minimized);
/* Scale a sub-native fullscreen X11 client up to fill its output (xwayland.c);
 * no-op for xdg, override-redirect, multi-surface or already-filling clients. */
void view_fullscreen_rescale(syn_view_t *view);
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

/* ── synapd_mon.c ────────────────────────────────────────── */
void synmon_start(syn_server_t *s);      /* poll synapd status/activity */
void synmon_stop(syn_server_t *s);       /* join the thread, close the pipe */
void synmon_set_active(syn_server_t *s, int on);  /* poll fast while overlay open */

/* ── config.c ────────────────────────────────────────────── */
void synui_config_load(syn_config_t *cfg);

/* ── render.c ────────────────────────────────────────────── */
void synui_ui_init(syn_server_t *s);
void synui_render_welcome(syn_server_t *s);
void synui_welcome_hide(syn_server_t *s);
void synui_render_cmdbar(syn_server_t *s);
void synui_render_overlay(syn_server_t *s);
void synui_render_dispcfg(syn_server_t *s);

/* Shared cairo↔wlr_buffer bridge, reused by wallpaper.c: draw into a cairo
 * surface with an offscreen wlr_buffer backing, then attach/replace it as a
 * scene node's buffer. */
struct wlr_buffer *create_cairo_buf(int w, int h, cairo_t **cr_out);
void set_scene_buffer(struct wlr_scene_buffer **node,
                       struct wlr_scene_tree *parent, struct wlr_buffer *buf);
void cairo_begin(cairo_t *cr);   /* clear to transparent + set default font */

/* ── wallpaper.c ─────────────────────────────────────────── */
void wallpaper_init(syn_server_t *s);             /* create wallpaper_tree, decode initial config */
void wallpaper_output_created(syn_output_t *o);   /* paint this output (server_new_output) */
void wallpaper_output_destroy(syn_output_t *o);   /* destroy this output's buffer (output_destroy) */
void wallpaper_relayout(syn_server_t *s);         /* repaint all outputs (output_layout_changed) */
void wallpaper_reload(syn_server_t *s);           /* re-decode + repaint from current config */

/* Persisted wallpaper choice (~/.config/synui/wallpaper.state). Written by
 * the wppick.c picker; applied over the parsed config on every load so a
 * GUI choice survives restart without rewriting synuirc. */
void wallpaper_state_save(syn_server_t *s);
void wallpaper_state_load(syn_config_t *cfg);

/* ── Power saving (power.c) ──────────────────────────────── */
/* Create the idle timers and arm them from the current config. */
void power_init(syn_server_t *s);
void power_finish(syn_server_t *s);
/* Called from every input event (via notify_activity) and whenever an idle
 * inhibitor appears/disappears: undoes any stage that has fired and rearms. */
void power_notify_activity(syn_server_t *s);
/* Re-arm after the config changed (panel edit, config reload). */
void power_reload(syn_server_t *s);

/* ── GPU telemetry (gpu.c) ───────────────────────────────── */
/* Probes NVML (dlopen) then amdgpu sysfs; leaves gpu_n at 0 if neither is
 * there, which is a normal outcome, not a failure. */
void gpu_init(syn_server_t *s);
void gpu_finish(syn_server_t *s);
/* Refresh s->gpu[] and the per-pid VRAM table. Called from the task manager's
 * poll, so it only runs while that panel is open. */
void gpu_sample(syn_server_t *s);
/* VRAM charged to one pid at the last gpu_sample; 0 if it uses none, or if the
 * back end cannot attribute VRAM per process (amdgpu). */
unsigned long gpu_proc_vram_kb(syn_server_t *s, pid_t pid);

/* ── Task manager (taskmgr.c) ────────────────────────────── */
void taskmgr_init(syn_server_t *s);
void taskmgr_finish(syn_server_t *s);
void taskmgr_show(syn_server_t *s);
void taskmgr_hide(syn_server_t *s);
void taskmgr_toggle(syn_server_t *s);
/* Modal key handling while the panel is open, as in power_key: unmodified keys
 * are absorbed, Super+… still reaches the global binds. Returns 1 if handled. */
int  taskmgr_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* One /proc + GPU poll into s->taskmgr. Public so the panel can resample
 * immediately after a sort or a kill instead of waiting for the next tick. */
void taskmgr_sample(syn_server_t *s);
const char *taskmgr_sort_label(syn_tm_sort_t sort);
void synui_render_taskmgr(syn_server_t *s);

/* ── Game mode (game.c) ──────────────────────────────────── */
/* Startup: publish the (off) state for waybar's indicator, so a file left
 * behind by a synui that died mid-game cannot show a phantom game. */
void game_init(syn_server_t *s);
/* Idempotent decision point: call after any fullscreen change, map, or unmap.
 * Enters/leaves game mode (suspend synapd, hold off idle) as needed. */
void game_reevaluate(syn_server_t *s);
/* Super+G — cycle auto → forced-on → forced-off → auto. */
void game_toggle(syn_server_t *s);
/* Shutdown: restart synapd if we suspended it, so synui exiting mid-game
 * doesn't leave the box with no AI. */
void game_finish(syn_server_t *s);

void power_show(syn_server_t *s);
void power_hide(syn_server_t *s);
void power_toggle(syn_server_t *s);
int  power_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);

/* ── CRT filter panel (filters.c) ────────────────────────── */
void filters_show(syn_server_t *s);
void filters_hide(syn_server_t *s);
void filters_toggle(syn_server_t *s);
/* Modal key handling while the panel is open, as in power_key. */
int  filters_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* Persisted strengths (~/.config/synui/filters.state), applied over the config
 * defaults at startup so a look tuned by eye survives a restart. */
void filters_state_load(syn_server_t *s);
void filters_state_save(syn_server_t *s);
/* Name/value for one panel row; render.c draws. The return is the row's 0..1
 * fraction for its slider, or -1.0f for the master switch (which has no bar). */
const char *filters_row_label(int row);
float filters_row_value(syn_server_t *s, int row, char *buf, size_t n);
void synui_render_filters(syn_server_t *s);
void synui_render_power(syn_server_t *s);

void power_state_save(syn_server_t *s);
void power_state_load(syn_config_t *cfg);
/* Name/value strings for one panel row; render.c draws, power.c owns the
 * formatting so the ladder and the labels stay in one place. */
void power_panel_rows(syn_server_t *s, int row, char *name, size_t nn,
                      char *value, size_t vn);

/* ── matrix.c (animated wallpaper) ───────────────────────── */
void matrix_init(syn_server_t *s);                /* compile shader, load atlas (no-op on non-GLES2) */
void matrix_finish(syn_server_t *s);
bool matrix_active(syn_server_t *s);              /* config selects matrix AND it initialized */
bool matrix_output_frame(syn_output_t *o);        /* render one frame; true = keep animating */
void matrix_output_destroy(syn_output_t *o);      /* drop this output's buffer + swapchain */

/* ── wppick.c (wallpaper selector GUI) ───────────────────── */
/* The picker's option table (shared with render.c). token is interpreted the
 * same way the synuirc `wallpaper` key is. */
struct wppick_option { const char *label; const char *desc; const char *token; };
extern const struct wppick_option wppick_options[];
extern const int wppick_option_count;

void wppick_show(syn_server_t *s);
/* Rescan the wallpaper directories into s->wppick.found[] (the "browse" list). */
void wppick_scan(syn_server_t *s);
/* Rows in the panel: the built-in options, then every image the scan found. */
int  wppick_total(syn_server_t *s);
/* Label + subtitle for one row; wppick.c owns the text, render.c draws it. */
void wppick_row(syn_server_t *s, int row, const char **label, const char **desc);
void wppick_hide(syn_server_t *s);
void wppick_toggle(syn_server_t *s);
int  wppick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
void synui_render_wppick(syn_server_t *s);

/* ── icons.c ─────────────────────────────────────────────── */
/* Resolved .desktop info for one app_id, cached after first lookup. Matching
 * is v1-simple: a .desktop file is only found if its basename equals the
 * app_id exactly (no StartupWMClass/heuristic matching) — documented
 * limitation, real app_id-to-.desktop mapping is fuzzy even in GNOME/KDE. */
typedef struct {
    char app_id[128];
    char display_name[128];   /* .desktop Name=, or app_id if unresolved */
    char exec[256];           /* .desktop Exec= (field codes stripped), or
                                * app_id if unresolved — a literal spawn()able
                                * shell command either way */
    char icon_hint[128];      /* .desktop Icon= value; internal to icons.c's
                                * resolution, not meaningful to callers */
    cairo_surface_t *icon_surface;   /* decoded PNG; NULL = draw a monogram */
} syn_icon_entry_t;

/* Look up (and cache) name/exec/icon for an app_id. Always returns a valid
 * pointer with app_id/display_name/exec populated (falling back to the
 * app_id string itself when no .desktop file matches); icon_surface may be
 * NULL, in which case the caller should fall back to icon_draw_monogram(). */
const syn_icon_entry_t *icon_lookup(const char *app_id);
/* Draw a coloured monogram chip (first letter of app_id, uppercased) into a
 * size x size box at (x, y) — the fallback when icon_lookup() found no icon
 * file (SVG icon themes, or nothing on disk at all: both out of scope). */
void icon_draw_monogram(cairo_t *cr, const char *app_id,
                        double x, double y, double size);

/* ── dock.c ──────────────────────────────────────────────── */
void dock_init(syn_server_t *s);                  /* load config; entries start empty */
void dock_output_created(syn_output_t *o);        /* create this output's dock tree */
void dock_output_destroy(syn_output_t *o);        /* destroy this output's dock tree */
/* Re-merge pinned (config) + running (all workspaces') apps into
 * s->dock_entries, then re-render every output. Called on view map/unmap. */
void dock_rebuild(syn_server_t *s);
void dock_view_mapped(syn_view_t *v);             /* foreign_toplevel_map hook */
void dock_view_unmapped(syn_view_t *v);           /* foreign_toplevel_unmap hook */
/* Auto-hide: advance one output's slide animation + hover state for a frame
 * (now = CLOCK_MONOTONIC secs); returns true while more frames are needed. */
bool dock_tick(syn_output_t *o, double now);
/* Pointer moved: schedule frames on outputs whose dock may need to react. */
void dock_pointer_motion(syn_server_t *s);
/* Re-render every output's dock without changing the entry model — used
 * after output geometry changes (output_layout_changed). */
void dock_relayout(syn_server_t *s);
/* Hit-test layout coordinates against every output's dock icon row; returns
 * the entry under (lx, ly) or NULL. Used by input.c's pointer_button to
 * route clicks before falling back to normal view hit-testing. */
syn_dock_entry_t *dock_entry_at(syn_server_t *s, double lx, double ly);
/* Click on a running entry's icon: focuses/raises primary_view, minimizes it
 * if it was already focused, or restores+focuses it if minimized. A pinned-
 * but-not-running entry launches its .desktop Exec. */
void dock_entry_click(syn_server_t *s, syn_dock_entry_t *e);

/* Persisted dock state (~/.config/synui/dock.state): the edge + the pinned
 * app_id set. Overrides synuirc when present (delete the file to revert). */
void dock_state_load(syn_config_t *cfg);
void dock_state_save(syn_server_t *s);
/* Toggle app_id in the pinned set, persist, and rebuild the dock. */
void dock_pin_toggle(syn_server_t *s, const char *app_id);

/* Drag-to-reposition (input.c wires these to pointer button/motion). A press
 * on a dock bar's background (not an icon) begins a drag; motion floats the
 * bar under the cursor; release snaps it to the nearest screen edge. */
void dock_drag_begin(syn_server_t *s, double lx, double ly);
void dock_drag_motion(syn_server_t *s, double lx, double ly);
void dock_drag_end(syn_server_t *s, double lx, double ly);
/* True if (lx,ly) is over a shown dock bar's background (not an icon); sets
 * *out to that output. Used to distinguish an icon click from a bar drag. */
bool dock_bar_at(syn_server_t *s, double lx, double ly, syn_output_t **out);

/* Right-click context menu (mouse-driven; rendered by synui_render_dockmenu).
 * open() builds the item list for an entry and shows the menu at (lx,ly);
 * motion() updates the hover highlight; click() runs the item under the
 * cursor (or dismisses on an outside click); close() hides it. */
void dockmenu_open(syn_server_t *s, syn_dock_entry_t *e, double lx, double ly);
void dockmenu_motion(syn_server_t *s, double lx, double ly);
void dockmenu_click(syn_server_t *s, double lx, double ly);
void dockmenu_close(syn_server_t *s);
void synui_render_dockmenu(syn_server_t *s);

/* Launch a shell command (fork/exec); exposed for dock launches. */
void synui_spawn(const char *cmd);
/* Call in the child between fork() and exec(): drops synui's blocked signal
 * mask (signalfd blocks SIGINT/SIGTERM/SIGHUP) and its SIG_IGN dispositions,
 * both of which survive exec. Without it, nothing synui launches can be killed
 * with SIGTERM. See input.c. */
void synui_child_reset_signals(void);

/* ── screensaver.c (org.freedesktop.ScreenSaver over D-Bus) ── */
/* Best-effort: no session bus, or the name already taken, just logs and leaves
 * the feature off — synui runs fine on the Wayland idle-inhibit protocol alone. */
void screensaver_init(syn_server_t *s);
void screensaver_finish(syn_server_t *s);

/* Is anything holding the screen on — a Wayland idle inhibitor (synui-media-
 * inhibit, say) or a D-Bus ScreenSaver inhibit (Firefox playing a video)?
 * The idle stages and the idle notifier both key off this, not off either
 * counter alone. */
static inline bool idle_inhibited(syn_server_t *s)
{
    return s->idle_inhibitors > 0 || s->screensaver_inhibitors > 0;
}
