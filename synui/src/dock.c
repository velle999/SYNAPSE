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
#include <wlr/util/log.h>

#include "synui.h"

#define DOCK_ICON_SIZE 48
#define DOCK_ICON_PAD  8

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

static void dock_render_output(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!o->dock.tree) return;

    if (!s->config.dock_enabled) {
        wlr_scene_node_set_enabled(&o->dock.tree->node, false);
        return;
    }

    int n = s->dock_entry_count;
    int icon = DOCK_ICON_SIZE, pad = DOCK_ICON_PAD;
    int bar_w = n > 0 ? n * icon + (n + 1) * pad : pad * 2;
    int bar_h = s->config.dock_height;

    struct wlr_box ob;
    output_box_of(s, o, &ob);
    if (ob.width <= 0 || ob.height <= 0) return;

    int bx = ob.x + (ob.width - bar_w) / 2;
    if (bx < ob.x) bx = ob.x;   /* wider than the output: left-align, clip */
    int by = ob.y + ob.height - bar_h;

    wlr_scene_node_set_position(&o->dock.tree->node, bx, by);

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

    wlr_scene_node_set_enabled(&o->dock.tree->node, true);
    wlr_scene_node_raise_to_top(&o->dock.tree->node);
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
    /* Step-3 placeholder: always shown (no hover/slide gating yet — that
     * lands with the auto-hide trigger logic). */
    o->dock.shown = 1;
    o->dock.slide_progress = 1.0;
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
