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

/* The GGUF header reader. Named syn_gguf.h rather than gguf.h on purpose:
 * llama.cpp installs its own /usr/include/gguf.h, and this tree is compiled
 * with the llama include path in reach. */
#include "syn_gguf.h"

/* BTN_LEFT and friends. Every panel that takes a click has to name the button
 * it is answering, so the codes belong here rather than in fifteen separate
 * includes that would each have to be remembered. */
#include <linux/input-event-codes.h>

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
/* The floating desktop's own tiler (layout_float_arrange). Its whole point is
 * that it does NOT fill the screen — the inset is a percentage of the usable
 * box kept clear at all four edges, so the wallpaper reads as part of the
 * composition instead of being something the windows have covered up. Separate
 * from `gap` on purpose: tiling wants a hairline between windows, this wants a
 * margin you can see. */
#define FLOAT_INSET_DEFAULT  8    /* % of the usable box, per edge */
#define FLOAT_GAP_DEFAULT    24   /* px between floating tiles */
#define FLOAT_INSET_MAX      40   /* beyond this the tiles are smaller than the margin */

/* ── synapd IPC ──────────────────────────────────────────── */
#define SYNAPD_SOCKET       "/run/synapd/synapd.sock"

/* The inference device synui-ai-backend last set: "gpu", "cpu" or "off", and
 * absent until something has toggled it (synapd auto-detects, so that reads as
 * "auto"). In /etc because "off" now MASKS synapd and therefore outlives a
 * reboot — the record of it has to outlive one too, and the old path was on a
 * tmpfs. The legacy path is read as a fallback so an upgraded desktop keeps its
 * label until the next toggle; it can go once no /run copy is left anywhere. */
#define SYNAPD_BACKEND_STATE        "/etc/synapd/backend"
#define SYNAPD_BACKEND_STATE_LEGACY "/run/synapd/backend"

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
#define ANIM_RISE_PX_DEF     24   /* synuirc anim_rise_px; how far Rise travels */

/*
 * ── What an animation does, per event ────────────────────
 *
 * Two events, because they are two different questions: a window arriving is
 * about that one window, and a desktop switch is about every window at once.
 * They used to share `animation_ms` and one hard-coded fade, so turning the
 * desktop switch down also turned window openings down.
 *
 * The styles are limited by what wlr_scene can actually do without lying to
 * clients — see the header of anim.c. Opacity is free, and POSITION is free
 * (moving a scene node configures nobody). SIZE is not, so there is no "zoom":
 * animating a window's size means re-configuring the client every frame.
 */
/*
 * Opening only. A window CLOSING cannot be animated here: the client's buffer
 * is gone the moment it unmaps, and fading what is left would mean holding a
 * snapshot texture wlr_scene does not hand us. Better an honest three options
 * than a fourth that quietly does nothing.
 */
typedef enum {
    ANIM_WINDOW_NONE = 0,  /* windows appear at full opacity, in place        */
    ANIM_WINDOW_FADE,      /* fade in on open                                 */
    ANIM_WINDOW_RISE,      /* fade, and glide up anim_rise_px into place      */
    ANIM_WINDOW_COUNT,     /* keep last — the panel steps on it               */
} syn_anim_window_t;

typedef enum {
    ANIM_WS_NONE = 0,      /* the desktop is simply the other one now         */
    ANIM_WS_FADE,          /* outgoing windows fade out, incoming fade in     */
    ANIM_WS_SLIDE,         /* both desks slide, in the direction you switched */
    ANIM_WS_COUNT,         /* keep last                                       */
} syn_anim_ws_t;

/* Shared by both events: an animation system whose halves decay differently
 * reads as two systems. Order matches ctl_names_anim_curve[] and the spellings
 * config.c accepts. */
typedef enum {
    ANIM_CURVE_EASE_OUT = 0,   /* fast, then settling — the default          */
    ANIM_CURVE_LINEAR,         /* constant speed                             */
    ANIM_CURVE_EASE_IN_OUT,    /* eases at both ends                         */
    ANIM_CURVE_EASE_IN,        /* slow, then arriving fast                   */
    ANIM_CURVE_COUNT,          /* keep last                                  */
} syn_anim_curve_t;

/* The synuirc vocabulary for the three enums above, indexed by them. These
 * spellings are a FORMAT — renaming one turns an existing config line into an
 * unknown word — and they live in config.c beside the parser that needs them,
 * exactly like syn_focus_mode_names. */
extern const char *const syn_anim_window_names[ANIM_WINDOW_COUNT];
extern const char *const syn_anim_ws_names[ANIM_WS_COUNT];
extern const char *const syn_anim_curve_names[ANIM_CURVE_COUNT];

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

/* ── Panel pointer geometry (hit.c) ──────────────────────────
 *
 * Every panel the compositor draws is the same shape: a rectangle somewhere on
 * the focused output, with a column of fixed-pitch rows inside it. That is all
 * a pointer needs to know, so it is recorded once, in one struct, rather than
 * each panel growing its own set of x/y/w/h fields — which is how the
 * Bluetooth panel (the first one to get a pointer) did it, and doing that
 * fourteen more times would be fourteen more places for the drawn geometry and
 * the hit-tested geometry to drift apart.
 *
 * The render function is the only writer, because the render function is the
 * only code that knows where the panel actually landed: the rect is centred on
 * whichever output has focus, and several panels size themselves to their
 * content. It writes this on every paint and blanks it when the panel is
 * hidden, so a hit test can never be answered from a stale rect.
 *
 * All coordinates are LAYOUT coords — the same space s->cursor->x/y are in —
 * so a hit test is a comparison and never a transform.
 */
typedef struct {
    int x, y, w, h;      /* panel rect; w == 0 means "not on screen" */
    int row_x, row_y;    /* top-left of row 0's hit box */
    int row_w, row_h;    /* one cell's hit box; both are also the pitch */
    int rows;            /* rows currently drawn — the hit-testable window */
    /* Cells per row. 1 for every list, which is what hit_set_rows() sets and
     * what a zeroed struct is read as. Grids (the emoji picker) set it with
     * hit_set_grid() and then row_w is one CELL rather than the full width. */
    int cols;
    /* List index of the first DRAWN row, for the panels whose list scrolls.
     * Several of them (the theme manager) derive it from the selection on every
     * render and keep no scroll position at all, so the only place that knows
     * where the window sits is the render pass — which is exactly why it is
     * recorded here rather than recomputed by the hit test. 0 for the panels
     * that draw their whole list. */
    int first;
    /* The corner close button, when the panel draws one (see syn_panel_close_t).
     * Zero width means there is none — the panel does not offer one, or
     * `panel_close` is set to click-off. In here with the rest of the geometry
     * for this file's whole reason: render.c writes it, the panel reads it, and
     * a second copy elsewhere is how the drawn button and the clickable button
     * drift apart. hit_set_panel() clears it, so a stale rect can never be read
     * back from the panel that used this struct last. */
    int close_x, close_y, close_w, close_h;
    /* The header strip a windowed panel is dragged by. Zero width when the
     * panel is not in window mode. Cleared by hit_set_panel() like the rest. */
    int drag_x, drag_y, drag_w, drag_h;
} syn_hit_t;

/* Record the panel rect. Call once px/py/pw/ph are known. */
void hit_set_panel(syn_hit_t *g, int x, int y, int w, int h);
/* Record the row grid in PANEL-LOCAL coords — the same numbers the cairo draw
 * uses — so no render function has to add its own origin back on by hand.
 * Must follow hit_set_panel(). `n` is the number of rows actually drawn, not
 * the number that exist: a scrolling list only hit-tests its visible window. */
void hit_set_rows(syn_hit_t *g, int lx, int ly, int w, int h, int n);
/* The same, for a panel whose items sit in a GRID (the emoji picker). cell_w
 * and cell_h are the pitch as well as the size, so a gutter goes inside the
 * cell. Follows hit_set_panel(), like hit_set_rows(). */
void hit_set_grid(syn_hit_t *g, int lx, int ly,
                  int cell_w, int cell_h, int cols, int rows);
/* Record which list index the first drawn row is, for a scrolling list. Follows
 * hit_set_rows(); leaving it out means "the window starts at 0". */
void hit_set_first(syn_hit_t *g, int first);
/* Record the corner close button, in PANEL-LOCAL coordinates as hit_set_rows()
 * takes them. Call it AFTER hit_set_panel(), which clears the rect. */
void hit_set_close(syn_hit_t *g, int lx, int ly, int w, int h);
/* Is the cursor on it? False whenever there is no button, so a panel can ask
 * unconditionally instead of testing the config first. */
int  hit_in_close(const syn_hit_t *g, double lx, double ly);
/* The drag handle, same contract as the close button above. */
void hit_set_drag(syn_hit_t *g, int lx, int ly, int w, int h);
int  hit_in_drag(const syn_hit_t *g, double lx, double ly);
/* Blank the rect. A hidden panel must hit-test as nothing at all. */
void hit_clear(syn_hit_t *g);
int  hit_in_panel(const syn_hit_t *g, double lx, double ly);
/* Row index under (lx,ly) counting from the first DRAWN row, or -1 for the
 * panel's chrome and for anywhere outside it. Callers holding a scrolled list
 * add their own first-row offset. */
int  hit_row_at(const syn_hit_t *g, double lx, double ly);
/* Column under (lx,ly), or -1 off the grid. Always 0 on a list, so only the
 * grid panels have any reason to ask. */
int  hit_col_at(const syn_hit_t *g, double lx, double ly);
/* The same test in LIST coordinates: hit_row_at() plus the scroll offset above,
 * so a panel with a scrolling list never has to add it back on itself and get it
 * wrong on the one path that forgot. -1 for a miss, as ever. */
int  hit_index_at(const syn_hit_t *g, double lx, double ly);

/* ── How a panel is dismissed ────────────────────────────────
 *
 * Every compositor-drawn panel closes when you click off it, and for a list of
 * rows that is right: the panel is a menu, and clicking away from a menu means
 * you are done with it.
 *
 * It is wrong for a panel you AIM at. The calculator is thirty small keys, so a
 * near-miss on "7" is a click on the desktop — which throws away the expression
 * you were half way through typing, with no undo. velle: "the calc is click off
 * to close, that's probably problematic for using."
 *
 * So it is a choice, and it applies to the three panels you work IN rather than
 * pick from: the calculator, the control panel and the task manager. Esc closes
 * every panel either way, so there is always a way out even if a button fails
 * to draw.
 *
 * Deliberately either/or rather than both. A panel with a close button that
 * ALSO vanishes on a near-miss has not fixed anything.
 */
typedef enum {
    /* Click anywhere off the panel to close it. What every panel did before
     * this setting existed, and still the right default for a menu. */
    SYN_PANEL_CLOSE_CLICKOFF = 0,
    /* A close button in the top-right corner. Clicking off does not close it,
     * but is still SWALLOWED: the panel stays modal, so a near-miss cannot act
     * on the window underneath. */
    SYN_PANEL_CLOSE_BUTTON,
    /* Not chrome at all — a window. A close button, a header you can DRAG it
     * around by, and clicks elsewhere go straight through to whatever is under
     * them: no swallowing, no stolen pointer, and the keyboard goes back to the
     * window you clicked. The panel simply stays where you put it.
     *
     * velle: "the menus don't need to force focus they can just be a normal
     * window that you can still click other places and drag around."
     *
     * The panel keeps the keyboard until you click something else, so opening
     * one and typing still works; clicking away hands keys back without closing
     * anything. Esc still closes it while it has the keyboard. */
    SYN_PANEL_CLOSE_WINDOW,
    SYN_PANEL_CLOSE_COUNT,
} syn_panel_close_t;

/* Per-panel window state: where it has been dragged to, and whether it still
 * has the keyboard. Both only mean anything in SYN_PANEL_CLOSE_WINDOW.
 *
 * The offset is from where the panel WOULD be centred, not an absolute
 * position, so a windowed panel still lands somewhere sensible when the monitor
 * changes size or the panel's own height changes (the calculator's tape grows).
 *
 * `kbd` is the whole of the focus model, and it is deliberately not more than
 * this: a windowed panel takes the keyboard when it is opened or clicked, and
 * gives it back the moment you click anything else. That is what makes typing
 * into a freshly-opened calculator work while still letting you click into a
 * terminal without closing it. */
typedef struct {
    int dx, dy;   /* offset from centred, in layout pixels */
    int kbd;      /* holds the keyboard */
} syn_panel_win_t;

/* A windowed panel being dragged. One at a time by construction — you have one
 * pointer — so this is a single block on the server rather than per panel. */
typedef enum { SYN_PDRAG_NONE = 0, SYN_PDRAG_CALC, SYN_PDRAG_CTLPANEL,
               SYN_PDRAG_TASKMGR } syn_pdrag_t;

/* ── The panel pointer contract ──────────────────────────────
 *
 * Every compositor-drawn panel exposes the same three functions, and input.c
 * walks them in the same order it walks the key handlers. They exist because
 * these panels were keyboard-only: they were each written as "press Super+X,
 * then arrow around", and a desktop where half the settings cannot be clicked
 * is a desktop that is half broken for anyone holding a mouse.
 *
 *   int <p>_motion(syn_server_t *s, double lx, double ly);
 *   int <p>_click (syn_server_t *s, double lx, double ly, uint32_t button,
 *                 uint32_t time_msec);
 *   int <p>_scroll(syn_server_t *s, double lx, double ly, double delta);
 *
 * All three return 1 if the panel was open and therefore consumed the event,
 * and 0 if it was not — the same "did you take it" answer <p>_key gives, so the
 * pointer chain in input.c reads like the keyboard chain above it.
 *
 * What they must do, so that learning one panel is learning all of them:
 *
 *   motion — the row under the pointer becomes the selected row. Hover IS the
 *            cursor; nothing else moves and nothing fires.
 *   click  — BTN_LEFT on a row does that row's primary keyboard action, by
 *            calling the panel's own path rather than a second copy of it.
 *            On most panels that is Enter. On the panels whose Enter is a second
 *            spelling of Esc (filters, power) it is instead Right — the key that
 *            drives the row — because a click that closed the panel you just
 *            aimed at would be indistinguishable from a misfire. BTN_RIGHT on
 *            such a cycling row steps the other way, i.e. Left.
 *            A click on the panel's chrome does nothing but is swallowed: these
 *            panels are modal, and letting a near-miss fall through to the
 *            window underneath is how you act on something you cannot see.
 *   click  — anything OFF the panel closes it, whichever button it was. This is
 *            the click-off-to-close every menu on every desktop has, and the
 *            panels went without it for far too long.
 *   scroll — scrolls a list that scrolls, and otherwise moves the selection.
 *            The same split Up/Down already have in that panel. It is given the
 *            cursor position because a wheel acts on what is under the pointer,
 *            not on whatever the keyboard happens to be focused on — which is
 *            the whole difference between the two devices, and matters on the
 *            two-column panels where they can be looking at different lists.
 *            Only the vertical wheel gets here: every one of these panels is a
 *            column, so a horizontal one has nothing to mean in it — input.c
 *            swallows that rather than letting it scroll the window underneath.
 *
 * A panel with a genuinely destructive row (the task manager's kill) must NOT
 * put it on a click. The mouse gets the safe actions; the key that already
 * spells out the dangerous one keeps it.
 */

/* ── Enums ───────────────────────────────────────────────── */
typedef enum {
    LAYOUT_TILING = 0,
    LAYOUT_FLOATING,
    LAYOUT_MONOCLE,
    LAYOUT_AI,
    /* niri-style scrollable tiling: one endless horizontal strip of columns
     * per (desktop, monitor), scrolled so the focused column is on screen.
     * Appended rather than slotted next to LAYOUT_TILING on purpose — the
     * ordinals are the Super+Tab cycle order, and inserting one would silently
     * renumber the three below it. */
    LAYOUT_NIRI,
    /* Fibonacci spiral tiling. The first window takes the left half, the next
     * the top of what is left, the next the right of what is left after that —
     * winding inward clockwise, so no window is ever the letterbox strip a
     * master-stack column becomes once five of them are open. It buys that
     * shape with area: its smallest window is smaller, not bigger.
     * Appended for the same reason LAYOUT_NIRI was: the ordinals ARE the
     * Super+Tab cycle order and layouts.state is written against them. */
    LAYOUT_SPIRAL,
    /* Cascade: overlapping windows offset down-and-right so every titlebar
     * stays reachable, and the pile SPLITS INTO SEVERAL once one would run off
     * the screen — a few hands of cards dealt side by side rather than one
     * fifty-two-card slide off the desk.
     *
     * The only layout here whose windows overlap on purpose. That is what it is
     * for: a master-stack column of eight windows is eight letterbox strips,
     * and a grid of eight is eight postage stamps, but eight cascaded windows
     * are eight windows you can actually read one at a time, with the other
     * seven one click away.
     *
     * Appended for the same reason NIRI and SPIRAL were: the ordinals ARE the
     * Super+Tab cycle order and layouts.state is written against them. */
    LAYOUT_CASCADE,
} syn_layout_t;

/* How many layouts Super+Tab walks. NOT an enumerator: adding one to
 * syn_layout_t would make every switch over it (layout_label, layout_key,
 * ipc.c's layout_name, layout_apply's dispatch) grow a case for a value that
 * is not a layout. */
#define SYN_LAYOUT_COUNT  (LAYOUT_CASCADE + 1)

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
    /* Pointer geometry, written by synui_render_cmdbar(). Rect only — see
     * there. */
    syn_hit_t hit;
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

    /* What synapd DETECTED about the loaded model (synapd >= 0.1.0-25).
     * Empty against an older daemon — every consumer must test before use and
     * draw a dash rather than invent a value. The AI model panel is the reason
     * these exist: a filename cannot tell you whether the model's turn format
     * was recognised or which sampling profile won, and those are the two
     * things that fail silently. */
    char          model_name[128];  /* general.name out of the GGUF */
    char          model_file[128];  /* the GGUF's FILENAME — unrelated to the above */
    char          format[40];       /* "[INST]", "<|im_start|>user", "legacy" */
    char          profile[64];      /* matched sampling profile, or "none" */
    float         temperature;
    float         top_p;
    int           top_k;

    /* The last switch that FAILED (synapd >= 0.1.0-29), and llama.cpp's own
     * reason for it. A failed switch restores the previous model, so every
     * field above returns to exactly what it was — these two are the only
     * evidence that anything happened at all. Empty when the last switch
     * worked, and against an older daemon. */
    char          switch_file[128];
    char          switch_err[192];
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
#define WPPICK_WE_MAX   256   /* Wallpaper Engine wallpapers the scan will list */
#define WPPICK_ROWS      10   /* rows visible at once; the rest scroll */
/* Where synapse-wallpapers installs OURS — the second root the picker scans and
 * the second one synui-wpengine's wp_dir() resolves an id against. Both honour
 * $SYNUI_WPENGINE_SYSROOT over it. Keep the two in step: the picker hands the
 * engine a bare id, so a root only one of them knows about is a row that cannot
 * be applied, or a wallpaper that cannot be chosen. */
#define WPPICK_WE_SYSROOT "/usr/share/synapse/wallpapers/431960"

/* ── Cursor theme picker (cursor.c) ──────────────────────── */
#define CURPICK_MAX  64   /* installed cursor themes the scan will list */
#define CURPICK_ROWS 10   /* rows visible at once; the rest scroll */

/* Font families the picker will list. Generous next to CURPICK_MAX because
 * fonts are not installed one at a time: a single noto-fonts package brings
 * hundreds of families, and the dev box carries 521 before any of them are
 * filtered for Latin coverage. Overflowing this truncates the list rather than
 * breaking anything, but the truncation is alphabetical and therefore silent,
 * so it is set well above what a normal install reaches. */
#define FONTPICK_MAX  512
#define FONTPICK_ROWS 12   /* rows visible at once; the rest scroll */

/* One family name, as fontconfig spells it. A struct rather than a bare char
 * array so a style/coverage field can be added later without touching every
 * user of the list. */
struct syn_font_family {
    char name[96];
    /* fontconfig spacing >= FC_MONO. The terminals are given whatever family is
     * picked (synui-apply-font no longer gates on this), so the picker has to
     * be the thing that says a proportional face will not render kitty's
     * columns and box-drawing properly — otherwise the only feedback is the
     * terminal looking wrong afterwards. */
    int  mono;
};

/* ── The image cropper (crop.c) ──────────────────────────────
 *
 * The one panel that takes an argument: it operates on a file rather than
 * configuring something. The selection is held in IMAGE PIXELS — see crop.c
 * for why storing it in screen coordinates loses precision and breaks on a
 * different-sized output.
 *
 * It has TWO faces, `picking` says which: the recent-images list it opens on
 * when it was given no file, and the cropper proper once one is chosen. */

/* How many recent images the picker will hold, and how many rows it draws.
 * The cap is on the NEWEST that many, not the first that many found — see
 * crop_recent_add(). */
#define CROP_RECENT_MAX  80
#define CROP_RECENT_ROWS 10

typedef struct {
    char   path[256];
    time_t mtime;                  /* what the list is sorted by, newest first */
} syn_crop_recent_t;

typedef struct {
    int visible;

    cairo_surface_t *img;          /* decoded source; freed on hide */
    int              img_w, img_h;
    char             path[512];    /* what was opened */

    /* The source, already scaled to the size it is drawn at.
     *
     * NOT an optimisation — the panel is unusable without it. Every pointer
     * motion during a drag redraws this panel, and redrawing it used to mean
     * resampling the whole source image: measured at 122 ms per frame for a
     * 6000x4000 photo fitted to 1080p, against 0.29 ms to blit a copy that is
     * already the right size. At even 60 motion events a second that asks for
     * seven seconds of CPU per second of dragging on the thread that also runs
     * the Wayland event loop, so the drag did not lag — the whole desktop
     * stopped until the pointer did.
     *
     * Keyed on the scale it was built at, so a move to a differently-sized
     * output rebuilds it rather than drawing the old fit. NULL when the image
     * is drawn at 1:1 (crop_fit never scales up), where the source is already
     * the right thing to paint and a second copy would be pure memory. */
    cairo_surface_t *scaled;
    double           scaled_at;    /* the crop_fit() scale `scaled` was built for */

    /* The selection's two corners, in image pixels and NOT normalised — a drag
     * up and to the left leaves bx < ax. crop_selection() sorts them.
     *
     * INVARIANT: `b` is the ACTIVE corner and `a` is the one diagonally
     * opposite. Everything that adjusts the selection — the drag, the arrows, a
     * grabbed handle — moves `b`, so there is one piece of code that knows how
     * to move a corner and three callers of it. Changing WHICH corner is active
     * is then not a mode the movers consult but a rewrite of a/b that leaves the
     * rectangle on screen identical (crop_set_active). */
    int ax, ay, bx, by;
    int dragging;                  /* a press is in flight; input.c feeds it */

    /* Which corner `b` currently IS: 0 TL, 1 TR, 2 BL, 3 BR (bit 0 = right,
     * bit 1 = bottom). DERIVED from a/b by crop_active_sync() and never
     * authoritative — nudge the active corner past its opposite and the
     * rectangle flips, so the corner that was the bottom-right becomes the
     * bottom-left. Stored only so the render can highlight it without
     * recomputing the comparison. */
    int active;

    char status[64];               /* an error to show without closing */

    /* ── The recent-images list ──────────────────────────────
     * Populated by crop_recent_scan() when the panel is opened with no file.
     * `sel` is a list index, `scroll` the first drawn row. */
    int picking;
    int from_pick;                 /* opened FROM the list — Backspace goes back */
    syn_crop_recent_t recent[CROP_RECENT_MAX];
    int recent_count, recent_sel, recent_scroll;
    syn_hit_t hit;                 /* rows, while picking; blank otherwise */
} syn_crop_panel_t;

/* ── The equalizer panel (eq.c; the DSP is synui-eq(1)) ────── */

/* Bands the panel will hold. The REAL count comes from eq.state's `freqs=`
 * line, which synui-eq writes from its own table — this is only the ceiling on
 * the arrays. Raising the script's band count past this truncates the panel
 * rather than overrunning it. */
#define EQ_BANDS 16

typedef struct {
    int visible;
    int selected;
    int scroll;

    /* The optimistic cache. See eq.c's header for why it exists and why the
     * mtime is not cleared after this panel's own writes. */
    int  enabled;
    int  preamp;
    char preset[24];
    int  gain[EQ_BANDS];
    int  freq[EQ_BANDS];
    int  freq_count;
    long mtime;        /* eq.state's mtime when it was last read */

    syn_hit_t hit;
} syn_eq_panel_t;

/* ── The emoji picker (emoji.c + the generated emoji_data.c) ── */

#define EMOJI_COLS        12    /* cells across the grid */
#define EMOJI_ROWS         7    /* rows visible at once; the rest scroll */
#define EMOJI_RECENT_MAX  24    /* remembered in ~/.config/synui/emoji.recent */
#define EMOJI_SEARCH_MAX  32    /* typed search text */
/* Ceiling on one filtered view. Above the table's own size, so "All" with an
 * empty search is never truncated; it exists so the index array is a fixed
 * allocation in the server struct rather than a malloc on every keystroke. */
#define EMOJI_FILT_MAX  2048

/* Tab indices. Recents and All are not blocks, so they sit in front of the
 * generated category list rather than inside it. */
enum {
    EMOJI_CAT_RECENT = 0,
    EMOJI_CAT_ALL,
    EMOJI_CAT_FIRST_BLOCK,   /* …then one per entry in syn_emoji_cats[] */
};

/* One row of the generated table. `name` is the canonical Unicode name, already
 * lowercased so the search is a plain strstr. */
struct syn_emoji {
    const char *ch;
    const char *name;
    const char *cat;
};

extern const struct syn_emoji syn_emoji_table[];
extern const int              syn_emoji_count;
extern const char *const      syn_emoji_cats[];
extern const int              syn_emoji_cat_count;

/* The picker's live state. A named type rather than an anonymous struct inside
 * syn_server_t (as the other panels use) because emoji.c passes it around as
 * one thing — the key handler alone touches eight of these fields. */
typedef struct {
    int visible;
    int cat;         /* EMOJI_CAT_*; >= EMOJI_CAT_FIRST_BLOCK indexes the blocks */
    int selected;    /* index into the CURRENT view, not into the table */
    int scroll;      /* first visible ROW of the grid */

    char search[EMOJI_SEARCH_MAX];
    int  search_len;

    /* Indices into syn_emoji_table[] matching the search + category. Rebuilt on
     * every edit; see emoji_rebuild() for why it holds indices, not copies. */
    int filt[EMOJI_FILT_MAX];
    int filt_count;

    /* Most recently used, newest first. Stored as the CHARACTERS rather than
     * table indices so ~/.config/synui/emoji.recent stays readable and survives
     * the generated table being regenerated against a newer Unicode. */
    char recent[EMOJI_RECENT_MAX][16];
    int  recent_count;

    syn_hit_t hit;
} syn_emoji_panel_t;

/* ── The calculator (calc.c) ─────────────────────────────────
 *
 * A grid like the emoji picker, but the grid is the POINTER's interface only —
 * the keyboard types into the expression box and never walks the keys. See
 * calc.c's header. */

#define CALC_ENTRY_MAX    128   /* one typed expression */
#define CALC_RESULT_MAX    48   /* %.12g of a double, with room to spare */
#define CALC_HISTORY_MAX   32   /* kept in ~/.config/synui/calc.history */
#define CALC_TAPE_ROWS      5   /* transcript lines on screen; the rest scroll */
#define CALC_COLS           5   /* keypad, and calc_buttons[] is COLS * ROWS */
#define CALC_ROWS           6

/* One line of the tape: what was typed, and what it came to. The answer is
 * stored rather than recomputed — see calc.c, an expression containing `ans`
 * would re-derive against a different number every time it was loaded. */
typedef struct {
    char expr[CALC_ENTRY_MAX];
    char result[CALC_RESULT_MAX];
} syn_calc_entry_t;

/* A named type rather than an anonymous struct in syn_server_t, for the emoji
 * panel's reason: calc.c passes it around as one thing. */
typedef struct {
    int visible;

    char entry[CALC_ENTRY_MAX];
    int  entry_len;

    /* The last answer, both ways. `result` is what the panel draws, `ans` is
     * what the identifier of the same name resolves to; has_ans separates "no
     * answer yet" from an answer that happens to be zero. All three OUTLIVE a
     * close, so shutting the panel to go and look something up does not throw
     * away the figure you had. */
    char   result[CALC_RESULT_MAX];
    double ans;
    int    has_ans;

    /* The error line, or a note like "copied 42". Cleared by the next edit.
     * status_err separates the two so render.c can put a failed sum in amber
     * without also shouting about a successful copy. */
    char status[96];
    int  status_err;

    syn_calc_entry_t hist[CALC_HISTORY_MAX];   /* newest first */
    int hist_count;
    /* Where Up/Down have walked to in hist[], or -1 when the line is the
     * user's own. A shell's history cursor. */
    int recall;
    int scroll;    /* first tape line shown, counting from the newest */

    int hover;     /* keypad cell under the pointer, -1 for none */

    /* Dragged position + keyboard, in window mode. See syn_panel_win_t. */
    syn_panel_win_t win;

    syn_hit_t hit;
} syn_calc_panel_t;

/* One installed cursor theme. The name is a directory name, which means it
 * comes from whatever archive it was unpacked from — treat it as untrusted
 * text on both the render path (cairo) and any command line. */
struct syn_cursor_theme {
    char name[64];    /* directory name, e.g. "Adwaita" */
    char path[256];   /* where it was found, shown as the row's subtitle */
};

/* ── Display settings panel (dispcfg.c) ──────────────────── */
#define DISPCFG_MAX_OUTPUTS 8

typedef struct {
    int visible;
    int selected;   /* index into order[] */
    int count;
    syn_output_t *order[DISPCFG_MAX_OUTPUTS];  /* panel list, reading order
                                                 * (grid_y then grid_x) */
    char status[96];   /* last action / error, shown in the panel */
    /* Pointer geometry, written by synui_render_dispcfg(). */
    syn_hit_t hit;                          /* panel rect + the monitor rows */
    /* The mini-map box each monitor is drawn in, LAYOUT coords, parallel to
     * order[]. Not a row grid: the boxes sit at arrangement-grid positions,
     * which can leave holes, so each one is recorded as it is drawn. */
    struct wlr_box cell[DISPCFG_MAX_OUTPUTS];
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

/* ── Window-effect rows (uifx.c) ─────────────────────────────
 *
 * Page two of the same Super+E panel. The CRT rows above are one shader over the
 * whole screen; these are the per-window scenefx knobs — corners, drop shadow,
 * backdrop blur, translucency. They were synuirc-only until now, which made the
 * one class of setting you can only judge by eye the one class that cost a
 * restart to try. Same keys, same panel, second page.
 *
 * Kept a separate enum (rather than more FILTER_ROWs) because the two pages
 * scale differently: a CRT strength is always 0..1, while these are pixels,
 * pass counts and percentages, each with its own range and notch.
 */
typedef enum {
    UIFX_ROW_CORNER = 0,        /* corner_radius */
    UIFX_ROW_SHADOW,            /* shadow — a word, not a bar */
    UIFX_ROW_SHADOW_SIZE,       /* shadow_blur_sigma */
    UIFX_ROW_SHADOW_SPREAD,     /* shadow_spread */
    UIFX_ROW_SHADOW_DROP,       /* shadow_offset_y */
    UIFX_ROW_SHADOW_OPACITY,    /* shadow_color[3] */
    UIFX_ROW_BLUR,              /* blur — a word, not a bar */
    UIFX_ROW_BLUR_RADIUS,       /* blur_radius */
    UIFX_ROW_BLUR_PASSES,       /* blur_passes */
    UIFX_ROW_HALO,              /* glass_halo */
    /* These last two are NOT this page's to own: theme.c drives translucency
     * (one slider sets both levels and pushes foot's own alpha) and persists it
     * in theme.state. They are shown here because this is where you look for
     * them, but they are edited through transparency_set_*() and saved by that
     * code — see uifx_adjust and uifx_state_save. */
    UIFX_ROW_TRANSPARENCY,      /* transparency — a word, not a bar */
    UIFX_ROW_OPACITY,           /* active_opacity; inactive follows it */
    UIFX_ROW_COUNT,
} syn_uifx_row_t;

/* Which page Super+E is showing; Tab cycles. */
typedef enum {
    FILTER_PAGE_CRT = 0,
    FILTER_PAGE_UIFX,
    FILTER_PAGE_COUNT,
} syn_filter_page_t;

typedef struct {
    int  visible;
    int  page;         /* syn_filter_page_t */
    int  selected;     /* syn_filter_row_t — the CRT page's cursor */
    int  dirty;        /* edited since the last save — drives the panel hint */
    /* The window-effects page keeps its own cursor and dirty flag: the pages
     * have different lengths and save to different files, so sharing either
     * would put the cursor out of range on a switch and make `s` ambiguous. */
    int  uifx_selected;
    int  uifx_dirty;
    char status[96];
    /* Pointer geometry, written by this panel's synui_render_*. */
    syn_hit_t hit;
} syn_filters_t;

/* ── AI model picker (aimodel.c) ─────────────────────────── */
/*
 * Pick which GGUF synapd runs, and show what it worked out about it.
 *
 * The three facts above the list are the point of the panel. A filename tells
 * you nothing about whether the model's turn format was recognised or which
 * sampling profile matched, and both fail SILENTLY — a wrongly-framed prompt
 * still comes back fluent, which is how synapd spent its whole life speaking
 * Zephyr to a Mistral without one line of evidence. They are read from
 * SYN_MSG_STATUS rather than worked out here, so the panel reports what the
 * daemon actually did rather than what synui would have predicted.
 *
 * Switching is SYN_MSG_RELOAD, which synapd confines to its own models
 * directory; this panel never sends a path.
 *
 * The panel also lists models that are NOT here yet. Those come from a live
 * Hugging Face query on a background thread — the news.c idiom, for the same
 * reason: a DNS lookup or a TLS handshake on the event-loop thread would stall
 * every client's frame callbacks, so the compositor never blocks on the
 * network. Downloading is not synui's job either; it queues a request and
 * starts a systemd unit that runs as root (see syn-model fetch).
 */
#define AIMODEL_MAX  32   /* models listed; a plausible library, not a limit */

typedef struct {
    char name[128];       /* bare filename — what RELOAD wants */
    long long bytes;

    /* What the file says about itself, read on demand and kept.
     *
     * Lazy because it is the only thing in the panel that touches the disk on
     * the compositor thread: reading every model's header at scan time would
     * put a directory's worth of them in front of the panel opening, for facts
     * about models the cursor may never stop on. `probed` covers the failures
     * too, so an unreadable file is read once and not retried on every frame. */
    int        probed;
    syn_gguf_t info;
} syn_aimodel_entry_t;

/* ── The download catalogue ──────────────────────────────── */

#define AIMODEL_ROWS       14   /* list slots visible at once; the rest scroll */
#define AIMODEL_CAT_MAX    32   /* repos held from one search */
#define AIMODEL_FILE_MAX   16   /* GGUF files shown for one repo */
#define AIMODEL_QUERY_MAX  64   /* typed search text */
/* Tags kept per repo. Hugging Face lists a dozen or so on a busy model, most
 * of which gguf_tag_english() drops; this only has to be deep enough that the
 * few describing a SKILL are not cut off by the noise ahead of them. */
#define AIMODEL_TAG_MAX    24
/* Sized by the LONGEST tag that carries meaning, not by the typical one. Skill
 * tags are short ("coding"), but the base model arrives as
 * "base_model:quantized:owner/Some-Model-30B-Instruct-GGUF" — 60-odd
 * characters, and an entry that does not fit is dropped. At 40 this silently
 * discarded every base_model tag, i.e. exactly the ones being read, and the
 * pane simply showed no "Based on" line. Caught by the catalogue test. */
#define AIMODEL_TAG_LEN    96

typedef struct {
    char file[128];        /* path inside the repo, and the local filename */
    char quant[16];        /* Q4_K_M etc., read out of the filename */
    long long bytes;       /* -1 until the file listing lands */
} syn_aimodel_file_t;

/* What the detail pane knows about one repo. */
typedef enum {
    AIMODEL_DETAIL_NONE = 0,   /* not asked for yet */
    AIMODEL_DETAIL_WANT,       /* the cursor settled here; the thread is next */
    AIMODEL_DETAIL_BUSY,       /* being fetched */
    AIMODEL_DETAIL_OK,
    AIMODEL_DETAIL_FAIL,
} syn_aimodel_detail_t;

typedef struct {
    char id[128];          /* "microsoft/Phi-3-mini-4k-instruct-gguf" */
    char author[64];       /* the half before the slash */
    char name[96];         /* the half after it */
    char license[32];      /* from the license: tag, "" when the repo has none */
    char params[16];       /* "7B" — read out of the name, "" when unreadable */
    long long downloads;
    long long likes;

    /* What the repo says it IS, so the AVAILABLE side can describe a model
     * BEFORE it is several GB on the disk — which is the only time the
     * description can still change your mind. The installed side reads this
     * out of the GGUF header; there is no header to read until it is
     * downloaded, so the same words are assembled from the repo's tags
     * instead. Same vocabulary either way (gguf_tag_english), so a model does
     * not change its description by being downloaded.
     *
     * `base_model` is the "base_model:owner/Name" tag with the qualified
     * "base_model:quantized:..." form preferred away — that one names the repo
     * this was quantised FROM, which is usually itself. */
    char tags[AIMODEL_TAG_MAX][AIMODEL_TAG_LEN];
    int  n_tags;
    char base_model[96];

    syn_aimodel_file_t   files[AIMODEL_FILE_MAX];
    int                  n_files;
    int                  sel_file;
    syn_aimodel_detail_t detail;
} syn_aimodel_cat_t;

/* A download in flight, as the progress file describes it. */
typedef enum {
    AIMODEL_DL_IDLE = 0,
    AIMODEL_DL_STARTING,   /* the unit has been asked for, nothing reported yet */
    AIMODEL_DL_RUNNING,
    AIMODEL_DL_DONE,
    AIMODEL_DL_FAILED,
} syn_aimodel_dlstate_t;

typedef struct {
    syn_aimodel_dlstate_t state;
    char token[72];        /* the request/progress file stem, and the unit
                            * instance — sanitised at the point it is made */
    char file[128];        /* destination filename */
    char msg[128];         /* whatever the privileged half last reported */
    long long got, total;
    int  pct;
    /* When the unit was asked for. `systemctl start` is fire-and-forget, so a
     * refusal (no polkit rule installed) is silent — this is what turns that
     * into a failure the panel can report instead of "starting…" forever. */
    double started_at;
} syn_aimodel_dl_t;

typedef struct {
    int  visible;
    int  selected;
    int  count;
    syn_aimodel_entry_t models[AIMODEL_MAX];

    /* The last scan could not open the directory, as opposed to opening it and
     * finding nothing. Both leave count == 0, and the pane used to say "No
     * models installed" to either — which is a confident false statement when
     * the truth is that synui was not allowed to look. Worth a whole field
     * because that lie cost a long hunt once already: the models were there,
     * the daemon had one loaded, and the panel said the disk was empty. */
    int  scan_err;

    /* Which entry is loaded right now, matched by filename against synapd's
     * reply, or -1 when nothing matches (no model, or one outside the
     * directory because it was set by an ExecStart flag). */
    int  loaded_idx;

    /* Set when a switch has been asked for and the daemon has not finished.
     * The list stays visible but refuses a second pick — synapd rejects one
     * anyway, and letting the key through would only produce an error toast. */
    int  switching;

    /* What that switch asked for, and when it was asked.
     *
     * A failed load makes synapd restore the previous model, so the daemon
     * ends up reporting exactly what it reported before the request — there is
     * no state to compare against without remembering the question. Keeping
     * the FILENAME rather than the row index matters because the cursor moves
     * freely while several GB load. `switch_seen_loading` records that synapd
     * was observed mid-load, which separates "it failed and restored" from
     * "it has not started yet". */
    char   switch_file[128];
    double switch_at;
    int    switch_seen_loading;

    char status[160];
    syn_hit_t hit;          /* panel rect + the model/catalogue rows */
    syn_hit_t hit_files;    /* same rect; rows = the detail pane's file list */

    /* ── The AVAILABLE section ───────────────────────────── */

    /* The cursor is one column over two sections: -1 means it is in INSTALLED
     * at `selected`, and >= 0 means it is in AVAILABLE at cat[cat_sel].
     * `selected` keeps its meaning either way, because the control-panel row
     * reads it and must not follow the cursor into a model that is not here. */
    int  cat_sel;
    int  n_cat;
    syn_aimodel_cat_t cat[AIMODEL_CAT_MAX];

    /* The list column scrolls: three installed models and a full page of
     * search results do not fit a panel sized to the screen. Counted in list
     * SLOTS, section headings included, so it matches the hit grid exactly. */
    int  scroll;

    char query[AIMODEL_QUERY_MAX]; /* the search text, "" = the default listing */
    int  typing;                   /* the search box has the keyboard */
    char search_msg[96];           /* "searching…", "offline", "no matches" */

    /* The detail fetch is debounced: arrowing down the list must not fire a
     * request per keypress. Zero when nothing is pending. */
    double detail_at;

    syn_aimodel_dl_t dl;
    struct wl_event_source *dl_timer;   /* polls the progress file */

    /* ── Deleting an installed model ─────────────────────────
     *
     * Two steps on purpose. `del_armed` is the index the cursor was on when
     * Delete was pressed; the key does nothing but arm, and only a second
     * confirming key removes anything. An index rather than a flag because the
     * cursor can move between the two presses, and a confirmation that follows
     * the cursor would delete a model nobody pointed at.
     *
     * `del_token`/`del_file` are the request in flight. synui cannot unlink in
     * synapd's models directory (0750 synapd:synapse — readable, not writable),
     * so the work is done by syn-model-delete@TOKEN.service and this side only
     * watches the directory for the file to go. `del_until` is the deadline
     * that turns a silent polkit refusal into a reported failure rather than
     * "deleting …" forever. */
    int    del_armed;
    char   del_token[72];
    char   del_file[128];
    double del_until;

    /* ── The fetch thread (news.c idiom) ─────────────────── */

    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  cv;
    _Atomic int     stop;
    _Atomic int     want;
    int             running;
    int             pipe[2];
    struct wl_event_source *src;

    /* Handed to the thread under the lock. The flags say what was asked for,
     * rather than the contents of the buffers saying it: a query string that
     * still holds the last search is not a request to run it again, and reading
     * it as one made every cursor move re-fetch the whole listing. */
    int  req_search;                   /* run a search for req_query */
    char req_query[AIMODEL_QUERY_MAX];
    char req_detail[128];              /* non-empty = fetch this repo's files */
    int  searching;                    /* a search is out; do not queue another */

    /* Handed back under the lock; the pipe is only the wake-up. */
    int                have_search;    /* fetched[] is fresh, even if empty */
    syn_aimodel_cat_t  fetched[AIMODEL_CAT_MAX];
    int                n_fetched;
    int                search_rc;      /* 0 ok, -1 the network said no */
    char               det_id[128];
    syn_aimodel_file_t det_files[AIMODEL_FILE_MAX];
    int                n_det;
    int                det_rc;
} syn_aimodel_t;

/* ── Desktop widget manager (widgets.c) ──────────────────── */
/*
 * The quickshell desktop widgets, one row each, plus a master row that is
 * the old group toggle. They were all-or-nothing from the desktop until now:
 * `synui-widgets <name> on` could always address one, but Super+Shift+A and the
 * control panel row both ran the group form, so the only way to have the clock
 * without the visualiser was the command line.
 *
 * Order is display order AND the order of synui-widgets' own $WIDGETS list, so
 * widget_name() below is the whole binding between the two.
 */
typedef enum {
    WIDGET_ROW_ALL = 0,        /* master: everything on/off, the old bind */
    WIDGET_ROW_VISUALIZER,
    WIDGET_ROW_SYSMON,
    WIDGET_ROW_CLOCK,
    WIDGET_ROW_LAUNCHER,
    WIDGET_ROW_POSTIT,
    WIDGET_ROW_PIZZA,
    WIDGET_ROW_COUNT,
} syn_widget_row_t;

typedef struct {
    int  visible;
    int  selected;                 /* syn_widget_row_t */
    /* The panel's own model of widgets.state, read on open. Indexed by
     * syn_widget_row_t, so slot WIDGET_ROW_ALL is unused. It is updated the
     * instant a key is pressed and the helper is spawned to persist: re-reading
     * the file after the spawn would race it and show the previous value under
     * the cursor, which is exactly the bug the control panel's AI-backend row
     * had to grow a poll for. */
    int  on[WIDGET_ROW_COUNT];
    /* st_mtime of the widgets.state the copy above came from. Re-read only when
     * that moves, which is what lets the panel notice a `synui-widgets` run in a
     * terminal WITHOUT undoing its own optimistic change: this panel's own write
     * goes through a spawned child, so an unconditional re-read on the next
     * keypress could land before the child had written and show the previous
     * value under the cursor. An unchanged mtime means the child has not landed
     * yet and the optimistic value is the better answer. */
    long mtime;
    /* cava is an optdepend and the visualiser is dark without it — the panel
     * says so rather than letting a toggle look broken. Probed on open. */
    int  have_cava;
    char status[96];
    /* Pointer geometry, written by this panel's synui_render_*. */
    syn_hit_t hit;
} syn_widgets_t;

/* ── Event sounds (sound.c) ──────────────────────────────── */
/*
 * Order is display order and, via sound_event_name(), the key in sounds.state
 * and the argument to `synui-sound play`. Everything is OFF by default: the
 * desktop is silent until someone asks for a noise.
 */
typedef enum {
    SOUND_EVT_LOGIN = 0,
    SOUND_EVT_LOGOUT,
    SOUND_EVT_DEVICE_ADDED,     /* USB (and any udev device) plugged in */
    SOUND_EVT_DEVICE_REMOVED,
    SOUND_EVT_LOCK,
    SOUND_EVT_UNLOCK,
    SOUND_EVT_NOTIFY,
    SOUND_EVT_SCREENSHOT,
    SOUND_EVT_VOLUME,
    SOUND_EVT_ERROR,
    SOUND_EVT_COUNT,
} syn_sound_event_t;

/* Two fixed rows above the per-event ones: the master switch and the volume
 * slider. SOUND_ROW_EVENT is where the event rows start, so
 * row - SOUND_ROW_EVENT is the syn_sound_event_t. */
enum {
    SOUND_ROW_ENABLED = 0,
    SOUND_ROW_VOLUME,
    SOUND_ROW_THEME,
    SOUND_ROW_EVENT,
    SOUND_ROW_COUNT = SOUND_ROW_EVENT + SOUND_EVT_COUNT,
};

#define SOUND_THEME_MAX  64
#define SOUND_SAMPLE_MAX 64

typedef struct {
    int  visible;
    int  selected;

    /* The compositor's copy of sounds.state. Re-read whenever the file's mtime
     * moves (sound_state_refresh), so a `synui-sound` run from a terminal takes
     * effect with no reload — and so this stays a CACHE that only ever skips a
     * fork, never the authority on whether a sound plays. The helper re-checks. */
    int   enabled;                    /* master switch */
    int   volume;                     /* 0..100; 0 is mute */
    int   on[SOUND_EVT_COUNT];
    char  theme[SOUND_THEME_MAX];
    /* The sample picked for each event, from the "<event>_sound" keys. Empty
     * means none was picked and the automatic chain in sound_event_ids() is
     * used — which is not the same as a sample literally named "default". */
    char  sample[SOUND_EVT_COUNT][SOUND_SAMPLE_MAX];
    long  mtime;                      /* st_mtime of the file this came from */
    int   loaded;

    char status[96];
    /* Pointer geometry, written by this panel's synui_render_*. */
    syn_hit_t hit;
} syn_sound_t;

/* ── Clock & Time / Calendar (clock.c) ───────────────────── */
#define CLOCK_ZONES_MAX    6
#define CLOCK_SETTING_ROWS 4   /* format, seconds, date, NTP — clock_row_label() */

/* The date layout is stored as its ID STRING, not as an index.
 *
 * synui-clock is the authority on which layouts exist — it is what actually
 * renders the bar and the desktop widget, and `synui-clock --layouts` is what
 * syn-settings asks. This panel keeps its own list only so it can cycle, and an
 * id it has never heard of is DISPLAYED AND PRESERVED rather than reset: an
 * index would silently become a different layout the moment the lists differ
 * in length, which is the failure mode a shared enum invites across two
 * languages and two packages. */
#define CLOCK_DATE_ID_MAX 24

typedef struct {
    int  fmt24;        /* 0 = 12-hour, 1 = 24-hour (persisted to clock.state) */
    int  seconds;      /* show seconds in the bar clock */
    char date[CLOCK_DATE_ID_MAX];  /* date layout id: iso, dmy, mdy, … */
    char zones[CLOCK_ZONES_MAX][64];  /* world-clock IANA zone names */
    int  nzones;
    char tz[128];      /* system zone, read from /etc/localtime */
    int  ntp;          /* timedatectl NTP sync on */
    char status[96];   /* last action / error, shown on the panel */
    int  visible;
    int  selected;     /* 0..CLOCK_SETTING_ROWS-1 */
    struct wl_event_source *timer;    /* 1 Hz repaint while the panel is open */
    /* Pointer geometry, written by this panel's synui_render_*. */
    syn_hit_t hit;
} syn_clock_t;

typedef struct {
    int visible;
    int year;
    int mon;   /* 0-11 */
    int sel;   /* selected day, 1-based */
    /* Pointer geometry, written by synui_render_calendar(). The row band spans
     * all seven day columns; clock.c splits it back up. */
    syn_hit_t hit;
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
    /* Pointer geometry, written by this panel's synui_render_*. */
    syn_hit_t hit;
} syn_thememgr_t;

/* ── Control panel (ctlpanel.c) ──────────────────────────── */
/*
 * One settings front door, shaped the way every other desktop shapes one: a
 * category list down the left, that category's rows on the right, and the
 * panels that own the details opening *from* it and returning *to* it.
 *
 * It was a flat list until it wasn't: twenty-one rows in one column, toggles
 * and jump-offs interleaved with a separator doing the work a heading should,
 * and every jump-off a one-way door — open Displays from here and Esc dropped
 * you on the desktop, not back where you were. Categories make the list
 * findable; syn_ctlpanel_t::child makes the doors swing both ways.
 *
 * The *shortcuts* category deliberately has no table here: it is generated from
 * the live bind table (syn_config_t::binds) every time the panel renders, so it
 * cannot drift out of step with the binds actually in force — the failure the
 * waybar start menu shipped once, where a stale hand-maintained list mapped
 * entries to the wrong actions.
 */
typedef enum {
    CTL_CAT_APPEARANCE = 0,
    CTL_CAT_WINDOWS,       /* borders, titlebars, shadows, blur, snapping */
    CTL_CAT_DESKTOP,       /* the shell furniture: dock, start button, widgets */
    CTL_CAT_INPUT,         /* keyboard + pointer */
    CTL_CAT_DISPLAY,
    CTL_CAT_SOUND,
    CTL_CAT_NETWORK,
    CTL_CAT_POWER,
    CTL_CAT_SYSTEM,
    CTL_CAT_SHORTCUTS,     /* not settings — the live bind table, read-only */
    CTL_CAT_COUNT,
} syn_ctl_cat_t;

/* Rows are grouped by category here purely so the enum reads like the panel;
 * the category each one belongs to is declared in ctlpanel.c's item table, and
 * that table is what the panel walks. Order within a category is display order. */
typedef enum {
    /* Appearance */
    CTL_ROW_THEME = 0,
    CTL_ROW_WALLPAPER,
    CTL_ROW_CURSOR,        /* cursor theme picker (curpick.c) */
    CTL_ROW_UI_FONT,       /* UI font picker (fontpick.c) */
    CTL_ROW_EFFECTS,       /* CRT post-process master switch */
    CTL_ROW_FILTERS,       /* …and the per-filter strengths behind it */
    CTL_ROW_TRANSPARENCY,  /* window translucency master switch + level */
    CTL_ROW_TITLEBARS,
    /* Desktop */
    CTL_ROW_LAYOUT,        /* tiling / floating / monocle / AI / niri — of the ACTIVE desktop */
    CTL_ROW_DOCK,
    CTL_ROW_DOCK_AUTOHIDE, /* dock slides away when unhovered, or stays put */
    CTL_ROW_LAUNCHER,      /* start-button style: text ◢ SYNAPSE, or ◢ + emblem */
    CTL_ROW_BAR_SHELL,     /* which QML tree synui-bar starts: SYNAPSE or Antiquity */
    CTL_ROW_WIDGETS,       /* desktop widgets: visualiser, sysmon, clock, launcher, post-it, pizza */
    /* Display */
    CTL_ROW_DISPLAYS,
    CTL_ROW_NIGHTLIGHT,
    CTL_ROW_CLOCK,         /* date & time */
    /* Sound */
    CTL_ROW_DND,           /* Do Not Disturb: no toast, no chime */
    CTL_ROW_SOUNDS,        /* event sounds: login, device plugged in, … */
    CTL_ROW_EQUALIZER,     /* 10-band system equalizer (eq.c) */
    CTL_ROW_RECORD_AUDIO,  /* Super+Shift+R captures desktop sound too */
    CTL_ROW_RECORD_EDIT,   /* Super+Shift+R records an editable mezzanine */
    /* Network */
    CTL_ROW_NETWORK,
    CTL_ROW_BLUETOOTH,
    CTL_ROW_PRINTERS,
    /* Power */
    CTL_ROW_POWER,
    CTL_ROW_SAVER,
    CTL_ROW_GAME,
    CTL_ROW_LOCK,
    CTL_ROW_LOCK_FPRINT,
    /* System */
    CTL_ROW_TASKMGR,
    CTL_ROW_AI_BACKEND,
    CTL_ROW_AI_MODEL,
    CTL_ROW_NEWS,
    CTL_ROW_CLIPBOARD,

    /* ── The config-backed rows ──────────────────────────────
     *
     * Everything above predates the item table carrying a setting's VALUE: each
     * one is either a jump-off to the panel that owns it, or a toggle with a
     * hand-written case in ctlpanel_activate(). That was fine for thirty rows
     * and does not go to a hundred — synuirc has ~106 keys and the panel reached
     * about a quarter of them, so most of what synui can do was configurable
     * only by editing a file and logging out.
     *
     * These rows name a synuirc key, an offset into syn_config_t and a range,
     * and the panel reads, writes, clamps, persists and resets them generically.
     * Adding a setting is a line in ctl_items[] and nothing else — no case in a
     * switch, no persistence code, no render change.
     */
    CTL_ROW_BORDER_WIDTH,
    CTL_ROW_CORNER_RADIUS,
    CTL_ROW_GAP,
    CTL_ROW_TITLEBAR_HEIGHT,
    CTL_ROW_ANIMATION_MS,      /* the window half; anim_window_ms            */
    CTL_ROW_ANIM_WINDOW,
    CTL_ROW_ANIM_RISE_PX,
    CTL_ROW_ANIM_WORKSPACE,
    CTL_ROW_ANIM_WORKSPACE_MS,
    CTL_ROW_ANIM_CURVE,
    CTL_ROW_MASTER_FACTOR,
    CTL_ROW_CASCADE_STACK, /* windows per pile in LAYOUT_CASCADE */
    CTL_ROW_FOCUS_MODE,
    CTL_ROW_FOCUS_DELAY,
    CTL_ROW_PANEL_FOLLOW,  /* do panels chase the pointer between monitors */
    CTL_ROW_SNAP,
    CTL_ROW_SNAP_ZONE,
    CTL_ROW_REMEMBER_GEOMETRY,
    CTL_ROW_CLIP_CSD_MARGIN,
    CTL_ROW_GLASS_HALO,
    CTL_ROW_FOOT_ALPHA,
    CTL_ROW_INACTIVE_OPACITY,

    CTL_ROW_SHADOW,
    CTL_ROW_SHADOW_SIGMA,
    CTL_ROW_SHADOW_SPREAD,
    CTL_ROW_SHADOW_OFFSET_X,
    CTL_ROW_SHADOW_OFFSET_Y,

    CTL_ROW_BLUR,
    CTL_ROW_BLUR_PASSES,
    CTL_ROW_BLUR_RADIUS,
    CTL_ROW_BLUR_NOISE,
    CTL_ROW_BLUR_BRIGHTNESS,
    CTL_ROW_BLUR_CONTRAST,
    CTL_ROW_BLUR_SATURATION,

    CTL_ROW_ALT_TAB_STYLE,
    CTL_ROW_ALT_TAB_PREVIEW,
    CTL_ROW_ALT_TAB_ALL_DESKTOPS,
    CTL_ROW_ALT_TAB_MINIMIZED,

    CTL_ROW_REPEAT_RATE,
    CTL_ROW_REPEAT_DELAY,
    CTL_ROW_NUMLOCK,
    CTL_ROW_TAP_TO_CLICK,
    CTL_ROW_NATURAL_SCROLL,
    CTL_ROW_LEFT_HANDED,
    CTL_ROW_ACCEL_SPEED,
    CTL_ROW_CURSOR_SIZE,

    CTL_ROW_NIGHTLIGHT_TEMP,
    CTL_ROW_DOCK_HEIGHT,
    CTL_ROW_DOCK_EDGE,
    CTL_ROW_DOCK_HOVER_MARGIN,
    CTL_ROW_DESKTOP_ICONS,
    CTL_ROW_DESKTOP_ICON_ARRANGE,
    CTL_ROW_CAT_START,
    CTL_ROW_CAT_BREED,
    CTL_ROW_WELCOME_AT_STARTUP,
    CTL_ROW_START_OVERLAY,

    CTL_ROW_EFFECT_SCANLINE,
    CTL_ROW_EFFECT_CURVATURE,
    CTL_ROW_EFFECT_ABERRATION,
    CTL_ROW_EFFECT_GLITCH,
    CTL_ROW_EFFECT_PHOSPHOR,
    CTL_ROW_EFFECT_MONO,
    CTL_ROW_EFFECT_BLOOM,

    CTL_ROW_GAME_MODE,
    CTL_ROW_GAME_OUTPUT,
    CTL_ROW_GAME_SUSPEND_AI,
    CTL_ROW_GAME_INHIBIT_IDLE,
    /* The rest of what game mode borrows. Ordered by what each is worth, which
     * is also the order the panel shows them in. */
    CTL_ROW_GAME_DROP_EFFECTS,
    CTL_ROW_GAME_PAUSE_WALLPAPER,
    CTL_ROW_GAME_STOP_BAR,
    CTL_ROW_GAME_QUIET_KMOD,

    CTL_ROW_AI_LAYOUT,
    CTL_ROW_AI_CTX_DECOR,
    CTL_ROW_NEWS_REFRESH,
    CTL_ROW_ABOUT,         /* System ▸ About OS — fetch, in a terminal */
    CTL_ROW_BAR_EDGE,      /* which screen edge synui-bar puts the bar on */
    CTL_ROW_BAR_SHAPE,     /* full-width / rounded-ends / floating-pill, when rounded */
    CTL_ROW_KEYBINDS,      /* the shortcut palette, which is the rebind editor */
    CTL_ROW_OVERVIEW,      /* mission control (overview.c) */
    CTL_ROW_BAR,           /* Desktop ▸ Bar — is there one at all */
    CTL_ROW_CALC_CLOSE,    /* Windows ▸ Panels — one row per panel */
    CTL_ROW_CTLPANEL_CLOSE,
    CTL_ROW_TASKMGR_CLOSE,

    CTL_ROW_COUNT,
} syn_ctl_row_t;

/* What KIND of value a row carries, for the rows that carry one directly.
 *
 * CTL_VAL_NONE covers everything that predates this: jump-offs with no value,
 * and the handful of toggles whose state lives somewhere other than a plain
 * syn_config_t field (game mode is the compositor's, the AI backend is a file
 * synapd writes, the layout is per-desktop). Those keep their bespoke cases.
 * Everything else is read and written through the offset. */
typedef enum {
    CTL_VAL_NONE = 0,
    CTL_VAL_BOOL,    /* int, shown on/off                                  */
    /* Same setting, one byte wide. Two fields in syn_config_t are `bool`
     * rather than `int` (remember_geometry, desktop_icons), and reading one
     * through an int* would pick up three neighbouring bytes and report a
     * nonsense value — silently, and only for those two rows. Naming the width
     * is cheaper than converting the fields and auditing every reader. */
    CTL_VAL_BOOL8,
    CTL_VAL_INT,     /* int in [min,max], stepped by `step`                */
    CTL_VAL_FLOAT,   /* float in [min,max]                                 */
    CTL_VAL_ENUM,    /* int index into `names`                             */
    CTL_VAL_TRI,     /* int -1/0/1: "device default" / off / on (libinput) */
} syn_ctl_val_t;

/* What has to happen after a value changes for the screen to agree with it.
 *
 * A setting is not applied by being stored: most of these are read once, when
 * a window is framed or the blur data is pushed or a device is configured. The
 * table names which of those to re-run, so a new row does not have to know how
 * the compositor is wired — and so that a row which needs a relayout cannot
 * silently ship without one, which is the failure that makes a settings panel
 * feel broken ("I changed it and nothing happened"). */
typedef enum {
    CTL_APPLY_NONE = 0,  /* read at point of use; the store IS the change */
    CTL_APPLY_REPAINT,   /* damage every output                          */
    CTL_APPLY_RELAYOUT,  /* re-tile every workspace, then repaint        */
    CTL_APPLY_DECO,      /* re-frame every view (borders, titlebars)     */
    CTL_APPLY_GLASS,     /* re-push opacity/radius/blur to every buffer  */
    CTL_APPLY_SHADOW,    /* rebuild every view's shadow node             */
    CTL_APPLY_BLURDATA,  /* wlr_scene_set_blur_data + repaint            */
    CTL_APPLY_INPUT,     /* re-apply libinput/xkb settings to every device */
    CTL_APPLY_DOCK,      /* dock_rebuild + dock_relayout                  */
    CTL_APPLY_NIGHTLIGHT,/* re-commit the gamma ramps                    */
    CTL_APPLY_CURSOR,    /* reload the cursor theme at the new size       */
    CTL_APPLY_DESKICONS, /* redraw the desktop icon grid                  */
} syn_ctl_apply_t;

/* What activating a row does. The distinction is not cosmetic: only CTL_KIND_PANEL
 * rows arm the return-to-the-control-panel path, because only they open something
 * this panel can be handed back from. A LAUNCH row hands off to a process synui
 * does not own, and an ACTION row (lock) means the panel should be gone. */
typedef enum {
    CTL_KIND_TOGGLE = 0,   /* flips in place; the panel stays up */
    /* A number or a named option, driven entirely by Left/Right and described
     * by the item table's vtype/range. Distinct from CTL_KIND_SLIDER, which is
     * the Transparency row's bespoke "master switch plus a level" — and from
     * CTL_KIND_CHOICE, whose options are not synui's to enumerate (the AI model
     * list is whatever GGUFs are on disk). Enter on one of these does nothing,
     * because there is nothing an Enter would mean that Left/Right did not
     * already say; the reset key is what it has instead. */
    CTL_KIND_VALUE,
    CTL_KIND_SLIDER,       /* toggle, plus Left/Right on a level */
    /* Left/Right pick from a list drawn in the row itself, Enter opens the
     * panel that owns the setting for the detail the row has no space for. The
     * difference from SLIDER is that the choices are DISCRETE and come from
     * somewhere outside this file, so the row cannot draw or commit one on its
     * own — see ctlpanel_choice_* below. */
    CTL_KIND_CHOICE,
    CTL_KIND_PANEL,        /* opens a synui panel; its Esc comes back here */
    CTL_KIND_LAUNCH,       /* spawns something external; the panel closes */
    CTL_KIND_ACTION,       /* fires and closes */
} syn_ctl_kind_t;

/* Which column has the keyboard. Left/Right (and Tab) move between them, which
 * is what makes the category list a menu and the rows its submenu. */
enum {
    CTL_FOCUS_CATS = 0,
    CTL_FOCUS_ITEMS,
};

/* Upper bound on rows in one category, so callers can size a stack array
 * without walking the table. It was 8, which was the longest category back when
 * the panel had thirty rows in total; the config-backed rows took Windows past
 * forty on their own. Deliberately generous — going over it silently truncates
 * a category, which is the one failure mode that looks like a missing feature
 * rather than a bug.
 *
 * It also bounds the SEARCH result list, which draws from every category at
 * once, so it must be at least as large as the whole table. */
#define CTL_CAT_ITEMS_MAX  CTL_ROW_COUNT

/* A shortcuts-column line. The nine workspace binds (and the nine move-to-
 * workspace binds) are collapsed into one row each — listed literally they are
 * 18 of ~40 rows and drown everything worth reading. */
/* Sizes of one bind's action and argument. Declared here rather than down in
 * the keybinding section because syn_ctl_shortcut_t below carries a copy of
 * both, and a struct cannot use a macro defined after it. */
#define SYN_BINDS_MAX        96
#define SYN_BIND_ACTION_LEN  24
#define SYN_BIND_ARG_LEN     104

typedef struct {
    char combo[48];
    char desc[64];
    /* The bind this line came from, so a list of shortcuts can also RUN one.
     * The control panel's column ignores both — it is read-only — but the
     * Super+/ palette (keys.c) presses Enter on them, and generating the two
     * lists from one function is the whole reason the shortcuts column is not
     * a hand-written table. Empty for the rows that are not a single bind:
     * Super-tap, and the collapsed workspace pair. */
    char action[SYN_BIND_ACTION_LEN];
    char arg[SYN_BIND_ARG_LEN];
    /* The chord itself, for the rebind helper — which has to find this line's
     * entry in the bind table, and cannot do it by action: `spawn` appears
     * three times over and `move_output` twice.
     *
     * `rebindable` is NOT "sym != NoSymbol". The row that is not a single bind
     * and cannot be moved is the collapsed workspace pair: each stands for nine
     * binds, and rebinding "Super+1–9" would mean picking one of the nine the
     * row does not name.
     *
     * `tap` is the OTHER shape that is not a chord — the modifier tap that
     * opens the start menu (config.tap_mod). It is rebindable, but to a bare
     * modifier rather than to a combo, so the two panels' capture loops have to
     * tell them apart: everywhere else a modifier keysym is the half of a chord
     * you are still holding and must be ignored, and here it is the answer.
     * `mods` carries the tap modifier and `sym` is NoSymbol. */
    int          rebindable;
    int          tap;
    uint32_t     mods;
    xkb_keysym_t sym;
} syn_ctl_shortcut_t;

#define CTL_SHORTCUTS_MAX  SYN_BINDS_MAX

/* ── The shortcut palette (keys.c, Super+/) ──────────────────
 *
 * The control panel's Shortcuts category already lists every bind, and that is
 * where the list belongs — but it is a page you have to be *in* the control
 * panel to reach, it does not filter, and it cannot run anything. This is the
 * same list as a palette: one key from anywhere, type to narrow it, Enter to
 * run what you landed on.
 *
 * It is not a second list. Both come out of ctlpanel_shortcuts(), for the
 * reason that function exists at all — a hand-kept copy of the bind table is a
 * copy that goes stale, and this project has shipped that bug before.
 *
 * The whole list is snapshotted into all[] on open rather than rebuilt per
 * frame: the filter runs on every keystroke, and re-walking the bind table to
 * re-derive strings that cannot have changed since the panel opened would be
 * work done once per character typed. Reopening picks up a config reload. */
#define KEYS_MAX        CTL_SHORTCUTS_MAX
#define KEYS_ROWS       14    /* rows drawn at once; the rest scroll */
#define KEYS_QUERY_MAX  48

typedef struct {
    int  visible;
    int  selected;     /* index into view[], NOT into all[] */
    int  scroll;       /* first view[] row drawn */
    char query[KEYS_QUERY_MAX];
    int  query_len;

    syn_ctl_shortcut_t all[KEYS_MAX];
    int  n;

    /* Row -> all[] index, under the query. Rebuilt by keys_filter() on every
     * edit, so the draw, the cursor and the pointer all walk one list. */
    int  view[KEYS_MAX];
    int  n_view;

    /* ── Rebinding ───────────────────────────────────────────
     *
     * F2 (or Ctrl+R) on a row arms `capturing`, and the NEXT chord becomes that
     * shortcut's key. This lives in the palette rather than in a panel of its
     * own because the palette is already the list — "find the shortcut, then
     * change it" is one journey, and a separate rebind window would be a second
     * list of shortcuts, which is the bug ctlpanel_shortcuts() exists to
     * prevent.
     *
     * `capture_all` indexes all[], not view[]: the query can be edited while a
     * capture is armed only by cancelling it first, but all[] indices survive a
     * re-filter and view[] indices do not, and the difference costs nothing.
     *
     * The shortcut being rebound is remembered as its OWN chord rather than as
     * a pointer: the moment a new bind lands, the table is rewritten and any
     * pointer into it is stale. */
    int          capturing;
    int          capture_all;
    uint32_t     capture_mods;   /* the chord the row had when F2 was pressed */
    xkb_keysym_t capture_sym;

    /* One line under the list: what the last rebind did, or why it was refused.
     * Cleared on the next keystroke that is not part of a capture, so it does
     * not sit there describing something two searches ago. */
    char status[96];

    syn_hit_t hit;
} syn_keys_t;

typedef struct {
    int  visible;
    int  cat;          /* syn_ctl_cat_t — the highlighted category */
    int  item;         /* index *within* that category, not a syn_ctl_row_t */
    int  focus;        /* CTL_FOCUS_CATS / CTL_FOCUS_ITEMS */
    int  scroll;       /* first shortcuts row drawn */
    /* ── The shortcuts cursor ────────────────────────────────
     *
     * Which shortcut row is highlighted, indexing the list ctlpanel_shortcuts()
     * builds. The pane used to be a pure scroll with no cursor at all, because
     * it was read-only and there was nothing to point AT; rebinding from here
     * needs a row to rebind, so Up/Down now move this and drag `scroll` along
     * behind them, as they do in every other pane on this panel.
     *
     * Not folded into `item`: that one indexes ctl_items[] within a category,
     * and this category deliberately has no ctl_items[] entries at all (the list
     * is generated from the live bind table). Sharing the field would make
     * ctlpanel_item_count() == 0 stop meaning "no rows here", which is what
     * every Up/Down/Page/Home path on this panel branches on. */
    int  sc_sel;
    /* First SETTINGS row drawn. Separate from `scroll` above because the two
     * lists are different lengths and are scrolled by different keys; sharing
     * one offset carried a shortcuts position into a category with six rows and
     * drew an empty pane. Reset whenever the category changes. */
    int  row_scroll;
    char status[96];

    /* ── Rebinding, from this panel ──────────────────────────
     *
     * F2/Ctrl+R arms `sc_capturing` and the NEXT chord becomes the selected
     * shortcut's key; Ctrl+Shift+R puts every shortcut back. The rules are
     * keys.c's syn_rebind_* — this panel owns only the arming.
     *
     * The target is kept as a COPY of the row rather than an index into
     * anything. The pane rebuilds the shortcut list on every render (it is a
     * view of the live bind table, which the rebind is about to rewrite), so an
     * index would point at a different shortcut the moment the table moved, and
     * a pointer would dangle outright. */
    int                sc_capturing;
    syn_ctl_shortcut_t sc_capture;

    /* ── Search ──────────────────────────────────────────────
     *
     * A hundred settings across nine categories is the point at which knowing
     * the setting exists stops being the same as being able to find it — which
     * is the complaint every deep settings panel eventually earns. Typing
     * filters every row in every category down to what matches, so a name is
     * enough; you do not also have to guess which category we filed it under.
     *
     * Opened with '/' or by typing any letter in the row pane. While it is open
     * the pane shows results instead of a category, each labelled with the
     * category it came from — the label is what makes it a search rather than a
     * second, flatter menu you now have to learn. */
    int  searching;         /* the box is open and taking keys */
    char search[48];
    int  search_len;
    /* The bind action of the panel this one opened, empty when none is out.
     * Set only for CTL_KIND_PANEL rows and only once the panel is confirmed up,
     * because it is what every panel's hide path checks to decide whether its
     * Esc means "back to the control panel" or "back to the desktop". A stale
     * value here would pop the control panel open at some unrelated close. */
    char child[24];
    int  child_cat, child_item;   /* where the cursor was — restored on return */
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
    /* Which row the poll above is watching. It was written for the AI backend
     * and hardcoded to that row; the desktop-widgets row has exactly the same
     * shape (fire a helper, the value lands a moment later), so the row is a
     * field rather than a second copy of the machinery. */
    int    poll_row;
    /* The AI-model row's settle timer: CLOCK_MONOTONIC deadline at which the
     * model the cursor has landed on is actually asked for, 0 when nothing is
     * pending.
     *
     * Left/Right cannot load on the keypress. The choices are multi-gigabyte
     * GGUFs and cycling from the first to the third would load the second on
     * the way past — a key repeat would load every model in the directory in
     * turn. So the row moves instantly and the request waits for the cursor to
     * stop, which is also what makes holding the key harmless. */
    double model_commit_at;
    /* Pointer geometry, written by synui_render_ctlpanel(). Two grids because
     * the panel is two columns and a click has to know which one it landed in —
     * that is the same question Tab answers for the keyboard. Both carry the
     * same panel rect, so either one answers "was this click off the panel". */
    syn_hit_t hit;         /* panel rect; rows = the category sidebar */
    syn_hit_t hit_items;   /* same rect; rows = the settings/shortcuts pane */
    /* Dragged position + keyboard, in window mode. See syn_panel_win_t. */
    syn_panel_win_t win;
} syn_ctlpanel_t;


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

    /* How many toasts Do Not Disturb has swallowed since it was switched on.
     * Runtime only — it is deliberately NOT persisted, because "you missed 12
     * things" is a fact about this session and a stale count restored at login
     * would be a lie the user cannot check. Reset every time DND is enabled,
     * and reported once when it is switched off.
     *
     * The DND flag itself lives in syn_config_t, not here: it survives a
     * restart AND a config reload, and only the config sources are re-read by
     * synui_config_reload(). See notif_dnd_state_load_config(). */
    int         missed;

    /* The "Do Not Disturb on/off" toast, kept so toggling twice replaces one
     * card instead of stacking two. 0 until the first toggle. */
    uint32_t    dnd_notif_id;
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
    /* Pointer geometry, written by this panel's synui_render_*. */
    syn_hit_t hit;
} syn_clipboard_t;

/* ── Mission control / overview (overview.c) ─────────────────
 *
 * Every window on the desktop, scaled down, laid out so none of them overlap,
 * with the virtual desktops along the bottom. GNOME's Activities and macOS's
 * Mission Control, and the point is the same: the desk you cannot see because
 * of the windows on it.
 *
 * ON ALT+TAB since 2026-08-07 (`alt_tab_style`), and still not the switcher.
 * The strip answers "the window I was just in" and is built around that: MRU
 * order, one fixed-size grid in the middle of the screen, up while a key is
 * held. This answers "where did I put it", so it is spatial rather than
 * temporal — stable order, tiles over the whole output at whatever size they
 * fit, and it stays up until you pick something. Sharing the key did not make
 * them the same panel: overview_alt_step moves a selection through the stable
 * grid, it does not reorder it. They share their thumbnail machinery (render.c's
 * alttab_tile_source) and nothing else.
 *
 * ── It stores no view pointers, and that is load-bearing ──
 *
 * The candidate list and the tile layout are both recomputed from live state on
 * every render AND on every pointer event — overview_candidates() and
 * overview_layout() are pure functions of the workspace and the output box. So
 * there is nothing here for a closing window to dangle, and no fifth place to
 * remember on view destroy. It is the same trade the switcher makes: a window
 * that closes between the frame and the click shifts what the click lands on by
 * one, and the alternative is a snapshot that has to be invalidated from four
 * different places.
 *
 * `selected` is an index into that recomputed list for the same reason.
 */
#define OVERVIEW_MAX      48   /* tiles; past this the desk is not the problem */
#define OVERVIEW_GAP      18   /* between tiles */
#define OVERVIEW_MARGIN   48   /* from the output's edges */
#define OVERVIEW_LABEL_H  24   /* title strip under each tile */
#define OVERVIEW_STRIP_H  64   /* the virtual-desktop pills along the bottom */
#define OVERVIEW_HEAD_H   44   /* the heading along the top */

typedef struct {
    int visible;
    int selected;      /* index into the list overview_candidates() rebuilds */
    /* Opened by Alt+Tab with Alt still held (alt_tab_overview). While this is
     * set the overview is behaving as a switcher: Tab walks the tiles and
     * LETTING GO OF ALT activates the one under the cursor of the selection.
     * Cleared the moment it is opened any other way, or committed, so a plain
     * mission control never closes itself on a stray modifier. */
    int alt_held;
} syn_overview_t;

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
     * render and read by the pointer hit-tests. */
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
/* What closing a laptop lid does. Three of these are configured, because the
 * right answer genuinely differs with the situation: a lid closed on the sofa
 * means "sleep", a lid closed on a desk with a monitor attached means "keep
 * working", and one closed on a charger is somewhere in between. Same three
 * cases logind splits HandleLidSwitch / ExternalPower / Docked into, and
 * resolved in the same order — docked wins, then mains, then battery.
 *
 * SYN_LID_SYSTEM hands the lid back to logind (whatever HandleLidSwitch= in
 * logind.conf says). Every other value means synui takes logind's
 * handle-lid-switch inhibitor and acts itself. */
typedef enum {
    SYN_LID_SYSTEM = 0,   /* leave it to logind (default HandleLidSwitch) */
    SYN_LID_IGNORE,       /* nothing at all */
    SYN_LID_BLANK,        /* DPMS the built-in panel off, keep externals */
    SYN_LID_LOCK,         /* blank the panel and lock the session */
    SYN_LID_SUSPEND,      /* run power_suspend_cmd */
    SYN_LID_ACTION_COUNT, /* keep last — the panel steps on it */
} syn_lid_action_t;

/* Names for the panel, the config parser and power.state, indexed by
 * syn_lid_action_t. Defined in power.c; keep in step with the enum above. */
extern const char *const syn_lid_action_names[SYN_LID_ACTION_COUNT];

/* Which window the keyboard follows. See syn_config_t.focus_mode.
 *
 * SYN_FOCUS_CLICK is the default and is what synui did before this existed;
 * the two pointer modes differ only over the desktop, which is the whole
 * distinction between KWin's "follows mouse" and "strictly under mouse". */
typedef enum {
    SYN_FOCUS_CLICK = 0,   /* only a click moves focus                        */
    SYN_FOCUS_SLOPPY,      /* pointer moves it; the desktop keeps the last    */
    SYN_FOCUS_STRICT,      /* pointer moves it; the desktop takes it away     */
    SYN_FOCUS_MODE_COUNT,  /* keep last — the panel steps on it               */
} syn_focus_mode_t;

/* Indexed by syn_focus_mode_t. Defined in config.c, beside the parser that
 * reads them; keep in step with the enum above. Note these double as the
 * control panel's display names, folded to lower case on the way to disk. */
extern const char *const syn_focus_mode_names[SYN_FOCUS_MODE_COUNT];
/* syn_lid_action_t for an action name, or -1 if it is not one. */
int lid_action_from_name(const char *name);

/* Panel rows, in display order. POWER_ROW_ENABLED toggles the master switch,
 * the four after it each map to one syn_config_t idle timeout, and the last
 * three pick a syn_lid_action_t rather than a timeout. The lid rows are listed
 * least-specific first, which is the reverse of the order they are resolved
 * in — the panel's header note names whichever one is live, so the precedence
 * is visible without the rows having to encode it. */
typedef enum {
    POWER_ROW_ENABLED = 0,
    POWER_ROW_DIM,
    POWER_ROW_BLANK,
    POWER_ROW_LOCK,
    POWER_ROW_SUSPEND,
    POWER_ROW_LID,          /* on battery */
    POWER_ROW_LID_AC,       /* plugged in */
    POWER_ROW_LID_DOCKED,   /* external monitor — beats both of the above */
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
    /* The screensaver stage. Its timeout lives in config.saver_timeout with the
     * rest of the saver's settings rather than beside power_dim, so the Super+Z
     * panel edits one block — but it is armed and disarmed by power.c with the
     * four above, because it IS one of them. */
    struct wl_event_source *t_saver;
    int dimmed;
    int blanked;

    /* Post-resume sink sweep. A resume re-enables every output synchronously,
     * but a DP link that is going to fail has not failed yet at that moment —
     * so the un-blank can bind a CRTC to a head that is about to go away, and
     * on DP-3 that is what stops the panel re-enumerating. This ticks
     * power_apply_blank() for a short window afterwards so the release happens
     * once the connectors have settled. Counts down; 0 = not sweeping. */
    struct wl_event_source *t_sinksweep;
    int sink_sweeps;
    int locked;        /* we spawned the locker and have seen no activity since */
    uint32_t last_arm_ms;  /* rearm throttle — see power_notify_activity */

    /* Lid state, as last reported by a switch device. lid_blanked is separate
     * from `blanked` above because the lid only ever turns the built-in panel
     * off, so opening it again must not re-enable outputs the idle blank
     * stage is legitimately holding down. */
    int lid_closed;
    int lid_blanked;
    int lid_seen;      /* a lid switch exists — the panel says so if it doesn't */
    /* Pointer geometry, written by this panel's synui_render_*. */
    syn_hit_t hit;
} syn_power_t;

/* ── Game mode (game.c) ───────────────────────────────────── */
typedef struct {
    int  active;        /* engaged right now */
    int  forced;        /* Super+G: -1 forced off, 0 auto, +1 forced on */
    int  ai_suspended;  /* we stopped synapd and owe it a restart */
    /* Each is "we did this and owe the undo", never "this is the current
     * state" — so a setting flipped mid-game, or a wallpaper the user had off
     * already, is not clobbered on the way out. */
    int  effects_dropped;
    int  wallpaper_paused;
    int  bar_stopped;
    int  kmod_quieted;
    int  effects_saved;  /* what config.effects was before we dropped it */
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
    /* Pointer geometry, written by this panel's synui_render_*. */
    syn_hit_t hit;
    /* Dragged position + keyboard, in window mode. See syn_panel_win_t. */
    syn_panel_win_t win;
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
    /* Pointer geometry, written by this panel's synui_render_*. */
    syn_hit_t hit;
    /* Row and time of the last left click — a double click opens the story,
     * on the same 400ms window as the pickers and the titlebar. */
    int       last_click_row;
    uint32_t  last_click_ms;
} syn_news_t;

/* ── Keybinding (table-driven; syntax in config.c) ─────────
 * The three size limits live up by syn_ctl_shortcut_t, which copies an
 * action/arg out of this table and so needs them before this point. */

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
    /* A Steam Workshop wallpaper rendered by linux-wallpaperengine, which is
     * an external layer-shell client, NOT a synui backend: it paints its own
     * BACKGROUND surface, which sits above wallpaper_tree and so covers
     * whatever wallpaper.c drew. synui only starts and stops it (via
     * synui-wpengine); nothing here renders. Never persisted to
     * wallpaper.state — synui-wpengine owns that state, and the synuirc
     * autostart line replays it at login. */
    SYN_WP_SRC_WPENGINE,
} syn_wallpaper_src_t;

/* How many outputs can carry their own wallpaper. Overrides are keyed by
 * connector name rather than by index so a monitor keeps its picture across
 * a hotplug that renumbers the others. */
#define SYN_WP_PEROUT_MAX 8

/* One monitor's wallpaper, overriding the global `wallpaper` keys for that
 * connector only. An output with no entry here uses the global config, so an
 * untouched setup behaves exactly as it did before per-monitor wallpapers
 * existed. Written by the Super+W picker (scope = a monitor) and by the
 * synuirc `wallpaper_output` key. */
typedef struct {
    char                  output[32];   /* connector name, e.g. "DP-1" */
    char                  path[256];    /* image path; empty = solid bg_color */
    syn_wallpaper_mode_t  mode;
    syn_wallpaper_src_t   src;
} syn_wp_output_t;

/* ── Screensaver (saver.c) ───────────────────────────────────
 *
 * NOT screensaver.c, which owns the org.freedesktop.ScreenSaver D-Bus name so
 * apps can *inhibit* idle. This is the thing that actually draws. The two meet
 * only at idle_inhibited(): an app holding a D-Bus inhibit stops this from ever
 * showing, which is the whole point of that file.
 */
typedef enum {
    SYN_SAVER_BLANK = 0,      /* flat black — the old behaviour, kept nameable */
    SYN_SAVER_CLOCK,          /* a drifting clock; cairo, no GPU needed */
    SYN_SAVER_STARFIELD,      /* flight through starfield; cairo */
    SYN_SAVER_SLIDESHOW,      /* wallpapers, crossfaded */
    SYN_SAVER_MATRIX,         /* the matrix.c kanji rain, full screen */
    SYN_SAVER_MODE_COUNT,     /* keep last — the Super+Z panel cycles on it */
} syn_saver_mode_t;

/* Names for the panel, the config parser and saver.state, indexed by
 * syn_saver_mode_t. Defined in saver.c; keep in step with the enum above.
 * Names, not indices, in the state file, for the reason the lid actions are:
 * the enum will grow and a saved 3 must not become a different mode after it
 * does. */
extern const char *const syn_saver_mode_names[SYN_SAVER_MODE_COUNT];
int saver_mode_from_name(const char *name);

/* What the lock screen and greeter draw BEHIND the clock panel. DESKTOP is the
 * default: whatever wallpaper the desktop is showing, dimmed and blurred so the
 * panel stays readable over a busy photo. BLACK is what the lock did before
 * this existed and is still the right answer on a slow machine — the blur is a
 * per-lock cost, not a per-frame one, but the decode is not free.
 *
 * The MATRIX and Workshop wallpaper backends have no still frame to grab, so
 * DESKTOP falls back to BLACK on an output showing one. See lock_bg_surface(). */
typedef enum {
    SYN_LOCK_BG_DESKTOP = 0,
    SYN_LOCK_BG_BLACK,
    SYN_LOCK_BG_IMAGE,        /* lock_bg_image, a path of its own */
    SYN_LOCK_BG_COUNT,
} syn_lock_bg_t;

extern const char *const syn_lock_bg_names[SYN_LOCK_BG_COUNT];
int lock_bg_from_name(const char *name);

/* Rows in the Super+Z screensaver panel. */
typedef enum {
    SAVER_ROW_MODE = 0,
    SAVER_ROW_TIMEOUT,
    SAVER_ROW_LOCK,
    SAVER_ROW_INTERVAL,     /* slideshow seconds per image */
    SAVER_ROW_LOCK_BG,      /* lock/greeter background source */
    SAVER_ROW_LOCK_DIM,
    SAVER_ROW_LOCK_BLUR,
    SAVER_ROW_LOCK_THEME,   /* follow the desktop theme, or not */
    SAVER_ROW_COUNT,
} syn_saver_row_t;

/* How many outputs the saver can paint at once. Same bound as the lock's
 * panes, and for the same reason: past this the backstop keeps the screen
 * black, which is a safe degradation rather than a crash. */
#define SYN_SAVER_PANE_MAX 8

/* One star in the SYN_SAVER_STARFIELD mode. z is depth; a star that flies past
 * the viewer is respawned at the back rather than reallocated. */
typedef struct {
    float x, y, z;    /* x/y in [-1,1] at z=1; z in (0,1] */
    float pz;         /* previous z, so the streak has a length */
} syn_star_t;

#define SYN_SAVER_STARS 320

/* Live screensaver state. `active` is the saver being ON SCREEN, which is a
 * different thing from the stage being armed — see saver.c. */
typedef struct {
    int  active;
    int  visible;                  /* the Super+Z settings panel is open */
    int  selected;                 /* syn_saver_row_t, in the panel */
    int  dirty;                    /* unsaved panel edits */
    char status[96];               /* panel footer line */
    syn_hit_t hit;                 /* panel rows, for the pointer */

    /* Everything below is only live while `active`. */
    struct wlr_scene_tree   *tree;
    struct {
        struct wlr_output       *output;
        struct wlr_scene_buffer *buf;
    } pane[SYN_SAVER_PANE_MAX];
    int  npane;

    struct wl_event_source *t_frame;   /* animation tick */
    uint32_t start_ms;                 /* when the saver came up */
    uint32_t last_frame_ms;

    /* Repaint gating. The tick runs at 30 Hz because the starfield needs it,
     * but a full-screen cairo buffer is ~33 MB on a 4K panel and reallocating
     * one thirty times a second — for a CLOCK whose pixels change once a
     * minute — is about a gigabyte a second of memory traffic on a machine
     * that is supposed to be idling. lock.c makes the same point about its own
     * panel and solves it the same way.
     *
     * `drawn_min` is the minute the current buffer was painted for; -1 forces
     * the next tick to paint. `repaint` is a one-shot for everything else that
     * invalidates it (a new slide, an output arriving). */
    int      drawn_min;
    int      repaint;

    /* Whether the session was already locked when the saver came up. A saver
     * that draws over the LOCK screen must not unlock anything when it is
     * dismissed, and must not re-lock on the way out either. */
    int  over_lock;

    /* SLIDESHOW: the image list, rebuilt on show so a wallpaper added since
     * last time is picked up. Paths only; the decode is cached one at a time,
     * because a full-screen decode is megabytes and a slideshow that held ten
     * of them would cost more resident memory than the compositor. */
    char   (*slides)[256];
    int      nslides;
    int      slide;                /* index of the image showing now */
    uint32_t slide_started_ms;
    cairo_surface_t *slide_surf;   /* the image showing now */
    cairo_surface_t *slide_prev;   /* the one crossfading out */

    /* STARFIELD */
    syn_star_t stars[SYN_SAVER_STARS];

    /* CLOCK: the drift, in layout coordinates, so the glyphs do not sit on the
     * same pixels for hours. Burn-in is not hypothetical on the OLED. */
    double drift_x, drift_y, drift_dx, drift_dy;
} syn_saver_t;

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

/* Which QML tree synui-bar starts (systemd/synui-bar.sh reads the resolved
 * value out of settings.state, falling back to synuirc's `bar_shell`).
 *
 * synui does not launch the bar itself and never reads this — the key exists in
 * the parser so that ONE file spells the setting and the control panel can
 * write it through the same settings.state path as everything else. A row that
 * wrote a private file would be the eighth per-subject state file settings.c
 * exists to stop.
 *
 * Order matches syn_bar_shell_names[], and the lower-cased spellings are what
 * config.c parses back out of settings.state. */
typedef enum {
    SYN_BAR_SHELL_SYNAPSE = 0,  /* quickshell/ — the shipped SYNAPSE bar */
    SYN_BAR_SHELL_ANTIQUITY,    /* quickshell-antiquity/ — the diinki port */
    SYN_BAR_SHELL_COUNT,        /* keep last */
} syn_bar_shell_t;

extern const char *const syn_bar_shell_names[SYN_BAR_SHELL_COUNT];

/* Which screen edge the bar sits on. The dock has had this since it learned to
 * be dragged to an edge; the bar was nailed to the top, which on a desktop
 * whose dock is also at the top means both furniture stacked in one corner and
 * nothing along the bottom.
 *
 * TOP and BOTTOM only, where the dock has four. That is a real difference, not
 * an unfinished job: the dock is an icon strip and rotates into a column
 * unchanged, while the bar is a horizontal row — start button, desktop pills, a
 * centred clock, a tray — that has no vertical form. A left/right bar would be
 * a different bar, not this one turned on its side.
 *
 * Like bar_shell, the COMPOSITOR NEVER ACTS ON THIS. quickshell owns the bar;
 * the key lives in this parser so that one file spells the setting and the
 * control panel can persist it through settings.state like everything else.
 * BarConfig.qml reads it back out. Order matches syn_bar_edge_names[]. */
typedef enum {
    SYN_BAR_EDGE_TOP = 0,
    SYN_BAR_EDGE_BOTTOM,
    SYN_BAR_EDGE_COUNT,     /* keep last */
} syn_bar_edge_t;

extern const char *const syn_bar_edge_names[SYN_BAR_EDGE_COUNT];

/*
 * What SHAPE the bar is, which is the bar's share of `corner_radius`.
 *
 * FULL is the strip this project shipped for a year: edge to edge, square, its
 * accent rule running the whole width. ENDS keeps that strip and rounds the two
 * corners that face the desktop. PILL detaches it — a margin down both sides and
 * off the edge it lives on — and closes it into a capsule.
 *
 * NONE OF THEM APPLY WITH THE CORNERS OFF. chrome_corner_radius() is 0 for a
 * radius of 0 and for the retro chromes, and the bar reads the same fact: a
 * Windows 95 desktop with a floating capsule across the top is the same mistake
 * as a Windows 95 window with a 12px radius, and gating on the radius means
 * there is ONE rule rather than a shape row that has to know about chrome. So
 * this is a preference for what to do WHEN rounded, not an independent switch —
 * which is also why FULL is the default: it is what a desktop that never turns
 * the corners on keeps seeing.
 *
 * Like bar_edge and bar_shell, the COMPOSITOR NEVER ACTS ON THIS. quickshell
 * owns the bar; the key is parsed here so one file spells the setting and the
 * control panel can persist it through settings.state. Order matches
 * syn_bar_shape_names[].
 */
typedef enum {
    SYN_BAR_SHAPE_FULL = 0,
    SYN_BAR_SHAPE_ENDS,
    SYN_BAR_SHAPE_PILL,
    SYN_BAR_SHAPE_COUNT,    /* keep last */
} syn_bar_shape_t;

extern const char *const syn_bar_shape_names[SYN_BAR_SHAPE_COUNT];

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

/* deskmenu.c: the desktop (wallpaper) right-click menu. SEP draws a rule and
 * is not selectable; everything else is a row. */
typedef enum {
    SYN_DESKACT_TERMINAL = 0,
    SYN_DESKACT_FILES,
    SYN_DESKACT_APPS,        /* the start menu */
    SYN_DESKACT_SEP,
    SYN_DESKACT_WALLPAPER,
    SYN_DESKACT_THEME,
    SYN_DESKACT_DISPLAY,
    SYN_DESKACT_ICONS,       /* toggle desktop icons at runtime */
    /* Icon sort order. Only offered while the icons are on, since choosing one
     * with nothing drawn would be a setting with no visible effect. */
    SYN_DESKACT_ARRANGE_NAME,
    SYN_DESKACT_ARRANGE_TYPE,
    SYN_DESKACT_ARRANGE_SIZE,
    SYN_DESKACT_ARRANGE_DATE,
    SYN_DESKACT_TASKMGR,
    /* Offered only when the right-click landed ON an icon, and listed first:
     * a menu about the file you clicked has to be about that file before it is
     * about the desktop it sits on. */
    SYN_DESKACT_ICON_OPEN,
    SYN_DESKACT_ICON_TRASH,
} syn_deskact_t;
#define SYN_DESKMENU_MAX 20

/* The order the auto-grid flows ~/Desktop in. Every mode falls back to name
 * order for ties, so the desktop never reshuffles between two equal files. */
typedef enum {
    SYN_ARRANGE_NAME = 0,    /* case-insensitive, the default */
    SYN_ARRANGE_TYPE,        /* folders first, then by extension */
    SYN_ARRANGE_SIZE,        /* folders first, then largest first */
    SYN_ARRANGE_DATE,        /* most recently modified first */
} syn_arrange_t;

/* How many tiles the Alt+Tab switcher can put on screen at once — six columns
 * by three rows. Not a cap on the *cycle*, which still walks every candidate:
 * a nineteenth window scrolls the grid a page (render.c), the way the clipboard
 * panel pages its rows. Six across is what fits at a legible tile size on a
 * 1920-wide screen; more would mean shrinking the thumbnails to thumbnails of
 * thumbnails. */
#define SYN_ALTTAB_TILES  18

/* One spelling of each mode, shared by deskicons.state and synuirc. */
const char *syn_arrange_name(syn_arrange_t a);
bool syn_arrange_parse(const char *s, syn_arrange_t *out);

/* One ~/Desktop entry. `exec` and `icon_surface` are only meaningful for a
 * .desktop file; anything else opens through xdg-open on its path. The
 * surface is owned by icons.c's cache, not by us — never free it here. */
#define SYN_DESKICON_MAX  128
#define SYN_DESKICON_W    96     /* cell size, including the label */
#define SYN_DESKICON_H    92
#define SYN_DESKICON_PAD  16     /* inset from the usable area's edge */
typedef struct {
    char  path[512];
    char  label[128];
    char  exec[256];
    int   is_dir;
    int   is_desktop;
    /* Straight off the stat() the scan already does, so an arrange-by-size or
     * -date sort never has to walk ~/Desktop a second time. */
    off_t  size;
    time_t mtime;
    int   x, y;                    /* cell origin, layout coords */
    /* The user dragged this one: its cell came from deskicons.state (or from a
     * drop this session) and the auto-grid must leave it alone. */
    int   placed;
    /* Where the user actually put it, in layout coords — the drop, or the
     * deskicons.state line. Kept apart from x/y because x/y is *this* layout's
     * answer: the cell the pin snaps to depends on the usable box, which shrinks
     * when the bar reserves its strip and changes again on every display config.
     * Re-snapping x/y would fold each of those into the placement for good;
     * re-snapping the pin lets the icon come back the moment the box does, and
     * it is the pin that gets persisted. Only meaningful while `placed`. */
    int   pin_x, pin_y;
    cairo_surface_t *icon_surface; /* borrowed from icons.c; may be NULL */
} syn_deskicon_t;

typedef struct {
    char  terminal[64];
    char  autostart[SYN_AUTOSTART_MAX][128];
    int   autostart_count;
    int   border_width;
    int   gap;
    /* The floating desktop's aesthetic tiler; see FLOAT_INSET_DEFAULT. */
    int   float_inset;
    int   float_gap;
    float master_factor;
    /* LAYOUT_CASCADE: windows per pile before a second pile starts beside it. */
    int   cascade_stack_max;
    int   ai_layout;
    int   ai_ctx_decor;
    int   start_overlay;

    /* Drag a window against a screen edge to snap it to that half/quarter
     * (snap.c). Off means a drag is only ever a move. */
    int   snap;

    /* How wide the armed band along each screen edge is, in px. Was a fixed 28
     * (SNAP_EDGE). Worth a knob because the right number is a property of the
     * desk, not of the compositor: on a 4K panel 28px is a sliver you have to
     * aim at, and with a mouse set fast it is a band you cross without ever
     * being inside it for a frame. */
    int   snap_zone;

    /* ── Window behaviour ────────────────────────────────────────────────
     *
     * focus_mode. Click-to-focus is what synui has always done and stays the
     * default. The two pointer modes differ only in what happens over the
     * DESKTOP: "sloppy" keeps the last window focused, "strict" drops focus to
     * nothing — which is the distinction KWin draws between Focus Follows
     * Mouse and Focus Strictly Under Mouse, and the reason both exist.
     *
     * Deliberately does not touch the click path: clicking still focuses under
     * every mode, so a pointer mode is additive and cannot leave a window
     * unreachable if the pointer logic is wrong. */
    int   focus_mode;              /* syn_focus_mode_t */

    /* How long the pointer must rest on a window before focus follows, in ms.
     * 0 is immediate. Nonzero exists because focus that follows an in-flight
     * pointer steals keystrokes as you cross a window on the way somewhere
     * else — the keypress lands wherever the pointer happened to be. */
    int   focus_delay_ms;

    /* Does the compositor's own UI — the panels, the pickers, the toasts —
     * re-centre on whichever output the pointer is over, repaint by repaint?
     *
     * OFF, and off is the default. get_output_box() asks server_focused_output()
     * on every repaint, and that answers "the output under the cursor" first —
     * so an open panel that repaints (the task manager ticks, the control panel
     * repaints on hover) TELEPORTED to the other monitor the moment the pointer
     * crossed onto it, mid-read. velle, 2026-08-08: "turn off that thing where
     * windows chase the mouse around ... if i move the cursor to the other
     * monitor it will move task manager over there too ... i hate it myself".
     *
     * Off, the output is pinned by server_ui_output_track() at the click or
     * keystroke that opened the panel and held for as long as any panel is up,
     * so a panel stays on the monitor it was opened on. On restores the old
     * behaviour for anyone who wants the UI to come to the pointer. */
    int   panel_follow_pointer;

    /* NOT HERE YET, on purpose. KWin's other two window-behaviour staples —
     * focus-stealing prevention and "raise on click" — both want a change
     * inside focus_view(), which raises unconditionally and is called by
     * Alt+Tab, the dock, workspace switches and the layout as well as by a
     * click. Gating it there would change all of those, so the setting would
     * not mean what its label says. They need the raise separated from the
     * focus first; a row that does not do what it claims is worse than no
     * row. */

    /* The grid of window thumbnails Alt+Tab puts on screen while Alt is held
     * (render.c synui_render_alttab). Off leaves the cycle itself untouched —
     * Alt+Tab still walks the MRU order, it just does it silently, which is what
     * it did before the overlay existed. */
    int   alt_tab_preview;

    /* What the cycle is allowed to reach (input.c alttab_candidates). Both
     * default on: "the window I was just in" is as often on another desktop or
     * minimized as it is on screen, and a switcher that cannot reach those
     * makes you remember where you put them, which is the job it exists to do.
     *
     * Turning either off narrows the list only. Neither changes what happens
     * when you land on such a window — that is alttab_reveal(), which switches
     * desktop and un-minimizes at commit whatever these say, because a window
     * that is in the list at all has to be reachable from it. */
    int   alt_tab_all_desktops;
    int   alt_tab_minimized;

    /* WHICH SWITCHER Alt+Tab is. On (the default, velle 2026-08-07) the key
     * opens mission control — the whole desk, spatially, at a size you can
     * actually see — and tapping Tab with Alt still down walks the tiles;
     * letting go activates the one you landed on, so the gesture is the one
     * every switcher has. Off restores the MRU thumbnail strip (alttab_step),
     * which answers "the window I was just in" instead of "where did I put it".
     *
     * This is a style toggle, not a feature switch: both paths end on a focused
     * window and both are reachable from the keyboard. Mission control lost its
     * own key (super+x) when it took this one — it is not worth two. */
    int   alt_tab_overview;

    /* Border colors (RGBA 0..1) by window role; defaults COLOR_BORDER_*. */
    float border_color_norm[4];
    float border_color_focus[4];
    float border_color_ai[4];
    float border_color_warn[4];

    /* Server-side titlebar: drag to move, double-click to maximize, and the
     * three buttons. `titlebar_height` of 0 turns it off entirely (windows keep
     * their borders, and Super+drag still moves them). */
    /*
     * anim.c. `animation_ms` is the LEGACY key and is no longer read by
     * anything: config.c still accepts it and writes BOTH durations below, so
     * an existing synuirc keeps working and keeps meaning what it said. Every
     * consumer reads the specific one, because "how long does a window take to
     * appear" and "how long does the desk take to change" are separate answers.
     *
     * A duration of 0 disables that event's animation outright — the end state
     * is applied on the spot, and the rest of the compositor never has to care
     * which mode it is in.
     */
    int   animation_ms;        /* legacy alias; sets the two below           */
    int   anim_window_ms;      /* open/close, and the niri strip slide       */
    int   anim_workspace_ms;   /* virtual-desktop switch                     */
    int   anim_window;         /* syn_anim_window_t                          */
    int   anim_workspace;      /* syn_anim_ws_t                              */
    int   anim_curve;          /* syn_anim_curve_t, shared by both           */
    int   anim_rise_px;        /* how far ANIM_WINDOW_RISE travels           */

    int   titlebar_height;

    /* synuirc remember_geometry (default on): record each app's window
     * geometry when it closes and reopen it there. See geom_persist.c. */
    bool  remember_geometry;

    /* synuirc desktop_icons (default OFF): draw ~/Desktop on the wallpaper.
     * The desktop right-click menu can flip this at runtime, and the flip is
     * persisted to deskicons.state, which overrides this. */
    bool  desktop_icons;

    /* synuirc desktop_icon_arrange (default name): the order the auto-grid
     * flows icons in. Menu-flippable and persisted the same way. */
    syn_arrange_t desktop_icon_arrange;
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

    /* The surface those panels are drawn ON, and the ink on it. Same idea as
     * panel_accent one layer down: the accent alone being theme data meant every
     * panel kept SYNAPSE's near-black navy under a themed highlight, so a light
     * theme opened black panels on a beige desktop. Derived from the theme's app
     * window pair unless the preset names them (see theme.c). */
    float panel_bg[4], panel_ink[4];

    /* A palette handed in from OUTSIDE the preset table
     * (`synctl dispatch theme <accent> <base> <ink>`), which is how the bar's
     * own theme picker carries its palette onto the compositor's surfaces.
     * There is no sixth preset to add for it: the bar's palettes are the bar's,
     * they can be edited by hand in its Config.qml, and a preset table that has
     * to be kept in step with a QML file in another process is a preset table
     * that will be wrong. So the three colours ARE the theme, and theme.c
     * derives the rest of the chrome from them.
     *
     * `theme_custom` set means these override whatever `theme` names — which is
     * still tracked, because the two are not exclusive: only the colours here
     * are pushed, and everything a preset owns that these do not say (the warn
     * border, the chrome style, the opacity levels) keeps coming from it.
     * Picking a preset in the theme manager clears the flag. Persisted to
     * theme.state so it survives the login the bar does not re-apply on. */
    int   theme_custom;
    float theme_custom_accent[4], theme_custom_base[4], theme_custom_ink[4];

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
    /* How far the blur reaches PAST the window, in px. 0 (default) keeps it
     * inside the frame. Non-zero grows each blurred buffer's companion node and
     * drops its transparency mask, giving a blurred ring around the window —
     * the halo Firefox has always had by accident, because it ignores
     * xdg-decoration and keeps a GTK shadow margin inside its own surface. */
    int   glass_halo;            /* px; default 0 = off */

    /* Crop a client's surface to its xdg window geometry.
     *
     * A client that draws its own decorations reserves an invisible margin
     * around the visible window for its CSD drop shadow, and reports the window
     * proper via xdg_surface.set_window_geometry. synui says SERVER_SIDE over
     * xdg-decoration, but a client only has to obey if it *binds* the protocol
     * and Firefox never does — so it keeps painting a heavy GTK shadow in that
     * margin, OUTSIDE synui's own border, on top of synui's shadow. The result
     * is a second, bigger, square-cornered ring around exactly one app.
     *
     * Cropping to the geometry box hides the margin, so every window's ring is
     * the one synui draws: same size, same rounded corners, everywhere. It also
     * takes the margin's input region off the grab ring, which is what made
     * Firefox unresizable by its edges (see xdg_toplevel_request_resize). */
    int   clip_csd_margin;       /* default 1 = on */

    /* Drop shadow (scenefx wlr_scene_shadow, one node per window frame, drawn
     * behind everything and clipped out from under the window itself so it is a
     * soft outer ring — see view_shadow_update). `shadow` gates it; disabled
     * while maximized/fullscreen (an edge-to-edge window's shadow is clipped to
     * nothing). shadow_blur_sigma is the softness in px; the box is grown
     * 2·sigma so the falloff isn't cut off. shadow_offset_{x,y} bias the drop
     * direction (default straight down a touch). shadow_color is RGBA.
     *
     * shadow_spread pushes the shader's SOLID rect out past the window instead
     * of leaving only the gaussian tail outside it. Without it, peak darkening
     * at the border is half of shadow_color's alpha however high that alpha
     * goes — the shader insets the solid rect by sigma, so the window edge sits
     * exactly at the gaussian's half-way point and 50% is the ceiling. A
     * spread of S gives S px at FULL alpha before the tail begins, which is how
     * a GTK CSD shadow (Firefox's) gets its weight and its hard outer edge. */
    int   shadow;                /* master switch; default 1 */
    float shadow_blur_sigma;     /* px; default 18 */
    float shadow_spread;         /* px of solid shadow outside the window; default 0 */
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

    /* Per-monitor overrides of the three keys above (synuirc
     * `wallpaper_output`, or the Super+W picker with a monitor scope). An
     * output with no entry falls back to the global values, which is the only
     * state a setup that never asked for per-monitor wallpapers can be in. */
    syn_wp_output_t       wallpaper_out[SYN_WP_PEROUT_MAX];
    int                   wallpaper_out_n;

    /* ── Screensaver (saver.c) ────────────────────────────────
     *
     * `saver_timeout` is an idle stage like the four in power.c and is armed
     * by the same code, but it lives here rather than beside power_dim so the
     * saver's settings read as one block. 0 = never, which is also what a
     * config that predates this feature parses to — the saver is opt-in, and
     * an existing install's idle behaviour is unchanged until it is asked for.
     */
    int   saver_timeout;
    syn_saver_mode_t saver_mode;

    /* Lock the session when the saver is dismissed. Off by default: a
     * screensaver that demands a password is a different feature from one that
     * shows a clock, and turning the first on by surprise is how somebody gets
     * locked out of their own desktop. The lock STAGE (power_lock) is
     * unaffected and still fires on its own timeout. */
    int   saver_lock;

    /* SLIDESHOW source. Empty = the wallpapers the Super+W picker offers, which
     * is what makes the mode work with no configuration at all. */
    char  saver_dir[256];
    int   saver_interval;      /* seconds per image; clamped in saver.c */

    /* ── Lock / greeter appearance (lock.c) ───────────────────
     *
     * The lock screen and the greeter are the same drawing (see greeter.c), so
     * these style both. Before this they were ~20 hardcoded cairo literals, and
     * a theme switch reskinned every panel in the desktop except the two
     * screens people look at longest. */
    syn_lock_bg_t lock_bg;
    char  lock_bg_image[256];  /* used when lock_bg == SYN_LOCK_BG_IMAGE */
    int   lock_bg_dim;         /* 0..100% darkening over the background */
    int   lock_bg_blur;        /* box-blur radius in px; 0 = none */

    /* Follow the desktop theme's accent/ink (default), or use lock_accent. A
     * custom accent is kept as its own field rather than overwriting
     * panel_accent, so switching back to "follow" needs nothing restored. */
    int   lock_theme_follow;
    float lock_accent[4];

    /* Cursor theme (cursor.c). Empty name = whatever XCURSOR_THEME says, which
     * is what synui did unconditionally before this existed. The size is pinned
     * rather than left to libXcursor: with it unset, a client computes a size
     * from the X screen, and the Xwayland virtual screen spanning several
     * monitors is thousands of pixels wide — which is how Steam ended up with a
     * pointer several times the size of synui's own. */
    char                  cursor_theme[64];
    int                   cursor_size;

    /* UI font family every compositor-drawn panel renders in (fontpick.c).
     * Empty = "monospace", the fontconfig alias synui always drew with. Held
     * here as well as in text.c because text.c has no server handle — this is
     * the copy the config file and settings.state round-trip through. */
    char                  ui_font[96];

    /* cat.c: start with the kitty already wandering (synuirc `cat = on`).
     * Off by default — Super+Shift+C toggles it at runtime. */
    int   cat_start;
    int   cat_breed;   /* cat_breed_t — which coat the desktop cat wears */

    /* Show the welcome menu on login. The menu's own "Show At Startup" row
     * toggles this and writes welcome.state, which then overrides the synuirc
     * line (delete it to hand control back). Super+Escape opens the menu
     * either way, so turning this off never strands it. Default 1. */
    int   welcome_at_startup;

    /* Do Not Disturb: no toast is drawn and nothing chimes while this is on.
     * Toggled by Super+Shift+N, the control panel and `synctl dnd`, each of
     * which writes dnd.state — which then overrides the synuirc line, the same
     * precedent as every other state file (delete it to hand control back).
     *
     * IN THE CONFIG rather than in syn_notifs_t on purpose. synui_config_reload()
     * does `s->config = fresh`, so anything a reload must not forget has to be
     * re-read by synui_config_load(); a flag kept in the server struct would
     * survive a reload by accident and a flag kept only in the config would be
     * reset by one. See notif_dnd_state_load_config(), and filters.c for the
     * bug this shape exists to avoid.
     *
     * CRITICAL urgency still gets through — see notif_post(). Default 0. */
    int   notif_dnd;

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

    /* Which modifier, tapped alone, opens the start menu — a WLR_MODIFIER_*
     * mask, or 0 for "no tap at all". Default LOGO, which is what `tap_key` in
     * synuirc is named after.
     *
     * A mask rather than a keysym because a tap is the whole KEY, not one of
     * its two halves: Super_L and Super_R are the same shortcut, and storing
     * one of them would leave the other dead on a keyboard that has both.
     * input.c resolves the pressed keysym back with syn_tap_mod_from_sym(). */
    uint32_t tap_mod;

    /* And WHAT that tap does — a bind action and its argument, run through
     * synui_binding_execute() exactly as a chord's would be. `tap_action` in
     * synuirc; default "start_menu", which is what the tap did when it was the
     * only thing it could do.
     *
     * Split from tap_mod because they answer different questions and moved at
     * different times: tap_mod is "which key", and this is "which feature".
     * Making the tap a (mods, action) pair rather than a hard-coded call is
     * what lets it open rofi or the AI command bar — velle asked for exactly
     * that after finding the rebind helper could only move the tap, never
     * change what it opened. */
    char tap_action[SYN_BIND_ACTION_LEN];
    char tap_arg[SYN_BIND_ARG_LEN];

    /* Which QML tree synui-bar starts. A syn_bar_shell_t held as an int, for
     * the control panel's enum row. Read by systemd/synui-bar.sh, never by the
     * compositor — see the enum's comment. */
    int bar_shell;

    /* ── Is there a bar at all? ──────────────────────────────────────────
     *
     * The dock is drawn by the compositor and switches off by not drawing it.
     * The bar is a SEPARATE PROCESS this compositor did not start — the
     * session's `autostart =` line did — so turning it off means killing
     * something, and turning it back on means knowing what to run.
     *
     * Hence a command pair rather than a flag the renderer could honour. Same
     * shape as game_bar_stop_cmd / game_bar_start_cmd below, which solve the
     * identical problem for game mode, and overridable for the same reason:
     * two bars ship (quickshell and waybar) and only the user's autostart line
     * knows which one this desktop actually runs.
     *
     * The default STOP covers both, so "off" always works. The default START
     * is the shipped bar; a waybar desktop wants
     *     bar_start_cmd = synui-waybar
     * in synuirc, or the switch turns the bar off and cannot put it back. */
    /* How each of the three panels you work IN is dismissed — one setting per
     * panel, not one for all of them: a calculator you drag around and a
     * control panel you want gone the moment you look away are different
     * answers to the same question. syn_panel_close_t held as an int, for the
     * control panel's enum rows. */
    int  calc_close;
    int  ctlpanel_close;
    int  taskmgr_close;

    int  bar_enabled;           /* default 1 */
    char bar_stop_cmd[192];
    char bar_start_cmd[192];

    /* Which screen edge the bar sits on. A syn_bar_edge_t held as an int, for
     * the control panel's enum row. Read by quickshell's BarConfig.qml, never
     * by the compositor — see the enum's comment. */
    int bar_edge;

    /* The bar's shape when the corners are on. A syn_bar_shape_t held as an int,
     * for the control panel's enum row; read by BarConfig.qml and never by the
     * compositor — see the enum's comment for why it is gated on the radius
     * rather than being a switch of its own. */
    int bar_shape;

    /* Icon theme for the bar, exported to quickshell as QS_ICON_THEME. Empty
     * (the default) means "follow the system theme", which is what a theme
     * switch changes — so this is only for pinning something the rest of the
     * desktop is not using. The Antiquity shell is the reason it exists:
     * upstream hard-pinned buuf-nestort with a static pragma, which SYNAPSE
     * cannot ship (see quickshell-antiquity/FONTS.md) and which would have
     * overridden the theme anyway. */
    char bar_icon_theme[64];

    /* record.c: does Super+Shift+R capture sound as well? On, the `record`
     * action passes --audio to synui-record, which records the default sink's
     * MONITOR — what you can hear, not the microphone. Off by default and
     * persisted to record.state; see record.c for why this is a setting and
     * not a second keybind. */
    int   record_audio;         /* default 0 */
    /* Record an editable MEZZANINE (DNxHR in a .mov) instead of the H.264 mp4.
     * Free DaVinci Resolve on Linux decodes neither H.264 nor AAC, so the
     * default capture cannot be imported at all; this is the switch that makes
     * Super+Shift+R produce something an editor reads. Costs about 1.1 GB/min
     * against roughly 200 KB for a whole ordinary take, which is why it is off
     * by default and says so in the panel. Persisted to record.state. */
    int   record_edit;          /* default 0 */
#define DOCK_PIN_MAX 16
#define GAME_EXCLUDE_MAX 16
/* game_output: which monitor a detected game is fullscreened onto. The order
 * is the order the control panel cycles them in, so PRIMARY — the answer that
 * makes "games open on the main screen" true — comes first and is the
 * default. (#define, not an enum: this sits inside syn_config_t's body, where
 * an enum declaration draws -Wmissing-declarations for declaring no member.) */
#define GAME_OUT_PRIMARY 0   /* the monitor marked primary (Super+D, `p`) */
#define GAME_OUT_FOCUSED 1   /* wherever the keyboard focus is right now */
#define GAME_OUT_ASK     2   /* honour whatever the client asked for */
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

    /* Offer the fingerprint reader on the native lock screen (lock.c), by way
     * of the synui-lock-fprint helper. Default 1, and safe to leave on: the
     * helper answers "unavailable" in milliseconds on a machine with no reader,
     * no fprintd, or no enrolled prints, and the lock then stops asking and
     * shows nothing. So this switch is not "do I have a reader" — that is
     * detected — it is "I have one and would rather the lock ignored it". */
    int   lock_fingerprint;     /* default 1 */

    /* Laptop lid (syn_lid_action_t). Three settings, chosen between at the
     * moment the lid shuts: docked first, then mains power, then the plain
     * on-battery case. All three default to what the matching systemd setting
     * defaults to (HandleLidSwitch / HandleLidSwitchExternalPower /
     * HandleLidSwitchDocked), so a machine that never opens the panel behaves
     * the way its owner already expects. Persisted to power.state alongside
     * the timeouts. */
    int   lid_close_action;         /* on battery;  default SYN_LID_SUSPEND */
    int   lid_close_ac_action;      /* plugged in;  default SYN_LID_SUSPEND */
    int   lid_close_docked_action;  /* has monitor; default SYN_LID_IGNORE */

    /* Wi-Fi / network configuration UI. nmtui in a terminal by default: synui
     * has no text entry to type a passphrase into, so there is nothing native
     * to point this at yet. Overridable for non-NetworkManager setups. */
    char  network_cmd[192];

    /* "About OS" — the control panel's System ▸ About OS row. areofyl/fetch in
     * a terminal, spinning the SynapseOS mark next to the system info.
     *
     * A command rather than a native panel, and that is the point: everything
     * the row wants to show — kernel, packages, uptime, GPU, theme, wallpaper,
     * cursor — fetch already gathers, and a compositor-drawn About box would be
     * a second implementation of all of it that could disagree with `syn info`
     * about what machine this is. Overridable like network_cmd for anyone who
     * wants fastfetch, neofetch, or a terminal that is not the default. */
    char  about_cmd[192];

    char  power_suspend_cmd[192];

    /* Game mode (game.c). A fullscreen XWayland client is taken to be a game
     * unless its app_id matches game_exclude — that list is what keeps a
     * fullscreen Firefox video from suspending the AI. */
    int   game_mode;            /* master switch, default 1 */
    int   game_suspend_ai;      /* stop synapd while a game runs, default 1 */
    int   game_inhibit_idle;    /* hold off dim/blank/lock, default 1 */
    char  game_exclude[GAME_EXCLUDE_MAX][64];
    int   game_exclude_count;
    /* Wayland-NATIVE clients that are game wrappers, and so count as games
     * despite not being XWayland. "fullscreen XWayland" cannot see these: a
     * gamescope launched from a Wayland session uses its own Wayland backend
     * (its log says `xdg_backend: Initted Wayland backend`) and runs the game
     * on a NESTED Xwayland of its own, which synui never sees. To synui the
     * whole thing is one Wayland toplevel named "gamescope".
     *
     * This is an allow-list rather than "any fullscreen Wayland client"
     * precisely because the XWayland test was never about X: it was a cheap
     * proxy for "not an ordinary desktop app". Wayland-native fullscreen is
     * what a maximised video player, a slideshow and a browser all do, so the
     * proxy does not survive being dropped — it has to be replaced by naming
     * the wrappers. */
    char  game_include[GAME_EXCLUDE_MAX][64];
    int   game_include_count;
    /* Which monitor a detected game is fullscreened onto, regardless of where
     * the client asked to go. See game_output_for(). */
    int   game_output;          /* GAME_OUT_*, default GAME_OUT_PRIMARY */
    char  game_ai_stop_cmd[192];
    char  game_ai_start_cmd[192];

    /* The rest of what a fullscreen game does not need. Measured on a live
     * desktop before any of these were written, because the obvious candidate
     * was the wrong one: synguard costs 5 MB and 0.09% of a core over a day,
     * so suspending the security monitor would have bought nothing and blinded
     * it during exactly the window untrusted game code runs. These are where
     * the resources actually are.
     *
     * game_drop_effects is the big one and the reason this block exists. With
     * the post-process pass on, effects.c renders the scene into an offscreen
     * swapchain and forces WHOLE-OUTPUT damage every frame, then runs a
     * fullscreen shader over it — so a fullscreen game never reaches direct
     * scanout, where its buffer would go to the display untouched. Dropping the
     * pass for the duration costs nothing visible: CRT warp and scanlines are
     * behind an opaque game. */
    int   game_drop_effects;    /* restore direct scanout, default 1 */
    int   game_pause_wallpaper; /* stop linux-wallpaperengine, default 1 */
    /* RAM-only: the bar is ~400 MB but under 0.3% of a core, and restarting it
     * on exit is visible. Off by default for that reason — turn it on if the
     * memory matters more than a second of missing bar when you alt-tab out. */
    int   game_stop_bar;        /* default 0 */
    /* Honest expectation: near-zero. The probes still trap; events_enabled only
     * skips event construction, and a desktop sits around 50 syscalls/sec. It
     * also stops synguard seeing events while it applies, so it is off unless
     * asked for. */
    int   game_quiet_kmod;      /* default 0 */
    char  game_wp_stop_cmd[192];
    char  game_wp_start_cmd[192];
    char  game_bar_stop_cmd[192];
    char  game_bar_start_cmd[192];
    char  game_kmod_quiet_cmd[192];
    char  game_kmod_restore_cmd[192];

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
    /* The subsurface tree *inside* scene_tree — the client's own surfaces and
     * nothing else. xdg popups are added to scene_tree as siblings of it, and
     * wlr_scene_subsurface_tree_set_clip() recurses into every subsurface tree
     * below the node it is given, so clipping scene_tree would crop the menus
     * too. Captured at creation, when the xdg surface tree has exactly one
     * child; NULL for X11 views (no window geometry to clip to). */
    struct wlr_scene_tree       *client_tree;

    int mapped;
    int floating;
    int fullscreen;
    int maximized;
    int minimized;
    int x, y, w, h;

    /* The content size view_resize() last asked the client for, and how many
     * times we have re-asked for *this* size after the client committed
     * something else. See view_heal_size() (synui_main.c): a client that ends up
     * at a size we never configured stays that way, because nothing in synui
     * re-configures a window that is not being moved or re-laid-out. */
    int cfg_w, cfg_h;
    int heal_tries;

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

    /* Glass halo (scenefx): one blur node lowered below the shadow, sized
     * glass_halo px larger than the frame on every side and clipped to exclude
     * the window rect — the ring of blurred desktop around the window (see
     * view_halo_update). Distinct from the per-buffer backdrop blur inside the
     * window, which anim.c owns. NULL while glass_halo is 0. */
    struct wlr_scene_blur *halo;

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

    /* layout.c, LAYOUT_NIRI only. The strip is the workspace window list read
     * in order; these two say how it breaks into columns and how wide they are.
     *
     *   col_join  1 = this window shares the column of the window BEFORE it in
     *             the list, stacked under it. 0 = it opens a new column. So a
     *             column is a run of list entries, which means Super+Shift+J/K
     *             (layout_move_in_stack) moves windows along the strip and
     *             between columns for free, with no second ordering to keep in
     *             sync. The first window of an output's strip is always a
     *             leader whatever this says.
     *   col_frac  the column's width as a fraction of the usable width. Held on
     *             EVERY member of the column, not just the leader, so the width
     *             survives the leader being moved or closed. 0 = never set, use
     *             NIRI_COL_FRAC.
     *
     * Both are ignored, and left alone, by every other layout. */
    int    col_join;
    float  col_frac;

    /* layout.c, LAYOUT_FLOATING only. "The user has placed this one himself."
     *
     * The floating desktop arranges its windows into an inset grid
     * (layout_float_arrange), which is only welcome for windows nobody has an
     * opinion about yet. The moment a window is dragged or resized by hand it
     * stops being the tiler's business — otherwise the next window to open
     * would yank it back into a cell and the arrangement would be fighting the
     * user rather than helping. Set at the one choke point every hand grab
     * passes through (grab_release_constraints, so a titlebar drag, a border
     * pull, Super+drag and a CSD client's own xdg_toplevel.move all count) and
     * cleared wholesale by the `float_arrange` action and layout_reclaim,
     * which are the two ways of saying "forget that, do it again".
     *
     * Ignored, and left alone, by every other layout. */
    int    hand_placed;

    /* anim.c: the window's current opacity (1 = solid) and the fade in flight.
     * alpha is applied to every buffer under the frame *and* multiplied into
     * the border rects' colour, so a fading window fades whole. */
    float  alpha;
    int    fade_active;
    int    fade_hide_done;   /* disable the node once it reaches alpha 0 */
    float  fade_from, fade_to;
    double fade_start;       /* CLOCK_MONOTONIC secs */
    /* Per-RUN, not per-config: a window opening while the desk is still
     * sliding must keep the length and curve it started with, or the two
     * finish at times neither of them asked for. */
    double fade_dur;         /* seconds; always > 0 while fade_active */
    int    fade_curve;       /* syn_anim_curve_t this run decays on   */

    /* anim.c: where the frame is drawn RELATIVE to its logical x/y, while an
     * animation is displacing it — the rise of an opening window, the slide of
     * a desktop leaving. The logical geometry never moves: layout, focus,
     * hit-testing and geom_persist all keep reading view->x/y, and only
     * view_place_node() adds this. It is the only reason a slide costs no
     * client round trips (see view_move's header).
     *
     * Interpolated on the same clock as the fade above, so one window is only
     * ever running one animation. */
    int    anim_dx, anim_dy;
    int    anim_dx_from, anim_dx_to;
    int    anim_dy_from, anim_dy_to;

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
    /* The X server telling us a window MOVED. Only an override-redirect window
     * (a menu) acts on it — it places itself, and it may correct that placement
     * after mapping. See xw_set_geometry(). */
    struct wl_listener set_geometry;
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

    /* dispcfg.c: 10-bit (deep colour) scanout — the colour-depth column in
     * the display panel. `deep_color` is what the user asked for and what
     * outputs.conf stores; `deep_color_ok` is whether the backend actually
     * accepted it, so the panel can say "unsupported" instead of silently
     * doing nothing. `deep_color_capable` is whether the backend accepts a
     * 10-bit framebuffer at all, probed once at output creation.
     *
     * This is deliberately NOT HDR, and used to be labelled as if it were.
     * Real HDR needs the compositor to composite in a PQ/scRGB space and
     * tone-map SDR clients into it; scenefx renders 8-bit sRGB through GLES2
     * and cannot. What is here is the part the stack can honestly deliver: a
     * 10-bit framebuffer, which removes gradient banding and is a
     * prerequisite for HDR later. */
    int                      deep_color;
    int                      deep_color_ok;
    int                      deep_color_capable;

    /* dispcfg_probe_edid(): what the *monitor* says about HDR, read from its
     * EDID (CTA-861 HDR static metadata + colorimetry blocks) rather than
     * inferred from a framebuffer format. Purely informational — synui does
     * not drive the connector into PQ/BT.2020 — but the display panel has to
     * be able to tell a genuine HDR10 monitor from a merely 10-bit-capable
     * one, which the framebuffer test cannot do.
     *
     * Zeroed on non-DRM backends and on monitors with no CTA extension. */
    int                      hdr_pq;         /* SMPTE ST 2084 (HDR10) */
    int                      hdr_hlg;        /* Rec. BT.2100 HLG */
    int                      wide_gamut;     /* BT.2020 RGB/YCC colorimetry */
    float                    hdr_max_nits;   /* desired content peak, 0 unset */

    struct wl_list           layer_surfaces;  /* syn_layer_surface_t::link */
    struct wlr_box           usable_area;     /* full box minus exclusive zones */

    /* layout.c, LAYOUT_NIRI: how far this monitor's strip is scrolled, in
     * strip pixels, for each desktop. Per (output, desktop) because the strip
     * is: a desktop spans every monitor and each monitor holds its own run of
     * columns, so one shared offset would drag the other screens about
     * whenever you moved along this one.
     *
     * Session state, not a setting — it is derived from where the focus is,
     * and layout_niri() re-clamps it against the real strip on every reflow,
     * so a stale value can only ever cost one frame. calloc'd to 0 with the
     * output, which is "showing the left-hand end". */
    int                      strip_scroll[WORKSPACE_MAX];

    /* The niri slide (layout_scroll_tick). strip_scroll above is where the
     * strip is drawn RIGHT NOW; these three are the glide that is carrying it
     * somewhere else.
     *
     *   strip_target  where layout_niri decided the strip belongs. Reflows
     *                 compare against this rather than strip_scroll, or a
     *                 reflow mid-slide would re-derive the target from the
     *                 half-way position and the strip would creep.
     *   strip_from    strip_scroll when the slide started, so the easing has a
     *                 fixed origin — reading it live would ease the remaining
     *                 distance every frame and never actually arrive.
     *   strip_t0      CLOCK_MONOTONIC seconds at the start of the slide.
     *
     * Session state like strip_scroll, and worth exactly as much: a slide that
     * is interrupted is simply restarted from wherever the strip had got to.
     * animation_ms == 0 skips the whole mechanism (strip_scroll = target). */
    int                      strip_target[WORKSPACE_MAX];
    int                      strip_from[WORKSPACE_MAX];
    double                   strip_t0[WORKSPACE_MAX];
    int                      strip_sliding[WORKSPACE_MAX];

    /* effects.c: offscreen swapchain the scene renders into when the GLES
     * post-process pass is active (NULL until first effects frame). */
    struct wlr_swapchain    *fx_swapchain;

    /* nightlight.c: the colour temperature this output's committed colour
     * transform was built for; 0 is identity, which is also where a freshly
     * created output starts, so the zero value is already true. The transform
     * is a CRTC LUT the kernel keeps until it is replaced, so it is committed
     * on CHANGE rather than every frame: re-uploading a 1024-entry blob at the
     * refresh rate is real work for a value that almost never moves, while
     * *never* committing again would leave the screen warm after night light
     * was switched off. */
    int                      nightlight_temp;

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

/* Non-keyboard input device (pointer/touch/tablet/switch), tracked so a SIGHUP
 * config reload can reapply libinput options to it. */
typedef struct syn_input_dev {
    struct wl_list           link;
    struct syn_server       *server;   /* switch events need it; see toggle */
    struct wlr_input_device *dev;
    struct wl_listener       destroy;
    /* Switch devices only (the laptop lid). Its list link is initialised for
     * every device so destroy can remove it without knowing the type. */
    struct wl_listener       toggle;
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

        /* Decoded images for the per-output overrides, keyed by path. Two
         * monitors pointed at the same file share one entry — the cache is
         * keyed by path and not by output for exactly that reason, since the
         * common "same picture, different scaling" case would otherwise decode
         * (and hold) the image twice. Rebuilt wholesale by wallpaper_reload. */
        struct {
            char             path[256];
            cairo_surface_t *surf;
        } per[SYN_WP_PEROUT_MAX];
        int per_n;
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

        /* Fingerprint (synui-lock-fprint), running ALONGSIDE the password —
         * not instead of it. The helper sits blocked waiting for a finger for
         * as long as the screen is locked, so it is started at lock time and
         * killed at unlock, while the password path forks per attempt. Both can
         * be in flight at once; whichever answers first wins.
         *
         * fp_state is the whole retry policy. SYN_FP_UNAVAIL is terminal for
         * this lock: it means the machine cannot do this at all, and re-forking
         * against that answers the same way forever. */
        enum {
            SYN_FP_IDLE = 0,    /* not running; may be started */
            SYN_FP_RUNNING,     /* helper is waiting for a finger */
            SYN_FP_UNAVAIL,     /* no reader/fprintd/enrolled prints — give up */
        } fp_state;
        pid_t                    fp_pid;
        int                      fp_fd;     /* status-pipe read end, -1 when idle */
        struct wl_event_source  *fp_src;
        int                      fp_fails;  /* consecutive F verdicts this lock */
        uint32_t                 fp_retry_ms;  /* earliest restart, monotonic ms */
        char                     fp_msg[128];  /* last M line, drawn under the clock */
        /* The helper writes whole lines, but a pipe read can still split one:
         * partial bytes park here until the newline arrives. */
        char                     fp_rx[256];
        int                      fp_rxlen;

        struct {
            struct wlr_output       *output;
            struct wlr_scene_buffer *buf;
            /* The wallpaper behind the panel, sized to this output and already
             * dimmed and blurred. Built once per lock, not per frame: the blur
             * is the expensive part and the picture does not move. NULL when
             * the background is plain black, which is also the fallback
             * whenever a decode or an allocation fails. */
            struct wlr_scene_buffer *bg;
        } pane[8];              /* one clock panel centred on each output */
        int      npane;

        /* MEASURED relative luminance of the background, under the area the
         * clock panel covers, after the blur and the dim. The ink ladder in
         * lock.c runs from this.
         *
         * Measured rather than assumed: the first version estimated it from
         * lock_bg_dim alone ("a middling wallpaper is 0.35"), and on the cream
         * engravings in data/wallpapers that guess came out low, so the date
         * row was picked for a darker surface than it was actually drawn on.
         * The background is built once per lock and the panel is a fixed rect,
         * so the true number costs one pass over a few hundred kB — at lock
         * time, not per frame. 0 (black) until a background is built, which is
         * also the right answer when there isn't one. */
        double   bg_lum;
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
    struct wl_event_source                 *pointer_rebase_idle; /* coalesced pointer_rebase() */
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

    /* deskmenu.c / render.c: right-click menu on the wallpaper, and the
     * optional ~/Desktop icons. Same shape as dockmenu above. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_buffer *text_buf;
    } deskmenu_ui;
    struct {
        int  visible;
        syn_deskact_t actions[SYN_DESKMENU_MAX];
        int  action_count;
        int  selected;                    /* hovered item, -1 = none */
        int  x, y, w, h;                  /* menu rect, layout coords */
    } deskmenu;

    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_buffer *buf;
        /* The icon under a live drag is lifted out of the desktop buffer into
         * one of its own, so following the cursor is a node move rather than a
         * full-desktop repaint. See synui_render_deskicons. */
        struct wlr_scene_tree   *drag_tree;
        struct wlr_scene_buffer *drag_buf;
    } deskicons_ui;
    syn_deskicon_t deskicons[SYN_DESKICON_MAX];
    int            deskicon_count;
    int            deskicon_selected;     /* -1 = none */
    /* Double-click tracking for launching an icon, same 400ms window as the
     * titlebar's (input.c) so the desktop does not feel different. */
    uint32_t       deskicon_last_click_ms;
    int            deskicon_last_click_idx;
    /* deskmenu.c: drag an icon to a new cell. Same armed-then-moved shape as
     * dock_drag above — a press arms it, crossing the slop makes it a real
     * drag, and the drop snaps to a grid cell and writes deskicons.state. */
    struct {
        int    active;
        int    moved;              /* passed the slop → really dragging */
        int    idx;                /* icon being dragged, -1 when idle */
        double start_x, start_y;   /* press point, layout coords */
        int    orig_x, orig_y;     /* the icon's cell origin at press */
    } deskicon_drag;

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

    /* The switcher overlay Alt+Tab draws while Alt is held: a grid of live
     * window thumbnails with the one you would land on highlighted.
     *
     * `thumb` is one scene buffer per tile, each showing the *client's* current
     * buffer scaled down — the window itself, not a picture of it, so a video
     * playing in a tile is the frame that was on screen when the tile was
     * built. A scene buffer takes its own lock on what it is given, which is why
     * the tiles survive a window closing mid-cycle, and why alttab_hide() has to
     * set them back to NULL: a client buffer held past the overlay is one the
     * client cannot reuse.
     *
     * Deliberately holds no syn_view_t pointers. The candidate list is passed in
     * per render and not kept, so the "no snapshot to dangle" property the cycle
     * itself has (see the alttab struct above) survives the overlay. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
        /* Own tree so the thumbnails stay above the cairo layer whatever order
         * the two get (re)built in. */
        struct wlr_scene_tree   *thumb_tree;
        struct wlr_scene_buffer *thumb[SYN_ALTTAB_TILES];
    } alttab_ui;

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

    /* focus_mode's delay timer (input.c). Deliberately holds no view pointer:
     * it re-queries what is under the cursor when it fires, so a window that
     * unmapped while the timer was pending cannot be a dangling pointer here.
     * Re-armed on every motion, so it expires once the pointer has been STILL
     * for focus_delay_ms, which is the behaviour the setting describes. */
    struct wl_event_source *focus_follow_timer;

    /* The output the compositor's own UI is drawn on — see
     * server_ui_output_track(). Re-derived at every click and keystroke while
     * nothing is up, and held from there for as long as a panel is open, which
     * is what stops a repainting panel from following the pointer onto the
     * other monitor. NULL means "nothing pinned yet, ask the cursor", and the
     * output-destroy handler puts it back to NULL so this cannot dangle. */
    syn_output_t     *ui_output;

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

    /* input.c: a titlebar press *arms* a move grab rather than committing to
     * it. Un-maximizing (and un-snapping, and un-tiling) is deferred until the
     * pointer has actually travelled — otherwise merely clicking a maximized
     * window's titlebar to focus it dropped it straight back to a floating
     * window. grab_press_* is where the button went down. */
    bool              grab_armed;
    double            grab_press_x, grab_press_y;

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

    /* Screensaver settings panel (Super+Z). The saver's own full-screen
     * drawing does NOT live here — it builds its tree on show and destroys it
     * on dismiss, the way the lock does, because a saver that is off should
     * cost no scene nodes at all. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } saver_ui;

    syn_saver_t     saver;

    syn_game_t      game;

    /* CRT filter panel (Super+E) — sliders for the effects.c strengths. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } filters_ui;

    syn_filters_t   filters;

    /* AI model picker (control panel ▸ System ▸ AI model). */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } aimodel_ui;

    syn_aimodel_t   aimodel;

    /* Desktop widget manager (Super+Shift+A) — one row per quickshell widget. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } widgets_ui;

    syn_widgets_t   widgets;

    /* Event sounds (Super+S) — the panel, plus the udev monitor that turns a
     * device appearing into SOUND_EVT_DEVICE_ADDED. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } sound_ui;

    syn_sound_t     sound;
    struct udev            *udev;
    struct udev_monitor    *udev_mon;
    struct wl_event_source *udev_src;

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

    /* Shortcut palette (Super+/ and Super+?) — the bind table, filtered as you
     * type. Its own tree so it can be raised over the control panel that may
     * have opened it. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } keys_ui;

    syn_keys_t      keys;

    /* Theme manager (Super+T) — its own scene subtree, like ctlpanel. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } thememgr_ui;

    syn_thememgr_t  thememgr;

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

    /* The toast Super+Tab posts, kept so the next press replaces it instead of
     * stacking a card per press. 0 until the first one. */
    uint32_t        layout_notif_id;

    /* Clipboard history (Super+V) — see syn_clipboard_t. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } clip_ui;

    syn_clipboard_t  clipboard;
    struct wl_listener clipboard_set_selection;

    /* Mission control (overview.c). Two trees for the same reason the switcher
     * has two: the thumbnails are scene buffers and the frame around them is a
     * cairo layer, and the buffers have to stay above it whichever order the
     * two get rebuilt in. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;      /* the dim over the whole output */
        struct wlr_scene_buffer *text_buf;
        struct wlr_scene_tree   *thumb_tree;
        struct wlr_scene_buffer *thumb[OVERVIEW_MAX];
    } overview_ui;

    syn_overview_t   overview;

    /* The modifier tap: config.tap_mod pressed and released with nothing in
     * between opens the start menu, the way a tapped Super does on every other
     * desktop. Armed on that press and disarmed by *any* intervening key or
     * pointer button, so the modifier used as a modifier (Super+E, Super+drag)
     * never opens the menu on release. Without that disarm the modifier and the
     * tap are indistinguishable.
     *
     * Not named super_armed any more because the key is `tap_key` and Super is
     * only its default — the palette can move the tap onto Alt, or switch it
     * off, and a field named after one modifier would be read as meaning that
     * modifier by the next person to touch this. */
    int             tap_armed;

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

        /* Steam Workshop wallpapers (Wallpaper Engine), listed after the
         * images. These are handed to linux-wallpaperengine rather than
         * decoded here, so the row only needs the id to pass along and the
         * title to show. `type` is scene/video/web, shown so it is obvious
         * which are the animated ones.
         *
         * Not every Workshop subscription is a wallpaper: property presets
         * (which only re-configure some OTHER wallpaper) and editor asset packs
         * subscribe into the same tree and have no top-level "type" at all, and
         * their own id makes the engine answer "Project type missing" and draw
         * nothing. A preset is still reachable — synui-wpengine resolves it to
         * the wallpaper its "dependency" names plus a --set-property per saved
         * value — so it counts as renderable whenever that base wallpaper is
         * also subscribed. Asset packs, and presets whose base is missing, are
         * not: `renderable` marks them and `type` says so on the row, rather
         * than leaving rows that look like wallpapers and silently do nothing. */
        struct {
            char id[24];
            char title[96];
            char type[32];
            bool renderable;
            /* Full path to the preview image project.json names, or empty.
             * Resolved at scan time because the Workshop root and the id are
             * both in hand there and neither is kept afterwards. */
            char preview[320];
        } we[WPPICK_WE_MAX];
        int  we_count;

        /* Applying a Workshop wallpaper restarts a GPU process, so unlike the
         * images it must NOT fire on every arrow keypress. Rows past the
         * images defer to Enter, and this is the row Enter should commit.
         *
         * A row that is NOT a Workshop entry defers too, but only while the
         * scope already has a Workshop wallpaper running: applying one there
         * means stopping the engine, and it cannot be restarted cheaply (see
         * wpengine_restore_soon), so an arrow key must never trigger it. */
        int  pending_we;    /* row index, or -1 when nothing is deferred */

        /* A live preview is on screen that has not been committed. Any close
         * other than Enter puts `saved` back, so arrowing through the list and
         * changing your mind leaves the wallpaper exactly as you found it. */
        bool previewed;

        /* The wallpaper config as it stood when the panel opened. Preview no
         * longer persists anything, so the on-disk state still matches this —
         * restoring is purely an in-memory revert plus a repaint.
         *
         * The per-output table is here because a pick under "all monitors"
         * calls wallpaper_output_clear(): without a copy, arrowing past one
         * row would silently discard every per-monitor wallpaper. */
        struct {
            char                  wallpaper[256];
            syn_wallpaper_mode_t  mode;
            syn_wallpaper_src_t   src;
            syn_wp_output_t       out[SYN_WP_PEROUT_MAX];
            int                   out_n;
        } saved;

        /* Which monitor a pick applies to, cycled with Tab. -1 = all of them
         * (clears the per-monitor overrides and sets the global wallpaper);
         * otherwise an index into out[], the connector names snapshotted when
         * the panel opened. Snapshotted rather than walked live because the
         * scope has to survive a monitor being unplugged mid-pick without the
         * index silently coming to mean a different screen. */
        char out[SYN_WP_PEROUT_MAX][32];
        int  out_count;
        int  scope;

        /* Pointer geometry, written by synui_render_wppick(). Covers the LIST
         * only, not the preview pane beside it. */
        syn_hit_t hit;
        /* The row and time of the last left click, for the double-click that
         * commits a pick — same 400ms window the titlebar and the desktop icons
         * use, because a desktop with three different double-click speeds is
         * three bugs waiting to be reported. */
        int      last_click_row;
        uint32_t last_click_ms;
    } wppick;

    /* Re-arming linux-wallpaperengine after a suspend or a monitor change —
     * see wpengine_restore_soon() in wppick.c. The timer coalesces a burst of
     * triggers into one engine restart; lost_output gates the new-output
     * trigger so it cannot fire during startup, when synuirc's autostart line
     * is already running `synui-wpengine restore`. */
    struct {
        struct wl_event_source *timer;
        int lost_output;   /* an output was destroyed earlier this session */
    } wpengine;

    /* Cursor theme picker (cursor.c) — Super+C. Same modal shape as wppick. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } curpick_ui;
    struct {
        int visible;
        int selected;
        int scroll;

        struct syn_cursor_theme themes[CURPICK_MAX];
        int count;

        /* What was active when the panel opened, so Esc can undo the live
         * preview — arrowing onto an unreadable cursor is otherwise a trap. */
        char restore_theme[64];

        /* Pointer geometry + double-click state, exactly as in wppick. */
        syn_hit_t hit;
        int       last_click_row;
        uint32_t  last_click_ms;
    } curpick;

    /* Emoji picker (emoji.c) — Super+;. A GRID rather than a list, so it is the
     * one panel using hit_set_grid(); otherwise the same modal shape. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } emoji_ui;
    syn_emoji_panel_t emoji;

    /* Equalizer panel (eq.c) — Control panel ▸ Sound ▸ Equalizer. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } eq_ui;
    syn_eq_panel_t eq;

    /* Image cropper (crop.c). Full-screen while cropping, so there the whole
     * output is the target and the mapping is crop_fit() rather than a hit
     * rect; the recent-images list it opens on is an ordinary centred panel
     * with rows, and that one does use s->crop.hit. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_buffer *text_buf;
    } crop_ui;
    syn_crop_panel_t crop;

    /* UI font picker (fontpick.c). Same modal shape as curpick above. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } fontpick_ui;
    struct {
        int visible;
        int selected;
        int scroll;

        struct syn_font_family fonts[FONTPICK_MAX];
        int count;

        /* What was active when the panel opened, so Esc can undo the live
         * preview. Load-bearing here in a way it is not for the cursor: the
         * preview redraws the PANEL ITSELF in the candidate font, so a family
         * with no digits leaves every panel on the desktop unreadable. */
        char restore_font[96];

        /* Pointer geometry + double-click state, exactly as in wppick. */
        syn_hit_t hit;
        int       last_click_row;
        uint32_t  last_click_ms;
    } fontpick;

    /* Calculator (calc.c) — Super+X. A grid like the emoji picker, but the
     * grid is the pointer's half only; the keyboard types an expression. */
    struct {
        struct wlr_scene_tree   *tree;
        struct wlr_scene_rect   *bg;
        struct wlr_scene_rect   *accent;
        struct wlr_scene_buffer *text_buf;
    } calc_ui;
    syn_calc_panel_t calc;

    /* A windowed panel being dragged (panel.c). Same shape as dock_drag. */
    struct {
        int         active;
        syn_pdrag_t which;
        double      grab_lx, grab_ly;   /* pointer where the drag started */
        int         base_dx, base_dy;   /* the panel's offset at that moment */
    } panel_drag;

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

    /* Set when SYNUI_RUNNING was ALREADY in the environment at startup, i.e.
     * this synui is nested inside another one (the test rig, a headless run).
     * Recorded before synui_init() sets that variable for our own children.
     *
     * It gates anything that reaches OUTSIDE this compositor into shared
     * session state: a nested synui shares the session D-Bus with the live
     * desktop, so pushing our socket name into the activation environment
     * would repoint the real desktop's services at the nested display. */
    int             nested;

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
    /* Cleared when the X server exits. Re-armed on every ready, because lazy
     * Xwayland is destroyed and recreated rather than reused. */
    struct wl_listener xwayland_server_destroy;
    int xwayland_up;    /* the X server is actually running (ready has fired),
                         * not merely socket-listening — see the lazy-start
                         * deadlock note in xwayland_apply_primary().
                         * MUST be cleared when Xwayland dies: a stale socket in
                         * /tmp/.X11-unix accepts a connection and then never
                         * answers, so believing a dead X server is still up is
                         * what hangs a worker forever. */
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

    /* security-context-v1: lets a sandbox (Flatpak et al) tag its client so
     * the global filter in synui_main.c can withhold screen capture, input
     * interception and clipboard snooping from it. NULL means the manager
     * failed to create, in which case the filter passes everything through —
     * see security_context_filter() for the limits of what this protects. */
    struct wlr_security_context_manager_v1 *security_context_mgr;

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

/*
 * The output the compositor draws its OWN UI on — panels, pickers, toasts.
 *
 * Not the same question as server_focused_output(), and that is the whole
 * point. The focused output is re-derived from the cursor every time it is
 * asked, which is right for "where should this new window go" and wrong for
 * "where is the panel I am already reading": a panel repaints (the task
 * manager ticks, a hover redraws a row) and the answer moved with the pointer,
 * so the panel jumped monitors under the user's hands.
 *
 * So the UI output is a PIN. server_ui_output_track() re-takes it at every
 * click and keystroke — but only while nothing is open, so an open panel keeps
 * its monitor until it is closed. `panel_follow_pointer = on` gives the old
 * chase back.
 */
syn_output_t *server_ui_output(syn_server_t *s);
void server_ui_output_track(syn_server_t *s);

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
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  dispcfg_motion(syn_server_t *s, double lx, double ly);
int  dispcfg_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  dispcfg_scroll(syn_server_t *s, double lx, double ly, double delta);
/* 10-bit scanout. _set returns whether the backend accepted it; _probe fills
 * deep_color_capable. See the syn_output_t fields for why this is not HDR. */
int  dispcfg_set_deep_color(syn_server_t *s, syn_output_t *o, int enable);
void dispcfg_probe_deep_color(syn_server_t *s, syn_output_t *o);
/* Read this connector's EDID and fill the hdr_* / wide_gamut fields — what the
 * monitor advertises, which is a different question from what the framebuffer
 * can carry. Safe to call on any backend; a no-op when there is no EDID. */
void dispcfg_probe_edid(syn_server_t *s, syn_output_t *o);
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

/* Rebuild the lock/greeter background from the current settings. Called when
 * the lock engages, and again by the Super+Z panel whenever a background row
 * changes so the effect can be seen on the next lock without a restart. Cheap
 * and safe to call while unlocked: it does nothing when there is no lock up. */
void lock_bg_invalidate(syn_server_t *s);
void lock_output_destroy(syn_output_t *o);       /* drop a dying output's lock pane (output_destroy) */
void lock_output_create(syn_output_t *o);        /* pane for an output arriving mid-lock (server_new_output) */

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
/* Re-derive pointer focus after a scene change with a stationary cursor (a
 * surface mapping or unmapping). Without it a client gets no wl_pointer.enter
 * until the mouse physically moves. Safe to call from map/unmap handlers. */
void pointer_rebase(syn_server_t *s);
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
/* Crop the client's surfaces to its xdg window geometry, hiding a CSD shadow
 * margin (Firefox's). Called by view_update_decorations; see clip_csd_margin. */
void view_clip_csd_margin(syn_view_t *view);
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
void view_halo_update(syn_view_t *view);
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
void layout_niri(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o);
void layout_spiral(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o);
void layout_cascade(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o);
/* How many windows one cascade pile may hold before the arrangement starts a
 * second pile beside it. See layout_cascade() for why this is a count rather
 * than something derived from the geometry. */
#define CASCADE_STACK_MAX_DEF  5
#define CASCADE_STACK_MIN      2
#define CASCADE_STACK_MAX      12
/* The smallest useful diagonal offset between two cards in a pile, for a
 * desktop whose titlebars are off — without it the step would be the border
 * width and the pile would be a single window with a two-pixel fringe. */
#define CASCADE_STEP_MIN       24
/* How big one card is allowed to get, as a percentage of the working box. This
 * is what makes cascade a deck of cards rather than a tiler with an offset: a
 * pile is capped at a third of the width and half the height, so six windows
 * come out as six small cards across the whole screen instead of two
 * half-screen slabs with the right third of the desktop empty. The two numbers
 * are also the grid: 33% wide is three pile columns, 50% tall is two rows. */
#define CASCADE_CARD_W_PCT     33
#define CASCADE_CARD_H_PCT     50
/* The floating desktop's own tiler: an inset grid that deliberately leaves the
 * wallpaper showing (float_inset / float_gap). Skips any window the user has
 * placed by hand (view->hand_placed), so a drag is permanent. */
void layout_float_arrange(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o);
/* "Forget who I moved, lay the whole desktop out again" — clears hand_placed
 * across ws and reflows. Bound to Super+Shift+G; returns how many windows it
 * freed. (layout_reclaim clears the same flag on the windows IT takes back, but
 * only those — this is the unconditional version.) */
int layout_float_release_all(syn_server_t *s, syn_workspace_t *ws);
/* Advance the niri strip slide for every output showing the active desktop.
 * Returns true while any strip is still moving, so output_frame keeps pumping
 * frames — same contract as anim_tick/dock_tick. */
bool layout_scroll_tick(syn_server_t *s, double now);
/* Move a window's frame WITHOUT re-sizing it, and without configuring the
 * client. The scroll slide and interactive drags both need this: a client is
 * never told its own position (xdg cannot be, and X11 gets one configure when
 * the movement settles), so a position-only animation costs no round trips. */
void view_move(syn_view_t *view, int x, int y);
/* Put the frame node where the view's logical geometry says it goes, PLUS
 * whatever anim_dx/anim_dy is currently displacing it by. The single place the
 * node's position is computed, so an animation's offset cannot be dropped by
 * the next reflow: view_resize, view_move and anim_tick all end here. */
void view_place_node(syn_view_t *view);
/* Move the focused window between columns on a niri desktop: join = 1 pulls it
 * into the column on its left (niri's "consume"), join = 0 pushes it back out
 * into a column of its own ("expel"). Bound to Super+, and Super+. — a no-op,
 * with no reflow, on every other layout. */
void layout_column_join(syn_server_t *s, syn_view_t *view, int join);
/* Both map paths call this, BEFORE they focus the new view: on a niri desktop
 * it moves the freshly-inserted window from the head of the workspace list (the
 * tiling master slot, and the far left of the strip) to just right of the
 * focused column. No-op on every other layout. */
void layout_strip_insert(syn_server_t *s, syn_view_t *view);
/* A layout's name for a HUMAN ("AI", not "ai"): the Super+Tab toast, the
 * control panel's Layout row, the AI overlay. Deliberately NOT ipc.c's
 * layout_name(), which is the wire value synctl and the tests parse. */
const char *layout_label(syn_layout_t l);
/* Each desktop's chosen layout, remembered across restarts in
 * ~/.config/synui/layouts.state. _save on every change to ws->layout; _load
 * once at startup, over the LAYOUT_TILING the workspaces are seeded with. */
void layout_state_save(syn_server_t *s);
void layout_state_load(syn_server_t *s);
void view_resize(syn_view_t *view, int x, int y, int w, int h);
void layout_float_place(syn_server_t *s, syn_view_t *view);

/* Smallest interactive window size, px. Shared with geom_persist.c so the size
 * that gets recorded and the size that gets placed obey one floor. */
#define MIN_WIN 40

/* Put a window back where its app last left it. Called from both map paths
 * (and from layout_float_place, where the remembered box beats the centred
 * default). A window opening on a tiling or AI desktop skips the table
 * entirely — those layouts place their own windows. Returns false if the app
 * has nothing saved, or if the layout owns the placement. */
bool layout_restore_geometry(syn_server_t *s, syn_view_t *view);
/* Hand every window on ws back to the layout (un-maximize, un-snap, un-float);
 * returns how many were taken back. Dialogs and fullscreen windows are left
 * alone. Called when a layout that places windows is SELECTED, and by the
 * `retile` action — never from layout_apply, or Super+F would be undone by the
 * reflow it triggers. */
int layout_reclaim(syn_server_t *s, syn_workspace_t *ws);

/* geom_persist.c: per-app window geometry, remembered across restarts.
 * _save is called when a window unmaps; _lookup feeds layout_restore_geometry,
 * which does the clamping onto a currently-connected output. */
void geom_persist_save(syn_view_t *view);
bool geom_persist_lookup(syn_view_t *view, struct wlr_box *box, int *maximized,
                         int *floating);
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
/* The pointer contract's click-off half, and only that half: the bar is a
 * prompt with nothing in it to point at, so there is no _motion and no
 * _scroll. Returns 1 if the bar was open and took the click. */
int  cmdbar_click(syn_server_t *s, double lx, double ly);
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
/* Recompute the poll from every panel that wants it — the overlay, the model
 * picker and the control panel. Call this instead of synmon_set_active() from
 * anything that shows or hides one of them: with three owners, "turn it off
 * unless the other one is up" is no longer a rule that can be written locally. */
void synmon_want_refresh(syn_server_t *s);

/* ── config.c ────────────────────────────────────────────── */
void synui_config_load(syn_config_t *cfg);

/* ── Binds, as data ──────────────────────────────────────────
 *
 * The four calls the rebind helper (keys.c) needs, and the reason they are here
 * rather than static in config.c: a shortcut the user moves has to be written
 * out in synuirc's language and read back by synuirc's parser, so the formatter
 * and the parser must be the same pair the config file goes through. Two
 * spellings of a chord is a shortcut that works all session and is gone at the
 * next login — see syn_bind_format_combo's comment for how the two differ from
 * the ones the PANEL draws.
 */
bool syn_bind_parse_combo(const char *combo, uint32_t *mods, xkb_keysym_t *sym);
void syn_bind_format_combo(uint32_t mods, xkb_keysym_t sym, char *out, size_t n);
/* The same pair for `tap_key`, which is a modifier and not a combo: a keysym
 * back to the modifier it belongs to (0 if it is not one), and a modifier to
 * the word synuirc spells it with ("none" for 0, which is a setting and not an
 * error). See the tap-key block in config.c. */
uint32_t    syn_tap_mod_from_sym(xkb_keysym_t sym);
const char *syn_tap_mod_name(uint32_t mod);
void config_bind_set(syn_config_t *cfg, uint32_t mods, xkb_keysym_t sym,
                     const char *action, const char *arg);
bool config_unbind_combo(syn_config_t *cfg, uint32_t mods, xkb_keysym_t sym);
bool config_unbind(syn_config_t *cfg, const char *combo);

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

/* cairo_rounded_rect() — the cairo half of the corner radius, for the panels
 * that stroke or fill their own frame in the overlay buffer rather than leaving
 * it to a scene rect. Declared in its own header because the implementation is
 * pure cairo and links without a compositor; see cairo_shapes.c for why that
 * matters. */
#include "cairo_shapes.h"
void set_scene_buffer(struct wlr_scene_buffer **node,
                       struct wlr_scene_tree *parent, struct wlr_buffer *buf);
void cairo_begin(cairo_t *cr);   /* clear to transparent + set default font */

/* ── text.c ──────────────────────────────────────────────────
 *
 * Text with per-glyph font fallback. Nothing on a draw path should call
 * cairo_show_text() or cairo_text_extents() directly: the toy font API resolves
 * to ONE face with no fallback, so a character that face lacks draws nothing at
 * all. See the header of text.c for how a face gets chosen and when a colour
 * font is asked for.
 */

/* Copy a UTF-8 string into a fixed buffer, dropping invalid bytes and never
 * truncating mid-character. Anything drawn from data we did not write (a Steam
 * Workshop title, a window title, a filename) must go through this. */
void syn_utf8_copy(char *dst, size_t n, const char *src);

/* cairo_show_text() that cannot silently blank the rest of a panel, and that
 * falls back to a font which can draw what the active face cannot. */
void syn_show_text(cairo_t *cr, const char *text);

/* cairo_text_extents() measured through the SAME fallback syn_show_text draws
 * with. Anything that centres, elides or right-aligns must use this, or it
 * measures a string that is not the one being painted. Only x_advance is
 * fallback-aware; see the definition. */
void syn_text_extents(cairo_t *cr, const char *text, cairo_text_extents_t *ext);

/* The family every compositor-drawn panel renders in — what the font picker
 * sets. Defaults to "monospace", the fontconfig alias synui always drew with. */
void        syn_text_set_ui_font(const char *family);
const char *syn_text_ui_font(void);

/* Drop the cached fallback faces. Called from synui_destroy(); safe any time. */
void syn_text_shutdown(void);

/* ── wallpaper.c ─────────────────────────────────────────── */
void wallpaper_init(syn_server_t *s);             /* create wallpaper_tree, decode initial config */
void wallpaper_output_created(syn_output_t *o);   /* paint this output (server_new_output) */
void wallpaper_output_destroy(syn_output_t *o);   /* destroy this output's buffer (output_destroy) */
void wallpaper_relayout(syn_server_t *s);         /* repaint all outputs (output_layout_changed) */
void wallpaper_reload(syn_server_t *s);           /* re-decode + repaint from current config */

/* ── imgdec.c ────────────────────────────────────────────── */

/* JPEG -> cairo surface, shared by wallpaper.c and wpthumb.c. The PNG decoders
 * in the tree are duplicated on purpose (each is a five-line cairo wrapper),
 * but this one carries a libjpeg error manager and a longjmp, and a second copy
 * is a second place for a corrupt JPEG to abort the compositor.
 * NULL on any failure; caller owns the surface. */
cairo_surface_t *syn_decode_jpeg(const char *path);

/* Persisted wallpaper choice (~/.config/synui/wallpaper.state). Written by
 * the wppick.c picker; applied over the parsed config on every load so a
 * GUI choice survives restart without rewriting synuirc. */
void wallpaper_state_save(syn_server_t *s);
void wallpaper_state_load(syn_config_t *cfg);

/* ── Per-monitor overrides ───────────────────────────────── */
/* This output's entry in cfg->wallpaper_out[], or NULL when it has none.
 * `create` allocates one (seeded from the global config) if there is room. */
syn_wp_output_t *wallpaper_output_entry(syn_config_t *cfg, const char *name,
                                        bool create);
/* The wallpaper that actually applies to `name`: its override if it has one,
 * the global config otherwise. Any out-parameter may be NULL. `path` points
 * into the config and stays valid until the config changes. */
void wallpaper_effective(syn_config_t *cfg, const char *name,
                         syn_wallpaper_src_t *src, const char **path,
                         syn_wallpaper_mode_t *mode);
/* Drop one output's override (NULL name = all of them), handing it back to
 * the global config. */
void wallpaper_output_clear(syn_config_t *cfg, const char *name);
/* Point one monitor at `tok` — the same vocabulary the synuirc `wallpaper` key
 * takes (matrix / default / none / a path), creating the override if needed.
 * NULL tok changes only the mode; mode < 0 changes only the token. */
void wallpaper_output_apply(syn_config_t *cfg, const char *name,
                            const char *tok, int mode);
/* syn_wallpaper_mode_t for a mode name, or -1 if it is not one. */
int  wallpaper_mode_from_name(const char *name);

/* Decode a PNG/JPEG into an image surface, ~ expanded. NULL on any failure —
 * every caller has a fallback, and the failure is logged here so none of them
 * has to. Shared with lock.c (the lock background) and saver.c (the slideshow)
 * so there is exactly one decoder in the tree. */
cairo_surface_t *wallpaper_decode(const char *path);

/* Paint `src` into a dst_w × dst_h box at the origin of `cr`, framed by
 * `mode`. Shared for the same reason. */
void wallpaper_paint_box(cairo_t *cr, cairo_surface_t *src,
                         int dst_w, int dst_h, syn_wallpaper_mode_t mode);

/* Box-blur an ARGB32 image surface in place, `radius` px, three passes (which
 * approximates a Gaussian closely enough for a background nobody is reading).
 * A no-op for radius <= 0 or a non-image surface. Used by the lock background;
 * this is a once-per-lock cost, not a per-frame one. */
void syn_surface_blur(cairo_surface_t *surf, int radius);

/* ── Power saving (power.c) ──────────────────────────────── */
/* Create the idle timers and arm them from the current config. */
void power_init(syn_server_t *s);
void power_finish(syn_server_t *s);
/* Called from every input event (via notify_activity) and whenever an idle
 * inhibitor appears/disappears: undoes any stage that has fired and rearms. */
void power_notify_activity(syn_server_t *s);
/* Un-dim and un-blank on resume, then re-arm. NOT power_notify_activity():
 * that clears power.locked, which after a sleep-lock is still true. */
void power_wake_display(syn_server_t *s);
/* Called from output_destroy() while the wlr_output is still live: if the head
 * is losing its sink with a CRTC still bound, commit it disabled so the panel
 * is free to re-enumerate. The sweep in power.c cannot do this — the output has
 * already left s->outputs by the time any pass runs. */
void power_release_dead_head(syn_output_t *o);
/* Re-arm after the config changed (panel edit, config reload). */
void power_reload(syn_server_t *s);
/* A laptop lid toggled. `closed` is libinput's switch state, so the lid is
 * shut when it is true. Runs the configured lid action (which one depends on
 * whether an external output is connected — see syn_lid_action_t). */
void power_lid_set(syn_server_t *s, bool closed);
/* True when an output that is not the built-in panel is enabled, i.e. the
 * laptop is docked and closing the lid should not necessarily stop anything. */
bool power_docked(syn_server_t *s);
/* True when a charger is plugged in. Read from /sys/class/power_supply at the
 * moment it is asked, so it is never stale; a machine that reports no mains
 * supply at all (a desktop) counts as on mains. */
bool power_on_ac(void);
/* Which of the three lid cases is live right now — "docked", "plugged in" or
 * "on battery". The panel names it so the rows do not have to spell out their
 * own precedence. */
const char *power_lid_case(syn_server_t *s);

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
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  taskmgr_motion(syn_server_t *s, double lx, double ly);
int  taskmgr_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  taskmgr_scroll(syn_server_t *s, double lx, double ly, double delta);
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
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  news_motion(syn_server_t *s, double lx, double ly);
int  news_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  news_scroll(syn_server_t *s, double lx, double ly, double delta);
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
/* Would this view, as it stands, make game mode engage? The single definition
 * of "this is a game", shared by the detector and by the fullscreen placement
 * in layout.c so the two can never disagree about what a game is — a game sent
 * to the main screen that then failed to trigger game mode (or the reverse)
 * would be the worst of both. Silent and cheap: it is called per fullscreen
 * transition, not per frame. */
int game_view_is_game(syn_server_t *s, syn_view_t *view);
/* Where this view should be fullscreened, or NULL to leave the choice to the
 * caller's usual rules. Non-NULL only for an actual game with game_output set
 * to something other than GAME_OUT_ASK. */
syn_output_t *game_output_for(syn_server_t *s, syn_view_t *view);
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

/* Coats. NEON is the house cat — the neon-on-slate original — and is FIRST so
 * that it is what a zeroed config and an unrecognised `cat_breed =` both land
 * on. Every other entry only changes colours and adds markings; the anatomy,
 * the walk cycle and the poses are one drawing for all of them.
 *
 * Order is display order in the control panel and must match cat_breed_names[]
 * in cat_draw.c and ctl_names_cat_breed[] in ctlpanel.c. */
typedef enum {
    CAT_BREED_NEON,          /* slate coat, cyan rim — the original */
    CAT_BREED_TABBY,
    CAT_BREED_GINGER,        /* marmalade, tabby-striped */
    CAT_BREED_TUXEDO,
    CAT_BREED_SIAMESE,
    CAT_BREED_CALICO,
    CAT_BREED_TORTIE,
    CAT_BREED_RUSSIAN_BLUE,
    CAT_BREED_BLACK,
    CAT_BREED_COUNT
} cat_breed_t;

/* Lower-case tokens for synuirc's `cat_breed =`, indexed by cat_breed_t. */
extern const char *const cat_breed_names[CAT_BREED_COUNT];

/* Everything cat_paint needs. Kept free of syn_server_t so the drawing can be
 * rendered to a PNG by tests/cat_render_test.c — "it doesn't look like a cat"
 * is the one bug here that no assertion will ever catch. */
typedef struct {
    int    state;      /* CAT_WALK / CAT_SIT / CAT_SLEEP */
    double phase;      /* walk cycle */
    double now;        /* drives tail sway, ear twitch, z's */
    bool   blinking;
    int    breed;      /* cat_breed_t; out of range falls back to NEON */
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
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  power_motion(syn_server_t *s, double lx, double ly);
int  power_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  power_scroll(syn_server_t *s, double lx, double ly, double delta);

/* ── Screensaver (saver.c) ───────────────────────────────────
 *
 * Two halves that share a file: the saver itself (saver_show/saver_dismiss,
 * driven by the idle stage in power.c) and its Super+Z settings panel, which
 * follows the same contract as every other panel here. */
void saver_init(syn_server_t *s);
void saver_finish(syn_server_t *s);

/* Put the saver on screen. A no-op when the mode is BLANK (there is nothing to
 * draw that the blank stage does not already do), when it is already up, or
 * when no output can take it. */
void saver_show(syn_server_t *s);

/* Take it down. `by_input` distinguishes the user dismissing it — which is what
 * arms saver_lock — from a teardown (mode change, output loss, shutdown),
 * which must not lock anybody out. */
void saver_dismiss(syn_server_t *s, bool by_input);

/* True while the saver is drawing. power.c asks so a second stage firing
 * underneath it does not fight it for the screen. */
bool saver_active(syn_server_t *s);

/* True while the MATRIX mode is the thing on screen. matrix.c asks so it knows
 * whether the rain it is rendering is a wallpaper (bottom of wallpaper_tree) or
 * a screensaver (top of the saver's tree). */
bool saver_wants_matrix(syn_server_t *s);

/* An output appearing or going away while the saver is up, mirroring
 * lock_output_create/destroy. Suspend/resume on the NVIDIA box destroys and
 * recreates connectors, so neither is hypothetical. */
void saver_output_create(syn_output_t *o);
void saver_output_destroy(syn_output_t *o);

/* The settings panel. */
void saver_show_panel(syn_server_t *s);
void saver_hide(syn_server_t *s);
void saver_toggle(syn_server_t *s);
int  saver_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
int  saver_motion(syn_server_t *s, double lx, double ly);
int  saver_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  saver_scroll(syn_server_t *s, double lx, double ly, double delta);

/* Name/value for one panel row; render.c draws, saver.c owns the vocabulary.
 * Returns true when the row is inert (a "never" timeout, a slideshow interval
 * on a non-slideshow mode), so the renderer can grey it without knowing why. */
int  saver_panel_rows(syn_server_t *s, int row, char *name, size_t nn,
                      char *value, size_t vn);

void saver_state_save(syn_server_t *s);
void saver_state_load(syn_config_t *cfg);

/* ── Clock & Time settings + calendar (clock.c) ──────────── */
void clock_init(syn_server_t *s);
void clock_finish(syn_server_t *s);
void clock_state_load(syn_server_t *s);
void clock_state_save(syn_server_t *s);
void clock_show(syn_server_t *s);
void clock_hide(syn_server_t *s);
void clock_toggle(syn_server_t *s);
int  clock_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  clock_motion(syn_server_t *s, double lx, double ly);
int  clock_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  clock_scroll(syn_server_t *s, double lx, double ly, double delta);
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
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  calendar_motion(syn_server_t *s, double lx, double ly);
int  calendar_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  calendar_scroll(syn_server_t *s, double lx, double ly, double delta);
int  calendar_days_in_month(int year, int mon);
int  calendar_first_weekday(int year, int mon);
void synui_render_calendar(syn_server_t *s);

/* ── CRT filter panel (filters.c) ────────────────────────── */
void filters_show(syn_server_t *s);
void filters_hide(syn_server_t *s);
void filters_toggle(syn_server_t *s);

/* AI model picker (aimodel.c). */
/* The fetch thread and its pipe. Paired with aimodel_finish() at teardown,
 * which joins the thread — a transfer in flight aborts within a poll interval
 * rather than holding logout for a connect timeout, the news.c lesson. */
void aimodel_init(syn_server_t *s);
void aimodel_finish(syn_server_t *s);
void aimodel_show(syn_server_t *s);
void aimodel_hide(syn_server_t *s);
void aimodel_toggle(syn_server_t *s);
int  aimodel_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
int  aimodel_motion(syn_server_t *s, double lx, double ly);
int  aimodel_click(syn_server_t *s, double lx, double ly, uint32_t button,
                   uint32_t time_msec);
int  aimodel_scroll(syn_server_t *s, double lx, double ly, double delta);
/* ── The control-panel row (System ▸ AI model) ────────────
 *
 * The row drives the SAME list and the same cursor this panel does, out of
 * s->aimodel, so the two can never disagree about which models exist or which
 * one is loaded — and cycling the row leaves the panel opened on the model you
 * were looking at. Everything that knows where the GGUFs live stays in
 * aimodel.c; ctlpanel.c only asks.
 */
/* Read the directory and put the cursor on the loaded model. Called when the
 * control panel opens, so the row starts on the truth rather than on whatever
 * the picker was last left on. */
void aimodel_row_sync(syn_server_t *s);
/* The row's value text: the pick, with the .gguf stripped, or the state
 * ("loading …", "none") when there is no name worth showing. */
void aimodel_row_value(syn_server_t *s, char *buf, size_t n);
/* Move the pick. Returns 0 and leaves a reason in the panel's status when
 * there is nothing to move through or a switch is already in flight. */
int  aimodel_row_cycle(syn_server_t *s, int dir);
/* Ask synapd for whatever the pick has settled on. Returns 1 if a request went
 * out (the row shows "loading …" until a status poll says otherwise). */
int  aimodel_row_commit(syn_server_t *s);
/* The panel's own status line, so the row can show synapd's refusal verbatim
 * instead of a generic failure. Empty when there is nothing to say. */
const char *aimodel_status_text(syn_server_t *s);

/* What installed model `idx` says about itself — architecture, real
 * quantisation, context length, whether it has a chat template. Read from the
 * file's own header on first use and cached on the entry; see aimodel.c. NULL
 * if `idx` is not an installed model. The returned struct's `ok` is 0 when the
 * header could not be read, with the reason in `err`. */
const syn_gguf_t *aimodel_info(syn_server_t *s, int idx);
/* Ask synapd to load a different model. Bare filename, never a path — synapd
 * confines it to its own models directory. Returns 0 if accepted, -1 with
 * synapd's own refusal text in out[]. */
int  synmon_send_reload(const char *model_name, char *out, size_t out_len);
/* Called by synapd_mon.c when a poll lands, so the panel follows the daemon
 * rather than guessing when a switch finished. */
void aimodel_status_changed(syn_server_t *s);

/* ── The download catalogue ──────────────────────────────────
 *
 * Exposed for tests/aimodel_catalog_test.c. The parsers take a body and never
 * touch the network, which is what makes them testable at all — and the
 * validators below are a privilege boundary (a name off the network becomes a
 * filename root writes), so they are pinned by name rather than left inline.
 */
/* Parse a Hugging Face /api/models listing. Returns how many entries were
 * filled, never more than `max`. */
int  aimodel_parse_search(const char *body, size_t len,
                          syn_aimodel_cat_t *out, int max);
/* Parse a /api/models/ID/tree listing into the repo's GGUF files, largest
 * `size` per entry winning (an LFS entry reports both the pointer and the
 * real one). Returns how many were filled. */
int  aimodel_parse_tree(const char *body, size_t len,
                        syn_aimodel_file_t *out, int max);
/* Deleting an installed model, in two presses. `arm` marks the row under the
 * cursor and returns 1 when a confirmation is now showing; `confirm` queues the
 * privileged delete (syn-model-delete@TOKEN.service — synui cannot unlink in
 * synapd's models directory itself); `cancel` backs out and is safe to call
 * when nothing is armed. All three refuse the model synapd currently has
 * loaded, and leave the reason in the panel's status. */
int  aimodel_delete_arm(syn_server_t *s);
int  aimodel_delete_confirm(syn_server_t *s);
void aimodel_delete_cancel(syn_server_t *s);
/* Describe a repo that is NOT downloaded yet, in the same words the installed
 * side uses on the file it will become — the installed description comes out of
 * the GGUF header, which does not exist until several GB have been fetched, and
 * that is after the decision rather than before it. Assembled from the repo's
 * tags plus the SELECTED quantisation, so the text moves with that choice.
 * `f` may be NULL (the file list has not landed); each writes "" rather than
 * inventing a claim. Tested in tests/aimodel_catalog_test.c. */
void aimodel_cat_bio(const syn_aimodel_cat_t *c, const syn_aimodel_file_t *f,
                     char *out, size_t len);
/* "reasoning · coding · conversation" — the repo's tags through the same
 * gguf_tag_english() map the installed side uses. */
void aimodel_cat_good_at(const syn_aimodel_cat_t *c, char *out, size_t len);
/* "Qwen3 Coder 30B by Qwen" — the base_model: tag, shaped like
 * gguf_based_on(). Empty when the repo named none or named only itself. */
void aimodel_cat_based_on(const syn_aimodel_cat_t *c, char *out, size_t len);
/* The quantisation read out of a GGUF filename ("…Q4_K_M.gguf" → "Q4_K_M"),
 * and the parameter count read out of a repo name ("Phi-3-mini" → "", but
 * "Mistral-7B-Instruct" → "7B"). Both write "" when there is nothing to read
 * rather than guessing. */
void aimodel_quant_of(const char *filename, char *out, size_t n);
void aimodel_params_of(const char *name, char *out, size_t n);
/* Is this a filename synui may ask root to write into synapd's models
 * directory? Bare, .gguf, no dot-leading, ASCII-safe. Returns 1 if so.
 * syn-model re-checks the same rule; this is the half that stops a bad name
 * being sent at all. */
int  aimodel_name_ok(const char *file);
/* Is this a URL synui may hand to the downloader? https, huggingface.co, no
 * whitespace or control characters. Returns 1 if so. */
int  aimodel_url_ok(const char *url);
/* The list column's slot layout, shared with render.c so the drawn rows and
 * the clickable rows cannot drift: slot 0 and slot count+1 are the INSTALLED
 * and AVAILABLE headings, and everything between them is a row. */
int  aimodel_slots(const syn_aimodel_t *am);
int  aimodel_slot_is_head(const syn_aimodel_t *am, int slot);
int  aimodel_cursor_slot(const syn_aimodel_t *am);
/* The request token for a destination filename: the stem, reduced to the
 * characters a systemd instance name and a path can both carry. Returns 1 on
 * success, 0 if nothing usable survived. */
int  aimodel_token_of(const char *file, char *out, size_t n);
/* Modal key handling while the panel is open, as in power_key. */
int  filters_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  filters_motion(syn_server_t *s, double lx, double ly);
int  filters_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  filters_scroll(syn_server_t *s, double lx, double ly, double delta);
/* Persisted strengths (~/.config/synui/filters.state), applied over the config
 * defaults so a look tuned by eye survives a restart. The load takes a CONFIG
 * and runs inside synui_config_load(), not once at startup — a reload replaces
 * s->config wholesale, and a state file it does not read is a state file every
 * reload discards. */
void filters_state_load_config(syn_config_t *cfg);
void filters_state_save(syn_server_t *s);
/* Name/value for one panel row; render.c draws. The return is the row's 0..1
 * fraction for its slider, or -1.0f for the master switch (which has no bar). */
const char *filters_row_label(int row);
float filters_row_value(syn_server_t *s, int row, char *buf, size_t n);
void synui_render_filters(syn_server_t *s);
void synui_render_aimodel(syn_server_t *s);
void synui_render_power(syn_server_t *s);
void synui_render_saver(syn_server_t *s);   /* the Super+Z settings panel */

/* ── Window-effect page of that panel (uifx.c) ───────────── */
/* Same shape as the filters rows above, so render.c draws both with one loop:
 * a label, a value string, and the 0..1 fraction for the bar (-1.0f = a word). */
const char *uifx_row_label(int row);
float uifx_row_value(syn_server_t *s, int row, char *buf, size_t n);
/* Move the selected row by one notch and push the result to the live scene. */
void uifx_adjust(syn_server_t *s, int dir);
/* Why THIS row is currently doing nothing — "shadow is off", "retro chrome is
 * square" — or NULL when it bites. render.c greys a row that has a reason and
 * uifx_adjust appends it to the status, the way the CRT page names an off master
 * switch rather than letting a moving number look broken. */
const char *uifx_row_inert(syn_server_t *s, int row);
/* The same idea for the panel as a whole, drawn under the title, or NULL. */
const char *uifx_note(syn_server_t *s);
/* Persisted to ~/.config/synui/uifx.state, over the config defaults. Loaded in
 * synui_config_load()'s tail (see filters_state_load_config); uifx_apply() is
 * the server half, owed by startup and by every reload. */
void uifx_state_load_config(syn_config_t *cfg);
void uifx_state_save(syn_server_t *s);
/* Re-push every window-effect value to the scene graph. Public because the
 * config load reads uifx.state after the scene already took the blur data. */
void uifx_apply(syn_server_t *s);
/* Space on this page: toggle the selected row if it is a switch, else the
 * switch that GOVERNS it — which is the one you want when a row is greyed. */
void uifx_space(syn_server_t *s);

/* ── Desktop widget manager (widgets.c) ──────────────────── */
void widgets_show(syn_server_t *s);
void widgets_hide(syn_server_t *s);
void widgets_toggle(syn_server_t *s);
int  widgets_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  widgets_motion(syn_server_t *s, double lx, double ly);
int  widgets_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  widgets_scroll(syn_server_t *s, double lx, double ly, double delta);
/* The name synui-widgets knows this row by ("visualizer", "sysmon", …), or NULL
 * for the master row. This is the whole binding between the enum and the helper. */
const char *widget_row_name(int row);
const char *widget_row_label(int row);
/* The word a row shows. The master row answers all/some/none, because "on" with
 * one widget off would be a lie. */
const char *widgets_row_value(syn_server_t *s, int row);
void synui_render_widgets(syn_server_t *s);

/* ── Event sounds (sound.c) ──────────────────────────────── */
/* Play one event's sound, if it is enabled. Cheap and safe to call from
 * anywhere: it does nothing at all when sounds are off, which is the default. */
void sound_play(syn_server_t *s, syn_sound_event_t evt);
const char *sound_event_name(syn_sound_event_t evt);   /* sounds.state key */
const char *sound_event_label(syn_sound_event_t evt);  /* panel text */
/* The automatic sample ids for an event, best first, space-separated — the same
 * chain synui-sound's ids() holds, and it must stay the same. Used only to show
 * what an event WILL play; the helper is still the one that resolves and plays. */
const char *sound_event_ids(syn_sound_event_t evt);
/* The sample id this event will actually play, written to `out`. Returns 0 when
 * the theme has no such sample — an event that is on and still silent, which the
 * panel has to be able to say out loud or it reads as a broken toggle. */
int sound_resolved_id(const syn_sound_t *snd, int evt, char *out, size_t outsz);
/* Whether the selected theme is one the picker offers. A state file written
 * before the ghost-theme filter can name one that is not (alsa), which is a
 * desktop with every sound on and nothing audible. */
int sound_theme_installed(const char *theme);
/* Read sounds.state if it has changed since the last look. Called by sound_play
 * and by the panel; there is no reload hook to remember. */
void sound_state_refresh(syn_server_t *s);
void sound_show(syn_server_t *s);
void sound_hide(syn_server_t *s);
void sound_toggle(syn_server_t *s);
int  sound_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  sound_motion(syn_server_t *s, double lx, double ly);
int  sound_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  sound_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_sound(syn_server_t *s);
/* udev monitor: a device appearing or going away becomes a sound event. Both
 * are no-ops if udev is unavailable — a desktop with no device notifications is
 * a smaller loss than a compositor that will not start. */
void sound_udev_init(syn_server_t *s);
void sound_udev_finish(syn_server_t *s);

/* ── Control panel (ctlpanel.c) ──────────────────────────── */
void ctlpanel_show(syn_server_t *s);
void ctlpanel_hide(syn_server_t *s);
void ctlpanel_toggle(syn_server_t *s);
/* Open onto a named category ("display", "appearance", … — the sidebar names,
 * case-insensitively). The `control` bind action's argument, and how the start
 * menu's Settings submenu reaches the same tree instead of listing its own copy
 * of it. An unrecognised name falls back to a plain toggle. */
void ctlpanel_show_cat(syn_server_t *s, const char *name);
int  ctlpanel_cat_from_name(const char *name);
/* Per-frame poll for the AI-backend row; 1 while it wants another frame. */
int  ctlpanel_tick(syn_server_t *s);
/* Modal key handling while the panel is open, as in filters_key. */
int  ctlpanel_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this header.
 * Hovering the sidebar moves focus there as well as selecting, because the two
 * columns are a menu and its submenu — see ctlpanel.c. */
int  ctlpanel_motion(syn_server_t *s, double lx, double ly);
int  ctlpanel_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  ctlpanel_scroll(syn_server_t *s, double lx, double ly, double delta);
/* Row text. value[] is filled with the row's current state ("on"/"off"/"GPU"),
 * or left empty for a row that only opens a panel and holds no state itself. */
const char *ctlpanel_row_label(int row);
/* Redraw the panel if it is up, for state changed from outside it (a bind fired
 * while it was open). No-op when the panel is hidden. */
void ctlpanel_refresh(syn_server_t *s);
void ctlpanel_row_value(syn_server_t *s, int row, char *buf, size_t n);
/* Category name for the sidebar, and that category's rows in display order —
 * both driven by the one item table in ctlpanel.c, so the sidebar, the row pane
 * and the keyboard cursor can never disagree about what is in a category.
 * ctlpanel_cat_items() returns how many row ids were written into out[]. */
const char *ctlpanel_cat_name(int cat);
int  ctlpanel_cat_items(int cat, int *out, int max);
syn_ctl_kind_t ctlpanel_row_kind(int row);
/* The row id the cursor is on, or -1 when the category has no rows of its own
 * (the shortcuts list). Derived from cat+item rather than stored, so there is
 * one answer to "what is selected" instead of two that can drift. */
int  ctlpanel_selected_row(syn_server_t *s);
/* Called by a panel's hide path with the bind action that opens it. A no-op
 * unless the control panel is the thing that opened it, in which case the
 * control panel comes back with the cursor where it was left. */
void ctlpanel_child_closed(syn_server_t *s, const char *action);
/* The shortcuts column, rebuilt from the live bind table on every render.
 * Returns how many rows were written into out[] (at most max). */
int  ctlpanel_shortcuts(syn_server_t *s, syn_ctl_shortcut_t *out, int max);
/* The shortcuts pane's cursor, for the rebind keys and for render.c's
 * highlight. `_selected` copies the row out by value because every caller is
 * about to rewrite the bind table it was derived from. */
int  ctlpanel_shortcut_count(syn_server_t *s);
int  ctlpanel_shortcut_selected(syn_server_t *s, syn_ctl_shortcut_t *out);
/* What a bind action does, in words — the same table the shortcuts column
 * labels its rows from. Exposed for the rebind helper, which has to name the
 * shortcut a chord is already taken by. */
const char *ctlpanel_action_desc(const char *action, const char *arg);
/* The tap modifier as a KEYCAP word — "Super", "Ctrl", "Alt", "Shift", or "Off"
 * for no tap. Exposed for the same reason as the line above: keys.c's status
 * line names the tap key the user just chose, and a third spelling of "Super"
 * is a third one to keep in step. syn_tap_mod_name() is the synuirc half. */
const char *ctlpanel_tap_key_name(uint32_t mod);
/* ── Shortcut palette + rebind helper (keys.c) ───────────────
 * Super+/ (and Super+?): the same list ctlpanel_shortcuts() builds, filtered as
 * you type, with Enter running the bind you land on. Same modal contract as
 * every other panel, except that it claims bare Shift and every printable key —
 * it is a search box, so `q` types a q rather than closing it.
 *
 * F2 (or Ctrl+R) on a row rebinds it: the next chord becomes that shortcut's
 * key, applied live and persisted to binds.state as a diff against the config
 * as loaded. Ctrl+Shift+R puts every shortcut back. It lives here rather than in
 * a panel of its own because a rebind window would be a second list of
 * shortcuts, and a hand-kept second list is the bug ctlpanel_shortcuts() was
 * written to make impossible. */
void keys_show(syn_server_t *s);
/* ── The rebind rules, shared (keys.c) ───────────────────────
 *
 * The control panel's Shortcuts category rebinds with the same three keys, and
 * these are what it drives so that it cannot mean something different by them.
 * The two panels share the shortcut LIST already; the rules are the half where
 * a fork would be hardest to spot, because a second copy of "a bare letter is
 * not bindable" fails only for the user who tries a bare letter.
 *
 * Each panel still owns its own capture state — which row is armed, and where
 * the status line goes. That part is UI, and the two panels differ in it.
 *
 * syn_rebind_apply() returns 1 when the bind table actually changed, so the
 * caller knows to re-read the list; `status` is filled either way. Its `sc` must
 * not point into s->config.binds[], which it rewrites. */
/* Should an armed capture throw this keysym away? True for a modifier arriving
 * as a press of its own while you reach for the other half of a chord — without
 * it every capture comes out as "Super". FALSE for the tap row, where the
 * modifier IS the shortcut, which is the whole reason this takes the row and is
 * not a predicate on the keysym alone. */
bool        syn_rebind_capture_ignores(const syn_ctl_shortcut_t *sc,
                                       xkb_keysym_t sym);
const char *syn_rebind_refusal(const syn_ctl_shortcut_t *sc);
int         syn_rebind_apply(syn_server_t *s, const syn_ctl_shortcut_t *sc,
                             xkb_keysym_t sym, uint32_t mods,
                             char *status, size_t status_n);
void        syn_rebind_reset_all(syn_server_t *s, char *status, size_t status_n);
/* Point the modifier tap at THIS row's action (F3 in both panels). The other
 * half of the tap: syn_rebind_apply() moves it to another key, this says what
 * it opens when tapped. Takes a row rather than an action string so the two
 * panels cannot disagree about what is assignable — the collapsed workspace
 * rows name no single action, and the tap row cannot be pointed at itself.
 * Returns 1 if config.tap_action changed; `status` is filled either way. */
int         syn_rebind_set_tap_action(syn_server_t *s,
                                      const syn_ctl_shortcut_t *sc,
                                      char *status, size_t status_n);
void keys_hide(syn_server_t *s);
void keys_toggle(syn_server_t *s);
int  keys_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
int  keys_motion(syn_server_t *s, double lx, double ly);
int  keys_click(syn_server_t *s, double lx, double ly, uint32_t button,
                uint32_t time_msec);
int  keys_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_keys(syn_server_t *s);

/* ── Mission control / overview (overview.c) ─────────────────
 * Super+X: every window on this desktop laid out so none of them overlap, with
 * the virtual desktops along the bottom. See syn_overview_t for what makes it a
 * different thing from the Alt+Tab switcher rather than a bigger one.
 *
 * The two functions below the panel API are what makes "what you click is what
 * you see" true by construction: both the renderer and the hit test call them,
 * so a tile cannot be drawn in one place and clicked in another. */
void overview_show(syn_server_t *s);
void overview_hide(syn_server_t *s);
void overview_toggle(syn_server_t *s);
/* Mission control AS THE SWITCHER (config.alt_tab_overview). The first press
 * opens it on the focused window and steps one tile; every press after that
 * walks the grid, which is what Alt+Tab does everywhere else. */
void overview_alt_step(syn_server_t *s, int dir);
/* Alt let go: activate what the walk landed on. A no-op unless the overview is
 * up AND was opened by overview_alt_step, so a mission control opened from the
 * control panel stays up when a modifier happens to be released over it. */
void overview_alt_commit(syn_server_t *s);
int  overview_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
int  overview_motion(syn_server_t *s, double lx, double ly);
int  overview_click(syn_server_t *s, double lx, double ly, uint32_t button,
                    uint32_t time_msec);
int  overview_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_overview(syn_server_t *s);

/* Every window on the desktop the overview is showing, in a STABLE order —
 * stacking order, not most-recently-used. A grid that reshuffled itself as you
 * looked at windows would defeat the one thing it is for, which is remembering
 * where you left something. Returns how many were written. */
int  overview_candidates(syn_server_t *s, syn_view_t **out, int max);
/* Where those tiles go, in LAYOUT coordinates. `ob` is the output box the
 * overview is drawn on. Pure — same inputs, same boxes. */
void overview_layout(const struct wlr_box *ob, int n, struct wlr_box *out);
/* The desktop pills along the bottom, likewise. Always WORKSPACE_MAX of them:
 * the strip is how you reach an EMPTY desktop, so hiding the empty ones would
 * hide the only thing there is to go to. */
void overview_ws_layout(const struct wlr_box *ob, struct wlr_box *out);
/* The output the overview is on — the one with the focus, like every other
 * full-screen thing synui draws. */
void overview_output_box(syn_server_t *s, struct wlr_box *ob);
/* Snapshot the bind table as the baseline, then lay binds.state over it. Called
 * from synui_config_load() after synuirc and the other state files, because the
 * diff the helper writes is measured against exactly that. */
void binds_state_load(syn_config_t *cfg);

/* How many shortcut rows the panel has room to draw — render.c owns the
 * geometry, ctlpanel.c owns the scroll clamp, so they have to agree. */
#define CTL_SHORTCUT_ROWS  16
/* And how many SETTINGS rows. The same number, for the same reason: the pane is
 * the same pane. Categories are now longer than this, so the pane scrolls. */
#define CTL_ROW_ROWS       CTL_SHORTCUT_ROWS

/* The rows the pane should draw right now, in order, and how many there are.
 * Either the selected category's rows or — while the search box is open — the
 * matches from every category. render.c asks this one question instead of
 * knowing about search at all, so the pointer grid, the cursor and the draw all
 * walk the same list. Returns the count written into out[]. */
int  ctlpanel_visible_rows(syn_server_t *s, int *out, int max);
/* The section a row is IN (walking back to whichever row opened it), and
 * whether this row is the one that opens it. Sections are what keep a
 * forty-row category readable: the pane rules above each one, and the
 * breadcrumb names the one the cursor is in. */
const char *ctlpanel_row_section(int row);
int  ctlpanel_row_starts_section(int row);
/* One line explaining the selected row, drawn in the footer. NULL when the row
 * has none. */
const char *ctlpanel_row_help(int row);
/* Which category a row belongs to — needed only by the search results, where
 * rows from everywhere are mixed and each has to say where it came from. */
int  ctlpanel_row_cat(int row);
/* The synuirc key a row drives, or NULL for a jump-off or a toggle whose state
 * is not a config field. The line between the table-driven rows and the rest. */
const char *ctlpanel_row_key(int row);
/* How many options a CTL_VAL_ENUM row has, or 0 for every other kind of row.
 * Exists for the table test, which has to walk each option and round-trip it —
 * a spelling the parser rejects can hide behind any option but the first. */
int  ctlpanel_row_options(int row);
/* Is this row still at its compiled-in default? Drives the "modified" marker
 * and tells the reset key whether there is anything to undo. */
int  ctlpanel_row_is_default(syn_server_t *s, int row);
void synui_render_ctlpanel(syn_server_t *s);
/*
 * Push the desktop's corner radius onto every panel's own background rect —
 * the control panel, the task manager, the pickers, the desktop and dock
 * menus. Windows have been rounded since the scenefx migration and synui's own
 * furniture never was, so turning corners on rounded every application and left
 * the compositor square.
 *
 * Safe and cheap to call every frame: the rects are created lazily on a panel's
 * first render (an unopened panel's slot is NULL and is skipped), and
 * wlr_scene_rect_set_corner_radii() returns without damaging anything when the
 * radii already match. Calling it per frame is also what makes a radius change
 * land on panels that are already open.
 */
void panel_chrome_sync(syn_server_t *s);

/* ── Theme manager (theme.c) ─────────────────────────────── */
/* Apply a preset: overwrite the border/titlebar colours + default opacities in
 * cfg, re-decorate every mapped window, repaint, and (for the app colour-scheme)
 * write kdeglobals / GTK / Firefox so Dolphin & co. follow. Persists theme.state
 * unless `save` is 0 (startup load passes 0 — it is applying what it just read). */
void theme_apply(syn_server_t *s, syn_theme_t theme, int save);
/* Copy a preset's colours + opacity levels into a config only (no server) —
 * what config.c calls for a synuirc `theme =` line at parse time. */
void theme_load_colors(syn_config_t *cfg, syn_theme_t theme);
/* theme.state → cfg: the theme name, a pushed palette, and the translucency
 * trio. Pure — no server, no render.c — so synui_config_load() can call it with
 * the rest of the state files, which is what stops a config RELOAD from
 * resetting the desktop to stock SYNAPSE. See theme.c for the whole story. */
void theme_state_load_config(syn_config_t *cfg);
/* …and the applying half: put the desktop on whatever the config now says.
 * Startup passes push_apps=1 so the toolkits are reskinned too; a reload passes
 * 0, since nothing outside the compositor changed. Never saves. */
void theme_apply_from_config(syn_server_t *s, int push_apps);
/* `synctl dispatch theme <arg>` — a preset token ("dark"), or three #rrggbb
 * colours (accent, panel surface, ink) to apply as a custom palette. Returns 0
 * and logs when the argument is neither. Bare `theme` opens the picker instead;
 * this is only reached with an argument. */
int  theme_dispatch(syn_server_t *s, const char *arg);
/* Apply a palette that is not in the preset table: the three colours, plus the
 * window chrome derived from them. Does NOT touch the opacity levels (they are
 * the user's slider, not a colour) and does NOT spawn synui-apply-theme — the
 * caller pushing a palette in already owns the app side, and two writers of
 * kdeglobals with slightly different numbers is a race with no winner. */
void theme_apply_custom(syn_server_t *s, const float accent[4],
                        const float base[4], const float ink[4], int save);
const char *theme_name(syn_theme_t t);    /* display label, e.g. "Windows XP" */
/* Two-tone swatch for the picker: the caption colour and the focus accent. */
void theme_preview_colors(syn_theme_t t, float caption[4], float accent[4]);
/* Cache the panel accent render.c draws every synui panel with. Called from
 * theme_load_colors so a theme switch (or a synuirc `theme =`) reskins the UI. */
void render_set_panel_accent(const float rgb[4]);
void render_set_panel_surface(const float bg[4], const float ink[4]);

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
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  theme_motion(syn_server_t *s, double lx, double ly);
int  theme_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  theme_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_thememgr(syn_server_t *s);

/* ── Clipboard history (clipboard.c) ─────────────────────── */
void clipboard_init(syn_server_t *s);
void clipboard_finish(syn_server_t *s);
void clipboard_show(syn_server_t *s);
void clipboard_hide(syn_server_t *s);
void clipboard_toggle(syn_server_t *s);
void clipboard_clear(syn_server_t *s);
/* The clipboard's current text, or NULL when there is none. Borrowed — valid
 * until the next selection. Synchronous: the history is already in memory, so
 * a panel can paste without piping a client. See clipboard.c. */
const char *clipboard_current_text(syn_server_t *s);
int  clipboard_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  clipboard_motion(syn_server_t *s, double lx, double ly);
int  clipboard_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  clipboard_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_clipboard(syn_server_t *s);

/* ── Alt+Tab switcher overlay (render.c, driven by input.c) ──
 *
 * Draw the tile grid for one step of the cycle. `cands` is the candidate list
 * as input.c just rebuilt it, `sel` the index within it of the window now
 * focused. Neither is retained past the call — the pointers are read, drawn and
 * dropped — so a view destroyed between two presses can never be reached
 * through the overlay. */
void synui_render_alttab(syn_server_t *s, syn_view_t **cands, int n, int sel);
/* Take the overlay down and release the client buffers the tiles were holding. */
void synui_alttab_hide(syn_server_t *s);

/* ── Night light (nightlight.c) ──────────────────────────── */
/* Writes the gamma LUT on every output. No-op where the backend has no gamma
 * (headless/pixman). wlr_gamma_control_manager_v1 stays exported, so wlsunset
 * still works for anyone who prefers it — last writer wins. */
void nightlight_apply(syn_server_t *s);
void nightlight_toggle(syn_server_t *s);
/* The warmth, as the colour transform the OUTPUT STATE must carry (wlroots 0.20
 * replaced the gamma-LUT commit with this). NULL when night light is off.
 * Borrowed — do not unref. */
struct wlr_color_transform *nightlight_color_transform(syn_server_t *s);
/* The temperature that transform stands for: the configured Kelvin while night
 * light is on, 0 (identity) while it is off. What syn_output.nightlight_temp is
 * compared against to decide a commit is needed. */
int nightlight_effective_temp(syn_server_t *s);
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
/* Take or drop logind's handle-lid-switch block inhibitor to match the current
 * lid config: synui has to hold it to stop logind suspending out from under a
 * lid action of its own. Idempotent — call it after any config change. */
void logind_lid_update(syn_server_t *s);
/* Is synui holding that inhibitor? False means logind still acts on the lid
 * itself and power.c must keep its hands off. */
bool logind_holds_lid(void);
/* logind's own configured handler for one lid case ("suspend", "ignore",
 * "lock", "hibernate", …), so a lid row left on `system` can do what logind
 * would have done even while synui holds the inhibitor for the other rows.
 * False if it cannot be read (no bus). */
bool logind_lid_handler(bool docked, bool on_ac, char *buf, size_t n);

/* ── Notifications (notif.c) ─────────────────────────────── */
/* notif_init takes org.freedesktop.Notifications on the session bus. Safe where
 * there is no bus or the name is already owned (a stray mako): it logs, leaves
 * the name to whoever has it, and synui simply draws no toasts. */
void notif_init(syn_server_t *s);
void notif_finish(syn_server_t *s);
void synui_render_notifs(syn_server_t *s);
/* Post a toast from inside synui — the body of Notify(), so a compositor that
 * *is* the notification daemon does not have to go over the bus to reach it.
 * `replaces` 0 appends a new toast and chimes; a live id updates it in place and
 * stays quiet. `expire` is the protocol's: -1 server default, 0 never. Returns
 * the id to pass back as `replaces`. */
uint32_t notif_post(syn_server_t *s, const char *app, const char *summary,
                    const char *body, int urgency, int32_t expire,
                    uint32_t replaces);
/* The same, with an explicit override of Do Not Disturb. The ONLY caller that
 * should pass true is the toggle's own confirmation — feedback for the key you
 * just pressed is not an interruption, and without it turning DND on while it is
 * already on is indistinguishable from a dead keybinding. Anything else that
 * wants to be heard through DND should be posting at CRITICAL urgency, which
 * gets through on its own merits. */
uint32_t notif_post_ex(syn_server_t *s, const char *app, const char *summary,
                       const char *body, int urgency, int32_t expire,
                       uint32_t replaces, bool dnd_bypass);
/* Dismiss the toast under (lx, ly) in layout coords. Returns 1 if one was hit,
 * so the click is not also delivered to whatever is behind it. */
int  notif_click(syn_server_t *s, double lx, double ly);

/* ── Do Not Disturb ──────────────────────────────────────────
 *
 * On: nothing is drawn and nothing chimes. Notify() still succeeds and still
 * returns a real id — a client that gets an error or a 0 back decides the
 * desktop has no notification daemon and starts drawing its own windows, which
 * is the opposite of quiet.
 *
 * CRITICAL urgency is delivered anyway. The spec already singles it out (it may
 * never auto-expire), synguard's intrusion alerts use it, and a mode that can
 * silence a security alert is a mode that should not exist. Everything else is
 * counted and reported once when DND is switched off. */
void notif_dnd_set(syn_server_t *s, bool on);
void notif_dnd_toggle(syn_server_t *s);
static inline bool notif_dnd_on(const syn_server_t *s) { return s->config.notif_dnd != 0; }
/* Reads dnd.state over the config. Called from synui_config_load(), NOT from
 * startup, so a config reload does not switch the desktop's ringer back on. */
void notif_dnd_state_load_config(syn_config_t *cfg);
void notif_dnd_state_save(syn_server_t *s);
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
 * formatting so the ladder and the labels stay in one place. Returns 1 when
 * the row's value means "this does nothing" (never / ignore), so the panel can
 * grey it out without having to know which strings those are. */
int power_panel_rows(syn_server_t *s, int row, char *name, size_t nn,
                     char *value, size_t vn);

/* ── matrix.c (animated wallpaper) ───────────────────────── */
void matrix_init(syn_server_t *s);                /* compile shader, load atlas (no-op on non-GLES2) */
void matrix_finish(syn_server_t *s);
bool matrix_active(syn_server_t *s);              /* SOME output selects matrix AND it initialized */
/* Can the rain actually draw? Stronger than matrix_active: the shader and atlas
 * are built on the FIRST FRAME, so "initialized" does not yet mean "works".
 * Anything picking a mode in advance (saver.c) has to ask this one. */
bool matrix_usable(syn_server_t *s);
bool matrix_output_active(syn_output_t *o);       /* ...and specifically this one does */
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
/* Path of the image that previews one row, or NULL when there is nothing real
 * to show (a solid colour, or the live Matrix shader). */
const char *wppick_row_preview(syn_server_t *s, int row);

/* ── wpthumb.c (decoded preview images for the picker) ───── */

/* Decoded, downscaled thumbnail for `path`, or NULL if it cannot be read.
 * Cached — the returned surface belongs to wpthumb.c, so do not destroy it.
 * Handles PNG, JPEG and GIF (first frame). */
cairo_surface_t *wpthumb_get(const char *path);
/* Drop every cached thumbnail. */
void wpthumb_clear(void);
/* The monitor a pick currently applies to: its connector name, or NULL for
 * "all monitors". The _label form is the same thing spelled for the panel. */
const char *wppick_scope_output(syn_server_t *s);
const char *wppick_scope_label(syn_server_t *s);
void wppick_hide(syn_server_t *s);
void wppick_toggle(syn_server_t *s);
/* Set the wallpaper to a path with no picker: `dispatch wallpaper <path>`.
 * Global scope only, and persisted — see the comment on the definition. */
void wppick_set_path(syn_server_t *s, const char *path);
int  wppick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  wppick_motion(syn_server_t *s, double lx, double ly);
int  wppick_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  wppick_scroll(syn_server_t *s, double lx, double ly, double delta);

/* Restart linux-wallpaperengine shortly from now, if wpengine.state says one
 * should be running. Coalescing and no-op-when-idle both live inside, so it is
 * safe to call from anywhere that might have cost the engine its surfaces —
 * a resume, an output coming back — as often as that happens. */
void wpengine_restore_soon(syn_server_t *s);
/* Kill the engines and WAIT for them to be gone, before we let the machine
 * sleep. They hold a CUDA context, and one caught exiting inside nvidia_uvm's
 * teardown is unfreezable — that aborted a suspend and wedged an output until
 * reboot. Blocking, bounded to ~2.3s so it fits inside logind's 5s
 * InhibitDelayMaxSec; call it BEFORE dropping the inhibitor. */
void wpengine_note_before_sleep(void);
/* An output was destroyed: from here on, one coming back is a reason to
 * re-arm the engine. Called from output_destroy(). */
void wpengine_output_lost(syn_server_t *s);
/* An output came up. No-op until wpengine_output_lost() has been seen, so the
 * outputs present at startup do not trigger a second restore. */
void wpengine_output_added(syn_server_t *s);
void synui_render_wppick(syn_server_t *s);

/* ── cursor.c (cursor theme + picker) ────────────────────── */

/* Persisted choice, shared with the synui-cursor(1) helper. Applied AFTER
 * synuirc (config.c calls it last), so it overrides the cursor_theme line the
 * same way wallpaper.state overrides `wallpaper`. */
void cursor_state_load(syn_config_t *cfg);
void cursor_state_save(syn_server_t *s);

/* Swap the live wlr_xcursor_manager to config.cursor_theme/cursor_size. Safe to
 * call at any time; keeps the previous manager if the new one cannot be built. */
void cursor_apply(syn_server_t *s);
/* Re-read cursor.state and apply it — what `synctl dispatch cursor_reload`
 * runs, so the helper can change the pointer without a re-login. */
void cursor_reload(syn_server_t *s);

/* Rescan the icon directories into s->curpick.themes[]. */
void cursor_scan(syn_server_t *s);

void curpick_show(syn_server_t *s);
void curpick_hide(syn_server_t *s);
void curpick_toggle(syn_server_t *s);
int  curpick_total(syn_server_t *s);
/* Label + subtitle for one row; cursor.c owns the text, render.c draws it. */
void curpick_row(syn_server_t *s, int row, const char **label, const char **desc);
int  curpick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  curpick_motion(syn_server_t *s, double lx, double ly);
int  curpick_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  curpick_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_curpick(syn_server_t *s);

/* ── fontpick.c (UI font picker) ─────────────────────────────
 *
 * The other half of text.c: that made the compositor able to draw characters
 * its font lacks, this makes the font itself a choice. Same modal shape as
 * curpick above — the preview redraws the panel in the candidate font, so Esc
 * restoring is load-bearing rather than a nicety. */

/* Rescan fontconfig into s->fontpick.fonts[]. */
void fontpick_scan(syn_server_t *s);

void fontpick_show(syn_server_t *s);
void fontpick_hide(syn_server_t *s);
void fontpick_toggle(syn_server_t *s);
int  fontpick_total(syn_server_t *s);
/* Whether a terminal will render properly in this row's family — the picker
 * warns when it will not, since synui-apply-font hands the terminals whatever
 * is chosen. Row 0 (the default) is "monospace" and never warns. */
int  fontpick_row_is_mono(syn_server_t *s, int row);
/* One row's family name; fontpick.c owns the text, render.c draws it — in that
 * family's own face, which is the whole point of the list. */
void fontpick_row(syn_server_t *s, int row, const char **label);
int  fontpick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  fontpick_motion(syn_server_t *s, double lx, double ly);
int  fontpick_click(syn_server_t *s, double lx, double ly, uint32_t button,
                    uint32_t time_msec);
int  fontpick_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_fontpick(syn_server_t *s);

/* ── emoji.c (emoji picker) ──────────────────────────────────
 *
 * A GRID, and the only panel that is one — everything else here is a list. It
 * exists because text.c can now resolve a colour font per glyph; before that a
 * compositor-drawn emoji grid was a grid of question marks. */

void emoji_show(syn_server_t *s);
void emoji_hide(syn_server_t *s);
void emoji_toggle(syn_server_t *s);

/* How many cells the CURRENT view has — the recents row and a filtered slice of
 * the table are different arrays, and only emoji.c knows which is showing. */
int  emoji_total(syn_server_t *s);
/* The character at a view index, and its Unicode name. NULL / "" past the end. */
const char *emoji_at(syn_server_t *s, int i);
const char *emoji_name_at(syn_server_t *s, int i);
/* Tab labels: "Recent", "All", then one per generated block. */
const char *emoji_cat_label(int cat);
int  emoji_cat_total(void);

int  emoji_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  emoji_motion(syn_server_t *s, double lx, double ly);
int  emoji_click(syn_server_t *s, double lx, double ly, uint32_t button,
                 uint32_t time_msec);
int  emoji_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_emoji(syn_server_t *s);

/* ── Windowed panels, in one place ───────────────────────────
 *
 * Three panels, three settings and three struct members, but one set of rules —
 * so the rules live here and each panel asks rather than reimplementing them.
 * panel.c owns these.
 */

/* This panel's syn_panel_close_t. Takes the SETTING and the state together so
 * the caller never has to remember which config field goes with which panel. */
int  panel_mode(syn_server_t *s, syn_pdrag_t which);
/* Shorthands for the two questions every call site actually asks. */
int  panel_is_windowed(syn_server_t *s, syn_pdrag_t which);
int  panel_has_button(syn_server_t *s, syn_pdrag_t which);
/* The panel's window state, or NULL for SYN_PDRAG_NONE. */
syn_panel_win_t *panel_win(syn_server_t *s, syn_pdrag_t which);
/* Its geometry, so the drag can clamp against the panel it is moving. */
syn_hit_t *panel_hit(syn_server_t *s, syn_pdrag_t which);
/* Repaint it — the drag moves the panel and something has to redraw it. */
void panel_render(syn_server_t *s, syn_pdrag_t which);

/* Give this panel the keyboard and take it from the other two. Called when a
 * windowed panel is opened or clicked. */
void panel_take_kbd(syn_server_t *s, syn_pdrag_t which);
/* A click landed somewhere that is not a panel: every windowed panel gives the
 * keyboard back, and none of them close. */
void panel_drop_kbd(syn_server_t *s);
/* Does this panel currently answer for keys? Always true when it is not
 * windowed, so the modal panels are unaffected by any of this. */
int  panel_wants_keys(syn_server_t *s, syn_pdrag_t which);

/*
 * Is ANY panel on screen — modal or windowed?
 *
 * Lives in input.c beside panel_pointer_active(), because both walk the one
 * SYN_PANEL_LIST and a second hand-kept roster is the bug that list exists to
 * prevent. The difference is the windowed panels: they are excluded there
 * (a window is not modal) and included here, because a windowed task manager
 * has a monitor it lives on just as much as a modal one does.
 *
 * Asked by server_ui_output_track(): while this is true, the UI's output is
 * pinned where it was.
 */
bool panel_any_visible(syn_server_t *s);

/* Dragging, wired into input.c beside the dock and desktop-icon drags. */
void panel_drag_begin(syn_server_t *s, syn_pdrag_t which, double lx, double ly);
void panel_drag_motion(syn_server_t *s, double lx, double ly);
void panel_drag_end(syn_server_t *s);

/* Clamp a windowed panel's offset so it can never be dragged somewhere it
 * cannot be dragged back from. render.c calls this as it positions the panel. */
void panel_clamp(syn_panel_win_t *w, const struct wlr_box *ob,
                 int px, int py, int pw, int ph);

/* ── calc.c (calculator) ─────────────────────────────────────
 *
 * The second grid panel, and the only one whose grid the keyboard never walks:
 * typing goes into the expression box, and the keypad is there so the pointer
 * has something to press. See calc.c's header. */

void calc_show(syn_server_t *s);
void calc_hide(syn_server_t *s);
void calc_toggle(syn_server_t *s);

/*
 * The evaluator, and the reason this file is testable without a compositor.
 *
 * `ans` is what the identifier of that name resolves to. Returns false with
 * *err pointing at a static reason — "missing )", "division by zero",
 * "unknown name" — and leaves *out alone. A result that is not finite is a
 * failure, not an answer: that is how sqrt(-1) and 1e308*10 report themselves.
 */
bool calc_eval(const char *expr, double ans, double *out, const char **err);
/* The panel's "=" without the panel: evaluates, updates `ans` and appends to
 * the tape, so `synctl calc` and Super+X share one calculator. */
bool calc_run(syn_server_t *s, const char *expr, char *out, size_t n,
              const char **err);
/* The answer as the panel draws it: %.12g, and never "-0". */
void calc_format(double v, char *buf, size_t n);
/* Every function name the evaluator knows, space-separated — built from the
 * same table it dispatches on, so the help line cannot name one that is gone. */
const char *calc_func_hint(void);

/* The keypad. calc.c owns the labels and what each key means; render.c draws
 * them into a CALC_COLS × CALC_ROWS grid, in this order. */
int  calc_button_count(void);
const char *calc_button_label(int i);
/* An action key (del, C, copy, =) rather than something to type — render.c
 * tints those differently. */
int  calc_button_is_action(int i);

/* One tape line, row 0 being the TOP. 0 when that row is empty; the newest
 * entry is drawn LAST, nearest the expression box. */
int  calc_tape_row(syn_server_t *s, int r, const char **expr, const char **result);

int  calc_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file.
 * The wheel scrolls the TAPE rather than moving the selection — the one
 * documented divergence, argued in calc.c. */
int  calc_motion(syn_server_t *s, double lx, double ly);
int  calc_click(syn_server_t *s, double lx, double ly, uint32_t button,
                uint32_t time_msec);
int  calc_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_calc(syn_server_t *s);

/* ── eq.c (equalizer panel) ──────────────────────────────────
 *
 * The panel only. synui-eq(1) owns the PipeWire filter chain, eq.state and
 * every decision about what an equalizer is; this reads that state and asks the
 * script to change it. Never runs it synchronously — see eq.c's header. */

void eq_show(syn_server_t *s);
void eq_hide(syn_server_t *s);
void eq_toggle(syn_server_t *s);

/* Re-read eq.state if its mtime moved. Cheap, and a no-op otherwise. */
void eq_state_refresh(syn_server_t *s);

/* Rows: on/off, preset, preamp, then one per band the script declared. */
int  eq_total_rows(syn_server_t *s);
void eq_row(syn_server_t *s, int row, char *label, size_t ln,
            char *value, size_t vn);
/* 0..1 position of a slider row's value, or -1 when the row is not a slider. */
double eq_row_frac(syn_server_t *s, int row);

int  eq_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* …and the pointer, per the panel pointer contract at the top of this file. */
int  eq_motion(syn_server_t *s, double lx, double ly);
int  eq_click(syn_server_t *s, double lx, double ly, uint32_t button,
              uint32_t time_msec);
int  eq_scroll(syn_server_t *s, double lx, double ly, double delta);
void synui_render_eq(syn_server_t *s);

/* ── crop.c (image cropper) ──────────────────────────────────
 *
 * `synctl dispatch crop <path>`, Dolphin's right-click ▸ Crop Image, or the
 * `crop` bind with no argument — which opens the recent-images list instead.
 * Writes a NEW file beside the original and never touches the input. */

void crop_open(syn_server_t *s, const char *path);   /* NULL/"" → the list */
void crop_hide(syn_server_t *s);
void crop_toggle(syn_server_t *s);   /* closes, or opens the recent list */

/* The recent-images list: rebuild it, and read a row for the render. `when` is
 * a relative age ("2h ago"), `dir` the containing directory. All three point at
 * storage owned by crop.c and are valid until the next call. */
void crop_recent_scan(syn_server_t *s);
void crop_recent_row(syn_server_t *s, int i,
                     const char **name, const char **dir, const char **when);

/* Where the image lands on the output, shared by the render and the pointer so
 * the drawn image and the clickable image cannot drift apart. */
void crop_fit(syn_server_t *s, struct wlr_box *ob,
              double *scale, double *ox, double *oy);
/* The source at `scale`, ready to blit — the cache described on the struct.
 * Returns s->crop.img itself at 1:1, and NULL only if there is no image, so the
 * caller paints whatever comes back rather than choosing between two surfaces.
 * Rebuilds when the scale changes; the render is the only caller. */
cairo_surface_t *crop_scaled(syn_server_t *s, double scale);
/* The drag's two corners as a normalised rect, in image pixels. */
void crop_selection(syn_server_t *s, int *x, int *y, int *w, int *h);
int  crop_has_selection(syn_server_t *s);

int  crop_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods);
/* Press / release, per the desktop-icon drag pattern rather than the plain
 * click contract: a rectangle needs all three. */
int  crop_motion(syn_server_t *s, double lx, double ly);
int  crop_click(syn_server_t *s, double lx, double ly, uint32_t button,
                uint32_t time_msec);
int  crop_scroll(syn_server_t *s, double lx, double ly, double delta);
void crop_drag_motion(syn_server_t *s, double lx, double ly);
void crop_drag_end(syn_server_t *s, double lx, double ly);
void synui_render_crop(syn_server_t *s);

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
/* Ease-out cubic, the curve every synui animation decays on. Shared so the
 * niri strip slide (layout.c) and the fades (anim.c) settle identically —
 * two easings on one desktop read as two different desktops. */
float anim_ease_out(float t);
/* The curve config.anim_curve names, for `t` in [0,1]. anim_ease_out is the
 * ANIM_CURVE_EASE_OUT case, kept by name because the niri strip slide hard-uses
 * it — a strip that decays on a curve the windows are not using reads as two
 * animation systems, and the two routinely run in the same frame. */
float anim_curve_apply(int curve, float t);

/* A window has just mapped: config.anim_window decides whether it fades, rises
 * or is simply there. */
void anim_window_open(syn_view_t *view);
/* The two halves of a desktop switch. `dir` is the direction the desk moved in
 * — +1 for a higher-numbered workspace, -1 for a lower one, 0 when there is no
 * meaningful direction (an index jump from a pager) — and only ANIM_WS_SLIDE
 * reads it. hide() disables the node when it finishes, show() re-enables it up
 * front, so callers never sequence enable/disable around a running animation. */
void anim_workspace_hide(syn_view_t *view, int dir);
void anim_workspace_show(syn_view_t *view, int dir);
void anim_reset(syn_view_t *view);
void anim_apply_alpha(syn_view_t *view);
/* Re-assert the glass halo's place under the frame's chrome. Called by the
 * decoration pass, which lowers the border and shadow on its own schedule; a
 * no-op unless glass_halo is set. See anim.c. */
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

/* ── Start-button setting (launcher.c) ───────────────────── */
/* The button is drawn by the bar (quickshell/modules/Launcher.qml); only the
 * setting is the compositor's. The toggle's write to launcher.state is what the
 * bar watches, so it is the update signal and not merely persistence. */
void launcher_toggle_style(syn_server_t *s);       /* flip text↔logo, persist */
void launcher_state_load(syn_config_t *cfg);       /* lay launcher.state over synuirc */

/* ── Recording audio setting (record.c) ──────────────────── */
/* Whether Super+Shift+R records desktop sound as well. The capture itself is
 * synui-record's; this is only the switch, and it persists like the launcher's
 * so a reload cannot undo it. */
void record_audio_toggle(syn_server_t *s);         /* flip on↔off, persist */
void record_edit_toggle(syn_server_t *s);          /* flip mezzanine on↔off, persist */
void record_state_load(syn_config_t *cfg);         /* lay record.state over synuirc */

/* ── settings.state (settings.c) ─────────────────────────────
 *
 * The control panel's persistence, and the general case of the seven
 * per-subject `.state` files above: `key = value` in synuirc's own syntax,
 * applied after synuirc so it overrides it, and read back through the very
 * parser synuirc uses (config_parse_kv). Only keys the user has changed are
 * stored, so an untouched setting still tracks the default.
 */
void settings_state_load(syn_config_t *cfg);            /* lay it over synuirc */
void settings_state_set(const char *key, const char *val);  /* set + rewrite   */
void settings_state_clear(const char *key);             /* forget = use default */
int  settings_state_has(const char *key);               /* is it overridden?   */

/* One `key = value` out of synuirc, applied to cfg. Exposed so settings.state
 * is read by the same code — see settings.c. `val` is mutable: the bind case
 * splits it in place. */
void config_parse_kv(syn_config_t *cfg, const char *key, char *val);

/* The defaults as a value, for "is this row still at its default?" and for the
 * reset that puts it back. Built by the same code that seeds a real config. */
const syn_config_t *synui_config_defaults(void);
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

/* deskmenu.c — the desktop right-click menu and the optional ~/Desktop icons.
 * Same pointer-driven, modal-while-open contract as the dock menu above. */
void deskmenu_open(syn_server_t *s, double lx, double ly);
void deskmenu_motion(syn_server_t *s, double lx, double ly);
void deskmenu_click(syn_server_t *s, double lx, double ly);
void deskmenu_close(syn_server_t *s);
const char *deskact_label(syn_deskact_t a);
/* Row geometry, shared with render.c so both walk the rows the same way. */
int  deskmenu_row_top(syn_server_t *s, int i);
int  deskmenu_row_height(syn_server_t *s, int i);
/* Does row i show a checkmark? The rows that are settings rather than actions
 * (icons on/off, the arrange mode) draw one when they are the state we are
 * already in; render.c asks rather than re-deriving the rule. */
bool deskmenu_row_checked(syn_server_t *s, int i);

void deskicons_reload(syn_server_t *s);   /* rescan ~/Desktop */
void deskicons_layout(syn_server_t *s);   /* re-grid onto the primary output */
/* Lay deskicons.state's `icons=` over synuirc's desktop_icons, so the menu's
 * toggle outlives a config reload and a logout. The dragged cells and the
 * arrange mode are read separately, inside deskicons_reload. */
void deskicons_state_load(syn_config_t *cfg);
/* Re-sort the desktop into `mode`. Re-flows everything, dragged icons
 * included: a sort that left half the desktop where it was would not be one. */
void deskicons_arrange(syn_server_t *s, syn_arrange_t mode);
int  deskicon_at(syn_server_t *s, double lx, double ly);
void deskicon_activate(syn_server_t *s, int i);
void deskicon_select(syn_server_t *s, int i);

/* Drag an icon to a new cell. begin() arms on a press; motion() floats the
 * icon under the cursor once the slop is crossed; end() snaps it to the
 * nearest cell and persists every dragged icon to deskicons.state. */
/* Delete on the desktop: the selected icon's file goes to the XDG trash via
 * `gio trash`, never unlink(). Recoverable, like Delete anywhere else. */
void deskicon_trash_selected(syn_server_t *s);

/* ── deskdrag.c — the desktop as a drag SOURCE ──────────────────────────────
 * Dragging an icon off the desktop and into a window. The compositor owns the
 * file, so the compositor is the wl_data_source; a drag out is always a COPY.
 * Promoted from the reposition gesture the moment the cursor leaves the
 * desktop, which is what keeps moving an icon to a cell unchanged. */
bool deskdrag_start(syn_server_t *s, const char *path);
/* So deskdrop.c does not accept our own drag back onto the desktop and copy a
 * file on top of itself. */
bool deskdrag_is_ours(struct wlr_data_source *source);
/* file:// URI for a path, percent-encoded. Exposed for the test. */
bool deskdrag_uri_for(const char *path, char *out, size_t n);

void deskicon_drag_begin(syn_server_t *s, int idx, double lx, double ly);
void deskicon_drag_motion(syn_server_t *s, double lx, double ly);
void deskicon_drag_end(syn_server_t *s, double lx, double ly);

/* Pin freshly-arrived files (basenames, as they now exist in ~/Desktop) at the
 * point they were dropped, first one on the cell under the cursor and the rest
 * in the free cells after it. Call AFTER deskicons_reload(), or the model has
 * no icons by these names to place. Persists, like any other drop. */
void deskicons_place_dropped(syn_server_t *s, const char *const *names, int n,
                             int lx, int ly);

/* ── deskdrop.c (a client's drag-and-drop, dropped on the desktop) ──
 * The compositor is the drop target here — the desktop is wallpaper plus a
 * cairo buffer, not a wl_surface, so there is nothing for wlroots to deliver a
 * drop to and it talks to the drag's data source directly. hover() is called
 * on every motion during a drag and answers accept/refuse; take() claims the
 * release, reads the uri-list and copies the files in. See deskdrop.c for why
 * the release must be intercepted before wlroots sees it. */
void deskdrop_hover(syn_server_t *s, bool over_desktop);
bool deskdrop_take(syn_server_t *s, double lx, double ly);
/* The drag ended some other way (dropped on a client, cancelled): forget any
 * acceptance, so the next drag starts from a clean answer. */
void deskdrop_reset(syn_server_t *s);
/* Shutdown: abandon any transfer still in flight. */
void deskdrop_finish(syn_server_t *s);

void synui_render_deskmenu(syn_server_t *s);
void synui_render_deskicons(syn_server_t *s);
/* Move the drag layer to the dragged icon's current position. No repaint —
 * this is the per-motion-event path of a drag. */
void synui_move_deskicon_drag(syn_server_t *s);

/* icons.c: resolve a .desktop file we have the path of (rather than an
 * app_id). NULL if it has no runnable Exec=. */
const syn_icon_entry_t *icon_lookup_desktop_path(const char *path);

/* Launch a shell command (fork/exec); exposed for dock launches. */
void synui_spawn(const char *cmd);

/* ── spawntoggle.c ───────────────────────────────────────────
 *
 * `spawn` is fire-and-forget and right for a terminal: the second press of that
 * bind is meant to give you a second terminal. A launcher is the other case —
 * the key that opens it should put it away, the way every panel bind in synui
 * already does. That is the `spawn_toggle <cmd>` bind action. */
pid_t synui_spawn_pid(const char *cmd);          /* fork + setsid + sh -c */
void  synui_spawn_toggle(const char *cmd);       /* open it, or close the one up */
pid_t synui_spawn_toggle_pid(const char *cmd);   /* live pid for cmd, or 0 */
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
