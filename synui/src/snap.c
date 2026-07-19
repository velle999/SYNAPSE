/*
 * snap.c — drag-to-edge window snapping ("snap to fit")
 *
 * Drag a window against a screen edge and it fills that region on release, the
 * way every other desktop has worked for fifteen years:
 *
 *   top edge          maximize (the output's usable box)
 *   left / right      that half
 *   the four corners  that quarter
 *
 * The zone is decided by where the *cursor* is, not where the window is — a
 * window is dragged by a titlebar that may be nowhere near the edge the user is
 * aiming at, and the cursor is the thing they're actually pointing with.
 *
 * There is deliberately no bottom-edge zone. The dock lives on a screen edge
 * with hover-reveal, and a bottom band would fight it for the same pixels every
 * time a window was dragged low; the bottom corners are narrow enough not to.
 *
 * Snapping is a *state* (view->snapped), not just a resize: dragging a snapped
 * window off its edge restores the size it had before, so snap/unsnap round-trips
 * instead of leaving windows stuck at half width. Top-edge snap defers to
 * view_apply_maximized() rather than reimplementing it, so the client and the
 * taskbar see a real maximize.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "synui.h"

/* How close to an edge the cursor must be to arm a zone. Generous: this is a
 * gesture aimed at a screen edge, and the cursor stops dead at the edge of the
 * output, so overshoot can't help the user find it. */
#define SNAP_EDGE       28

/* How far along an edge still counts as its corner. A fraction of the output so
 * it scales with resolution, clamped so it is neither a pixel hunt on a 4K
 * screen nor most of the edge on a small one. */
#define SNAP_CORNER_FRAC  6      /* 1/6 of the edge */
#define SNAP_CORNER_MIN  80
#define SNAP_CORNER_MAX 300

/* Preview: a translucent wash with brighter edges, in the focus-border colour
 * family (neon magenta #ff296d) so it reads as "this is where it lands". */
static const float PREVIEW_FILL[4] = { 1.00f, 0.16f, 0.43f, 0.22f };
static const float PREVIEW_EDGE[4] = { 1.00f, 0.16f, 0.43f, 0.85f };
#define PREVIEW_EDGE_PX 3

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ── Zone hit test ───────────────────────────────────────── */
syn_snap_zone_t snap_zone_at(syn_server_t *s, double lx, double ly,
                             struct wlr_box *box)
{
    if (!s->config.snap) return SYN_SNAP_NONE;

    struct wlr_output *wo =
        wlr_output_layout_output_at(s->output_layout, lx, ly);
    if (!wo || !wo->data) return SYN_SNAP_NONE;

    struct wlr_box a;
    output_usable_box_of(s, wo->data, &a);
    if (a.width <= 0 || a.height <= 0) return SYN_SNAP_NONE;

    /* Distances to each edge of the *output* (not the usable box): the cursor
     * physically cannot travel past the output, and the dock's exclusive zone
     * would otherwise put the armed band somewhere the cursor can't reach. */
    struct wlr_box full;
    output_box_of(s, wo->data, &full);
    int dl = (int)lx - full.x;
    int dr = (full.x + full.width)  - (int)lx;
    int dt = (int)ly - full.y;
    int db = (full.y + full.height) - (int)ly;

    bool at_left  = dl <= SNAP_EDGE;
    bool at_right = dr <= SNAP_EDGE;
    bool at_top   = dt <= SNAP_EDGE;
    bool at_bot   = db <= SNAP_EDGE;

    int corner_v = clampi(full.height / SNAP_CORNER_FRAC,
                          SNAP_CORNER_MIN, SNAP_CORNER_MAX);
    bool near_top = dt <= corner_v;
    bool near_bot = db <= corner_v;

    int hw = a.width  / 2;
    int hh = a.height / 2;

    syn_snap_zone_t zone = SYN_SNAP_NONE;
    struct wlr_box b = a;

    if ((at_left || at_right) && (near_top || near_bot)) {
        /* A corner: on a side band, within a corner's length of top or bottom. */
        bool left = at_left;
        bool top  = near_top && (!near_bot || dt <= db);
        b = (struct wlr_box){ left ? a.x : a.x + a.width - hw,
                              top  ? a.y : a.y + a.height - hh, hw, hh };
        zone = left ? (top ? SYN_SNAP_TOP_LEFT  : SYN_SNAP_BOTTOM_LEFT)
                    : (top ? SYN_SNAP_TOP_RIGHT : SYN_SNAP_BOTTOM_RIGHT);
    } else if (at_top) {
        b = a;
        zone = SYN_SNAP_MAX;
    } else if (at_left) {
        b = (struct wlr_box){ a.x, a.y, hw, a.height };
        zone = SYN_SNAP_LEFT;
    } else if (at_right) {
        b = (struct wlr_box){ a.x + a.width - hw, a.y, hw, a.height };
        zone = SYN_SNAP_RIGHT;
    } else {
        (void)at_bot;   /* no bottom-edge zone — see the file comment */
    }

    if (zone != SYN_SNAP_NONE && box) *box = b;
    return zone;
}

/* ── Preview ─────────────────────────────────────────────── */
static void preview_create(syn_server_t *s)
{
    if (s->snap.tree) return;

    /* Inside window_tree: above the windows once raised, but still below the
     * dock, the layer-shell panels and the menus, which live in later trees. */
    s->snap.tree = wlr_scene_tree_create(s->window_tree);
    if (!s->snap.tree) return;

    s->snap.fill = wlr_scene_rect_create(s->snap.tree, 1, 1, PREVIEW_FILL);
    for (int i = 0; i < 4; i++)
        s->snap.edge[i] = wlr_scene_rect_create(s->snap.tree, 1, 1, PREVIEW_EDGE);

    wlr_scene_node_set_enabled(&s->snap.tree->node, false);
}

static void preview_show(syn_server_t *s, const struct wlr_box *b)
{
    preview_create(s);
    if (!s->snap.tree || !s->snap.fill) return;

    int e = PREVIEW_EDGE_PX;
    int w = b->width, h = b->height;

    wlr_scene_rect_set_size(s->snap.fill, w, h);
    wlr_scene_node_set_position(&s->snap.fill->node, 0, 0);

    const struct { int x, y, w, h; } edges[4] = {
        { 0,     0,     w, e },          /* top    */
        { 0,     h - e, w, e },          /* bottom */
        { 0,     0,     e, h },          /* left   */
        { w - e, 0,     e, h },          /* right  */
    };
    for (int i = 0; i < 4; i++) {
        if (!s->snap.edge[i]) continue;
        wlr_scene_rect_set_size(s->snap.edge[i], edges[i].w, edges[i].h);
        wlr_scene_node_set_position(&s->snap.edge[i]->node,
                                    edges[i].x, edges[i].y);
    }

    wlr_scene_node_set_position(&s->snap.tree->node, b->x, b->y);
    wlr_scene_node_raise_to_top(&s->snap.tree->node);
    wlr_scene_node_set_enabled(&s->snap.tree->node, true);
}

void snap_preview_hide(syn_server_t *s)
{
    s->snap.zone = SYN_SNAP_NONE;
    if (s->snap.tree)
        wlr_scene_node_set_enabled(&s->snap.tree->node, false);
}

/* ── Drag hooks ──────────────────────────────────────────── */
void snap_drag_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->config.snap) return;

    struct wlr_box b;
    syn_snap_zone_t zone = snap_zone_at(s, lx, ly, &b);

    if (zone == SYN_SNAP_NONE) {
        if (s->snap.zone != SYN_SNAP_NONE) snap_preview_hide(s);
        return;
    }
    /* Same zone, same box: nothing to repaint. */
    if (zone == s->snap.zone && wlr_box_equal(&b, &s->snap.box)) return;

    s->snap.zone = zone;
    s->snap.box  = b;
    preview_show(s, &b);
}

void snap_drag_end(syn_server_t *s, syn_view_t *view)
{
    syn_snap_zone_t zone = s->snap.zone;
    struct wlr_box  box  = s->snap.box;
    snap_preview_hide(s);

    if (!view || !view->mapped || view->fullscreen)
        return;

    /* Re-home the window onto the monitor it was *dropped* on, whether or not the
     * drop landed in a snap zone. Everything downstream reads view->output, not
     * the geometry: maximize sizes itself to view->output's usable area, and the
     * tiling reflows per output. So a window merely dragged to another monitor
     * kept pointing at the one it came from, and the next maximize — double-click
     * the titlebar, Super+M — sent it back there to fill *that* screen instead.
     *
     * A snap is defined by its zone's box, a plain drop by where the window came
     * to rest (falling back to the cursor if it was dragged mostly off-screen).
     * For a snap this must still happen before the zone is applied, or the
     * preview lands on the right screen and the window doesn't. */
    double cx, cy;
    if (zone != SYN_SNAP_NONE) {
        cx = box.x + box.width  / 2.0;
        cy = box.y + box.height / 2.0;
    } else {
        cx = view->x + view->w / 2.0;
        cy = view->y + view->h / 2.0;
    }
    struct wlr_output *wo =
        wlr_output_layout_output_at(s->output_layout, cx, cy);
    if (!wo)
        wo = wlr_output_layout_output_at(s->output_layout,
                                         s->cursor->x, s->cursor->y);
    if (wo && wo->data && wo->data != view->output)
        view_set_output(s, view, wo->data);

    if (zone == SYN_SNAP_NONE)
        return;

    /* The top zone is a maximize, and maximize already knows how to save the
     * geometry, tell the client and reflow the workspace. */
    if (zone == SYN_SNAP_MAX) {
        view->snapped = SYN_SNAP_NONE;
        view_apply_maximized(s, view, 1);
        return;
    }

    /* Remember what to come back to — but only on the *first* snap, or dragging
     * from one zone straight into another would record a half-screen window as
     * the size to restore. */
    if (!view->snapped) {
        view->saved_geo      = (struct wlr_box){ view->x, view->y,
                                                 view->w, view->h };
        view->saved_floating = view->floating;
    }
    view->snapped  = zone;
    view->floating = 1;              /* a snapped window is out of the tiling flow */

    wlr_scene_node_raise_to_top(view_node(view));
    view_resize(view, box.x, box.y, box.width, box.height);
    layout_apply(s, view->workspace);   /* reflow whatever it left behind */
    view_update_decorations(view);

    wlr_log(WLR_INFO, "synui: snapped window to zone %d (%d,%d %dx%d)",
            (int)zone, box.x, box.y, box.width, box.height);
}

void snap_release_view(syn_server_t *s, syn_view_t *view, int keep_under_cursor)
{
    if (!view || !view->snapped) return;

    struct wlr_box g = view->saved_geo;
    view->snapped  = SYN_SNAP_NONE;
    view->floating = view->saved_floating ? 1 : view->floating;

    if (g.width <= 0 || g.height <= 0) return;   /* nothing sane to restore */

    int nx = g.x, ny = g.y;
    if (keep_under_cursor) {
        /* Re-centre the restored window on the cursor horizontally and keep the
         * grab point on the titlebar, so the window doesn't leap out from under
         * the pointer mid-drag. */
        nx = (int)s->cursor->x - g.width / 2;
        ny = view->y;
    }
    view_resize(view, nx, ny, g.width, g.height);
}
