/*
 * synui.h — SynapseOS Wayland Compositor internal header
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include <scenefx/types/wlr_scene.h>
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
/* Captured stdout+stderr of a CMD: launch, as rendered in the bar. Eight rows
 * keeps the bar inside a sane height at the bottom of the screen; anything
 * past that is reported as a "+N more lines" tail rather than dropped in
 * silence. Columns are a byte cap, not a glyph count. */
#define CMDBAR_OUT_LINES    8
#define CMDBAR_OUT_COLS     128
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

/* Titlebar palette; overridable from synuirc as titlebar_color / _focus and
 * titlebar_text / _focus. Deliberately muted — the border already carries the
 * focus/AI/alert signal, so a titlebar in border-magenta would shout. */
#define COLOR_TITLEBAR_NORM   { 0.07f, 0.07f, 0.11f, 1.0f } /* #12121c */
#define COLOR_TITLEBAR_FOCUS  { 0.12f, 0.12f, 0.19f, 1.0f } /* #1e1e31 */
#define COLOR_TITLE_TEXT      { 0.45f, 0.45f, 0.55f, 1.0f } /* #73738c */
#define COLOR_TITLE_TEXT_FOCUS{ 0.90f, 0.90f, 0.95f, 1.0f } /* #e6e6f2 */

#define TITLEBAR_HEIGHT_DEF  26   /* synuirc titlebar_height; 0 disables */
#define ANIMATION_MS_DEF     140   /* synuirc animation_ms; 0 disables */

/* What the pointer is over, in a window's server-side decorations. Buttons are
 * square, titlebar-height, right-aligned: [ _ ] [ □ ] [ × ]. */
typedef enum {
    DECO_NONE = 0,
    DECO_TITLEBAR,     /* drag area: press-drag moves, double-click maximizes */
    DECO_BTN_MIN,
    DECO_BTN_MAX,
    DECO_BTN_CLOSE,
    DECO_BORDER,       /* press-drag resizes from the nearest edge/corner */
} syn_deco_region_t;
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

/* Edge-snap (snap.c): the region a dragged window lands in when it is released
 * against a screen edge. The value doubles as syn_view::snapped, i.e. "which
 * half/quarter this window is currently snapped to", so a later drag knows to
 * restore it to its pre-snap size. */
typedef enum {
    SYN_SNAP_NONE = 0,
    SYN_SNAP_MAX,            /* top edge → fill the usable box */
    SYN_SNAP_LEFT,           /* left/right edge → half */
    SYN_SNAP_RIGHT,
    SYN_SNAP_TOP_LEFT,       /* corners → quarter */
    SYN_SNAP_TOP_RIGHT,
    SYN_SNAP_BOTTOM_LEFT,
    SYN_SNAP_BOTTOM_RIGHT,
} syn_snap_zone_t;

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
    /* Non-empty when the bar was opened via ai_ask (Super+Backspace) to ask
     * about a specific window: "<app_id> — <title>". Folded into the prompt so
     * "what is this?" has a referent. Cleared by a plain cmdbar_show. */
    char  ctx[192];

    /* stdout+stderr of the last CMD:, split to lines and sanitised for cairo.
     * Filled by the capture in ai_interface.c when the child hits EOF; empty
     * for a launch that printed nothing (the GUI-app case), which is what
     * keeps `response` alone on the bar there. */
    char  out[CMDBAR_OUT_LINES][CMDBAR_OUT_COLS];
    int   out_lines;
    int   out_more;     /* lines produced beyond the ones we kept */
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
/* Monochrome phosphor tint (effects.c): the shader desaturates to luminance and
 * multiplies it by one phosphor colour, for the amber-P3 and green-P1 terminal
 * looks. OFF leaves colour untouched. Order is what FILTER_ROW_PHOSPHOR cycles. */
typedef enum {
    SYN_PHOSPHOR_OFF = 0,
    SYN_PHOSPHOR_GREEN,   /* P1 — the IBM 5150 green */
    SYN_PHOSPHOR_AMBER,   /* P3 — warm amber terminal */
    SYN_PHOSPHOR_WHITE,   /* P4 — paper-white monochrome */
    SYN_PHOSPHOR_COUNT,
} syn_phosphor_t;

/* Panel rows, in display order. FILTER_ROW_ENABLED toggles the master switch;
 * the slider rows each map to one syn_config_t effect_* strength. PHOSPHOR is a
 * discrete tint selector (a word, not a bar), MONO its blend strength. */
typedef enum {
    FILTER_ROW_ENABLED = 0,
    FILTER_ROW_SCANLINE,
    FILTER_ROW_CURVATURE,
    FILTER_ROW_ABERRATION,
    FILTER_ROW_GLITCH,
    FILTER_ROW_PHOSPHOR,
    FILTER_ROW_MONO,
    FILTER_ROW_BLOOM,
    FILTER_ROW_COUNT,
} syn_filter_row_t;

typedef struct {
    int  visible;
    int  selected;     /* syn_filter_row_t */
    int  dirty;        /* edited since the last save — drives the panel hint */
    char status[96];
} syn_filters_t;

/* ── Clock & Time / Calendar (clock.c) ───────────────────── */
#define CLOCK_ZONES_MAX    6
#define CLOCK_SETTING_ROWS 3   /* format, seconds, NTP — see clock_row_label() */

typedef struct {
    int  fmt24;        /* 0 = 12-hour, 1 = 24-hour (persisted to clock.state) */
    int  seconds;      /* show seconds in the bar clock */
    char zones[CLOCK_ZONES_MAX][64];  /* world-clock IANA zone names */
    int  nzones;
    char tz[128];      /* system zone, read from /etc/localtime */
    int  ntp;          /* timedatectl NTP sync on */
    char status[96];   /* last action / error, shown on the panel */
    int  visible;
    int  selected;     /* 0..CLOCK_SETTING_ROWS-1 */
    struct wl_event_source *timer;    /* 1 Hz repaint while the panel is open */
} syn_clock_t;

typedef struct {
    int visible;
    int year;
    int mon;   /* 0-11 */
    int sel;   /* selected day, 1-based */
} syn_cal_t;

/* ── Theme manager (theme.c) ─────────────────────────────── */
/* A theme is a preset bundle: window-chrome colours (borders + titlebar),
 * a default translucency, and an app colour-scheme (light/dark) that synui
 * writes out to kdeglobals / GTK / Firefox so Dolphin, GTK apps and Firefox
 * follow the desktop. Picked from the Super+T panel or `theme = ` in
 * synuirc; persisted to theme.state. SYNAPSE is the neon default. */
typedef enum {
    SYN_THEME_SYNAPSE = 0,   /* the neon "night drive" default */
    SYN_THEME_DARK,          /* flat modern dark (a plain dark mode) */
    SYN_THEME_WINXP,         /* Windows XP "Luna" blue */
    SYN_THEME_WIN95,         /* Windows 95 grey 3D */
    /* The rices: the palettes people actually theme their desktops with. Each
     * is the upstream palette's real hex, not an approximation by eye. */
    SYN_THEME_CATPPUCCIN,    /* Catppuccin Mocha (mauve) */
    SYN_THEME_GRUVBOX,       /* Gruvbox dark (hard) */
    SYN_THEME_TOKYONIGHT,    /* Tokyo Night (storm) */
    SYN_THEME_NORD,          /* Nord (frost) */
    SYN_THEME_DRACULA,       /* Dracula (purple/pink) */
    SYN_THEME_BUBBLEGUM,     /* Bubblegum pink (light, pastel) */
    SYN_THEME_COUNT,
} syn_theme_t;

/* How window chrome is *drawn*, as opposed to what colour it is. Colours alone
 * could never make the retro themes read as the real thing: XP's caption is a
 * vertical gradient with rounded top corners and a red pill close button, and
 * 95's is a flat navy bar inside a raised 3D bevel with square bevelled buttons.
 * The titlebar is a cairo surface (deco.c), so it can draw all three properly —
 * the borders around it stay flat scene rects, which is why the retro presets
 * make their frame colour the frame *face* colour rather than the caption's. */
typedef enum {
    SYN_CHROME_FLAT = 0,     /* one solid caption colour — the modern look */
    SYN_CHROME_LUNA,         /* Windows XP: gradient, rounded top, pill buttons */
    SYN_CHROME_BEVEL,        /* Windows 95: 3D bevels, square bevelled buttons */
} syn_chrome_t;

extern const char *const syn_theme_names[SYN_THEME_COUNT];

typedef struct {
    int  visible;
    int  selected;     /* syn_theme_t */
    char status[96];
} syn_thememgr_t;

/* ── Control panel (ctlpanel.c) ──────────────────────────── */
/* The settings column, in display order. The *shortcuts* column deliberately
 * has no table here: it is generated from the live bind table (syn_config_t::
 * binds) every time the panel renders, so it cannot drift out of step with the
 * binds actually in force — the failure the waybar start menu shipped once,
 * where a stale hand-maintained list mapped entries to the wrong actions. */
typedef enum {
    CTL_ROW_EFFECTS = 0,   /* toggles: act in place, no panel */
    CTL_ROW_GAME,
    CTL_ROW_AI_BACKEND,
    CTL_ROW_DOCK,
    CTL_ROW_DOCK_AUTOHIDE, /* dock slides away when unhovered, or stays put */
    CTL_ROW_TITLEBARS,
    CTL_ROW_LAUNCHER,      /* start-button style: text ◢ SYNAPSE, or ◢ + emblem */
    CTL_ROW_TRANSPARENCY,  /* window translucency master switch */
    CTL_ROW_SEP,           /* rule, not selectable — skipped by the cursor */
    CTL_ROW_THEME,         /* jump-off: the Super+T theme manager */
    CTL_ROW_DISPLAYS,      /* jump-offs: open the panel that owns the setting */
    CTL_ROW_FILTERS,
    CTL_ROW_WALLPAPER,
    CTL_ROW_POWER,
    CTL_ROW_TASKMGR,
    CTL_ROW_NETWORK,
    CTL_ROW_BLUETOOTH,
    CTL_ROW_PRINTERS,
    CTL_ROW_LOCK,
    CTL_ROW_COUNT,
} syn_ctl_row_t;

/* A shortcuts-column line. The nine workspace binds (and the nine move-to-
 * workspace binds) are collapsed into one row each — listed literally they are
 * 18 of ~40 rows and drown everything worth reading. */
typedef struct {
    char combo[48];
    char desc[64];
} syn_ctl_shortcut_t;

#define CTL_SHORTCUTS_MAX  SYN_BINDS_MAX

typedef struct {
    int  visible;
    int  selected;     /* syn_ctl_row_t, always a selectable row */
    int  scroll;       /* first shortcuts row drawn */
    char status[96];
    /* The AI-backend row's value is not compositor state: it is read back out of
     * /run/synapd/backend, which the synui-ai-backend helper only writes once it
     * has rewritten the drop-in and restarted synapd — a second or two later,
     * with nothing to notify us. The panel otherwise repaints only on input, so
     * the row sat on the old device until some keypress happened to redraw it
     * ("it doesn't update until you move off the row"). Non-zero = keep
     * repainting the panel until this CLOCK_MONOTONIC deadline. */
    double backend_poll_until;
    /* The row's value at the moment the switch was fired — what the poll is
     * waiting to see change. Kept here rather than in a static: two opens of the
     * panel are two separate waits, and a static would carry the first one's
     * answer into the second and call it an instant success. */
    char   backend_before[16];
} syn_ctlpanel_t;

/* ── Start menu (menu.c) ─────────────────────────────────── */
/* Super-tap's menu, drawn by the compositor rather than clicked out of waybar.
 *
 * It used to be waybar's: synui synthesised a pointer click on the bar and GTK
 * popped a menu. That menu could never be arrow-navigated — waybar asks for
 * keyboard_interactivity NONE once at startup and never revises it, so the
 * client is handed no keyboard focus, and three separate synui-side focus fixes
 * all delivered keys that GTK then ignored. The wall was inside waybar/GDK, not
 * here, so the menu moved in-process: a panel synui draws is one synui can also
 * give the keyboard to, and it is arrow-navigable by construction.
 *
 * The entries are scanned from the installed .desktop files at open, NOT read
 * from a generated file. The waybar menu's XML/menu-actions pair had to be
 * regenerated in lockstep and drifted apart at least once, mapping entries to
 * the wrong commands; there is nothing to keep in step if the list is built
 * from the source of truth each time. Scanning ~170 files takes ~ms.
 */
#define MENU_ENTRIES_MAX  512
#define MENU_LABEL_MAX     72
#define MENU_CMD_MAX      512
#define MENU_CAT_MAX       24

typedef enum {
    MENU_ROW_HEADER = 0,   /* section rule — never selectable */
    MENU_ROW_ITEM,
    MENU_ROW_SUBMENU,      /* opens the page named by ->menu_to, launches nothing */
    MENU_ROW_BACK,         /* a submenu page's first row; returns to the root */
} syn_menu_kind_t;

typedef struct {
    int  kind;                     /* syn_menu_kind_t */
    char label[MENU_LABEL_MAX];
    /* Which page this row lives on: "" is the root, otherwise a submenu name.
     * Every row of every page is in this one flat array, which is what lets a
     * search run across the whole menu rather than only the page you happen to
     * be looking at — the reason drilling in does not cost you reachability. */
    char menu[MENU_CAT_MAX];
    /* On a MENU_ROW_SUBMENU, the page it opens (matched against ->menu). */
    char menu_to[MENU_CAT_MAX];
    /* Exactly one of these is set on an item. A bind action goes through
     * synui_binding_execute() — the same path a keypress takes — so the menu
     * cannot become a second, disagreeing definition of what "Control Panel"
     * means. Anything synui does not own is a command for spawn(). */
    char action[24];
    char cmd[MENU_CMD_MAX];
} syn_menu_entry_t;

typedef struct {
    int  visible;
    int  count;                       /* entries[] in use */
    int  view[MENU_ENTRIES_MAX];      /* indices of entries[] passing the filter */
    int  view_count;
    int  selected;                    /* index into view[], not entries[] */
    int  scroll;                      /* first view[] row drawn */
    char filter[48];                  /* type-to-search; empty = show all */
    /* The page on show: "" is the root. One level deep by construction — the
     * root's submenu rows are the only ones there are, so backing out is always
     * a return to the root and needs no stack. */
    char page[MENU_CAT_MAX];
    int  root_selected, root_scroll;  /* where to land back on when we do */
    /* Hovering a submenu row arms this to open its page after a short delay, so
     * the pointer can cross the category rows without flipping through pages. */
    struct wl_event_source *hover_timer;
    /* Panel geometry in layout coords, written by synui_render_menu() on every
     * render and read by the pointer hit-tests. The renderer owns it because the
     * height depends on the row count it just drew; nothing reads it while the
     * menu is hidden, and showing the menu renders it before any pointer event
     * can arrive. */
    int  x, y, w, h;
    syn_menu_entry_t entries[MENU_ENTRIES_MAX];
} syn_menu_t;

/* ── Notifications (notif.c) ─────────────────────────────── */
/* synui owns org.freedesktop.Notifications and draws the toasts itself.
 *
 * Nothing owned that name, so every notify() on the system failed silently:
 * Firefox, chibi, synguard's alerts, and synui-screenshot's own "saved" toast
 * (whose script has carried a "SYNAPSE ships no notification daemon" comment
 * and a guard around the call). A desktop where nothing can tell you anything
 * is the state this replaces.
 *
 * Native for the same reason as the start menu and Bluetooth: the compositor
 * already owns the screen, so it can place a toast in the usable area (below
 * waybar's exclusive zone), above every window, without a layer-shell client
 * and without a second process to keep alive.
 */
#define NOTIF_MAX       6     /* on screen at once; older ones drop off */
#define NOTIF_APP_MAX  48
#define NOTIF_SUM_MAX  96
#define NOTIF_BODY_MAX 256

/* org.freedesktop.Notifications urgency hint. Critical is the one that matters:
 * the spec says it must not auto-expire, so it stays until dismissed. */
typedef enum {
    NOTIF_URGENCY_LOW = 0,
    NOTIF_URGENCY_NORMAL = 1,
    NOTIF_URGENCY_CRITICAL = 2,
} syn_notif_urgency_t;

/* NotificationClosed reasons, from the spec. Sent so a client can tell an
 * expiry from a dismissal — some redraw or re-post on one but not the other. */
typedef enum {
    NOTIF_CLOSED_EXPIRED   = 1,
    NOTIF_CLOSED_DISMISSED = 2,
    NOTIF_CLOSED_BY_CALL   = 3,
    NOTIF_CLOSED_UNDEFINED = 4,
} syn_notif_reason_t;

typedef struct {
    uint32_t id;
    char     app[NOTIF_APP_MAX];
    char     summary[NOTIF_SUM_MAX];
    char     body[NOTIF_BODY_MAX];
    int      urgency;
    /* CLOCK_MONOTONIC ms at which this expires; 0 = never (critical, or an
     * explicit expire_timeout of 0). */
    int64_t  expires_ms;
} syn_notif_t;

typedef struct {
    syn_notif_t items[NOTIF_MAX];
    int         count;
    uint32_t    next_id;   /* ids must never be 0: 0 means "no id" to callers */
} syn_notifs_t;

/* ── Clipboard history (clipboard.c) ─────────────────────── */
/* The compositor already owns the seat, so it sees every selection with no
 * protocol, no client and no wl-paste poller. Memory only, never a file: a
 * clipboard that silently persists every password you copy is a liability. */
#define CLIP_HISTORY_MAX  32
#define CLIP_TEXT_MAX     (256 * 1024)   /* a client may offer 900MB of "text" */
#define CLIP_ROWS         12             /* render.c draws this many; keep in step */

typedef struct {
    char *text;          /* owned; strdup'd out of the client's pipe */
} syn_clip_item_t;

typedef struct {
    int             visible;
    int             selected;
    int             scroll;
    int             count;
    syn_clip_item_t items[CLIP_HISTORY_MAX];   /* [0] is most recent */
} syn_clipboard_t;

/* ── Bluetooth panel (bt.c) ──────────────────────────────── */
/* Native BlueZ client: synui talks org.bluez over sd-bus itself rather than
 * shelling out to bluetoothctl and scraping it, or handing the job to a GTK
 * applet. Same reasoning as the start menu — a panel the compositor draws is one
 * it can hand the keyboard to.
 *
 * Everything here is async. A radio can take seconds to answer, and a sync
 * sd_bus_call() would block the wl_event_loop — i.e. freeze the whole desktop —
 * so every method goes out via sd_bus_call_async and the panel repaints when the
 * reply or a PropertiesChanged lands. The bus fd sits in the Wayland event loop
 * (the screensaver.c idiom), so this costs nothing while the panel is closed.
 *
 * Discovery is owned by the D-Bus *connection* that started it: BlueZ stops
 * scanning when that client drops off the bus. synui's connection is long-lived,
 * so a scan survives closing the panel — bt_hide() stops it deliberately rather
 * than leaving the radio burning power in the background. */
#define BT_DEVICES_MAX  64

typedef struct {
    char path[160];        /* /org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX */
    char name[64];         /* Alias, falling back to Name, then Address */
    char addr[24];
    char icon[24];         /* BlueZ's freedesktop icon name: audio-headset, … */
    /* AddressType == "random": an LE privacy address, rotated every ~15 minutes
     * by the phone/watch/earbud broadcasting it. Nameless ones are the noise the
     * panel hides by default — the address identifies nothing and is a different
     * address by the time you look again. A "public" address is a real fixed one
     * and its device is worth listing even unnamed. */
    int  random_addr;
    int  paired, trusted, connected, blocked;
    int  battery;          /* org.bluez.Battery1 percentage, -1 if none */
    int  rssi;             /* 0 when absent — a device off the air has none */
    int  has_rssi;
} syn_bt_dev_t;

typedef struct {
    int  visible;
    int  selected;         /* index into devs[] */
    int  touched;          /* the cursor has been moved: pin the selection
                            * to its device rather than to the top row */
    int  scroll;
    /* 'a': list the anonymous advertisers too. Off by default — see
     * dev_listable() in bt.c for what that leaves out and why. */
    int  show_all;

    /* Panel geometry in layout coords, written by synui_render_bt() on every
     * render and read by the pointer hit-tests — as in syn_menu_t. */
    int  x, y, w, h;

    int  has_adapter;
    char adapter[160];     /* /org/bluez/hci0 */
    int  powered;
    int  discovering;
    int  discoverable;

    syn_bt_dev_t devs[BT_DEVICES_MAX];
    int  count;

    char status[96];       /* last action / error, shown in the footer */

    /* A pairing agent request waiting on the user. BlueZ is blocked on our
     * reply the whole time it is up, so it must be answered (y/n) or cancelled —
     * the message is kept so the reply can be sent when they decide. */
    int      ask_kind;     /* syn_bt_ask_t */
    char     ask_dev[64];
    char     ask_detail[64];
    uint32_t ask_passkey;
} syn_bt_t;

typedef enum {
    BT_ASK_NONE = 0,
    BT_ASK_CONFIRM,        /* RequestConfirmation: passkey matches? y/n */
    BT_ASK_AUTHORIZE,      /* RequestAuthorization / AuthorizeService: y/n */
    BT_ASK_DISPLAY,        /* DisplayPasskey / DisplayPinCode: type it on the
                            * device; informational, no reply expected */
} syn_bt_ask_t;

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

/* ── News aggregator (news.c) ────────────────────────────── */
#define NEWS_SOURCES_MAX  12
#define NEWS_ITEMS_MAX    360   /* across every feed, after the per-feed cap */
#define NEWS_PER_FEED     30    /* newest N kept from any one feed */
#define NEWS_ROWS         16    /* rows visible at once */
#define NEWS_TITLE_LEN    200
#define NEWS_URL_LEN      400
#define NEWS_SEEN_MAX     4096  /* read-marks kept in news.seen */
#define NEWS_QUERY_MAX    48

typedef struct {
    char name[16];    /* column tag: "HN", "ARCH", "LWN"… */
    char url[256];    /* feed URL (RSS or Atom) */
} syn_news_source_t;

typedef struct {
    char   title[NEWS_TITLE_LEN];
    char   url[NEWS_URL_LEN];
    char   comments[NEWS_URL_LEN];  /* discussion link, or "" */
    int    src;                     /* index into news.sources[] */
    int    rank;                    /* position within its own feed: HN's front
                                     * page is ranked, not chronological, and
                                     * sorting by time alone throws that away */
    time_t ts;                      /* published; 0 = the feed didn't say */
    int    seen;
} syn_news_item_t;

typedef enum {
    NEWS_SORT_TIME = 0,   /* one river, newest first */
    NEWS_SORT_SOURCE,     /* grouped by feed, each in its own feed order */
} syn_news_sort_t;

typedef struct {
    int   visible;
    int   selected;       /* index into view[] */
    int   scroll;
    int   filter;         /* -1 = every source, else a sources[] index */
    syn_news_sort_t sort;
    int   searching;      /* '/' typing mode */
    char  query[NEWS_QUERY_MAX];
    char  status[96];

    syn_news_source_t sources[NEWS_SOURCES_MAX];
    int   n_sources;

    /* What the panel draws. Main thread only. */
    syn_news_item_t items[NEWS_ITEMS_MAX];
    int   n;
    int   view[NEWS_ITEMS_MAX];   /* row -> items[] index, under filter+query */
    int   n_view;

    time_t updated;       /* last fetch that brought something back */
    int    fetching;
    int    failed;        /* feeds that errored in that fetch */

    /* Read-marks: FNV-1a of the item URL, persisted to news.seen. */
    uint64_t seen[NEWS_SEEN_MAX];
    int      n_seen;

    /* ── Shared with the fetch thread ─────────────────────
     * The thread owns no wlroots state: it fetches, parses into fetched[]
     * under lock, and pokes the pipe. The main thread does the rest. */
    pthread_t        thread;
    pthread_mutex_t  lock;
    pthread_cond_t   cv;
    int              running;
    atomic_int       stop;        /* also aborts a transfer mid-flight */
    atomic_int       want;        /* a refresh has been asked for */
    int              pipe[2];
    struct wl_event_source *src;
    struct wl_event_source *timer;   /* auto-refresh, armed only while visible */

    syn_news_item_t  fetched[NEWS_ITEMS_MAX];
    int              n_fetched;
    int              fetch_failed;
} syn_news_t;

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
    SYN_WALLPAPER_TILE,       /* 1:1, repeated from the top-left */
    SYN_WALLPAPER_MODE_COUNT, /* keep last — the Super+W picker cycles on it */
} syn_wallpaper_mode_t;

/* Names for the picker and the config parser, indexed by syn_wallpaper_mode_t.
 * Defined in wallpaper.c; keep in step with the enum above. */
extern const char *const syn_wallpaper_mode_names[SYN_WALLPAPER_MODE_COUNT];

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

/* The start-menu launcher (launcher.c) synui draws in the top-left of every
 * output — the "◢ SYNAPSE" button that used to be a waybar module. Text is the
 * old look; logo swaps it for the dendrite emblem (SYNUI_DATADIR/logo.svg). */
typedef enum {
    SYN_LAUNCHER_TEXT = 0,   /* "◢ SYNAPSE" */
    SYN_LAUNCHER_LOGO,       /* logo.svg emblem */
} syn_launcher_style_t;

/* Dock right-click context-menu actions (dock.c / render.c). */
typedef enum {
    SYN_DOCKACT_PIN = 0,   /* add app_id to the pinned set */
    SYN_DOCKACT_UNPIN,     /* remove it from the pinned set */
    SYN_DOCKACT_OPEN,      /* launch (.desktop Exec) — not currently running */
    SYN_DOCKACT_NEWWIN,    /* launch another instance — already running */
    SYN_DOCKACT_CLOSEWIN,  /* close one window of this app_id — the focused one */
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

    /* Drag a window against a screen edge to snap it to that half/quarter
     * (snap.c). Off means a drag is only ever a move. */
    int   snap;

    /* Border colors (RGBA 0..1) by window role; defaults COLOR_BORDER_*. */
    float border_color_norm[4];
    float border_color_focus[4];
    float border_color_ai[4];
    float border_color_warn[4];

    /* Server-side titlebar: drag to move, double-click to maximize, and the
     * three buttons. `titlebar_height` of 0 turns it off entirely (windows keep
     * their borders, and Super+drag still moves them). */
    /* anim.c: fade duration in ms; 0 disables animations entirely (every fade
     * then jumps straight to its end state). */
    int   animation_ms;

    int   titlebar_height;
    float titlebar_color[4];
    float titlebar_color_focus[4];
    float titlebar_text[4];
    float titlebar_text_focus[4];

    /* theme.c: which preset is active. Its colours seed the border/titlebar
     * fields above (theme_apply overwrites them), so an explicit border_color_*
     * in synuirc set AFTER `theme =` still wins — the parse order is the config's
     * to decide. Persisted as a name to theme.state. */
    syn_theme_t theme;

    /* Chrome style + the extra colours only the non-flat styles use. `chrome`
     * picks the titlebar painter in deco.c; the gradient ends are the BOTTOM of
     * each caption gradient (the *_color fields above are the top), and
     * chrome_face is the 3D face colour bevels and retro buttons are cut from.
     * A flat theme leaves the gradient ends equal to its caption colours, so the
     * same painter code is correct with no branching on the theme itself. */
    syn_chrome_t chrome;
    float titlebar_grad[4];         /* inactive caption, gradient end */
    float titlebar_grad_focus[4];   /* active caption, gradient end   */
    float chrome_face[4];           /* frame/button face (95 silver, XP beige) */

    /* Panel accent (RGBA 0..1): the one colour every compositor-drawn panel
     * (menu, control panel, overlays) uses for headers, selections and rules.
     * Seeded from the active theme (theme_load_colors), so a theme switch
     * reskins synui's own UI, not just window chrome. render.c caches it via
     * render_set_panel_accent() so draw helpers need no server handle. */
    float panel_accent[4];

    /* Window translucency (theme.c / anim.c). `transparency` is the master
     * switch — off, everything is opaque and the opacities are ignored. When on,
     * the focused window sits at active_opacity and the rest at inactive_opacity.
     * Applied compositor-side to every buffer under a window, so it covers native
     * and XWayland clients (Firefox, Dolphin) uniformly, without their help. */
    int   transparency;          /* default 0 (opaque) */
    float active_opacity;        /* focused window, 0.5..1.0; default 1.0 */
    float inactive_opacity;      /* unfocused windows; default 0.92 */

    /* Terminals draw their own background alpha (glyphs stay opaque), so foot is
     * excluded from the compositor fade above and driven through synui-glass
     * instead — see glass_push() in theme.c. It needs its OWN level rather than
     * the slider's: the same alpha over foot's near-black background reads far
     * more solid than over a light GTK window, so tracking the slider 1:1 made a
     * comfortable desktop opacity into an almost-opaque terminal.
     * -1 = untracked, fall back to active_opacity (the old coupled behaviour). */
    float foot_alpha;            /* 0.0..1.0, or -1 to follow active_opacity */

    /* scenefx glass (Stage 5 of the scenefx migration). Applied to every buffer
     * under a window via the same anim.c walk that drives opacity. `corner_radius`
     * rounds each window's corners (0 = square, forced to 0 while maximized/
     * fullscreen so nothing pokes past the output). `blur` turns on backdrop blur
     * behind translucent windows — foot's app-native glass and transparent
     * Firefox get frosted; behind an opaque window it renders nothing, so it is
     * safe on every client. blur_* feed wlr_scene_set_blur_data once at init. */
    int   corner_radius;         /* px; default 12, 0 disables */
    int   blur;                  /* master backdrop-blur switch; default 1 */
    int   blur_passes;           /* default 3 */
    int   blur_radius;           /* default 5 */
    float blur_noise;            /* default 0.02 */
    float blur_brightness;       /* default 0.90 */
    float blur_contrast;         /* default 1.00 */
    float blur_saturation;       /* default 1.15 */

    /* Drop shadow (scenefx wlr_scene_shadow, one node per window frame, drawn
     * behind everything and clipped out from under the window itself so it is a
     * soft outer ring — see view_shadow_update). `shadow` gates it; disabled
     * while maximized/fullscreen (an edge-to-edge window's shadow is clipped to
     * nothing). shadow_blur_sigma is the softness/spread in px; the box is grown
     * 2·sigma so the falloff isn't cut off. shadow_offset_{x,y} bias the drop
     * direction (default straight down a touch). shadow_color is RGBA. */
    int   shadow;                /* master switch; default 1 */
    float shadow_blur_sigma;     /* px; default 18 */
    int   shadow_offset_x;       /* px; default 0 */
    int   shadow_offset_y;       /* px; default 6 */
    float shadow_color[4];       /* default black @ 0.45 */

    /* GLES post-process effects (effects.c). `effects` gates the pass;
     * it silently stays off on non-GLES2 renderers (pixman VMs).
     * Strengths are 0..1; 0 disables the individual effect. */
    int   effects;
    float effect_scanline;
    float effect_curvature;
    float effect_aberration;
    float effect_glitch;     /* strength of the alert/close glitch; 0 = off */
    int   effect_phosphor;   /* syn_phosphor_t tint; SYN_PHOSPHOR_OFF = colour */
    float effect_mono;       /* 0..1 blend toward the phosphor tint */
    float effect_bloom;      /* 0..1 phosphor glow bleed; only bites with mono */

    /* Keyboard: XKB keymap (empty = XKB_DEFAULT_* env / system default). */
    char  xkb_rules[64];
    char  xkb_model[64];
    char  xkb_layout[64];
    char  xkb_variant[64];
    char  xkb_options[256];
    int   repeat_rate;       /* key repeats per second */
    int   repeat_delay;      /* ms before repeat starts */
    /* Lock the NumLock modifier on every keyboard as it is attached (and
     * again after a SIGHUP, which recompiles the keymap and so resets the
     * xkb state). A freshly compiled xkb state has NumLock off, which leaves
     * the numpad emitting arrows until someone presses the key — including on
     * the lock screen. Default 1. */
    int   numlock;

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

    /* cat.c: start with the kitty already wandering (synuirc `cat = on`).
     * Off by default — Super+Shift+C toggles it at runtime. */
    int   cat_start;

    /* Show the welcome menu on login. The menu's own "Show At Startup" row
     * toggles this and writes welcome.state, which then overrides the synuirc
     * line (delete it to hand control back). Super+Escape opens the menu
     * either way, so turning this off never strands it. Default 1. */
    int   welcome_at_startup;

    /* macOS-style auto-hide dock (dock.c). Mirrored on every output; never
     * reserves an exclusive zone (see syn_output::dock's comment) — hidden
     * it takes zero layout space, shown it floats above window content. */
    int   dock_enabled;         /* default 1 */
    /* Auto-hide: slide the dock off its edge when the pointer leaves and
     * reveal it from a trigger strip (default 1). Off, it stays on screen —
     * pinned like the drag branch, still floating above content, not reserving
     * layout space. Persisted to dock.state. */
    int   dock_autohide;        /* default 1 */
    /* Night light: warm the screen by writing the outputs' gamma LUTs directly
     * (nightlight.c). 6500K is daylight — the identity ramp — so the *temp* is
     * only meaningful while night_light is on. */
    int   night_light;          /* default 0 */
    int   night_light_temp;     /* Kelvin, default 4000 */
    int   dock_height;          /* px thickness, default 64 */
    int   dock_hover_margin;    /* px trigger strip at the dock's edge, default 4 */
    syn_dock_edge_t dock_edge;  /* which screen edge, default BOTTOM */
    /* launcher.c: the synui-drawn start-menu button. Default TEXT. */
    syn_launcher_style_t launcher_style;
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
    /* 512, not 192: the themed swaylock invocation is ~320 chars and snprintf
     * would have truncated it mid-flag, silently. */
    char  power_lock_cmd[512];

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

    /* News aggregator (news.c). Empty means "use the built-in source list";
     * the first `news_source =` line in synuirc replaces the lot, so a user
     * who wants only their own feeds is not stuck with ours. */
    syn_news_source_t news_sources[NEWS_SOURCES_MAX];
    int   news_sources_n;
    int   news_refresh_min;     /* re-fetch a feed at most this often */

    syn_bind_t binds[SYN_BINDS_MAX];
    int        bind_count;
} syn_config_t;

/* Retro chrome is SQUARE. scenefx's rounded corners are the house look and stay
 * the user's setting (`corner_radius`), but a Windows 95 window with 12px
 * rounded corners is instantly wrong, and no amount of correct navy fixes it —
 * so the chrome style overrides the radius here rather than overwriting the
 * config, and switching back to a modern theme restores the user's value with
 * no state to remember. XP is square by this rule too: its rounded top corners
 * are drawn by the titlebar itself (deco.c), which can round the TOP only, the
 * way Luna did. 95 also drops the shadow — it sat flat on the desktop. */
static inline int chrome_corner_radius(const syn_config_t *cfg)
{
    return cfg->chrome == SYN_CHROME_FLAT ? cfg->corner_radius : 0;
}
static inline int chrome_shadow(const syn_config_t *cfg)
{
    return cfg->chrome == SYN_CHROME_BEVEL ? 0 : cfg->shadow;
}

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
/* A workspace is a *virtual desktop*: it spans every monitor at once, the way
 * KDE/GNOME virtual desktops do. Switching to it switches all outputs together
 * (server::active_workspace); each window inside it remembers which monitor it
 * sits on (syn_view::output), so a desktop keeps its own arrangement across the
 * whole desk. Exactly one workspace is visible at a time.
 *
 * `layout` and `master_factor` are per-desktop, not per-monitor: every output
 * showing this desktop tiles the same way. */
struct syn_workspace {
    int              index;
    char             name[WORKSPACE_NAME_LEN];
    char             intent[256];
    syn_layout_t     layout;
    int              visible;        /* == (index == server->active_workspace) */
    float            master_factor;  /* master column width, 0.10–0.90 */
    struct wl_list   windows;   /* syn_view_t::link — across all outputs */
};

/* ── View (window) ───────────────────────────────────────── */
struct syn_view {
    struct wl_list          link;       /* in workspace->windows */
    /* In server->xw_views; X11 views only, and valid from new_surface rather
     * than from map. `link` above is only ever on a workspace list, i.e. only
     * while mapped, so it cannot reach a window that never mapped — which is
     * precisely the Steam wedge. See xwayland_unwedge(). */
    struct wl_list          xw_link;
    syn_server_t           *server;
    syn_workspace_t        *workspace;
    /* The monitor this window lives on within its workspace. Never NULL for a
     * mapped view while any output exists; output removal re-homes it. */
    syn_output_t           *output;

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

    /* When this view was last focused, from the server's focus_counter. Higher
     * is more recent; 0 means never focused. This is what Alt+Tab orders by —
     * the workspace list is in stacking order, which is not the order anyone
     * means by "the last window I was in". */
    uint64_t focus_seq;

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

    /* Per-view decoration frame. The client surface (scene_tree) and every bit
     * of chrome (borders, titlebar) are children of it, so enabling, hiding and
     * raising a window moves its decorations with it. Before this existed the
     * borders were siblings in window_tree, and a window hidden by a workspace
     * switch left its border rects painted on screen.
     *
     * Frame-local coordinates: the frame node sits at (view->x, view->y), so
     * the chrome is laid out from (0,0) and only the frame is repositioned when
     * the window moves. NULL for override-redirect X11 surfaces, which are
     * undecorated and live in the overlay layer — use view_node() to get the
     * node to enable/raise/position for any view. */
    struct wlr_scene_tree *frame;

    /* Border scene rects (children of frame) */
    /* ONE rect spanning the whole frame with its middle clipped out, not four
     * edge rects: a scene rect's corner radius rounds the rect itself, and four
     * strips border_width thick cannot describe a corner of radius 12 — they
     * rendered a square frame around visibly rounded glass. The ring carries the
     * outer radius; the clipped region carries the inner one. */
    struct wlr_scene_rect *border;

    /* Drop shadow (scenefx): one node lowered to the bottom of the frame so it
     * sits behind the client + chrome, sized 2·blur_sigma larger than the frame
     * and clipped to exclude the window rect (see view_shadow_update). NULL for
     * override-redirect surfaces (no frame) and while shadows are off. */
    struct wlr_scene_shadow *shadow;

    /* Invisible resize-grab ring: four fully transparent rects sitting *outside*
     * the window, one per edge, overhanging the corners. The visible border is
     * only border_width px thick, so before these existed a corner grab meant
     * hitting a 2px sliver and edge drags were a game of pixel darts. Scene
     * rects are hit-tested on their bounds alone — alpha is irrelevant and,
     * unlike scene buffers, they have no point_accepts_input opt-out — so a
     * transparent rect is exactly a click target with nothing drawn in it.
     * They live in the frame (so they move/raise with the window) and are
     * disabled when there is nothing to resize (fullscreen, maximized). */
    struct wlr_scene_rect *grab_top;
    struct wlr_scene_rect *grab_bottom;
    struct wlr_scene_rect *grab_left;
    struct wlr_scene_rect *grab_right;

    /* Titlebar: one cairo buffer holding the title text and the three buttons
     * (child of frame). Re-rendered only when something it draws changes — the
     * cached fields below are what it was last drawn with. */
    struct wlr_scene_buffer *titlebar;
    int   tb_w, tb_h;                 /* size it was last rendered at */
    int   tb_focused;                 /* focus state it was last drawn with */
    syn_deco_region_t tb_hover;       /* button highlighted under the pointer */
    char  tb_title[128];              /* title it was last drawn with */

    /* Geometry to restore when un-maximizing or un-snapping, and whether the
     * window was floating before (both maximize and snap leave the tiling
     * flow). Maximize and snap are mutually exclusive states, so they share the
     * one slot. */
    struct wlr_box saved_geo;
    int            saved_floating;

    /* snap.c: which edge zone this window is currently snapped to, if any.
     * Dragging a snapped window releases it back to saved_geo. */
    syn_snap_zone_t snapped;

    /* anim.c: the window's current opacity (1 = solid) and the fade in flight.
     * alpha is applied to every buffer under the frame *and* multiplied into
     * the border rects' colour, so a fading window fades whole. */
    float  alpha;
    int    fade_active;
    int    fade_hide_done;   /* disable the node once it reaches alpha 0 */
    float  fade_from, fade_to;
    double fade_start;       /* CLOCK_MONOTONIC secs */

    /* theme.c / anim.c: the window's *settled* translucency, driven by focus
     * (config.active_opacity vs inactive_opacity) when config.transparency is on.
     * This is a separate lever from `alpha` (the fade in flight): the two are
     * multiplied at apply time, so a window can be fading in AND translucent at
     * once, and neither clobbers the other. 1.0 = opaque; the default. */
    float  base_opacity;

    /* Listeners (shared: xdg + xwayland reuse map/unmap/destroy/request_*) */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    /* xdg-only: a CSD client (Firefox) drags its own titlebar / resize edges and
     * asks the compositor to run the grab. See view_begin_interactive(). */
    struct wl_listener request_move;
    struct wl_listener request_resize;
    /* xwayland-only */
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener request_configure;
    struct wl_listener request_activate;
    struct wl_listener request_minimize;   /* ICCCM iconify; xdg-shell has no equivalent */
};

/* ── Layer-shell surface (panels, bars, wallpaper, launchers) ── */
/* ── Input method relay (ime.c) ──────────────────────────── */
/* The switchboard between the application's text field (text-input-v3) and the
 * IME (input-method-v2); neither protocol can see the other. */
typedef struct syn_text_input {
    struct wlr_text_input_v3 *input;
    struct syn_ime           *relay;
    struct wl_list            link;    /* in syn_ime::text_inputs */
    struct wl_listener enable, commit, disable, destroy;
} syn_text_input_t;

/* The IME's candidate window ("你好 / 泥號 / …"), parked at the caret. */
typedef struct syn_ime_popup {
    struct wlr_input_popup_surface_v2 *popup;
    struct syn_ime                    *relay;
    struct wlr_scene_tree             *tree;
    struct wl_list                     link;   /* in syn_ime::popups */
    struct wl_listener destroy, surface_commit;
} syn_ime_popup_t;

typedef struct syn_ime {
    syn_server_t *server;
    struct wl_list text_inputs;                    /* syn_text_input::link */
    struct wl_list popups;                         /* syn_ime_popup::link  */

    /* At most one IME per seat; a second is told it's unavailable. */
    struct wlr_input_method_v2                *input_method;
    struct wlr_input_method_keyboard_grab_v2  *keyboard_grab;

    struct wl_listener new_text_input;
    struct wl_listener new_input_method;
    struct wl_listener im_commit, im_new_popup, im_grab_keyboard, im_destroy;
    struct wl_listener grab_keyboard_destroy;
} syn_ime_t;

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

    /* launcher.c: this output's "◢ SYNAPSE" start-menu button, drawn top-left
     * over the waybar bar. A UI-sibling scene tree like the dock's, so it floats
     * above the top-layer bar; hidden when a fullscreen window covers the
     * output, exactly as the bar and dock are. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_buffer *buf;
        int  x, y, w, h;   /* layout-space hit box; valid while visible */
        int  visible;
    } launcher;

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
    /* The DRM session, kept so Ctrl+Alt+F1..F12 can change VT. NULL under a
     * nested/headless backend, where there is no VT to change to. */
    struct wlr_session         *session;
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

    /* Native lock screen (lock.c). Instead of spawning swaylock as an
     * ext-session-lock client, synui draws the lock itself — a stylized clock
     * that brightens on input and fades when idle — and authenticates through
     * the synui-lock-auth helper (PAM in a child, so the event loop never
     * blocks on the fail delay). Reuses `locked` above, so every input gate
     * that already exists for the session lock holds for this one too; its
     * own scene tree is layered over everything, as session.c's backstop is. */
    struct {
        int      active;
        char     pw[256];
        int      pw_len;
        int      failed;          /* last attempt was rejected */
        int      busy;            /* auth helper in flight — ignore keys */
        double   bright;          /* 0..1 fade level of the clock/indicator */
        uint32_t last_input_ms;
        struct wlr_scene_tree   *tree;      /* backstop + the per-output panes */
        struct wl_event_source  *t_clock;   /* 1 Hz, so the minute updates */
        struct wl_event_source  *t_fade;    /* eases `bright` toward its target */
        pid_t                    auth_pid;
        int                      auth_fd;   /* result-pipe read end, -1 when idle */
        struct wl_event_source  *auth_src;
        struct {
            struct wlr_output       *output;
            struct wlr_scene_buffer *buf;
        } pane[8];              /* one clock panel centred on each output */
        int      npane;
    } nlock;

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
    /* Armed by a tray-restore click, disarmed if the window shows up in time;
     * see dock_arm_unwedge(). */
    struct wl_event_source *dock_unwedge_timer;

    /* cat.c: the wandering kitty (Super+Shift+C). One animal for the whole
     * layout, so its tree is top-level and its position is in LAYOUT coords —
     * that is what lets it walk between monitors with no seam handling. */
    struct {
        int    enabled;
        struct wlr_scene_tree   *tree;   /* top-level; raised above everything */
        struct wlr_scene_buffer *buf;    /* the cairo-drawn kitty */
        double x, y;                     /* layout coords (roughly its feet) */
        double tx, ty;                   /* where it is headed */
        double last_t;                   /* clock of the last simulation step */
        double state_until;              /* when the current sit/nap ends */
        double phase;                    /* walk cycle */
        double blink_until;
        int    state;                    /* CAT_WALK / CAT_SIT / CAT_SLEEP */
        int    facing;                   /* +1 right, -1 left */
    } cat;

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

    /* Alt+Tab (input.c). focus_counter stamps syn_view::focus_seq on every real
     * focus change, which is the most-recently-used order Alt+Tab walks.
     *
     * `active` means Alt is still held mid-cycle. While it is set, focus_view()
     * deliberately does NOT stamp focus_seq: the running order has to stay
     * still while you tab through it, or the list would reshuffle under you and
     * a second Tab would bounce back to where you started. The stamp lands once
     * on release. `depth` is how many steps back we have walked; there is no
     * snapshot of views to dangle — the candidate list is rebuilt from live
     * views on each press. */
    struct {
        bool     active;
        int      depth;
    } alttab;
    uint64_t focus_counter;

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

    /* Workspaces are virtual desktops spanning every monitor. Exactly one is
     * shown at a time, on all outputs at once — active_workspace is the whole
     * desk's current desktop, and server_active_workspace() returns it. */
    syn_workspace_t workspaces[WORKSPACE_MAX];
    int             active_workspace;
    syn_view_t     *focused_view;

    /* layout.c: the output an AI layout request was issued for, so the async
     * response lands on the right monitor's windows (AI layout runs on the
     * focused output; the others tile). */
    syn_output_t   *ai_layout_output;

    syn_cmdbar_t    cmdbar;
    syn_overlay_t   overlay;
    syn_config_t    config;

    /* Interactive move/resize grab state (Super + mouse drag). */
    syn_cursor_mode_t cursor_mode;      /* PASSTHROUGH / MOVE / RESIZE */
    syn_view_t       *grabbed_view;
    double            grab_x, grab_y;   /* MOVE: cursor→view offset; RESIZE: cursor anchor */
    struct wlr_box    grab_geobox;      /* RESIZE: view geometry at grab start */
    uint32_t          resize_edges;     /* RESIZE: WLR_EDGE_* being dragged */

    /* snap.c: the drag-to-edge preview. `zone` is what the cursor is currently
     * over (NONE for most of a drag) and `box` the geometry it would land in —
     * computed on motion, applied on release. The preview is a translucent fill
     * plus four bright edge rects, in a tree of its own inside window_tree so it
     * paints above the windows but below the dock and menus. */
    struct {
        struct wlr_scene_tree *tree;
        struct wlr_scene_rect *fill;
        struct wlr_scene_rect *edge[4];   /* top, bottom, left, right */
        syn_snap_zone_t        zone;
        struct wlr_box         box;
    } snap;

    /* deco.c: the titlebar button currently highlighted under the pointer, and
     * the last titlebar press — a second press on the same window inside the
     * double-click window maximizes it instead of starting a drag. */
    syn_view_t       *deco_hover_view;
    uint32_t          tb_last_click_ms;
    syn_view_t       *tb_last_click_view;

    /* deco.c: `decorations_toggle` (Super+Shift+D) hides every titlebar at
     * runtime. Server state, not config state, so a config reload can't undo
     * it — synuirc's `titlebar_height = 0` is the permanent version of this. */
    bool              titlebars_hidden;

    /* The cursor image the compositor is currently forcing over its own chrome
     * or for the length of a grab (a resize arrow, a grab hand). NULL means the
     * client under the pointer owns the cursor again. Always a string literal —
     * compared by pointer, never freed. See cursor_set_deco(). */
    const char       *deco_cursor;

    /* Bumped by every CMD: launch. A capture stamps this at fork time and only
     * writes to the bar if it still matches at EOF, so a slow command cannot
     * overwrite the output of a newer one that already finished. */
    unsigned          cmdcap_gen;
    /* Live CMD: output captures (ai_interface.c). A capture lives until its
     * child closes stdout, so launching a GUI app from the bar leaves one here
     * for that app's whole life — including across a logout, which is why
     * synui_destroy() has to free them rather than let them leak. */
    struct wl_list    cmdcaps;

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

    /* Clock & Time settings panel ("Date & Time" on the Settings menu) and the
     * calendar popup (Super+Shift+T, or a click on the bar clock). Two trees
     * because the calendar can open over the settings panel and vice-versa. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } clock_ui;
    syn_clock_t     clock;

    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } cal_ui;
    syn_cal_t       cal;

    /* Control panel (Super+C, and the top entry of the waybar start menu) —
     * the live keybind list plus the toggles and panel jump-offs. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } ctlpanel_ui;

    syn_ctlpanel_t  ctlpanel;

    /* Theme manager (Super+T) — its own scene subtree, like ctlpanel. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } thememgr_ui;

    syn_thememgr_t  thememgr;

    /* Start menu (Super-tap) — synui's own, see syn_menu_t. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } menu_ui;

    syn_menu_t      menu;

    /* Bluetooth panel (Super+B) — native BlueZ client, see syn_bt_t. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } bt_ui;

    syn_bt_t        bt;

    /* Notification toasts — org.freedesktop.Notifications, see syn_notifs_t.
     * No bg rect: the stack is one cairo buffer that draws its own cards, since
     * each toast is a different height and a single rect could not back them. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_buffer *text_buf;
    } notif_ui;

    syn_notifs_t    notifs;

    /* Clipboard history (Super+V) — see syn_clipboard_t. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } clip_ui;

    syn_clipboard_t  clipboard;
    struct wl_listener clipboard_set_selection;

    /* Super-tap: Super pressed and released with nothing in between opens the
     * start menu, the way it does on every other desktop. Armed on the Super
     * press and disarmed by *any* intervening key or pointer button, so Super
     * used as a modifier (Super+E, Super+drag) never opens the menu on release.
     * Without that disarm the modifier and the tap are indistinguishable. */
    int             super_armed;

    /* Task manager panel (Ctrl+Alt+Del) — process table + resource overview. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } taskmgr_ui;

    syn_taskmgr_t   taskmgr;

    /* News aggregator panel (Super+N) — HN, Lobsters, Arch, LWN, Phoronix… */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } news_ui;

    syn_news_t      news;

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
    /* Set by ai_thread_stop() before it shuts the socket down. Shutting the
     * socket down only kills the query *in flight*: the thread would then drain
     * the requests still queued in the pipe, reconnect for each one and block on
     * a fresh socket that nobody is going to shut down — so a logout right after
     * a few workspace switches (each notifies the AI) waited on that many LLM
     * round trips, 15s+ in practice. The thread checks this and bails instead. */
    atomic_int      ai_stopping;
    int             ai_disabled;        /* --no-ai: AI thread never starts */

    /* --greeter: run as the greetd login greeter. Draws the same panel as the
     * lock screen (lock.c), but Enter hands the password to greetd to start the
     * session instead of unlocking one. See greeter.c. */
    int             greeter;
    struct {
        char     user[64];              /* the account to log in (default: UID 1000) */
        int      editing_user;          /* Tab focus is on the username field, not the password */
        int      busy;                  /* a greetd exchange is in flight */
        int      failed;                /* last attempt was rejected */
        int      sock;                  /* GREETD_SOCK fd, -1 when idle */
        int      state;                 /* syn_greetd_state_t */
        struct wl_event_source *src;    /* sock readable on the event loop */
        char     rbuf[1024];            /* partial length-prefixed reply */
        size_t   rlen;
        char     pw[512];               /* password held only across the exchange */
        int      pw_len;
    } greetd;

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
    /* Every X11 view, mapped or not, in syn_view::xw_link. The workspace lists
     * hold only mapped views, so this is the one place a never-mapped surface
     * is still reachable. Read by xwayland_unwedge(). */
    struct wl_list xw_views;
    struct wl_listener new_decoration;
    struct wl_listener new_idle_inhibitor;

    /* xdg-activation-v1: how a running app asks to be brought to the front
     * ("open this link" landing in the Firefox you already have open). Without
     * it the request is silently dropped and the window just never surfaces. */
    struct wlr_xdg_activation_v1 *xdg_activation;
    struct wl_listener            request_activate;

    /* cursor-shape-v1: clients name a cursor ("text", "grab") instead of
     * shipping a pixel buffer. Without it they fall back to drawing their own,
     * which is why the cursor changed size/theme between apps. */
    struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
    struct wl_listener                  request_set_shape;

    /* xdg-toplevel-icon-v1: a window can name its own icon, which is the only
     * way to get an icon for an app that ships no .desktop file. */
    struct wlr_xdg_toplevel_icon_manager_v1 *toplevel_icon_mgr;
    struct wl_listener                       set_icon;

    /* ipc.c: the control socket (synctl). */
    int                     ipc_fd;
    struct wl_event_source *ipc_source;
    int                     ipc_clients;
    char                    ipc_path[256];

    /* text-input-v3 + input-method-v2 (ime.c). Without these every toolkit
     * disables its IME, which means no CJK, no compose key and no emoji picker
     * — nothing that isn't a direct keysym can be typed at all. */
    struct wlr_text_input_manager_v3   *text_input_mgr;
    struct wlr_input_method_manager_v2 *input_method_mgr;
    syn_ime_t                          *ime;
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

/* Last-resort recovery for an X11 window that mapped in X but never associated
 * a wl_surface, so no compositor can ever show it (the Steam wedge). Forces an
 * X unmap/map on the one managed window matching class+title, which makes
 * Xwayland tear down and rebuild the surface. Asynchronous and best-effort:
 * safe to call when nothing is wedged, in which case it does nothing. */
void xwayland_unwedge(syn_server_t *s, const char *app_id, const char *title);

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

/* Native lock screen (lock.c). synui_lock is idempotent — a no-op if the
 * session is already locked (by this or by an ext-session-lock client), so the
 * idle timer, the power panel, logind's before-sleep and Super+L can all just
 * call it. lock_handle_key returns 1 when it consumed the key. */
void synui_lock(syn_server_t *s);
void synui_unlock(syn_server_t *s);
int  lock_handle_key(syn_server_t *s, xkb_keysym_t sym, uint32_t codepoint);
void lock_notify_activity(syn_server_t *s);      /* brighten + reset the fade */
void lock_render(syn_server_t *s);               /* repaint panes (greeter reuses) */
void lock_output_destroy(syn_output_t *o);       /* drop a dying output's lock pane (output_destroy) */

/* ── greeter.c: the greetd login greeter (synui --greeter) ── */
/* State of the greetd IPC exchange (greetd.state). */
typedef enum {
    GREETD_IDLE = 0,
    GREETD_WAIT_CREATE,   /* sent create_session, awaiting auth_message/success */
    GREETD_WAIT_AUTH,     /* sent the password, awaiting success/error */
    GREETD_WAIT_START,    /* sent start_session, greetd will kill us on success */
} syn_greetd_state_t;

/* Set up the login panel (same panes as the lock screen) and pick the account
 * to log in. Called once at startup when --greeter is given. */
void greeter_start(syn_server_t *s);
/* Enter pressed: run the greetd create/auth/start handshake for the typed
 * password. Non-blocking — driven off the wl_event_loop like lock auth. */
void greeter_submit(syn_server_t *s);

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
/* Take the cursor image for the compositor's own chrome (`name` = an xcursor
 * name), or give it back to the client under the pointer (`name` = NULL). */
void cursor_set_deco(syn_server_t *s, const char *name, uint32_t time_msec);
void focus_view(syn_server_t *s, syn_view_t *view,
                struct wlr_surface *surface);
syn_view_t *view_at(syn_server_t *s, double lx, double ly,
                    struct wlr_surface **surface, double *sx, double *sy);
struct wlr_surface *surface_at(syn_server_t *s, double lx, double ly,
                               syn_view_t **view_out, double *sx, double *sy);
void view_set_security(syn_view_t *view, win_security_t state);

/* ── deco.c — server-side decorations ────────────────────── */
/* The node to enable / raise / position for a view: its frame if it has one,
 * else the bare surface tree (override-redirect X11). */
struct wlr_scene_node *view_node(syn_view_t *view);
/* Create the per-view frame and reparent `child` (the client's surface tree)
 * into it. Called from the xdg and XWayland map paths. */
struct wlr_scene_tree *view_frame_create(syn_view_t *view,
                                         struct wlr_scene_tree *parent);
/* Border + titlebar widths for this view; 0 when fullscreen or disabled. */
int  view_deco_border(const syn_view_t *view);
int  view_deco_titlebar(const syn_view_t *view);
/* The client area inside the frame, in layout coordinates. */
void view_content_box(const syn_view_t *view, struct wlr_box *out);
/* Redraw borders + titlebar for the view's current geometry/focus/title. */
void view_update_decorations(syn_view_t *view);
/* Force the next view_update_decorations to repaint the titlebar surface even
 * if its size/focus/title are unchanged — see the definition (theme switches). */
void view_invalidate_titlebar(syn_view_t *view);
void view_deco_destroy(syn_view_t *view);
/* Destroy the frame and everything inside it (chrome + the client's surface
 * tree), clearing view->frame and view->scene_tree. Destroying the surface tree
 * alone leaves the frame leaked and view->frame dangling — use this instead. */
void view_frame_destroy(syn_view_t *view);
/* Re-apply every mapped view's geometry to its current frame box. The frame is
 * unchanged; the *content* box inside it is not, so this is what has to run
 * after anything that moves the border/titlebar metrics (the titlebar toggle, a
 * config reload). view_update_decorations alone would repaint the chrome and
 * leave the client sized and offset for the old one. */
void deco_refresh_all(syn_server_t *s);
/* Hide/show every titlebar at runtime — the `decorations_toggle` action.
 * Persists the new state, so both it and the control panel's row survive a
 * restart. */
void deco_toggle_titlebars(syn_server_t *s);
/* Persisted titlebar toggle (~/.config/synui/deco.state). Loaded once at
 * startup, before any view is mapped, so no refresh is owed; written by
 * deco_toggle_titlebars() itself. */
void deco_state_load(syn_server_t *s);
void deco_state_save(syn_server_t *s);
/* What decoration (if any) sits under a layout-space point. Fills *edges with
 * the WLR_EDGE_* to resize from when the region is DECO_BORDER. */
syn_view_t *deco_at(syn_server_t *s, double lx, double ly,
                    syn_deco_region_t *region, uint32_t *edges);
/* Repaint button highlights as the pointer moves across titlebars. */
void deco_hover_update(syn_server_t *s, double lx, double ly, uint32_t time_msec);
/* The cursor a MOVE/RESIZE grab holds for its whole duration. */
const char *deco_grab_cursor(syn_server_t *s, syn_cursor_mode_t mode,
                             uint32_t edges);
/* (Re)place the invisible resize-grab ring outside the window. Called from
 * view_update_decorations; the window's edges and corners are only realistically
 * grabbable because of it. */
void view_grab_ring_update(syn_view_t *view);
/* (Re)build the window's scenefx drop shadow at the frame's current geometry,
 * folding the fade/focus alpha in like the borders. Called from
 * view_update_decorations; a no-op (disables the node) when shadows are off or
 * the window is maximized/fullscreen. */
void view_shadow_update(syn_view_t *view);
/* Maximize/restore for real: fills the output's usable box and leaves the
 * tiling flow, restoring the previous geometry (and tiled-ness) on the way
 * back. */
void view_apply_maximized(syn_server_t *s, syn_view_t *view, int maximized);

/* ── input.c ─────────────────────────────────────────────── */
/* Start a compositor-run pointer grab on a view: SYNUI_CURSOR_MOVE, or
 * SYNUI_CURSOR_RESIZE from `edges` (a WLR_EDGE_* mask; 0 = derive from the
 * cursor's quadrant). The same path the titlebar and the grab ring use, exposed
 * so a CSD client's own xdg_toplevel.move/.resize request runs it too. */
void view_begin_interactive(syn_view_t *view, syn_cursor_mode_t mode,
                            uint32_t edges);

/* ── snap.c ──────────────────────────────────────────────── */
/* Drag-to-edge window snapping ("snap to fit"). The move grab in input.c drives
 * these: motion picks a zone and paints the preview, release applies it. */
/* Which zone the cursor at (lx, ly) is in, and the box that zone fills. */
syn_snap_zone_t snap_zone_at(syn_server_t *s, double lx, double ly,
                             struct wlr_box *box);
/* Called on every motion of a MOVE grab: updates (and shows/hides) the preview. */
void snap_drag_motion(syn_server_t *s, double lx, double ly);
/* Called when a MOVE grab is released: snaps `view` into the previewed zone, if
 * any, and clears the preview. */
void snap_drag_end(syn_server_t *s, syn_view_t *view);
/* Hide the preview without applying it (grab cancelled, view destroyed). */
void snap_preview_hide(syn_server_t *s);
/* Put a snapped window back to its pre-snap geometry, keeping it under the
 * cursor — a drag off an edge un-snaps, as it does everywhere else. */
void snap_release_view(syn_server_t *s, syn_view_t *view, int keep_under_cursor);

/* ── layout.c ────────────────────────────────────────────── */
void layout_apply(syn_server_t *s, syn_workspace_t *ws);
void view_apply_fullscreen(syn_server_t *s, syn_view_t *view, int fs);
void view_apply_minimized(syn_server_t *s, syn_view_t *view, int minimized);
/* Scale a sub-native fullscreen X11 client up to fill its output (xwayland.c);
 * no-op for xdg, override-redirect, multi-surface or already-filling clients. */
void view_fullscreen_rescale(syn_view_t *view);
void workspace_focus_first(syn_server_t *s, syn_workspace_t *ws);
/* The tiling passes act on one (workspace, output) pair: the windows of ws that
 * live on o. layout_apply() runs them for every output showing ws. */
void layout_tile(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o);
void layout_monocle(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o);
void view_resize(syn_view_t *view, int x, int y, int w, int h);
void layout_float_place(syn_server_t *s, syn_view_t *view);
void layout_move_in_stack(syn_server_t *s, syn_view_t *view, int dir);
void layout_adjust_master(syn_server_t *s, syn_workspace_t *ws, float delta);
/* Re-home a window onto another monitor, keeping it on its current desktop. */
void view_set_output(syn_server_t *s, syn_view_t *view, syn_output_t *o);
void layout_request_ai(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o);
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
void cmdbar_ask_window(syn_server_t *s);
void cmdbar_hide(syn_server_t *s);
void cmdbar_key(syn_server_t *s, uint32_t keysym);
void cmdbar_submit(syn_server_t *s);
void overlay_toggle(syn_server_t *s);
void overlay_update(syn_server_t *s);
void overlay_render(syn_server_t *s, struct wlr_renderer *renderer,
                    int width, int height);
void execute_ai_action(syn_server_t *s, const char *response);
/* Drop every in-flight CMD: output capture. Teardown only: a capture outlives
 * its command only to catch late output, and at shutdown there is no bar left
 * to write it to. */
void cmdcap_stop_all(syn_server_t *s);

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

/* Resolve <config dir>/<name> into buf, where the config dir is
 * $XDG_CONFIG_HOME/synui (preferred) or ~/.config/synui. Every file synui
 * reads or writes under its config dir MUST go through this — synuirc,
 * outputs.conf and the *.state files used to resolve their paths separately,
 * and the .state ones ignored XDG_CONFIG_HOME, so a non-default
 * XDG_CONFIG_HOME split the config across two directories (settings from one,
 * persisted picker/dock/power choices from the other). Returns false if
 * neither variable is set, in which case buf is untouched. */
bool syn_config_path(char *buf, size_t n, const char *name);

/* Create the config dir if absent. Call before writing a *.state file. */
void syn_config_ensure_dir(void);

/* ── render.c ────────────────────────────────────────────── */
void synui_ui_init(syn_server_t *s);
void synui_render_welcome(syn_server_t *s);
void synui_welcome_hide(syn_server_t *s);
/* welcome.state — persists the menu's "Show At Startup" row across restarts.
 * Loaded from config.c (after synuirc), saved when the row is toggled. */
void welcome_state_load(syn_config_t *cfg);
void welcome_state_save(syn_config_t *cfg);
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

/* ── News aggregator (news.c) ────────────────────────────── */
void news_init(syn_server_t *s);
void news_finish(syn_server_t *s);
void news_show(syn_server_t *s);
void news_hide(syn_server_t *s);
void news_toggle(syn_server_t *s);
/* Modal while visible; returns 1 if the key was consumed. */
int  news_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* Age of an item as "3m"/"5h"/"2d", for the panel's right-hand column. */
void news_age(time_t ts, char *buf, size_t n);
/* Host of a URL ("lwn.net"), sans "www.". Empty for a URL we can't parse. */
void news_host(const char *url, char *buf, size_t n);
/* Longest prefix of `len` bytes that doesn't end inside a UTF-8 sequence.
 * Anything cutting text for display must go through this: cairo_show_text()
 * poisons its whole context on invalid UTF-8, and everything drawn afterwards
 * silently disappears. */
size_t news_utf8_trim(const char *b, size_t len);
void synui_render_news(syn_server_t *s);

/* ── Game mode (game.c) ──────────────────────────────────── */
/* Startup: publish the (off) state for waybar's indicator, so a file left
 * behind by a synui that died mid-game cannot show a phantom game. */
void game_init(syn_server_t *s);
/* Idempotent decision point: call after any fullscreen change, map, or unmap.
 * Enters/leaves game mode (suspend synapd, hold off idle) as needed. */
void game_reevaluate(syn_server_t *s);
/* Super+G — cycle auto → forced-on → forced-off → auto. */
void game_toggle(syn_server_t *s);

/* ── Cat mode (cat.c) ────────────────────────────────────── */

/* Canvas the kitty is drawn into. Deliberately small — a desk pet, not a
 * mascot: at 64x48 it reads clearly without burying what is under it. */
#define CAT_W 64
#define CAT_H 48

enum { CAT_WALK, CAT_SIT, CAT_SLEEP };

/* Everything cat_paint needs. Kept free of syn_server_t so the drawing can be
 * rendered to a PNG by tests/cat_render_test.c — "it doesn't look like a cat"
 * is the one bug here that no assertion will ever catch. */
typedef struct {
    int    state;      /* CAT_WALK / CAT_SIT / CAT_SLEEP */
    double phase;      /* walk cycle */
    double now;        /* drives tail sway, ear twitch, z's */
    bool   blinking;
} cat_pose_t;

void cat_paint(cairo_t *cr, const cat_pose_t *pose);   /* faces right */
void cat_toggle(syn_server_t *s);
/* Advance + redraw the kitty; true if this output wants another frame. */
bool cat_tick(syn_output_t *o, double now);
/* Shutdown: restart synapd if we suspended it, so synui exiting mid-game
 * doesn't leave the box with no AI. */
void game_finish(syn_server_t *s);

void power_show(syn_server_t *s);
void power_hide(syn_server_t *s);
void power_toggle(syn_server_t *s);
int  power_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);

/* ── Clock & Time settings + calendar (clock.c) ──────────── */
void clock_init(syn_server_t *s);
void clock_finish(syn_server_t *s);
void clock_state_load(syn_server_t *s);
void clock_state_save(syn_server_t *s);
void clock_show(syn_server_t *s);
void clock_hide(syn_server_t *s);
void clock_toggle(syn_server_t *s);
int  clock_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
const char *clock_row_label(int row);
void clock_row_value(syn_server_t *s, int row, char *out, size_t n);
void clock_local_string(syn_server_t *s, char *out, size_t n);
void clock_zone_string(syn_server_t *s, int i, char *out, size_t n);
void synui_render_clock(syn_server_t *s);
/* Calendar popup — shares clock.c's date helpers. */
void calendar_show(syn_server_t *s);
void calendar_hide(syn_server_t *s);
void calendar_toggle(syn_server_t *s);
int  calendar_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
int  calendar_days_in_month(int year, int mon);
int  calendar_first_weekday(int year, int mon);
void synui_render_calendar(syn_server_t *s);

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

/* ── Control panel (ctlpanel.c) ──────────────────────────── */
void ctlpanel_show(syn_server_t *s);
void ctlpanel_hide(syn_server_t *s);
void ctlpanel_toggle(syn_server_t *s);
/* Per-frame poll for the AI-backend row; 1 while it wants another frame. */
int  ctlpanel_tick(syn_server_t *s);
/* Modal key handling while the panel is open, as in filters_key. */
int  ctlpanel_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* Settings-column row text. value[] is filled with the row's current state
 * ("on"/"off"/"GPU"), or left empty for a jump-off row, which has none. */
const char *ctlpanel_row_label(int row);
void ctlpanel_row_value(syn_server_t *s, int row, char *buf, size_t n);
int  ctlpanel_row_selectable(int row);
/* The shortcuts column, rebuilt from the live bind table on every render.
 * Returns how many rows were written into out[] (at most max). */
int  ctlpanel_shortcuts(syn_server_t *s, syn_ctl_shortcut_t *out, int max);
/* How many shortcut rows the panel has room to draw — render.c owns the
 * geometry, ctlpanel.c owns the scroll clamp, so they have to agree. */
#define CTL_SHORTCUT_ROWS  16
void synui_render_ctlpanel(syn_server_t *s);

/* ── Theme manager (theme.c) ─────────────────────────────── */
/* Apply a preset: overwrite the border/titlebar colours + default opacities in
 * cfg, re-decorate every mapped window, repaint, and (for the app colour-scheme)
 * write kdeglobals / GTK / Firefox so Dolphin & co. follow. Persists theme.state
 * unless `save` is 0 (startup load passes 0 — it is applying what it just read). */
void theme_apply(syn_server_t *s, syn_theme_t theme, int save);
/* Copy a preset's colours + opacity levels into a config only (no server) —
 * what config.c calls for a synuirc `theme =` line at parse time. */
void theme_load_colors(syn_config_t *cfg, syn_theme_t theme);
void theme_state_load(syn_server_t *s);   /* lay theme.state over the config default */
const char *theme_name(syn_theme_t t);    /* display label, e.g. "Windows XP" */
/* Two-tone swatch for the picker: the caption colour and the focus accent. */
void theme_preview_colors(syn_theme_t t, float caption[4], float accent[4]);
/* Cache the panel accent render.c draws every synui panel with. Called from
 * theme_load_colors so a theme switch (or a synuirc `theme =`) reskins the UI. */
void render_set_panel_accent(const float rgb[4]);

/* Shared translucency controls behind the control-panel + theme-manager sliders.
 * set_opacity clamps the focused level to 0.50..1.00 and derives the unfocused
 * level just below it; set_enabled flips the master switch and, when turning on
 * a still-opaque desktop, drops to a visibly translucent default. Both re-push
 * alpha to every window and persist to theme.state. */
void transparency_set_opacity(syn_server_t *s, float active);
void transparency_set_enabled(syn_server_t *s, int on);

void theme_show(syn_server_t *s);
void theme_hide(syn_server_t *s);
void theme_toggle(syn_server_t *s);
int  theme_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
void synui_render_thememgr(syn_server_t *s);

/* ── Clipboard history (clipboard.c) ─────────────────────── */
void clipboard_init(syn_server_t *s);
void clipboard_finish(syn_server_t *s);
void clipboard_show(syn_server_t *s);
void clipboard_hide(syn_server_t *s);
void clipboard_toggle(syn_server_t *s);
void clipboard_clear(syn_server_t *s);
int  clipboard_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
void synui_render_clipboard(syn_server_t *s);

/* ── Night light (nightlight.c) ──────────────────────────── */
/* Writes the gamma LUT on every output. No-op where the backend has no gamma
 * (headless/pixman). wlr_gamma_control_manager_v1 stays exported, so wlsunset
 * still works for anyone who prefers it — last writer wins. */
void nightlight_apply(syn_server_t *s);
void nightlight_toggle(syn_server_t *s);
/* A new output comes up at identity and has to be told, or a second monitor
 * plugged in with night light on stays blue while the first is warm. */
void nightlight_output_added(syn_server_t *s, syn_output_t *o);

/* ── logind (logind.c) ───────────────────────────────────── */
/* systemd-logind: lock before a sleep synui did not initiate (the lid), and set
 * the backlight — both over sd-bus, so neither needs root, a udev rule, or
 * brightnessctl. Safe with no system bus and on machines with no backlight. */
void logind_init(syn_server_t *s);
void logind_finish(syn_server_t *s);
/* Step the backlight by a percentage of its range (+/-). No-op where there is
 * no panel. */
void logind_brightness_step(syn_server_t *s, int pct);

/* ── Notifications (notif.c) ─────────────────────────────── */
/* notif_init takes org.freedesktop.Notifications on the session bus. Safe where
 * there is no bus or the name is already owned (a stray mako): it logs, leaves
 * the name to whoever has it, and synui simply draws no toasts. */
void notif_init(syn_server_t *s);
void notif_finish(syn_server_t *s);
void synui_render_notifs(syn_server_t *s);
/* Dismiss the toast under (lx, ly) in layout coords. Returns 1 if one was hit,
 * so the click is not also delivered to whatever is behind it. */
int  notif_click(syn_server_t *s, double lx, double ly);
/* Which toast is under a layout-space point, or -1; stack gets the whole
 * stack's box. render.c owns toast geometry, so it answers this — one
 * definition of where a toast is, rather than two that disagree and eat
 * clicks. */
int  synui_notif_hit(syn_server_t *s, double lx, double ly,
                     struct wlr_box *stack);

/* ── Bluetooth (bt.c) ────────────────────────────────────── */
/* bt_init opens the system bus and exports the pairing agent; it is safe to call
 * where there is no bus or no bluetoothd — Bluetooth just stays unavailable and
 * the panel says so. */
void bt_init(syn_server_t *s);
void bt_finish(syn_server_t *s);
void bt_show(syn_server_t *s);
void bt_hide(syn_server_t *s);
void bt_toggle(syn_server_t *s);
int  bt_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* Pointer, from input.c while the panel is up — as with the start menu, except
 * that a click only selects: connect/pair/trust/forget are several actions and
 * one click cannot mean all of them. */
void bt_motion(syn_server_t *s, double lx, double ly);
void bt_click(syn_server_t *s, double lx, double ly);
void bt_scroll(syn_server_t *s, double delta);
int  bt_first_row(const syn_bt_t *b);
/* How many rows the list shows: every device when show_all, else only the ones
 * dev_listable() keeps (dev_cmp sorts those to the front, so it is devs[0..n)).
 * render.c sizes and clamps against this; bt.c moves the selection within it. */
int  bt_shown_count(const syn_bt_t *b);
/* How a device is written in the list: its name, or — for the nameless, whose
 * BlueZ Alias is just the address spelled with dashes — what kind of thing it
 * is, plus the address. See bt.c. */
void bt_dev_label(const syn_bt_dev_t *d, char *out, size_t n);

/* Panel geometry: render.c draws with it, bt.c hit-tests against it. */
#define BT_ROWS      12
#define BT_W        520
#define BT_ROW_H     26
#define BT_ROW_ASC   16
#define BT_TOP       98
#define BT_FOOTER    52
#define BT_PAD       18
void synui_render_bt(syn_server_t *s);

/* ── Start menu (menu.c) ─────────────────────────────────── */
void menu_show(syn_server_t *s);
void menu_hide(syn_server_t *s);
void menu_toggle(syn_server_t *s);
/* Modal key handling while the menu is open, as in ctlpanel_key. Returns 1 if
 * the key was consumed. */
int  menu_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* Pointer, from input.c while the menu is up. The menu is modal for the pointer
 * exactly as the dock's context menu is: hover selects, left click activates,
 * anything else dismisses. */
void menu_motion(syn_server_t *s, double lx, double ly);
void menu_click(syn_server_t *s, double lx, double ly);
/* Wheel. delta is in the same sense wlroots reports it: positive is down. */
void menu_scroll(syn_server_t *s, double delta);
/* The view[] row drawn at the top of the panel — see menu.c. */
int  menu_first_row(const syn_menu_t *m);

/* The y of the first row's baseline. The root has no breadcrumb (the "SYNAPSE"
 * brand line that once sat there is gone), so its rows — and the search and
 * separator lines above them — ride up by MENU_ROOT_SHIFT to close the gap it
 * left; a submenu keeps the full head for its page name. render.c draws with
 * this and menu.c hit-tests with it, so it lives here where both can see it. */
int  menu_top_y(const syn_menu_t *m);

/* Panel geometry. render.c draws it and menu.c hit-tests and scroll-clamps
 * against it, so the two have to agree — hence here rather than in either (as
 * with CTL_SHORTCUT_ROWS). Rows step MENU_ROW_H from a first baseline at
 * MENU_TOP, and MENU_ROW_ASC is how far the row's band rises above its
 * baseline: text sits on the baseline, the highlight is drawn around it. */
#define MENU_ROWS      22
#define MENU_W        420
#define MENU_ROW_H     24
#define MENU_ROW_ASC   15
#define MENU_TOP       92
#define MENU_FOOTER    46
#define MENU_PAD       18
/* The blank breadcrumb line the root no longer needs. Reclaimed at the root
 * (see menu_top_y); kept on submenu pages, which draw their name in it. */
#define MENU_ROOT_SHIFT 24
/* How long the pointer must rest on a submenu row before its page opens. Long
 * enough that sliding across the category rows to reach a row below them does
 * not flip through every page; short enough to feel like a hover, not a wait. */
#define MENU_HOVER_OPEN_MS 350
void synui_render_menu(syn_server_t *s);

/* Run a bind action by name (input.c owns the dispatch table). The control
 * panel's rows are actions, so they go through exactly the path a keybind
 * does rather than reimplementing it. */
void synui_binding_execute(syn_server_t *s, const char *action, const char *arg);
/* Open the waybar start menu, by synthesizing a click on its bar surface —
 * waybar's menu is a GTK popup with no IPC to open it. See input.c. */
void synui_start_menu_open(syn_server_t *s);

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
/* ── ipc.c ───────────────────────────────────────────────── */
/* Control socket: JSON state queries + `dispatch <action>` for every keybind
 * action. $XDG_RUNTIME_DIR/synui-$WAYLAND_DISPLAY.sock, 0600. */
void ipc_setup(syn_server_t *s);
void ipc_destroy(syn_server_t *s);

/* ── anim.c ──────────────────────────────────────────────── */
bool anim_tick(syn_server_t *s, double now);
void anim_fade_in(syn_view_t *view);
void anim_fade_out_and_hide(syn_view_t *view);
void anim_reset(syn_view_t *view);
void anim_apply_alpha(syn_view_t *view);
/* The window's settled translucency (config.active/inactive_opacity gated by
 * config.transparency), by whether it is the focused view. 1.0 when off. */
float anim_view_opacity(syn_view_t *view);
/* Re-push opacity to every mapped window — after a transparency toggle or a
 * theme change moved the active/inactive levels. */
void anim_apply_alpha_all(syn_server_t *s);
/* Cheap per-commit re-push of buffer opacity — scenefx resets it on every
 * surface commit. Call from the surface commit handlers, never per frame tick. */
void anim_reapply_opacity(syn_view_t *view);

/* ── ime.c ───────────────────────────────────────────────── */
void ime_setup(syn_server_t *s);
void ime_destroy(syn_server_t *s);
/* Keyboard focus moved: re-point the IME at the newly focused text field. */
void ime_set_focus(syn_server_t *s, struct wlr_surface *surface);
/* True when the IME grabbed the keyboard and consumed the event — the key must
 * then NOT reach the application (it's being composed, not typed). */
bool ime_handle_key(syn_server_t *s, struct wlr_keyboard *kb,
                    uint32_t time_msec, uint32_t keycode,
                    enum wl_keyboard_key_state state);
bool ime_handle_modifiers(syn_server_t *s, struct wlr_keyboard *kb);

const syn_icon_entry_t *icon_lookup(const char *app_id);
/* A window named its own icon (xdg-toplevel-icon). Attach it to that app's
 * cache entry so the dock's existing icon_lookup() picks it up — this is the
 * only icon source for an app that ships no .desktop file (Wine, Electron). */
void icon_provide_name(const char *app_id, const char *icon_name);
/* Draw a coloured monogram chip (first letter of app_id, uppercased) into a
 * size x size box at (x, y) — the fallback when icon_lookup() found no icon
 * file (SVG icon themes, or nothing on disk at all: both out of scope). */
void icon_draw_monogram(cairo_t *cr, const char *app_id,
                        double x, double y, double size);

/* ── dock.c ──────────────────────────────────────────────── */
void dock_init(syn_server_t *s);                  /* load config; entries start empty */
void dock_output_created(syn_output_t *o);        /* create this output's dock tree */
void dock_output_destroy(syn_output_t *o);        /* destroy this output's dock tree */

/* ── Start-menu launcher (launcher.c) ────────────────────── */
void launcher_output_created(syn_output_t *o);    /* create this output's button */
void launcher_output_destroy(syn_output_t *o);    /* destroy it */
void launcher_render_all(syn_server_t *s);         /* rebuild buffers (style change) */
void launcher_relayout(syn_server_t *s);           /* reposition + fullscreen hide */
void launcher_toggle_style(syn_server_t *s);       /* flip text↔logo, redraw, persist */
void launcher_state_load(syn_config_t *cfg);       /* lay launcher.state over synuirc */
/* True if (lx,ly) is over a visible launcher button; used by the click router. */
bool launcher_at(syn_server_t *s, double lx, double ly);
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
void dock_wake(syn_server_t *s);                  /* re-tick every output's dock */
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
