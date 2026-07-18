/*
 * launcher.c — the "◢ SYNAPSE" start-menu button, drawn by the compositor.
 *
 * This used to be a waybar module (custom/synapse): a GTK button whose on-click
 * synthesised a Super tap so synui would open its own menu. That indirection was
 * a workaround, not a design — waybar could never own the menu (it sets
 * keyboard_interactivity NONE once and never revises it, so a GTK menu there is
 * deaf by construction; see the note in config/waybar/config.jsonc). With the
 * menu already synui's, the button belongs to synui too.
 *
 * So synui draws it directly, top-left of every output, over the waybar bar.
 * It is a UI-sibling scene tree like the dock's — created after the layer trees,
 * placed just above the top layer — so it floats over the bar without being a
 * layer surface itself. A left click calls menu_toggle() straight, no synthetic
 * keypress. Clicks are geometry-hit-tested in server_cursor_button (launcher_at)
 * ahead of forwarding, the same way the dock's icons are, so the bar underneath
 * never sees them.
 *
 * launcher_style (synuirc) picks text ("◢ SYNAPSE") or the dendrite logo.svg.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdbool.h>

#include <cairo.h>
#include <librsvg/rsvg.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Bar-height button. Matches config/waybar/config.jsonc "height": 28 so the
 * text lines up with the clock and tray to its right. */
#define LAUNCHER_H       28
#define LAUNCHER_PAD_X   12      /* matches #custom-synapse padding: 0 12px */
#define LAUNCHER_FONT    13.0
#define LAUNCHER_LOGO_SZ 20      /* emblem square in logo mode */

/* Waybar's #custom-synapse teal (#05d9e8). Kept in step by eye — the bar's CSS
 * and the compositor do not share a palette. */
static const double LAUNCHER_R = 0x05 / 255.0;
static const double LAUNCHER_G = 0xd9 / 255.0;
static const double LAUNCHER_B = 0xe8 / 255.0;

static const char *LAUNCHER_TEXT = "\xe2\x97\xa2 SYNAPSE";   /* ◢ SYNAPSE */

/* True while this output shows a mapped, non-minimized fullscreen window on the
 * active workspace — the launcher hides then, mirroring layer_update_occlusion
 * (which hides the bar) and dock_output_fullscreen (which tucks the dock). */
static bool launcher_output_fullscreen(syn_output_t *o)
{
    syn_server_t *s = o->server;
    syn_view_t *v;
    wl_list_for_each(v, &server_active_workspace(s)->windows, link) {
        if (v->output != o) continue;
        if (v->mapped && v->fullscreen && !v->minimized) return true;
    }
    return false;
}

/* Width the button needs for the current style. Text is measured on a throwaway
 * context so the real buffer can be sized to fit before it is created. */
static int launcher_width(syn_server_t *s)
{
    if (s->config.launcher_style == SYN_LAUNCHER_LOGO)
        return LAUNCHER_PAD_X + LAUNCHER_LOGO_SZ + LAUNCHER_PAD_X;

    cairo_surface_t *tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(tmp);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, LAUNCHER_FONT);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, LAUNCHER_TEXT, &ext);
    int w = LAUNCHER_PAD_X + (int)(ext.x_advance + 0.5) + LAUNCHER_PAD_X;
    cairo_destroy(cr);
    cairo_surface_destroy(tmp);
    return w;
}

/* Place the tree at this output's top-left and decide whether it shows. Called
 * after (re)rendering and whenever occlusion/outputs change (launcher_relayout).
 * The hit box (o->launcher.{x,y,w,h}) is the button's on-screen rectangle. */
static void launcher_apply_position(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!o->launcher.tree) return;

    struct wlr_box ob;
    output_box_of(s, o, &ob);

    if (ob.width <= 0 || launcher_output_fullscreen(o)) {
        wlr_scene_node_set_enabled(&o->launcher.tree->node, false);
        o->launcher.visible = 0;
        return;
    }

    o->launcher.x = ob.x;
    o->launcher.y = ob.y;
    wlr_scene_node_set_position(&o->launcher.tree->node, ob.x, ob.y);
    wlr_scene_node_set_enabled(&o->launcher.tree->node, true);
    /* Just above the top layer (the bar), not raise_to_top: the menu it opens,
     * the overlay, and the lock screen all live above and must keep covering it. */
    wlr_scene_node_place_above(&o->launcher.tree->node,
        &s->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_TOP]->node);
    o->launcher.visible = 1;
}

static void launcher_render_output(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!o->launcher.tree) return;

    int w = launcher_width(s), h = LAUNCHER_H;

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(w, h, &cr);
    if (!buf) return;
    cairo_begin(cr);   /* transparent — the bar's own background shows through */

    if (s->config.launcher_style == SYN_LAUNCHER_LOGO) {
        RsvgHandle *lh = rsvg_handle_new_from_file(SYNUI_DATADIR "/logo.svg", NULL);
        if (lh) {
            RsvgRectangle vp = { .x = LAUNCHER_PAD_X,
                                 .y = (h - LAUNCHER_LOGO_SZ) / 2.0,
                                 .width = LAUNCHER_LOGO_SZ,
                                 .height = LAUNCHER_LOGO_SZ };
            rsvg_handle_render_document(lh, cr, &vp, NULL);
            g_object_unref(lh);
        }
    } else {
        cairo_select_font_face(cr, "monospace",
                               CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, LAUNCHER_FONT);
        cairo_font_extents_t fe;
        cairo_font_extents(cr, &fe);
        /* Vertically centre the cap height in the bar. */
        double baseline = (h + fe.ascent - fe.descent) / 2.0;
        cairo_set_source_rgba(cr, LAUNCHER_R, LAUNCHER_G, LAUNCHER_B, 1.0);
        cairo_move_to(cr, LAUNCHER_PAD_X, baseline);
        cairo_show_text(cr, LAUNCHER_TEXT);
    }

    cairo_destroy(cr);
    set_scene_buffer(&o->launcher.buf, o->launcher.tree, buf);

    o->launcher.w = w;
    o->launcher.h = h;
    launcher_apply_position(o);
}

void launcher_output_created(syn_output_t *o)
{
    o->launcher.tree = wlr_scene_tree_create(&o->server->scene->tree);
    o->launcher.buf  = NULL;
    o->launcher.visible = 0;
    launcher_render_output(o);
}

void launcher_output_destroy(syn_output_t *o)
{
    if (o->launcher.tree) {
        wlr_scene_node_destroy(&o->launcher.tree->node);
        o->launcher.tree = NULL;
        o->launcher.buf = NULL;
    }
    o->launcher.visible = 0;
}

/* Rebuild every output's button — the style (text/logo) changed the pixels, so
 * a reposition is not enough. Used on config reload. */
void launcher_render_all(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        launcher_render_output(o);
}

/* Reposition + re-evaluate visibility on every output. Called next to
 * dock_relayout() from the occlusion and output-layout paths. */
void launcher_relayout(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        launcher_apply_position(o);
}

bool launcher_at(syn_server_t *s, double lx, double ly)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->launcher.visible) continue;
        if (lx >= o->launcher.x && lx < o->launcher.x + o->launcher.w &&
            ly >= o->launcher.y && ly < o->launcher.y + o->launcher.h)
            return true;
    }
    return false;
}
