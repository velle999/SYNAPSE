/*
 * render.c — Cairo-based UI rendering for synui
 *
 * Creates and manages scene-graph nodes for the compositor's own UI:
 *   - Welcome screen   (keybinding hints on empty desktop)
 *   - Command bar       (Super+Space AI input)
 *   - Neural overlay    (Super+A system status panel)
 *
 * Each element is a wlr_scene_tree containing:
 *   - wlr_scene_rect nodes for background / accent lines
 *   - wlr_scene_buffer node with cairo-rendered text
 *
 * SynapseOS Project — GPLv2
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>

#include <wlr/types/wlr_buffer.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include "synui.h"

/* DRM_FORMAT_ARGB8888 — same bit layout as CAIRO_FORMAT_ARGB32 on LE */
#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888 0x34325241
#endif

/* ── Cairo-backed wlr_buffer ─────────────────────────────── */

struct synui_cairo_buf {
    struct wlr_buffer base;
    cairo_surface_t *surface;
};

static void cairo_buf_destroy(struct wlr_buffer *wlr_buf)
{
    struct synui_cairo_buf *buf = wl_container_of(wlr_buf, buf, base);
    cairo_surface_destroy(buf->surface);
    free(buf);
}

static bool cairo_buf_begin_data_ptr_access(struct wlr_buffer *wlr_buf,
    uint32_t flags, void **data, uint32_t *format, size_t *stride)
{
    (void)flags;
    struct synui_cairo_buf *buf = wl_container_of(wlr_buf, buf, base);
    cairo_surface_flush(buf->surface);
    *data = cairo_image_surface_get_data(buf->surface);
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)cairo_image_surface_get_stride(buf->surface);
    return true;
}

static void cairo_buf_end_data_ptr_access(struct wlr_buffer *wlr_buf)
{
    (void)wlr_buf;
}

static const struct wlr_buffer_impl cairo_buf_impl = {
    .destroy = cairo_buf_destroy,
    .begin_data_ptr_access = cairo_buf_begin_data_ptr_access,
    .end_data_ptr_access = cairo_buf_end_data_ptr_access,
};

/*
 * Allocate a cairo image surface wrapped in a wlr_buffer.
 * Returns the buffer; writes the cairo context to *cr_out.
 * Caller must cairo_destroy(*cr_out) and wlr_buffer_drop(buf) when done.
 */
static struct wlr_buffer *create_cairo_buf(int w, int h, cairo_t **cr_out)
{
    struct synui_cairo_buf *buf = calloc(1, sizeof(*buf));
    if (!buf) return NULL;

    buf->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(buf->surface) != CAIRO_STATUS_SUCCESS) {
        free(buf);
        return NULL;
    }

    wlr_buffer_init(&buf->base, &cairo_buf_impl, w, h);
    *cr_out = cairo_create(buf->surface);
    return &buf->base;
}

/* ── Helpers ─────────────────────────────────────────────── */

/* Layout-space box of the output the UI should appear on (the focused one),
 * so panels centre on the active monitor rather than always output[0]. */
static void get_output_box(syn_server_t *s, struct wlr_box *box)
{
    server_output_box(s, box);
}

/*
 * Set a cairo buffer on a scene buffer node, creating the node if needed.
 * Drops our reference after the scene graph takes its own.
 */
static void set_scene_buffer(struct wlr_scene_buffer **node,
                             struct wlr_scene_tree *parent,
                             struct wlr_buffer *buf)
{
    if (!*node)
        *node = wlr_scene_buffer_create(parent, buf);
    else
        wlr_scene_buffer_set_buffer(*node, buf);
    wlr_buffer_drop(buf);
}

/* Start a cairo context cleared to transparent */
static void cairo_begin(cairo_t *cr)
{
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
}

/* ── Welcome screen ──────────────────────────────────────── */

/* Menu entries: input.c navigates with Up/Down and executes the entry's
 * bind action on Enter (welcome_menu_key). */
const syn_welcome_entry_t synui_welcome_menu[] = {
    { "Terminal",         "Super+Enter",   "term"     },
    { "AI Command Bar",   "Super+Space",   "cmdbar"   },
    { "Neural Overlay",   "Super+A",       "overlay"  },
    { "Display Settings", "Super+D",       "displays" },
    { "Quit synui",       "Super+Shift+Q", "quit"     },
};
const int synui_welcome_menu_len =
    (int)(sizeof(synui_welcome_menu) / sizeof(synui_welcome_menu[0]));

void synui_render_welcome(syn_server_t *s)
{
    struct wlr_box ob;
    get_output_box(s, &ob);

    int pw = 460, ph = 340;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->welcome_ui.tree->node, px, py);

    /* Background rect */
    if (!s->welcome_ui.bg) {
        float color[4] = { 0.08f, 0.08f, 0.14f, 0.92f };
        s->welcome_ui.bg = wlr_scene_rect_create(s->welcome_ui.tree,
                                                   pw, ph, color);
    }

    /* Brand accent line at top */
    if (!s->welcome_ui.accent) {
        float accent[4] = { 0.00f, 0.85f, 0.75f, 1.0f };
        s->welcome_ui.accent = wlr_scene_rect_create(s->welcome_ui.tree,
                                                       pw, 2, accent);
    }

    /* Cairo text buffer */
    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 28);
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
    cairo_move_to(cr, 76, 60);
    cairo_show_text(cr, "S Y N A P S E O S");

    /* Separator */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 30, 78);
    cairo_line_to(cr, pw - 30, 78);
    cairo_stroke(cr);

    /* Selectable menu (input.c: Up/Down + Enter) */
    cairo_set_font_size(cr, 15);
    int y = 112;
    for (int i = 0; i < synui_welcome_menu_len; i++) {
        int sel = (i == s->welcome_ui.selected);

        if (sel) {
            cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
            cairo_move_to(cr, 44, y);
            cairo_show_text(cr, ">");
            cairo_set_source_rgba(cr, 0.92, 0.98, 0.97, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.70, 0.70, 0.80, 1.0);
        }
        cairo_move_to(cr, 66, y);
        cairo_show_text(cr, synui_welcome_menu[i].label);

        cairo_set_source_rgba(cr, sel ? 0.0 : 0.45, sel ? 0.85 : 0.45,
                              sel ? 0.75 : 0.55, sel ? 1.0 : 1.0);
        cairo_move_to(cr, 280, y);
        cairo_show_text(cr, synui_welcome_menu[i].hint);

        y += 28;
    }

    /* Footer hints */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 44, y + 18);
    cairo_show_text(cr, "Up/Down + Enter select");
    cairo_move_to(cr, 44, y + 36);
    cairo_show_text(cr, "Super+1-9 workspaces \xc2\xb7 Super+Tab cycle layout");

    /* Version */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.35, 0.35, 0.45, 0.8);
    cairo_move_to(cr, 175, ph - 20);
    cairo_show_text(cr, SYNUI_VERSION);

    cairo_destroy(cr);
    set_scene_buffer(&s->welcome_ui.text_buf, s->welcome_ui.tree, buf);

    wlr_scene_node_set_enabled(&s->welcome_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->welcome_ui.tree->node);
    s->welcome_ui.shown = 1;
}

void synui_welcome_hide(syn_server_t *s)
{
    if (!s->welcome_ui.shown) return;
    wlr_scene_node_set_enabled(&s->welcome_ui.tree->node, false);
    s->welcome_ui.shown = 0;
}

/* ── Command bar ─────────────────────────────────────────── */

void synui_render_cmdbar(syn_server_t *s)
{
    struct wlr_box ob;
    get_output_box(s, &ob);

    int bw = 620, bh = 56;
    int bx = ob.x + (ob.width - bw) / 2, by = ob.y + ob.height - bh - 48;

    wlr_scene_node_set_position(&s->cmdbar_ui.tree->node, bx, by);

    if (!s->cmdbar.visible) {
        wlr_scene_node_set_enabled(&s->cmdbar_ui.tree->node, false);
        return;
    }
    wlr_scene_node_set_enabled(&s->cmdbar_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->cmdbar_ui.tree->node);

    /* Background */
    if (!s->cmdbar_ui.bg) {
        float color[4] = { 0.06f, 0.06f, 0.12f, 0.95f };
        s->cmdbar_ui.bg = wlr_scene_rect_create(s->cmdbar_ui.tree,
                                                  bw, bh, color);
    }

    /* Accent line */
    if (!s->cmdbar_ui.accent) {
        float accent[4] = { 0.00f, 0.85f, 0.75f, 1.0f };
        s->cmdbar_ui.accent = wlr_scene_rect_create(s->cmdbar_ui.tree,
                                                      bw, 2, accent);
    }

    /* Cairo text */
    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(bw, bh, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Prompt symbol */
    cairo_set_font_size(cr, 18);
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
    cairo_move_to(cr, 14, 30);
    cairo_show_text(cr, ">");

    /* Input text */
    cairo_set_source_rgba(cr, 0.92, 0.92, 0.96, 1.0);
    cairo_move_to(cr, 34, 30);
    if (s->cmdbar.input_len > 0)
        cairo_show_text(cr, s->cmdbar.input);

    /* Cursor block */
    if (!s->cmdbar.waiting) {
        cairo_text_extents_t ext;
        cairo_text_extents(cr, s->cmdbar.input_len ? s->cmdbar.input : "", &ext);
        double cx = 34 + ext.x_advance + 2;
        cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 0.7);
        cairo_rectangle(cr, cx, 14, 10, 20);
        cairo_fill(cr);
    }

    /* Response line */
    if (s->cmdbar.response[0]) {
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, s->cmdbar.waiting ? 0.45 : 0.65,
                              s->cmdbar.waiting ? 0.45 : 0.65,
                              s->cmdbar.waiting ? 0.55 : 0.75, 0.9);
        cairo_move_to(cr, 34, 50);
        cairo_show_text(cr, s->cmdbar.response);
    }

    cairo_destroy(cr);
    set_scene_buffer(&s->cmdbar_ui.text_buf, s->cmdbar_ui.tree, buf);
}

/* ── Neural overlay ──────────────────────────────────────── */

void synui_render_overlay(syn_server_t *s)
{
    struct wlr_box ob;
    get_output_box(s, &ob);

    int pw = 340, ph = 200;
    int px = ob.x + ob.width - pw - 20, py = ob.y + 20;

    wlr_scene_node_set_position(&s->overlay_ui.tree->node, px, py);

    if (!s->overlay.visible) {
        wlr_scene_node_set_enabled(&s->overlay_ui.tree->node, false);
        return;
    }
    wlr_scene_node_set_enabled(&s->overlay_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->overlay_ui.tree->node);

    /* Background */
    if (!s->overlay_ui.bg) {
        float color[4] = { 0.05f, 0.05f, 0.10f, 0.88f };
        s->overlay_ui.bg = wlr_scene_rect_create(s->overlay_ui.tree,
                                                   pw, ph, color);
    }

    /* Accent */
    if (!s->overlay_ui.accent) {
        float accent[4] = { 0.00f, 0.85f, 0.75f, 1.0f };
        s->overlay_ui.accent = wlr_scene_rect_create(s->overlay_ui.tree,
                                                       pw, 2, accent);
    }

    /* Cairo text */
    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 13);
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
    cairo_move_to(cr, 14, 24);
    cairo_show_text(cr, "NEURAL OVERLAY");

    /* Separator */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.4);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 14, 34);
    cairo_line_to(cr, pw - 14, 34);
    cairo_stroke(cr);

    int y = 56;
    cairo_set_font_size(cr, 13);

    /* synapd status */
    cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
    cairo_move_to(cr, 14, y);
    cairo_show_text(cr, "synapd:");
    cairo_set_source_rgba(cr, 0.85, 0.85, 0.92, 1.0);
    cairo_move_to(cr, 100, y);
    cairo_show_text(cr, s->overlay.synapd_status[0]
                        ? s->overlay.synapd_status : "not connected");
    y += 24;

    /* Workspace */
    syn_workspace_t *ws = server_active_workspace(s);
    cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
    cairo_move_to(cr, 14, y);
    cairo_show_text(cr, "workspace:");
    cairo_set_source_rgba(cr, 0.85, 0.85, 0.92, 1.0);
    char ws_info[64];
    snprintf(ws_info, sizeof(ws_info), "%s [%d]", ws->name, ws->index + 1);
    cairo_move_to(cr, 120, y);
    cairo_show_text(cr, ws_info);
    y += 24;

    /* Layout */
    static const char *layout_names[] = {
        "tiling", "floating", "monocle", "AI"
    };
    cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
    cairo_move_to(cr, 14, y);
    cairo_show_text(cr, "layout:");
    cairo_set_source_rgba(cr, 0.85, 0.85, 0.92, 1.0);
    cairo_move_to(cr, 100, y);
    cairo_show_text(cr, layout_names[ws->layout]);
    y += 24;

    /* Window count */
    int wc = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped) wc++;
    cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
    cairo_move_to(cr, 14, y);
    cairo_show_text(cr, "windows:");
    cairo_set_source_rgba(cr, 0.85, 0.85, 0.92, 1.0);
    char wcbuf[16];
    snprintf(wcbuf, sizeof(wcbuf), "%d", wc);
    cairo_move_to(cr, 110, y);
    cairo_show_text(cr, wcbuf);
    y += 24;

    /* AI context line */
    if (s->overlay.ai_context[0]) {
        cairo_set_font_size(cr, 11);
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.8);
        cairo_move_to(cr, 14, y);
        char ctx[52];
        snprintf(ctx, sizeof(ctx), "%.50s", s->overlay.ai_context);
        cairo_show_text(cr, ctx);
    }

    cairo_destroy(cr);
    set_scene_buffer(&s->overlay_ui.text_buf, s->overlay_ui.tree, buf);
}

/* ── Display settings panel ──────────────────────────────── */

static const char *transform_name(enum wl_output_transform t)
{
    switch (t) {
    case WL_OUTPUT_TRANSFORM_NORMAL:      return "normal";
    case WL_OUTPUT_TRANSFORM_90:          return "90\xc2\xb0";
    case WL_OUTPUT_TRANSFORM_180:         return "180\xc2\xb0";
    case WL_OUTPUT_TRANSFORM_270:         return "270\xc2\xb0";
    case WL_OUTPUT_TRANSFORM_FLIPPED:     return "flipped";
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:  return "flip-90\xc2\xb0";
    case WL_OUTPUT_TRANSFORM_FLIPPED_180: return "flip-180\xc2\xb0";
    case WL_OUTPUT_TRANSFORM_FLIPPED_270: return "flip-270\xc2\xb0";
    }
    return "?";
}

void synui_render_dispcfg(syn_server_t *s)
{
    syn_dispcfg_t *d = &s->dispcfg;

    if (!d->visible) {
        wlr_scene_node_set_enabled(&s->dispcfg_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    int rows = d->count > 0 ? d->count : 1;
    int pw = 560, ph = 196 + rows * 28;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->dispcfg_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->dispcfg_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->dispcfg_ui.tree->node);

    /* Background + accent; the panel height depends on the monitor count,
     * so resize them on every render (hotplug can change the count). */
    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4]   = { 0.00f, 0.85f, 0.75f, 1.0f };
    if (!s->dispcfg_ui.bg)
        s->dispcfg_ui.bg = wlr_scene_rect_create(s->dispcfg_ui.tree,
                                                 pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->dispcfg_ui.bg, pw, ph);
    if (!s->dispcfg_ui.accent)
        s->dispcfg_ui.accent = wlr_scene_rect_create(s->dispcfg_ui.tree,
                                                     pw, 2, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 15);
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "DISPLAY SETTINGS");

    /* Separator */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 42);
    cairo_line_to(cr, pw - 18, 42);
    cairo_stroke(cr);

    /* Monitor rows: name, mode, rotation, layout position */
    cairo_set_font_size(cr, 14);
    int y = 70;
    if (d->count == 0) {
        cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
        cairo_move_to(cr, 40, y);
        cairo_show_text(cr, "no outputs connected");
        y += 28;
    }
    for (int i = 0; i < d->count; i++) {
        struct wlr_output *wo = d->order[i]->wlr_output;
        int sel = (i == d->selected);

        int w, h;
        wlr_output_effective_resolution(wo, &w, &h);
        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, wo, &box);

        if (sel) {
            cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
            cairo_move_to(cr, 18, y);
            cairo_show_text(cr, ">");
            cairo_set_source_rgba(cr, 0.92, 0.98, 0.97, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.70, 0.70, 0.80, 1.0);
        }
        cairo_move_to(cr, 40, y);
        cairo_show_text(cr, wo->name);

        char col[48];
        snprintf(col, sizeof(col), "%dx%d", w, h);
        cairo_move_to(cr, 190, y);
        cairo_show_text(cr, col);

        cairo_move_to(cr, 310, y);
        cairo_show_text(cr, transform_name(wo->transform));

        snprintf(col, sizeof(col), "(%d,%d)", box.x, box.y);
        cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
        cairo_move_to(cr, 430, y);
        cairo_show_text(cr, col);

        y += 28;
    }

    /* Arrangement axis */
    cairo_set_font_size(cr, 13);
    cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
    cairo_move_to(cr, 18, y + 8);
    cairo_show_text(cr, d->column
        ? "arrangement: column (top\xe2\x86\x92" "bottom)"
        : "arrangement: row (left\xe2\x86\x92right)");

    /* Status line (last action or error) */
    if (d->status[0]) {
        cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 0.9);
        cairo_move_to(cr, 18, y + 30);
        cairo_show_text(cr, d->status);
    }

    /* Controls legend */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 40);
    cairo_show_text(cr, "Up/Down monitor \xc2\xb7 Left/Right rotate \xc2\xb7 [ ] reorder");
    cairo_move_to(cr, 18, ph - 20);
    cairo_show_text(cr, "a row/column \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->dispcfg_ui.text_buf, s->dispcfg_ui.tree, buf);
}

/* ── Initialize all UI scene trees ───────────────────────── */

void synui_ui_init(syn_server_t *s)
{
    /* Create scene trees — later children render on top */
    s->welcome_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->overlay_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->dispcfg_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->cmdbar_ui.tree  = wlr_scene_tree_create(&s->scene->tree);

    /* All hidden until explicitly shown */
    wlr_scene_node_set_enabled(&s->welcome_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->overlay_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->dispcfg_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->cmdbar_ui.tree->node, false);

    /* Render welcome screen (uses fallback 1920x1080 until output connects) */
    synui_render_welcome(s);
}
