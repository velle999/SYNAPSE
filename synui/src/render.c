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
#include <unistd.h>
#include <cairo.h>
#include <librsvg/rsvg.h>

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
struct wlr_buffer *create_cairo_buf(int w, int h, cairo_t **cr_out)
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
void set_scene_buffer(struct wlr_scene_buffer **node,
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
void cairo_begin(cairo_t *cr)
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
    { "Terminal",         "Super+Enter",   "term"      },
    { "AI Command Bar",   "Super+Space",   "cmdbar"    },
    { "Neural Overlay",   "Super+A",       "overlay"   },
    { "Display Settings", "Super+D",       "displays"  },
    { "Wallpaper",        "Super+W",       "wallpaper" },
    { "Power Saving",     "Super+P",       "power"     },
    { "Task Manager",     "Super+T",       "taskmgr"   },
    { "Game Mode",        "Super+G",       "game"      },
    { "Lock Screen",      "Super+L",       "lock"      },
    { "AI Backend",       "GPU/CPU",       "ai_backend"},
    { "Quit synui",       "Super+Shift+Q", "quit"      },
};
const int synui_welcome_menu_len =
    (int)(sizeof(synui_welcome_menu) / sizeof(synui_welcome_menu[0]));

/* Live AI-backend label for the "AI Backend" row's hint. synui-ai-backend
 * writes "gpu" or "cpu" to /run/synapd/backend when it toggles synapd; if the
 * file is absent (nothing toggled yet) synapd's own default is auto-detect, so
 * show "auto". Kept tiny + best-effort — a read failure just falls back. */
static const char *synui_ai_backend_label(void)
{
    FILE *f = fopen("/run/synapd/backend", "r");
    if (!f) return "auto";
    char b[16] = {0};
    size_t n = fread(b, 1, sizeof(b) - 1, f);
    fclose(f);
    while (n > 0 && (b[n - 1] == '\n' || b[n - 1] == ' ')) b[--n] = 0;
    if (strcmp(b, "gpu") == 0) return "GPU";
    if (strcmp(b, "cpu") == 0) return "CPU";
    return "auto";
}

void synui_render_welcome(syn_server_t *s)
{
    struct wlr_box ob;
    get_output_box(s, &ob);

    int pw = 500, ph = 474;
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

    /* SynapseOS dendrite emblem, top-left of the header (transparent SVG
     * rendered straight into the panel's cairo context via librsvg, the same
     * path icons.c uses for app icons). Best-effort: a missing/broken asset
     * just leaves the header text as before. */
    {
        RsvgHandle *lh =
            rsvg_handle_new_from_file(SYNUI_DATADIR "/logo.svg", NULL);
        if (lh) {
            RsvgRectangle vp = { .x = 26, .y = 18, .width = 64, .height = 64 };
            rsvg_handle_render_document(lh, cr, &vp, NULL);
            g_object_unref(lh);
        }
    }

    /* Title — vertically centred against the 64px emblem */
    cairo_set_font_size(cr, 30);
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
    cairo_move_to(cr, 106, 62);
    cairo_show_text(cr, "SYNAPSEOS");

    /* Separator, clear of the taller emblem */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 30, 96);
    cairo_line_to(cr, pw - 30, 96);
    cairo_stroke(cr);

    /* Selectable menu (input.c: Up/Down + Enter) */
    cairo_set_font_size(cr, 15);
    int y = 128;
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

        /* The AI Backend row shows the live synapd backend instead of a fixed
         * keybind — it has no default bind, it toggles in place on Enter. */
        const char *hint = synui_welcome_menu[i].hint;
        if (strcmp(synui_welcome_menu[i].action, "ai_backend") == 0)
            hint = synui_ai_backend_label();

        cairo_set_source_rgba(cr, sel ? 0.0 : 0.45, sel ? 0.85 : 0.45,
                              sel ? 0.75 : 0.55, sel ? 1.0 : 1.0);
        cairo_move_to(cr, 290, y);
        cairo_show_text(cr, hint);

        y += 28;
    }

    /* Footer hints */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 44, y + 16);
    cairo_show_text(cr, "Up/Down + Enter select");
    cairo_move_to(cr, 44, y + 34);
    cairo_show_text(cr, "Super+1-9 workspaces \xc2\xb7 Super+Tab cycle layout");
    cairo_move_to(cr, 44, y + 52);
    cairo_show_text(cr, "Super+E filters \xc2\xb7 Super+O move monitor \xc2\xb7 Super+Q close");

    /* Version */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.35, 0.35, 0.45, 0.8);
    cairo_move_to(cr, 190, ph - 16);
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

    int pw = 360, ph = 320;
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

    syn_overlay_t *ov = &s->overlay;

    /* Title */
    cairo_set_font_size(cr, 13);
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
    cairo_move_to(cr, 14, 24);
    cairo_show_text(cr, "NEURAL OVERLAY");

    /* Model badge, right-aligned on the title row. Colour tracks state:
     * teal = loaded, amber = loading, grey = none/offline. */
    const char *badge;
    double br, bg, bb;
    if (!ov->mon_online) {
        badge = "\xe2\x97\x8b offline"; br = 0.55; bg = 0.55; bb = 0.60;
    } else if (strcmp(ov->model, "loaded") == 0) {
        badge = "\xe2\x9a\xa1 loaded";  br = 0.0;  bg = 0.85; bb = 0.55;
    } else if (strcmp(ov->model, "loading") == 0) {
        badge = "\xe2\x97\x8b loading"; br = 0.95; bg = 0.70; bb = 0.20;
    } else {
        badge = "\xe2\x97\x8b no model"; br = 0.55; bg = 0.55; bb = 0.60;
    }
    cairo_text_extents_t bext;
    cairo_text_extents(cr, badge, &bext);
    cairo_set_source_rgba(cr, br, bg, bb, 1.0);
    cairo_move_to(cr, pw - 14 - bext.width, 24);
    cairo_show_text(cr, badge);

    /* Separator */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.4);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 14, 34);
    cairo_line_to(cr, pw - 14, 34);
    cairo_stroke(cr);

    int y = 56;

    /* Request counters */
    cairo_set_font_size(cr, 13);
    char stat[96];
    snprintf(stat, sizeof(stat), "req %lu    active %lu",
             ov->requests, ov->active);
    cairo_set_source_rgba(cr, 0.85, 0.85, 0.92, 1.0);
    cairo_move_to(cr, 14, y);
    cairo_show_text(cr, stat);
    /* Highlight in-flight work in teal when there is any. */
    if (ov->active > 0) {
        cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
        cairo_move_to(cr, 14, y + 3);
        cairo_arc(cr, pw - 22, y - 4, 4, 0, 2 * 3.14159);
        cairo_fill(cr);
    }
    y += 22;

    /* Context-window gauge */
    if (ov->ctx_window > 0) {
        char cbuf[64];
        snprintf(cbuf, sizeof(cbuf), "ctx %u / %u tok",
                 ov->ctx_used, ov->ctx_window);
        cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
        cairo_move_to(cr, 14, y);
        cairo_show_text(cr, cbuf);

        /* Thin fill bar under the text. */
        double frac = (double)ov->ctx_used / (double)ov->ctx_window;
        if (frac > 1.0) frac = 1.0;
        int bx = 14, bw = pw - 28, byy = y + 8, bh = 4;
        cairo_set_source_rgba(cr, 0.25, 0.25, 0.32, 0.8);
        cairo_rectangle(cr, bx, byy, bw, bh);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 0.9);
        cairo_rectangle(cr, bx, byy, (int)(bw * frac), bh);
        cairo_fill(cr);
        y += 26;
    } else {
        y += 4;
    }

    /* Activity feed heading */
    cairo_set_font_size(cr, 11);
    cairo_set_source_rgba(cr, 0.45, 0.60, 0.65, 0.9);
    cairo_move_to(cr, 14, y);
    cairo_show_text(cr, "live activity");
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.4);
    cairo_move_to(cr, 92, y - 4);
    cairo_line_to(cr, pw - 14, y - 4);
    cairo_stroke(cr);
    y += 18;

    /* Recent synapd events (newest last). */
    if (ov->activity_n == 0) {
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.7);
        cairo_move_to(cr, 14, y);
        cairo_show_text(cr, ov->mon_online ? "(idle — no recent events)"
                                           : "(synapd unavailable)");
    } else {
        cairo_set_font_size(cr, 11);
        for (int i = 0; i < ov->activity_n; i++) {
            cairo_set_source_rgba(cr, 0.70, 0.72, 0.80, 0.95);
            cairo_move_to(cr, 14, y);
            char line[80];
            snprintf(line, sizeof(line), "%.72s", ov->activity[i]);
            cairo_show_text(cr, line);
            y += 16;
        }
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

    /* Mini-map geometry: one box per monitor, placed by grid cell (the same
     * grid_x/grid_y dispcfg_rechain() packs into real pixel positions) so
     * "where is what in the grid" has a visual answer instead of just the
     * grid(x,y) numbers in the list below. */
    const int cell_w = 96, cell_h = 60, cell_gap = 10, map_top = 54;
    int min_gx = 0, max_gx = 0, min_gy = 0, max_gy = 0;
    if (d->count > 0) {
        min_gx = max_gx = d->order[0]->grid_x;
        min_gy = max_gy = d->order[0]->grid_y;
        for (int i = 1; i < d->count; i++) {
            syn_output_t *o = d->order[i];
            if (o->grid_x < min_gx) min_gx = o->grid_x;
            if (o->grid_x > max_gx) max_gx = o->grid_x;
            if (o->grid_y < min_gy) min_gy = o->grid_y;
            if (o->grid_y > max_gy) max_gy = o->grid_y;
        }
    }
    int map_cols = d->count > 0 ? max_gx - min_gx + 1 : 0;
    int map_rows = d->count > 0 ? max_gy - min_gy + 1 : 0;
    int map_w = map_cols > 0 ? map_cols * cell_w + (map_cols - 1) * cell_gap : 0;
    int map_h = map_rows > 0 ? map_rows * cell_h + (map_rows - 1) * cell_gap : 0;

    int rows = d->count > 0 ? d->count : 1;
    int list_top = d->count > 0 ? map_top + map_h + 24 : 70;
    int pw = d->count > 0 && map_w + 36 > 620 ? map_w + 36 : 620;
    int ph = list_top + rows * 28 + 126;
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

    /* Mini-map: a box per monitor at its grid cell, selected one accented. */
    for (int i = 0; i < d->count; i++) {
        syn_output_t *o = d->order[i];
        int sel = (i == d->selected);
        int cx = 18 + (o->grid_x - min_gx) * (cell_w + cell_gap);
        int cy = map_top + (o->grid_y - min_gy) * (cell_h + cell_gap);

        if (sel)
            cairo_set_source_rgba(cr, 0.00, 0.35, 0.32, 1.0);
        else
            cairo_set_source_rgba(cr, 0.14, 0.14, 0.20, 1.0);
        cairo_rectangle(cr, cx, cy, cell_w, cell_h);
        cairo_fill(cr);

        cairo_set_line_width(cr, sel ? 2 : 1);
        if (sel)
            cairo_set_source_rgba(cr, 0.00, 0.85, 0.75, 1.0);
        else
            cairo_set_source_rgba(cr, 0.35, 0.35, 0.45, 0.8);
        cairo_rectangle(cr, cx + 0.5, cy + 0.5, cell_w - 1, cell_h - 1);
        cairo_stroke(cr);

        char label[64];
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.75, sel ? 1.0 : 0.75,
                              sel ? 0.99 : 0.85, 1.0);
        cairo_move_to(cr, cx + 8, cy + 24);
        cairo_show_text(cr, o->wlr_output->name);

        cairo_set_font_size(cr, 11);
        cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
        snprintf(label, sizeof(label), "(%d,%d)", o->grid_x, o->grid_y);
        cairo_move_to(cr, cx + 8, cy + 42);
        cairo_show_text(cr, label);
    }

    /* Monitor rows: name, mode, rotation, layout position */
    cairo_set_font_size(cr, 14);
    int y = list_top;
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

        cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
        snprintf(col, sizeof(col), "grid(%d,%d)",
                 d->order[i]->grid_x, d->order[i]->grid_y);
        cairo_move_to(cr, 430, y);
        cairo_show_text(cr, col);

        snprintf(col, sizeof(col), "(%d,%d)", box.x, box.y);
        cairo_move_to(cr, 540, y);
        cairo_show_text(cr, col);

        y += 28;
    }

    /* Status line (last action or error) */
    if (d->status[0]) {
        cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 0.9);
        cairo_move_to(cr, 18, y + 8);
        cairo_show_text(cr, d->status);
    }

    /* Controls legend */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 40);
    cairo_show_text(cr, "Up/Down select \xc2\xb7 Left/Right rotate");
    cairo_move_to(cr, 18, ph - 20);
    cairo_show_text(cr, "Shift+arrows move in grid (swaps) \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->dispcfg_ui.text_buf, s->dispcfg_ui.tree, buf);
}

/* ── Wallpaper selector (wppick.c) ───────────────────────── */

void synui_render_wppick(syn_server_t *s)
{
    if (!s->wppick.visible) {
        wlr_scene_node_set_enabled(&s->wppick_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    const int row_h = 48, top = 58, pad = 22;
    int pw = 440;
    int ph = top + wppick_option_count * row_h + 56;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->wppick_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->wppick_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->wppick_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4]   = { 0.00f, 0.85f, 0.75f, 1.0f };
    if (!s->wppick_ui.bg)
        s->wppick_ui.bg = wlr_scene_rect_create(s->wppick_ui.tree,
                                                pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->wppick_ui.bg, pw, ph);
    if (!s->wppick_ui.accent)
        s->wppick_ui.accent = wlr_scene_rect_create(s->wppick_ui.tree,
                                                    pw, 2, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 15);
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "WALLPAPER");

    /* Separator */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 42);
    cairo_line_to(cr, pw - 18, 42);
    cairo_stroke(cr);

    /* Options: highlighted row gets a filled bar + accent border. */
    for (int i = 0; i < wppick_option_count; i++) {
        int sel = (i == s->wppick.selected);
        int ry = top + i * row_h;

        if (sel) {
            cairo_set_source_rgba(cr, 0.00, 0.35, 0.32, 1.0);
            cairo_rectangle(cr, 12, ry, pw - 24, row_h - 8);
            cairo_fill(cr);
            cairo_set_line_width(cr, 2);
            cairo_set_source_rgba(cr, 0.00, 0.85, 0.75, 1.0);
            cairo_rectangle(cr, 12.5, ry + 0.5, pw - 25, row_h - 9);
            cairo_stroke(cr);
        }

        cairo_set_font_size(cr, 15);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.78, sel ? 1.0 : 0.78,
                              sel ? 0.99 : 0.86, 1.0);
        cairo_move_to(cr, pad + 8, ry + 22);
        cairo_show_text(cr, wppick_options[i].label);

        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, sel ? 0.70 : 0.50, sel ? 0.80 : 0.50,
                              sel ? 0.85 : 0.60, 1.0);
        cairo_move_to(cr, pad + 8, ry + 38);
        cairo_show_text(cr, wppick_options[i].desc);
    }

    /* Controls legend */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 20);
    cairo_show_text(cr, "Up/Down preview \xc2\xb7 Enter/Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->wppick_ui.text_buf, s->wppick_ui.tree, buf);
}

/* ── Power saving panel (power.c) ────────────────────────── */

void synui_render_power(syn_server_t *s)
{
    syn_power_t *p = &s->power;

    if (!p->visible) {
        wlr_scene_node_set_enabled(&s->power_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    const int row_h = 30, top = 66, pad = 18;
    int pw = 520;
    int ph = top + POWER_ROW_COUNT * row_h + 96;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->power_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->power_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->power_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4]   = { 0.00f, 0.85f, 0.75f, 1.0f };
    if (!s->power_ui.bg)
        s->power_ui.bg = wlr_scene_rect_create(s->power_ui.tree,
                                               pw, ph, bg_color);
    if (!s->power_ui.accent)
        s->power_ui.accent = wlr_scene_rect_create(s->power_ui.tree,
                                                   pw, 2, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 15);
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "POWER SAVING");

    /* An inhibitor beats every timeout, so say so where it can't be missed
     * rather than letting the panel imply the timeouts are counting down. */
    cairo_set_font_size(cr, 12);
    if (s->idle_inhibitors > 0) {
        cairo_set_source_rgba(cr, 0.95, 0.75, 0.25, 1.0);
        cairo_move_to(cr, 18, 50);
        cairo_show_text(cr, "idle inhibited (media playing) \xc2\xb7 timers held");
    } else if (!s->config.power_enabled) {
        cairo_set_source_rgba(cr, 0.75, 0.45, 0.45, 1.0);
        cairo_move_to(cr, 18, 50);
        cairo_show_text(cr, "disabled \xc2\xb7 no stage will fire");
    }

    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 58);
    cairo_line_to(cr, pw - 18, 58);
    cairo_stroke(cr);

    for (int i = 0; i < POWER_ROW_COUNT; i++) {
        int sel = (i == p->selected);
        int ry = top + i * row_h;

        if (sel) {
            cairo_set_source_rgba(cr, 0.00, 0.35, 0.32, 1.0);
            cairo_rectangle(cr, 12, ry - 16, pw - 24, row_h - 4);
            cairo_fill(cr);
        }

        char name[48], value[32];
        power_panel_rows(s, i, name, sizeof(name), value, sizeof(value));

        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.78, sel ? 1.0 : 0.78,
                              sel ? 0.99 : 0.86, 1.0);
        cairo_move_to(cr, pad + 8, ry + 4);
        cairo_show_text(cr, name);

        /* A stage at "never" is inert; grey it out so the panel reads at a
         * glance as "these three are armed, those two are not". */
        int off = (i == POWER_ROW_ENABLED) ? !s->config.power_enabled
                                           : (strcmp(value, "never") == 0);
        if (off) cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
        else     cairo_set_source_rgba(cr, 0.00, 0.85, 0.75, 1.0);
        cairo_move_to(cr, 330, ry + 4);
        cairo_show_text(cr, value);
    }

    if (p->status[0]) {
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 0.9);
        cairo_move_to(cr, 18, ph - 56);
        cairo_show_text(cr, p->status);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 34);
    cairo_show_text(cr, "Up/Down select \xc2\xb7 Left/Right adjust \xc2\xb7 Space toggle");
    cairo_move_to(cr, 18, ph - 16);
    cairo_show_text(cr, p->dirty ? "s save (unsaved changes) \xc2\xb7 Esc close"
                                 : "s save \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->power_ui.text_buf, s->power_ui.tree, buf);
}

/* ── Task manager (taskmgr.c) ────────────────────────────── */

/* Panel geometry. The column x's are tuned for the 13px monospace face the
 * rows are drawn in; the number columns are right-aligned to their x, so they
 * line up under headings whose text is a different width. */
#define TM_W        660
#define TM_ROW_H    22
#define TM_COL_PID   18
#define TM_COL_NAME  92
#define TM_COL_CPU  372   /* right edge */
#define TM_COL_MEM  462   /* right edge */
#define TM_COL_VRAM 560   /* right edge */
#define TM_COL_WIN  580

static void draw_right(cairo_t *cr, double x_right, double y, const char *text)
{
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    cairo_move_to(cr, x_right - ext.width, y);
    cairo_show_text(cr, text);
}

/* A meter. frac < 0 means "this back end can't report the value" — draw the
 * trough only, so an unknown reads as unknown rather than as zero. */
static void draw_bar(cairo_t *cr, double x, double y, double w, double h,
                     double frac, double r, double g, double b)
{
    cairo_set_source_rgba(cr, 0.16, 0.16, 0.22, 1.0);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    if (frac < 0) return;
    if (frac > 1.0) frac = 1.0;

    cairo_set_source_rgba(cr, r, g, b, 1.0);
    cairo_rectangle(cr, x, y, w * frac, h);
    cairo_fill(cr);
}

/* Hot values go amber then red: the point of a resource panel is that a
 * saturated bar catches the eye without being read. */
static void bar_color(double frac, double *r, double *g, double *b)
{
    if (frac >= 0.90)      { *r = 0.90; *g = 0.30; *b = 0.35; }
    else if (frac >= 0.70) { *r = 0.95; *g = 0.75; *b = 0.25; }
    else                   { *r = 0.00; *g = 0.85; *b = 0.75; }
}

/* KiB → the largest unit that keeps the number under four digits. */
static void fmt_kb(unsigned long kb, char *buf, size_t n)
{
    if (kb >= 1024UL * 1024UL)
        snprintf(buf, n, "%.1fG", (double)kb / (1024.0 * 1024.0));
    else if (kb >= 1024UL)
        snprintf(buf, n, "%.0fM", (double)kb / 1024.0);
    else
        snprintf(buf, n, "%luK", kb);
}

/* "6.9 / 12.0G" — used against total, for the meter labels. */
static void fmt_kb_pair(unsigned long used, unsigned long total,
                        char *buf, size_t n)
{
    if (!total) { snprintf(buf, n, "n/a"); return; }
    snprintf(buf, n, "%.1f / %.1fG",
             (double)used / (1024.0 * 1024.0),
             (double)total / (1024.0 * 1024.0));
}

/* One "LABEL [====----] value" line of the system overview. */
static double draw_meter(cairo_t *cr, double y, const char *label,
                         double frac, const char *value)
{
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.62, 0.66, 0.72, 1.0);
    cairo_move_to(cr, 18, y);
    cairo_show_text(cr, label);

    double r, g, b;
    bar_color(frac < 0 ? 0 : frac, &r, &g, &b);
    draw_bar(cr, 76, y - 9, 200, 10, frac, r, g, b);

    cairo_set_source_rgba(cr, 0.86, 0.90, 0.94, 1.0);
    cairo_move_to(cr, 292, y);
    cairo_show_text(cr, value);

    return y + 22;
}

void synui_render_taskmgr(syn_server_t *s)
{
    syn_taskmgr_t *t = &s->taskmgr;

    if (!t->visible) {
        wlr_scene_node_set_enabled(&s->taskmgr_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    /* A machine with no GPU still gets one line saying so, so the panel does
     * not silently look like it forgot to draw the GPU section. */
    int gpu_lines = s->gpu_n ? s->gpu_n * 2 : 1;

    const int sys_top = 62;
    int table_top = sys_top + (2 + gpu_lines) * 22 + 22;
    int pw = TM_W;
    int ph = table_top + 20 + TASKMGR_ROWS * TM_ROW_H + 74;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->taskmgr_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->taskmgr_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->taskmgr_ui.tree->node);

    /* Denser than the other panels, so it is more opaque than their 0.94: at
     * that alpha the wallpaper (the Matrix one animates) and the welcome
     * screen's menu text ghost straight through the rows and the small type
     * stops being readable. */
    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.985f };
    float accent[4]   = { 0.00f, 0.85f, 0.75f, 1.0f };
    if (!s->taskmgr_ui.bg)
        s->taskmgr_ui.bg = wlr_scene_rect_create(s->taskmgr_ui.tree,
                                                 pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->taskmgr_ui.bg, pw, ph);
    if (!s->taskmgr_ui.accent)
        s->taskmgr_ui.accent = wlr_scene_rect_create(s->taskmgr_ui.tree,
                                                     pw, 2, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title, with the live sort key — the table's order is not otherwise
     * self-evident once every column has plausible-looking numbers in it. */
    cairo_set_font_size(cr, 15);
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "TASK MANAGER");

    char sub[96];
    snprintf(sub, sizeof(sub), "%d procs \xc2\xb7 sort: %s%s",
             t->n, taskmgr_sort_label(t->sort), t->own_only ? " \xc2\xb7 mine" : "");
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
    draw_right(cr, pw - 18, 30, sub);

    /* System overview */
    double y = sys_top;
    char val[96];

    double cpu_frac = t->cpu_pct / 100.0;
    snprintf(val, sizeof(val), "%.0f%%  (%ld cores)", t->cpu_pct,
             sysconf(_SC_NPROCESSORS_ONLN));
    y = draw_meter(cr, y, "CPU", cpu_frac, val);

    double mem_frac = t->mem_total_kb
                        ? (double)t->mem_used_kb / (double)t->mem_total_kb : 0;
    fmt_kb_pair(t->mem_used_kb, t->mem_total_kb, val, sizeof(val));
    if (t->swap_used_kb) {
        char sw[32];
        fmt_kb(t->swap_used_kb, sw, sizeof(sw));
        size_t used = strlen(val);
        snprintf(val + used, sizeof(val) - used, "  \xc2\xb7 swap %s", sw);
    }
    y = draw_meter(cr, y, "RAM", mem_frac, val);

    if (!s->gpu_n) {
        y = draw_meter(cr, y, "GPU", -1.0, "no GPU telemetry available");
    } else {
        for (int i = 0; i < s->gpu_n; i++) {
            syn_gpu_t *g = &s->gpu[i];

            if (g->util >= 0) snprintf(val, sizeof(val), "%d%%  \xc2\xb7 %s",
                                       g->util, g->name);
            else              snprintf(val, sizeof(val), "n/a  \xc2\xb7 %s", g->name);
            y = draw_meter(cr, y, "GPU", g->util >= 0 ? g->util / 100.0 : -1.0,
                           val);

            double vf = g->vram_total_kb
                          ? (double)g->vram_used_kb / (double)g->vram_total_kb
                          : -1.0;
            fmt_kb_pair(g->vram_used_kb, g->vram_total_kb, val, sizeof(val));

            /* Temperature and draw ride along on the VRAM line rather than
             * taking a third one: they are glanceable, not scanned. */
            size_t used = strlen(val);
            if (g->temp_c >= 0)
                used += (size_t)snprintf(val + used, sizeof(val) - used,
                                         "  \xc2\xb7 %d\xc2\xb0\x43", g->temp_c);
            if (g->power_w >= 0)
                snprintf(val + used, sizeof(val) - used, "  %dW", g->power_w);

            y = draw_meter(cr, y, "VRAM", vf, val);
        }
    }

    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, table_top - 26);
    cairo_line_to(cr, pw - 18, table_top - 26);
    cairo_stroke(cr);

    /* Column headings */
    cairo_set_font_size(cr, 11);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
    cairo_move_to(cr, TM_COL_PID, table_top - 8);
    cairo_show_text(cr, "PID");
    cairo_move_to(cr, TM_COL_NAME, table_top - 8);
    cairo_show_text(cr, "NAME");
    draw_right(cr, TM_COL_CPU,  table_top - 8, "CPU%");
    draw_right(cr, TM_COL_MEM,  table_top - 8, "MEM");
    draw_right(cr, TM_COL_VRAM, table_top - 8, "VRAM");

    for (int row = 0; row < TASKMGR_ROWS; row++) {
        int i = t->scroll + row;
        if (i >= t->n) break;

        syn_tm_proc_t *p = &t->procs[i];
        int sel = (i == t->selected);
        int ry = table_top + 14 + row * TM_ROW_H;

        if (sel) {
            cairo_set_source_rgba(cr, 0.00, 0.35, 0.32, 1.0);
            cairo_rectangle(cr, 12, ry - 14, pw - 24, TM_ROW_H - 2);
            cairo_fill(cr);
        }

        cairo_set_font_size(cr, 13);

        char txt[48];
        snprintf(txt, sizeof(txt), "%d", (int)p->pid);
        cairo_set_source_rgba(cr, 0.50, 0.54, 0.62, 1.0);
        cairo_move_to(cr, TM_COL_PID, ry);
        cairo_show_text(cr, txt);

        cairo_set_source_rgba(cr, sel ? 0.95 : 0.80, sel ? 1.00 : 0.84,
                              sel ? 0.99 : 0.90, 1.0);
        cairo_move_to(cr, TM_COL_NAME, ry);
        cairo_show_text(cr, p->name);

        /* Colour the CPU figure by load so a runaway process is visible in
         * peripheral vision even when the table is sorted by something else. */
        double load = p->cpu / 100.0;
        if (load >= 0.9)      cairo_set_source_rgba(cr, 0.90, 0.30, 0.35, 1.0);
        else if (load >= 0.3) cairo_set_source_rgba(cr, 0.95, 0.75, 0.25, 1.0);
        else                  cairo_set_source_rgba(cr, 0.62, 0.66, 0.72, 1.0);
        snprintf(txt, sizeof(txt), "%.1f", p->cpu);
        draw_right(cr, TM_COL_CPU, ry, txt);

        cairo_set_source_rgba(cr, 0.62, 0.66, 0.72, 1.0);
        fmt_kb(p->rss_kb, txt, sizeof(txt));
        draw_right(cr, TM_COL_MEM, ry, txt);

        if (p->vram_kb) {
            cairo_set_source_rgba(cr, 0.45, 0.80, 0.55, 1.0);
            fmt_kb(p->vram_kb, txt, sizeof(txt));
        } else {
            cairo_set_source_rgba(cr, 0.32, 0.34, 0.42, 1.0);
            snprintf(txt, sizeof(txt), "\xe2\x80\x93");   /* en dash */
        }
        draw_right(cr, TM_COL_VRAM, ry, txt);

        if (p->has_window) {
            cairo_set_source_rgba(cr, 0.00, 0.85, 0.75, 0.9);
            cairo_move_to(cr, TM_COL_WIN, ry);
            cairo_show_text(cr, "\xe2\x96\xa3");          /* has a window */
        }
    }

    /* Confirmation line, or the last action's result. The confirmation gets
     * the whole width and a red bar behind it: it is the one place in this
     * panel where the next keystroke is destructive. */
    int foot = ph - 52;
    if (t->confirm != TM_CONFIRM_NONE) {
        cairo_set_source_rgba(cr, 0.45, 0.10, 0.14, 1.0);
        cairo_rectangle(cr, 12, foot - 16, pw - 24, 24);
        cairo_fill(cr);

        char q[160];
        snprintf(q, sizeof(q), "%s %s (%d)?  y / n",
                 t->confirm == TM_CONFIRM_KILL ? "SIGKILL" : "SIGTERM",
                 t->confirm_name, (int)t->confirm_pid);
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 1.0, 0.86, 0.86, 1.0);
        cairo_move_to(cr, 18, foot);
        cairo_show_text(cr, q);
    } else if (t->status[0]) {
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, 0.0, 0.85, 0.75, 0.9);
        cairo_move_to(cr, 18, foot);
        cairo_show_text(cr, t->status);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 20);
    cairo_show_text(cr, "j/k move \xc2\xb7 c/m/g/p sort \xc2\xb7 u mine \xc2\xb7 "
                        "x term \xc2\xb7 X kill \xc2\xb7 r refresh \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->taskmgr_ui.text_buf, s->taskmgr_ui.tree, buf);
}

/* ── Dock right-click context menu (dock.c) ──────────────── */

static const char *dockact_label(syn_dockact_t a)
{
    switch (a) {
    case SYN_DOCKACT_PIN:    return "Pin to Dock";
    case SYN_DOCKACT_UNPIN:  return "Unpin from Dock";
    case SYN_DOCKACT_OPEN:   return "Open";
    case SYN_DOCKACT_NEWWIN: return "New Window";
    case SYN_DOCKACT_QUIT:   return "Quit";
    }
    return "?";
}

void synui_render_dockmenu(syn_server_t *s)
{
    if (!s->dockmenu.visible) {
        wlr_scene_node_set_enabled(&s->dockmenu_ui.tree->node, false);
        return;
    }

    int pw = s->dockmenu.w, ph = s->dockmenu.h;
    const int item_h = 30;

    wlr_scene_node_set_position(&s->dockmenu_ui.tree->node,
                                s->dockmenu.x, s->dockmenu.y);
    wlr_scene_node_set_enabled(&s->dockmenu_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->dockmenu_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.96f };
    if (!s->dockmenu_ui.bg)
        s->dockmenu_ui.bg = wlr_scene_rect_create(s->dockmenu_ui.tree,
                                                  pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->dockmenu_ui.bg, pw, ph);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Border */
    cairo_set_source_rgba(cr, 0.00, 0.85, 0.75, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, 0.5, 0.5, pw - 1, ph - 1);
    cairo_stroke(cr);

    for (int i = 0; i < s->dockmenu.action_count; i++) {
        int iy = 4 + i * item_h;
        int sel = (i == s->dockmenu.selected);
        if (sel) {
            cairo_set_source_rgba(cr, 0.00, 0.35, 0.32, 1.0);
            cairo_rectangle(cr, 3, iy, pw - 6, item_h);
            cairo_fill(cr);
        }
        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.82, sel ? 1.0 : 0.82,
                              sel ? 0.99 : 0.90, 1.0);
        cairo_move_to(cr, 14, iy + 20);
        cairo_show_text(cr, dockact_label(s->dockmenu.actions[i]));
    }

    cairo_destroy(cr);
    set_scene_buffer(&s->dockmenu_ui.text_buf, s->dockmenu_ui.tree, buf);
}

/* ── Initialize all UI scene trees ───────────────────────── */

void synui_ui_init(syn_server_t *s)
{
    /* Create scene trees — later children render on top */
    s->welcome_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->overlay_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->dispcfg_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->wppick_ui.tree  = wlr_scene_tree_create(&s->scene->tree);
    s->power_ui.tree   = wlr_scene_tree_create(&s->scene->tree);
    /* The dim overlay covers the scene, so it needs a tree of its own that
     * can be raised above every window without dragging the panel with it. */
    s->power_ui.dim_tree = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_set_enabled(&s->power_ui.dim_tree->node, true);
    s->taskmgr_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->dockmenu_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->cmdbar_ui.tree  = wlr_scene_tree_create(&s->scene->tree);

    /* All hidden until explicitly shown */
    wlr_scene_node_set_enabled(&s->welcome_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->overlay_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->dispcfg_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->wppick_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->taskmgr_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->dockmenu_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->cmdbar_ui.tree->node, false);

    /* Render welcome screen (uses fallback 1920x1080 until output connects) */
    synui_render_welcome(s);
}
