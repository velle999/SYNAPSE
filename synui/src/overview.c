/*
 * overview.c — mission control (Super+X).
 *
 * Every window on the desktop, scaled down and laid out so none of them
 * overlap, with the virtual desktops as pills along the bottom. GNOME calls it
 * Activities, macOS calls it Mission Control, and both are answering the same
 * question: what is on this desk, given that the windows are covering it.
 *
 * ── Why this is not a bigger Alt+Tab ─────────────────────────────────────────
 *
 * The switcher answers "the window I was just in", and everything about it
 * follows from that: most-recently-used order, one fixed-size grid in the
 * middle of the screen, up only while a key is held. This answers "where did I
 * put it", which is a SPATIAL question — so the order is stable (stacking
 * order, never MRU), the tiles take the whole output at whatever size they fit,
 * and it stays up until you choose something. A grid that reshuffled itself as
 * you looked at windows would defeat the one thing it is for.
 *
 * They share the thumbnail machinery in render.c and nothing else.
 *
 * ── Nothing here holds a view pointer ────────────────────────────────────────
 *
 * overview_candidates() and overview_layout() are both recomputed on every
 * render AND on every pointer event. They are pure functions of the workspace
 * and the output box, so what is drawn and what is hit-tested cannot disagree,
 * and there is no snapshot for a closing window to dangle — which would
 * otherwise be a fifth place to remember on view destroy, next to the four that
 * null focused_view.
 *
 * The cost is the same one the switcher documents: a window that closes between
 * the frame and the click shifts what the click lands on by one. That is a
 * better trade than a cache with four invalidation sites.
 *
 * Keys follow the rest of the panels (Up/Down/Left/Right move, Enter activates,
 * Esc closes) per the panel contract in synui.h.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>

#include <linux/input-event-codes.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* ── What is on the desk ─────────────────────────────────── */

/*
 * Every mapped window on the ACTIVE desktop, in the order the workspace holds
 * them — which is stacking order, and deliberately not focus order.
 *
 * Minimized windows are included. They are the ones you most need an overview
 * for: a window you minimized is a window you cannot see by looking, and
 * "restore the thing I put away" has no other single gesture. They still hold
 * their last committed buffer (view_apply_minimized only disables the scene
 * node), so a minimized tile is a real picture rather than an app icon.
 *
 * Other desktops are NOT included, and that is the strip's job instead. A grid
 * of every window on every desktop is a grid you have to read rather than
 * glance at, and it throws away the one piece of structure the desktops are
 * there to provide.
 */
int overview_candidates(syn_server_t *s, syn_view_t **out, int max)
{
    int n = 0;
    syn_view_t *v;
    wl_list_for_each(v, &s->workspaces[s->active_workspace].windows, link) {
        if (!v->mapped) continue;
        if (n >= max) break;
        out[n++] = v;
    }
    return n;
}

void overview_output_box(syn_server_t *s, struct wlr_box *ob)
{
    server_output_box(s, ob);
}

/* ── Layout ──────────────────────────────────────────────── */

/*
 * A grid over the output, with the column count chosen to make the THUMBNAILS
 * as large as possible.
 *
 * That objective is the whole of the algorithm, and it is not the obvious one.
 * The textbook answer — `cols = round(sqrt(n * aspect))`, giving cells as close
 * to square as the area allows — is optimising for the wrong thing here: the
 * pictures that go in these cells are WINDOWS, and a window is roughly the
 * shape of the screen it is on. Square cells therefore waste most of their area
 * on letterboxing. On a 16:9 screen it put four windows in three columns, where
 * two rows of two give tiles nearly half again as big.
 *
 * So each candidate column count is scored by how large a rectangle of the
 * OUTPUT's shape fits in the cell it produces, and the best one wins. Ties go to
 * the smaller count, which is what the `>` does: fewer, larger tiles when it
 * makes no difference to the picture.
 *
 * That is n iterations of arithmetic, at most OVERVIEW_MAX of them, once per
 * repaint of a panel that repaints on hover. It is nothing, and it means the
 * layout has no magic constant to be wrong.
 *
 * The tiles are UNIFORM cells, not packed to each window's own aspect ratio. A
 * packed layout looks better in a screenshot and is worse to use: cell sizes
 * that depend on the windows' shapes mean the same window is somewhere
 * different every time you open the overview, and the arrow keys stop having a
 * grid to move on. Each window's real aspect is honoured INSIDE its cell by the
 * renderer, which is where it belongs.
 */
void overview_layout(const struct wlr_box *ob, int n, struct wlr_box *out)
{
    if (n <= 0) return;

    int ax = ob->x + OVERVIEW_MARGIN;
    int ay = ob->y + OVERVIEW_MARGIN + OVERVIEW_HEAD_H;
    int aw = ob->width  - 2 * OVERVIEW_MARGIN;
    int ah = ob->height - 2 * OVERVIEW_MARGIN - OVERVIEW_HEAD_H - OVERVIEW_STRIP_H;
    if (aw < 1) aw = 1;
    if (ah < 1) ah = 1;

    /* What a tile has to hold, in shape terms. The output's own box: every
     * window on it is at most that big and most are close to it. */
    double want_w = ob->width  > 0 ? (double)ob->width  : 16.0;
    double want_h = ob->height > 0 ? (double)ob->height : 9.0;

    int cols = 1;
    double best = -1.0;
    for (int c = 1; c <= n; c++) {
        int r  = (n + c - 1) / c;
        double cw = (double)(aw - (c - 1) * OVERVIEW_GAP) / c;
        double ch = (double)(ah - (r - 1) * OVERVIEW_GAP) / r;
        if (cw < 1.0 || ch < 1.0) continue;

        /* The scale a window of the output's shape would be drawn at in this
         * cell — i.e. how big the picture actually comes out. */
        double sc = cw / want_w;
        if (ch / want_h < sc) sc = ch / want_h;
        if (sc > best) { best = sc; cols = c; }
    }
    if (cols < 1) cols = 1;
    if (cols > n) cols = n;
    int rows = (n + cols - 1) / cols;
    if (rows < 1) rows = 1;

    int cw = (aw - (cols - 1) * OVERVIEW_GAP) / cols;
    int ch = (ah - (rows - 1) * OVERVIEW_GAP) / rows;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;

    for (int i = 0; i < n; i++) {
        int c = i % cols, r = i / cols;

        /* The last row is centred rather than left-aligned. Five windows in a
         * 3x2 grid put two tiles under three, and hanging them off the left
         * edge reads as a layout that ran out rather than one that finished. */
        int in_row = n - r * cols;
        if (in_row > cols) in_row = cols;
        int row_w  = in_row * cw + (in_row - 1) * OVERVIEW_GAP;
        int row_x  = ax + (aw - row_w) / 2;

        out[i].x      = row_x + c * (cw + OVERVIEW_GAP);
        out[i].y      = ay + r * (ch + OVERVIEW_GAP);
        out[i].width  = cw;
        out[i].height = ch;
    }
}

/*
 * The desktop pills along the bottom. Always WORKSPACE_MAX of them, empty ones
 * included: the strip is how you REACH an empty desktop, so hiding the empty
 * ones would hide the only thing there is to go to.
 */
void overview_ws_layout(const struct wlr_box *ob, struct wlr_box *out)
{
    int pad  = 10;
    int h    = OVERVIEW_STRIP_H - 2 * pad;
    int aw   = ob->width - 2 * OVERVIEW_MARGIN;
    int w    = (aw - (WORKSPACE_MAX - 1) * pad) / WORKSPACE_MAX;
    if (w < 1) w = 1;

    /* Wide screens would give each desktop a 200px-wide slab, which reads as
     * nine more windows rather than as a row of desktops. Cap it and centre. */
    if (w > 140) w = 140;
    int strip_w = WORKSPACE_MAX * w + (WORKSPACE_MAX - 1) * pad;
    int x0 = ob->x + (ob->width - strip_w) / 2;
    int y  = ob->y + ob->height - OVERVIEW_STRIP_H + pad;

    for (int i = 0; i < WORKSPACE_MAX; i++) {
        out[i].x      = x0 + i * (w + pad);
        out[i].y      = y;
        out[i].width  = w;
        out[i].height = h;
    }
}

/* ── Hit testing ─────────────────────────────────────────── */

static int box_holds(const struct wlr_box *b, double lx, double ly)
{
    return lx >= b->x && lx < b->x + b->width &&
           ly >= b->y && ly < b->y + b->height;
}

/* Which tile is under the pointer, or -1. Recomputes the list and the layout
 * rather than reading anything stored — see the file header. */
static int overview_tile_at(syn_server_t *s, double lx, double ly, int *n_out)
{
    syn_view_t *views[OVERVIEW_MAX];
    struct wlr_box tiles[OVERVIEW_MAX], ob;

    int n = overview_candidates(s, views, OVERVIEW_MAX);
    if (n_out) *n_out = n;
    if (n <= 0) return -1;

    overview_output_box(s, &ob);
    overview_layout(&ob, n, tiles);

    for (int i = 0; i < n; i++)
        if (box_holds(&tiles[i], lx, ly)) return i;
    return -1;
}

static int overview_ws_at(syn_server_t *s, double lx, double ly)
{
    struct wlr_box ws[WORKSPACE_MAX], ob;
    overview_output_box(s, &ob);
    overview_ws_layout(&ob, ws);

    for (int i = 0; i < WORKSPACE_MAX; i++)
        if (box_holds(&ws[i], lx, ly)) return i;
    return -1;
}

/* ── Acting on a tile ────────────────────────────────────── */

/* Bring a window out to where it can be used and focus it, then leave.
 *
 * Restoring before focusing, not after: view_apply_minimized() is what puts the
 * scene node back, and focusing a node that is still disabled hands the
 * keyboard to something that is not on screen. Same order alttab_reveal()
 * uses, and for the same reason.
 *
 * The overview comes down FIRST. Half the point of picking a window is seeing
 * it, and a full-screen dim still over the top of it would make the pick look
 * like it had not worked. */
static void overview_activate(syn_server_t *s, int idx)
{
    syn_view_t *views[OVERVIEW_MAX];
    int n = overview_candidates(s, views, OVERVIEW_MAX);
    if (idx < 0 || idx >= n) return;

    syn_view_t *v = views[idx];
    overview_hide(s);

    if (v->minimized)
        view_apply_minimized(s, v, 0);
    if (s->focused_view != v)
        focus_view(s, v, view_surface(v));
    if (s->focused_view)
        s->focused_view->focus_seq = ++s->focus_counter;
}

/* Close the window a tile stands for, and STAY in the overview.
 *
 * Staying is the whole reason this is worth having: clearing a desk is several
 * windows, and an overview that closed itself after each one would make the
 * second and third closes a matter of reopening it and finding the tile again.
 *
 * view_close() is a request, not a destruction — the client may put up a "save
 * changes?" dialog and never go away — so nothing is assumed about the list
 * here. It is rebuilt on the next render like everything else, and the
 * selection is clamped there. */
static void overview_close_tile(syn_server_t *s, int idx)
{
    syn_view_t *views[OVERVIEW_MAX];
    int n = overview_candidates(s, views, OVERVIEW_MAX);
    if (idx < 0 || idx >= n) return;

    view_close(views[idx]);
    synui_render_overview(s);
}

/* Switch desktop without leaving. That is what the strip is for: an overview
 * that closed on a desktop switch would be a slow way to press Super+2. */
static void overview_goto_ws(syn_server_t *s, int ws)
{
    if (ws < 0 || ws >= WORKSPACE_MAX) return;
    if (ws != s->active_workspace)
        workspace_switch(s, ws);
    s->overview.selected = 0;
    synui_render_overview(s);
}

/* ── Panel ───────────────────────────────────────────────── */

void overview_show(syn_server_t *s)
{
    /* Open on the FOCUSED window rather than on the first tile. The overview is
     * usually opened to go somewhere else, and starting the cursor where you
     * already are is what makes one arrow press mean "the one next to this". */
    syn_view_t *views[OVERVIEW_MAX];
    int n = overview_candidates(s, views, OVERVIEW_MAX);

    s->overview.selected = 0;
    for (int i = 0; i < n; i++)
        if (views[i] == s->focused_view) { s->overview.selected = i; break; }

    s->overview.visible = 1;
    synui_render_overview(s);
}

void overview_hide(syn_server_t *s)
{
    s->overview.visible = 0;
    synui_render_overview(s);
    ctlpanel_child_closed(s, "overview");
}

void overview_toggle(syn_server_t *s)
{
    if (s->overview.visible) overview_hide(s);
    else                     overview_show(s);
}

/* ── Keys ────────────────────────────────────────────────────
 *
 * Modal, like every other panel: while it is up the keyboard belongs to it.
 * Left/Right walk the list and Up/Down walk it a row at a time, which needs the
 * same column count the layout used — so it is derived the same way rather than
 * remembered, for the same reason the tiles are.
 */
static int overview_cols(syn_server_t *s, int n)
{
    struct wlr_box tiles[OVERVIEW_MAX], ob;
    if (n <= 1) return 1;

    overview_output_box(s, &ob);
    overview_layout(&ob, n, tiles);

    /* The first tile whose y differs from tile 0's starts row 1, so its index
     * IS the column count. Reading it back out of the layout rather than
     * recomputing the formula means the keys can never move on a grid the
     * screen does not have. */
    for (int i = 1; i < n; i++)
        if (tiles[i].y != tiles[0].y) return i;
    return n;
}

static void overview_move(syn_server_t *s, int delta)
{
    syn_view_t *views[OVERVIEW_MAX];
    int n = overview_candidates(s, views, OVERVIEW_MAX);
    if (n <= 0) { s->overview.selected = 0; return; }

    int t = s->overview.selected + delta;
    if (t < 0)  t = 0;
    if (t >= n) t = n - 1;
    s->overview.selected = t;
}

int overview_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->overview.visible) return 0;

    syn_view_t *views[OVERVIEW_MAX];
    int n    = overview_candidates(s, views, OVERVIEW_MAX);
    int cols = overview_cols(s, n);

    /* Super and Alt still belong to the compositor. The overview is somewhere
     * you sit and look, so Super+P for the power panel and Alt+Tab have to keep
     * working out of it — unlike a picker, there is nothing here being typed
     * into that a passing chord could corrupt. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
        overview_hide(s);
        return 1;

    case XKB_KEY_Left:  case XKB_KEY_h:
        overview_move(s, -1);    synui_render_overview(s); return 1;
    case XKB_KEY_Right: case XKB_KEY_l:
        overview_move(s, +1);    synui_render_overview(s); return 1;
    case XKB_KEY_Up:    case XKB_KEY_k:
        overview_move(s, -cols); synui_render_overview(s); return 1;
    case XKB_KEY_Down:  case XKB_KEY_j:
        overview_move(s, +cols); synui_render_overview(s); return 1;

    case XKB_KEY_Home:  overview_move(s, -n); synui_render_overview(s); return 1;
    case XKB_KEY_End:   overview_move(s, +n); synui_render_overview(s); return 1;

    /* Tab walks the grid too. It is the key the hand is already on when the
     * overview was reached from a switcher habit, and there is nothing else in
     * a panel with no text entry for it to mean. */
    case XKB_KEY_Tab:
        overview_move(s, +1); synui_render_overview(s); return 1;
    case XKB_KEY_ISO_Left_Tab:
        overview_move(s, -1); synui_render_overview(s); return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        overview_activate(s, s->overview.selected);
        return 1;

    /* Close the window under the cursor and stay. Both spellings: Delete is
     * what a list means by "get rid of this row", and q is what the desktop
     * already means by "close this window" (Super+Q). */
    case XKB_KEY_Delete:
    case XKB_KEY_q:
        overview_close_tile(s, s->overview.selected);
        return 1;

    default:
        break;
    }

    /* 1-9 jump straight to a desktop, the same digits Super+1-9 uses — the
     * strip is right there naming them, so the keys that match it should work.
     * Only the top-row digits: the numpad sends its own keysyms and the strip
     * is not worth a second table. */
    if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
        overview_goto_ws(s, (int)(sym - XKB_KEY_1));
        return 1;
    }

    return 1;   /* modal: swallow everything else while it is up */
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h. Hover moves the cursor, left click
 * activates, middle click closes — the three things every window list on every
 * desktop already does.
 */

int overview_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->overview.visible) return 0;

    int idx = overview_tile_at(s, lx, ly, NULL);
    if (idx < 0 || idx == s->overview.selected) return 1;

    s->overview.selected = idx;
    synui_render_overview(s);
    return 1;
}

int overview_click(syn_server_t *s, double lx, double ly, uint32_t button,
                   uint32_t time_msec)
{
    (void)time_msec;   /* nothing here needs a double click */
    if (!s->overview.visible) return 0;

    int ws = overview_ws_at(s, lx, ly);
    if (ws >= 0) {
        if (button == BTN_LEFT) overview_goto_ws(s, ws);
        return 1;
    }

    int n = 0;
    int idx = overview_tile_at(s, lx, ly, &n);

    if (idx >= 0) {
        if (button == BTN_LEFT)        overview_activate(s, idx);
        else if (button == BTN_MIDDLE) overview_close_tile(s, idx);
        return 1;
    }

    /* The gap between tiles, the heading, the margin. Closing on it is the
     * behaviour every full-screen overview has, and there is nothing else those
     * pixels could mean — the overview covers the whole output, so "click
     * outside the panel" does not exist here. */
    if (button == BTN_LEFT)
        overview_hide(s);
    return 1;
}

int overview_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->overview.visible) return 0;
    if (delta == 0) return 1;

    /* The grid never scrolls — every window is on screen at once, which is the
     * point — so the wheel walks the selection instead of doing nothing. */
    overview_move(s, delta > 0 ? 1 : -1);
    synui_render_overview(s);
    return 1;
}
