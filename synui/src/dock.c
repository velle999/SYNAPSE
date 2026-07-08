/*
 * dock.c — macOS-style auto-hide dock
 *
 * Shows pinned (synuirc `dock_pin`) and currently-running apps as icons in a
 * bar mirrored on every output. Auto-hide only (no "always visible" mode):
 * hidden, the dock reserves zero layout space; shown, it simply floats above
 * window content (its scene tree sits alongside the welcome/overlay/dispcfg
 * UI trees, not parented under window_tree/layer_tree) — so unlike a
 * layer-shell panel, showing or hiding it never triggers a tiling relayout.
 * The entry model (which apps are pinned/running) is server-global; only the
 * per-output scene tree and its show/hide state live on syn_output::dock.
 *
 * SynapseOS Project — GPLv2
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cairo.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "synui.h"

#define DOCK_ICON_SIZE 48
#define DOCK_ICON_PAD  8

/* Auto-hide timing. The dock slides fully in/out over DOCK_SLIDE_SECS; once
 * the cursor leaves it stays put for DOCK_HIDE_DELAY before sliding away, so
 * brushing past the edge doesn't make it flicker. */
#define DOCK_SLIDE_SECS 0.16
#define DOCK_HIDE_DELAY 0.45

/* ── Entry model ─────────────────────────────────────────── */

void dock_rebuild(syn_server_t *s)
{
    syn_dock_entry_t merged[DOCK_MAX_ENTRIES];
    memset(merged, 0, sizeof(merged));
    int count = 0;

    /* Seed pinned entries in synuirc order. */
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

/* ── Rendering ───────────────────────────────────────────── */

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

/* Bar geometry for this output's mirror: fully-shown position (bx,by) and
 * size. Shared by the renderer and the auto-hide tick so both agree on where
 * the bar lives. */
static bool dock_geometry(syn_output_t *o, int *bx, int *by,
                          int *bar_w, int *bar_h)
{
    syn_server_t *s = o->server;
    int n = s->dock_entry_count;
    int icon = DOCK_ICON_SIZE, pad = DOCK_ICON_PAD;
    int w = n > 0 ? n * icon + (n + 1) * pad : pad * 2;
    int h = s->config.dock_height;

    struct wlr_box ob;
    output_box_of(s, o, &ob);
    if (ob.width <= 0 || ob.height <= 0) return false;

    int x = ob.x + (ob.width - w) / 2;
    if (x < ob.x) x = ob.x;   /* wider than the output: left-align, clip */
    *bx = x;
    *by = ob.y + ob.height - h;
    *bar_w = w;
    *bar_h = h;
    return true;
}

/* Place the tree at its slide offset and enable it only while any part is
 * on-screen. slide_progress 1 = flush at the bottom edge, 0 = pushed fully
 * below it. Called both after (re)rendering the buffer and every anim tick. */
static void dock_apply_position(syn_output_t *o)
{
    if (!o->dock.tree) return;

    int bx, by, bw, bh;
    if (!o->server->config.dock_enabled || !dock_geometry(o, &bx, &by, &bw, &bh)) {
        wlr_scene_node_set_enabled(&o->dock.tree->node, false);
        return;
    }

    double p = o->dock.slide_progress;
    int y = by + (int)lround((1.0 - p) * bh);
    wlr_scene_node_set_position(&o->dock.tree->node, bx, y);

    bool visible = p > 0.001;
    wlr_scene_node_set_enabled(&o->dock.tree->node, visible);
    if (visible)
        wlr_scene_node_raise_to_top(&o->dock.tree->node);
}

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
        int ix = pad + i * (icon + pad);
        int iy = (bar_h - icon) / 2 - 4;   /* room for the dot below */

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
            cairo_set_source_rgba(cr, 0.92, 0.92, 0.96, 0.9);
            cairo_arc(cr, ix + icon / 2.0, iy + icon + 6, 2.5, 0, 2 * M_PI);
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
    /* Auto-hide: start hidden (pushed below the bottom edge). The tick slides
     * it in when the cursor reaches the trigger strip. */
    o->dock.shown = 0;
    o->dock.slide_progress = 0.0;
    o->dock.hover_since = 0.0;
    o->dock.unhover_since = 0.0;
    o->dock.last_tick = 0.0;
    dock_render_output(o);
}

void dock_output_destroy(syn_output_t *o)
{
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

    bool on_output = cx >= ob.x && cx < ob.x + ob.width &&
                     cy >= ob.y && cy < ob.y + ob.height;
    /* Reveal trigger: the bottom `margin` px under the bar's footprint. */
    bool in_trigger = on_output && cy >= ob.y + ob.height - margin &&
                      cx >= bx - DOCK_ICON_PAD && cx < bx + bw + DOCK_ICON_PAD;
    /* Keep-shown region: anywhere over the fully-shown bar. */
    bool in_bar = on_output && cx >= bx && cx < bx + bw &&
                  cy >= by && cy < by + bh;
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
    /* Still shown but disengaged and not yet past the hide delay: keep frames
     * coming so the delay can fire without a further pointer event. */
    bool waiting_to_hide = !engaged && o->dock.shown;
    return animating || waiting_to_hide;
}

/* Pointer moved: wake the outputs whose dock might need to react (cursor near
 * the bottom edge, or a dock already on-screen that may now need to hide).
 * dock_tick does the actual state work on the frame this schedules. */
void dock_pointer_motion(syn_server_t *s)
{
    if (!s->config.dock_enabled) return;

    double cx = s->cursor->x, cy = s->cursor->y;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree) continue;
        struct wlr_box ob;
        output_box_of(s, o, &ob);
        bool near_bottom =
            cx >= ob.x && cx < ob.x + ob.width &&
            cy >= ob.y + ob.height - (s->config.dock_height + 8) &&
            cy < ob.y + ob.height;
        if (near_bottom || o->dock.shown)
            wlr_output_schedule_frame(o->wlr_output);
    }
}

/* ── Clicking ────────────────────────────────────────────── */

syn_dock_entry_t *dock_entry_at(syn_server_t *s, double lx, double ly)
{
    if (!s->config.dock_enabled) return NULL;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->dock.tree || !o->dock.shown) continue;

        /* Entry hit-boxes are dock-canvas-local (identical on every
         * output's mirror); the tree's scene position is that canvas's
         * layout-coordinate origin. */
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

void dock_entry_click(syn_server_t *s, syn_dock_entry_t *e)
{
    syn_view_t *v = e->primary_view;
    if (!v || !v->mapped) return;

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
