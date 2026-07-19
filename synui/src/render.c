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
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

/* ── Panel accent ────────────────────────────────────────────
 * Every compositor-drawn panel used to hard-code the house neon cyan for its
 * headers, selections and rules — so switching themes reskinned window chrome
 * but left the menu and every overlay stuck on SYNAPSE's colours. The accent is
 * now theme data: theme_load_colors() pushes it here (render_set_panel_accent),
 * and set_accent() draws with it. A file-scope cache keeps every draw helper
 * free of a server handle — rendering only ever runs on the main loop, so there
 * is no concurrent writer. The initialiser is SYNAPSE's cyan, so a render before
 * any theme is applied looks exactly as it always did. */
static float g_panel_accent[3] = { 0.00f, 0.85f, 0.75f };

void render_set_panel_accent(const float rgb[4])
{
    g_panel_accent[0] = rgb[0];
    g_panel_accent[1] = rgb[1];
    g_panel_accent[2] = rgb[2];
}

/* cairo_set_source_rgba with the active panel accent at alpha `a`. */
static inline void set_accent(cairo_t *cr, double a)
{
    cairo_set_source_rgba(cr, g_panel_accent[0], g_panel_accent[1],
                          g_panel_accent[2], a);
}

/* ── Welcome screen ──────────────────────────────────────── */

/* Menu entries: input.c navigates with Up/Down and executes the entry's
 * bind action on Enter (welcome_menu_key). */
const syn_welcome_entry_t synui_welcome_menu[] = {
    { "Control Panel",    "Super+C",       "control"   },
    { "Terminal",         "Super+Enter",   "term"      },
    { "AI Command Bar",   "Super+Space",   "cmdbar"    },
    { "Neural Overlay",   "Super+A",       "overlay"   },
    { "Display Settings", "Super+D",       "displays"  },
    { "Wallpaper",        "Super+W",       "wallpaper" },
    { "Power Saving",     "Super+P",       "power"     },
    { "Task Manager",     "Ctrl+Alt+Del",  "taskmgr"   },
    { "News",             "Super+R",       "news"      },
    { "Network / Wi-Fi",  "Super+I",       "network"   },
    { "Game Mode",        "Super+G",       "game"      },
    { "Cat Mode",         "Super+Shift+C", "cat"       },
    { "Lock Screen",      "Super+L",       "lock"      },
    { "AI Backend",       "GPU/CPU/off",   "ai_backend"},
    { "Show At Startup",  "[x]",           "welcome_startup" },
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
    if (strcmp(b, "off") == 0) return "off";
    return "auto";
}

void synui_render_welcome(syn_server_t *s)
{
    struct wlr_box ob;
    get_output_box(s, &ob);

    /* Height follows the menu rather than being a constant someone has to
     * remember to bump: the rows start at WELCOME_TOP and step WELCOME_ROW_H,
     * and the footer + version sit below them. With a hard-coded height, adding
     * a menu entry silently pushed the footer off the bottom of the panel.
     *
     * Prefixed, unlike everything else local to this function: the start menu's
     * geometry is MENU_* in synui.h now, and these are a different panel's
     * numbers that happened to be spelled the same. */
    /* WELCOME_FOOTER_H covers three hint lines (at y+16/+34/+52) *and* the
     * version line, which is drawn from the bottom at ph-16 — too small a value
     * and the two collide rather than the footer simply being cut off. */
    const int WELCOME_TOP = 128, WELCOME_ROW_H = 28, WELCOME_FOOTER_H = 92;
    int pw = 500;
    int ph = WELCOME_TOP + synui_welcome_menu_len * WELCOME_ROW_H + WELCOME_FOOTER_H;
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
        float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
        s->welcome_ui.accent = wlr_scene_rect_create(s->welcome_ui.tree,
                                                       pw, 2, accent);
    } else {
        wlr_scene_rect_set_color(s->welcome_ui.accent, (float[4]){
            g_panel_accent[0], g_panel_accent[1], g_panel_accent[2], 1.0f });
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
    set_accent(cr, 1.0);
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
    int y = WELCOME_TOP;
    for (int i = 0; i < synui_welcome_menu_len; i++) {
        int sel = (i == s->welcome_ui.selected);

        if (sel) {
            set_accent(cr, 1.0);
            cairo_move_to(cr, 44, y);
            cairo_show_text(cr, ">");
            cairo_set_source_rgba(cr, 0.92, 0.98, 0.97, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.70, 0.70, 0.80, 1.0);
        }
        cairo_move_to(cr, 66, y);
        cairo_show_text(cr, synui_welcome_menu[i].label);

        /* The AI Backend row shows the live synapd backend instead of a fixed
         * keybind — it has no default bind, it toggles in place on Enter. So
         * does Show At Startup, whose hint is its own checkbox. */
        const char *hint = synui_welcome_menu[i].hint;
        if (strcmp(synui_welcome_menu[i].action, "ai_backend") == 0)
            hint = synui_ai_backend_label();
        else if (strcmp(synui_welcome_menu[i].action, "welcome_startup") == 0)
            hint = s->config.welcome_at_startup ? "[x]" : "[ ]";

        cairo_set_source_rgba(cr, sel ? 0.0 : 0.45, sel ? 0.85 : 0.45,
                              sel ? 0.75 : 0.55, sel ? 1.0 : 1.0);
        cairo_move_to(cr, 290, y);
        cairo_show_text(cr, hint);

        y += WELCOME_ROW_H;
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

/* Resolve ~/.config/synui/welcome.state; false if $HOME is unset. */
static bool welcome_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "welcome.state");
}

void welcome_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!welcome_state_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;   /* never toggled — the synuirc line stands */

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "show_at_startup=", 16) == 0)
            cfg->welcome_at_startup = atoi(line + 16) ? 1 : 0;
    }
    fclose(f);
}

void welcome_state_save(syn_config_t *cfg)
{
    char path[256];
    if (!welcome_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: welcome: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    fprintf(f, "show_at_startup=%d\n", cfg->welcome_at_startup ? 1 : 0);
    fclose(f);
}

/* ── Command bar ─────────────────────────────────────────── */

void synui_render_cmdbar(syn_server_t *s)
{
    struct wlr_box ob;
    get_output_box(s, &ob);

    /* The bar grows downward-anchored to fit whatever the last CMD: printed
     * (ai_interface.c fills cmdbar.out): `by` is derived from the final height,
     * so extra rows push the top edge up and the bar stays put above the dock.
     * A command that printed nothing leaves out_lines at 0 and the bar keeps
     * its original single-line size. */
    const int row_h = 17;                 /* matches the 13px monospace face */
    int rows = s->cmdbar.out_lines + (s->cmdbar.out_more > 0 ? 1 : 0);

    /* Wider only when there is tabular output to hold: 620px fits ~73 columns
     * of the monospace face, which cuts `df -h` mid-table. Text past the edge
     * is clipped by the cairo buffer itself, which is sized to the bar. */
    int bw = rows ? 860 : 620;
    /* Rows begin one row_h below the response baseline at y=50, after a 12px
     * gap, so the first sits at y=79 and the last at 79+(rows-1)*row_h; +6
     * clears its descenders. Keep in step with the draw loop below. */
    int bh = 56 + (rows ? rows * row_h + 12 : 0);
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
    /* Created once, but bw/bh move with the output — resize every pass. */
    wlr_scene_rect_set_size(s->cmdbar_ui.bg, bw, bh);

    /* Accent line */
    if (!s->cmdbar_ui.accent) {
        float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
        s->cmdbar_ui.accent = wlr_scene_rect_create(s->cmdbar_ui.tree,
                                                      bw, 2, accent);
    } else {
        wlr_scene_rect_set_color(s->cmdbar_ui.accent, (float[4]){
            g_panel_accent[0], g_panel_accent[1], g_panel_accent[2], 1.0f });
    }
    wlr_scene_rect_set_size(s->cmdbar_ui.accent, bw, 2);

    /* Cairo text */
    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(bw, bh, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Prompt symbol */
    cairo_set_font_size(cr, 18);
    set_accent(cr, 1.0);
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
        set_accent(cr, 0.7);
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

    /* Captured stdout+stderr of the last CMD:, under the command that made it.
     * Every row is already sanitised to valid UTF-8 (cmdcap_sanitize) — cairo
     * puts its whole context into a permanent error state on a single bad byte,
     * and one row of `ls` output could otherwise blank every row below it. */
    if (rows) {
        int y = 50 + 12 + row_h;
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 0.80, 0.84, 0.88, 0.95);
        for (int i = 0; i < s->cmdbar.out_lines; i++) {
            cairo_move_to(cr, 34, y);
            cairo_show_text(cr, s->cmdbar.out[i]);
            y += row_h;
        }
        if (s->cmdbar.out_more > 0) {
            char more[64];
            snprintf(more, sizeof more, "+%d more line%s",
                     s->cmdbar.out_more, s->cmdbar.out_more == 1 ? "" : "s");
            cairo_set_source_rgba(cr, 0.55, 0.58, 0.66, 0.9);
            cairo_move_to(cr, 34, y);
            cairo_show_text(cr, more);
        }
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
        float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
        s->overlay_ui.accent = wlr_scene_rect_create(s->overlay_ui.tree,
                                                       pw, 2, accent);
    } else {
        wlr_scene_rect_set_color(s->overlay_ui.accent, (float[4]){
            g_panel_accent[0], g_panel_accent[1], g_panel_accent[2], 1.0f });
    }

    /* Cairo text */
    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    syn_overlay_t *ov = &s->overlay;

    /* Title */
    cairo_set_font_size(cr, 13);
    set_accent(cr, 1.0);
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
        set_accent(cr, 1.0);
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
        set_accent(cr, 0.9);
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
    /* 760 min: the rows run out to the PRIMARY tag at x=630. */
    int pw = d->count > 0 && map_w + 36 > 760 ? map_w + 36 : 760;
    int ph = list_top + rows * 28 + 126;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->dispcfg_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->dispcfg_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->dispcfg_ui.tree->node);

    /* Background + accent; the panel height depends on the monitor count,
     * so resize them on every render (hotplug can change the count). */
    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->dispcfg_ui.bg)
        s->dispcfg_ui.bg = wlr_scene_rect_create(s->dispcfg_ui.tree,
                                                 pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->dispcfg_ui.bg, pw, ph);
    if (!s->dispcfg_ui.accent)
        s->dispcfg_ui.accent = wlr_scene_rect_create(s->dispcfg_ui.tree,
                                                     pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->dispcfg_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
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
            set_accent(cr, 0.35);
        else
            cairo_set_source_rgba(cr, 0.14, 0.14, 0.20, 1.0);
        cairo_rectangle(cr, cx, cy, cell_w, cell_h);
        cairo_fill(cr);

        cairo_set_line_width(cr, sel ? 2 : 1);
        if (sel)
            set_accent(cr, 1.0);
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

    /* Monitor rows: name, mode, rotation, layout position, primary */
    syn_output_t *primary = server_primary_output(s);
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
            set_accent(cr, 1.0);
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

        /* The X11 primary — where SDL games open. Dimmer when it's only the
         * largest-output fallback rather than a choice the user made, so the
         * panel doesn't imply a setting that isn't actually stored. */
        if (d->order[i] == primary) {
            int explicit_choice = d->order[i]->primary;
            set_accent(cr, explicit_choice ? 0.95 : 0.45);
            cairo_move_to(cr, 630, y);
            cairo_show_text(cr, explicit_choice ? "PRIMARY" : "primary (auto)");
        }

        y += 28;
    }

    /* Status line (last action or error) */
    if (d->status[0]) {
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, y + 8);
        cairo_show_text(cr, d->status);
    }

    /* Controls legend */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 40);
    cairo_show_text(cr, "Up/Down select \xc2\xb7 Left/Right rotate \xc2\xb7 "
                        "p set primary (X11/game default)");
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

    /* The browse list can run to WPPICK_FOUND_MAX images, so the panel shows a
     * window of WPPICK_ROWS rows and scrolls rather than growing off-screen. */
    const int row_h = 48, top = 58, pad = 22;
    int total = wppick_total(s);
    int shown = total < WPPICK_ROWS ? total : WPPICK_ROWS;
    if (shown < 1) shown = 1;

    int pw = 520;
    int ph = top + shown * row_h + 56;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->wppick_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->wppick_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->wppick_ui.tree->node);

    /* More opaque than the 0.94 the sparser panels use: the browse list puts a
     * small-type path under every row, and at 0.94 whatever is behind the panel
     * (the welcome menu, or the wallpaper itself) reads straight through them. */
    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.985f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->wppick_ui.bg)
        s->wppick_ui.bg = wlr_scene_rect_create(s->wppick_ui.tree,
                                                pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->wppick_ui.bg, pw, ph);
    if (!s->wppick_ui.accent)
        s->wppick_ui.accent = wlr_scene_rect_create(s->wppick_ui.tree,
                                                    pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->wppick_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "WALLPAPER");

    /* Scaling mode, right-aligned on the title row. fill/fit/stretch/center
     * existed for ages but only as a synuirc key, so nobody knew they were
     * there — show the current one and how to change it. */
    {
        syn_wallpaper_mode_t m = s->config.wallpaper_mode;
        const char *mname = (m >= 0 && m < SYN_WALLPAPER_MODE_COUNT)
                            ? syn_wallpaper_mode_names[m] : "?";
        char label[64];
        snprintf(label, sizeof(label), "[m] %s", mname);
        cairo_set_font_size(cr, 12);
        cairo_text_extents_t te;
        cairo_text_extents(cr, label, &te);
        cairo_set_source_rgba(cr, 0.75, 0.55, 0.95, 1.0);
        cairo_move_to(cr, pw - 18 - te.width, 30);
        cairo_show_text(cr, label);
    }

    /* Separator */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 42);
    cairo_line_to(cr, pw - 18, 42);
    cairo_stroke(cr);

    /* Options: highlighted row gets a filled bar + accent border. */
    for (int r = 0; r < shown; r++) {
        int i = s->wppick.scroll + r;
        if (i >= total) break;

        int sel = (i == s->wppick.selected);
        int ry = top + r * row_h;

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, 12, ry, pw - 24, row_h - 8);
            cairo_fill(cr);
            cairo_set_line_width(cr, 2);
            set_accent(cr, 1.0);
            cairo_rectangle(cr, 12.5, ry + 0.5, pw - 25, row_h - 9);
            cairo_stroke(cr);
        }

        const char *label, *desc;
        wppick_row(s, i, &label, &desc);

        cairo_set_font_size(cr, 15);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.78, sel ? 1.0 : 0.78,
                              sel ? 0.99 : 0.86, 1.0);
        cairo_move_to(cr, pad + 8, ry + 22);
        cairo_show_text(cr, label);

        /* A found image's subtitle is its full path, which can be far wider
         * than the panel — clip it to the row so it cannot spill over the
         * border. */
        cairo_save(cr);
        cairo_rectangle(cr, pad, ry + 26, pw - 2 * pad - 8, 16);
        cairo_clip(cr);
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, sel ? 0.70 : 0.50, sel ? 0.80 : 0.50,
                              sel ? 0.85 : 0.60, 1.0);
        cairo_move_to(cr, pad + 8, ry + 38);
        cairo_show_text(cr, desc);
        cairo_restore(cr);
    }

    /* Scroll position, when there is more than one screenful. */
    if (total > shown) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", s->wppick.selected + 1, total);
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
        cairo_move_to(cr, pw - 62, 30);
        cairo_show_text(cr, pos);
    }

    /* Controls legend */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 20);
    cairo_show_text(cr, "Up/Down preview \xc2\xb7 r rescan \xc2\xb7 Enter/Esc close");

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
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->power_ui.bg)
        s->power_ui.bg = wlr_scene_rect_create(s->power_ui.tree,
                                               pw, ph, bg_color);
    if (!s->power_ui.accent)
        s->power_ui.accent = wlr_scene_rect_create(s->power_ui.tree,
                                                   pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->power_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "POWER SAVING");

    /* An inhibitor beats every timeout, so say so where it can't be missed
     * rather than letting the panel imply the timeouts are counting down. */
    cairo_set_font_size(cr, 12);
    if (idle_inhibited(s)) {
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
            set_accent(cr, 0.35);
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
        else     set_accent(cr, 1.0);
        cairo_move_to(cr, 330, ry + 4);
        cairo_show_text(cr, value);
    }

    if (p->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
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

/* ── CRT filter panel (filters.c) ────────────────────────── */

/* Right-align text to x_right. Shared by the control panel and the task
 * manager, whose number columns only line up if they end on the same x. */
static void draw_right(cairo_t *cr, double x_right, double y, const char *text)
{
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    cairo_move_to(cr, x_right - ext.width, y);
    cairo_show_text(cr, text);
}

/* Cut text to fit a column, with an ellipsis. Monospace, so a width is a
 * character count — but measure anyway: a headline can carry an em dash or a
 * CJK glyph, and those are not one cell wide.
 *
 * Lives up here with draw_right rather than down in the news section it was
 * written for: the start menu clips app names with it too, and both callers
 * have to sit below the definition. */
static void draw_clipped(cairo_t *cr, double x, double y, double max_w,
                         const char *text)
{
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);

    if (ext.width <= max_w) {
        cairo_move_to(cr, x, y);
        cairo_show_text(cr, text);
        return;
    }

    char buf[256];
    size_t len = strlen(text);
    if (len > sizeof(buf) - 4) len = sizeof(buf) - 4;

    while (len > 0) {
        /* Never cut inside a UTF-8 sequence: cairo refuses invalid UTF-8 by
         * poisoning the whole context, so one badly-cut headline would blank
         * every row under it. news_utf8_trim drops a partial character whole. */
        len = news_utf8_trim(text, len);
        if (len == 0) break;

        memcpy(buf, text, len);
        memcpy(buf + len, "\xe2\x80\xa6", 4);   /* … */
        cairo_text_extents(cr, buf, &ext);
        if (ext.width <= max_w) break;
        len--;
    }
    if (len == 0) return;

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, buf);
}

/* The slider itself: a trough with a filled portion. Drawn rather than spelled
 * out because these values are judged by eye, and a bar you can see moving is
 * the whole difference between tuning a look and typing numbers at it. */
static void draw_slider(cairo_t *cr, double x, double y, double w, double h,
                        double frac, int sel, int dimmed)
{
    cairo_set_source_rgba(cr, 0.16, 0.16, 0.24, 1.0);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    if (frac > 0.0) {
        if (dimmed)   cairo_set_source_rgba(cr, 0.35, 0.35, 0.44, 1.0);
        else if (sel) cairo_set_source_rgba(cr, 0.00, 0.95, 0.85, 1.0);
        else          cairo_set_source_rgba(cr, 0.00, 0.62, 0.56, 1.0);
        cairo_rectangle(cr, x, y, w * frac, h);
        cairo_fill(cr);
    }

    /* The selected row gets an outline, so which slider the arrow keys will
     * move is obvious even when its fill is at zero. */
    if (sel) {
        cairo_set_source_rgba(cr, 0.00, 0.95, 0.85, 0.9);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, x - 0.5, y - 0.5, w + 1, h + 1);
        cairo_stroke(cr);
    }
}

void synui_render_filters(syn_server_t *s)
{
    syn_filters_t *fl = &s->filters;

    if (!fl->visible) {
        wlr_scene_node_set_enabled(&s->filters_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    const int row_h = 34, top = 66, pad = 18;
    int pw = 560;
    int ph = top + FILTER_ROW_COUNT * row_h + 96;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->filters_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->filters_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->filters_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->filters_ui.bg)
        s->filters_ui.bg = wlr_scene_rect_create(s->filters_ui.tree,
                                                 pw, ph, bg_color);
    if (!s->filters_ui.accent)
        s->filters_ui.accent = wlr_scene_rect_create(s->filters_ui.tree,
                                                     pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->filters_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "CRT FILTERS");

    /* Two ways for a slider to do nothing, and the panel names both: the master
     * switch is off, or there is no GLES pass to configure at all (pixman). */
    cairo_set_font_size(cr, 12);
    if (!s->effects) {
        cairo_set_source_rgba(cr, 0.75, 0.45, 0.45, 1.0);
        cairo_move_to(cr, 18, 50);
        cairo_show_text(cr, "no GLES renderer \xc2\xb7 filters unavailable on this display");
    } else if (!s->config.effects) {
        cairo_set_source_rgba(cr, 0.95, 0.75, 0.25, 1.0);
        cairo_move_to(cr, 18, 50);
        cairo_show_text(cr, "filters off \xc2\xb7 Space to turn them on");
    }

    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 58);
    cairo_line_to(cr, pw - 18, 58);
    cairo_stroke(cr);

    /* A slider that cannot bite is drawn as such: master off (or no GLES pass)
     * greys every strength, because none of them is doing anything. */
    int dimmed = (!s->config.effects || !s->effects);

    for (int i = 0; i < FILTER_ROW_COUNT; i++) {
        int sel = (i == fl->selected);
        int ry = top + i * row_h;

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, 12, ry - 18, pw - 24, row_h - 4);
            cairo_fill(cr);
        }

        char value[32];
        float frac = filters_row_value(s, i, value, sizeof(value));

        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.78, sel ? 1.0 : 0.78,
                              sel ? 0.99 : 0.86, 1.0);
        cairo_move_to(cr, pad + 8, ry + 4);
        cairo_show_text(cr, filters_row_label(i));

        if (frac < 0.0f) {              /* master switch: a word, not a bar */
            if (s->config.effects) set_accent(cr, 1.0);
            else                   cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
            cairo_move_to(cr, 250, ry + 4);
            cairo_show_text(cr, value);
        } else {
            draw_slider(cr, 250, ry - 9, 220, 12, frac, sel, dimmed);

            cairo_set_font_size(cr, 12);
            if (dimmed) cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
            else        set_accent(cr, 1.0);
            cairo_move_to(cr, 484, ry + 4);
            cairo_show_text(cr, value);
        }
    }

    if (fl->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, ph - 56);
        cairo_show_text(cr, fl->status);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 34);
    cairo_show_text(cr, "Up/Down select \xc2\xb7 Left/Right adjust \xc2\xb7 Space on/off");
    cairo_move_to(cr, 18, ph - 16);
    cairo_show_text(cr, fl->dirty ? "s save (unsaved changes) \xc2\xb7 Esc close"
                                  : "s save \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->filters_ui.text_buf, s->filters_ui.tree, buf);
}

/* ── Clock & Time settings panel (clock.c) ───────────────── */

void synui_render_clock(syn_server_t *s)
{
    syn_clock_t *c = &s->clock;

    if (!c->visible) {
        wlr_scene_node_set_enabled(&s->clock_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    const int row_h = 34, top = 116, pad = 26;
    int pw = 560;
    int ph = top + CLOCK_SETTING_ROWS * row_h + 40 + c->nzones * 24 + 72;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->clock_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->clock_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->clock_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->clock_ui.bg)
        s->clock_ui.bg = wlr_scene_rect_create(s->clock_ui.tree, pw, ph, bg_color);
    if (!s->clock_ui.accent)
        s->clock_ui.accent = wlr_scene_rect_create(s->clock_ui.tree, pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->clock_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, pad, 30);
    cairo_show_text(cr, "DATE & TIME");

    char local[128];
    clock_local_string(s, local, sizeof(local));
    cairo_set_font_size(cr, 20);
    cairo_set_source_rgba(cr, 0.92, 0.96, 1.0, 1.0);
    cairo_move_to(cr, pad, 66);
    cairo_show_text(cr, local);

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.55, 0.6, 0.72, 1.0);
    cairo_move_to(cr, pad, 90);
    char tzline[168];
    snprintf(tzline, sizeof(tzline), "system zone \xc2\xb7 %s", c->tz);
    cairo_show_text(cr, tzline);

    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, pad, 100);
    cairo_line_to(cr, pw - pad, 100);
    cairo_stroke(cr);

    for (int i = 0; i < CLOCK_SETTING_ROWS; i++) {
        int sel = (i == c->selected);
        int ry = top + i * row_h;
        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, 12, ry - 18, pw - 24, row_h - 4);
            cairo_fill(cr);
        }
        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.78, sel ? 1.0 : 0.78,
                              sel ? 0.99 : 0.86, 1.0);
        cairo_move_to(cr, pad + 8, ry + 4);
        cairo_show_text(cr, clock_row_label(i));

        char value[32];
        clock_row_value(s, i, value, sizeof(value));
        set_accent(cr, 1.0);
        cairo_move_to(cr, 340, ry + 4);
        cairo_show_text(cr, value);
    }

    int wy = top + CLOCK_SETTING_ROWS * row_h + 16;
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.55, 0.6, 0.72, 1.0);
    cairo_move_to(cr, pad, wy);
    cairo_show_text(cr, "WORLD CLOCK");
    wy += 22;
    for (int i = 0; i < c->nzones; i++) {
        char zs[64];
        clock_zone_string(s, i, zs, sizeof(zs));
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 0.82, 0.86, 0.94, 1.0);
        cairo_move_to(cr, pad + 8, wy);
        cairo_show_text(cr, c->zones[i]);
        cairo_set_source_rgba(cr, 0.6, 0.85, 0.82, 1.0);
        cairo_move_to(cr, 340, wy);
        cairo_show_text(cr, zs);
        wy += 24;
    }

    if (c->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, pad, ph - 40);
        cairo_show_text(cr, c->status);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, pad, ph - 18);
    cairo_show_text(cr, "Up/Down select \xc2\xb7 Space toggle \xc2\xb7 c calendar \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->clock_ui.text_buf, s->clock_ui.tree, buf);
}

/* ── Calendar popup (clock.c) ────────────────────────────── */

void synui_render_calendar(syn_server_t *s)
{
    syn_cal_t *cal = &s->cal;

    if (!cal->visible) {
        wlr_scene_node_set_enabled(&s->cal_ui.tree->node, false);
        return;
    }

    /* The *usable* box, not the output: its top edge sits just under waybar
     * (the bar's height comes off the layer-shell exclusive zone, so a bar that
     * moves or resizes takes the calendar with it). The bar clock lives in
     * modules-center, so we centre the popup horizontally and hang it from the
     * bar — it drops out of the clock instead of floating in mid-screen. */
    struct wlr_box ob;
    server_usable_box(s, &ob);

    const int pw = 336, ph = 322;
    const int cell_w = 44, grid_x = 14, grid_y = 66, cell_h = 34;
    const int gap = 4;   /* breathing room between the bar and the popup */
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + gap;
    /* A short output could push the footer off the bottom; ride up if so. */
    if (py + ph > ob.y + ob.height) py = ob.y + ob.height - ph;
    if (py < ob.y) py = ob.y;

    wlr_scene_node_set_position(&s->cal_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->cal_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->cal_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.96f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->cal_ui.bg)
        s->cal_ui.bg = wlr_scene_rect_create(s->cal_ui.tree, pw, ph, bg_color);
    if (!s->cal_ui.accent)
        s->cal_ui.accent = wlr_scene_rect_create(s->cal_ui.tree, pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->cal_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    static const char *mon_name[] = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December" };

    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%s %d", mon_name[cal->mon], cal->year);
    cairo_set_font_size(cr, 17);
    set_accent(cr, 1.0);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, hdr, &ext);
    cairo_move_to(cr, (pw - ext.width) / 2, 36);
    cairo_show_text(cr, hdr);

    static const char *wd[] = { "Su","Mo","Tu","We","Th","Fr","Sa" };
    cairo_set_font_size(cr, 12);
    for (int i = 0; i < 7; i++) {
        cairo_set_source_rgba(cr, i == 0 || i == 6 ? 0.72 : 0.55,
                              0.6, 0.72, 1.0);
        cairo_move_to(cr, grid_x + i * cell_w + 13, grid_y);
        cairo_show_text(cr, wd[i]);
    }

    time_t now = time(NULL);
    struct tm tmn;
    localtime_r(&now, &tmn);
    int t_year = tmn.tm_year + 1900, t_mon = tmn.tm_mon, t_day = tmn.tm_mday;

    int first = calendar_first_weekday(cal->year, cal->mon);
    int dim   = calendar_days_in_month(cal->year, cal->mon);

    int rowi = 0;
    for (int day = 1; day <= dim; day++) {
        int col = (first + day - 1) % 7;
        if (day > 1 && col == 0) rowi++;
        int cx = grid_x + col * cell_w;
        int cy = grid_y + 12 + rowi * cell_h;

        bool is_today = (cal->year == t_year && cal->mon == t_mon && day == t_day);
        bool is_sel   = (day == cal->sel);

        if (is_sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, cx + 2, cy - 2, cell_w - 4, cell_h - 6);
            cairo_fill(cr);
        }
        if (is_today) {
            set_accent(cr, 1.0);
            cairo_set_line_width(cr, 1.5);
            cairo_rectangle(cr, cx + 2, cy - 2, cell_w - 4, cell_h - 6);
            cairo_stroke(cr);
        }

        char ds[8];
        snprintf(ds, sizeof(ds), "%d", day);
        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, is_sel ? 0.95 : 0.82, is_sel ? 1.0 : 0.86,
                              is_sel ? 0.99 : 0.94, 1.0);
        cairo_move_to(cr, cx + (day < 10 ? 16 : 12), cy + 18);
        cairo_show_text(cr, ds);
    }

    cairo_set_font_size(cr, 11);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 14, ph - 12);
    cairo_show_text(cr,
        "\xe2\x86\x90\xe2\x86\x92 day \xc2\xb7 PgUp/PgDn month \xc2\xb7 t today \xc2\xb7 Esc");

    cairo_destroy(cr);
    set_scene_buffer(&s->cal_ui.text_buf, s->cal_ui.tree, buf);
}

/* ── Control panel (ctlpanel.c) ──────────────────────────── */

/* Two columns: shortcuts left, settings right. The column x's are tuned for the
 * 13px monospace face; CTL_SHORTCUT_ROWS (synui.h) is how many shortcut rows fit
 * between the header and the footer, and the panel height is derived from it, so
 * the two cannot disagree about how much room there is. */
#define CTL_W          860
#define CTL_ROW_H       26
#define CTL_TOP         92
#define CTL_FOOTER      64
#define CTL_COL_RIGHT  470   /* x of the settings column */
#define CTL_SETTING_V  790   /* right edge of the settings value column */

void synui_render_ctlpanel(syn_server_t *s)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;

    if (!cp->visible) {
        wlr_scene_node_set_enabled(&s->ctlpanel_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    /* The taller of the two columns sets the height — the settings column is a
     * fixed CTL_ROW_COUNT, the shortcuts column scrolls within its window. */
    int body_rows = CTL_ROW_COUNT > CTL_SHORTCUT_ROWS
                  ? CTL_ROW_COUNT : CTL_SHORTCUT_ROWS;
    int pw = CTL_W;
    int ph = CTL_TOP + body_rows * CTL_ROW_H + CTL_FOOTER;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->ctlpanel_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->ctlpanel_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->ctlpanel_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->ctlpanel_ui.bg)
        s->ctlpanel_ui.bg = wlr_scene_rect_create(s->ctlpanel_ui.tree,
                                                  pw, ph, bg_color);
    if (!s->ctlpanel_ui.accent)
        s->ctlpanel_ui.accent = wlr_scene_rect_create(s->ctlpanel_ui.tree,
                                                      pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->ctlpanel_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "CONTROL PANEL");

    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 44);
    cairo_line_to(cr, pw - 18, 44);
    cairo_stroke(cr);

    /* Column headings */
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
    cairo_move_to(cr, 18, 70);
    cairo_show_text(cr, "SHORTCUTS");
    cairo_move_to(cr, CTL_COL_RIGHT, 70);
    cairo_show_text(cr, "SETTINGS");

    /* Divider between the columns */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.35);
    cairo_move_to(cr, CTL_COL_RIGHT - 24, 56);
    cairo_line_to(cr, CTL_COL_RIGHT - 24, ph - CTL_FOOTER + 8);
    cairo_stroke(cr);

    /* ── Shortcuts column (live bind table) ── */
    syn_ctl_shortcut_t sc[CTL_SHORTCUTS_MAX];
    int n = ctlpanel_shortcuts(s, sc, CTL_SHORTCUTS_MAX);

    int first = cp->scroll;
    if (first > n - CTL_SHORTCUT_ROWS) first = n - CTL_SHORTCUT_ROWS;
    if (first < 0) first = 0;

    cairo_set_font_size(cr, 13);
    for (int i = 0; i < CTL_SHORTCUT_ROWS && first + i < n; i++) {
        int ry = CTL_TOP + i * CTL_ROW_H;

        set_accent(cr, 0.95);
        cairo_move_to(cr, 18, ry);
        cairo_show_text(cr, sc[first + i].combo);

        cairo_set_source_rgba(cr, 0.78, 0.78, 0.86, 1.0);
        cairo_move_to(cr, 190, ry);
        cairo_show_text(cr, sc[first + i].desc);
    }

    /* Say so when the list runs off the window, rather than silently truncating
     * it — a shortcut you cannot see is a shortcut you do not have. */
    if (n > CTL_SHORTCUT_ROWS) {
        cairo_set_font_size(cr, 11);
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
        char more[64];
        snprintf(more, sizeof(more), "%d\xe2\x80\x93%d of %d \xc2\xb7 PgUp/PgDn",
                 first + 1,
                 first + CTL_SHORTCUT_ROWS < n ? first + CTL_SHORTCUT_ROWS : n,
                 n);
        cairo_move_to(cr, 18, CTL_TOP + CTL_SHORTCUT_ROWS * CTL_ROW_H + 6);
        cairo_show_text(cr, more);
    }

    /* ── Settings column ── */
    for (int i = 0; i < CTL_ROW_COUNT; i++) {
        int ry = CTL_TOP + i * CTL_ROW_H;

        if (i == CTL_ROW_SEP) {          /* a rule, not a row */
            cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.4);
            cairo_move_to(cr, CTL_COL_RIGHT, ry - 8);
            cairo_line_to(cr, CTL_SETTING_V + 40, ry - 8);
            cairo_stroke(cr);
            continue;
        }

        int sel = (i == cp->selected);
        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, CTL_COL_RIGHT - 12, ry - 16,
                            (CTL_SETTING_V + 52) - (CTL_COL_RIGHT - 12), CTL_ROW_H - 4);
            cairo_fill(cr);
        }

        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.78, sel ? 1.0 : 0.78,
                              sel ? 0.99 : 0.86, 1.0);
        cairo_move_to(cr, CTL_COL_RIGHT, ry);
        cairo_show_text(cr, ctlpanel_row_label(i));

        char value[32];
        ctlpanel_row_value(s, i, value, sizeof(value));
        if (value[0]) {
            /* "on" reads as live, everything else (off/n/a) as inert. */
            if (strcmp(value, "off") == 0 || strcmp(value, "n/a") == 0)
                cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
            else
                set_accent(cr, 1.0);
            cairo_set_font_size(cr, 13);
            draw_right(cr, CTL_SETTING_V + 40, ry, value);
        }
    }

    /* ── Footer ── */
    if (cp->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, ph - 38);
        cairo_show_text(cr, cp->status);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 18);
    cairo_show_text(cr,
        "Up/Down select \xc2\xb7 Enter activate \xc2\xb7 PgUp/PgDn scroll shortcuts \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->ctlpanel_ui.text_buf, s->ctlpanel_ui.tree, buf);
}

/* ── Theme manager (theme.c) ─────────────────────────────── */

#define THM_W        460
#define THM_ROW_H     52
#define THM_TOP       96
#define THM_FOOTER    96   /* room for the transparency slider + status + help */
#define THM_PAD       22
#define THM_SWATCH    30

/* One-line "what this theme is" under each name, so the pick is not just a word. */
static const char *thememgr_blurb(syn_theme_t t)
{
    switch (t) {
    case SYN_THEME_SYNAPSE: return "The neon night-drive default \xc2\xb7 apps dark";
    case SYN_THEME_DARK:    return "Flat modern dark \xc2\xb7 apps + Dolphin + Firefox dark";
    case SYN_THEME_WINXP:   return "Luna blue \xc2\xb7 apps light";
    case SYN_THEME_WIN95:   return "Grey 3D, navy titles \xc2\xb7 apps light";
    default:                return "";
    }
}

void synui_render_thememgr(syn_server_t *s)
{
    syn_thememgr_t *tm = &s->thememgr;

    if (!tm->visible) {
        wlr_scene_node_set_enabled(&s->thememgr_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    int pw = THM_W;
    int ph = THM_TOP + SYN_THEME_COUNT * THM_ROW_H + THM_FOOTER;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->thememgr_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->thememgr_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->thememgr_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->thememgr_ui.bg)
        s->thememgr_ui.bg = wlr_scene_rect_create(s->thememgr_ui.tree,
                                                  pw, ph, bg_color);
    if (!s->thememgr_ui.accent)
        s->thememgr_ui.accent = wlr_scene_rect_create(s->thememgr_ui.tree,
                                                      pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->thememgr_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, THM_PAD, 34);
    cairo_show_text(cr, "THEME MANAGER");

    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, THM_PAD, 50);
    cairo_line_to(cr, pw - THM_PAD, 50);
    cairo_stroke(cr);

    for (int i = 0; i < SYN_THEME_COUNT; i++) {
        int ry = THM_TOP + i * THM_ROW_H;
        int sel    = (i == tm->selected);
        int active = (i == s->config.theme);

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, THM_PAD - 10, ry - 26, pw - 2 * (THM_PAD - 10),
                            THM_ROW_H - 6);
            cairo_fill(cr);
        }

        /* Swatch: the theme's focused-title colour, framed. */
        float sw[4];
        theme_preview_color((syn_theme_t)i, sw);
        cairo_set_source_rgba(cr, sw[0], sw[1], sw[2], 1.0);
        cairo_rectangle(cr, THM_PAD, ry - 24, THM_SWATCH, THM_SWATCH);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.6, 0.8);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, THM_PAD, ry - 24, THM_SWATCH, THM_SWATCH);
        cairo_stroke(cr);

        double tx = THM_PAD + THM_SWATCH + 16;

        cairo_set_font_size(cr, 15);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.80, sel ? 1.0 : 0.80,
                              sel ? 0.99 : 0.88, 1.0);
        cairo_move_to(cr, tx, ry - 6);
        cairo_show_text(cr, theme_name((syn_theme_t)i));

        /* "active" marker: the theme currently in force (not just highlighted). */
        if (active) {
            set_accent(cr, 1.0);
            draw_right(cr, pw - THM_PAD, ry - 6, "\xe2\x97\x8f active");
        }

        cairo_set_font_size(cr, 11);
        cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 1.0);
        cairo_move_to(cr, tx, ry + 12);
        cairo_show_text(cr, thememgr_blurb((syn_theme_t)i));
    }

    /* Transparency slider: a track filled in proportion to the focused-window
     * opacity (0.50..1.00 spans it), greyed when the master switch is off. This
     * is the control the control panel row mirrors — Left/Right move it, T flips
     * it — so the "make windows glassy" knob lives with the rest of the look. */
    {
        int    on   = s->config.transparency;
        double sy   = ph - 60;
        double tx0  = THM_PAD, tx1 = pw - THM_PAD;
        double tw   = tx1 - tx0;

        cairo_set_font_size(cr, 12);
        char lbl[48];
        if (on) snprintf(lbl, sizeof lbl, "Transparency  %d%%",
                         (int)(s->config.active_opacity * 100 + 0.5f));
        else    snprintf(lbl, sizeof lbl, "Transparency  off");
        if (on) set_accent(cr, 0.95);
        else    cairo_set_source_rgba(cr, 0.55, 0.55, 0.65, 0.9);
        cairo_move_to(cr, tx0, sy - 8);
        cairo_show_text(cr, lbl);

        cairo_set_source_rgba(cr, 0.22, 0.22, 0.30, 0.95);
        cairo_rectangle(cr, tx0, sy, tw, 6);
        cairo_fill(cr);
        if (on) {
            double frac = (s->config.active_opacity - 0.50) / 0.50;
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            set_accent(cr, 0.9);
            cairo_rectangle(cr, tx0, sy, tw * frac, 6);
            cairo_fill(cr);
        }
    }

    if (tm->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, THM_PAD, ph - 34);
        cairo_show_text(cr, tm->status);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, THM_PAD, ph - 14);
    cairo_show_text(cr,
        "Up/Down theme \xc2\xb7 \xe2\x86\x90/\xe2\x86\x92 opacity \xc2\xb7 T transparency \xc2\xb7 Enter apply \xc2\xb7 Esc");

    cairo_destroy(cr);
    set_scene_buffer(&s->thememgr_ui.text_buf, s->thememgr_ui.tree, buf);
}

/* ── Clipboard history (clipboard.c) ─────────────────────── */

#define CLIP_W       560
#define CLIP_ROW_H    24
#define CLIP_TOP      78
#define CLIP_FOOTER   46
#define CLIP_PAD      18

void synui_render_clipboard(syn_server_t *s)
{
    syn_clipboard_t *c = &s->clipboard;

    if (!c->visible) {
        wlr_scene_node_set_enabled(&s->clip_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    int rows = c->count < CLIP_ROWS ? c->count : CLIP_ROWS;
    if (rows < 1) rows = 1;                 /* the empty line still needs a row */

    int pw = CLIP_W;
    int ph = CLIP_TOP + rows * CLIP_ROW_H + CLIP_FOOTER;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->clip_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->clip_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->clip_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->clip_ui.bg)
        s->clip_ui.bg = wlr_scene_rect_create(s->clip_ui.tree, pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->clip_ui.bg, pw, ph);   /* height tracks the list */
    if (!s->clip_ui.accent)
        s->clip_ui.accent = wlr_scene_rect_create(s->clip_ui.tree, pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->clip_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, CLIP_PAD, 30);
    cairo_show_text(cr, "CLIPBOARD");

    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, CLIP_PAD, 44);
    cairo_line_to(cr, pw - CLIP_PAD, 44);
    cairo_stroke(cr);

    int first = c->scroll;
    if (first > c->count - CLIP_ROWS) first = c->count - CLIP_ROWS;
    if (first < 0) first = 0;

    if (c->count == 0) {
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
        cairo_move_to(cr, CLIP_PAD, CLIP_TOP);
        cairo_show_text(cr, "Nothing copied yet");
    }

    for (int i = 0; i < CLIP_ROWS && first + i < c->count; i++) {
        const syn_clip_item_t *it = &c->items[first + i];
        int ry = CLIP_TOP + i * CLIP_ROW_H;
        int sel = (first + i == c->selected);

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, CLIP_PAD - 8, ry - 15, pw - 2 * (CLIP_PAD - 8),
                            CLIP_ROW_H - 3);
            cairo_fill(cr);
        }

        /* Flatten to one line for display. The stored text keeps its newlines —
         * this is only what the row shows, and a raw \n would draw as a box
         * glyph and throw the row's baseline out. */
        char line[160];
        size_t n = 0;
        for (const char *p = it->text; *p && n < sizeof(line) - 1; p++)
            line[n++] = ((unsigned char)*p < 0x20) ? ' ' : *p;
        line[n] = '\0';
        /* Cut on a character boundary: a chop through a multi-byte sequence
         * poisons cairo's context and blanks every row below this one. */
        line[news_utf8_trim(line, strlen(line))] = '\0';

        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.78, sel ? 1.0 : 0.78,
                              sel ? 0.99 : 0.86, 1.0);
        draw_clipped(cr, CLIP_PAD, ry, pw - 2 * CLIP_PAD - 40, line);
    }

    cairo_set_font_size(cr, 11);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    if (c->count > CLIP_ROWS) {
        char more[64];
        snprintf(more, sizeof(more), "%d\xe2\x80\x93%d of %d", first + 1,
                 first + CLIP_ROWS < c->count ? first + CLIP_ROWS : c->count,
                 c->count);
        cairo_move_to(cr, CLIP_PAD, ph - 30);
        cairo_show_text(cr, more);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, CLIP_PAD, ph - 14);
    cairo_show_text(cr, "Enter copy \xc2\xb7 Del clear all \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->clip_ui.text_buf, s->clip_ui.tree, buf);
}

/* ── Notification toasts (notif.c) ───────────────────────── */

#define NOTIF_W       360
#define NOTIF_GAP       8
#define NOTIF_MARGIN   12
#define NOTIF_PAD      12

/* A toast is two lines, or three when it has a body. Fixed heights rather than
 * measured ones: the text is clipped to one line each anyway, so there is
 * nothing to measure, and a stack whose cards jump height as text arrives reads
 * as jitter. */
static int notif_height(const syn_notif_t *t) { return t->body[0] ? 82 : 58; }

/* Top-right of the *usable* box — the full output box would put the first toast
 * underneath waybar, which owns an exclusive zone at the top. Toasts follow the
 * focused output, so they appear on the screen you are looking at. */
static void notif_stack_box(syn_server_t *s, struct wlr_box *out)
{
    struct wlr_box ub;
    server_usable_box(s, &ub);

    int h = 0;
    for (int i = 0; i < s->notifs.count; i++) {
        h += notif_height(&s->notifs.items[i]);
        if (i) h += NOTIF_GAP;
    }

    out->width  = NOTIF_W;
    out->height = h;
    out->x = ub.x + ub.width - NOTIF_W - NOTIF_MARGIN;
    out->y = ub.y + NOTIF_MARGIN;
}

/* Which toast is under a layout-space point, or -1. Lives here because render.c
 * owns the geometry — notif.c asking for it keeps one definition of where a
 * toast is, instead of two that can disagree by a pixel and eat clicks. */
int synui_notif_hit(syn_server_t *s, double lx, double ly, struct wlr_box *stack)
{
    if (!s->notifs.count) return -1;

    notif_stack_box(s, stack);
    if (lx < stack->x || lx >= stack->x + stack->width) return -1;
    if (ly < stack->y || ly >= stack->y + stack->height) return -1;

    int y = stack->y;
    for (int i = 0; i < s->notifs.count; i++) {
        int h = notif_height(&s->notifs.items[i]);
        if (ly >= y && ly < y + h) return i;
        y += h + NOTIF_GAP;
    }
    return -1;   /* landed in a gap between cards */
}

/* Urgency reads as colour: critical is the only one allowed to shout. */
static void notif_accent(int urgency, double rgb[3])
{
    if (urgency >= NOTIF_URGENCY_CRITICAL) {
        rgb[0] = 0.95; rgb[1] = 0.30; rgb[2] = 0.35;
    } else if (urgency <= NOTIF_URGENCY_LOW) {
        rgb[0] = 0.45; rgb[1] = 0.45; rgb[2] = 0.55;
    } else {
        rgb[0] = 0.00; rgb[1] = 0.85; rgb[2] = 0.75;
    }
}

void synui_render_notifs(syn_server_t *s)
{
    syn_notifs_t *n = &s->notifs;

    if (!n->count) {
        wlr_scene_node_set_enabled(&s->notif_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->clip_ui.tree->node, false);
        return;
    }

    struct wlr_box box;
    notif_stack_box(s, &box);
    if (box.height <= 0) {
        wlr_scene_node_set_enabled(&s->notif_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->clip_ui.tree->node, false);
        return;
    }

    wlr_scene_node_set_position(&s->notif_ui.tree->node, box.x, box.y);
    wlr_scene_node_set_enabled(&s->notif_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->notif_ui.tree->node);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(box.width, box.height, &cr);
    if (!buf) return;
    cairo_begin(cr);

    int y = 0;
    for (int i = 0; i < n->count; i++) {
        const syn_notif_t *t = &n->items[i];
        int h = notif_height(t);

        /* The card. Each toast paints its own background: they are separate
         * rounded slabs with gaps between them, which one backing rect for the
         * whole stack could not express. */
        cairo_set_source_rgba(cr, 0.06, 0.06, 0.12, 0.96);
        cairo_rectangle(cr, 0, y, box.width, h);
        cairo_fill(cr);

        double rgb[3];
        notif_accent(t->urgency, rgb);
        cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], 1.0);
        cairo_rectangle(cr, 0, y, 3, h);      /* urgency stripe down the left */
        cairo_fill(cr);

        /* App name, dim: it is context, not the message. */
        if (t->app[0]) {
            cairo_set_font_size(cr, 11);
            cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], 0.9);
            draw_clipped(cr, NOTIF_PAD, y + 20, box.width - 2 * NOTIF_PAD, t->app);
        }

        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, 0.95, 0.95, 1.0, 1.0);
        draw_clipped(cr, NOTIF_PAD, y + 40, box.width - 2 * NOTIF_PAD, t->summary);

        if (t->body[0]) {
            cairo_set_font_size(cr, 12);
            cairo_set_source_rgba(cr, 0.78, 0.78, 0.86, 1.0);
            draw_clipped(cr, NOTIF_PAD, y + 62, box.width - 2 * NOTIF_PAD, t->body);
        }

        y += h + NOTIF_GAP;
    }

    cairo_destroy(cr);
    set_scene_buffer(&s->notif_ui.text_buf, s->notif_ui.tree, buf);
}

/* ── Bluetooth (bt.c) ────────────────────────────────────── */
/* The geometry lives in synui.h: bt.c hit-tests the pointer against the same
 * numbers this draws with. */

/* A device's state in one word, plus the colour to say it in. Connected is the
 * only thing worth shouting about; the rest is context. */
static const char *bt_dev_state(const syn_bt_dev_t *d, double rgb[3])
{
    if (d->connected) { rgb[0] = 0.00; rgb[1] = 0.85; rgb[2] = 0.75; return "connected"; }
    if (d->paired)    { rgb[0] = 0.60; rgb[1] = 0.60; rgb[2] = 0.70; return "paired"; }
    rgb[0] = 0.45; rgb[1] = 0.45; rgb[2] = 0.55;
    return "";
}

void synui_render_bt(syn_server_t *s)
{
    syn_bt_t *b = &s->bt;

    if (!b->visible) {
        wlr_scene_node_set_enabled(&s->bt_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->notif_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->clip_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    /* The list is only the shown devices, not every device the scan turned up:
     * the anonymous advertisers are filtered out unless 'a' is on. Everything
     * below sizes, clamps and iterates against this, never b->count. */
    int shown = bt_shown_count(b);

    int rows = shown < BT_ROWS ? shown : BT_ROWS;
    if (rows < 1) rows = 1;                  /* the empty-list line needs a row */

    int pw = BT_W;
    int ph = BT_TOP + rows * BT_ROW_H + BT_FOOTER;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    /* What the pointer hit-tests measure against. */
    b->x = px; b->y = py; b->w = pw; b->h = ph;

    wlr_scene_node_set_position(&s->bt_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->bt_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->bt_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->bt_ui.bg)
        s->bt_ui.bg = wlr_scene_rect_create(s->bt_ui.tree, pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->bt_ui.bg, pw, ph);   /* height tracks the list */
    if (!s->bt_ui.accent)
        s->bt_ui.accent = wlr_scene_rect_create(s->bt_ui.tree, pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->bt_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, BT_PAD, 30);
    cairo_show_text(cr, "BLUETOOTH");

    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, BT_PAD, 44);
    cairo_line_to(cr, pw - BT_PAD, 44);
    cairo_stroke(cr);

    /* Adapter line: the two facts that explain an empty list. */
    cairo_set_font_size(cr, 13);
    if (!b->has_adapter) {
        cairo_set_source_rgba(cr, 0.85, 0.45, 0.45, 1.0);
        cairo_move_to(cr, BT_PAD, 70);
        cairo_show_text(cr, "No Bluetooth adapter found");
    } else {
        cairo_set_source_rgba(cr, b->powered ? 0.0 : 0.45, b->powered ? 0.85 : 0.45,
                              b->powered ? 0.75 : 0.55, 1.0);
        cairo_move_to(cr, BT_PAD, 70);
        cairo_show_text(cr, b->powered ? "Radio on" : "Radio off");

        if (b->discovering) {
            set_accent(cr, 1.0);
            cairo_move_to(cr, BT_PAD + 110, 70);
            cairo_show_text(cr, "\xc2\xb7 scanning\xe2\x80\xa6");
        }
    }

    /* A pairing prompt takes over the body: BlueZ is blocked on the answer, so
     * showing the device list underneath would invite acting on it. */
    if (b->ask_kind != BT_ASK_NONE) {
        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, 0.95, 0.95, 1.0, 1.0);
        cairo_move_to(cr, BT_PAD, BT_TOP + 6);
        cairo_show_text(cr, b->ask_dev[0] ? b->ask_dev : "A device");

        char l2[128];
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 0.78, 0.78, 0.86, 1.0);
        cairo_move_to(cr, BT_PAD, BT_TOP + 32);

        if (b->ask_kind == BT_ASK_CONFIRM) {
            snprintf(l2, sizeof(l2), "Passkey %06u \xc2\xb7 does it match the device?",
                     b->ask_passkey);
        } else if (b->ask_kind == BT_ASK_AUTHORIZE) {
            snprintf(l2, sizeof(l2), "wants to connect%s",
                     b->ask_detail[0] ? " (service)" : "");
        } else if (b->ask_passkey) {
            snprintf(l2, sizeof(l2), "Type %06u on the device, then Enter",
                     b->ask_passkey);
        } else {
            snprintf(l2, sizeof(l2), "Enter PIN %s on the device", b->ask_detail);
        }
        draw_clipped(cr, BT_PAD, BT_TOP + 32, pw - 2 * BT_PAD, l2);

        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, BT_PAD, ph - 14);
        cairo_show_text(cr, b->ask_kind == BT_ASK_DISPLAY
                        ? "Any key to dismiss"
                        : "y accept \xc2\xb7 n reject");
        cairo_destroy(cr);
        set_scene_buffer(&s->bt_ui.text_buf, s->bt_ui.tree, buf);
        return;
    }

    int first = bt_first_row(b);

    if (shown == 0) {
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
        cairo_move_to(cr, BT_PAD, BT_TOP);
        if (!b->powered) {
            cairo_show_text(cr, "Radio is off \xc2\xb7 press o");
        } else if (b->count > 0) {
            /* Devices are there, just filtered: say so, or an empty panel next
             * to a phone that is plainly in range reads as broken. */
            char msg[64];
            snprintf(msg, sizeof(msg), "%d hidden \xc2\xb7 press a to show", b->count);
            cairo_show_text(cr, msg);
        } else {
            cairo_show_text(cr, "No devices \xc2\xb7 press s to scan");
        }
    }

    for (int i = 0; i < BT_ROWS && first + i < shown; i++) {
        const syn_bt_dev_t *d = &b->devs[first + i];
        int ry = BT_TOP + i * BT_ROW_H;
        int sel = (first + i == b->selected);

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, BT_PAD - 8, ry - BT_ROW_ASC, pw - 2 * (BT_PAD - 8),
                            BT_ROW_H - 3);
            cairo_fill(cr);
        }

        /* bt_dev_label, not d->name: BlueZ hands out an Alias for every device
         * whether it has a name or not, and for the nameless that alias is the
         * address with dashes — which is why this list read as MAC addresses. */
        char label[128];
        bt_dev_label(d, label, sizeof(label));
        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.78, sel ? 1.0 : 0.78,
                              sel ? 0.99 : 0.86, 1.0);
        draw_clipped(cr, BT_PAD, ry, 250, label);

        double rgb[3];
        const char *st = bt_dev_state(d, rgb);
        if (st[0]) {
            cairo_set_font_size(cr, 12);
            cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], 1.0);
            cairo_move_to(cr, 290, ry);
            cairo_show_text(cr, st);
        }
        if (d->trusted) {
            cairo_set_font_size(cr, 11);
            cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
            cairo_move_to(cr, 372, ry);
            cairo_show_text(cr, "trusted");
        }

        char right[32] = {0};
        if (d->battery >= 0)      snprintf(right, sizeof(right), "%d%%", d->battery);
        else if (d->has_rssi)     snprintf(right, sizeof(right), "%d dBm", d->rssi);
        if (right[0]) {
            cairo_set_font_size(cr, 12);
            cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
            draw_right(cr, pw - BT_PAD, ry, right);
        }
    }

    cairo_set_font_size(cr, 11);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    if (shown > BT_ROWS) {
        char more[64];
        snprintf(more, sizeof(more), "%d\xe2\x80\x93%d of %d", first + 1,
                 first + BT_ROWS < shown ? first + BT_ROWS : shown, shown);
        cairo_move_to(cr, BT_PAD, ph - 34);
        cairo_show_text(cr, more);
    }

    if (b->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        draw_clipped(cr, BT_PAD, ph - 34, pw - 2 * BT_PAD, b->status);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, BT_PAD, ph - 14);
    cairo_show_text(cr, b->show_all
        ? "Enter connect \xc2\xb7 p pair \xc2\xb7 t trust \xc2\xb7 r forget \xc2\xb7 "
          "s scan \xc2\xb7 o radio \xc2\xb7 a fewer \xc2\xb7 Esc"
        : "Enter connect \xc2\xb7 p pair \xc2\xb7 t trust \xc2\xb7 r forget \xc2\xb7 "
          "s scan \xc2\xb7 o radio \xc2\xb7 a all \xc2\xb7 Esc");

    cairo_destroy(cr);
    set_scene_buffer(&s->bt_ui.text_buf, s->bt_ui.tree, buf);
}

/* ── Start menu (menu.c) ─────────────────────────────────── */
/* The geometry lives in synui.h: menu.c hit-tests the pointer and clamps the
 * scroll against the same numbers this draws with. */

void synui_render_menu(syn_server_t *s)
{
    syn_menu_t *m = &s->menu;

    if (!m->visible) {
        wlr_scene_node_set_enabled(&s->menu_ui.tree->node, false);
        return;
    }

    /* The *usable* box, not the output: it is the output minus the layer-shell
     * exclusive zones, so its top-left corner is the pixel under the left end of
     * waybar — which is where the SYNAPSE button is. Anchoring there is what
     * makes the menu drop out of the button that opens it instead of appearing
     * in the middle of the screen with no relationship to anything. Reading the
     * bar's height off the exclusive zone rather than hard-coding 28 means a
     * bar that moves or resizes takes the menu with it. */
    struct wlr_box ob;
    server_usable_box(s, &ob);

    int rows = m->view_count < MENU_ROWS ? m->view_count : MENU_ROWS;
    if (rows < 1) rows = 1;                 /* "no matches" still needs a line */

    /* The root sheds the blank breadcrumb line; a submenu keeps it for its page
     * name. Everything above the rows (separator, search) and the rows' own
     * baseline shift up by the same amount so the panel closes around them. */
    int mtop = menu_top_y(m);

    int pw = MENU_W;
    int ph = mtop + rows * MENU_ROW_H + MENU_FOOTER;
    int px = ob.x, py = ob.y;
    /* A short output could leave the footer off the bottom; ride up if so. */
    if (py + ph > ob.y + ob.height) py = ob.y + ob.height - ph;
    if (py < ob.y) py = ob.y;

    /* What the pointer hit-tests measure against. */
    m->x = px; m->y = py; m->w = pw; m->h = ph;

    wlr_scene_node_set_position(&s->menu_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->menu_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->menu_ui.tree->node);

    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.94f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    /* The panel's height tracks the filtered row count, so unlike the fixed
     * panels these rects have to be resized on every render, not just created. */
    if (!s->menu_ui.bg)
        s->menu_ui.bg = wlr_scene_rect_create(s->menu_ui.tree, pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->menu_ui.bg, pw, ph);
    if (!s->menu_ui.accent)
        s->menu_ui.accent = wlr_scene_rect_create(s->menu_ui.tree, pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->menu_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* The top line is the breadcrumb. The "SYNAPSE" brand text that used to sit
     * here was removed (it read as chrome, not information); at the root the
     * line is now blank, and a submenu shows its page name with a back-arrow so
     * you still know where you are and that Esc goes up a level. The page name
     * comes from a .desktop's Categories, but only ever as one of CATEGORIES'
     * own display strings — never third-party text — so it is safe to draw. */
    if (m->page[0]) {
        cairo_set_font_size(cr, 15);
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
        cairo_move_to(cr, MENU_PAD, 30);
        cairo_show_text(cr, "\xe2\x80\xb9  ");        /* ‹ back indicator */
        cairo_set_source_rgba(cr, 0.78, 0.78, 0.86, 1.0);
        cairo_show_text(cr, m->page);
    }

    /* The separator sits one line above the search, the search one line above
     * the first row — both pinned to mtop so they follow it up at the root. */
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, MENU_PAD, mtop - 48);
    cairo_line_to(cr, pw - MENU_PAD, mtop - 48);
    cairo_stroke(cr);

    /* Search line. Always drawn, with a prompt when empty, so that typing is
     * discoverable rather than a thing you have to already know about. */
    cairo_set_font_size(cr, 13);
    if (m->filter[0]) {
        cairo_set_source_rgba(cr, 0.95, 0.95, 1.0, 1.0);
        cairo_move_to(cr, MENU_PAD, mtop - 24);
        cairo_show_text(cr, m->filter);
        cairo_show_text(cr, "\xe2\x96\x8f");          /* caret */
    } else {
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
        cairo_move_to(cr, MENU_PAD, mtop - 24);
        cairo_show_text(cr, "Type to search\xe2\x80\xa6");
    }

    int first = menu_first_row(m);

    if (m->view_count == 0) {
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
        cairo_move_to(cr, MENU_PAD, mtop);
        cairo_show_text(cr, "No matches");
    }

    for (int i = 0; i < MENU_ROWS && first + i < m->view_count; i++) {
        const syn_menu_entry_t *e = &m->entries[m->view[first + i]];
        int ry = mtop + i * MENU_ROW_H;

        if (e->kind == MENU_ROW_HEADER) {
            cairo_set_font_size(cr, 11);
            cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
            cairo_move_to(cr, MENU_PAD, ry);
            cairo_show_text(cr, e->label);
            continue;
        }

        int sel = (first + i == m->selected);
        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, MENU_PAD - 8, ry - MENU_ROW_ASC,
                            pw - 2 * (MENU_PAD - 8), MENU_ROW_H - 3);
            cairo_fill(cr);
        }

        cairo_set_font_size(cr, 14);
        cairo_set_source_rgba(cr, sel ? 0.95 : 0.78, sel ? 1.0 : 0.78,
                              sel ? 0.99 : 0.86, 1.0);

        /* A submenu says so with the usual arrow on the right, and the label is
         * clipped short of it so a long category can never run underneath it. A
         * Back row points the other way, a left chevron ahead of its label, so
         * the mouse's way out reads as such at a glance. */
        int text_w = pw - MENU_PAD - 16;
        if (e->kind == MENU_ROW_SUBMENU) {
            text_w -= 20;
            cairo_move_to(cr, pw - MENU_PAD - 8, ry);
            cairo_show_text(cr, "\xe2\x80\xba");
        }
        int text_x = MENU_PAD + 8;
        if (e->kind == MENU_ROW_BACK) {
            cairo_move_to(cr, MENU_PAD + 4, ry);
            cairo_show_text(cr, "\xe2\x80\xb9");   /* ‹ */
            text_x = MENU_PAD + 20;
            text_w -= 12;
        }
        /* draw_clipped, not cairo_show_text: a long app name would otherwise
         * run past the panel edge. It truncates on a character boundary — a cut
         * through a multi-byte sequence would poison the context and blank
         * every row below this one. */
        cairo_move_to(cr, text_x, ry);
        draw_clipped(cr, text_x, ry, text_w, e->label);
    }

    /* Say so when the list runs off the window rather than silently truncating:
     * an entry you cannot see is an entry you do not have. */
    cairo_set_font_size(cr, 11);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    if (m->view_count > MENU_ROWS) {
        char more[64];
        snprintf(more, sizeof(more), "%d\xe2\x80\x93%d of %d",
                 first + 1,
                 first + MENU_ROWS < m->view_count ? first + MENU_ROWS : m->view_count,
                 m->view_count);
        cairo_move_to(cr, MENU_PAD, ph - 30);
        cairo_show_text(cr, more);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, MENU_PAD, ph - 14);
    /* The hint tracks where you are: at the root there is nothing to go back
     * to, and in a submenu the way out is the thing worth saying. */
    cairo_show_text(cr, m->page[0]
        ? "Enter launch \xc2\xb7 \xe2\x86\x90 back \xc2\xb7 Esc close"
        : "\xe2\x86\x91\xe2\x86\x93 select \xc2\xb7 \xe2\x86\x92 open \xc2\xb7 Enter launch \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->menu_ui.text_buf, s->menu_ui.tree, buf);
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
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->taskmgr_ui.bg)
        s->taskmgr_ui.bg = wlr_scene_rect_create(s->taskmgr_ui.tree,
                                                 pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->taskmgr_ui.bg, pw, ph);
    if (!s->taskmgr_ui.accent)
        s->taskmgr_ui.accent = wlr_scene_rect_create(s->taskmgr_ui.tree,
                                                     pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->taskmgr_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title, with the live sort key — the table's order is not otherwise
     * self-evident once every column has plausible-looking numbers in it. */
    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
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
            set_accent(cr, 0.35);
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
            set_accent(cr, 0.9);
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
        set_accent(cr, 0.9);
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

/* ── News aggregator (news.c) ────────────────────────────── */

#define NW_W          1040
#define NW_ROW_H        30
#define NW_TOP          76   /* first row's baseline area */
#define NW_COL_TAG      36   /* source tag */
#define NW_COL_TITLE   142
#define NW_COL_HOST    900   /* right edge of the host column */
#define NW_HOST_W      210   /* …and its width. A host can be as long as
                              * "newsletter.pragmaticengineer.com", so the
                              * title's room is what is left after this, not
                              * some fixed slack — get that wrong and a long
                              * headline prints straight over the host. */
#define NW_COL_AGE    1022   /* right edge */
#define NW_TITLE_W    (NW_COL_HOST - NW_HOST_W - 16 - NW_COL_TITLE)

/* One colour per source, so a feed is recognisable in peripheral vision
 * without reading the tag. Indexed by source, wrapping past the palette. */
static void src_color(int i, double *r, double *g, double *b)
{
    static const double pal[8][3] = {
        { 1.00, 0.50, 0.20 },   /* HN orange */
        { 0.60, 0.75, 0.35 },   /* Lobsters */
        { 0.35, 0.65, 0.95 },   /* Arch blue */
        { 0.95, 0.35, 0.40 },   /* Arch security red */
        { 0.85, 0.75, 0.35 },   /* kernel */
        { 0.55, 0.80, 0.85 },   /* LWN */
        { 0.70, 0.55, 0.90 },   /* Phoronix */
        { 0.40, 0.85, 0.60 },   /* GamingOnLinux */
    };
    int k = ((i % 8) + 8) % 8;
    *r = pal[k][0]; *g = pal[k][1]; *b = pal[k][2];
}

/* Right-aligned at x_right, ellipsized if it will not fit in max_w. */
static void draw_right_clipped(cairo_t *cr, double x_right, double y,
                               double max_w, const char *text)
{
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);

    if (ext.width <= max_w) {
        cairo_move_to(cr, x_right - ext.width, y);
        cairo_show_text(cr, text);
        return;
    }
    /* Left-align the clipped form at the column's left edge: an ellipsized
     * host that is still right-aligned looks like it is falling off the row. */
    draw_clipped(cr, x_right - max_w, y, max_w, text);
}

void synui_render_news(syn_server_t *s)
{
    syn_news_t *n = &s->news;

    if (!n->visible) {
        wlr_scene_node_set_enabled(&s->news_ui.tree->node, false);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    int pw = NW_W;
    int ph = NW_TOP + NEWS_ROWS * NW_ROW_H + 76;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->news_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->news_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->news_ui.tree->node);

    /* As opaque as the task manager, and for the same reason: this is a dense
     * table of small type, and at 0.94 the (animated) wallpaper reads through
     * the headlines. */
    float bg_color[4] = { 0.06f, 0.06f, 0.12f, 0.985f };
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->news_ui.bg)
        s->news_ui.bg = wlr_scene_rect_create(s->news_ui.tree, pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->news_ui.bg, pw, ph);
    if (!s->news_ui.accent)
        s->news_ui.accent = wlr_scene_rect_create(s->news_ui.tree, pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->news_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "NEWS");

    /* Right of the title: what this list currently *is* — which source, how
     * fresh, and whether a fetch is in flight. */
    char sub[160];
    char age[16];
    news_age(n->updated, age, sizeof(age));
    snprintf(sub, sizeof(sub), "%s \xc2\xb7 %s \xc2\xb7 %d/%d \xc2\xb7 %s",
             n->filter < 0 ? "all sources" : n->sources[n->filter].name,
             n->sort == NEWS_SORT_TIME ? "by time" : "by source",
             n->n_view, n->n,
             n->fetching ? "refreshing\xe2\x80\xa6"
                         : (n->updated ? age : "never fetched"));
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0);
    draw_right(cr, pw - 18, 30, sub);

    /* Search box, in place of the separator, while '/' is active. */
    if (n->searching) {
        set_accent(cr, 0.35);
        cairo_rectangle(cr, 12, 40, pw - 24, 22);
        cairo_fill(cr);
        char q[80];
        snprintf(q, sizeof(q), "/%s_", n->query);
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 0.90, 1.00, 0.98, 1.0);
        cairo_move_to(cr, 18, 56);
        cairo_show_text(cr, q);
    } else {
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.4, 0.5);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, 18, 50);
        cairo_line_to(cr, pw - 18, 50);
        cairo_stroke(cr);

        if (n->query[0]) {
            cairo_set_font_size(cr, 12);
            set_accent(cr, 0.9);
            char q[80];
            snprintf(q, sizeof(q), "filter: /%s", n->query);
            cairo_move_to(cr, 18, 66);
            cairo_show_text(cr, q);
        }
    }

    /* An empty river is either "still fetching" or "the network said no", and
     * those need different reactions — so say which. */
    if (n->n_view == 0) {
        cairo_set_font_size(cr, 13);
        cairo_set_source_rgba(cr, 0.55, 0.58, 0.66, 1.0);
        cairo_move_to(cr, 18, NW_TOP + 24);
        cairo_show_text(cr,
            n->fetching  ? "fetching feeds\xe2\x80\xa6"
          : n->n         ? "nothing matches this filter"
          : n->failed    ? "no feeds answered \xe2\x80\x94 check the network, then press r"
                         : "no items yet \xe2\x80\x94 press r to fetch");
    }

    for (int r = 0; r < NEWS_ROWS; r++) {
        int vi = n->scroll + r;
        if (vi >= n->n_view) break;

        syn_news_item_t *it = &n->items[n->view[vi]];
        int sel = (vi == n->selected);
        int ry  = NW_TOP + r * NW_ROW_H;   /* row top */
        double ty = ry + 20;               /* text baseline */

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, 12, ry, pw - 24, NW_ROW_H - 4);
            cairo_fill(cr);
            cairo_set_line_width(cr, 2);
            set_accent(cr, 1.0);
            cairo_rectangle(cr, 12.5, ry + 0.5, pw - 25, NW_ROW_H - 5);
            cairo_stroke(cr);
        }

        /* Unread marker: a filled dot in the gutter. This is the whole point of
         * the seen-set — without it a river of 240 headlines looks identical
         * whether or not you have already read all of it. */
        if (!it->seen) {
            set_accent(cr, 1.0);
            cairo_arc(cr, 24, ry + 14, 3.5, 0, 2 * 3.14159265);
            cairo_fill(cr);
        }

        double sr, sg, sb;
        src_color(it->src, &sr, &sg, &sb);
        cairo_set_font_size(cr, 11);
        cairo_set_source_rgba(cr, sr, sg, sb, it->seen ? 0.65 : 1.0);
        cairo_move_to(cr, NW_COL_TAG, ty);
        cairo_show_text(cr, n->sources[it->src].name);

        /* Read stories stay in the list but recede — the eye should land on
         * what is new without the list jumping around. */
        cairo_set_font_size(cr, 14);
        if (sel)           cairo_set_source_rgba(cr, 0.96, 1.00, 0.99, 1.0);
        else if (it->seen) cairo_set_source_rgba(cr, 0.52, 0.55, 0.62, 1.0);
        else               cairo_set_source_rgba(cr, 0.86, 0.90, 0.94, 1.0);
        draw_clipped(cr, NW_COL_TITLE, ty, NW_TITLE_W, it->title);

        char host[64];
        news_host(it->url, host, sizeof(host));
        cairo_set_font_size(cr, 11);
        cairo_set_source_rgba(cr, 0.40, 0.43, 0.52, 1.0);
        draw_right_clipped(cr, NW_COL_HOST, ty, NW_HOST_W, host);

        /* A discussion link exists for this item (HN/Lobsters): 'c' opens it. */
        if (it->comments[0]) {
            cairo_set_source_rgba(cr, 0.45, 0.50, 0.60, 1.0);
            cairo_move_to(cr, NW_COL_HOST + 16, ty);
            cairo_show_text(cr, "\xe2\x97\x8b");   /* ○ */
        }

        char age_s[16];
        news_age(it->ts, age_s, sizeof(age_s));
        cairo_set_font_size(cr, 11);
        cairo_set_source_rgba(cr, 0.45, 0.48, 0.56, 1.0);
        draw_right(cr, NW_COL_AGE, ty, age_s);
    }

    /* Scroll position */
    if (n->n_view > NEWS_ROWS) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", n->selected + 1, n->n_view);
        cairo_set_font_size(cr, 11);
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
        draw_right(cr, pw - 18, ph - 44, pos);
    }

    if (n->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, ph - 44);
        cairo_show_text(cr, n->status);
    }

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 0.9);
    cairo_move_to(cr, 18, ph - 20);
    cairo_show_text(cr, n->searching
        ? "type to filter \xc2\xb7 Enter keep \xc2\xb7 Esc clear"
        : "j/k move \xc2\xb7 Enter open \xc2\xb7 o background \xc2\xb7 c comments \xc2\xb7 "
          "y copy \xc2\xb7 Tab source \xc2\xb7 / search \xc2\xb7 s sort \xc2\xb7 "
          "m read \xc2\xb7 r refresh \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->news_ui.text_buf, s->news_ui.tree, buf);
}

/* ── Dock right-click context menu (dock.c) ──────────────── */

static const char *dockact_label(syn_dockact_t a)
{
    switch (a) {
    case SYN_DOCKACT_PIN:    return "Pin to Dock";
    case SYN_DOCKACT_UNPIN:  return "Unpin from Dock";
    case SYN_DOCKACT_OPEN:   return "Open";
    case SYN_DOCKACT_NEWWIN:   return "New Window";
    case SYN_DOCKACT_CLOSEWIN: return "Close Window";
    case SYN_DOCKACT_QUIT:     return "Quit All Windows";
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
    set_accent(cr, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, 0.5, 0.5, pw - 1, ph - 1);
    cairo_stroke(cr);

    for (int i = 0; i < s->dockmenu.action_count; i++) {
        int iy = 4 + i * item_h;
        int sel = (i == s->dockmenu.selected);
        if (sel) {
            set_accent(cr, 0.35);
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
    s->news_ui.tree    = wlr_scene_tree_create(&s->scene->tree);
    s->filters_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->clock_ui.tree   = wlr_scene_tree_create(&s->scene->tree);
    s->cal_ui.tree     = wlr_scene_tree_create(&s->scene->tree);
    s->ctlpanel_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->thememgr_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->menu_ui.tree    = wlr_scene_tree_create(&s->scene->tree);
    s->bt_ui.tree      = wlr_scene_tree_create(&s->scene->tree);
    s->notif_ui.tree   = wlr_scene_tree_create(&s->scene->tree);
    s->clip_ui.tree    = wlr_scene_tree_create(&s->scene->tree);
    s->dockmenu_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->cmdbar_ui.tree  = wlr_scene_tree_create(&s->scene->tree);

    /* All hidden until explicitly shown */
    wlr_scene_node_set_enabled(&s->welcome_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->overlay_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->dispcfg_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->wppick_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->taskmgr_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->news_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->filters_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->clock_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->cal_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->ctlpanel_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->thememgr_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->menu_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->bt_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->notif_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->clip_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->dockmenu_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->cmdbar_ui.tree->node, false);

    /* Render welcome screen (uses fallback 1920x1080 until output connects).
     * Opted out of via the menu's own "Show At Startup" row: leave the tree
     * empty and disabled — synui_render_welcome builds its nodes lazily, so
     * the first Super+Escape still brings up a complete menu. */
    if (s->config.welcome_at_startup)
        synui_render_welcome(s);
}
