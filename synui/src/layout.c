/*
 * layout.c — SynapseOS window layout engine
 *
 * Implements six layout modes:
 *
 *   TILING   — Master-stack tiling (dwm-style)
 *              First window is master (left, 60% width).
 *              Remaining windows stack right.
 *
 *   SPIRAL   — Fibonacci spiral tiling. Each window takes half of what
 *              is left, the split alternating vertical/horizontal and
 *              the side rotating clockwise, so the windows wind inward.
 *              It trades AREA for SHAPE against TILING: the smallest
 *              window ends up smaller, but nothing ever becomes the
 *              letterbox strip a master-stack column degenerates into.
 *              See the section comment above layout_spiral().
 *
 *   NIRI     — Scrollable tiling (niri-style). One endless horizontal
 *              strip of columns per monitor, scrolled so the focused
 *              column is on screen; a new window never shrinks the
 *              others, it extends the strip. Windows stack vertically
 *              inside a column (Super+, / Super+. to move them in and
 *              out). The strip SLIDES to its new scroll position rather
 *              than jumping — see layout_scroll_tick(), and the section
 *              comment above layout_niri().
 *
 *   MONOCLE  — One window per output, filling that output's usable
 *              box; the rest of its windows are hidden. Which one is
 *              whichever has focus, so Alt+Tab and Super+J/K change it
 *              (focus_view reflows the desktop for exactly this).
 *              Floating windows are exempt and stay on top.
 *
 *   FLOATING — You place the windows; the compositor places the ones
 *              you haven't. An inset grid that deliberately does not
 *              fill the screen (layout_float_arrange), skipped for any
 *              window that has been dragged or resized by hand.
 *
 *   AI       — Ask synapd to suggest positions based on
 *              workspace intent + running apps. If AI is
 *              unavailable, falls back to TILING.
 *
 * Border width: 2px (configurable).
 * Gap between windows: 8px.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "synui.h"

/* Same clock the fades run on (anim.c). The strip slide and a cross-fade
 * routinely start in the same frame — a desktop switch does both — so they have
 * to be measured against one timebase or they visibly finish apart. */
static double now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* border width and gap come from s->config (synuirc `border_width`/`gap`,
 * live-reloadable via SIGHUP) */
#define MASTER_FACTOR  0.60f   /* default master column width fraction */
#define MASTER_MIN     0.10f
#define MASTER_MAX     0.90f
/* MIN_WIN lives in synui.h — geom_persist.c needs the same floor so it never
 * records a box this code would only have to clamp back up again. */

/* ── Get output geometry for a view ──────────────────────── */
/* A window is laid out on the monitor it lives on (falling back to the focused
 * output if it has none yet), minus any layer-shell exclusive zones so tiling
 * doesn't cover panels/bars. */
static void get_view_geom(syn_server_t *s, syn_view_t *view,
                          struct wlr_box *out)
{
    syn_output_t *o = (view && view->output) ? view->output
                                            : server_focused_output(s);
    output_usable_box_of(s, o, out);
}

/* ── Count mapped windows of ws on one output ────────────── */
static int count_windows(syn_workspace_t *ws, syn_output_t *o)
{
    int n = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped && !v->floating && !v->fullscreen && !v->minimized &&
            v->output == o)
            n++;
    return n;
}

/* ── Place / size a view ─────────────────────────────────── */
/* Public so input.c (interactive move/resize) reuses the same path. */
void view_resize(syn_view_t *view, int x, int y, int w, int h)
{
    view->x = x;
    view->y = y;
    view->w = w;
    view->h = h;

    /* x/y/w/h is the *frame*; the client gets what's left inside the border and
     * below the titlebar. */
    struct wlr_box c;
    view_content_box(view, &c);

    /* Record what the client is being asked for, and reset the heal budget
     * whenever that target changes — view_heal_size() re-sends this configure a
     * bounded number of times if the client settles on a different size. */
    if (c.width != view->cfg_w || c.height != view->cfg_h) {
        view->cfg_w = c.width;
        view->cfg_h = c.height;
        view->heal_tries = 0;
    }

    /* Commit the size to the client. X11 clients also need their absolute
     * layout position, which the xdg path derives from the scene node. */
    if (view->is_xwayland)
        wlr_xwayland_surface_configure(view->xsurface, c.x, c.y,
                                       c.width, c.height);
    else
        wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel,
                                  c.width, c.height);

    /* Move the frame; the client surface sits at its content offset *inside*
     * the frame, so the chrome travels with the window for free. */
    wlr_scene_node_set_position(view_node(view), x, y);
    if (view->frame)
        wlr_scene_node_set_position(&view->scene_tree->node, c.x - x, c.y - y);

    view_update_decorations(view);
}
#define place_view(v, x, y, w, h) view_resize((v), (x), (y), (w), (h))

/* ── Move without resizing ───────────────────────────────── */
/*
 * The same frame move view_resize does, minus the configure.
 *
 * A client is never told where it is — xdg-shell has no concept of a window
 * position at all, and an X11 client only needs one when it has stopped moving
 * (menus place themselves from the last configure, which is what
 * project_synui_x11_move_stale_root_position is about). So a movement that
 * changes only x/y can be driven at frame rate for free, and that is the whole
 * reason the niri strip can slide: layout_scroll_tick calls this per frame and
 * layout_apply sends the one authoritative configure when the slide settles.
 *
 * process_cursor_move has always done exactly this inline for interactive
 * drags, for exactly this reason. This is that, named, so the slide and the
 * drag cannot drift apart — in particular so both keep remembering to move the
 * client subtree with the frame (a decorated window's surface sits at a content
 * offset INSIDE the frame node, and forgetting it leaves the titlebar behind).
 */
void view_move(syn_view_t *view, int x, int y)
{
    if (view->x == x && view->y == y) return;
    view->x = x;
    view->y = y;

    struct wlr_box c;
    view_content_box(view, &c);

    wlr_scene_node_set_position(view_node(view), x, y);
    if (view->frame)
        wlr_scene_node_set_position(&view->scene_tree->node, c.x - x, c.y - y);

    view_update_decorations(view);
}

/* What a layout is called when a HUMAN reads it — the Super+Tab toast, the
 * control panel's Layout row, the AI overlay's context line. All three used to
 * spell the list out for themselves, and the control panel was about to be the
 * fourth.
 *
 * NOT the same list as ipc.c's layout_name(), and they must not be merged: that
 * one is the wire value `synctl` and the test rigs parse, so it is lowercase
 * "ai" and changing it is an API break. This one is for reading, so it is "AI".
 * Two audiences, two functions, on purpose. */
const char *layout_label(syn_layout_t l)
{
    switch (l) {
    case LAYOUT_TILING:   return "tiling";
    case LAYOUT_FLOATING: return "floating";
    case LAYOUT_MONOCLE:  return "monocle";
    case LAYOUT_AI:       return "AI";
    case LAYOUT_NIRI:     return "niri";
    case LAYOUT_SPIRAL:   return "spiral";
    case LAYOUT_CASCADE:  return "cascade";
    }
    return "unknown";
}

/* ── Persisted layout choice ─────────────────────────────── */
/*
 * The layout is a per-desktop *setting*, not session scratch. Every desktop
 * used to start at LAYOUT_TILING no matter what was chosen last, so a desk
 * left on floating came back tiling on the next login and had to be cycled
 * round again — every restart, by hand.
 *
 * Written on every change (there are only two places that assign ws->layout:
 * layout_cycle, and the retile that switches a floating desktop), so a crash
 * or a kill -9 keeps the choice; read once at startup, over the default.
 *
 * master_factor is deliberately NOT in this file: it is per-desktop too, but
 * it already has a synuirc default (`master_factor`) that a state file would
 * start shadowing for every desktop the moment anyone touched Super+H once.
 * If it is ever added here, it needs a "never set" marker, not a value.
 */
static bool layout_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "layouts.state");
}

/* The spelling on disk is the lowercase `synctl` wire vocabulary, not
 * layout_label()'s "AI". A state file is a format: it must not change spelling
 * because a toast was restyled, which is exactly the trap layout_label's
 * comment above warns about. Third audience, third switch, on purpose. */
static const char *layout_key(syn_layout_t l)
{
    switch (l) {
    case LAYOUT_TILING:   return "tiling";
    case LAYOUT_FLOATING: return "floating";
    case LAYOUT_MONOCLE:  return "monocle";
    case LAYOUT_AI:       return "ai";
    case LAYOUT_NIRI:     return "niri";
    case LAYOUT_SPIRAL:   return "spiral";
    case LAYOUT_CASCADE:  return "cascade";
    }
    return "tiling";
}

void layout_state_save(syn_server_t *s)
{
    char path[256];
    if (!layout_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: layout: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    /* Every desktop, every time — the file is nine lines and one desktop's
     * layout is not independent of the others as far as the user is concerned
     * ("my desks are how I left them" or they are not). */
    for (int i = 0; i < WORKSPACE_MAX; i++)
        fprintf(f, "desktop%d=%s\n", i + 1, layout_key(s->workspaces[i].layout));
    fclose(f);
}

/* Applied over the LAYOUT_TILING seeded in server init. An absent file is not
 * an error — it means "never changed one", and every desktop keeps the
 * default. So does a line with a value this build doesn't know. */
void layout_state_load(syn_server_t *s)
{
    char path[256];
    if (!layout_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;
        if (strncmp(key, "desktop", 7) != 0) continue;

        /* 1-based on disk, the way the toast, the control panel and Super+1-9
         * all count desktops. */
        int idx = atoi(key + 7) - 1;
        if (idx < 0 || idx >= WORKSPACE_MAX) continue;

        syn_layout_t l;
        if      (strcmp(val, "tiling")   == 0) l = LAYOUT_TILING;
        else if (strcmp(val, "floating") == 0) l = LAYOUT_FLOATING;
        else if (strcmp(val, "monocle")  == 0) l = LAYOUT_MONOCLE;
        else if (strcmp(val, "ai")       == 0) l = LAYOUT_AI;
        else if (strcmp(val, "niri")     == 0) l = LAYOUT_NIRI;
        else if (strcmp(val, "spiral")   == 0) l = LAYOUT_SPIRAL;
        else if (strcmp(val, "cascade")  == 0) l = LAYOUT_CASCADE;
        else continue;

        s->workspaces[idx].layout = l;
    }
    fclose(f);
}

/* ── TILING layout (master-stack) ────────────────────────── */
/* Tiles only the windows of ws that live on o, into o's usable box. */
void layout_tile(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    /* Apply outer gap. Clamp the working area: a large configured gap on a
     * small output must not go negative — negative sizes would flow into
     * scene rects and client configures. */
    int gap = s->config.gap;
    int x = area.x + gap;
    int y = area.y + gap;
    int W = area.width  - 2 * gap;
    int H = area.height - 2 * gap;
    if (W < MIN_WIN) { x = area.x; W = area.width  > MIN_WIN ? area.width  : MIN_WIN; }
    if (H < MIN_WIN) { y = area.y; H = area.height > MIN_WIN ? area.height : MIN_WIN; }

    int n = count_windows(ws, o);
    if (n == 0) return;

    float mf = ws->master_factor;
    if (mf < MASTER_MIN || mf > MASTER_MAX) mf = MASTER_FACTOR;
    int master_w = (n == 1) ? W : (int)(W * mf) - gap / 2;
    if (master_w < MIN_WIN) master_w = MIN_WIN;
    int stack_w  = W - master_w - gap;
    if (stack_w < MIN_WIN) stack_w = MIN_WIN;
    int stack_x  = x + master_w + gap;

    int i = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->fullscreen || v->minimized) continue;
        if (v->output != o) continue;

        if (i == 0) {
            /* Master */
            place_view(v, x, y, master_w, H);
        } else {
            /* Stack */
            int nstack = n - 1;
            int slot_h = (H - (nstack - 1) * gap) / nstack;
            if (slot_h < MIN_WIN) slot_h = MIN_WIN;
            int vy = y + (i - 1) * (slot_h + gap);
            place_view(v, stack_x, vy, stack_w, slot_h);
        }
        i++;
    }
}

/* ── SPIRAL layout (fibonacci) ───────────────────────────── */
/*
 * Each window takes half of whatever box is left, and the half it takes rotates
 * clockwise: left, top, right, bottom, left again. The last window gets the
 * whole remainder, so nothing is ever left over.
 *
 *   ┌───────┬───────┐   ┌───────┬───────┐   ┌───────┬───────┐
 *   │       │       │   │       │   2   │   │       │   2   │
 *   │   1   │   2   │   │   1   ├───────┤   │   1   ├───┬───┤
 *   │       │       │   │       │   3   │   │       │ 4 │ 3 │
 *   └───────┴───────┘   └───────┴───────┘   └───────┴───┴───┘
 *      two windows         three windows        four windows
 *
 * Why have this as well as layout_tile: they only differ once the desktop is
 * busy, and the difference is one of SHAPE.
 *
 * Master-stack puts every window after the first into one column, so each new
 * window makes that column's windows shorter without making them any narrower.
 * On a 1280x720 screen its stack slot goes 502x170 at five windows, 502x134 at
 * six, 502x81 at nine — 2.9:1, then 3.7:1, then 6.2:1. A letterbox is not a
 * shape an application is designed to draw in. The spiral halves an ever
 * smaller box instead and alternates the direction of the cut, so every window
 * stays between about 1:1 and 2:1 however many are open.
 *
 * That is a TRADE and it is worth stating plainly, because the obvious guess is
 * the wrong way round: the spiral's SMALLEST window is smaller than
 * master-stack's, not bigger — 52700 px² against 85340 at five windows, because
 * halving compounds where dividing a column does not. What it buys is that
 * nothing ever becomes a strip. Pick the layout for the shape, not the area;
 * spiral_layout.sh asserts exactly this, and asserting the area would fail.
 *
 * ORDER IS THE WORKSPACE LIST, like every other layout here, so Super+Shift+J/K
 * (layout_move_in_stack) rotates windows through the spiral with no second
 * ordering to keep in sync, and master_factor is deliberately not consulted —
 * there is no master.
 *
 * THE DEGENERATE CASE. Halving cannot continue forever: past about eight
 * windows on a 1080p monitor a half is below MIN_WIN, and a spiral that kept
 * dividing would hand out boxes smaller than a window can be, which the clamp
 * would then quietly overlap on top of each other. So the moment a split would
 * leave either side under MIN_WIN, the remaining windows are stacked evenly in
 * what is left and the spiral stops. That is a visible, describable layout
 * ("the tail ends up as a stack") rather than a pile.
 */
void layout_spiral(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    /* Same outer-gap clamp as layout_tile: a big gap on a small output must not
     * hand negative sizes to scene rects and client configures. */
    int gap = s->config.gap;
    int x = area.x + gap;
    int y = area.y + gap;
    int W = area.width  - 2 * gap;
    int H = area.height - 2 * gap;
    if (W < MIN_WIN) { x = area.x; W = area.width  > MIN_WIN ? area.width  : MIN_WIN; }
    if (H < MIN_WIN) { y = area.y; H = area.height > MIN_WIN ? area.height : MIN_WIN; }

    int n = count_windows(ws, o);
    if (n == 0) return;

    /* The box still to be divided. */
    int rx = x, ry = y, rw = W, rh = H;

    int i = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->fullscreen || v->minimized) continue;
        if (v->output != o) continue;

        int left = n - i;              /* this window included */
        if (left == 1) {               /* last one takes the remainder */
            place_view(v, rx, ry, rw, rh);
            break;
        }

        /* Which way this window's cut runs. Even steps split the box side by
         * side, odd steps split it top and bottom; the step number mod 4 says
         * which of the two halves this window gets, and that is what makes it
         * wind rather than march. */
        bool vertical = (i % 2) == 0;
        int  half = vertical ? (rw - gap) / 2 : (rh - gap) / 2;
        int  rest = (vertical ? rw : rh) - half - gap;

        /* Out of room to keep halving: stack everything left over, evenly, in
         * the box we still have. Bounded by MIN_WIN like every other slot here,
         * so a desktop with more windows than pixels overlaps at the floor
         * instead of going negative. */
        if (half < MIN_WIN || rest < MIN_WIN) {
            int slot_h = (rh - (left - 1) * gap) / left;
            if (slot_h < MIN_WIN) slot_h = MIN_WIN;
            int k = 0;
            bool reached = false;
            syn_view_t *m;
            wl_list_for_each(m, &ws->windows, link) {
                if (!m->mapped || m->floating || m->fullscreen || m->minimized)
                    continue;
                if (m->output != o) continue;
                if (m == v) reached = true;
                if (!reached) continue;    /* already placed, back up the spiral */
                place_view(m, rx, ry + k * (slot_h + gap), rw, slot_h);
                k++;
            }
            break;
        }

        switch (i % 4) {
        case 0:   /* left half; the remainder is to the right */
            place_view(v, rx, ry, half, rh);
            rx += half + gap;
            rw  = rest;
            break;
        case 1:   /* top half; the remainder is below */
            place_view(v, rx, ry, rw, half);
            ry += half + gap;
            rh  = rest;
            break;
        case 2:   /* right half; the remainder is to the left */
            place_view(v, rx + rw - half, ry, half, rh);
            rw  = rest;
            break;
        default:  /* bottom half; the remainder is above */
            place_view(v, rx, ry + rh - half, rw, half);
            rh  = rest;
            break;
        }
        i++;
    }
}

/* ── CASCADE layout ──────────────────────────────────────── */
/*
 * Windows overlapping, each offset down-and-right from the one behind it, so
 * every titlebar stays reachable and the front one is whole. A hand of cards.
 *
 *   ┌──────────┐        ┌────────┐     ┌────────┐
 *   │ 1        │        │ 1      │     │ 4      │
 *   │ ┌──────────┐      │ ┌────────┐   │ ┌────────┐
 *   │ │ 2        │      │ │ 2      │   │ │ 5      │
 *   └─│ ┌──────────┐    └─│ ┌────────┐ └─│ ┌────────┐
 *     │ │ 3        │      │ │ 3      │   │ │ 6      │
 *     └─│          │      └─│        │   └─│        │
 *       └──────────┘        └────────┘     └────────┘
 *        three windows          six windows, two piles
 *
 * THE SPLIT IS THE WHOLE FEATURE. A plain cascade is fine for four windows and
 * useless for fourteen: the offsets accumulate, so the last window is a sliver
 * in the bottom-right corner and everything before it is a stack of titlebars.
 * Every cascade ever shipped has this problem. Past `cascade_stack_max` the
 * pile splits and deals a second one beside it, and the split is BALANCED —
 * twelve windows at a max of five is three piles of four, not 5+5+2.
 *
 * Why a window COUNT rather than something derived from the geometry: the limit
 * is not "when does it stop fitting", it is "when does it stop being readable".
 * A 1440p screen has room for eighteen 30px offsets and nobody can use a pile
 * of eighteen windows. Five is about a hand of cards.
 *
 * The step comes from the titlebar height, so what peeks out from behind each
 * window is exactly the thing that says which window it is. With titlebars off
 * there is nothing to reveal, so it falls back to a fixed CASCADE_STEP_MIN
 * fringe rather than to the border width, which would stack the windows
 * essentially on top of each other.
 *
 * Windows are UNIFORM within a run of the desktop — every card the same size,
 * including the ones in a short last pile. A pile of two whose cards were
 * bigger than the pile of five beside it would read as two arrangements.
 */
void layout_cascade(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    /* Same outer-gap clamp as layout_tile: a big gap on a small output must not
     * drive the working area negative. */
    int gap = s->config.gap;
    int x = area.x + gap;
    int y = area.y + gap;
    int W = area.width  - 2 * gap;
    int H = area.height - 2 * gap;
    if (W < MIN_WIN) { x = area.x; W = area.width  > MIN_WIN ? area.width  : MIN_WIN; }
    if (H < MIN_WIN) { y = area.y; H = area.height > MIN_WIN ? area.height : MIN_WIN; }

    int n = count_windows(ws, o);
    if (n == 0) return;

    /* What peeks out from behind each card. The titlebar, when there is one. */
    int step = s->config.titlebar_height + s->config.border_width;
    if (step < CASCADE_STEP_MIN) step = CASCADE_STEP_MIN;

    int per_max = s->config.cascade_stack_max;
    if (per_max < CASCADE_STACK_MIN) per_max = CASCADE_STACK_MIN;
    if (per_max > CASCADE_STACK_MAX) per_max = CASCADE_STACK_MAX;

    /* Balance the split. `stacks` from the cap, then `per` back out of
     * `stacks` — the round trip is what turns 12-at-5 into 4+4+4 instead of
     * 5+5+2, and a lopsided last pile is the thing that makes an arrangement
     * look like it ran out rather than like it finished. */
    int stacks = (n + per_max - 1) / per_max;
    if (stacks < 1) stacks = 1;
    int per = (n + stacks - 1) / stacks;
    if (per < 1) per = 1;

    /* A pile can also be capped by the SCREEN rather than by the count, and it
     * has to be: on a 1024x768 VM (the ISO's default) five 40px steps eat more
     * than half the height, and the cards come out shorter than they are wide.
     * Shrink the pile until each card keeps at least half the working box, then
     * re-derive the stacks — never the other way round, or the last pile is
     * lopsided again. */
    int per_fit = 1 + (H / 2) / step;
    if (per_fit < 1) per_fit = 1;
    if (per > per_fit) {
        per    = per_fit;
        stacks = (n + per - 1) / per;
        per    = (n + stacks - 1) / stacks;
    }

    int col_w = (W - (stacks - 1) * gap) / stacks;
    if (col_w < MIN_WIN) col_w = MIN_WIN;

    int win_w = col_w - (per - 1) * step;
    int win_h = H     - (per - 1) * step;
    if (win_w < MIN_WIN) win_w = MIN_WIN;
    if (win_h < MIN_WIN) win_h = MIN_WIN;

    int i = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->fullscreen || v->minimized) continue;
        if (v->output != o) continue;

        int stack = i / per;      /* which pile */
        int depth = i % per;      /* how far into it — 0 is the back card */

        place_view(v,
                   x + stack * (col_w + gap) + depth * step,
                   y + depth * step,
                   win_w, win_h);

        /* Stacking order IS the arrangement here — this is the one layout whose
         * windows overlap, so a card placed lower-right but drawn behind the
         * one above it would hide the very thing the offset exposes. Raising in
         * list order leaves each pile's front card on top.
         *
         * This does NOT fight focus. layout_apply() is not run on focus changes
         * (only monocle asks for that), so a window raised by focus_view stays
         * raised — which is exactly the card-pulled-out-of-the-pile behaviour
         * you want, since its titlebar has not moved. */
        wlr_scene_node_raise_to_top(view_node(v));
        i++;
    }
}

/* ── MONOCLE layout ──────────────────────────────────────── */
/* Each monitor showing ws gets its own top window — monocle is per-output, so
 * a 3-monitor desktop still shows three windows, one filling each screen. */
void layout_monocle(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    /* Show exactly one window *of this output*: the focused view if it lives
     * here, else the first mapped one. Keying off the global focused view
     * would blank the monitor whenever focus sits on another output (or on
     * nothing, right after a close). */
    syn_view_t *top = NULL;
    syn_view_t *v;
    if (s->focused_view && s->focused_view->workspace == ws &&
        s->focused_view->output == o &&
        s->focused_view->mapped && !s->focused_view->floating &&
        !s->focused_view->minimized)
        top = s->focused_view;
    else
        wl_list_for_each(v, &ws->windows, link)
            if (v->mapped && !v->floating && !v->minimized && v->output == o) {
                top = v;
                break;
            }

    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->minimized) continue;
        if (v->output != o) continue;
        /* Only configure a window whose box actually moved. Every window here
         * is already at the same full-usable-box, and focus_view() now reflows
         * a monocle desktop on every focus change — without the compare that
         * would send each client a redundant configure per Alt+Tab press, per
         * click, per Super+J. Same guard, same reason, as the maximize re-fit
         * in layout_apply. */
        if (v->x != area.x || v->y != area.y ||
            v->w != area.width || v->h != area.height)
            place_view(v,
                       area.x, area.y,
                       area.width, area.height);
        wlr_scene_node_set_enabled(view_node(v), v == top);
    }
}

/* ── NIRI layout (scrollable tiling) ─────────────────────── */
/*
 * The niri model, in this codebase's terms.
 *
 * Each (desktop, monitor) pair holds one endless horizontal STRIP of COLUMNS.
 * A column is one or more windows stacked vertically, sharing a width; the
 * strip is as wide as its columns need and is scrolled sideways so the focused
 * column is on screen. Nothing is ever resized to make a new window fit —
 * that is the whole point of the layout, and the difference between it and
 * layout_tile above, where every extra window shrinks the stack.
 *
 * THE STRIP IS THE WORKSPACE LIST. There is no second data structure: the
 * order is ws->windows read front to back (filtered to this output), and a
 * window with col_join set belongs to the column of the window before it. That
 * is worth a sentence because the alternative — a real column tree — would need
 * every path that already touches the list (map, unmap, workspace_move_view,
 * view_set_output, layout_move_in_stack, layout_reclaim) taught about it, and
 * each one is a chance to leave a column holding a freed view. This way
 * Super+Shift+J/K moves a window along the strip and in and out of columns for
 * free, and a window that unmaps simply stops being iterated.
 *
 * WHAT IS DRAWN. A column is shown only if it fits ENTIRELY in the visible box.
 * niri proper lets the columns either side peek in at the edges; synui cannot,
 * because a window is placed by moving its scene node in *layout* coordinates
 * and the compositor has no clip on the frame — the borders and titlebar are
 * separate scene rects outside client_tree, so a half-off column would not be
 * cropped at the monitor edge, it would be drawn across whatever monitor sits
 * next to it. Hiding the partial column is the honest version of that, and the
 * scroll rule below guarantees the focused one is never the hidden one.
 */
/* Default column width: half the screen, so two columns sit side by side.
 * A fraction of the SLOT, not of the bare viewport — see niri_col_width(). */
#define NIRI_COL_FRAC  0.50f
#define NIRI_FRAC_MIN  0.10f
#define NIRI_FRAC_MAX  1.00f

/* Windows the strip is made of: the same test the other layouts tile by. */
static bool niri_tileable(syn_view_t *v, syn_output_t *o)
{
    return v->mapped && !v->floating && !v->fullscreen && !v->minimized &&
           v->output == o;
}

/* The next strip member after `v`, or the first one when v is NULL. */
static syn_view_t *niri_next(syn_workspace_t *ws, syn_output_t *o, syn_view_t *v)
{
    struct wl_list *n = v ? v->link.next : ws->windows.next;
    while (n != &ws->windows) {
        syn_view_t *c = wl_container_of(n, c, link);
        if (niri_tileable(c, o)) return c;
        n = n->next;
    }
    return NULL;
}

/* A column's width in pixels, from its leader.
 *
 * The fraction is of the SLOT — the column plus the gap that follows it — not
 * of the bare viewport, so that 1/n of the screen means n columns fit on it
 * exactly. Taking the naive `viewport * f` instead costs a gap per column, and
 * at the default 0.5 that is enough to push the second column a few pixels past
 * the right edge, where the "fits entirely" rule hides it: a two-thirds-empty
 * screen showing one window, on the layout whose entire point is showing
 * several. layout_tile has the same subtraction on its master column for the
 * same reason.
 *
 * Clamped to the viewport at the top end: a column wider than the screen could
 * never fit entirely and so would be invisible at every scroll position. */
static int niri_col_width(syn_view_t *lead, int viewport_w, int gap)
{
    float f = lead->col_frac;
    if (f < NIRI_FRAC_MIN || f > NIRI_FRAC_MAX) f = NIRI_COL_FRAC;
    int w = (int)((viewport_w + gap) * f) - gap;
    if (w > viewport_w) w = viewport_w;
    if (w < MIN_WIN)    w = MIN_WIN;
    return w;
}

/* The window that leads `view`'s column — `view` itself unless it has joined
 * one. Found by walking forward from the head of the strip rather than
 * backwards from the view, because col_join is positional: it says "the one
 * before me", which only the forward order can resolve. NULL if the view is not
 * on a strip at all. */
static syn_view_t *niri_column_lead(syn_workspace_t *ws, syn_view_t *view)
{
    syn_view_t *lead = NULL;
    for (syn_view_t *v = niri_next(ws, view->output, NULL); v;
         v = niri_next(ws, view->output, v)) {
        if (!lead || !v->col_join) lead = v;
        if (v == view) return lead;
    }
    return NULL;
}

/* Walk `ws`'s strip on `o` one column at a time. Both passes below use this so
 * the measure and the placement cannot disagree about where a column starts or
 * who is in it: on entry *lead is the column's first window, and on return
 * *n_members is how many windows it holds and the value is the NEXT column's
 * leader (NULL at the end of the strip). */
static syn_view_t *niri_column_end(syn_workspace_t *ws, syn_output_t *o,
                                   syn_view_t *lead, int *n_members)
{
    int n = 1;
    syn_view_t *m = niri_next(ws, o, lead);
    /* The first window of a strip is a leader whatever its flag says — it has
     * nothing to its left to join. */
    while (m && m->col_join) {
        n++;
        m = niri_next(ws, o, m);
    }
    *n_members = n;
    return m;
}

/* The box the strip is laid out in: the usable box less the outer gap, with the
 * same clamp layout_tile applies (a big gap on a small output must not hand
 * negative sizes to scene rects and client configures).
 *
 * Factored out because THREE passes need to agree on it exactly — the measure,
 * the placement, and the per-frame slide. Two of those run in different
 * functions at different times, and a viewport computed twice is a viewport
 * that can be computed differently once. */
static void niri_viewport(syn_server_t *s, syn_output_t *o,
                          struct wlr_box *vp, int *gap_out)
{
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    int gap = s->config.gap;
    vp->x      = area.x + gap;
    vp->y      = area.y + gap;
    vp->width  = area.width  - 2 * gap;
    vp->height = area.height - 2 * gap;
    if (vp->width < MIN_WIN) {
        vp->x = area.x;
        vp->width = area.width > MIN_WIN ? area.width : MIN_WIN;
    }
    if (vp->height < MIN_WIN) {
        vp->y = area.y;
        vp->height = area.height > MIN_WIN ? area.height : MIN_WIN;
    }
    *gap_out = gap;
}

/*
 * Would drawing a column at `vx` spill onto ANOTHER MONITOR?
 *
 * This is the question the layout's header note is really about. A window is
 * placed by moving its scene node in *layout* coordinates and there is no clip
 * on the frame — the borders and titlebar are separate scene rects outside
 * client_tree — so a column hanging off this monitor's edge is not cropped
 * there. It is simply drawn at those coordinates, and if a monitor happens to
 * occupy them, that is where it appears.
 *
 * But "hangs off the edge" and "lands on another monitor" are not the same
 * thing, and the difference is worth a function: an overhang into empty layout
 * space is drawn nowhere and costs nothing. On a single-monitor desk NOTHING
 * can bleed, and on a row of them only the inward edges can. That is what lets
 * the slide below show columns peeking in at the edges — niri's actual look —
 * wherever it is safe to, instead of banning it everywhere because of a case
 * that may not exist on this desk at all.
 *
 * Probes just outside whichever edge is crossed, at the monitor's vertical
 * midpoint: a strip only ever overhangs left or right.
 */
static bool niri_column_bleeds(syn_server_t *s, syn_output_t *o, int vx, int cw)
{
    struct wlr_box ob;
    output_box_of(s, o, &ob);

    if (vx >= ob.x && vx + cw <= ob.x + ob.width)
        return false;                       /* wholly on this monitor */

    double my = ob.y + ob.height / 2.0;

    if (vx < ob.x) {
        struct wlr_output *n =
            wlr_output_layout_output_at(s->output_layout, ob.x - 1, my);
        if (n && n->data && n->data != o) return true;
    }
    if (vx + cw > ob.x + ob.width) {
        struct wlr_output *n =
            wlr_output_layout_output_at(s->output_layout,
                                        ob.x + ob.width, my);
        if (n && n->data && n->data != o) return true;
    }
    return false;
}

/*
 * Place the strip at scroll offset `scroll`. Shared by the reflow and the slide.
 *
 * move_only is the whole reason the slide is affordable. During a slide nothing
 * about a window changes except its x, so the frames in between are driven with
 * view_move — a scene-node move, no client configure. Sizing every window on
 * every frame instead would be exactly the resize storm anim.c's header refuses
 * to ship, and at 60fps across a full strip it is a lot of round trips to spend
 * on pixels that are about to move again.
 *
 * layout_scroll_tick runs the FULL pass (move_only false) the moment the slide
 * settles, so no client is left believing a size or an X11 root position that
 * the compositor has since changed.
 */
static void niri_place(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o,
                       struct wlr_box vp, int gap, int scroll, bool move_only,
                       bool sliding)
{
    syn_view_t *lead = niri_next(ws, o, NULL);
    int cx = 0;
    while (lead) {
        int members;
        int cw   = niri_col_width(lead, vp.width, gap);
        syn_view_t *next = niri_column_end(ws, o, lead, &members);

        int vx = vp.x + cx - scroll;

        /*
         * AT REST: whole column or nothing — the header note on clipping, and
         * the rule this layout has always had.
         *
         * MID-SLIDE: a column may peek in at the edge, wherever that cannot
         * spill onto a neighbouring monitor (niri_column_bleeds). Without this
         * the slide would be pointless on its most important frame: the column
         * you just focused does not "fit entirely" until the strip has finished
         * moving, so the strict rule keeps it hidden for the whole animation
         * and then pops it into existence at the end — which is the jump the
         * slide exists to replace, with a delay added.
         *
         * The at-rest rule is deliberately left untouched rather than folded
         * into this one. Every column the strip settles on is fully on screen,
         * on every desk, exactly as before; the peeking is a property of the
         * animation, and it ends when the animation does.
         */
        bool shown;
        if (sliding) {
            bool intersects = (vx + cw > vp.x) && (vx < vp.x + vp.width);
            shown = intersects && !niri_column_bleeds(s, o, vx, cw);
        } else {
            shown = (cx >= scroll) && (cx + cw <= scroll + vp.width);
        }

        int slot_h = (vp.height - (members - 1) * gap) / members;
        if (slot_h < MIN_WIN) slot_h = MIN_WIN;

        syn_view_t *m = lead;
        for (int i = 0; i < members && m; i++) {
            int my = vp.y + i * (slot_h + gap);
            if (move_only) {
                view_move(m, vx, my);
            } else if (m->x != vx || m->y != my ||
                       m->w != cw || m->h != slot_h) {
                /* Only configure a window whose box actually moved. focus_view()
                 * reflows a niri desktop on every focus change, and most of those
                 * do not move the strip at all — without the compare that would
                 * send every client on the monitor a configure per Alt+Tab press,
                 * per click, per Super+J. Same guard, same reason, as the monocle
                 * pass above. */
                place_view(m, vx, my, cw, slot_h);
            }
            wlr_scene_node_set_enabled(view_node(m), shown);
            m = niri_next(ws, o, m);
        }

        cx  += cw + gap;
        lead = next;
    }
}

/*
 * Point this monitor's strip at `target` and answer where it should be DRAWN
 * this frame.
 *
 * With animations off that is the target itself and nothing else happens. With
 * them on, a target that has changed starts a slide from wherever the strip has
 * actually got to — interrupting a slide in flight is just a new slide from the
 * current position, which is what makes holding Super+J down feel continuous
 * rather than like a series of jumps.
 */
static int niri_scroll_to(syn_server_t *s, syn_output_t *o, int idx, int target)
{
    if (target != o->strip_target[idx]) {
        o->strip_target[idx]  = target;
        o->strip_from[idx]    = o->strip_scroll[idx];
        o->strip_t0[idx]      = now_secs();
        o->strip_sliding[idx] = 1;
    }

    if (s->config.animation_ms <= 0) {
        o->strip_sliding[idx] = 0;
        o->strip_scroll[idx]  = target;
    }
    return o->strip_scroll[idx];
}

void layout_niri(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    struct wlr_box vp;
    int gap;
    niri_viewport(s, o, &vp, &gap);
    int W = vp.width;

    /* ── Pass 1: measure the strip, and find the focused column ──
     * Strip coordinates: 0 is the left-hand end, independent of where the
     * monitor sits in the layout. */
    int total = 0;
    int focus_x = -1, focus_w = 0;
    syn_view_t *lead = niri_next(ws, o, NULL);
    int cx = 0;
    while (lead) {
        int members;
        int cw   = niri_col_width(lead, W, gap);
        syn_view_t *next = niri_column_end(ws, o, lead, &members);

        /* Is the focus in this column? Scanning the members rather than
         * testing the leader alone: focusing the second window of a column has
         * to scroll that column on screen just the same. */
        if (s->focused_view && s->focused_view->workspace == ws) {
            syn_view_t *m = lead;
            for (int i = 0; i < members && m; i++) {
                if (m == s->focused_view) { focus_x = cx; focus_w = cw; break; }
                m = niri_next(ws, o, m);
            }
        }

        total = cx + cw;
        cx   += cw + gap;
        lead  = next;
    }
    if (total == 0) return;             /* nothing on this monitor's strip */

    /* ── Scroll ──
     * Least movement that puts the focused column fully on screen, then
     * clamped to the strip so the end of it can't be scrolled past. Kept in
     * this order: the focus adjustment can never breach the clamp (a column
     * lies inside the strip by construction), but the clamp very much can
     * breach the focus, e.g. when the strip just got shorter because a window
     * closed.
     *
     * Measured from the last TARGET, not from where the strip currently sits.
     * Mid-slide those are different numbers, and re-deriving from the drawn
     * position would make every reflow during a slide move the goalposts a
     * fraction of the remaining distance — the strip would converge on
     * something that is not a column edge, and never quite stop. Reflows during
     * a slide are not rare: focus_view() reflows a niri desktop on every focus
     * change, which is what started the slide in the first place. */
    int idx    = ws->index;
    int target = o->strip_target[idx];
    int maxs   = total - W;
    if (maxs < 0) maxs = 0;
    if (target > maxs) target = maxs;
    if (target < 0)    target = 0;
    if (focus_x >= 0) {
        if (focus_x < target)
            target = focus_x;                        /* off the left edge */
        else if (focus_x + focus_w > target + W)
            target = focus_x + focus_w - W;          /* off the right edge */
    }
    int scroll = niri_scroll_to(s, o, idx, target);

    /* ── Pass 2: place ── */
    niri_place(s, ws, o, vp, gap, scroll, false, o->strip_sliding[idx]);
}

/* ── The niri slide ──────────────────────────────────────── */
/*
 * niri's signature is not that the strip scrolls, it is that you can SEE it
 * scroll — the columns glide and you keep your bearings on a workspace wider
 * than the monitor. Landing each column instantly, which is what synui did
 * until now, throws that away: the screen just contains different windows.
 *
 * This is the one geometry animation wlr_scene can actually carry, and the
 * reason is worth stating because anim.c's header rules geometry animation out
 * in general. It rules out animating SIZE: a size tween re-configures the client
 * every frame and Hyprland only escapes that by animating a snapshot, which
 * needs render control wlr_scene does not expose. A scroll changes no window's
 * size at all — every column keeps its width and height for the whole slide and
 * only x moves — so it is driven entirely by moving scene nodes (view_move),
 * costing zero client round trips. anim.c's "position-only animation would work,
 * but a tiling reflow almost always changes size too" is exactly right, and this
 * is the case it names as the exception rather than the rule.
 *
 * Only the ACTIVE desktop is ticked. A hidden workspace's nodes are disabled, so
 * sliding it would be arithmetic nobody can see, and layout_apply reflows it
 * from scratch when it next becomes visible anyway.
 */
bool layout_scroll_tick(syn_server_t *s, double now)
{
    if (!s) return false;

    syn_workspace_t *ws = &s->workspaces[s->active_workspace];
    if (ws->layout != LAYOUT_NIRI) return false;

    /* Animations off: niri_scroll_to already snapped every strip to its target
     * and cleared the flag, so there is nothing in flight to advance. */
    double dur = s->config.animation_ms / 1000.0;
    if (dur <= 0.0) return false;

    int idx = ws->index;
    bool running = false;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->strip_sliding[idx]) continue;

        float t = (float)((now - o->strip_t0[idx]) / dur);
        if (t < 0.0f) t = 0.0f;

        int from = o->strip_from[idx];
        int to   = o->strip_target[idx];

        if (t >= 1.0f) {
            o->strip_scroll[idx]  = to;
            o->strip_sliding[idx] = 0;
            /* The authoritative pass. Everything above moved scene nodes only,
             * so the clients have not been told anything for the length of the
             * slide; this is where they are, once, told the truth — and it is
             * what keeps an X11 client's idea of its root position from going
             * stale, which is what makes its menus open in the wrong place. */
            layout_niri(s, ws, o);
            /* That reflow re-measures the strip, and it may have found a NEW
             * target — a window closed while the slide was running, or the
             * focus moved on the last frame — in which case niri_scroll_to has
             * just started another slide. Report it, or this tick returns false
             * for an output that is still moving, nothing schedules the next
             * frame, and the strip stops wherever it happened to be. */
            if (o->strip_sliding[idx]) running = true;
            continue;
        }

        o->strip_scroll[idx] = from + (int)((to - from) * anim_ease_out(t));

        struct wlr_box vp;
        int gap;
        niri_viewport(s, o, &vp, &gap);
        niri_place(s, ws, o, vp, gap, o->strip_scroll[idx], true, true);
        running = true;
    }
    return running;
}

/* ── Columns: consume / expel ────────────────────────────── */
/*
 * Super+, pulls the focused window into the column on its left; Super+. pushes
 * it back out into a column of its own. niri's names for these are "consume"
 * and "expel" and they are the only two column operations that cannot be
 * expressed by moving along the strip, which Super+Shift+J/K already does.
 *
 * Joining adopts the target column's width, or the window would set the width
 * of the column it just walked into — the column it LEFT is not the one being
 * resized, and a wide window consumed into a narrow column would drag the
 * narrow column's other windows out with it.
 */
void layout_column_join(syn_server_t *s, syn_view_t *view, int join)
{
    if (!view || !view->mapped) return;
    syn_workspace_t *ws = view->workspace;
    if (!ws || ws->layout != LAYOUT_NIRI) return;
    if (!niri_tileable(view, view->output)) return;

    if (!join) {
        if (!view->col_join) return;         /* already a column of its own */
        view->col_join = 0;
    } else {
        /* The window before this one on the SAME strip — the list may hold
         * windows of other monitors between the two. Nothing to join if this
         * is the leftmost window here. */
        syn_view_t *prev = NULL, *v = niri_next(ws, view->output, NULL);
        while (v && v != view) {
            prev = v;
            v = niri_next(ws, view->output, v);
        }
        if (!prev || view->col_join) return;
        view->col_join = 1;
        view->col_frac = prev->col_frac;
    }
    layout_apply(s, ws);
}

/* Where a freshly-mapped window goes in the strip.
 *
 * Every other layout wants a new window at the HEAD of the workspace list —
 * that is the tiling master slot, and both map paths insert there. On a niri
 * desktop the head is the far LEFT of the strip, so a new window would shove
 * the whole desktop rightwards and scroll you back to the beginning of it. niri
 * opens a window in its own column immediately right of the focused one, which
 * is also the only placement that leaves the strip you were reading where it
 * was.
 *
 * Called from both map paths BEFORE they focus the new view, so `focused_view`
 * is still the window the user was actually in.
 */
void layout_strip_insert(syn_server_t *s, syn_view_t *view)
{
    if (!view || !view->workspace || view->workspace->layout != LAYOUT_NIRI)
        return;

    /* A new window is its own column even if it landed next to one; whatever
     * the calloc left, the strip owns this flag now. */
    view->col_join = 0;

    syn_view_t *anchor = s->focused_view;
    if (!anchor || anchor == view || anchor->workspace != view->workspace ||
        !anchor->mapped)
        return;

    /* Past the END of the anchor's column, not just past the anchor: dropping
     * the window between a leader and its stack would silently make it a member
     * of that column (col_join is read positionally). */
    syn_view_t *tail = anchor;
    for (syn_view_t *n = niri_next(view->workspace, anchor->output, anchor);
         n && n->col_join;
         n = niri_next(view->workspace, anchor->output, n))
        tail = n;

    wl_list_remove(&view->link);
    wl_list_insert(&tail->link, &view->link);
}

/* ── AI layout ───────────────────────────────────────────── */
/*
 * Build a prompt describing all open windows and the workspace intent,
 * then ask synapd for a JSON layout suggestion.
 *
 * Expected response (JSON, one window per line):
 *   {"comm":"vim","x":0,"y":0,"w":0.6,"h":1.0}
 *   {"comm":"terminal","x":0.6,"y":0.5,"w":0.4,"h":0.5}
 *   {"comm":"firefox","x":0.6,"y":0.0,"w":0.4,"h":0.5}
 *
 * w and h are fractions of the output dimensions (0.0-1.0).
 * If the response is malformed we fall back to tiling.
 */
void layout_request_ai(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    if (!atomic_load(&s->ai_connected)) {
        layout_tile(s, ws, o);
        return;
    }

    /* Build window list string */
    char win_list[1024] = {0};
    int pos = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->floating || v->output != o) continue;
        const char *title = view_title(v) ? view_title(v) : "unknown";
        const char *app   = view_app_id(v) ? view_app_id(v) : "unknown";
        pos += snprintf(win_list + pos, sizeof(win_list) - pos,
                        "  - app=%s title=\"%.30s\"\n", app, title);
        if (pos >= (int)sizeof(win_list) - 64) break;
    }

    if (!win_list[0]) {
        layout_tile(s, ws, o);
        return;
    }

    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    /* Remember which monitor asked, so the async response places the right
     * windows into the right box. */
    s->ai_layout_output = o;

    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
        "[LAYOUT_REQUEST]\n"
        "workspace: %s\n"
        "intent: %s\n"
        "windows:\n%s\n"
        "output: %dx%d\n"
        "\n"
        "Suggest a tiling layout. For each window reply with one JSON object per line:\n"
        "{\"app\":\"APP_ID\",\"x\":FRAC,\"y\":FRAC,\"w\":FRAC,\"h\":FRAC}\n"
        "x,y,w,h are fractions 0.0-1.0 of output dimensions. No explanation.",
        ws->name,
        ws->intent[0] ? ws->intent : "general use",
        win_list,
        area.width, area.height
    );

    syn_ai_request_t req = {
        .type = AI_MSG_QUERY_LAYOUT,
        .id   = (uint64_t)ws->index,   /* response routes back by workspace index */
    };
    strncpy(req.prompt, prompt, sizeof(req.prompt) - 1);
    ai_thread_send(s, &req);

    /* Apply tiling immediately as placeholder; the AI response arrives
     * asynchronously and the frame loop calls layout_apply_ai_response(). */
    layout_tile(s, ws, o);
}

/* ── Parse one AI layout line ────────────────────────────── */
/*
 * Parses a single line of the AI layout response:
 *   {"app":"APP_ID","x":FRAC,"y":FRAC,"w":FRAC,"h":FRAC}
 * Returns 1 and fills the outputs on success, 0 on a malformed line.
 * Fractions are validated to the sane 0.0–1.0 range (w/h must be > 0) so a
 * bad model reply can't place a window off-screen or at zero size. Pure
 * function (no wlroots deps) so it can be unit-tested directly.
 */
int parse_ai_layout_line(const char *line, char *app_id, size_t app_len,
                         float *x, float *y, float *w, float *h)
{
    char app[128] = {0};
    float fx, fy, fw, fh;
    if (sscanf(line, " {\"app\":\"%127[^\"]\",\"x\":%f,\"y\":%f,\"w\":%f,\"h\":%f}",
               app, &fx, &fy, &fw, &fh) != 5)
        return 0;

    if (fx < 0.0f || fx > 1.0f || fy < 0.0f || fy > 1.0f) return 0;
    if (fw <= 0.0f || fw > 1.0f || fh <= 0.0f || fh > 1.0f) return 0;
    if (fx + fw > 1.001f || fy + fh > 1.001f) return 0;   /* must fit on screen */

    snprintf(app_id, app_len, "%s", app);
    *x = fx; *y = fy; *w = fw; *h = fh;
    return 1;
}

/* ── Apply AI layout response ────────────────────────────── */
void layout_apply_ai_response(syn_server_t *s, syn_workspace_t *ws,
                               const char *json_response)
{
    /* The monitor whose windows this suggestion is about (recorded when the
     * request went out — the response is async and the focus may have moved). */
    syn_output_t *o = s->ai_layout_output ? s->ai_layout_output
                                          : server_focused_output(s);
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    char copy[2048];
    strncpy(copy, json_response, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    int applied = 0;
    char *save = NULL;
    char *line = strtok_r(copy, "\n", &save);
    while (line) {
        char app_id[128];
        float fx, fy, fw, fh;
        if (parse_ai_layout_line(line, app_id, sizeof(app_id),
                                 &fx, &fy, &fw, &fh)) {
            syn_view_t *v;
            wl_list_for_each(v, &ws->windows, link) {
                if (!v->mapped || v->floating || v->output != o) continue;
                const char *aid = view_app_id(v);
                if (aid && strcmp(aid, app_id) == 0) {
                    int gap = s->config.gap;
                    int nx = area.x + (int)(fx * area.width);
                    int ny = area.y + (int)(fy * area.height);
                    int nw = (int)(fw * area.width);
                    int nh = (int)(fh * area.height);
                    nw = nw > gap * 2 ? nw - gap : nw;
                    nh = nh > gap * 2 ? nh - gap : nh;
                    /* Mark AI-managed before placing so the border picks the
                     * AI colour, and record the app as the window's intent. */
                    v->ai_ctx.has_ctx = 1;
                    snprintf(v->ai_ctx.intent, sizeof(v->ai_ctx.intent),
                             "%s", app_id);
                    place_view(v, nx + gap/2, ny + gap/2, nw, nh);
                    applied++;
                    break;
                }
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }

    /* If the model returned nothing usable, keep the tiling placeholder. */
    if (!applied)
        wlr_log(WLR_DEBUG, "synui: AI layout response had no usable windows");
}

/* ── Main dispatch ───────────────────────────────────────── */
/* A workspace spans the whole desk, so laying it out means laying out each
 * monitor's share of it: every output runs the workspace's layout over just
 * the windows that live on that output. */
void layout_apply(syn_server_t *s, syn_workspace_t *ws)
{
    if (!s || !ws) return;

    /* A hidden workspace re-flows when it next becomes visible; laying it
     * out now would re-enable its scene nodes on top of the visible one. */
    if (!workspace_visible(ws)) return;

    /* Re-enable all nodes first (minimized ones stay hidden — their own
     * apply path owns disabling/enabling the node). */
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped && !v->minimized)
            wlr_scene_node_set_enabled(view_node(v), true);

    /* AI-managed marking (and its cyan border) only persists under the AI
     * layout; clear it for the other layouts so the border reflects reality. */
    if (ws->layout != LAYOUT_AI)
        wl_list_for_each(v, &ws->windows, link)
            v->ai_ctx.has_ctx = 0;

    /* Re-fit maximized windows to the CURRENT usable box.
     *
     * view_apply_maximized sizes the window once and marks it floating (it has
     * to — otherwise the tiling pass below would drag it back into a slot), so
     * from then on every loop here skips it. But the usable box is not fixed:
     * it moves whenever a panel maps, unmaps or changes its exclusive zone, and
     * the bar's auto-hide changes it on every reveal (28 → 0 → 28). Without
     * this the window keeps whatever box it was maximized into, so a hidden bar
     * leaves a strip of bare desktop above a "maximized" window. Verified: with
     * the bar's zone dropped to 0, a maximized window stayed at 1280x692 while
     * a freshly tiled one correctly took the full 704.
     *
     * Runs for every layout, FLOATING included — maximize is not a tiling
     * feature. The compare is what makes it cheap: layout_apply is called from
     * a lot of paths and view_resize sends the client a configure, so it must
     * not fire when the box has not actually moved. */
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || !v->maximized || v->fullscreen || v->minimized)
            continue;
        struct wlr_box area;
        output_usable_box_of(s, v->output ? v->output : server_focused_output(s),
                             &area);
        if (v->x == area.x && v->y == area.y &&
            v->w == area.width && v->h == area.height)
            continue;
        view_resize(v, area.x, area.y, area.width, area.height);
    }

    /* AI layout is a single-monitor feature: only the focused output gets a
     * suggestion (one in-flight request at a time), the rest tile. */
    syn_output_t *focused = server_focused_output(s);
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        switch (ws->layout) {
        case LAYOUT_TILING:   layout_tile(s, ws, o);     break;
        case LAYOUT_SPIRAL:   layout_spiral(s, ws, o);   break;
        case LAYOUT_CASCADE:  layout_cascade(s, ws, o);  break;
        case LAYOUT_MONOCLE:  layout_monocle(s, ws, o);  break;
        case LAYOUT_NIRI:     layout_niri(s, ws, o);     break;
        case LAYOUT_AI:
            if (o == focused) layout_request_ai(s, ws, o);
            else              layout_tile(s, ws, o);
            break;
        case LAYOUT_FLOATING:
            /* The user positions windows — and the compositor positions the
             * ones he hasn't. Every window he HAS touched carries hand_placed
             * and this pass steps over it, so "floating" still means floating.
             * See layout_float_arrange. */
            layout_float_arrange(s, ws, o);
            break;
        }
    }
}

/* ── Fullscreen ──────────────────────────────────────────── */
/* Which monitor should a fullscreen window cover?
 *
 * A client names the monitor it wants in whatever way its protocol allows.
 * xdg-shell clients pass a wl_output straight to set_fullscreen. X11 clients
 * have no such argument, so SDL — and therefore Chibi, which picks the
 * portrait monitor itself — moves the window onto the target monitor first
 * and only then sets _NET_WM_STATE_FULLSCREEN; the window's own rectangle is
 * the request. Honouring that also matches what X11 window managers do.
 *
 * Only called with a mapped view, so xsurface geometry is the placed one.
 * A client that named no monitor falls back to its workspace's output, which
 * for a window already on that output is the same answer as before. */
static syn_output_t *fullscreen_target_output(syn_server_t *s, syn_view_t *view)
{
    struct wlr_output *wo = NULL;

    if (view->is_xwayland) {
        struct wlr_xwayland_surface *xs = view->xsurface;
        if (xs->width > 0 && xs->height > 0)
            wo = wlr_output_layout_output_at(s->output_layout,
                                             xs->x + xs->width  / 2.0,
                                             xs->y + xs->height / 2.0);
    } else {
        wo = view->xdg_surface->toplevel->requested.fullscreen_output;
    }

    if (wo && wo->data) return wo->data;
    return view->output ? view->output : server_focused_output(s);
}

/* Enter/leave fullscreen with real geometry: cover the target output's
 * full box (raised, borders hidden — view_update_borders checks the flag),
 * or hand the window back to the layout. Shared by the xdg and XWayland
 * request handlers and the foreign-toplevel (taskbar) request. */
void view_apply_fullscreen(syn_server_t *s, syn_view_t *view, int fs)
{
    view->fullscreen = fs ? 1 : 0;
    view_set_fullscreen(view, view->fullscreen);   /* client + taskbar state */
    if (!view->mapped) return;

    if (view->fullscreen) {
        /* Leaving fullscreen re-places a floating window (layout_float_place),
         * which would move it out of the zone it claims to be snapped to. */
        view->snapped = SYN_SNAP_NONE;
        syn_output_t *o = fullscreen_target_output(s, view);
        /* Fullscreening onto another monitor hands the window to that monitor
         * (same desktop), so it untiles back where it is actually shown. */
        if (o && view->output != o)
            view->output = o;
        struct wlr_box area;
        output_box_of(s, o, &area);
        /* view_resize re-seats the client at the frame origin: fullscreen has
         * no border or titlebar, so content == frame. */
        view_resize(view, area.x, area.y, area.width, area.height);
        wlr_scene_node_raise_to_top(view_node(view));
        /* A sub-native X11 client (old game locked to 1080p) fills the box by
         * scaling its buffer; re-applied per-commit from xw_surface_commit. */
        view_fullscreen_rescale(view);
    } else {
        layout_apply(s, view->workspace);
        if (view->floating)
            layout_float_place(s, view);
        view_update_decorations(view);
        /* Undo any fullscreen buffer scale now the view is back in the layout. */
        view_fullscreen_rescale(view);
    }

    /* Re-push the glass: corner_radius is squared off while fullscreen and blur
     * is gated on !fullscreen, and both persist on the scene_buffer node until
     * something recomputes them — same staleness that left maximized windows
     * rounded (see view_apply_maximized). Only for a mapped view; the unmapped
     * early-return above never reaches here. */
    anim_apply_alpha(view);

    /* A fullscreen view has to cover the bar, and it may have just been handed
     * to another output — refresh every output, not just the target. */
    layer_update_occlusion_all(s);

    /* Fullscreen is the game-mode signal, and this is the one choke point every
     * path (xdg, XWayland, foreign-toplevel) funnels through. */
    game_reevaluate(s);
}

/* ── Minimize (iconify) ──────────────────────────────────── */
/* Same split as fullscreen: view_set_minimized tells the client (X11 only —
 * xdg-shell has no minimize protocol) and taskbars; this makes it real by
 * hiding the scene node and excluding the window from tiling/monocle, then
 * reflowing. Restoring re-enables the node, raises and focuses it if its
 * workspace is currently shown (a hidden workspace's window stays disabled
 * until workspace_switch re-enables mapped, non-minimized nodes). */
void view_apply_minimized(syn_server_t *s, syn_view_t *view, int minimized)
{
    view->minimized = minimized ? 1 : 0;
    view_set_minimized(view, view->minimized);
    if (!view->mapped) return;

    /* Only actually show it if its workspace is visible somewhere — a hidden
     * workspace keeps all its nodes disabled regardless of minimized state
     * (workspace_switch re-enables mapped, non-minimized nodes when it next
     * becomes visible). */
    int show = !view->minimized && workspace_visible(view->workspace);
    wlr_scene_node_set_enabled(view_node(view), show);

    if (view->minimized) {
        if (s->focused_view == view)
            workspace_focus_first(s, view->workspace);
    } else if (show) {
        wlr_scene_node_raise_to_top(view_node(view));
        focus_view(s, view, view_surface(view));
    }

    layout_apply(s, view->workspace);
}

/* Focus the first mapped, non-minimized window on ws — or clear focus
 * entirely if there is none, so keyboard input can't keep flowing to a
 * hidden window. */
void workspace_focus_first(syn_server_t *s, syn_workspace_t *ws)
{
    /* The desktop spans every monitor, so prefer a window on the one the user
     * is actually looking at — otherwise switching desktops would throw focus
     * onto whichever screen happens to hold the list's first window. */
    syn_output_t *focused = server_focused_output(s);
    syn_view_t *v, *fallback = NULL;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->mapped || v->minimized) continue;
        if (v->output == focused) {
            focus_view(s, v, view_surface(v));
            return;
        }
        if (!fallback) fallback = v;
    }
    focus_view(s, fallback, fallback ? view_surface(fallback) : NULL);
}

/* ── Workspace switching ─────────────────────────────────── */
/* Switch the whole desk to virtual desktop `index`: every monitor swaps to its
 * share of that workspace at once. Nothing is bound to a particular output, so
 * this always does something no matter how many monitors are plugged in. */
void workspace_switch(syn_server_t *s, int index)
{
    if (index < 0 || index >= WORKSPACE_MAX) return;
    if (index == s->active_workspace) return;

    syn_workspace_t *cur    = &s->workspaces[s->active_workspace];
    syn_workspace_t *target = &s->workspaces[index];

    /* Cross-fade: the outgoing desktop's windows fade out and are only disabled
     * once they're actually invisible (anim.c), the incoming ones fade in. */
    syn_view_t *v;
    wl_list_for_each(v, &cur->windows, link)
        if (v->mapped)
            anim_fade_out_and_hide(v);
    cur->visible = 0;

    /* Show the incoming one. */
    s->active_workspace = index;
    target->visible = 1;
    wlr_log(WLR_INFO, "synui: workspace %d", index + 1);
    wl_list_for_each(v, &target->windows, link)
        if (v->mapped && !v->minimized) {
            wlr_scene_node_set_enabled(view_node(v), true);
            anim_fade_in(v);
        }

    layout_apply(s, target);

    /* Focus the target workspace's first window (or clear focus if empty —
     * the previous workspace's windows are hidden now). */
    workspace_focus_first(s, target);

    /* Switching away from a fullscreen window must bring the bar back — and
     * switching onto one must hide it again. */
    layer_update_occlusion_all(s);

    /* Refresh overlay if visible */
    if (s->overlay.visible)
        synui_render_overlay(s);

    /* Notify AI about workspace switch */
    if (atomic_load(&s->ai_connected)) {
        char prompt[256];
        snprintf(prompt, sizeof(prompt),
            "[WORKSPACE_SWITCH] switched to workspace '%s' (intent: %s). "
            "Update neural overlay context.",
            s->workspaces[index].name,
            s->workspaces[index].intent[0] ? s->workspaces[index].intent : "general");
        syn_ai_request_t req = { .type = AI_MSG_STATUS_UPDATE };
        strncpy(req.prompt, prompt, sizeof(req.prompt) - 1);
        ai_thread_send(s, &req);
    }
}

/* Send a window to another virtual desktop (Super+Shift+1…9). It keeps the
 * monitor it was on, so switching to that desktop finds it on the same screen
 * you sent it from. */
void workspace_move_view(syn_server_t *s, syn_view_t *view, int ws_index)
{
    if (ws_index < 0 || ws_index >= WORKSPACE_MAX) return;
    int old_ws = view->workspace->index;
    if (old_ws == ws_index) return;

    view->workspace = &s->workspaces[ws_index];
    wl_list_remove(&view->link);
    wl_list_insert(&view->workspace->windows, &view->link);

    /* Visible only if the target desktop is the one being shown. */
    wlr_scene_node_set_enabled(view_node(view),
                                workspace_visible(view->workspace) &&
                                !view->minimized);

    layout_apply(s, &s->workspaces[old_ws]);
    layout_apply(s, &s->workspaces[ws_index]);

    /* If the moved window was focused and is now hidden, hand focus back to
     * the desktop still on screen so keys don't keep going to an invisible
     * window. */
    if (s->focused_view == view && !workspace_visible(view->workspace))
        workspace_focus_first(s, &s->workspaces[old_ws]);
}

/* Re-home a window onto another monitor, keeping its desktop (Super+O). */
void view_set_output(syn_server_t *s, syn_view_t *view, syn_output_t *o)
{
    if (!view || !o || view->output == o) return;
    view->output = o;
    layout_apply(s, view->workspace);
}

/* ── Remembered geometry ─────────────────────────────────── */
/*
 * Put a window back where its app last left it (the table lives in
 * geom_persist.c). Called from the xdg and XWayland map handlers, so an app
 * reopens the size it was closed at, and from layout_float_place, where a
 * remembered box beats the centred default.
 *
 * WHICH DESKTOP DECIDES. A layout that places windows itself owns their
 * geometry, and a remembered box is the user's answer to a question those
 * layouts do not ask. So a window *opening* on a tiling, niri or AI desktop
 * skips this entirely: it goes wherever the layout puts it — on tiling the
 * whole usable box for the first window and a master/stack slot after that,
 * on niri a fresh column beside the one you were in.
 *
 * That is not a refinement of the old rule, it replaces one. The entry carries
 * a floating flag (see geom_persist_save) and this function used to honour it
 * on any desktop, re-floating the window so its box would survive. On velle's
 * tiling desktop that made the tiler look broken: every app reopened floating,
 * every layout skips floating windows, and closing one wrote floating=1 back
 * out — self-sustaining, and Super+Tab moved nothing because there was nothing
 * left in the flow to move. Edge-snapping seeded it (snap_view sets floating
 * for the duration and unmap records the live flag), but any window ever
 * floated would have done.
 *
 * The floating and monocle desktops keep the whole feature: on those, where a
 * window sits is the user's business, so a saved box is worth honouring.
 *
 * Two ways in that ARE still honoured on a tiling desktop, because neither is
 * a window merely opening:
 *
 *  - Super+F (and a drag that auto-floats): the caller sets view->floating
 *    before calling through layout_float_place, so the window has already left
 *    the flow and is asking where it used to live. Freshly-mapped views are
 *    never floating yet, which is exactly what separates the two cases.
 *  - A client that asks to be maximized before it ever commits (Firefox does,
 *    restoring its session) — that is the client's own request, handled by the
 *    map path, not something read out of windows.conf.
 *
 * Returns false when the app has nothing saved — or when the layout owns the
 * placement — in which case the caller's own placement stands.
 */
bool layout_restore_geometry(syn_server_t *s, syn_view_t *view)
{
    /* Ask before the lookup: on these desktops the table has no say at all,
     * and not touching it keeps that visible in a trace. */
    bool layout_places_it = view->workspace &&
                            (view->workspace->layout == LAYOUT_TILING ||
                             view->workspace->layout == LAYOUT_SPIRAL ||
                             view->workspace->layout == LAYOUT_NIRI ||
                             view->workspace->layout == LAYOUT_CASCADE ||
                             view->workspace->layout == LAYOUT_AI);
    if (layout_places_it && !view->floating)
        return false;

    struct wlr_box saved;
    int saved_max = 0, saved_float = 0;
    if (!geom_persist_lookup(view, &saved, &saved_max, &saved_float))
        return false;

    bool on_floating_desk = view->workspace &&
                            view->workspace->layout == LAYOUT_FLOATING;

    /* The window was free when it closed, so give it back its freedom before
     * asking whether it may keep its box — otherwise the tiler owns it and the
     * saved geometry is dropped. Not needed on a floating desktop (nothing
     * tiles there), and not for a maximized entry: view_apply_maximized below
     * floats it for the duration anyway, and saved_floating has to stay 0 so
     * un-maximizing hands it back to the tiler. Only monocle reaches this now
     * — tiling, niri and AI returned above. */
    bool refloated = false;
    if (saved_float && !saved_max && !view->floating && !on_floating_desk) {
        view->floating = 1;
        refloated = true;
    }

    bool free_window = view->floating || on_floating_desk;

    if (free_window) {
        /* x/y are absolute layout coordinates, so they name a monitor as much
         * as a position. Re-home the view onto the monitor the saved box's
         * centre lands on before clamping — otherwise every window reopens on
         * whichever monitor happened to be focused, squashed into its box. If
         * that monitor is gone the lookup fails and the clamp below handles
         * it. */
        struct wlr_output *wo = wlr_output_layout_output_at(
            s->output_layout,
            saved.x + saved.width  / 2.0,
            saved.y + saved.height / 2.0);
        if (wo && wo->data)
            view->output = wo->data;

        /* Clamp back onto a monitor that exists *now*, so an entry saved on a
         * wider desk can't open the window off-screen where it could never be
         * reached. */
        struct wlr_box area;
        get_view_geom(s, view, &area);

        int sw = saved.width, sh = saved.height;
        if (sw > area.width)  sw = area.width;
        if (sh > area.height) sh = area.height;
        if (sw < MIN_WIN) sw = MIN_WIN;
        if (sh < MIN_WIN) sh = MIN_WIN;

        int sx = saved.x, sy = saved.y;
        if (sx + sw > area.x + area.width)  sx = area.x + area.width  - sw;
        if (sy + sh > area.y + area.height) sy = area.y + area.height - sh;
        if (sx < area.x) sx = area.x;
        if (sy < area.y) sy = area.y;

        view_resize(view, sx, sy, sw, sh);

        /* This box is the USER's, not a default: geom_persist only ever records
         * a window that was free when it closed, which is to say one he had
         * placed himself. So it counts as a hand placement, and the floating
         * desktop's arranger must step over it — otherwise the first reflow
         * after the window opened would sweep it into a grid cell and
         * `remember_geometry` would silently stop meaning anything on the one
         * layout it was written for. */
        view->hand_placed = 1;
    }

    /* The caller tiled this view before handing it here (both map paths run
     * layout_apply first), so the desktop is still laid out as though it were
     * one of the tiles. Reflow now it has left the flow, or the windows it was
     * sharing a slot with stay squeezed around a gap. */
    if (refloated)
        layout_apply(s, view->workspace);

    /* Re-maximizing needs the restore box already in place, which the
     * view_resize above just established. */
    if (saved_max && !view->maximized)
        view_apply_maximized(s, view, 1);
    return true;
}

/* ── Reclaiming windows into the layout ──────────────────── */
/*
 * Hand every window on `ws` back to the layout: un-maximize it, drop its snap,
 * and clear `floating`. Returns how many it took back.
 *
 * velle, 2026-07-31: "if it's in tile mode i want the tiling to work, this
 * isn't intuitive." A tiling desktop can quietly end up with nothing to tile,
 * because four things set view->floating during a session — dragging a window
 * to move it (input.c), snapping it to an edge (snap.c), maximizing it
 * (view_apply_maximized), and Super+F — and until now the ONLY thing that ever
 * cleared it again was Super+F, one window at a time. So one drag took a window
 * out of the tiler for the rest of the session, selecting the tiling layout did
 * not bring it back, and the desktop looked like a tiler that had stopped
 * working. It had not: it was running on an empty set.
 *
 * Maximize deserves its own note, because it is the one that looks like it
 * should help and cannot. view_apply_maximized records saved_floating on the
 * way in and restores it on the way out, so for a window that is ALREADY
 * floating it reads 1 and writes 1 back — a fixed point. Maximizing and
 * un-maximizing a floating window re-confirms floating every time, which is
 * exactly what "even if i try to remaximize and try again" was describing.
 * saved_floating is therefore cleared here too, or the next maximize/restore
 * cycle would undo this one.
 *
 * Two kinds of window are left alone, because neither is in the flow by
 * mistake: a fullscreen window (it is deliberately the whole output), and a
 * dialog — an X11 modal or transient, or an xdg toplevel with a parent. Tiling
 * a file picker into a master slot is not what "make tiling work" means, and it
 * is the same exclusion geom_persist applies for the same reason.
 */
int layout_reclaim(syn_server_t *s, syn_workspace_t *ws)
{
    if (!ws) return 0;

    int taken = 0;
    syn_view_t *v, *tmp;
    /* _safe: view_apply_maximized() below calls layout_apply(), and a
     * reflow is not something to iterate a list across unguarded. */
    wl_list_for_each_safe(v, tmp, &ws->windows, link) {
        if (!v->mapped || v->fullscreen || v->override_redirect) continue;

        if (v->is_xwayland) {
            if (v->xsurface->parent || v->xsurface->modal) continue;
        } else if (v->xdg_surface->toplevel->parent) {
            continue;
        }

        if (!v->floating && !v->maximized && v->snapped == SYN_SNAP_NONE)
            continue;                       /* already the layout's */

        /* Through the real path, so the client is told and its saved_geo is
         * restored rather than left describing a box it no longer has. */
        if (v->maximized)
            view_apply_maximized(s, v, 0);

        v->snapped        = SYN_SNAP_NONE;
        v->floating       = 0;
        v->saved_floating = 0;
        /* And forget that the user ever placed it. On a floating desktop that
         * flag is the whole reason a window is not in the grid, so leaving it
         * set would make "hand everything back to the layout" reclaim windows
         * into a layout that has been told to skip them — the same shape of bug
         * as saved_floating above, one field along. */
        v->hand_placed    = 0;
        taken++;
    }

    if (taken)
        layout_apply(s, ws);
    return taken;
}

/* ── Floating placement ──────────────────────────────────── */
/*
 * Give a newly-floating window a sane geometry: prefer the client's own
 * preferred size, clamp it to the output, and centre it. Called when a
 * window is toggled floating (Super+F) or auto-floated for a drag.
 */
void layout_float_place(syn_server_t *s, syn_view_t *view)
{
    struct wlr_box area;
    get_view_geom(s, view, &area);

    /* Where this app was last left wins over the centred default. */
    if (layout_restore_geometry(s, view))
        return;

    /* On a floating desktop, with nothing remembered and nothing the user has
     * said about this window, the ARRANGER owns it — centring it here would
     * drop it squarely on top of whatever is already in the middle of the
     * screen, which is the pile layout_float_arrange exists to stop. Both map
     * paths call layout_apply immediately before this, so the window is already
     * in its cell; the reflow is here for the other callers (Super+F, leaving
     * fullscreen) that reach a floating desktop without one.
     *
     * A hand_placed window never gets here — layout_restore_geometry set the
     * flag on its way to returning true. */
    if (view->workspace && view->workspace->layout == LAYOUT_FLOATING &&
        !view->hand_placed) {
        layout_apply(s, view->workspace);
        return;
    }

    int w = view->w, h = view->h;
    /* The frame has to hold the client plus its chrome. */
    int bw = view_deco_border(view);
    int th = view_deco_titlebar(view);

    /* Prefer the surface's natural size. */
    if (view->is_xwayland) {
        if (view->xsurface->width > 0 && view->xsurface->height > 0) {
            w = view->xsurface->width  + 2 * bw;
            h = view->xsurface->height + 2 * bw + th;
        }
    } else {
        struct wlr_box geo = view->xdg_surface->geometry;
        if (geo.width > 0 && geo.height > 0) {
            w = geo.width  + 2 * bw;
            h = geo.height + 2 * bw + th;
        }
    }

    /* Fall back to two-thirds of the output if the size is unusable. */
    if (w < MIN_WIN || w > area.width)  w = area.width  * 2 / 3;
    if (h < MIN_WIN || h > area.height) h = area.height * 2 / 3;

    int x = area.x + (area.width  - w) / 2;
    int y = area.y + (area.height - h) / 2;
    view_resize(view, x, y, w, h);
}

/* ── FLOATING layout: the aesthetic tiler ────────────────── */
/*
 * A floating desktop used to place nothing at all — layout_apply's FLOATING
 * case was a bare no-op — so windows landed wherever layout_float_place had
 * centred them and the third one opened squarely on top of the second. "You
 * place the windows" is the right rule for windows you have an opinion about;
 * it is a poor one for the three terminals you just opened and have not
 * touched.
 *
 * So: the windows nobody has moved get arranged into a grid, and the grid is
 * deliberately INSET. This is the difference between this and layout_tile, and
 * it is the whole point of the mode — `float_inset` keeps a percentage of the
 * usable box clear at all four edges and `float_gap` is wider than a tiling gap
 * has any reason to be, so the wallpaper reads as part of the composition
 * rather than as the thing the windows failed to cover. A floating desktop
 * should look like windows resting on a desk, not like a tiler with rounded
 * corners.
 *
 * WHAT IT WILL NOT TOUCH:
 *  - a window the user has dragged or resized (hand_placed) — that is the
 *    contract that keeps floating actually floating. Set once, at
 *    grab_release_constraints, and only ever cleared deliberately;
 *  - maximized, fullscreen and minimized windows, which are already claimed by
 *    something louder;
 *  - dialogs — an X11 modal/transient, or an xdg toplevel with a parent. Same
 *    exclusion, for the same reason, as layout_reclaim: gridding a file picker
 *    in beside the window that opened it is not tidiness.
 *
 * The last row is centred when it holds fewer windows than the row above. That
 * is one line of arithmetic and it is most of what separates "arranged" from
 * "left-aligned with a hole in it".
 */
static bool float_arrangeable(syn_view_t *v, syn_output_t *o)
{
    if (!v->mapped || v->output != o) return false;
    if (v->fullscreen || v->minimized || v->maximized) return false;
    if (v->hand_placed) return false;
    if (v->override_redirect) return false;

    if (v->is_xwayland) {
        if (v->xsurface->parent || v->xsurface->modal) return false;
    } else if (v->xdg_surface->toplevel->parent) {
        return false;
    }
    return true;
}

void layout_float_arrange(syn_server_t *s, syn_workspace_t *ws, syn_output_t *o)
{
    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    int n = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (float_arrangeable(v, o)) n++;
    if (n == 0) return;

    /* The inset, as pixels. Clamped so a large percentage on a small monitor
     * cannot eat the whole box — at that point the setting has stopped being a
     * margin and the windows still have to go somewhere. */
    int inset = s->config.float_inset;
    if (inset < 0) inset = 0;
    if (inset > FLOAT_INSET_MAX) inset = FLOAT_INSET_MAX;
    int ix = area.width  * inset / 100;
    int iy = area.height * inset / 100;

    int x = area.x + ix;
    int y = area.y + iy;
    int W = area.width  - 2 * ix;
    int H = area.height - 2 * iy;
    if (W < MIN_WIN) { x = area.x; W = area.width  > MIN_WIN ? area.width  : MIN_WIN; }
    if (H < MIN_WIN) { y = area.y; H = area.height > MIN_WIN ? area.height : MIN_WIN; }

    int gap = s->config.float_gap;
    if (gap < 0) gap = 0;

    /* Squarest grid that holds them: cols = ceil(sqrt(n)). Integer-only, so no
     * libm and no rounding to argue about — walk up until cols² covers n. */
    int cols = 1;
    while (cols * cols < n) cols++;
    int rows = (n + cols - 1) / cols;

    int cell_w = (W - (cols - 1) * gap) / cols;
    int cell_h = (H - (rows - 1) * gap) / rows;
    if (cell_w < MIN_WIN) cell_w = MIN_WIN;
    if (cell_h < MIN_WIN) cell_h = MIN_WIN;

    /* How many sit in the final row, so it can be centred rather than left
     * hanging. A full last row centres to zero offset, so there is no special
     * case to write. */
    int last_row_n = n - (rows - 1) * cols;
    if (last_row_n <= 0) last_row_n = cols;

    int i = 0;
    wl_list_for_each(v, &ws->windows, link) {
        if (!float_arrangeable(v, o)) continue;

        int r = i / cols;
        int c = i % cols;
        int in_row = (r == rows - 1) ? last_row_n : cols;
        int row_w  = in_row * cell_w + (in_row - 1) * gap;

        int vx = x + (W - row_w) / 2 + c * (cell_w + gap);
        int vy = y + r * (cell_h + gap);

        /* Only configure a window whose box actually moved. layout_apply runs
         * from a lot of paths — every map, every unmap, every workspace switch,
         * every maximize — and on a settled desktop this pass must cost a
         * compare per window, not a configure per window. Same guard, same
         * reason, as the monocle and niri passes above. */
        if (v->x != vx || v->y != vy || v->w != cell_w || v->h != cell_h)
            place_view(v, vx, vy, cell_w, cell_h);
        i++;
    }
}

/* "Forget who I moved." Clears hand_placed across the desktop so the next
 * arrange takes every window back, and reflows. Bound to Super+Shift+G, and
 * called by layout_reclaim — which is already the "hand everything back to the
 * layout" verb, and would otherwise reclaim a floating desktop's windows into a
 * grid it had been told to skip them in. */
int layout_float_release_all(syn_server_t *s, syn_workspace_t *ws)
{
    if (!ws) return 0;

    int freed = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link) {
        if (!v->hand_placed) continue;
        v->hand_placed = 0;
        freed++;
    }

    layout_apply(s, ws);
    return freed;
}

/* ── Move focused view within the tiling stack ───────────── */
/* dir > 0 → toward tail (down the stack), dir < 0 → toward head (master). */
void layout_move_in_stack(syn_server_t *s, syn_view_t *view, int dir)
{
    if (!view) return;
    syn_workspace_t *ws = view->workspace;
    if (wl_list_length(&ws->windows) < 2) return;

    struct wl_list *head = &ws->windows;
    struct wl_list *self = &view->link;
    struct wl_list *anchor;   /* self is re-inserted immediately after this node */

    if (dir > 0)
        anchor = (self->next == head) ? head : self->next;
    else
        anchor = (self->prev == head) ? head->prev : self->prev->prev;

    wl_list_remove(self);
    wl_list_insert(anchor, self);
    layout_apply(s, ws);
}

/* ── Adjust the master column width ──────────────────────── */
/* On a niri desktop there is no master, but the two keys still mean "make the
 * column I am in wider/narrower" — so they act on the focused column instead of
 * growing a slot that does not exist. Handled here rather than in a bind of its
 * own so Super+H / Super+Shift+L keep one meaning across both tiling layouts. */
void layout_adjust_master(syn_server_t *s, syn_workspace_t *ws, float delta)
{
    if (ws->layout == LAYOUT_NIRI) {
        syn_view_t *f = s->focused_view;
        if (!f || f->workspace != ws || !f->mapped || f->floating) return;

        float cf = f->col_frac;
        if (cf < NIRI_FRAC_MIN || cf > NIRI_FRAC_MAX) cf = NIRI_COL_FRAC;
        cf += delta;
        if (cf < NIRI_FRAC_MIN) cf = NIRI_FRAC_MIN;
        if (cf > NIRI_FRAC_MAX) cf = NIRI_FRAC_MAX;

        /* Every member of the column, not just the leader: col_frac is read
         * off whichever window happens to lead, and that changes as soon as
         * one is moved along the strip or closed. The focus may be halfway
         * down a stacked column, so find its leader first. */
        syn_view_t *lead = niri_column_lead(ws, f);
        if (!lead) return;
        lead->col_frac = cf;
        for (syn_view_t *m = niri_next(ws, f->output, lead);
             m && m->col_join;
             m = niri_next(ws, f->output, m))
            m->col_frac = cf;

        layout_apply(s, ws);
        return;
    }

    float mf = ws->master_factor;
    if (mf < MASTER_MIN || mf > MASTER_MAX) mf = MASTER_FACTOR;
    mf += delta;
    if (mf < MASTER_MIN) mf = MASTER_MIN;
    if (mf > MASTER_MAX) mf = MASTER_MAX;
    ws->master_factor = mf;
    layout_apply(s, ws);
}
