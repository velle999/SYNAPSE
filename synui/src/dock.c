/*
 * dock.c — macOS-style auto-hide dock
 *
 * Shows pinned and currently-running apps as icons in a bar mirrored on every
 * output. Auto-hide only (no "always visible" mode): hidden, the dock reserves
 * zero layout space; shown, it simply floats above window content (its scene
 * tree sits alongside the welcome/overlay/dispcfg UI trees, not parented under
 * window_tree/layer_tree) — so unlike a layer-shell panel, showing or hiding
 * it never triggers a tiling relayout. The entry model (which apps are
 * pinned/running) is server-global; only the per-output scene tree and its
 * show/hide state live on syn_output::dock.
 *
 * The dock lives on any screen edge (config/state `dock_edge`): BOTTOM/TOP
 * render a horizontal bar, LEFT/RIGHT a vertical column. It can be dragged to
 * a different edge (dock_drag_*), pinned apps are edited live via a right-
 * click context menu (dockmenu_*), and both the edge and the pinned set
 * persist to ~/.config/synui/dock.state.
 *
 * SynapseOS Project — GPLv2
 */

#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "synui.h"

#define DOCK_ICON_SIZE 48
#define DOCK_ICON_PAD  8

/* Auto-hide timing. The dock slides fully in/out over DOCK_SLIDE_SECS; once
 * the cursor leaves it stays put for DOCK_HIDE_DELAY before sliding away, so
 * brushing past the edge doesn't make it flicker. */
#define DOCK_SLIDE_SECS 0.16
#define DOCK_HIDE_DELAY 0.45

/* Pointer travel (px) before a press on the bar becomes a real drag. */
#define DOCK_DRAG_THRESHOLD 6.0

static bool edge_is_vertical(syn_dock_edge_t e)
{
    return e == SYN_DOCK_EDGE_LEFT || e == SYN_DOCK_EDGE_RIGHT;
}

/* ── Entry model ─────────────────────────────────────────── */

void dock_rebuild(syn_server_t *s)
{
    syn_dock_entry_t merged[DOCK_MAX_ENTRIES];
    memset(merged, 0, sizeof(merged));
    int count = 0;

    /* Seed pinned entries in configured order. */
    for (int i = 0; i < s->config.dock_pin_count && count < DOCK_MAX_ENTRIES; i++) {
        syn_dock_entry_t *e = &merged[count++];
        snprintf(e->app_id, sizeof(e->app_id), "%s", s->config.dock_pin[i]);
        e->pinned = 1;
    }

    /* Merge in every mapped view across all workspaces: match an existing
     * (pinned) entry by app_id, or append a new running-only one. Views
     * with no app_id are skipped — nothing sane to key them by. */
    for (int wi = 0; wi < WORKSPACE_MAX; wi++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[wi].windows, link) {
            if (!v->mapped) continue;
            const char *app_id = view_app_id(v);
            if (!app_id || !*app_id) continue;

            syn_dock_entry_t *e = NULL;
            for (int i = 0; i < count; i++)
                if (strcmp(merged[i].app_id, app_id) == 0) { e = &merged[i]; break; }
            if (!e) {
                if (count >= DOCK_MAX_ENTRIES) continue;
                e = &merged[count++];
                snprintf(e->app_id, sizeof(e->app_id), "%s", app_id);
            }
            e->running = 1;
            /* Prefer the focused instance as the click target when an
             * app_id has multiple mapped windows. */
            if (!e->primary_view || v == s->focused_view)
                e->primary_view = v;
        }
    }

    memcpy(s->dock_entries, merged, sizeof(merged));
    s->dock_entry_count = count;

    dock_relayout(s);
}

void dock_view_mapped(syn_view_t *v)
{
    dock_rebuild(v->server);
}

void dock_view_unmapped(syn_view_t *v)
{
    /* dock_rebuild() always recomputes from the live workspace lists rather
     * than patching entries in place, so there's no stale primary_view to
     * clean up here — just refresh. */
    dock_rebuild(v->server);
}

/* ── Geometry ────────────────────────────────────────────── */

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                         double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r,     y + r,     r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
}

/* Fully-shown bar rect for this output's mirror on the current edge. The
 * "run" axis (length) grows with the entry count; the cross axis is the fixed
 * dock_height thickness. Shared by the renderer and the auto-hide tick so both
 * agree on where the bar lives. */
static bool dock_geometry(syn_output_t *o, int *bx, int *by,
                          int *bar_w, int *bar_h)
{
    syn_server_t *s = o->server;
    int n = s->dock_entry_count;
    int icon = DOCK_ICON_SIZE, pad = DOCK_ICON_PAD;
    int run = n > 0 ? n * icon + (n + 1) * pad : pad * 2;
    int thick = s->config.dock_height;
    syn_dock_edge_t edge = s->config.dock_edge;

    struct wlr_box ob;
    output_box_of(s, o, &ob);
    if (ob.width <= 0 || ob.height <= 0) return false;

    int w, h;
    if (edge_is_vertical(edge)) { w = thick; h = run; }
    else                        { w = run;   h = thick; }

    int x, y;
    switch (edge) {
    case SYN_DOCK_EDGE_TOP:
        x = ob.x + (ob.width - w) / 2; y = ob.y;
        break;
    case SYN_DOCK_EDGE_LEFT:
        x = ob.x; y = ob.y + (ob.height - h) / 2;
        break;
    case SYN_DOCK_EDGE_RIGHT:
        x = ob.x + ob.width - w; y = ob.y + (ob.height - h) / 2;
        break;
    case SYN_DOCK_EDGE_BOTTOM:
    default:
        x = ob.x + (ob.width - w) / 2; y = ob.y + ob.height - h;
        break;
    }
    if (x < ob.x) x = ob.x;   /* longer than the output: clip to origin */
    if (y < ob.y) y = ob.y;

    *bx = x; *by = y; *bar_w = w; *bar_h = h;
    return true;
}

/* Place the tree at its slide offset and enable it only while any part is
 * on-screen. slide_progress 1 = flush against the edge, 0 = pushed fully off
 * it (along the edge normal). While this output's dock is being dragged, the
 * bar floats under the cursor instead. Called after (re)rendering and every
 * anim tick. */
static void dock_apply_position(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!o->dock.tree) return;

    /* Dragging: float freely under the cursor, always visible. */
    if (s->dock_drag.active && s->dock_drag.moved && s->dock_drag.output == o) {
        wlr_scene_node_set_position(&o->dock.tree->node,
                                    (int)s->dock_drag.float_x,
                                    (int)s->dock_drag.float_y);
        wlr_scene_node_set_enabled(&o->dock.tree->node, true);
        wlr_scene_node_raise_to_top(&o->dock.tree->node);
        return;
    }

    int bx, by, bw, bh;
    if (!s->config.dock_enabled || !dock_geometry(o, &bx, &by, &bw, &bh)) {
        wlr_scene_node_set_enabled(&o->dock.tree->node, false);
        return;
    }

    double p = o->dock.slide_progress;
    double off = 1.0 - p;
    int x = bx, y = by;
    switch (s->config.dock_edge) {
    case SYN_DOCK_EDGE_TOP:    y = by - (int)lround(off * bh); break;
    case SYN_DOCK_EDGE_LEFT:   x = bx - (int)lround(off * bw); break;
    case SYN_DOCK_EDGE_RIGHT:  x = bx + (int)lround(off * bw); break;
    case SYN_DOCK_EDGE_BOTTOM:
    default:                   y = by + (int)lround(off * bh); break;
    }
    wlr_scene_node_set_position(&o->dock.tree->node, x, y);

    bool visible = p > 0.001;
    wlr_scene_node_set_enabled(&o->dock.tree->node, visible);
    if (visible)
        wlr_scene_node_raise_to_top(&o->dock.tree->node);
}

/* ── Rendering ───────────────────────────────────────────── */

static void dock_render_output(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!o->dock.tree) return;

    if (!s->config.dock_enabled) {
        wlr_scene_node_set_enabled(&o->dock.tree->node, false);
        return;
    }

    int bx, by, bar_w, bar_h;
    if (!dock_geometry(o, &bx, &by, &bar_w, &bar_h)) return;
    int n = s->dock_entry_count;
    int icon = DOCK_ICON_SIZE, pad = DOCK_ICON_PAD;
    bool vertical = edge_is_vertical(s->config.dock_edge);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(bar_w, bar_h, &cr);
    if (!buf) return;
    cairo_begin(cr);

    rounded_rect(cr, 0, 0, bar_w, bar_h, 16);
    cairo_set_source_rgba(cr, 0.06, 0.06, 0.12, 0.80);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.00, 0.85, 0.75, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    for (int i = 0; i < n; i++) {
        syn_dock_entry_t *e = &s->dock_entries[i];
        int ix, iy;
        if (vertical) {
            iy = pad + i * (icon + pad);
            ix = (bar_w - icon) / 2;
        } else {
            ix = pad + i * (icon + pad);
            iy = (bar_h - icon) / 2 - 4;   /* room for the dot below */
        }

        const syn_icon_entry_t *ic = icon_lookup(e->app_id);
        if (ic->icon_surface) {
            double sw = cairo_image_surface_get_width(ic->icon_surface);
            double sh = cairo_image_surface_get_height(ic->icon_surface);
            if (sw > 0 && sh > 0) {
                cairo_save(cr);
                cairo_translate(cr, ix, iy);
                cairo_scale(cr, icon / sw, icon / sh);
                cairo_set_source_surface(cr, ic->icon_surface, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
                cairo_paint(cr);
                cairo_restore(cr);
            }
        } else {
            icon_draw_monogram(cr, e->app_id, ix, iy, icon);
        }

        if (e->running) {
            double dx, dy;
            if (vertical) {
                /* Dot on the inner long edge of the column. */
                dx = (s->config.dock_edge == SYN_DOCK_EDGE_LEFT)
                         ? bar_w - 5.0 : 5.0;
                dy = iy + icon / 2.0;
            } else {
                dx = ix + icon / 2.0;
                dy = iy + icon + 6;
            }
            cairo_set_source_rgba(cr, 0.92, 0.92, 0.96, 0.9);
            cairo_arc(cr, dx, dy, 2.5, 0, 2 * M_PI);
            cairo_fill(cr);
        }

        /* Dock-canvas-local hit-box — identical across every output's
         * mirror since icon size/layout doesn't depend on output width. */
        e->x = ix; e->y = iy; e->w = icon; e->h = icon;
    }

    cairo_destroy(cr);
    set_scene_buffer(&o->dock.icons_buf, o->dock.tree, buf);

    /* Position/visibility follow the current slide state, not a forced
     * "shown" — the auto-hide tick owns whether the bar is on-screen. */
    dock_apply_position(o);
}

void dock_relayout(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        dock_render_output(o);
}

/* ── Public API ──────────────────────────────────────────── */

void dock_init(syn_server_t *s)
{
    s->dock_entry_count = 0;
    dock_rebuild(s);   /* seeds pinned-only entries; nothing mapped yet */
}

void dock_output_created(syn_output_t *o)
{
    o->dock.tree = wlr_scene_tree_create(&o->server->scene->tree);
    /* Auto-hide: start hidden (pushed off the edge). The tick slides it in
     * when the cursor reaches the trigger strip. */
    o->dock.shown = 0;
    o->dock.slide_progress = 0.0;
    o->dock.hover_since = 0.0;
    o->dock.unhover_since = 0.0;
    o->dock.last_tick = 0.0;
    dock_render_output(o);
}

void dock_output_destroy(syn_output_t *o)
{
    if (o->server->dock_drag.output == o) {
        o->server->dock_drag.active = 0;
        o->server->dock_drag.moved = 0;
        o->server->dock_drag.output = NULL;
    }
    if (o->dock.tree) {
        wlr_scene_node_destroy(&o->dock.tree->node);
        o->dock.tree = NULL;
        o->dock.icons_buf = NULL;
    }
}

/* ── Auto-hide ───────────────────────────────────────────── */

/* Advance this output's slide animation one frame and re-evaluate hover.
 * Returns true while more frames are needed (mid-slide, or shown-and-waiting
 * for the hide delay to elapse) so output_frame keeps scheduling. `now` is
 * CLOCK_MONOTONIC seconds. */
bool dock_tick(syn_output_t *o, double now)
{
    syn_server_t *s = o->server;
    if (!o->dock.tree) return false;

    /* While this output's dock is being dragged, the drag owns its position
     * and it stays shown — no auto-hide work. Keep frames coming. */
    if (s->dock_drag.active && s->dock_drag.output == o) {
        o->dock.shown = 1;
        o->dock.slide_progress = 1.0;
        return true;
    }

    if (!s->config.dock_enabled) {
        if (o->dock.shown || o->dock.slide_progress != 0.0) {
            o->dock.shown = 0;
            o->dock.slide_progress = 0.0;
            o->dock.last_tick = 0.0;
            dock_apply_position(o);
        }
        return false;
    }

    int bx, by, bw, bh;
    if (!dock_geometry(o, &bx, &by, &bw, &bh)) return false;
    struct wlr_box ob;
    output_box_of(s, o, &ob);

    double cx = s->cursor->x, cy = s->cursor->y;
    int margin = s->config.dock_hover_margin;
    if (margin < 1) margin = 1;
    int pad = DOCK_ICON_PAD;
    syn_dock_edge_t edge = s->config.dock_edge;

    bool on_output = cx >= ob.x && cx < ob.x + ob.width &&
                     cy >= ob.y && cy < ob.y + ob.height;

    /* Reveal trigger: a `margin`-thick strip along the dock's edge, within
     * the bar's footprint (plus a little padding on the long axis). */
    bool in_trigger = false;
    if (on_output) {
        switch (edge) {
        case SYN_DOCK_EDGE_TOP:
            in_trigger = cy < ob.y + margin &&
                         cx >= bx - pad && cx < bx + bw + pad;
            break;
        case SYN_DOCK_EDGE_LEFT:
            in_trigger = cx < ob.x + margin &&
                         cy >= by - pad && cy < by + bh + pad;
            break;
        case SYN_DOCK_EDGE_RIGHT:
            in_trigger = cx >= ob.x + ob.width - margin &&
                         cy >= by - pad && cy < by + bh + pad;
            break;
        case SYN_DOCK_EDGE_BOTTOM:
        default:
            in_trigger = cy >= ob.y + ob.height - margin &&
                         cx >= bx - pad && cx < bx + bw + pad;
            break;
        }
    }
    /* Keep-shown region: anywhere over the bar, but only once some of it is
     * actually on screen. dock_geometry() reports the *fully-shown* rect, so
     * testing it unconditionally would treat the whole dock_height band as a
     * reveal trigger and make `margin` meaningless. */
    bool on_screen = o->dock.shown || o->dock.slide_progress > 0.0;
    bool in_bar = on_screen && on_output &&
                  cx >= bx && cx < bx + bw &&
                  cy >= by && cy < by + bh;

    /* Don't summon a hidden dock mid-drag: a client holding an implicit
     * pointer grab (rubber-band select, window drag) owns the cursor, and
     * sliding the bar out from under it only covers what it is aimed at. */
    if (!on_screen && s->seat && s->seat->pointer_state.button_count > 0)
        in_trigger = false;

    bool engaged = in_trigger || in_bar;

    if (engaged) {
        o->dock.unhover_since = 0.0;
        o->dock.shown = 1;
    } else if (o->dock.shown) {
        if (o->dock.unhover_since == 0.0)
            o->dock.unhover_since = now;
        if (now - o->dock.unhover_since >= DOCK_HIDE_DELAY)
            o->dock.shown = 0;
    }

    double goal = o->dock.shown ? 1.0 : 0.0;
    if (o->dock.slide_progress != goal) {
        double dt = (o->dock.last_tick > 0.0) ? now - o->dock.last_tick : 0.0;
        if (dt <= 0.0 || dt > 0.5) dt = 0.016;   /* first frame / stall guard */
        o->dock.last_tick = now;

        double step = dt / DOCK_SLIDE_SECS;
        if (o->dock.slide_progress < goal)
            o->dock.slide_progress = fmin(goal, o->dock.slide_progress + step);
        else
            o->dock.slide_progress = fmax(goal, o->dock.slide_progress - step);

        dock_apply_position(o);
    } else {
        o->dock.last_tick = 0.0;
    }

    bool animating = o->dock.slide_progress != goal;
    bool waiting_to_hide = !engaged && o->dock.shown;
    return animating || waiting_to_hide;
}

/* Pointer moved: wake the outputs whose dock might need to react (cursor near
 * the dock's edge, a dock already on-screen, or an active drag). dock_tick
 * does the actual state work on the frame this schedules. */
void dock_pointer_motion(syn_server_t *s)
{
    if (!s->config.dock_enabled) return;

    double cx = s->cursor->x, cy = s->cursor->y;
    int band = s->config.dock_height + 8;
    syn_dock_edge_t edge = s->config.dock_edge;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree) continue;
        struct wlr_box ob;
        output_box_of(s, o, &ob);
        bool on_x = cx >= ob.x && cx < ob.x + ob.width;
        bool on_y = cy >= ob.y && cy < ob.y + ob.height;
        bool near_edge = false;
        switch (edge) {
        case SYN_DOCK_EDGE_TOP:
            near_edge = on_x && cy < ob.y + band; break;
        case SYN_DOCK_EDGE_LEFT:
            near_edge = on_y && cx < ob.x + band; break;
        case SYN_DOCK_EDGE_RIGHT:
            near_edge = on_y && cx >= ob.x + ob.width - band; break;
        case SYN_DOCK_EDGE_BOTTOM:
        default:
            near_edge = on_x && cy >= ob.y + ob.height - band; break;
        }
        if (near_edge || o->dock.shown ||
            (s->dock_drag.active && s->dock_drag.output == o))
            wlr_output_schedule_frame(o->wlr_output);
    }
}

/* ── Hit-testing ─────────────────────────────────────────── */

syn_dock_entry_t *dock_entry_at(syn_server_t *s, double lx, double ly)
{
    if (!s->config.dock_enabled) return NULL;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree || !o->dock.shown) continue;

        /* Entry hit-boxes are dock-canvas-local (identical on every output's
         * mirror); the tree's scene position is that canvas's layout-
         * coordinate origin (which already reflects any slide/float). */
        double rx = lx - o->dock.tree->node.x;
        double ry = ly - o->dock.tree->node.y;

        for (int i = 0; i < s->dock_entry_count; i++) {
            syn_dock_entry_t *e = &s->dock_entries[i];
            if (rx >= e->x && rx < e->x + e->w &&
                ry >= e->y && ry < e->y + e->h)
                return e;
        }
    }
    return NULL;
}

bool dock_bar_at(syn_server_t *s, double lx, double ly, syn_output_t **out)
{
    if (!s->config.dock_enabled) return false;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree || !o->dock.shown) continue;
        int bx, by, bw, bh;
        if (!dock_geometry(o, &bx, &by, &bw, &bh)) continue;
        /* Use the tree's live position (slide/float already applied). */
        double ox = o->dock.tree->node.x, oy = o->dock.tree->node.y;
        if (lx >= ox && lx < ox + bw && ly >= oy && ly < oy + bh) {
            if (out) *out = o;
            return true;
        }
    }
    return false;
}

void dock_entry_click(syn_server_t *s, syn_dock_entry_t *e)
{
    syn_view_t *v = e->primary_view;

    /* Pinned-but-not-running: launch its .desktop Exec. */
    if (!v || !v->mapped) {
        const syn_icon_entry_t *ic = icon_lookup(e->app_id);
        if (ic->exec[0]) synui_spawn(ic->exec);
        return;
    }

    if (v->minimized) {
        /* view_apply_minimized() itself raises+focuses on restore once the
         * workspace is visible — mirrors ft_handle_minimize. */
        if (v->workspace && !workspace_visible(v->workspace))
            workspace_switch(s, v->workspace->index);
        view_apply_minimized(s, v, 0);
        return;
    }

    if (s->focused_view == v) {
        view_apply_minimized(s, v, 1);
        return;
    }

    if (v->workspace && !workspace_visible(v->workspace))
        workspace_switch(s, v->workspace->index);
    focus_view(s, v, view_surface(v));
}

/* ── Drag to reposition ──────────────────────────────────── */

void dock_drag_begin(syn_server_t *s, double lx, double ly)
{
    syn_output_t *o = NULL;
    if (!dock_bar_at(s, lx, ly, &o) || !o) return;

    s->dock_drag.active  = 1;
    s->dock_drag.moved   = 0;
    s->dock_drag.output  = o;
    s->dock_drag.start_x = lx;
    s->dock_drag.start_y = ly;
}

void dock_drag_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->dock_drag.active) return;
    syn_output_t *o = s->dock_drag.output;
    if (!o || !o->dock.tree) return;

    if (!s->dock_drag.moved) {
        if (hypot(lx - s->dock_drag.start_x, ly - s->dock_drag.start_y)
                < DOCK_DRAG_THRESHOLD)
            return;
        s->dock_drag.moved = 1;
        o->dock.shown = 1;
        o->dock.slide_progress = 1.0;
    }

    int bx, by, bw, bh;
    if (!dock_geometry(o, &bx, &by, &bw, &bh)) return;
    s->dock_drag.float_x = lx - bw / 2.0;
    s->dock_drag.float_y = ly - bh / 2.0;
    dock_apply_position(o);
    wlr_output_schedule_frame(o->wlr_output);
}

/* Nearest screen edge to (lx,ly) within output box ob. */
static syn_dock_edge_t nearest_edge(struct wlr_box *ob, double lx, double ly)
{
    double dl = lx - ob->x;
    double dr = (ob->x + ob->width) - lx;
    double dt = ly - ob->y;
    double db = (ob->y + ob->height) - ly;
    double m = dl;
    syn_dock_edge_t edge = SYN_DOCK_EDGE_LEFT;
    if (dr < m) { m = dr; edge = SYN_DOCK_EDGE_RIGHT; }
    if (dt < m) { m = dt; edge = SYN_DOCK_EDGE_TOP; }
    if (db < m) { m = db; edge = SYN_DOCK_EDGE_BOTTOM; }
    return edge;
}

void dock_drag_end(syn_server_t *s, double lx, double ly)
{
    if (!s->dock_drag.active) return;

    bool moved = s->dock_drag.moved;
    syn_output_t *drag_o = s->dock_drag.output;
    s->dock_drag.active = 0;
    s->dock_drag.moved  = 0;
    s->dock_drag.output = NULL;

    if (!moved || !drag_o) {
        /* A press with no travel: not a reposition — just settle back. */
        if (drag_o) dock_apply_position(drag_o);
        return;
    }

    struct wlr_box ob;
    output_box_of(s, drag_o, &ob);
    syn_dock_edge_t edge = nearest_edge(&ob, lx, ly);

    if (edge != s->config.dock_edge) {
        s->config.dock_edge = edge;
        dock_state_save(s);
    }

    /* Land it shown on the new edge, then re-render every mirror in the new
     * orientation. */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        o->dock.shown = 1;
        o->dock.slide_progress = 1.0;
        o->dock.unhover_since = 0.0;
        o->dock.last_tick = 0.0;
    }
    dock_relayout(s);
    wl_list_for_each(o, &s->outputs, link)
        wlr_output_schedule_frame(o->wlr_output);
}

/* ── Pinning + persistence ───────────────────────────────── */

/* Resolve ~/.config/synui/dock.state; false if $HOME is unset. */
static bool dock_state_path(char *buf, size_t n)
{
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    snprintf(buf, n, "%s/.config/synui/dock.state", home);
    return true;
}

void dock_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!dock_state_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;   /* no persisted state — synuirc stands */

    /* The file, when present, is authoritative for both edge and the pin
     * set, so start the pin list empty and refill from `pin=` lines. */
    cfg->dock_pin_count = 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;

        if (strncmp(p, "edge=", 5) == 0) {
            const char *v = p + 5;
            if      (strcmp(v, "bottom") == 0) cfg->dock_edge = SYN_DOCK_EDGE_BOTTOM;
            else if (strcmp(v, "top")    == 0) cfg->dock_edge = SYN_DOCK_EDGE_TOP;
            else if (strcmp(v, "left")   == 0) cfg->dock_edge = SYN_DOCK_EDGE_LEFT;
            else if (strcmp(v, "right")  == 0) cfg->dock_edge = SYN_DOCK_EDGE_RIGHT;
        } else if (strncmp(p, "pin=", 4) == 0) {
            const char *v = p + 4;
            if (*v && cfg->dock_pin_count < DOCK_PIN_MAX) {
                snprintf(cfg->dock_pin[cfg->dock_pin_count], 128, "%s", v);
                cfg->dock_pin_count++;
            }
        }
    }
    fclose(f);
}

void dock_state_save(syn_server_t *s)
{
    char path[256];
    if (!dock_state_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: dock: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    static const char *edge_name[] = { "bottom", "top", "left", "right" };
    fprintf(f, "edge=%s\n", edge_name[s->config.dock_edge]);
    for (int i = 0; i < s->config.dock_pin_count; i++)
        fprintf(f, "pin=%s\n", s->config.dock_pin[i]);
    fclose(f);
}

void dock_pin_toggle(syn_server_t *s, const char *app_id)
{
    if (!app_id || !*app_id) return;
    syn_config_t *c = &s->config;

    int found = -1;
    for (int i = 0; i < c->dock_pin_count; i++)
        if (strcmp(c->dock_pin[i], app_id) == 0) { found = i; break; }

    if (found >= 0) {
        for (int i = found; i < c->dock_pin_count - 1; i++)
            memcpy(c->dock_pin[i], c->dock_pin[i + 1], 128);
        c->dock_pin_count--;
    } else if (c->dock_pin_count < DOCK_PIN_MAX) {
        snprintf(c->dock_pin[c->dock_pin_count], 128, "%s", app_id);
        c->dock_pin_count++;
    } else {
        wlr_log(WLR_ERROR, "synui: dock: pin list full (max %d)", DOCK_PIN_MAX);
        return;
    }

    dock_state_save(s);
    dock_rebuild(s);
}

/* ── Right-click context menu ────────────────────────────── */

#define DOCKMENU_ITEM_H 30
#define DOCKMENU_W      184

void dockmenu_open(syn_server_t *s, syn_dock_entry_t *e, double lx, double ly)
{
    snprintf(s->dockmenu.app_id, sizeof(s->dockmenu.app_id), "%s", e->app_id);

    int n = 0;
    s->dockmenu.actions[n++] = e->pinned ? SYN_DOCKACT_UNPIN : SYN_DOCKACT_PIN;

    const syn_icon_entry_t *ic = icon_lookup(e->app_id);
    if (ic->exec[0])
        s->dockmenu.actions[n++] = e->running ? SYN_DOCKACT_NEWWIN
                                              : SYN_DOCKACT_OPEN;
    if (e->running)
        s->dockmenu.actions[n++] = SYN_DOCKACT_QUIT;
    s->dockmenu.action_count = n;

    int w = DOCKMENU_W, h = n * DOCKMENU_ITEM_H + 8;

    /* Position above/left of the cursor so a bottom dock's menu pops upward,
     * then clamp within the output under the cursor. */
    int x = (int)lx, y = (int)ly - h;
    struct wlr_output *wo =
        wlr_output_layout_output_at(s->output_layout, lx, ly);
    if (wo && wo->data) {
        struct wlr_box ob;
        output_box_of(s, (syn_output_t *)wo->data, &ob);
        if (x + w > ob.x + ob.width) x = ob.x + ob.width - w;
        if (y < ob.y) y = ob.y;
        if (x < ob.x) x = ob.x;
        if (y + h > ob.y + ob.height) y = ob.y + ob.height - h;
    }
    s->dockmenu.x = x; s->dockmenu.y = y; s->dockmenu.w = w; s->dockmenu.h = h;
    s->dockmenu.selected = -1;
    s->dockmenu.visible = 1;
    synui_render_dockmenu(s);
}

/* Item index under (lx,ly), or -1 if outside the menu. */
static int dockmenu_item_at(syn_server_t *s, double lx, double ly)
{
    if (lx < s->dockmenu.x || lx >= s->dockmenu.x + s->dockmenu.w ||
        ly < s->dockmenu.y || ly >= s->dockmenu.y + s->dockmenu.h)
        return -1;
    int idx = (int)((ly - s->dockmenu.y - 4) / DOCKMENU_ITEM_H);
    if (idx < 0 || idx >= s->dockmenu.action_count) return -1;
    return idx;
}

void dockmenu_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->dockmenu.visible) return;
    int idx = dockmenu_item_at(s, lx, ly);
    if (idx != s->dockmenu.selected) {
        s->dockmenu.selected = idx;
        synui_render_dockmenu(s);
    }
}

void dockmenu_close(syn_server_t *s)
{
    if (!s->dockmenu.visible) return;
    s->dockmenu.visible = 0;
    synui_render_dockmenu(s);
}

void dockmenu_click(syn_server_t *s, double lx, double ly)
{
    if (!s->dockmenu.visible) return;
    int idx = dockmenu_item_at(s, lx, ly);
    if (idx < 0) { dockmenu_close(s); return; }   /* click outside → dismiss */

    syn_dockact_t act = s->dockmenu.actions[idx];
    char app_id[128];
    snprintf(app_id, sizeof(app_id), "%s", s->dockmenu.app_id);
    dockmenu_close(s);

    switch (act) {
    case SYN_DOCKACT_PIN:
    case SYN_DOCKACT_UNPIN:
        dock_pin_toggle(s, app_id);
        break;
    case SYN_DOCKACT_OPEN:
    case SYN_DOCKACT_NEWWIN: {
        const syn_icon_entry_t *ic = icon_lookup(app_id);
        if (ic->exec[0]) synui_spawn(ic->exec);
        break;
    }
    case SYN_DOCKACT_QUIT:
        for (int wi = 0; wi < WORKSPACE_MAX; wi++) {
            syn_view_t *v, *tmp;
            wl_list_for_each_safe(v, tmp, &s->workspaces[wi].windows, link) {
                if (!v->mapped) continue;
                const char *aid = view_app_id(v);
                if (aid && strcmp(aid, app_id) == 0) view_close(v);
            }
        }
        break;
    }
}
