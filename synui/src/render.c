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
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <scenefx/types/wlr_scene.h>

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

/* ── Drawing text we did not write ───────────────────────────
 *
 * Two separate things make a string undrawable here, and both used to take the
 * rest of the panel down with them.
 *
 * Invalid UTF-8 puts the cairo_t into CAIRO_STATUS_INVALID_STRING, and every
 * later operation on that context — every remaining row, the legend, the
 * counter — is then a silent no-op. One Steam Workshop title cut mid-character
 * by a fixed-size buffer was enough to blank the whole bottom half of the
 * wallpaper picker.
 *
 * And the toy font API resolves to a single face with NO per-glyph fallback, so
 * a character the font has no glyph for just draws nothing. A CJK or emoji
 * title is invisible on a box with no CJK font installed, which is most of
 * them.
 *
 * So: bad bytes are dropped, and characters the font cannot draw become '?'
 * (runs collapsed, or a ten-character CJK title becomes ten question marks).
 * A row is then always identifiable, even when it cannot be spelled.
 */

void syn_utf8_copy(char *dst, size_t n, const char *src)
{
    if (!n) return;

    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; ) {
        int len;
        if      (*p < 0x80)                 len = 1;
        else if (*p >= 0xC2 && *p <= 0xDF)  len = 2;
        else if (*p >= 0xE0 && *p <= 0xEF)  len = 3;
        else if (*p >= 0xF0 && *p <= 0xF4)  len = 4;
        else { p++; continue; }   /* stray continuation byte, or an invalid lead */

        /* A NUL is not a continuation byte, so this stops at the end of the
         * string rather than reading past it. */
        bool ok = true;
        for (int i = 1; i < len; i++)
            if ((p[i] & 0xC0) != 0x80) { ok = false; break; }
        if (!ok) { p++; continue; }

        if (o + (size_t)len + 1 > n) break;   /* truncate on a char boundary */
        memcpy(dst + o, p, (size_t)len);
        o += (size_t)len;
        p += len;
    }
    dst[o] = '\0';
}

void syn_show_text(cairo_t *cr, const char *text)
{
    char safe[512];
    syn_utf8_copy(safe, sizeof(safe), text);
    if (!safe[0]) return;

    cairo_glyph_t *glyphs = NULL;
    cairo_text_cluster_t *clusters = NULL;
    int nglyphs = 0, nclusters = 0;
    cairo_text_cluster_flags_t cflags = 0;

    /* Clusters are what tie a glyph back to the bytes it came from, which is
     * the only way to know WHICH character the font is missing. Right-to-left
     * output would need the walk below run backwards; not worth it for a
     * fallback path, so it just draws the string as-is. */
    if (cairo_scaled_font_text_to_glyphs(cairo_get_scaled_font(cr), 0, 0,
                                         safe, -1, &glyphs, &nglyphs,
                                         &clusters, &nclusters,
                                         &cflags) != CAIRO_STATUS_SUCCESS ||
        (cflags & CAIRO_TEXT_CLUSTER_FLAG_BACKWARD)) {
        cairo_show_text(cr, safe);
        goto out;
    }

    char sub[512];
    size_t o = 0, b = 0;
    int g = 0;

    for (int i = 0; i < nclusters; i++) {
        bool missing = false;
        for (int j = 0; j < clusters[i].num_glyphs; j++)
            if (glyphs[g + j].index == 0) { missing = true; break; }

        if (missing) {
            if (!(o && sub[o - 1] == '?') && o + 1 < sizeof(sub))
                sub[o++] = '?';
        } else if (o + (size_t)clusters[i].num_bytes < sizeof(sub)) {
            memcpy(sub + o, safe + b, (size_t)clusters[i].num_bytes);
            o += (size_t)clusters[i].num_bytes;
        }
        b += (size_t)clusters[i].num_bytes;
        g += clusters[i].num_glyphs;
    }
    sub[o] = '\0';
    cairo_show_text(cr, sub);

out:
    if (glyphs)   cairo_glyph_free(glyphs);
    if (clusters) cairo_text_cluster_free(clusters);
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

/* ── Panel surface and ink ───────────────────────────────────
 * The accent was theme data; the surface under it was not. Every panel in this
 * file hard-coded the same near-black navy and a ladder of lavender greys for
 * its text, so a theme switch recoloured the highlights and left the panel
 * itself SYNAPSE-coloured — which on a light theme meant XP's beige desktop
 * opening a black control panel with grey text. Both are theme data now
 * (theme_load_colors pushes them here), and the greys are expressed as a
 * POSITION between the two rather than as absolute colours, so the same ladder
 * that reads as light-on-dark reads as dark-on-light without a second table.
 *
 * The initialisers are SYNAPSE's, so a render before any theme is applied looks
 * exactly as it always did. Same threading argument as the accent: render runs
 * on the main loop only, so a file-scope cache needs no lock. */
static float g_panel_bg[3]  = { 0.06f, 0.06f, 0.12f };
static float g_panel_ink[3] = { 0.95f, 0.95f, 1.00f };

void render_set_panel_surface(const float bg[4], const float ink[4])
{
    for (int i = 0; i < 3; i++) {
        g_panel_bg[i]  = bg[i];
        g_panel_ink[i] = ink[i];
    }
}

/* The ink ladder. `level` is how far from the surface toward the ink a colour
 * sits: 1.0 is full-strength text, 0.28 is a hairline rule that should barely
 * separate itself from the panel. The old absolute greys map onto it within a
 * couple of hundredths, so stock is unchanged to the eye.
 *
 * Named rather than sprinkled as numbers because the whole point is that a
 * heading is a heading on every theme — INK_LABEL has to stay dimmer than
 * INK_BODY when the surface is beige, and a literal cannot promise that. */
#define INK_STRONG 1.00   /* the brightest text a panel draws              */
#define INK_TITLE  0.85   /* row titles, values, the thing you are reading */
#define INK_BODY   0.81   /* ordinary body text                            */
#define INK_MUTED  0.72   /* secondary text still meant to be read         */
#define INK_LABEL  0.55   /* column headings, units, hints                 */
#define INK_DIM    0.44   /* disabled rows, placeholders                   */
#define INK_RULE   0.27   /* separators and hairlines                      */

static inline void set_ink(cairo_t *cr, double level, double a)
{
    cairo_set_source_rgba(cr,
        g_panel_bg[0] + (g_panel_ink[0] - g_panel_bg[0]) * level,
        g_panel_bg[1] + (g_panel_ink[1] - g_panel_bg[1]) * level,
        g_panel_bg[2] + (g_panel_ink[2] - g_panel_bg[2]) * level, a);
}

/* The panel surface itself, at the alpha the caller wants. Panels differ in how
 * transparent they are (a menu is glassier than the lock screen), so the alpha
 * stays with the panel and only the colour comes from here. */
static inline void panel_bg_color(float out[4], float alpha)
{
    out[0] = g_panel_bg[0];
    out[1] = g_panel_bg[1];
    out[2] = g_panel_bg[2];
    out[3] = alpha;
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
    float color[4];
    panel_bg_color(color, 0.92f);
    if (!s->welcome_ui.bg) {
        s->welcome_ui.bg = wlr_scene_rect_create(s->welcome_ui.tree,
                                                   pw, ph, color);
    }
    wlr_scene_rect_set_color(s->welcome_ui.bg, color);

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
    set_ink(cr, INK_RULE, 0.5);
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
            set_ink(cr, INK_MUTED, 1.0);
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
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 44, y + 16);
    cairo_show_text(cr, "Up/Down + Enter select");
    cairo_move_to(cr, 44, y + 34);
    cairo_show_text(cr, "Super+1-9 workspaces \xc2\xb7 Super+Tab cycle layout");
    cairo_move_to(cr, 44, y + 52);
    cairo_show_text(cr, "Super+E filters \xc2\xb7 Super+O move monitor \xc2\xb7 Super+Q close");

    /* Version */
    cairo_set_font_size(cr, 12);
    set_ink(cr, 0.33, 0.8);
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
        hit_clear(&s->cmdbar.hit);
        return;
    }
    wlr_scene_node_set_enabled(&s->cmdbar_ui.tree->node, true);

    /* Pointer geometry. No rows: the bar is a prompt, not a list — the only
     * thing a pointer has to say about it is "not this", which is the click
     * off it that closes it. */
    hit_set_panel(&s->cmdbar.hit, bx, by, bw, bh);
    wlr_scene_node_raise_to_top(&s->cmdbar_ui.tree->node);

    /* Background */
    float color[4];
    panel_bg_color(color, 0.95f);
    if (!s->cmdbar_ui.bg) {
        s->cmdbar_ui.bg = wlr_scene_rect_create(s->cmdbar_ui.tree,
                                                  bw, bh, color);
    }
    wlr_scene_rect_set_color(s->cmdbar_ui.bg, color);
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
    set_ink(cr, 0.97, 1.0);
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
    float color[4];
    panel_bg_color(color, 0.88f);
    if (!s->overlay_ui.bg) {
        s->overlay_ui.bg = wlr_scene_rect_create(s->overlay_ui.tree,
                                                   pw, ph, color);
    }
    wlr_scene_rect_set_color(s->overlay_ui.bg, color);

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
    set_ink(cr, INK_RULE, 0.4);
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
    set_ink(cr, 0.89, 1.0);
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
        set_ink(cr, INK_LABEL, 1.0);
        cairo_move_to(cr, 14, y);
        cairo_show_text(cr, cbuf);

        /* Thin fill bar under the text. */
        double frac = (double)ov->ctx_used / (double)ov->ctx_window;
        if (frac > 1.0) frac = 1.0;
        int bx = 14, bw = pw - 28, byy = y + 8, bh = 4;
        set_ink(cr, 0.21, 0.8);
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
    set_ink(cr, INK_RULE, 0.4);
    cairo_move_to(cr, 92, y - 4);
    cairo_line_to(cr, pw - 14, y - 4);
    cairo_stroke(cr);
    y += 18;

    /* Recent synapd events (newest last). */
    if (ov->activity_n == 0) {
        set_ink(cr, INK_DIM, 0.7);
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
        hit_clear(&d->hit);
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
    /* 990 min: the rows run out to the HDR column at x=872, which sits past
     * the colour-depth column at x=760 and the PRIMARY tag at x=630. */
    int pw = d->count > 0 && map_w + 36 > 990 ? map_w + 36 : 990;
    /* +20 over the old 126: the HDR detail line for the selected monitor. */
    int ph = list_top + rows * 28 + 146;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->dispcfg_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->dispcfg_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->dispcfg_ui.tree->node);

    /* Pointer geometry: the monitor rows. The mini-map cells are recorded
     * separately as they are drawn — they sit at grid positions that can have
     * holes in them, so they are not a row grid and there is nothing to be
     * gained by pretending otherwise. */
    hit_set_panel(&d->hit, px, py, pw, ph);
    hit_set_rows(&d->hit, 12, list_top - 18, pw - 24, 28, d->count);

    /* Background + accent; the panel height depends on the monitor count,
     * so resize them on every render (hotplug can change the count). */
    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->dispcfg_ui.bg)
        s->dispcfg_ui.bg = wlr_scene_rect_create(s->dispcfg_ui.tree,
                                                 pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->dispcfg_ui.bg, pw, ph);
    wlr_scene_rect_set_color(s->dispcfg_ui.bg, bg_color);
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
    set_ink(cr, INK_RULE, 0.5);
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
            set_ink(cr, 0.09, 1.0);
        cairo_rectangle(cr, cx, cy, cell_w, cell_h);
        cairo_fill(cr);

        /* Remember the cell in LAYOUT coords so a click can pick the monitor
         * by pointing at the picture of it, which is the only part of this
         * panel a pointer would go for first. */
        d->cell[i] = (struct wlr_box){ px + cx, py + cy, cell_w, cell_h };

        cairo_set_line_width(cr, sel ? 2 : 1);
        if (sel)
            set_accent(cr, 1.0);
        else
            set_ink(cr, 0.33, 0.8);
        cairo_rectangle(cr, cx + 0.5, cy + 0.5, cell_w - 1, cell_h - 1);
        cairo_stroke(cr);

        char label[64];
        cairo_set_font_size(cr, 13);
        set_ink(cr, sel ? INK_STRONG : 0.78, 1.0);
        cairo_move_to(cr, cx + 8, cy + 24);
        cairo_show_text(cr, o->wlr_output->name);

        cairo_set_font_size(cr, 11);
        set_ink(cr, INK_LABEL, 1.0);
        snprintf(label, sizeof(label), "(%d,%d)", o->grid_x, o->grid_y);
        cairo_move_to(cr, cx + 8, cy + 42);
        cairo_show_text(cr, label);
    }

    /* Monitor rows: name, mode, rotation, layout position, primary */
    syn_output_t *primary = server_primary_output(s);
    cairo_set_font_size(cr, 14);
    int y = list_top;
    if (d->count == 0) {
        set_ink(cr, INK_LABEL, 1.0);
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
            set_ink(cr, INK_MUTED, 1.0);
        }
        cairo_move_to(cr, 40, y);
        cairo_show_text(cr, wo->name);

        char col[48];
        snprintf(col, sizeof(col), "%dx%d", w, h);
        cairo_move_to(cr, 190, y);
        cairo_show_text(cr, col);

        cairo_move_to(cr, 310, y);
        cairo_show_text(cr, transform_name(wo->transform));

        set_ink(cr, INK_LABEL, 1.0);
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

        /* Colour depth. Three distinct states, because "off" and "this
         * monitor/mode can't do it" are different answers to the same key. */
        syn_output_t *o = d->order[i];
        const char *depth;
        if (o->deep_color)             { set_accent(cr, 0.95); depth = "10-bit"; }
        else if (o->deep_color_capable){ set_ink(cr, INK_LABEL, 1.0);
                                         depth = "8-bit"; }
        else                           { set_ink(cr, 0.38, 1.0);
                                         depth = "8-bit (only)"; }
        cairo_move_to(cr, 760, y);
        cairo_show_text(cr, depth);

        /* What the monitor advertises over EDID — a separate fact from the
         * column to its left, and the one the panel used to get wrong by
         * deriving it from the framebuffer format. Deliberately dim: synui
         * composites SDR, so an HDR10 monitor here is a capability sitting
         * unused, not a mode that is switched on. */
        if (o->hdr_pq || o->hdr_hlg) {
            cairo_set_source_rgba(cr, 0.62, 0.58, 0.78, 1.0);
            cairo_move_to(cr, 872, y);
            cairo_show_text(cr, o->hdr_pq ? "HDR10" : "HLG");
        }

        y += 28;
    }

    /* Status line (last action or error) */
    if (d->status[0]) {
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, y + 8);
        cairo_show_text(cr, d->status);
    }

    /* What the selected monitor's EDID says, spelled out — the HDR column is
     * one word and this is where the rest of it goes. It also has to say what
     * synui does with it, which is nothing: a panel that prints "HDR10" and
     * stops reads as a mode you switched on. */
    cairo_set_font_size(cr, 12);
    syn_output_t *selo = (d->selected >= 0 && d->selected < d->count)
                       ? d->order[d->selected] : NULL;
    if (selo && (selo->hdr_pq || selo->hdr_hlg)) {
        char line[192];
        char nits[32] = "";
        if (selo->hdr_max_nits > 0.0f)
            snprintf(nits, sizeof(nits), " \xc2\xb7 %.0f cd/m\xc2\xb2 peak",
                     (double)selo->hdr_max_nits);
        snprintf(line, sizeof(line),
                 "%s reports %s%s%s%s \xe2\x80\x94 synui composites SDR sRGB; "
                 "HDR output is not driven",
                 selo->wlr_output->name,
                 selo->hdr_pq ? "HDR10 (PQ)" : "HLG",
                 (selo->hdr_pq && selo->hdr_hlg) ? " + HLG" : "",
                 selo->wide_gamut ? " \xc2\xb7 BT.2020" : "", nits);
        cairo_set_source_rgba(cr, 0.62, 0.58, 0.78, 0.95);
        cairo_move_to(cr, 18, ph - 62);
        cairo_show_text(cr, line);
    } else if (selo) {
        char line[192];
        snprintf(line, sizeof(line),
                 "%s advertises no HDR transfer function in its EDID "
                 "(SDR panel)", selo->wlr_output->name);
        set_ink(cr, 0.38, 0.9);
        cairo_move_to(cr, 18, ph - 62);
        cairo_show_text(cr, line);
    }

    /* Controls legend */
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 18, ph - 40);
    cairo_show_text(cr, "Up/Down select \xc2\xb7 Left/Right rotate \xc2\xb7 "
                        "p set primary (X11/game default)");
    cairo_move_to(cr, 18, ph - 20);
    cairo_show_text(cr, "Shift+arrows move in grid (swaps) \xc2\xb7 "
                        "d 10-bit colour (deep colour, not HDR) \xc2\xb7 "
                        "Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->dispcfg_ui.text_buf, s->dispcfg_ui.tree, buf);
}

/* ── Wallpaper selector (wppick.c) ───────────────────────── */

void synui_render_wppick(syn_server_t *s)
{
    if (!s->wppick.visible) {
        wlr_scene_node_set_enabled(&s->wppick_ui.tree->node, false);
        hit_clear(&s->wppick.hit);
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

    /* The list keeps its old 520 and the preview pane is added beside it, so
     * the rows lay out exactly as they did and only the panel gets wider. */
    const int list_w  = 520;
    const int prev_w  = 300;                 /* pane, including its gutter */
    const int prev_im = prev_w - 22;         /* image box inside it */
    int pw = list_w + prev_w;
    int ph = top + shown * row_h + 56;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->wppick_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->wppick_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->wppick_ui.tree->node);

    /* Pointer geometry. Only the LIST is hit-tested, not the preview pane
     * beside it: the preview is a picture of the selection, and clicking a
     * picture of the thing you already picked cannot mean anything. */
    hit_set_panel(&s->wppick.hit, px, py, pw, ph);
    hit_set_rows(&s->wppick.hit, 12, top, list_w - 24, row_h, shown);
    hit_set_first(&s->wppick.hit, s->wppick.scroll);

    /* More opaque than the 0.94 the sparser panels use: the browse list puts a
     * small-type path under every row, and at 0.94 whatever is behind the panel
     * (the welcome menu, or the wallpaper itself) reads straight through them. */
    float bg_color[4];
    panel_bg_color(bg_color, 0.985f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->wppick_ui.bg)
        s->wppick_ui.bg = wlr_scene_rect_create(s->wppick_ui.tree,
                                                pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->wppick_ui.bg, pw, ph);
    wlr_scene_rect_set_color(s->wppick_ui.bg, bg_color);
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

    /* The title row packs its extras from the right edge inward: the scaling
     * mode first, then the scroll counter beside it. Both used to be placed
     * independently — the counter at a hard-coded x — so they overdrew each
     * other into an unreadable mash. Anything else added here must keep
     * walking right_edge leftward rather than picking its own offset. */
    double right_edge = pw - 18;

    /* Scaling mode. fill/fit/stretch/center existed for ages but only as a
     * synuirc key, so nobody knew they were there — show the current one and
     * how to change it. Reads through wallpaper_effective so a scoped monitor
     * shows ITS mode, which is the one [m] would move. */
    {
        syn_wallpaper_mode_t m;
        wallpaper_effective(&s->config, wppick_scope_output(s), NULL, NULL, &m);
        const char *mname = (m >= 0 && m < SYN_WALLPAPER_MODE_COUNT)
                            ? syn_wallpaper_mode_names[m] : "?";
        char label[64];
        snprintf(label, sizeof(label), "[m] %s", mname);
        cairo_set_font_size(cr, 12);
        cairo_text_extents_t te;
        cairo_text_extents(cr, label, &te);
        cairo_set_source_rgba(cr, 0.75, 0.55, 0.95, 1.0);
        right_edge -= te.width;
        cairo_move_to(cr, right_edge, 30);
        cairo_show_text(cr, label);
        right_edge -= 12;   /* gutter before whatever packs in next */
    }

    /* Scope: which monitor the next pick lands on. Drawn brighter than the
     * mode and in the accent when it is narrowed to one screen — this is the
     * one piece of panel state that silently changes what a keypress does, so
     * it must not read as decoration. */
    {
        const char *scope = wppick_scope_output(s);
        char label[64];
        snprintf(label, sizeof(label), "[Tab] %s", wppick_scope_label(s));
        cairo_set_font_size(cr, 12);
        cairo_text_extents_t te;
        cairo_text_extents(cr, label, &te);
        if (scope) set_accent(cr, 1.0);
        else       cairo_set_source_rgba(cr, 0.55, 0.60, 0.72, 1.0);
        right_edge -= te.width;
        cairo_move_to(cr, right_edge, 30);
        cairo_show_text(cr, label);
        right_edge -= 12;   /* gutter before whatever packs in next */
    }

    /* Separator */
    set_ink(cr, INK_RULE, 0.5);
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
            cairo_rectangle(cr, 12, ry, list_w - 24, row_h - 8);
            cairo_fill(cr);
            cairo_set_line_width(cr, 2);
            set_accent(cr, 1.0);
            cairo_rectangle(cr, 12.5, ry + 0.5, list_w - 25, row_h - 9);
            cairo_stroke(cr);
        }

        const char *label, *desc;
        wppick_row(s, i, &label, &desc);

        /* Neither of these is our text — a Workshop title comes from whoever
         * published it and an image row is a filename — so both go through
         * syn_show_text(). A row whose title cannot be drawn must still leave
         * the rows under it readable. */
        cairo_set_font_size(cr, 15);
        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
        cairo_move_to(cr, pad + 8, ry + 22);
        syn_show_text(cr, label);

        /* A found image's subtitle is its full path, which can be far wider
         * than the panel — clip it to the row so it cannot spill over the
         * border. */
        cairo_save(cr);
        cairo_rectangle(cr, pad, ry + 26, list_w - 2 * pad - 8, 16);
        cairo_clip(cr);
        cairo_set_font_size(cr, 12);
        set_ink(cr, sel ? INK_MUTED : 0.49, 1.0);
        cairo_move_to(cr, pad + 8, ry + 38);
        syn_show_text(cr, desc);
        cairo_restore(cr);
    }

    /* ── Preview pane ────────────────────────────────────────
     *
     * What the highlighted row actually looks like. This is not decoration:
     * a Workshop row is deferred to Enter, and so is everything else while a
     * Workshop wallpaper is on screen (wppick.c), so for those rows nothing is
     * applied by moving the highlight and this pane is the only thing that
     * shows what you are about to pick. Titles do not — a Workshop title is
     * whatever its publisher typed.
     */
    {
        const int bx = list_w, by = top;
        const int bw = prev_im, bh = shown * row_h - 8;

        /* Frame, so an image with dark edges still reads as a pane. */
        set_ink(cr, 0.04, 1.0);
        cairo_rectangle(cr, bx, by, bw, bh);
        cairo_fill(cr);

        const char *ppath = wppick_row_preview(s, s->wppick.selected);
        cairo_surface_t *thumb = ppath ? wpthumb_get(ppath) : NULL;

        if (thumb) {
            const int iw = cairo_image_surface_get_width(thumb);
            const int ih = cairo_image_surface_get_height(thumb);
            if (iw > 0 && ih > 0) {
                /* Fit, not fill: a preview that is cropped to the pane can hide
                 * the very part of the wallpaper someone is choosing by. */
                double sc = (double)bw / iw;
                if (ih * sc > bh) sc = (double)bh / ih;
                const double dw = iw * sc, dh = ih * sc;

                cairo_save(cr);
                cairo_translate(cr, bx + (bw - dw) / 2, by + (bh - dh) / 2);
                cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, thumb, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
                cairo_paint(cr);
                cairo_restore(cr);
            }
        } else {
            /* Say which of the two it is. "No preview" on a Workshop row whose
             * file failed to decode and on "None", which has nothing to show by
             * definition, would read as the same bug. */
            const char *why = ppath ? "Preview unavailable" : "No preview";
            cairo_set_font_size(cr, 12);
            set_ink(cr, 0.38, 1.0);
            cairo_text_extents_t te;
            cairo_text_extents(cr, why, &te);
            cairo_move_to(cr, bx + (bw - te.width) / 2, by + bh / 2);
            cairo_show_text(cr, why);
        }

        cairo_set_line_width(cr, 1);
        set_ink(cr, INK_RULE, 0.6);
        cairo_rectangle(cr, bx + 0.5, by + 0.5, bw - 1, bh - 1);
        cairo_stroke(cr);
    }

    /* Scroll position, when there is more than one screenful. */
    if (total > shown) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", s->wppick.selected + 1, total);
        cairo_set_font_size(cr, 12);
        cairo_text_extents_t te;
        cairo_text_extents(cr, pos, &te);
        set_ink(cr, INK_DIM, 0.9);
        cairo_move_to(cr, right_edge - te.width, 30);
        cairo_show_text(cr, pos);
    }

    /* Controls legend */
    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 18, ph - 20);
    cairo_show_text(cr, "Up/Down preview \xc2\xb7 Tab monitor \xc2\xb7 m scaling "
                        "\xc2\xb7 r rescan \xc2\xb7 Enter/Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->wppick_ui.text_buf, s->wppick_ui.tree, buf);
}

/* ── Power saving panel (power.c) ────────────────────────── */

void synui_render_power(syn_server_t *s)
{
    syn_power_t *p = &s->power;

    if (!p->visible) {
        wlr_scene_node_set_enabled(&s->power_ui.tree->node, false);
        hit_clear(&p->hit);
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

    /* Pointer geometry: the panel rect, and a row grid matching the
     * selection highlight drawn below, so the row that lights up under the
     * cursor is the row a click lands on. */
    hit_set_panel(&p->hit, px, py, pw, ph);
    hit_set_rows(&p->hit, 12, top -16, pw - 24, row_h, POWER_ROW_COUNT);

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->power_ui.bg)
        s->power_ui.bg = wlr_scene_rect_create(s->power_ui.tree,
                                               pw, ph, bg_color);
    wlr_scene_rect_set_color(s->power_ui.bg, bg_color);
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

    /* Lid state, right-aligned on the title line. Without it the two lid rows
     * are a puzzle on a desktop — they would sit there configurable and never
     * do anything — and there is no other way to see which of the two the
     * machine would use right now. */
    {
        /* Naming the live case is what makes the three lid rows legible: they
         * are listed least-specific first but resolved docked-then-mains-then-
         * battery, and this says which one a lid close would use right now. */
        char lid[64];
        if (!s->power.lid_seen)
            snprintf(lid, sizeof(lid), "no lid switch");
        else
            snprintf(lid, sizeof(lid), "lid %s \xc2\xb7 %s",
                     p->lid_closed ? "closed" : "open", power_lid_case(s));

        cairo_set_font_size(cr, 12);
        cairo_text_extents_t te;
        cairo_text_extents(cr, lid, &te);
        set_ink(cr, INK_DIM, 0.9);
        cairo_move_to(cr, pw - 18 - te.width, 30);
        cairo_show_text(cr, lid);
    }

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

    set_ink(cr, INK_RULE, 0.5);
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
        /* A stage at "never" (or a lid row at "ignore") is inert; power.c says
         * which, so the panel reads at a glance as "these are armed, those are
         * not" without render.c having to know the vocabulary. */
        int off = power_panel_rows(s, i, name, sizeof(name),
                                   value, sizeof(value));

        cairo_set_font_size(cr, 14);
        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
        cairo_move_to(cr, pad + 8, ry + 4);
        cairo_show_text(cr, name);

        if (off) set_ink(cr, INK_DIM, 1.0);
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
    set_ink(cr, INK_DIM, 0.9);
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

/* ── Cursor theme picker (cursor.c) ──────────────────────── */

void synui_render_curpick(syn_server_t *s)
{
    if (!s->curpick.visible) {
        wlr_scene_node_set_enabled(&s->curpick_ui.tree->node, false);
        hit_clear(&s->curpick.hit);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    const int row_h = 48, top = 58, pad = 22;
    int total = curpick_total(s);
    int shown = total < CURPICK_ROWS ? total : CURPICK_ROWS;
    if (shown < 1) shown = 1;

    int pw = 520;
    int ph = top + shown * row_h + 56;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->curpick_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->curpick_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->curpick_ui.tree->node);

    /* Pointer geometry, as in the wallpaper picker above. */
    hit_set_panel(&s->curpick.hit, px, py, pw, ph);
    hit_set_rows(&s->curpick.hit, 12, top, pw - 24, row_h, shown);
    hit_set_first(&s->curpick.hit, s->curpick.scroll);

    /* Same near-opaque background as the wallpaper picker, and for the same
     * reason: every row carries a small-type path underneath it. */
    float bg_color[4];
    panel_bg_color(bg_color, 0.985f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->curpick_ui.bg)
        s->curpick_ui.bg = wlr_scene_rect_create(s->curpick_ui.tree,
                                                 pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->curpick_ui.bg, pw, ph);
    wlr_scene_rect_set_color(s->curpick_ui.bg, bg_color);
    if (!s->curpick_ui.accent)
        s->curpick_ui.accent = wlr_scene_rect_create(s->curpick_ui.tree,
                                                     pw, 2, accent);
    else
        wlr_scene_rect_set_color(s->curpick_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* Title */
    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "CURSOR THEME");

    double right_edge = pw - 18;

    /* Current size, and how to change it — the +/- keys are not guessable. */
    {
        char label[64];
        snprintf(label, sizeof(label), "[-/+] %dpx",
                 s->config.cursor_size > 0 ? s->config.cursor_size : 24);
        cairo_set_font_size(cr, 12);
        cairo_text_extents_t te;
        cairo_text_extents(cr, label, &te);
        cairo_set_source_rgba(cr, 0.75, 0.55, 0.95, 1.0);
        right_edge -= te.width;
        cairo_move_to(cr, right_edge, 30);
        cairo_show_text(cr, label);
        right_edge -= 12;
    }

    /* Separator */
    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 42);
    cairo_line_to(cr, pw - 18, 42);
    cairo_stroke(cr);

    if (total == 0) {
        cairo_set_font_size(cr, 13);
        set_ink(cr, INK_MUTED, 1.0);
        cairo_move_to(cr, pad, top + 24);
        cairo_show_text(cr, "No cursor themes installed.");
        cairo_set_font_size(cr, 12);
        set_ink(cr, 0.49, 1.0);
        cairo_move_to(cr, pad, top + 46);
        cairo_show_text(cr, "synui-cursor install <archive>   then press r");
    }

    for (int r = 0; r < shown && total > 0; r++) {
        int i = s->curpick.scroll + r;
        if (i >= total) break;

        int sel = (i == s->curpick.selected);
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
        curpick_row(s, i, &label, &desc);

        /* BOTH strings are third-party text: a theme's name is the directory
         * name from whatever archive it was unpacked from, and the path
         * contains it. cairo_show_text poisons its context on invalid UTF-8 and
         * every later draw silently no-ops, so one badly-named theme would
         * blank the whole panel from that row down. draw_clipped runs the text
         * through news_utf8_trim, which is why neither of these calls
         * cairo_show_text directly. */
        cairo_set_font_size(cr, 15);
        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
        draw_clipped(cr, pad + 8, ry + 22, pw - 2 * pad - 8, label);

        cairo_save(cr);
        cairo_rectangle(cr, pad, ry + 26, pw - 2 * pad - 8, 16);
        cairo_clip(cr);
        cairo_set_font_size(cr, 12);
        set_ink(cr, sel ? INK_MUTED : 0.49, 1.0);
        draw_clipped(cr, pad + 8, ry + 38, pw - 2 * pad - 8, desc);
        cairo_restore(cr);
    }

    if (total > shown) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", s->curpick.selected + 1, total);
        cairo_set_font_size(cr, 12);
        cairo_text_extents_t te;
        cairo_text_extents(cr, pos, &te);
        set_ink(cr, INK_DIM, 0.9);
        cairo_move_to(cr, right_edge - te.width, 30);
        cairo_show_text(cr, pos);
    }

    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 18, ph - 20);
    cairo_show_text(cr,
        "Up/Down preview \xc2\xb7 -/+ size \xc2\xb7 r rescan \xc2\xb7 Enter apply \xc2\xb7 Esc cancel");

    cairo_destroy(cr);
    set_scene_buffer(&s->curpick_ui.text_buf, s->curpick_ui.tree, buf);
}


/* The slider itself: a trough with a filled portion. Drawn rather than spelled
 * out because these values are judged by eye, and a bar you can see moving is
 * the whole difference between tuning a look and typing numbers at it. */
static void draw_slider(cairo_t *cr, double x, double y, double w, double h,
                        double frac, int sel, int dimmed)
{
    set_ink(cr, 0.11, 1.0);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    if (frac > 0.0) {
        if (dimmed)   set_ink(cr, 0.33, 1.0);
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
        hit_clear(&fl->hit);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    /* Two pages of different lengths share this frame, so nothing here may
     * assume FILTER_ROW_COUNT — including the background rect, which used to be
     * created once at the CRT page's height and never resized. */
    int uifx = (fl->page == FILTER_PAGE_UIFX);
    int rows = uifx ? UIFX_ROW_COUNT : FILTER_ROW_COUNT;
    int sel_row = uifx ? fl->uifx_selected : fl->selected;
    int page_dirty = uifx ? fl->uifx_dirty : fl->dirty;

    const int row_h = 34, top = 66, pad = 18;
    int pw = 560;
    int ph = top + rows * row_h + 96;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->filters_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->filters_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->filters_ui.tree->node);

    /* Pointer geometry: the panel rect, and a row grid matching the
     * selection highlight drawn below, so the row that lights up under the
     * cursor is the row a click lands on. */
    hit_set_panel(&fl->hit, px, py, pw, ph);
    hit_set_rows(&fl->hit, 12, top -18, pw - 24, row_h, rows);

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->filters_ui.bg)
        s->filters_ui.bg = wlr_scene_rect_create(s->filters_ui.tree,
                                                 pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->filters_ui.bg, pw, ph);
    wlr_scene_rect_set_color(s->filters_ui.bg, bg_color);
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
    cairo_show_text(cr, uifx ? "WINDOW EFFECTS" : "CRT FILTERS");

    /* The page you are NOT on, named at the top right, because a panel that
     * hides half of itself behind an unadvertised Tab has hidden it. */
    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_LABEL, 1.0);
    cairo_move_to(cr, pw - 210, 30);
    cairo_show_text(cr, uifx ? "Tab \xe2\x86\x92 CRT filters"
                             : "Tab \xe2\x86\x92 window effects");

    /* Why a row on this page might be doing nothing. The CRT page has two such
     * reasons (master off, or no GLES pass at all — pixman); the window page
     * asks uifx.c, whose answer is about the chrome style. */
    cairo_set_font_size(cr, 12);
    if (uifx) {
        const char *note = uifx_note(s);
        if (note) {
            cairo_set_source_rgba(cr, 0.95, 0.75, 0.25, 1.0);
            cairo_move_to(cr, 18, 50);
            cairo_show_text(cr, note);
        }
    } else if (!s->effects) {
        cairo_set_source_rgba(cr, 0.75, 0.45, 0.45, 1.0);
        cairo_move_to(cr, 18, 50);
        cairo_show_text(cr, "no GLES renderer \xc2\xb7 filters unavailable on this display");
    } else if (!s->config.effects) {
        cairo_set_source_rgba(cr, 0.95, 0.75, 0.25, 1.0);
        cairo_move_to(cr, 18, 50);
        cairo_show_text(cr, "filters off \xc2\xb7 Space to turn them on");
    }

    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 58);
    cairo_line_to(cr, pw - 18, 58);
    cairo_stroke(cr);

    /* A slider that cannot bite is drawn as such. On the CRT page that is all of
     * them at once (master off, or no GLES pass); on the window page it is
     * per-row, since "shadow is off" greys four rows and leaves blur alone. */
    int all_dimmed = !uifx && (!s->config.effects || !s->effects);

    for (int i = 0; i < rows; i++) {
        int sel = (i == sel_row);
        int ry = top + i * row_h;
        int dimmed = all_dimmed || (uifx && uifx_row_inert(s, i) != NULL);

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, 12, ry - 18, pw - 24, row_h - 4);
            cairo_fill(cr);
        }

        char value[32];
        float frac = uifx ? uifx_row_value(s, i, value, sizeof(value))
                          : filters_row_value(s, i, value, sizeof(value));

        cairo_set_font_size(cr, 14);
        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
        cairo_move_to(cr, pad + 8, ry + 4);
        cairo_show_text(cr, uifx ? uifx_row_label(i) : filters_row_label(i));

        if (frac < 0.0f) {              /* a switch: a word, not a bar */
            /* "on" reads as live only when the row is actually biting — an
             * accent-coloured "on" over a shadow the chrome has dropped would
             * be the panel contradicting the screen. */
            if (strcmp(value, "on") == 0 && !dimmed) set_accent(cr, 1.0);
            else set_ink(cr, INK_DIM, 1.0);
            cairo_move_to(cr, 250, ry + 4);
            cairo_show_text(cr, value);
        } else {
            draw_slider(cr, 250, ry - 9, 220, 12, frac, sel, dimmed);

            cairo_set_font_size(cr, 12);
            if (dimmed) set_ink(cr, INK_DIM, 1.0);
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
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 18, ph - 34);
    cairo_show_text(cr, "Up/Down select \xc2\xb7 Left/Right adjust \xc2\xb7 Space on/off");
    cairo_move_to(cr, 18, ph - 16);
    cairo_show_text(cr, page_dirty ? "s save (unsaved changes) \xc2\xb7 Tab page \xc2\xb7 Esc close"
                                   : "s save \xc2\xb7 Tab page \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->filters_ui.text_buf, s->filters_ui.tree, buf);
}

/* ── Desktop widget manager (widgets.c) ──────────────────── */

void synui_render_widgets(syn_server_t *s)
{
    syn_widgets_t *w = &s->widgets;

    if (!w->visible) {
        wlr_scene_node_set_enabled(&s->widgets_ui.tree->node, false);
        hit_clear(&w->hit);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    const int row_h = 34, top = 66, pad = 18;
    int pw = 520;
    int ph = top + WIDGET_ROW_COUNT * row_h + 96;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->widgets_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->widgets_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->widgets_ui.tree->node);

    /* Pointer geometry: the panel rect, and a row grid matching the
     * selection highlight drawn below, so the row that lights up under the
     * cursor is the row a click lands on. */
    hit_set_panel(&w->hit, px, py, pw, ph);
    hit_set_rows(&w->hit, 12, top -18, pw - 24, row_h, WIDGET_ROW_COUNT);

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->widgets_ui.bg)
        s->widgets_ui.bg = wlr_scene_rect_create(s->widgets_ui.tree,
                                                 pw, ph, bg_color);
    wlr_scene_rect_set_color(s->widgets_ui.bg, bg_color);
    if (!s->widgets_ui.accent)
        s->widgets_ui.accent = wlr_scene_rect_create(s->widgets_ui.tree,
                                                     pw, 2, accent);
    else
        wlr_scene_rect_set_color(s->widgets_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "DESKTOP WIDGETS");

    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_LABEL, 1.0);
    cairo_move_to(cr, 18, 50);
    cairo_show_text(cr, "each one on its own \xc2\xb7 drag by the grip \xc2\xb7 Space is all-or-nothing");

    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 58);
    cairo_line_to(cr, pw - 18, 58);
    cairo_stroke(cr);

    for (int i = 0; i < WIDGET_ROW_COUNT; i++) {
        int sel = (i == w->selected);
        int ry  = top + i * row_h;

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, 12, ry - 18, pw - 24, row_h - 4);
            cairo_fill(cr);
        }

        /* The master row is a summary of the rows below it, so it is set apart
         * by a rule rather than left to read as one more widget. */
        if (i == WIDGET_ROW_ALL + 1) {
            set_ink(cr, INK_RULE, 0.45);
            cairo_move_to(cr, 18, ry - 22);
            cairo_line_to(cr, pw - 18, ry - 22);
            cairo_stroke(cr);
        }

        cairo_set_font_size(cr, 14);
        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
        cairo_move_to(cr, pad + 8, ry + 4);
        cairo_show_text(cr, widget_row_label(i));

        const char *val = widgets_row_value(s, i);
        if (strcmp(val, "off") == 0)
            set_ink(cr, INK_DIM, 1.0);
        else
            set_accent(cr, 1.0);
        cairo_move_to(cr, 330, ry + 4);
        cairo_show_text(cr, val);

        /* A widget that is on and still invisible needs saying so on its own
         * row, not only in the status line after you toggle it. */
        if (i == WIDGET_ROW_VISUALIZER && !w->have_cava) {
            cairo_set_font_size(cr, 11);
            cairo_set_source_rgba(cr, 0.75, 0.55, 0.35, 1.0);
            cairo_move_to(cr, 390, ry + 4);
            cairo_show_text(cr, "needs cava");
        }
    }

    if (w->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, ph - 56);
        cairo_show_text(cr, w->status);
    }

    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 18, ph - 34);
    cairo_show_text(cr, "Up/Down select \xc2\xb7 Enter toggle \xc2\xb7 Left/Right off/on");
    cairo_move_to(cr, 18, ph - 16);
    cairo_show_text(cr, "Space all on/off \xc2\xb7 R reset positions \xc2\xb7 Esc close");

    cairo_destroy(cr);
    set_scene_buffer(&s->widgets_ui.text_buf, s->widgets_ui.tree, buf);
}

/* ── AI model picker (aimodel.c) ─────────────────────────── */

/* "7.0G" — a model's size is the thing you weigh against your VRAM, and bytes
 * are unreadable at this scale. */
static void aimodel_size_str(long long bytes, char *out, size_t n)
{
    double g = (double)bytes / (1024.0 * 1024.0 * 1024.0);
    if (g >= 1.0) snprintf(out, n, "%.1fG", g);
    else          snprintf(out, n, "%lldM", bytes / (1024 * 1024));
}

/*
 * Draw text at the current point, trimmed to fit with an ellipsis.
 *
 * Everything in the AVAILABLE section came off the network, so this goes
 * through syn_show_text() rather than cairo_show_text(): an invalid UTF-8
 * sequence puts the cairo context into a permanent error state, and an error
 * state is a BLANK PANEL — a repo name is not something to hand cairo raw.
 */
static void aimodel_fit(cairo_t *cr, const char *text, double max_w)
{
    char safe[512];
    syn_utf8_copy(safe, sizeof(safe), text ? text : "");

    cairo_text_extents_t ext;
    cairo_text_extents(cr, safe, &ext);
    if (ext.x_advance <= max_w) { syn_show_text(cr, safe); return; }

    /* Back off a character at a time, on UTF-8 boundaries, until the ellipsis
     * fits too. */
    size_t len = strlen(safe);
    while (len > 0) {
        do { len--; } while (len > 0 && ((unsigned char)safe[len] & 0xC0) == 0x80);

        char trial[520];
        snprintf(trial, sizeof(trial), "%.*s\xe2\x80\xa6", (int)len, safe);
        cairo_text_extents(cr, trial, &ext);
        if (ext.x_advance <= max_w) { syn_show_text(cr, trial); return; }
    }
}

/*
 * Draw `text` wrapped to `w`, at most `max_lines` of them, and return the y
 * just past the last line drawn.
 *
 * The pane's other text is all short enough to be one line and ellipsised by
 * aimodel_fit when it is not. The bio is a paragraph, and a paragraph that
 * ellipsises at the first line is a paragraph that was not worth assembling —
 * hence a second text routine rather than a wider call to the first.
 *
 * Every line goes out through aimodel_fit, so a single word wider than the
 * pane is cut with an ellipsis instead of running into the frame, and the
 * UTF-8 sanitising that keeps cairo out of a permanent error state applies
 * here too. When the text outruns max_lines the remainder is handed to the
 * last line whole, so it ends in an ellipsis rather than stopping mid-sentence
 * as though that were the end of it.
 */
static int aimodel_wrap(cairo_t *cr, const char *text, int x, int y,
                        double w, int lh, int max_lines)
{
    char safe[768];
    syn_utf8_copy(safe, sizeof(safe), text ? text : "");

    const char *p = safe;
    int drawn = 0;

    while (*p && drawn < max_lines) {
        while (*p == ' ') p++;
        if (!*p) break;

        /* Take words until one does not fit; `end` trails the last that did.
         * The first word of a line is taken unconditionally — otherwise a word
         * wider than the pane makes no progress and this loops forever. */
        const char *end = p, *scan = p;
        for (;;) {
            while (*scan && *scan != ' ') scan++;

            char trial[768];
            snprintf(trial, sizeof(trial), "%.*s", (int)(scan - p), p);
            cairo_text_extents_t ext;
            cairo_text_extents(cr, trial, &ext);
            if (ext.x_advance > w && end > p) break;

            end = scan;
            while (*scan == ' ') scan++;
            if (!*scan) break;
        }

        char line[768];
        int last = (drawn == max_lines - 1);
        snprintf(line, sizeof(line), "%.*s", (int)(end - p), p);

        cairo_move_to(cr, x, y + drawn * lh);
        aimodel_fit(cr, (last && *end) ? p : line, w);
        drawn++;
        p = end;
    }

    return y + drawn * lh;
}

/* "1.2M", "834K" — a download count is a rough sense of how used a model is,
 * not a figure anybody reads to the digit. */
static void aimodel_count_str(long long n, char *out, size_t len)
{
    if      (n >= 1000000) snprintf(out, len, "%.1fM", (double)n / 1000000.0);
    else if (n >= 1000)    snprintf(out, len, "%.0fK", (double)n / 1000.0);
    else                   snprintf(out, len, "%lld", n);
}

/*
 * A download in flight, drawn at the foot of the pane.
 *
 * Its own function because it is drawn from EVERY row, not only the one it
 * was started from: while several gigabytes are moving, that is the thing
 * most worth knowing, and hunting for the row you started it on to see how
 * far it got would be a worse panel than the one that had no downloads.
 */
static void aimodel_render_dl(cairo_t *cr, syn_aimodel_t *am,
                              int x, int w, int ph)
{
    if (am->dl.state == AIMODEL_DL_IDLE) return;

    int by = ph - 104;
    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_TITLE, 1.0);
    cairo_move_to(cr, x, by);
    aimodel_fit(cr, am->dl.file, w - 60);

    const char *state =
        am->dl.state == AIMODEL_DL_DONE     ? "done"     :
        am->dl.state == AIMODEL_DL_FAILED   ? "failed"   :
        am->dl.state == AIMODEL_DL_STARTING ? "starting" : "";
    if (state[0]) {
        cairo_set_font_size(cr, 11);
        if (am->dl.state == AIMODEL_DL_FAILED)
            cairo_set_source_rgba(cr, 0.80, 0.45, 0.40, 1.0);
        else
            set_accent(cr, 1.0);
        cairo_move_to(cr, x + w - 52, by);
        cairo_show_text(cr, state);
    }

    /* The bar, and the byte counts under it — a percentage alone says nothing
     * about whether a stalled download is 40 MB or 4 GB from finishing. */
    int bw = w, bh = 6, bx = x, byy = by + 10;
    set_ink(cr, 0.21, 1.0);
    cairo_rectangle(cr, bx, byy, bw, bh);
    cairo_fill(cr);

    int pct = am->dl.pct < 0 ? 0 : (am->dl.pct > 100 ? 100 : am->dl.pct);
    if (am->dl.state == AIMODEL_DL_DONE) pct = 100;
    if (pct > 0) {
        if (am->dl.state == AIMODEL_DL_FAILED)
            cairo_set_source_rgba(cr, 0.70, 0.35, 0.32, 1.0);
        else
            set_accent(cr, 1.0);
        cairo_rectangle(cr, bx, byy, bw * pct / 100, bh);
        cairo_fill(cr);
    }

    char got[24], tot[24], line[96];
    aimodel_size_str(am->dl.got, got, sizeof(got));
    aimodel_size_str(am->dl.total, tot, sizeof(tot));
    if (am->dl.total > 0)
        snprintf(line, sizeof(line), "%s of %s  \xc2\xb7  %d%%", got, tot, pct);
    else
        snprintf(line, sizeof(line), "%s", got);

    cairo_set_font_size(cr, 11);
    set_ink(cr, 0.49, 1.0);
    cairo_move_to(cr, x, byy + 20);
    cairo_show_text(cr, line);

    if (am->dl.msg[0]) {
        cairo_move_to(cr, x, byy + 36);
        aimodel_fit(cr, am->dl.msg, w);
    }
}

/*
 * The right-hand pane: everything known about the row under the cursor.
 *
 * For an installed model that is the filename and whether it is the one
 * running. For one that is not installed it is what the repo says about
 * itself, plus the list of quantisations — the choice that decides both how
 * big the download is and how good the answers are, and the reason this panel
 * needed a second column at all.
 */
/* Takes the server, not just the panel state: the installed-model side reads
 * each GGUF's header through aimodel_info(), which caches onto the entry and so
 * needs the owning server rather than a copy of the list. */
static void aimodel_render_pane(cairo_t *cr, syn_server_t *s,
                                int px, int py, int pw, int ph,
                                int x, int y, int w)
{
    syn_aimodel_t *am = &s->aimodel;

    hit_clear(&am->hit_files);

    const syn_aimodel_cat_t *c =
        (am->cat_sel >= 0 && am->cat_sel < am->n_cat) ? &am->cat[am->cat_sel]
                                                      : NULL;

    /* The pane's body stops short of the progress block when one is drawn, so
     * the quantisation list and the download never overlap. */
    int dl_live = (am->dl.state != AIMODEL_DL_IDLE);
    int body_bottom = ph - (dl_live ? 132 : 72);

    if (!c) {
        if (am->count == 0) {
            /* "Empty" and "could not look" are different facts and get
             * different words. Telling someone to download a model when the
             * directory is full of them, and the daemon has one loaded, sends
             * them off to fix the wrong thing entirely. */
            cairo_set_font_size(cr, 13);
            set_ink(cr, 0.61, 1.0);
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, am->scan_err ? "Cannot read the model directory."
                                             : "No models installed.");
            cairo_set_font_size(cr, 12);
            set_ink(cr, 0.49, 1.0);
            cairo_move_to(cr, x, y + 24);
            /* The footer already prints the path and strerror(); this says what
             * it means, since a permission error here is a packaging fault and
             * not something the user did wrong. */
            cairo_show_text(cr, am->scan_err == EACCES
                ? "A model may still be loaded \xc2\xb7 see the message below."
                : "Pick one from AVAILABLE below and press Enter.");
        } else if (am->selected >= 0 && am->selected < am->count) {
            const syn_aimodel_entry_t *m = &am->models[am->selected];

            cairo_set_font_size(cr, 15);
            set_ink(cr, 0.97, 1.0);
            cairo_move_to(cr, x, y);
            aimodel_fit(cr, m->name, w);

            /* Read out of the FILE, not out of its name. A downloaded GGUF can
             * be called anything, and the quantisation in a filename is whoever
             * uploaded it typing a claim — this is the header. */
            const syn_gguf_t *g = aimodel_info(s, am->selected);

            /* general.name under the filename, the way the repo pane puts the
             * author under the repo. Skipped when it says nothing the filename
             * did not: two lines of the same words waste the only space the
             * pane has. */
            int ry = y + 18;
            if (g && g->ok && g->name[0] &&
                strncasecmp(g->name, m->name, strlen(g->name)) != 0) {
                cairo_set_font_size(cr, 11);
                cairo_set_source_rgba(cr, 0.5, 0.55, 0.7, 1.0);
                cairo_move_to(cr, x, ry);
                aimodel_fit(cr, g->name, w);
            }

            char sz[24], params[24], ctx[24], arch[64];
            aimodel_size_str(m->bytes, sz, sizeof(sz));

            /* general.parameter_count is absent from most real files — all
             * three on this box omit it — so general.size_label ("12B") is
             * tried first and the count is the fallback, not the other way
             * round. A dash beats a number that is not there. */
            if (g && g->ok && g->size_label[0])
                snprintf(params, sizeof(params), "%s", g->size_label);
            else if (g && g->ok)
                gguf_params_str(g->params, params, sizeof(params));
            else
                snprintf(params, sizeof(params), "\xe2\x80\x94");

            if (g && g->ok && g->ctx >= 1000)
                snprintf(ctx, sizeof(ctx), "%lldk", g->ctx / 1000);
            else if (g && g->ok && g->ctx > 0)
                snprintf(ctx, sizeof(ctx), "%lld", g->ctx);
            else
                snprintf(ctx, sizeof(ctx), "\xe2\x80\x94");

            if (g && g->ok && g->arch[0] && g->n_layers > 0)
                snprintf(arch, sizeof(arch), "%s \xc2\xb7 %d layers",
                         g->arch, g->n_layers);
            else if (g && g->ok && g->arch[0])
                snprintf(arch, sizeof(arch), "%s", g->arch);
            else
                snprintf(arch, sizeof(arch), "\xe2\x80\x94");

            /* The filename quantisation stays as the fallback: an older or
             * hand-made GGUF can leave general.file_type out, and a guess
             * labelled as one beats an empty row. */
            char quant[24];
            if (g && g->ok && g->quant[0])
                snprintf(quant, sizeof(quant), "%s", g->quant);
            else {
                aimodel_quant_of(m->name, quant, sizeof(quant));
                if (!quant[0]) snprintf(quant, sizeof(quant), "\xe2\x80\x94");
            }

            struct { const char *k, *v; } rows[5];
            rows[0].k = "Size";    rows[0].v = sz;
            rows[1].k = "Params";  rows[1].v = params;
            rows[2].k = "Quant";   rows[2].v = quant;
            rows[3].k = "Context"; rows[3].v = ctx;
            rows[4].k = "Arch";    rows[4].v = arch;

            for (int i = 0; i < 5; i++) {
                int rry = y + 44 + i * 20;
                cairo_set_font_size(cr, 12);
                set_ink(cr, INK_DIM, 1.0);
                cairo_move_to(cr, x, rry);
                cairo_show_text(cr, rows[i].k);
                set_ink(cr, INK_TITLE, 1.0);
                cairo_move_to(cr, x + 70, rry);
                aimodel_fit(cr, rows[i].v, w - 70);
            }

            /* The one line here that is a WARNING rather than a fact. No chat
             * template means synapd has to guess how to frame a turn, and a
             * wrongly-framed prompt still answers fluently — the panel says so
             * before you load it, which is the whole reason the daemon's
             * format="legacy" was worth surfacing at all. */
            int wy = y + 44 + 5 * 20 + 6;
            cairo_set_font_size(cr, 11);
            if (g && g->ok && !g->has_template) {
                cairo_set_source_rgba(cr, 0.75, 0.55, 0.35, 1.0);
                cairo_move_to(cr, x, wy);
                aimodel_fit(cr, "no chat template \xc2\xb7 synapd will guess "
                                "the turn format", w);
            } else if (g && !g->ok) {
                cairo_set_source_rgba(cr, 0.75, 0.55, 0.35, 1.0);
                cairo_move_to(cr, x, wy);
                aimodel_fit(cr, g->err[0] ? g->err : "unreadable header", w);
            }

            /*
             * ── What this model is FOR ──────────────────────────────────
             *
             * Everything above this point is a measurement, and velle's
             * complaint was that a pane full of them still does not tell you
             * whether to pick the thing: "there's info but nothing that helps
             * decide to use that an end user would understand". "Arch
             * nomic-bert" is the sharpest example — a true statement that
             * hides the fact that the file cannot hold a conversation at all.
             *
             * So the same header gets read a second way, in sentences. Nothing
             * here is invented: it is general.tags, general.languages,
             * general.license and the base model, which were being parsed past
             * until now, plus the size and quantisation already on screen said
             * in words rather than in codes.
             */
            int by = wy + 26;
            cairo_set_font_size(cr, 11);
            set_ink(cr, 0.40, 1.0);
            cairo_move_to(cr, x, by);
            cairo_show_text(cr, "ABOUT");

            /* The three facts that are lists rather than prose. Each is
             * skipped when the file did not say — a row of dashes here would
             * be four lines saying nothing, and the pane's whole problem was
             * lines that say nothing. */
            char good[160], langs[160], based[160];
            gguf_good_at(g, good, sizeof(good));
            gguf_langs_str(g, langs, sizeof(langs));
            gguf_based_on(g, based, sizeof(based));

            struct { const char *k, *v; } about[4] = {
                { "Good at",  good              },
                { "Speaks",   langs             },
                { "License",  g ? g->license : "" },
                { "Based on", based             },
            };

            /* The rows below are counted BEFORE the paragraph is drawn, and
             * the paragraph gets what is left. Sized the other way round, a
             * five-line bio on a model that also names four facts would push
             * the last of them under the footer — and it is the facts that get
             * dropped in that arrangement, which is backwards: they are the
             * shortest and most specific things on the pane. */
            int n_rows = 0;
            for (int i = 0; i < 4; i++)
                if (about[i].v && about[i].v[0]) n_rows++;

            char bio[512];
            gguf_bio(g, m->bytes, bio, sizeof(bio));
            cairo_set_font_size(cr, 12);
            cairo_set_source_rgba(cr, 0.74, 0.76, 0.86, 1.0);

            int bio_top = by + 22;
            int budget  = (body_bottom - 8 - n_rows * 19 - 10 - bio_top) / 17;
            if (budget < 2) budget = 2;    /* never nothing; it ellipsises */
            if (budget > 8) budget = 8;

            int ly = aimodel_wrap(cr, bio, x, bio_top, w, 17, budget) + 10;

            for (int i = 0; i < 4; i++) {
                if (!about[i].v || !about[i].v[0]) continue;
                if (ly > body_bottom - 8) break;   /* never into the footer */
                cairo_set_font_size(cr, 11);
                set_ink(cr, INK_DIM, 1.0);
                cairo_move_to(cr, x, ly);
                cairo_show_text(cr, about[i].k);
                cairo_set_font_size(cr, 12);
                cairo_set_source_rgba(cr, 0.72, 0.74, 0.84, 1.0);
                cairo_move_to(cr, x + 70, ly);
                aimodel_fit(cr, about[i].v, w - 70);
                ly += 19;
            }

            /* Anchored to the bottom of the pane rather than to the block
             * above it, the way the repo side already does it: the block above
             * is now a variable number of lines, and a footer that floated
             * with it would sit in a different place for every model. */
            cairo_set_font_size(cr, 12);
            if (am->selected == am->loaded_idx) {
                set_accent(cr, 1.0);
                cairo_move_to(cr, x, body_bottom + 22);
                cairo_show_text(cr, am->switching ? "loading \xe2\x80\xa6"
                                                  : "this is the model synapd is running");
            } else {
                set_ink(cr, 0.61, 1.0);
                cairo_move_to(cr, x, body_bottom + 22);
                cairo_show_text(cr, "[ Enter: load this model ]");
            }
        }
        aimodel_render_dl(cr, am, x, w, ph);
        return;
    }

    /* ── A repo ──────────────────────────────────────────────────────── */
    cairo_set_font_size(cr, 15);
    set_ink(cr, 0.97, 1.0);
    cairo_move_to(cr, x, y);
    aimodel_fit(cr, c->name, w);

    cairo_set_font_size(cr, 11);
    cairo_set_source_rgba(cr, 0.5, 0.55, 0.7, 1.0);
    cairo_move_to(cr, x, y + 18);
    aimodel_fit(cr, c->author, w);

    const syn_aimodel_file_t *f =
        (c->n_files > 0 && c->sel_file >= 0 && c->sel_file < c->n_files)
            ? &c->files[c->sel_file] : NULL;

    char dls[24], szbuf[24];
    aimodel_count_str(c->downloads, dls, sizeof(dls));
    aimodel_size_str(f ? f->bytes : -1, szbuf, sizeof(szbuf));

    /* The quantisation list below is the choice this pane exists to make, and
     * "IQ3_XS" is not a thing anybody can choose on. The selected one gets a
     * line saying what it costs — the same words the installed side uses, so
     * the file reads the same before and after it is downloaded. */
    char qual[96];
    const char *qe = f ? gguf_quant_english(f->quant) : NULL;
    snprintf(qual, sizeof(qual), "%s", qe ? qe : "\xe2\x80\x94");

    struct { const char *k, *v; } det[5];
    det[0].k = "Params";   det[0].v = c->params[0] ? c->params : "\xe2\x80\x94";
    det[1].k = "Download"; det[1].v = f ? szbuf : "\xe2\x80\x94";
    det[2].k = "Quality";  det[2].v = qual;
    det[3].k = "License";  det[3].v = c->license[0] ? c->license : "\xe2\x80\x94";
    det[4].k = "Pulls";    det[4].v = dls;

    for (int i = 0; i < 5; i++) {
        int ry = y + 46 + i * 20;
        cairo_set_font_size(cr, 12);
        set_ink(cr, INK_DIM, 1.0);
        cairo_move_to(cr, x, ry);
        cairo_show_text(cr, det[i].k);
        set_ink(cr, INK_TITLE, 1.0);
        cairo_move_to(cr, x + 70, ry);
        aimodel_fit(cr, det[i].v, w - 70);
    }

    /* ── About ───────────────────────────────────────────────────────── */
    /*
     * The same block the installed pane carries, on the side where it can
     * still change the answer. Reading what a model is FOR only after several
     * GB have landed is the wrong order, and it was the missing half of this
     * pane: the rows above say how big and how compressed, which are the two
     * questions you can only ask once you already know you want it.
     *
     * Nothing is invented. It is the repo's own tags and base_model, run
     * through the same gguf_tag_english() the installed side uses, plus the
     * selected file's size and quantisation said in words — so a model reads
     * the same before and after it is downloaded.
     */
    int ay = y + 154;
    cairo_set_font_size(cr, 11);
    set_ink(cr, 0.40, 1.0);
    cairo_move_to(cr, x, ay);
    cairo_show_text(cr, "ABOUT");

    char cgood[160], cbased[160];
    aimodel_cat_good_at(c, cgood, sizeof(cgood));
    aimodel_cat_based_on(c, cbased, sizeof(cbased));

    struct { const char *k, *v; } cabout[2] = {
        { "Good at",  cgood  },
        { "Based on", cbased },
    };
    int c_rows = 0;
    for (int i = 0; i < 2; i++)
        if (cabout[i].v[0]) c_rows++;

    char cbio[512];
    aimodel_cat_bio(c, f, cbio, sizeof(cbio));

    /* This pane also owns the quantisation chooser, which is the thing being
     * interacted with — so the prose is capped harder than the installed
     * side's eight lines and the list keeps the rest. The floor of two is what
     * stops a very tall ABOUT on a short screen from leaving no list at all;
     * aimodel_wrap ellipsises rather than overflowing. */
    int cbio_top = ay + 22;
    int c_budget = (body_bottom - 120 - c_rows * 19 - cbio_top) / 17;
    if (c_budget < 2) c_budget = 2;
    if (c_budget > 4) c_budget = 4;

    cairo_set_font_size(cr, 12);
    cairo_set_source_rgba(cr, 0.74, 0.76, 0.86, 1.0);
    int cy = aimodel_wrap(cr, cbio, x, cbio_top, w, 17, c_budget) + 8;

    for (int i = 0; i < 2; i++) {
        if (!cabout[i].v[0]) continue;
        cairo_set_font_size(cr, 11);
        set_ink(cr, INK_DIM, 1.0);
        cairo_move_to(cr, x, cy);
        cairo_show_text(cr, cabout[i].k);
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, 0.72, 0.74, 0.84, 1.0);
        cairo_move_to(cr, x + 70, cy);
        aimodel_fit(cr, cabout[i].v, w - 70);
        cy += 19;
    }

    /* ── The quantisations ───────────────────────────────────────────── */
    /* Follows ABOUT, which is a variable number of lines — so this is measured
     * from where that block actually ended rather than from a fixed offset. */
    int fy = cy + 14;
    cairo_set_font_size(cr, 11);
    set_ink(cr, 0.40, 1.0);
    cairo_move_to(cr, x, fy);
    cairo_show_text(cr, "QUANTISATION");

    if (c->detail == AIMODEL_DETAIL_BUSY || c->detail == AIMODEL_DETAIL_WANT) {
        cairo_set_font_size(cr, 12);
        set_ink(cr, INK_LABEL, 1.0);
        cairo_move_to(cr, x, fy + 24);
        cairo_show_text(cr, "reading the repository \xe2\x80\xa6");
    } else if (c->detail == AIMODEL_DETAIL_FAIL) {
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, 0.75, 0.55, 0.35, 1.0);
        cairo_move_to(cr, x, fy + 24);
        cairo_show_text(cr, "could not read the repository");
    } else if (c->n_files == 0) {
        cairo_set_font_size(cr, 12);
        set_ink(cr, INK_LABEL, 1.0);
        cairo_move_to(cr, x, fy + 24);
        cairo_show_text(cr, "no single-file GGUF here");
    } else {
        const int frow_h = 22;
        int max_rows = (body_bottom - (fy + 12)) / frow_h;
        if (max_rows < 1) max_rows = 1;
        if (max_rows > c->n_files) max_rows = c->n_files;

        /* Keep the selected quantisation on screen when a repo has more of
         * them than the pane can show. */
        int first = 0;
        if (c->sel_file >= max_rows) first = c->sel_file - max_rows + 1;

        hit_set_panel(&am->hit_files, px, py, pw, ph);
        hit_set_rows(&am->hit_files, x, fy + 12 - 15, w, frow_h, max_rows);
        hit_set_first(&am->hit_files, first);

        for (int v = 0; v < max_rows; v++) {
            int i  = first + v;
            int ry = fy + 12 + v * frow_h + 15;
            int sel = (i == c->sel_file);

            if (sel) {
                set_accent(cr, 0.30);
                cairo_rectangle(cr, x - 6, ry - 15, w + 6, frow_h - 2);
                cairo_fill(cr);
            }

            cairo_set_font_size(cr, 12);
            set_ink(cr, sel ? INK_STRONG : 0.74, 1.0);
            cairo_move_to(cr, x, ry);
            cairo_show_text(cr, c->files[i].quant[0] ? c->files[i].quant
                                                     : "\xe2\x80\x94");

            char fsz[24];
            aimodel_size_str(c->files[i].bytes, fsz, sizeof(fsz));
            cairo_set_font_size(cr, 11);
            set_ink(cr, 0.49, 1.0);
            cairo_move_to(cr, x + 90, ry);
            cairo_show_text(cr, fsz);

            set_ink(cr, 0.40, 1.0);
            cairo_move_to(cr, x + 160, ry);
            aimodel_fit(cr, c->files[i].file, w - 160);
        }

        if (c->n_files > max_rows) {
            cairo_set_font_size(cr, 10);
            set_ink(cr, 0.38, 1.0);
            char more[48];
            snprintf(more, sizeof(more), "+%d more", c->n_files - max_rows);
            cairo_move_to(cr, x + w - 60, fy);
            cairo_show_text(cr, more);
        }

        cairo_set_font_size(cr, 12);
        set_ink(cr, 0.61, 1.0);
        cairo_move_to(cr, x, body_bottom + 22);
        cairo_show_text(cr, "[ Enter: download ]");
    }

    aimodel_render_dl(cr, am, x, w, ph);
}

void synui_render_aimodel(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    if (!am->visible) {
        wlr_scene_node_set_enabled(&s->aimodel_ui.tree->node, false);
        hit_clear(&am->hit);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    /* Two columns: the list of models on the left, and what is known about the
     * one under the cursor on the right. The frame is a fixed size now rather
     * than sized to the model count — the AVAILABLE section is a page of search
     * results, so the list scrolls inside a constant box instead of the box
     * growing to whatever Hugging Face returned. */
    const int row_h = 26, pad = 18;
    const int list_x = 12, list_w = 318;
    const int pane_x = list_x + list_w + 18;
    const int list_top = 200;                  /* first row's text baseline */
    int pw = 900;
    int ph = list_top + AIMODEL_ROWS * row_h + 74;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->aimodel_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->aimodel_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->aimodel_ui.tree->node);

    /* Row grid starts below the header, so a click lands on the model the
     * highlight is under rather than counting from the top of the panel. The
     * grid counts SLOTS — the two section headings included — because that is
     * what is drawn, and a grid that skipped them would select the row above
     * the one pointed at from the AVAILABLE section down.
     *
     * Only the visible window is registered, offset by the scroll position, so
     * a click below the last drawn row is chrome rather than a row off-screen. */
    int slots   = aimodel_slots(am);
    int visible = slots - am->scroll;
    if (visible > AIMODEL_ROWS) visible = AIMODEL_ROWS;
    if (visible < 0) visible = 0;

    hit_set_panel(&am->hit, px, py, pw, ph);
    hit_set_rows(&am->hit, list_x, list_top - 18, list_w, row_h, visible);
    hit_set_first(&am->hit, am->scroll);

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->aimodel_ui.bg)
        s->aimodel_ui.bg = wlr_scene_rect_create(s->aimodel_ui.tree,
                                                 pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->aimodel_ui.bg, pw, ph);
    wlr_scene_rect_set_color(s->aimodel_ui.bg, bg_color);
    if (!s->aimodel_ui.accent)
        s->aimodel_ui.accent = wlr_scene_rect_create(s->aimodel_ui.tree,
                                                     pw, 2, accent);
    else
        wlr_scene_rect_set_color(s->aimodel_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "AI MODEL");

    /* ── What synapd detected ────────────────────────────────────────────
     * The reason this panel exists. Every line is what the DAEMON reported,
     * never what synui would work out from the filename — the bug worth
     * catching is precisely the two disagreeing. A dash means synapd did not
     * say, which an older daemon never will. */
    const syn_overlay_t *ov = &s->overlay;
    int online = ov->mon_online;

    struct { const char *k; const char *v; } det[3];
    det[0].k = "Loaded";
    det[0].v = !online              ? "synapd not responding"
             : ov->model_name[0]    ? ov->model_name
             : "\xe2\x80\x94";
    det[1].k = "Format";
    det[1].v = ov->format[0] ? ov->format : "\xe2\x80\x94";
    det[2].k = "Profile";
    det[2].v = ov->profile[0] ? ov->profile : "\xe2\x80\x94";

    for (int i = 0; i < 3; i++) {
        int ry = 56 + i * 20;
        cairo_set_font_size(cr, 12);
        set_ink(cr, INK_DIM, 1.0);
        cairo_move_to(cr, pad, ry);
        cairo_show_text(cr, det[i].k);

        cairo_set_font_size(cr, 12);
        set_ink(cr, INK_TITLE, 1.0);
        cairo_move_to(cr, pad + 76, ry);
        cairo_show_text(cr, det[i].v);
    }

    /* "legacy" is not a format the model asked for — it is synapd falling back
     * because the GGUF declared none, and it is worth flagging rather than
     * printing as though it were a detected answer. */
    if (strcmp(ov->format, "legacy") == 0) {
        cairo_set_font_size(cr, 11);
        cairo_set_source_rgba(cr, 0.75, 0.55, 0.35, 1.0);
        cairo_move_to(cr, pad + 76 + 60, 76);
        cairo_show_text(cr, "GGUF declares no chat template");
    }

    char samp[96];
    if (online && ov->top_k > 0)
        snprintf(samp, sizeof(samp), "temp %.2f  \xc2\xb7  top_p %.2f  \xc2\xb7  top_k %d",
                 (double)ov->temperature, (double)ov->top_p, ov->top_k);
    else
        snprintf(samp, sizeof(samp), "\xe2\x80\x94");

    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 1.0);
    cairo_move_to(cr, pad, 116);
    cairo_show_text(cr, "Sampling");
    set_ink(cr, INK_TITLE, 1.0);
    cairo_move_to(cr, pad + 76, 116);
    cairo_show_text(cr, samp);

    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 132);
    cairo_line_to(cr, pw - 18, 132);
    cairo_stroke(cr);

    /* ── The search box ──────────────────────────────────────────────── */
    cairo_set_font_size(cr, 12);
    if (am->typing) {
        set_accent(cr, 0.25);
        cairo_rectangle(cr, list_x, 152, list_w, 24);
        cairo_fill(cr);
    }
    set_ink(cr, 0.49, 1.0);
    cairo_move_to(cr, list_x + 8, 168);
    cairo_show_text(cr, "/");

    set_ink(cr, 0.89, 1.0);
    cairo_move_to(cr, list_x + 22, 168);
    if (am->query[0]) {
        char q[AIMODEL_QUERY_MAX + 2];
        snprintf(q, sizeof(q), "%s%s", am->query, am->typing ? "_" : "");
        syn_show_text(cr, q);
    } else {
        set_ink(cr, INK_DIM, 1.0);
        cairo_show_text(cr, am->typing ? "_" : "search huggingface");
    }

    /* ── The list ────────────────────────────────────────────────────── */
    for (int v = 0; v < visible; v++) {
        int slot = am->scroll + v;
        int ry   = list_top + v * row_h;
        int sel  = (slot == aimodel_cursor_slot(am));

        /* A section heading. */
        if (aimodel_slot_is_head(am, slot)) {
            cairo_set_font_size(cr, 11);
            set_ink(cr, 0.40, 1.0);
            cairo_move_to(cr, list_x + 6, ry);
            if (slot == 0) {
                cairo_show_text(cr, am->count ? "INSTALLED" : "INSTALLED \xc2\xb7 none");
            } else {
                char head[128];
                if (am->search_msg[0])
                    snprintf(head, sizeof(head), "AVAILABLE \xc2\xb7 %s", am->search_msg);
                else
                    snprintf(head, sizeof(head), "AVAILABLE \xc2\xb7 %d", am->n_cat);
                cairo_show_text(cr, head);
            }
            continue;
        }

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, list_x, ry - 17, list_w, row_h - 3);
            cairo_fill(cr);
        }

        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);

        if (slot <= am->count) {
            /* An installed model: the filename, because that is what synapd
             * is asked to load and what tells one download from another. */
            int i = slot - 1;
            cairo_set_font_size(cr, 12);
            cairo_move_to(cr, list_x + 8, ry);
            aimodel_fit(cr, am->models[i].name, list_w - 96);

            char sz[24];
            aimodel_size_str(am->models[i].bytes, sz, sizeof(sz));
            cairo_set_font_size(cr, 11);
            set_ink(cr, 0.49, 1.0);
            cairo_move_to(cr, list_x + list_w - 46, ry);
            cairo_show_text(cr, sz);

            /* The armed row says so on the row itself, not only in the status
             * line at the bottom of the panel. The confirmation names a model
             * and the eye is on the list, so the question has to be answerable
             * without looking away from the thing it is about. Drawn ahead of
             * the "loaded" tag and returning early: an armed row cannot be the
             * loaded one (arming refuses it), so the two never compete. */
            if (i == am->del_armed) {
                cairo_set_font_size(cr, 10);
                cairo_set_source_rgba(cr, 0.90, 0.45, 0.40, 1.0);
                cairo_move_to(cr, list_x + list_w - 100, ry);
                cairo_show_text(cr, "delete?");
            } else if (i == am->loaded_idx) {
                cairo_set_font_size(cr, 10);
                if (am->switching) {
                    cairo_set_source_rgba(cr, 0.75, 0.55, 0.35, 1.0);
                    cairo_move_to(cr, list_x + list_w - 100, ry);
                    cairo_show_text(cr, "loading \xe2\x80\xa6");
                } else {
                    set_accent(cr, 1.0);
                    cairo_move_to(cr, list_x + list_w - 92, ry);
                    cairo_show_text(cr, "loaded");
                }
            }
        } else {
            /* A repo that is not here yet. The repo NAME leads and the author
             * follows in the pane, because the name is what distinguishes two
             * rows and the author rarely does. */
            const syn_aimodel_cat_t *c = &am->cat[slot - am->count - 2];
            cairo_set_font_size(cr, 12);
            cairo_move_to(cr, list_x + 8, ry);
            aimodel_fit(cr, c->name, list_w - 52);

            cairo_set_font_size(cr, 11);
            cairo_set_source_rgba(cr, 0.45, 0.55, 0.75, 1.0);
            cairo_move_to(cr, list_x + list_w - 36, ry);
            cairo_show_text(cr, "\xe2\x86\x93");     /* ↓ — this one downloads */
        }
    }

    /* The window is only part of the list: say so rather than letting the last
     * drawn row look like the last row there is. */
    if (slots > AIMODEL_ROWS) {
        cairo_set_font_size(cr, 10);
        set_ink(cr, 0.38, 1.0);
        char more[48];
        snprintf(more, sizeof(more), "%d\xe2\x80\x93%d of %d",
                 am->scroll + 1, am->scroll + visible, slots);
        cairo_move_to(cr, list_x + 8, list_top + AIMODEL_ROWS * row_h + 4);
        cairo_show_text(cr, more);
    }

    /* ── The info pane ───────────────────────────────────────────────── */
    set_ink(cr, INK_RULE, 0.4);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, pane_x - 9, 148);
    cairo_line_to(cr, pane_x - 9, ph - 62);
    cairo_stroke(cr);

    int pane_w = pw - pane_x - pad;
    aimodel_render_pane(cr, s, px, py, pw, ph, pane_x, 168, pane_w);

    /* ── The footer ──────────────────────────────────────────────────── */
    if (am->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, ph - 52);
        /* Clipped now that this carries llama.cpp's own failure text, which is
         * as long as llama felt like making it. It used to hold "loaded" and
         * "already loaded", so running past the frame was not reachable. */
        aimodel_fit(cr, am->status, pw - 36);
    }

    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 18, ph - 32);
    if (am->typing)
        cairo_show_text(cr, "type to search \xc2\xb7 Enter search \xc2\xb7 Esc cancel");
    else if (am->cat_sel >= 0)
        cairo_show_text(cr, "Up/Down select \xc2\xb7 Left/Right quantisation \xc2\xb7 "
                            "Enter download \xc2\xb7 / search \xc2\xb7 Esc close");
    else if (am->del_armed >= 0)
        /* While a confirmation is up, the only three keys that matter are the
         * three named here. Listing the usual set as well would bury the one
         * that removes several gigabytes among five that do not. */
        cairo_show_text(cr, "Delete again to confirm \xc2\xb7 Esc cancels \xc2\xb7 "
                            "any move cancels");
    else
        cairo_show_text(cr, "Up/Down select \xc2\xb7 Enter load \xc2\xb7 Del delete \xc2\xb7 "
                            "/ search \xc2\xb7 R rescan \xc2\xb7 Esc close");
    cairo_move_to(cr, 18, ph - 14);
    if (am->cat_sel >= 0)
        cairo_show_text(cr, "downloads run as a system service \xe2\x80\x94 "
                            "closing this panel does not cancel one");
    else
        cairo_show_text(cr, "switching reloads the model \xe2\x80\x94 the AI pauses while it loads");

    cairo_destroy(cr);
    set_scene_buffer(&s->aimodel_ui.text_buf, s->aimodel_ui.tree, buf);
}

/* ── Event sounds panel (sound.c) ────────────────────────── */

void synui_render_sound(syn_server_t *s)
{
    syn_sound_t *snd = &s->sound;

    if (!snd->visible) {
        wlr_scene_node_set_enabled(&s->sound_ui.tree->node, false);
        hit_clear(&snd->hit);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    const int row_h = 30, top = 66, pad = 18;
    /* Wide enough for the sample column at x=350 and the two footer lines; the
     * volume row's slider and its readout sit next to each other rather than
     * against the right edge, so they are unaffected. */
    int pw = 620;
    int ph = top + SOUND_ROW_COUNT * row_h + 96;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->sound_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->sound_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->sound_ui.tree->node);

    /* Pointer geometry: the panel rect, and a row grid matching the
     * selection highlight drawn below, so the row that lights up under the
     * cursor is the row a click lands on. */
    hit_set_panel(&snd->hit, px, py, pw, ph);
    hit_set_rows(&snd->hit, 12, top -16, pw - 24, row_h, SOUND_ROW_COUNT);

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->sound_ui.bg)
        s->sound_ui.bg = wlr_scene_rect_create(s->sound_ui.tree,
                                               pw, ph, bg_color);
    wlr_scene_rect_set_color(s->sound_ui.bg, bg_color);
    if (!s->sound_ui.accent)
        s->sound_ui.accent = wlr_scene_rect_create(s->sound_ui.tree,
                                                   pw, 2, accent);
    else
        wlr_scene_rect_set_color(s->sound_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "EVENT SOUNDS");

    /* The master switch off is the shipped default, not a fault — so it is
     * stated plainly rather than in the warning colour the filters panel uses
     * for a setting that is fighting you. */
    cairo_set_font_size(cr, 12);
    if (!snd->enabled) {
        set_ink(cr, INK_LABEL, 1.0);
        cairo_move_to(cr, 18, 50);
        cairo_show_text(cr, "silent \xc2\xb7 Space turns event sounds on");
    } else {
        set_ink(cr, INK_LABEL, 1.0);
        cairo_move_to(cr, 18, 50);
        cairo_show_text(cr, "t plays the selected sound without enabling it");
    }

    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 58);
    cairo_line_to(cr, pw - 18, 58);
    cairo_stroke(cr);

    /* With the master switch off, an event's own on/off decides nothing. */
    int dimmed = !snd->enabled;

    for (int i = 0; i < SOUND_ROW_COUNT; i++) {
        int sel = (i == snd->selected);
        int ry  = top + i * row_h;

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, 12, ry - 16, pw - 24, row_h - 4);
            cairo_fill(cr);
        }

        if (i == SOUND_ROW_EVENT) {
            set_ink(cr, INK_RULE, 0.45);
            cairo_move_to(cr, 18, ry - 20);
            cairo_line_to(cr, pw - 18, ry - 20);
            cairo_stroke(cr);
        }

        const char *label;
        /* Wide enough for a max-length theme name plus the "not a sound theme"
         * suffix; every other value here is a word or two. */
        char value[SOUND_THEME_MAX + 32];
        int is_event = 0, on = 0;

        /* The sample column: what this event will actually play. Filled only
         * for event rows; `missing` is a theme that has no sound for it. */
        char sample[SOUND_SAMPLE_MAX] = "";
        int  picked = 0, missing = 0;

        if (i == SOUND_ROW_ENABLED) {
            label = "Event sounds";
            snprintf(value, sizeof(value), "%s", snd->enabled ? "on" : "off");
            on = snd->enabled;
        } else if (i == SOUND_ROW_VOLUME) {
            label = "Volume";
            snprintf(value, sizeof(value), "%d%%", snd->volume);
        } else if (i == SOUND_ROW_THEME) {
            label = "Sound theme";
            if (sound_theme_installed(snd->theme))
                snprintf(value, sizeof(value), "%s", snd->theme);
            else
                snprintf(value, sizeof(value), "%s \xe2\x80\x94 not a sound theme",
                         snd->theme);
        } else {
            int evt = i - SOUND_ROW_EVENT;
            label = sound_event_label(evt);
            on = snd->on[evt];
            snprintf(value, sizeof(value), "%s", on ? "on" : "off");
            is_event = 1;
            picked  = snd->sample[evt][0] != '\0';
            missing = !sound_resolved_id(snd, evt, sample, sizeof(sample));
        }

        cairo_set_font_size(cr, 14);
        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
        cairo_move_to(cr, pad + 8, ry + 4);
        cairo_show_text(cr, label);

        if (i == SOUND_ROW_VOLUME) {
            draw_slider(cr, 300, ry - 9, 180, 12, snd->volume / 100.0f,
                        sel, dimmed);
            cairo_set_font_size(cr, 12);
            if (dimmed) set_ink(cr, INK_DIM, 1.0);
            else        set_accent(cr, 1.0);
            cairo_move_to(cr, 492, ry + 4);
            cairo_show_text(cr, value);
        } else {
            if ((is_event && dimmed) || !on)
                set_ink(cr, INK_DIM, 1.0);
            else
                set_accent(cr, 1.0);
            /* The theme is a name, not a state, so it is never greyed as "off" —
             * unless it is not a theme at all, which is worth shouting about. */
            if (i == SOUND_ROW_THEME) {
                if (sound_theme_installed(snd->theme))
                    set_ink(cr, INK_BODY, 1.0);
                else
                    cairo_set_source_rgba(cr, 0.85, 0.55, 0.35, 1.0);
            }
            cairo_move_to(cr, 300, ry + 4);
            cairo_show_text(cr, value);
        }

        /* Which sample the event plays, alongside its switch. Three states worth
         * telling apart, because "on and still silent" otherwise looks like a
         * broken toggle: a deliberate pick ([ ]) is brightest, the automatic
         * choice is dimmer, and a theme with no sound for this event says so
         * outright rather than showing the name of a file that is not there. */
        if (is_event && sample[0]) {
            cairo_set_font_size(cr, 12);
            if (missing)
                cairo_set_source_rgba(cr, 0.85, 0.55, 0.35, 1.0);
            else if (picked)
                cairo_set_source_rgba(cr, 0.72, 0.78, 0.92, 1.0);
            else
                set_ink(cr, INK_DIM, 1.0);

            cairo_move_to(cr, 350, ry + 4);
            cairo_show_text(cr, missing ? "not in this theme" : sample);
        }
    }

    if (snd->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, ph - 56);
        cairo_show_text(cr, snd->status);
    }

    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 18, ph - 34);
    cairo_show_text(cr, "Up/Down select \xc2\xb7 Enter toggle \xc2\xb7 Left/Right off/on \xc2\xb7 [ ] pick sound");
    cairo_move_to(cr, 18, ph - 16);
    cairo_show_text(cr, "t play \xc2\xb7 Space all sounds on/off \xc2\xb7 Esc close \xc2\xb7 more: synui-sound --help");

    cairo_destroy(cr);
    set_scene_buffer(&s->sound_ui.text_buf, s->sound_ui.tree, buf);
}

/* ── Clock & Time settings panel (clock.c) ───────────────── */

void synui_render_clock(syn_server_t *s)
{
    syn_clock_t *c = &s->clock;

    if (!c->visible) {
        wlr_scene_node_set_enabled(&s->clock_ui.tree->node, false);
        hit_clear(&c->hit);
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

    /* Pointer geometry. Only the three SETTING rows are hit-tested; the world
     * clocks below them are a readout, not a list you can put a cursor on. */
    hit_set_panel(&c->hit, px, py, pw, ph);
    hit_set_rows(&c->hit, 12, top - 18, pw - 24, row_h, CLOCK_SETTING_ROWS);

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->clock_ui.bg)
        s->clock_ui.bg = wlr_scene_rect_create(s->clock_ui.tree, pw, ph, bg_color);
    wlr_scene_rect_set_color(s->clock_ui.bg, bg_color);
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

    set_ink(cr, INK_RULE, 0.5);
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
        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
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
    set_ink(cr, INK_DIM, 0.9);
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
        hit_clear(&cal->hit);
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

    /* Pointer geometry. The calendar is the one panel whose "rows" are a 2-D
     * grid, so the row band is the full seven cells wide and clock.c divides it
     * back into columns — row_w / 7 is the cell width, which keeps the two
     * files from each carrying their own copy of the number. */
    hit_set_panel(&cal->hit, px, py, pw, ph);
    hit_set_rows(&cal->hit, grid_x, grid_y + 12, 7 * cell_w, cell_h, 6);

    float bg_color[4];
    panel_bg_color(bg_color, 0.96f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->cal_ui.bg)
        s->cal_ui.bg = wlr_scene_rect_create(s->cal_ui.tree, pw, ph, bg_color);
    wlr_scene_rect_set_color(s->cal_ui.bg, bg_color);
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
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 14, ph - 12);
    cairo_show_text(cr,
        "\xe2\x86\x90\xe2\x86\x92 day \xc2\xb7 PgUp/PgDn month \xc2\xb7 t today \xc2\xb7 Esc");

    cairo_destroy(cr);
    set_scene_buffer(&s->cal_ui.text_buf, s->cal_ui.tree, buf);
}

/* ── Control panel (ctlpanel.c) ──────────────────────────── */

/* Sidebar left, that category's rows right — the shape every desktop's settings
 * app has, and the shape the panel's own navigation assumes (Left/Right move
 * between these two columns). The x's are tuned for the 13px monospace face;
 * CTL_SHORTCUT_ROWS (synui.h) is how many shortcut rows fit between the header
 * and the footer, and the panel height is derived from it, so the two cannot
 * disagree about how much room there is. */
#define CTL_W          860
#define CTL_ROW_H       26
#define CTL_TOP         92
#define CTL_FOOTER      64
#define CTL_SIDEBAR    200   /* width of the category column */
#define CTL_COL_RIGHT  226   /* x of the row pane */
#define CTL_SETTING_V  (CTL_W - 30)   /* right edge of the value column */

void synui_render_ctlpanel(syn_server_t *s)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;

    if (!cp->visible) {
        wlr_scene_node_set_enabled(&s->ctlpanel_ui.tree->node, false);
        hit_clear(&cp->hit);
        hit_clear(&cp->hit_items);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    /* The taller of the two columns sets the height. The sidebar is a fixed
     * CTL_CAT_COUNT and no category has more rows than that; the shortcuts list
     * is the long one, and it scrolls within its window. */
    int body_rows = CTL_CAT_COUNT > CTL_SHORTCUT_ROWS
                  ? CTL_CAT_COUNT : CTL_SHORTCUT_ROWS;
    int pw = CTL_W;
    int ph = CTL_TOP + body_rows * CTL_ROW_H + CTL_FOOTER;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->ctlpanel_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->ctlpanel_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->ctlpanel_ui.tree->node);

    /* Pointer geometry. The sidebar's grid is fixed; the row pane's row count
     * depends on the category, so it is filled in down in the branch that draws
     * it — the same place the count is computed for the draw. The x/width of
     * each grid are the highlight rectangles below, so what lights up under the
     * pointer is exactly what a click lands on. */
    hit_set_panel(&cp->hit, px, py, pw, ph);
    hit_set_rows(&cp->hit, 10, CTL_TOP - 16, CTL_SIDEBAR - 22,
                 CTL_ROW_H, CTL_CAT_COUNT);
    hit_set_panel(&cp->hit_items, px, py, pw, ph);

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->ctlpanel_ui.bg)
        s->ctlpanel_ui.bg = wlr_scene_rect_create(s->ctlpanel_ui.tree,
                                                  pw, ph, bg_color);
    wlr_scene_rect_set_color(s->ctlpanel_ui.bg, bg_color);
    if (!s->ctlpanel_ui.accent)
        s->ctlpanel_ui.accent = wlr_scene_rect_create(s->ctlpanel_ui.tree,
                                                      pw, 2, accent);
        else
            wlr_scene_rect_set_color(s->ctlpanel_ui.accent, accent);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    int cats_focused = (cp->focus == CTL_FOCUS_CATS);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, 18, 30);
    cairo_show_text(cr, "CONTROL PANEL");

    /* Breadcrumb. Which category you are in is otherwise only visible as a
     * highlight in the sidebar, which is exactly the thing that goes quiet when
     * focus moves into the rows. */
    cairo_set_font_size(cr, 13);
    set_ink(cr, INK_LABEL, 1.0);
    cairo_move_to(cr, CTL_COL_RIGHT, 30);
    cairo_show_text(cr, "\xe2\x80\xba  ");
    if (cp->searching) {
        /* The breadcrumb is where you are, and while searching you are not in a
         * category — saying "Appearance" over a list of rows from six of them
         * would be the panel's only outright lie. */
        set_ink(cr, INK_TITLE, 1.0);
        cairo_show_text(cr, "Search");
    } else {
        set_ink(cr, INK_TITLE, 1.0);
        cairo_show_text(cr, ctlpanel_cat_name(cp->cat));

        /* …and which section of it, when the cursor is in one. This is where a
         * section's name lives; the pane only rules between them. */
        int selrow = ctlpanel_selected_row(s);
        const char *sect = selrow >= 0 ? ctlpanel_row_section(selrow) : NULL;
        if (sect) {
            set_ink(cr, INK_LABEL, 1.0);
            cairo_show_text(cr, "  \xe2\x80\xba  ");
            set_ink(cr, INK_TITLE, 1.0);
            cairo_show_text(cr, sect);
        }
    }

    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, 44);
    cairo_line_to(cr, pw - 18, 44);
    cairo_stroke(cr);

    /* Column headings */
    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 1.0);
    cairo_move_to(cr, 18, 70);
    cairo_show_text(cr, "CATEGORIES");
    cairo_move_to(cr, CTL_COL_RIGHT, 70);
    if (cp->searching) {
        /* The box IS the column heading while it is open: it sits exactly where
         * "SETTINGS" would, so nothing moves down the pane when you open it. */
        cairo_show_text(cr, "FIND  ");
        set_accent(cr, 1.0);
        cairo_set_font_size(cr, 13);
        char box[80];
        snprintf(box, sizeof(box), "%s_", cp->search);
        cairo_show_text(cr, box);
    } else {
        cairo_show_text(cr, cp->cat == CTL_CAT_SHORTCUTS ? "KEYBOARD SHORTCUTS"
                                                         : "SETTINGS");
    }

    /* Divider between the columns */
    set_ink(cr, INK_RULE, 0.35);
    cairo_move_to(cr, CTL_SIDEBAR, 56);
    cairo_line_to(cr, CTL_SIDEBAR, ph - CTL_FOOTER + 8);
    cairo_stroke(cr);

    /* ── Sidebar ── */
    for (int i = 0; i < CTL_CAT_COUNT; i++) {
        int ry  = CTL_TOP + i * CTL_ROW_H;
        int sel = (i == cp->cat);

        /* The current category stays marked while focus is in the rows, just
         * dimmer: it is where Esc goes back to, so losing it entirely would
         * leave the pane belonging to nothing. */
        if (sel) {
            set_accent(cr, cats_focused ? 0.35 : 0.14);
            cairo_rectangle(cr, 10, ry - 16, CTL_SIDEBAR - 22, CTL_ROW_H - 4);
            cairo_fill(cr);
        }

        cairo_set_font_size(cr, 14);
        if (sel) cairo_set_source_rgba(cr, 0.95, 1.0, 0.99, 1.0);
        else     set_ink(cr, INK_MUTED, 1.0);
        cairo_move_to(cr, 18, ry);
        cairo_show_text(cr, ctlpanel_cat_name(i));

        /* A "›" on the selected row, so the sidebar reads as a menu that opens
         * into the pane rather than a list of labels. */
        if (sel) {
            set_accent(cr, cats_focused ? 1.0 : 0.5);
            cairo_set_font_size(cr, 13);
            draw_right(cr, CTL_SIDEBAR - 16, ry, "\xe2\x80\xba");
        }
    }

    /* ── Row pane ── */
    if (cp->cat == CTL_CAT_SHORTCUTS) {
        /* Not settings: the live bind table, read-only, one scrolling list. */
        syn_ctl_shortcut_t sc[CTL_SHORTCUTS_MAX];
        int n = ctlpanel_shortcuts(s, sc, CTL_SHORTCUTS_MAX);

        int first = cp->scroll;
        if (first > n - CTL_SHORTCUT_ROWS) first = n - CTL_SHORTCUT_ROWS;
        if (first < 0) first = 0;

        cairo_set_font_size(cr, 13);
        for (int i = 0; i < CTL_SHORTCUT_ROWS && first + i < n; i++) {
            int ry = CTL_TOP + i * CTL_ROW_H;

            set_accent(cr, 0.95);
            cairo_move_to(cr, CTL_COL_RIGHT, ry);
            cairo_show_text(cr, sc[first + i].combo);

            /* +200 rather than the +172 the old narrow column used: the widest
             * combo in the default binds is "XF86MonBrightnessDown", which ran
             * straight into the description at 172. The pane is wide enough now
             * that there is no reason to keep the collision. */
            set_ink(cr, INK_BODY, 1.0);
            draw_clipped(cr, CTL_COL_RIGHT + 200, ry,
                         CTL_SETTING_V - (CTL_COL_RIGHT + 200),
                         sc[first + i].desc);
        }

        /* Say so when the list runs off the window, rather than silently
         * truncating it — a shortcut you cannot see is a shortcut you do not
         * have. */
        if (n > CTL_SHORTCUT_ROWS) {
            cairo_set_font_size(cr, 11);
            set_ink(cr, INK_DIM, 0.9);
            char more[64];
            snprintf(more, sizeof(more), "%d\xe2\x80\x93%d of %d \xc2\xb7 PgUp/PgDn",
                     first + 1,
                     first + CTL_SHORTCUT_ROWS < n ? first + CTL_SHORTCUT_ROWS : n,
                     n);
            cairo_move_to(cr, CTL_COL_RIGHT,
                          CTL_TOP + CTL_SHORTCUT_ROWS * CTL_ROW_H + 6);
            cairo_show_text(cr, more);
        }
    } else {
        int rows[CTL_CAT_ITEMS_MAX];
        int nrows = ctlpanel_visible_rows(s, rows, CTL_CAT_ITEMS_MAX);
        int cur   = ctlpanel_selected_row(s);

        /* The window into a category that no longer fits. Clamped here as well
         * as in ctlpanel.c because this is the one place that knows how many
         * rows were actually drawn, and the pointer grid below has to describe
         * exactly those. */
        int first = cp->row_scroll;
        if (first > nrows - CTL_ROW_ROWS) first = nrows - CTL_ROW_ROWS;
        if (first < 0) first = 0;

        int drawn = nrows - first;
        if (drawn > CTL_ROW_ROWS) drawn = CTL_ROW_ROWS;

        /* The pane's rows are clickable; the shortcuts list above is not (it is
         * a read-only view of the bind table), so it leaves the grid empty and
         * only the wheel does anything over it. */
        hit_set_rows(&cp->hit_items, CTL_COL_RIGHT - 12, CTL_TOP - 16,
                     (CTL_SETTING_V + 12) - (CTL_COL_RIGHT - 12),
                     CTL_ROW_H, drawn);

        for (int i = 0; i < drawn; i++) {
            int ry  = CTL_TOP + i * CTL_ROW_H;
            int row = rows[first + i];
            int sel = (row == cur);

            /* A rule above the row that opens a section.
             *
             * The section's NAME is in the breadcrumb rather than here: the row
             * pitch is 26px and the highlight takes 22 of them, so there is no
             * line to set a heading on without either growing every row or
             * spending a whole row on text. A rule costs the 2px that are
             * actually free and still does the one job that matters at forty
             * rows — saying where one group of settings ends and the next
             * begins. Not drawn on the very first drawn row: a rule hard
             * against the column heading reads as a border, not a divider.
             *
             * Suppressed while searching, where results come from every
             * category and the groupings they were pulled out of no longer
             * describe what is on screen. */
            if (!cp->searching && i > 0 && ctlpanel_row_starts_section(row)) {
                set_ink(cr, INK_RULE, 0.30);
                cairo_set_line_width(cr, 1);
                cairo_move_to(cr, CTL_COL_RIGHT - 12, ry - 19);
                cairo_line_to(cr, CTL_SETTING_V + 12, ry - 19);
                cairo_stroke(cr);
            }

            if (sel) {
                set_accent(cr, cats_focused ? 0.14 : 0.35);
                cairo_rectangle(cr, CTL_COL_RIGHT - 12, ry - 16,
                                (CTL_SETTING_V + 12) - (CTL_COL_RIGHT - 12),
                                CTL_ROW_H - 4);
                cairo_fill(cr);
            }

            cairo_set_font_size(cr, 14);
            set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
            cairo_move_to(cr, CTL_COL_RIGHT, ry);
            cairo_show_text(cr, ctlpanel_row_label(row));

            /* In a search result, say which category the row came from. Without
             * it the results are a flat list of names with no way to tell a
             * "Blur" in Windows from one somewhere else — and no way to learn
             * where a setting lives for next time, which is how a search
             * teaches the menu rather than replacing it. */
            if (cp->searching) {
                int rcat = ctlpanel_row_cat(row);
                if (rcat >= 0) {
                    cairo_set_font_size(cr, 11);
                    set_ink(cr, INK_DIM, 0.8);
                    cairo_show_text(cr, "   ");
                    cairo_show_text(cr, ctlpanel_cat_name(rcat));
                }
            }

            /* A dot on any row that is no longer at its default. The panel now
             * has a hundred rows and settings.state only records the ones that
             * were changed, so this is the only way to see at a glance what
             * this desktop has actually been made to differ on — and what the
             * Delete key would have something to undo on. */
            if (!ctlpanel_row_is_default(s, row)) {
                set_accent(cr, 0.85);
                cairo_arc(cr, CTL_COL_RIGHT - 7, ry - 4, 2.0, 0, 2 * 3.14159265);
                cairo_fill(cr);
            }

            /* Wide enough for a GGUF filename: the AI-model row's value is one,
             * and a truncated model name is a name you cannot act on. */
            char value[64];
            ctlpanel_row_value(s, row, value, sizeof(value));

            /* A row that opens something is marked as such, so the pane says
             * which rows flip here and which lead somewhere — the distinction
             * the old trailing "…" in the labels used to carry. */
            syn_ctl_kind_t kind = ctlpanel_row_kind(row);
            int leads_away = (kind == CTL_KIND_PANEL || kind == CTL_KIND_LAUNCH ||
                              kind == CTL_KIND_ACTION);
            double vx = CTL_SETTING_V;

            if (leads_away) {
                cairo_set_font_size(cr, 13);
                set_ink(cr, INK_LABEL, 1.0);
                draw_right(cr, CTL_SETTING_V, ry, "\xe2\x80\xba");
                vx -= 18;
            }

            /* A choice row wears its chevrons around the value instead: they
             * say the value itself is what Left/Right move through, where the
             * single trailing one says the row leads somewhere else. It opens a
             * panel too, but that is what Enter is for and the footer says so —
             * both marks at once would read as two different affordances.
             *
             * "‹"/"›" (U+2039/203A) and not "◀"/"▶" or the arrows: the 13px
             * face these panels draw with has no U+2192 and renders it as a
             * garbage glyph. Checked, because it has cost a build round before. */
            int choice = (kind == CTL_KIND_CHOICE && value[0]);
            if (choice) {
                cairo_set_font_size(cr, 13);
                set_ink(cr, INK_LABEL, 1.0);
                draw_right(cr, CTL_SETTING_V, ry, "\xe2\x80\xba");
                vx -= 14;
            }

            if (value[0]) {
                /* "on" reads as live, everything else (off/n/a) as inert. */
                if (strcmp(value, "off") == 0 || strcmp(value, "n/a") == 0 ||
                    strcmp(value, "none") == 0)
                    set_ink(cr, INK_DIM, 1.0);
                else
                    set_accent(cr, 1.0);
                cairo_set_font_size(cr, 13);
                draw_right(cr, vx, ry, value);

                if (choice) {
                    /* The opening chevron sits off the value's own width, so it
                     * tracks a name that changes length as the row is cycled. */
                    cairo_text_extents_t ext;
                    cairo_text_extents(cr, value, &ext);
                    set_ink(cr, INK_LABEL, 1.0);
                    draw_right(cr, vx - ext.width - 6, ry, "\xe2\x80\xb9");
                }

                /* A value row wears chevrons too, for the same reason the
                 * choice row does: they are what say the number itself is what
                 * Left/Right move. Drawn only on the SELECTED row — a hundred
                 * rows each carrying a pair is noise, and the affordance only
                 * matters where the keys would land. */
                if (kind == CTL_KIND_VALUE && sel) {
                    cairo_text_extents_t ext;
                    cairo_text_extents(cr, value, &ext);
                    set_ink(cr, INK_LABEL, 1.0);
                    draw_right(cr, vx + 12, ry, "\xe2\x80\xba");
                    draw_right(cr, vx - ext.width - 6, ry, "\xe2\x80\xb9");
                }
            }
        }

        /* Where in the category you are, when it does not fit. The shortcuts
         * list has said this since it was written; a forty-row Windows needs it
         * for exactly the same reason — a setting you cannot see is one you do
         * not know you have. */
        if (nrows > CTL_ROW_ROWS) {
            cairo_set_font_size(cr, 11);
            set_ink(cr, INK_DIM, 0.9);
            char more[80];
            snprintf(more, sizeof(more), "%d\xe2\x80\x93%d of %d \xc2\xb7 PgUp/PgDn",
                     first + 1, first + drawn, nrows);
            cairo_move_to(cr, CTL_COL_RIGHT, CTL_TOP + CTL_ROW_ROWS * CTL_ROW_H + 6);
            cairo_show_text(cr, more);
        } else if (cp->searching && nrows == 0) {
            /* An empty result is a real answer and has to look like one, or it
             * reads as the panel having broken. */
            cairo_set_font_size(cr, 13);
            set_ink(cr, INK_DIM, 0.9);
            cairo_move_to(cr, CTL_COL_RIGHT, CTL_TOP);
            cairo_show_text(cr, "No setting matches that.");
        }
    }

    /* ── Footer ── */
    /* The selected row's one-line explanation, above the status line. Most of
     * these settings are named in two or three words and several of them are
     * genuinely obscure — "Blur halo", "Crop client shadows" — so the sentence
     * is what makes the row safe to touch without reading the source. */
    {
        int selrow = ctlpanel_selected_row(s);
        const char *help = (selrow >= 0 && cp->focus == CTL_FOCUS_ITEMS)
                         ? ctlpanel_row_help(selrow) : NULL;
        if (help && !cp->status[0]) {
            cairo_set_font_size(cr, 12);
            set_ink(cr, INK_LABEL, 0.95);
            cairo_move_to(cr, 18, ph - 38);
            cairo_show_text(cr, help);
        }
    }

    if (cp->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, ph - 38);
        cairo_show_text(cr, cp->status);
    }

    /* The keys on offer are not the same in the two columns, and a hint listing
     * the ones that do nothing where you are is worse than none. */
    /* Spelled out rather than drawn with arrow glyphs: the 13px monospace face
     * the panels use has no U+2192, and a missing glyph in a key hint is a hint
     * that reads as a typo. "›" (U+203A), used above, it does have. */
    const char *hint;
    if (cp->searching)
        hint = "Type to filter \xc2\xb7 Up/Down select \xc2\xb7 Del reset \xc2\xb7 Esc back";
    else if (cats_focused)
        hint = "Up/Down category \xc2\xb7 Enter or Right opens \xc2\xb7 / find \xc2\xb7 Esc close";
    else if (cp->cat == CTL_CAT_SHORTCUTS)
        hint = "Up/Down \xc2\xb7 PgUp/PgDn scroll \xc2\xb7 Left or Esc goes back";
    else if (ctlpanel_selected_row(s) >= 0 &&
             ctlpanel_row_kind(ctlpanel_selected_row(s)) == CTL_KIND_VALUE)
        /* Left/Right are the value here, so the usual "Left goes back" would be
         * naming a key that does something else entirely. Tab is the way out,
         * and Del is the way back to the default — worth saying on exactly the
         * rows where there is a default to go back to. */
        hint = "Left/Right adjust \xc2\xb7 Del default \xc2\xb7 / find \xc2\xb7 Tab column \xc2\xb7 Esc back";
    else if (ctlpanel_selected_row(s) >= 0 &&
             ctlpanel_row_kind(ctlpanel_selected_row(s)) == CTL_KIND_CHOICE)
        /* On a choice row Left/Right are the choice, so the usual hint would be
         * naming a key that does something else entirely. Tab is the way out of
         * the column here, and it is worth saying so on the one row where Left
         * is not. */
        hint = "Left/Right switch \xc2\xb7 Enter details \xc2\xb7 Tab column \xc2\xb7 Esc back";
    else
        hint = "Up/Down select \xc2\xb7 Enter activate \xc2\xb7 / find \xc2\xb7 Left or Esc back";

    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, 18, ph - 18);
    cairo_show_text(cr, hint);

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
    case SYN_THEME_SYNAPSE:    return "The neon night-drive default \xc2\xb7 apps dark";
    case SYN_THEME_DARK:       return "Flat modern dark \xc2\xb7 apps + Dolphin + Firefox dark";
    case SYN_THEME_WINXP:      return "Luna blue, gradient captions \xc2\xb7 apps beige";
    case SYN_THEME_WIN95:      return "Silver 3D bevels, navy titles \xc2\xb7 apps grey";
    case SYN_THEME_CATPPUCCIN: return "Mocha \xc2\xb7 mauve on soft black \xc2\xb7 glassy";
    case SYN_THEME_GRUVBOX:    return "Retro warm \xc2\xb7 orange on brown \xc2\xb7 glassy";
    case SYN_THEME_TOKYONIGHT: return "Storm \xc2\xb7 blue + purple on navy \xc2\xb7 glassy";
    case SYN_THEME_NORD:       return "Arctic \xc2\xb7 frost blue on slate \xc2\xb7 glassy";
    case SYN_THEME_DRACULA:    return "Purple + pink on charcoal \xc2\xb7 glassy";
    case SYN_THEME_BUBBLEGUM:  return "Pastel pink, hot-pink titles \xc2\xb7 apps light";
    default:                   return "";
    }
}

void synui_render_thememgr(syn_server_t *s)
{
    syn_thememgr_t *tm = &s->thememgr;

    if (!tm->visible) {
        wlr_scene_node_set_enabled(&s->thememgr_ui.tree->node, false);
        hit_clear(&tm->hit);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    /* The list scrolls. With ten themes the full list is ~700px, which fits a
     * 1080p screen but not a 1024x768 one (the ISO's default, and any VM), and a
     * panel taller than its output silently loses its footer — the slider and the
     * keybind help. So: show as many rows as fit with a margin, and window them
     * around the selection. Stateless on purpose — deriving `first` from the
     * selection each render means there is no scroll position to keep in sync. */
    int rows = SYN_THEME_COUNT;
    int avail = ob.height - 80 - THM_TOP - THM_FOOTER;
    if (avail < THM_ROW_H * 3) avail = THM_ROW_H * 3;   /* never below 3 rows */
    if (rows > avail / THM_ROW_H) rows = avail / THM_ROW_H;

    int first = tm->selected - rows / 2;
    if (first > SYN_THEME_COUNT - rows) first = SYN_THEME_COUNT - rows;
    if (first < 0) first = 0;

    int pw = THM_W;
    int ph = THM_TOP + rows * THM_ROW_H + THM_FOOTER;
    int px = ob.x + (ob.width - pw) / 2, py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->thememgr_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->thememgr_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->thememgr_ui.tree->node);

    /* Pointer geometry. `first` is derived from the selection here and stored
     * nowhere else, so the hit test would have no way to recompute it — this is
     * the case syn_hit_t::first exists for. */
    hit_set_panel(&tm->hit, px, py, pw, ph);
    hit_set_rows(&tm->hit, THM_PAD - 10, THM_TOP - 26,
                 pw - 2 * (THM_PAD - 10), THM_ROW_H, rows);
    hit_set_first(&tm->hit, first);

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->thememgr_ui.bg)
        s->thememgr_ui.bg = wlr_scene_rect_create(s->thememgr_ui.tree,
                                                  pw, ph, bg_color);
    wlr_scene_rect_set_color(s->thememgr_ui.bg, bg_color);
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

    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, THM_PAD, 50);
    cairo_line_to(cr, pw - THM_PAD, 50);
    cairo_stroke(cr);

    for (int i = first; i < first + rows; i++) {
        int ry = THM_TOP + (i - first) * THM_ROW_H;
        int sel    = (i == tm->selected);
        int active = (i == s->config.theme);

        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, THM_PAD - 10, ry - 26, pw - 2 * (THM_PAD - 10),
                            THM_ROW_H - 6);
            cairo_fill(cr);
        }

        /* Swatch: caption colour on the left, focus accent on the right, framed.
         * Two halves because one colour cannot separate these themes — every
         * rice's caption is the same near-black, and their accent is the whole
         * point of picking one. */
        float cap[4], acc[4];
        theme_preview_colors((syn_theme_t)i, cap, acc);
        cairo_set_source_rgba(cr, cap[0], cap[1], cap[2], 1.0);
        cairo_rectangle(cr, THM_PAD, ry - 24, THM_SWATCH / 2.0, THM_SWATCH);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 1.0);
        cairo_rectangle(cr, THM_PAD + THM_SWATCH / 2.0, ry - 24,
                        THM_SWATCH / 2.0, THM_SWATCH);
        cairo_fill(cr);
        set_ink(cr, 0.49, 0.8);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, THM_PAD, ry - 24, THM_SWATCH, THM_SWATCH);
        cairo_stroke(cr);

        double tx = THM_PAD + THM_SWATCH + 16;

        cairo_set_font_size(cr, 15);
        set_ink(cr, sel ? INK_STRONG : INK_TITLE, 1.0);
        cairo_move_to(cr, tx, ry - 6);
        cairo_show_text(cr, theme_name((syn_theme_t)i));

        /* "active" marker: the theme currently in force (not just highlighted). */
        if (active) {
            set_accent(cr, 1.0);
            draw_right(cr, pw - THM_PAD, ry - 6, "\xe2\x97\x8f active");
        }

        cairo_set_font_size(cr, 11);
        set_ink(cr, INK_LABEL, 1.0);
        cairo_move_to(cr, tx, ry + 12);
        cairo_show_text(cr, thememgr_blurb((syn_theme_t)i));
    }

    /* "there is more above/below" — without these a windowed list reads as the
     * whole list, and the themes off-screen may as well not exist. */
    if (rows < SYN_THEME_COUNT) {
        cairo_set_font_size(cr, 11);
        set_ink(cr, INK_LABEL, 0.9);
        if (first > 0)
            draw_right(cr, pw - THM_PAD, THM_TOP - 32, "\xe2\x96\xb2");
        if (first + rows < SYN_THEME_COUNT)
            draw_right(cr, pw - THM_PAD, THM_TOP + rows * THM_ROW_H - 8,
                       "\xe2\x96\xbc");
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
        else    set_ink(cr, INK_LABEL, 0.9);
        cairo_move_to(cr, tx0, sy - 8);
        cairo_show_text(cr, lbl);

        set_ink(cr, 0.18, 0.95);
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
    set_ink(cr, INK_DIM, 0.9);
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
        hit_clear(&c->hit);
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

    /* Pointer geometry. The scroll clamp below decides which entry the top row
     * is, so hit_set_first() is called down there rather than here. */
    hit_set_panel(&c->hit, px, py, pw, ph);
    hit_set_rows(&c->hit, CLIP_PAD - 8, CLIP_TOP - 15,
                 pw - 2 * (CLIP_PAD - 8), CLIP_ROW_H,
                 c->count < CLIP_ROWS ? c->count : CLIP_ROWS);

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->clip_ui.bg)
        s->clip_ui.bg = wlr_scene_rect_create(s->clip_ui.tree, pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->clip_ui.bg, pw, ph);   /* height tracks the list */
    wlr_scene_rect_set_color(s->clip_ui.bg, bg_color);
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

    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, CLIP_PAD, 44);
    cairo_line_to(cr, pw - CLIP_PAD, 44);
    cairo_stroke(cr);

    int first = c->scroll;
    if (first > c->count - CLIP_ROWS) first = c->count - CLIP_ROWS;
    if (first < 0) first = 0;
    hit_set_first(&c->hit, first);

    if (c->count == 0) {
        cairo_set_font_size(cr, 13);
        set_ink(cr, INK_DIM, 1.0);
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
        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
        draw_clipped(cr, CLIP_PAD, ry, pw - 2 * CLIP_PAD - 40, line);
    }

    cairo_set_font_size(cr, 11);
    set_ink(cr, INK_DIM, 0.9);
    if (c->count > CLIP_ROWS) {
        char more[64];
        snprintf(more, sizeof(more), "%d\xe2\x80\x93%d of %d", first + 1,
                 first + CLIP_ROWS < c->count ? first + CLIP_ROWS : c->count,
                 c->count);
        cairo_move_to(cr, CLIP_PAD, ph - 30);
        cairo_show_text(cr, more);
    }

    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 0.9);
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
        set_ink(cr, 0.00, 0.96);
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
        set_ink(cr, INK_STRONG, 1.0);
        draw_clipped(cr, NOTIF_PAD, y + 40, box.width - 2 * NOTIF_PAD, t->summary);

        if (t->body[0]) {
            cairo_set_font_size(cr, 12);
            set_ink(cr, INK_BODY, 1.0);
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

    float bg_color[4];
    panel_bg_color(bg_color, 0.94f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->bt_ui.bg)
        s->bt_ui.bg = wlr_scene_rect_create(s->bt_ui.tree, pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->bt_ui.bg, pw, ph);   /* height tracks the list */
    wlr_scene_rect_set_color(s->bt_ui.bg, bg_color);
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

    set_ink(cr, INK_RULE, 0.5);
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
        set_ink(cr, INK_STRONG, 1.0);
        cairo_move_to(cr, BT_PAD, BT_TOP + 6);
        cairo_show_text(cr, b->ask_dev[0] ? b->ask_dev : "A device");

        char l2[128];
        cairo_set_font_size(cr, 13);
        set_ink(cr, INK_BODY, 1.0);
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
        set_ink(cr, INK_DIM, 1.0);
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
        set_ink(cr, sel ? INK_STRONG : INK_BODY, 1.0);
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
            set_ink(cr, INK_DIM, 1.0);
            cairo_move_to(cr, 372, ry);
            cairo_show_text(cr, "trusted");
        }

        char right[32] = {0};
        if (d->battery >= 0)      snprintf(right, sizeof(right), "%d%%", d->battery);
        else if (d->has_rssi)     snprintf(right, sizeof(right), "%d dBm", d->rssi);
        if (right[0]) {
            cairo_set_font_size(cr, 12);
            set_ink(cr, INK_DIM, 1.0);
            draw_right(cr, pw - BT_PAD, ry, right);
        }
    }

    cairo_set_font_size(cr, 11);
    set_ink(cr, INK_DIM, 0.9);
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
    set_ink(cr, INK_DIM, 0.9);
    cairo_move_to(cr, BT_PAD, ph - 14);
    cairo_show_text(cr, b->show_all
        ? "Enter connect \xc2\xb7 p pair \xc2\xb7 t trust \xc2\xb7 r forget \xc2\xb7 "
          "s scan \xc2\xb7 o radio \xc2\xb7 a fewer \xc2\xb7 Esc"
        : "Enter connect \xc2\xb7 p pair \xc2\xb7 t trust \xc2\xb7 r forget \xc2\xb7 "
          "s scan \xc2\xb7 o radio \xc2\xb7 a all \xc2\xb7 Esc");

    cairo_destroy(cr);
    set_scene_buffer(&s->bt_ui.text_buf, s->bt_ui.tree, buf);
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
    set_ink(cr, 0.11, 1.0);
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
        hit_clear(&t->hit);
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

    /* Pointer geometry: the process table. */
    hit_set_panel(&t->hit, px, py, pw, ph);
    hit_set_rows(&t->hit, 12, table_top, pw - 24, TM_ROW_H,
                 t->n - t->scroll < TASKMGR_ROWS ? t->n - t->scroll
                                                 : TASKMGR_ROWS);
    hit_set_first(&t->hit, t->scroll);

    /* Denser than the other panels, so it is more opaque than their 0.94: at
     * that alpha the wallpaper (the Matrix one animates) and the welcome
     * screen's menu text ghost straight through the rows and the small type
     * stops being readable. */
    float bg_color[4];
    panel_bg_color(bg_color, 0.985f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->taskmgr_ui.bg)
        s->taskmgr_ui.bg = wlr_scene_rect_create(s->taskmgr_ui.tree,
                                                 pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->taskmgr_ui.bg, pw, ph);
    wlr_scene_rect_set_color(s->taskmgr_ui.bg, bg_color);
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
    set_ink(cr, INK_DIM, 1.0);
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

    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 18, table_top - 26);
    cairo_line_to(cr, pw - 18, table_top - 26);
    cairo_stroke(cr);

    /* Column headings */
    cairo_set_font_size(cr, 11);
    set_ink(cr, INK_DIM, 1.0);
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

        set_ink(cr, sel ? INK_STRONG : INK_TITLE, 1.0);
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
    set_ink(cr, INK_DIM, 0.9);
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
        hit_clear(&n->hit);
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

    /* Pointer geometry: the headline rows. */
    hit_set_panel(&n->hit, px, py, pw, ph);
    hit_set_rows(&n->hit, 12, NW_TOP, pw - 24, NW_ROW_H,
                 n->n_view - n->scroll < NEWS_ROWS ? n->n_view - n->scroll
                                                   : NEWS_ROWS);
    hit_set_first(&n->hit, n->scroll);

    /* As opaque as the task manager, and for the same reason: this is a dense
     * table of small type, and at 0.94 the (animated) wallpaper reads through
     * the headlines. */
    float bg_color[4];
    panel_bg_color(bg_color, 0.985f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->news_ui.bg)
        s->news_ui.bg = wlr_scene_rect_create(s->news_ui.tree, pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->news_ui.bg, pw, ph);
    wlr_scene_rect_set_color(s->news_ui.bg, bg_color);
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
    set_ink(cr, INK_DIM, 1.0);
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
        set_ink(cr, INK_RULE, 0.5);
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
        set_ink(cr, INK_DIM, 0.9);
        draw_right(cr, pw - 18, ph - 44, pos);
    }

    if (n->status[0]) {
        cairo_set_font_size(cr, 12);
        set_accent(cr, 0.9);
        cairo_move_to(cr, 18, ph - 44);
        cairo_show_text(cr, n->status);
    }

    cairo_set_font_size(cr, 12);
    set_ink(cr, INK_DIM, 0.9);
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

    float bg_color[4];
    panel_bg_color(bg_color, 0.96f);
    if (!s->dockmenu_ui.bg)
        s->dockmenu_ui.bg = wlr_scene_rect_create(s->dockmenu_ui.tree,
                                                  pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->dockmenu_ui.bg, pw, ph);
    wlr_scene_rect_set_color(s->dockmenu_ui.bg, bg_color);

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
        set_ink(cr, sel ? INK_STRONG : INK_TITLE, 1.0);
        cairo_move_to(cr, 14, iy + 20);
        cairo_show_text(cr, dockact_label(s->dockmenu.actions[i]));
    }

    cairo_destroy(cr);
    set_scene_buffer(&s->dockmenu_ui.text_buf, s->dockmenu_ui.tree, buf);
}

void synui_render_deskmenu(syn_server_t *s)
{
    if (!s->deskmenu.visible) {
        wlr_scene_node_set_enabled(&s->deskmenu_ui.tree->node, false);
        return;
    }

    int pw = s->deskmenu.w, ph = s->deskmenu.h;

    wlr_scene_node_set_position(&s->deskmenu_ui.tree->node,
                                s->deskmenu.x, s->deskmenu.y);
    wlr_scene_node_set_enabled(&s->deskmenu_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->deskmenu_ui.tree->node);

    float bg_color[4];
    panel_bg_color(bg_color, 0.96f);
    if (!s->deskmenu_ui.bg)
        s->deskmenu_ui.bg = wlr_scene_rect_create(s->deskmenu_ui.tree,
                                                  pw, ph, bg_color);
    else
        wlr_scene_rect_set_size(s->deskmenu_ui.bg, pw, ph);
    wlr_scene_rect_set_color(s->deskmenu_ui.bg, bg_color);

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    set_accent(cr, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, 0.5, 0.5, pw - 1, ph - 1);
    cairo_stroke(cr);

    for (int i = 0; i < s->deskmenu.action_count; i++) {
        int iy = deskmenu_row_top(s, i);
        int rh = deskmenu_row_height(s, i);

        if (s->deskmenu.actions[i] == SYN_DESKACT_SEP) {
            set_accent(cr, 0.22);
            cairo_set_line_width(cr, 1);
            cairo_move_to(cr, 10, iy + rh / 2.0 + 0.5);
            cairo_line_to(cr, pw - 10, iy + rh / 2.0 + 0.5);
            cairo_stroke(cr);
            continue;
        }

        int sel = (i == s->deskmenu.selected);
        if (sel) {
            set_accent(cr, 0.35);
            cairo_rectangle(cr, 3, iy, pw - 6, rh);
            cairo_fill(cr);
        }
        cairo_set_font_size(cr, 14);
        set_ink(cr, sel ? INK_STRONG : INK_TITLE, 1.0);
        cairo_move_to(cr, 14, iy + 20);
        cairo_show_text(cr, deskact_label(s->deskmenu.actions[i]));

        /* A checkmark shows the state a settings row is already in — icons on,
         * or the arrange mode in force — so the row reads as a setting rather
         * than an action that might do it twice. */
        if (deskmenu_row_checked(s, i)) {
            cairo_move_to(cr, pw - 24, iy + 20);
            cairo_show_text(cr, "✓");
        }
    }

    cairo_destroy(cr);
    set_scene_buffer(&s->deskmenu_ui.text_buf, s->deskmenu_ui.tree, buf);
}

/* Well-formed UTF-8? Desktop labels come from filenames, which are arbitrary
 * bytes on Linux — the one string reaching cairo that no parser vetted. */
static int deskicon_utf8_ok(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n;
        if      (*p < 0x80)           n = 0;
        else if ((*p & 0xE0) == 0xC0) n = 1;
        else if ((*p & 0xF0) == 0xE0) n = 2;
        else if ((*p & 0xF8) == 0xF0) n = 3;
        else return 0;
        p++;
        for (int i = 0; i < n; i++, p++)
            if ((*p & 0xC0) != 0x80) return 0;
    }
    return 1;
}

/* One icon cell, into the icon-field buffer. `area` is the usable box the
 * buffer covers; cell coords are absolute, so they are offset by its origin. */
static void deskicon_draw_cell(cairo_t *cr, const syn_deskicon_t *ic,
                               const struct wlr_box *area, int sel)
{
    double cx = ic->x - area->x, cy = ic->y - area->y;

    if (sel) {
        set_accent(cr, 0.30);
        cairo_rectangle(cr, cx + 2, cy + 2,
                        SYN_DESKICON_W - 4, SYN_DESKICON_H - 4);
        cairo_fill(cr);
    }

    double ix = cx + (SYN_DESKICON_W - 48) / 2.0, iy = cy + 8;
    if (ic->icon_surface) {
        int iw = cairo_image_surface_get_width(ic->icon_surface);
        int ih = cairo_image_surface_get_height(ic->icon_surface);
        if (iw > 0 && ih > 0) {
            cairo_save(cr);
            cairo_translate(cr, ix, iy);
            cairo_scale(cr, 48.0 / iw, 48.0 / ih);
            cairo_set_source_surface(cr, ic->icon_surface, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    } else {
        /* No themed icon: a folder gets a tab, a file a dog-ear. Enough
         * to tell them apart at a glance without shipping artwork. */
        set_accent(cr, 0.75);
        cairo_set_line_width(cr, 2);
        if (ic->is_dir) {
            cairo_rectangle(cr, ix + 4, iy + 14, 40, 26);
            cairo_stroke(cr);
            cairo_move_to(cr, ix + 4, iy + 14);
            cairo_line_to(cr, ix + 16, iy + 14);
            cairo_line_to(cr, ix + 20, iy + 8);
            cairo_line_to(cr, ix + 30, iy + 8);
            cairo_stroke(cr);
        } else {
            cairo_rectangle(cr, ix + 8, iy + 6, 32, 38);
            cairo_stroke(cr);
            cairo_move_to(cr, ix + 30, iy + 6);
            cairo_line_to(cr, ix + 30, iy + 16);
            cairo_line_to(cr, ix + 40, iy + 16);
            cairo_stroke(cr);
        }
    }

    /* Label, centred and shortened to the cell. Guard the UTF-8 twice
     * over: a filename is arbitrary bytes, and cairo_show_text puts the
     * whole context into a permanent error state on invalid UTF-8 — which
     * would silently blank every icon drawn after it (see news.c). */
    char label[128];
    snprintf(label, sizeof(label), "%s", ic->label);
    if (!deskicon_utf8_ok(label))
        snprintf(label, sizeof(label), "?");

    cairo_set_font_size(cr, 12);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, label, &ext);
    while (ext.width > SYN_DESKICON_W - 8 && strlen(label) > 1) {
        /* Cut one whole character, never mid-sequence. */
        size_t n = strlen(label);
        size_t cut = news_utf8_trim(label, n - 1);
        if (cut >= n) cut = n - 1;
        label[cut] = '\0';
        cairo_text_extents(cr, label, &ext);
    }

    cairo_set_source_rgba(cr, 0, 0, 0, 0.75);   /* shadow for legibility */
    cairo_move_to(cr, cx + (SYN_DESKICON_W - ext.width) / 2.0 + 1,
                  cy + 74 + 1);
    cairo_show_text(cr, label);
    set_ink(cr, INK_STRONG, 1.0);
    cairo_move_to(cr, cx + (SYN_DESKICON_W - ext.width) / 2.0, cy + 74);
    cairo_show_text(cr, label);
}

/* The output the icons live on, or NULL. */
static syn_output_t *deskicons_output(syn_server_t *s)
{
    syn_output_t *o = server_primary_output(s);
    return o ? o : server_focused_output(s);
}

/*
 * Move the drag layer to the dragged icon's position. This is the whole cost
 * of a motion event mid-drag: no allocation, no repaint, and the damage is two
 * icon-sized rects rather than the entire desktop.
 */
void synui_move_deskicon_drag(syn_server_t *s)
{
    if (!s->deskicons_ui.drag_tree) return;

    int i = s->deskicon_drag.idx;
    if (i < 0 || i >= s->deskicon_count) return;

    syn_output_t *o = deskicons_output(s);
    if (!o) return;

    /* The layer is a child of the icon tree, which sits at the usable area's
     * origin, so it wants area-relative coordinates. */
    struct wlr_box area;
    output_usable_box_of(s, o, &area);
    wlr_scene_node_set_position(&s->deskicons_ui.drag_tree->node,
                                s->deskicons[i].x - area.x,
                                s->deskicons[i].y - area.y);
}

/*
 * Desktop icons: one buffer covering the primary output's usable area, with
 * every cell drawn into it. One buffer rather than one per icon because they
 * only ever move one at a time, under a drag, and a re-layout redraws the lot
 * anyway.
 *
 * The exception is that one dragged icon. Redrawing this buffer per motion
 * event meant allocating and repainting a screen-sized surface hundreds of
 * times a second, which is what made a drag lag behind the cursor — so the
 * dragged icon is drawn once into a cell-sized layer of its own, and the drag
 * then just moves that node (synui_move_deskicon_drag).
 */
void synui_render_deskicons(syn_server_t *s)
{
    if (!s->config.desktop_icons || s->deskicon_count <= 0) {
        wlr_scene_node_set_enabled(&s->deskicons_ui.tree->node, false);
        return;
    }

    syn_output_t *o = deskicons_output(s);
    if (!o) {
        wlr_scene_node_set_enabled(&s->deskicons_ui.tree->node, false);
        return;
    }

    struct wlr_box area;
    output_usable_box_of(s, o, &area);
    if (area.width <= 0 || area.height <= 0) return;

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(area.width, area.height, &cr);
    if (!buf) return;
    cairo_begin(cr);

    /* An icon mid-drag is left out of this buffer entirely — it belongs to the
     * drag layer below, which rides over the cells it passes across instead of
     * sliding under the ones later in the array. */
    int dragging = s->deskicon_drag.active && s->deskicon_drag.moved
                 ? s->deskicon_drag.idx : -1;
    if (dragging >= s->deskicon_count) dragging = -1;

    for (int i = 0; i < s->deskicon_count; i++) {
        if (i == dragging) continue;
        deskicon_draw_cell(cr, &s->deskicons[i], &area,
                           i == s->deskicon_selected);
    }

    cairo_destroy(cr);
    wlr_scene_node_set_position(&s->deskicons_ui.tree->node, area.x, area.y);
    wlr_scene_node_set_enabled(&s->deskicons_ui.tree->node, true);
    set_scene_buffer(&s->deskicons_ui.buf, s->deskicons_ui.tree, buf);

    if (dragging < 0) {
        if (s->deskicons_ui.drag_tree)
            wlr_scene_node_set_enabled(&s->deskicons_ui.drag_tree->node, false);
        return;
    }

    if (!s->deskicons_ui.drag_tree)
        s->deskicons_ui.drag_tree = wlr_scene_tree_create(s->deskicons_ui.tree);

    cairo_t *dcr;
    struct wlr_buffer *dbuf =
        create_cairo_buf(SYN_DESKICON_W, SYN_DESKICON_H, &dcr);
    if (dbuf) {
        cairo_begin(dcr);
        /* draw_cell places the icon at (ic->x - area->x, ic->y - area->y), so
         * an area pinned to the icon itself puts it at 0,0 in its own buffer. */
        struct wlr_box cell = { .x      = s->deskicons[dragging].x,
                                .y      = s->deskicons[dragging].y,
                                .width  = SYN_DESKICON_W,
                                .height = SYN_DESKICON_H };
        deskicon_draw_cell(dcr, &s->deskicons[dragging], &cell, 1);
        cairo_destroy(dcr);
        set_scene_buffer(&s->deskicons_ui.drag_buf,
                         s->deskicons_ui.drag_tree, dbuf);
    }

    /* Above the desktop buffer, which set_scene_buffer may have just created
     * as a sibling after this tree. */
    wlr_scene_node_raise_to_top(&s->deskicons_ui.drag_tree->node);
    wlr_scene_node_set_enabled(&s->deskicons_ui.drag_tree->node, true);
    synui_move_deskicon_drag(s);
}

/* ── Alt+Tab switcher (driven by input.c) ────────────────────
 *
 * The tile grid Alt+Tab puts on screen while Alt is held: one tile per
 * candidate window, the one you would land on outlined in the panel accent, and
 * the selected window's full title along the bottom — a tile label has room for
 * about twenty characters, and a browser tab does not.
 *
 * A tile shows the client's *own* current buffer, scaled by the scene graph.
 * The compositor copies no pixels and touches no GPU of its own here, which is
 * the only reason a grid of eighteen live windows is affordable at all. Two
 * consequences are worth knowing:
 *
 *   - A tile is the frame that happened to be committed when it was built, not
 *     a live view. Every press rebuilds every tile, so a playing video steps
 *     once per press. Making them live would mean a commit listener per tile
 *     for an overlay that is on screen for well under a second.
 *   - One surface is in it: the toplevel's root, or a subsurface that covers
 *     the whole window standing in for it (see alttab_content_surface). A
 *     client that paints only *part* of itself into a subsurface still shows
 *     that part as a hole. The alternative is a second scene tree per window
 *     (wlr_scene_subsurface_tree_create), which buys correctness by putting the
 *     client's surfaces into a second set of output enter/leave and frame
 *     callbacks for as long as the tile lives.
 *
 * A window with nothing committed — including an X11 window caught between
 * buffers — falls back to its app icon, so a tile is never an empty rectangle.
 */

#define ATB_TILE_W     208
#define ATB_TILE_H     132      /* thumbnail area; the label row sits under it */
#define ATB_LABEL_H     30
#define ATB_GAP         14
#define ATB_PAD         20
#define ATB_HEAD        46
#define ATB_FOOT        34
#define ATB_COLS_MAX     6
#define ATB_ROWS_MAX     3
#define ATB_INSET        5      /* thumbnail inset within its tile plate */
#define ATB_BADGE       20      /* app icon in the label row */

_Static_assert(ATB_COLS_MAX * ATB_ROWS_MAX == SYN_ALTTAB_TILES,
               "the thumb[] array must hold a full page of tiles");

/* What a tile draws: the client's current buffer, the region of it that is the
 * window proper, and the size that region stands for in logical pixels. The
 * logical size is what the tile's aspect ratio has to come from — a buffer's own
 * dimensions are multiplied by buffer_scale and permuted by its transform. */
typedef struct {
    struct wlr_buffer        *buf;
    struct wlr_fbox           src;    /* buffer pixels to sample */
    int                       lw, lh; /* logical size that region stands for */
    enum wl_output_transform  transform;
} atb_source_t;

/* Which surface actually holds the window's picture.
 *
 * Usually the toplevel's own, but Firefox keeps its root surface as a bare GTK
 * CSD frame and paints the whole window — chrome and content both — into a
 * single subsurface laid over it. Measured with WAYLAND_DEBUG on firefox
 * 2026-07-30: over one run the root surface took 4 commits to the subsurface's
 * 1052, and the four were resizes. Reading the root there gets a full-size
 * buffer with nothing in it, so the tile came out an empty plate — and not even
 * the app-icon fallback, since that turns on a *missing* buffer.
 *
 * A subsurface that covers the whole window box is, by definition, what is on
 * screen for that window, so it can stand in for the root with no second scene
 * tree and no extra frame callbacks. Anything more finely composited than that
 * still shows holes; that is what the tree in the comment above would buy.
 *
 * `box` is the window box, in root-surface-local logical pixels going in and in
 * the returned surface's local pixels coming out. */
static struct wlr_surface *alttab_content_surface(struct wlr_surface *root,
                                                  struct wlr_box *box)
{
    struct wl_list *lists[] = { &root->current.subsurfaces_below,
                                &root->current.subsurfaces_above };
    struct wlr_surface *best = NULL;
    int bx = 0, by = 0;

    /* Bottom to top, keeping the last cover found: if two subsurfaces both
     * cover the window, the upper one is the one you can see. */
    for (size_t i = 0; i < sizeof lists / sizeof lists[0]; i++) {
        struct wlr_subsurface *sub;
        wl_list_for_each(sub, lists[i], current.link) {
            struct wlr_surface *ss = sub->surface;
            if (!ss || !ss->buffer) continue;
            if (ss->current.transform != WL_OUTPUT_TRANSFORM_NORMAL) continue;
            int x = sub->current.x, y = sub->current.y;
            if (x > box->x || y > box->y ||
                x + ss->current.width  < box->x + box->width ||
                y + ss->current.height < box->y + box->height)
                continue;              /* partial: a doorhanger, a tooltip */
            best = ss; bx = x; by = y;
        }
    }
    if (!best) return root;

    box->x -= bx;
    box->y -= by;
    return best;
}

static bool alttab_tile_source(syn_view_t *v, atb_source_t *out)
{
    struct wlr_surface *root = view_surface(v);
    if (!root || !root->buffer) return false;
    if (root->current.width <= 0 || root->current.height <= 0) return false;

    /* The window box in root-surface-local logical pixels. For a CSD client
     * that is its xdg geometry, which crops off the shadow margin — the same
     * pixels view_clip_csd_margin() takes off the live window, and for the same
     * reason. Uncropped, a Firefox tile is its window adrift in 26 px of
     * transparent padding on three sides, and the aspect ratio the tile fits to
     * is the padding's rather than the window's.
     *
     * Clamped into the surface first: a client may declare a geometry bigger
     * than what it actually painted, and a source box running off the end of
     * the buffer samples whatever happens to be there.
     *
     * Only for an untransformed buffer: surface-local x/y map straight onto
     * buffer x/y there, and a rotated toplevel — which essentially does not
     * exist — is not worth an axis-swap table to shave a margin off. */
    struct wlr_box box = { 0, 0, root->current.width, root->current.height };
    struct wlr_surface *surf = root;

    if (root->current.transform == WL_OUTPUT_TRANSFORM_NORMAL) {
        struct wlr_box geo = {0};
        if (v->xdg_surface) geo = v->xdg_surface->geometry;
        if (geo.width > 0 && geo.height > 0) {
            int gx = geo.x < 0 ? 0 : geo.x;
            int gy = geo.y < 0 ? 0 : geo.y;
            int gw = geo.width, gh = geo.height;
            if (gx + gw > box.width)  gw = box.width  - gx;
            if (gy + gh > box.height) gh = box.height - gy;
            if (gw > 0 && gh > 0)
                box = (struct wlr_box){ gx, gy, gw, gh };
        }
        surf = alttab_content_surface(root, &box);
    }

    /* The client's own viewport crop, in buffer pixels: the whole of what this
     * surface displays, wp_viewport or not. */
    wlr_surface_get_buffer_source_box(surf, &out->src);
    out->buf       = &surf->buffer->base;
    out->transform = surf->current.transform;
    out->lw        = surf->current.width;
    out->lh        = surf->current.height;
    if (out->lw <= 0 || out->lh <= 0 ||
        out->src.width <= 0 || out->src.height <= 0)
        return false;

    if (out->transform != WL_OUTPUT_TRANSFORM_NORMAL) return true;

    if (box.x < 0) { box.width  += box.x; box.x = 0; }
    if (box.y < 0) { box.height += box.y; box.y = 0; }
    if (box.x + box.width  > out->lw) box.width  = out->lw - box.x;
    if (box.y + box.height > out->lh) box.height = out->lh - box.y;
    if (box.width <= 0 || box.height <= 0) return true;
    if (!box.x && !box.y && box.width == out->lw && box.height == out->lh)
        return true;                                            /* no margin */

    double fx = out->src.width  / (double)out->lw;
    double fy = out->src.height / (double)out->lh;
    out->src.x     += box.x * fx;
    out->src.y     += box.y * fy;
    out->src.width  = box.width  * fx;
    out->src.height = box.height * fy;
    out->lw = box.width;
    out->lh = box.height;
    return true;
}

/* The app icon, centred on (cx, cy) — what a window with no committed buffer
 * gets instead of a thumbnail, and what every tile gets as its label badge. */
static void alttab_draw_icon(cairo_t *cr, const char *app_id,
                             double cx, double cy, double size)
{
    const syn_icon_entry_t *ic = app_id ? icon_lookup(app_id) : NULL;
    if (ic && ic->icon_surface) {
        int iw = cairo_image_surface_get_width(ic->icon_surface);
        int ih = cairo_image_surface_get_height(ic->icon_surface);
        if (iw > 0 && ih > 0) {
            cairo_save(cr);
            cairo_translate(cr, cx - size / 2.0, cy - size / 2.0);
            cairo_scale(cr, size / iw, size / ih);
            cairo_set_source_surface(cr, ic->icon_surface, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
            return;
        }
    }
    icon_draw_monogram(cr, app_id, cx - size / 2.0, cy - size / 2.0, size);
}

/* Where a window is, for the windows that are not in front of you.
 *
 * The cycle reaches every desktop and every minimized window, so a tile is no
 * longer self-evidently a window you can see — two kitty windows on two
 * desktops are the same picture, and the thumbnail of a minimized window is
 * simply the last frame it drew. Empty for a window on the current desktop
 * that is not minimized, which is the common case and draws nothing.
 *
 * `longform` is for the footer, which has room to spell it out; the tile label
 * row does not. Plain ASCII either way — this font has no arrow glyph, and a
 * missing glyph in a label is a box, not a hint. */
static void alttab_where(syn_server_t *s, syn_view_t *v, bool longform,
                         char *out, size_t n)
{
    char ws[32] = "", mn[16] = "";

    if (v->workspace && v->workspace->index != s->active_workspace)
        snprintf(ws, sizeof ws, longform ? "Desktop %d" : "D%d",
                 v->workspace->index + 1);
    if (v->minimized)
        snprintf(mn, sizeof mn, "%s", longform ? "minimized" : "MIN");

    snprintf(out, n, "%s%s%s", ws,
             (ws[0] && mn[0]) ? (longform ? ", " : " ") : "", mn);
}

/* Release the client buffer a tile is holding and take the tile off screen.
 * The release is the point: a scene buffer locks what it is given, and a client
 * buffer still locked after the overlay is gone is one the client cannot put
 * back into its own rotation. */
static void alttab_tile_clear(syn_server_t *s, int i)
{
    struct wlr_scene_buffer *node = s->alttab_ui.thumb[i];
    if (!node) return;
    wlr_scene_buffer_set_buffer(node, NULL);
    wlr_scene_node_set_enabled(&node->node, false);
}

void synui_alttab_hide(syn_server_t *s)
{
    if (!s->alttab_ui.tree) return;
    for (int i = 0; i < SYN_ALTTAB_TILES; i++)
        alttab_tile_clear(s, i);
    wlr_scene_node_set_enabled(&s->alttab_ui.tree->node, false);
}

void synui_render_alttab(syn_server_t *s, syn_view_t **cands, int n, int sel)
{
    if (!s->alttab_ui.tree) return;
    if (!s->config.alt_tab_preview || n < 1 || sel < 0 || sel >= n) {
        synui_alttab_hide(s);
        return;
    }

    struct wlr_box ob;
    get_output_box(s, &ob);

    /* As many columns as the screen has room for, never more than the grid
     * holds and never more than there are windows — three windows should be
     * three tiles wide, not three tiles adrift in a six-wide panel. */
    int cols = (ob.width - 2 * ATB_PAD + ATB_GAP) / (ATB_TILE_W + ATB_GAP);
    if (cols > ATB_COLS_MAX) cols = ATB_COLS_MAX;
    if (cols > n)            cols = n;
    if (cols < 1)            cols = 1;

    /* Page the grid rather than scroll it a tile at a time: the selection then
     * walks across a still grid instead of the whole grid sliding under a fixed
     * selection. Same window-of-rows idea as the clipboard panel. */
    int page  = cols * ATB_ROWS_MAX;
    int first = (sel / page) * page;
    int shown = n - first;
    if (shown > page) shown = page;

    int rows = (shown + cols - 1) / cols;
    if (rows < 1) rows = 1;

    /* Only now, with the page decided, narrow the panel to the tiles actually
     * on it — a last page of two windows should be two tiles wide, not two
     * tiles adrift in the six-wide frame the full pages used. The page size
     * above deliberately stays computed from the *unshrunk* column count, so
     * which windows land on which page does not change with it. Tile positions
     * are unaffected: this can only shrink cols when shown < cols, and every
     * tile on such a page is already in row 0 at column i. */
    if (shown < cols) cols = shown;

    int pw = 2 * ATB_PAD + cols * ATB_TILE_W + (cols - 1) * ATB_GAP;
    int ph = ATB_HEAD + rows * (ATB_TILE_H + ATB_LABEL_H)
             + (rows - 1) * ATB_GAP + ATB_FOOT;
    int px = ob.x + (ob.width  - pw) / 2;
    int py = ob.y + (ob.height - ph) / 2;

    wlr_scene_node_set_position(&s->alttab_ui.tree->node, px, py);
    wlr_scene_node_set_enabled(&s->alttab_ui.tree->node, true);
    wlr_scene_node_raise_to_top(&s->alttab_ui.tree->node);

    float bg_color[4];
    panel_bg_color(bg_color, 0.93f);
    float accent[4] = { g_panel_accent[0], g_panel_accent[1],
                        g_panel_accent[2], 1.0f };
    if (!s->alttab_ui.bg) {
        s->alttab_ui.bg = wlr_scene_rect_create(s->alttab_ui.tree, pw, ph,
                                                bg_color);
    } else {
        /* Both dimensions track the grid: the panel is as wide as the columns
         * that fit and as tall as the rows in use, and both change with the
         * window count between one press and the next. */
        wlr_scene_rect_set_size(s->alttab_ui.bg, pw, ph);
    }
    wlr_scene_rect_set_color(s->alttab_ui.bg, bg_color);
    if (!s->alttab_ui.accent)
        s->alttab_ui.accent = wlr_scene_rect_create(s->alttab_ui.tree, pw, 2,
                                                    accent);
    else {
        wlr_scene_rect_set_size(s->alttab_ui.accent, pw, 2);
        wlr_scene_rect_set_color(s->alttab_ui.accent, accent);
    }

    cairo_t *cr;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;
    cairo_begin(cr);

    cairo_set_font_size(cr, 15);
    set_accent(cr, 1.0);
    cairo_move_to(cr, ATB_PAD, 30);
    cairo_show_text(cr, "WINDOWS");

    if (n > page) {
        char count[48];
        snprintf(count, sizeof(count), "%d\xe2\x80\x93%d of %d",
                 first + 1, first + shown, n);
        cairo_set_font_size(cr, 12);
        set_ink(cr, INK_DIM, 0.9);
        draw_right(cr, pw - ATB_PAD, 30, count);
    }

    set_ink(cr, INK_RULE, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, ATB_PAD, 38);
    cairo_line_to(cr, pw - ATB_PAD, 38);
    cairo_stroke(cr);

    for (int i = 0; i < shown; i++) {
        syn_view_t *v = cands[first + i];
        int  col = i % cols, row = i / cols;
        int  tx  = ATB_PAD  + col * (ATB_TILE_W + ATB_GAP);
        int  ty  = ATB_HEAD + row * (ATB_TILE_H + ATB_LABEL_H + ATB_GAP);
        bool cur = (first + i == sel);
        int  th  = ATB_TILE_H + ATB_LABEL_H;

        /* Something opaque has to sit under the thumbnail: a client buffer with
         * an alpha channel — any GTK window with rounded corners — would
         * otherwise show the desktop through its own corners. */
        cairo_set_source_rgba(cr, 0.10, 0.10, 0.16, cur ? 0.96 : 0.78);
        cairo_rectangle(cr, tx, ty, ATB_TILE_W, th);
        cairo_fill(cr);

        if (cur) {
            set_accent(cr, 0.20);
            cairo_rectangle(cr, tx, ty, ATB_TILE_W, th);
            cairo_fill(cr);
            set_accent(cr, 1.0);
            cairo_set_line_width(cr, 2);
            cairo_rectangle(cr, tx + 1, ty + 1, ATB_TILE_W - 2, th - 2);
            cairo_stroke(cr);
        }

        int aw = ATB_TILE_W - 2 * ATB_INSET;
        int ah = ATB_TILE_H - 2 * ATB_INSET;

        atb_source_t src;
        if (alttab_tile_source(v, &src)) {
            /* Fit, not fill. Cropping a thumbnail to the tile hides the part of
             * the window that would have said which one it is. */
            double sc = (double)aw / src.lw;
            if ((double)ah / src.lh < sc) sc = (double)ah / src.lh;
            if (sc > 1.0) sc = 1.0;     /* never blow a small window up */
            int dw = (int)(src.lw * sc + 0.5);
            int dh = (int)(src.lh * sc + 0.5);
            if (dw < 1) dw = 1;
            if (dh < 1) dh = 1;

            struct wlr_scene_buffer **node = &s->alttab_ui.thumb[i];
            if (!*node)
                *node = wlr_scene_buffer_create(s->alttab_ui.thumb_tree,
                                                src.buf);
            else
                wlr_scene_buffer_set_buffer(*node, src.buf);

            if (*node) {
                /* No wlr_buffer_drop() to pair with these, unlike every cairo
                 * buffer in this file: the surface owns this one and the scene
                 * takes its own lock. Dropping here would free a buffer the
                 * client is still drawing into. */
                wlr_scene_buffer_set_source_box(*node, &src.src);
                wlr_scene_buffer_set_dest_size(*node, dw, dh);
                wlr_scene_buffer_set_transform(*node, src.transform);
                wlr_scene_buffer_set_opacity(*node, cur ? 1.0f : 0.85f);
                wlr_scene_node_set_position(&(*node)->node,
                                            tx + ATB_INSET + (aw - dw) / 2,
                                            ty + ATB_INSET + (ah - dh) / 2);
                wlr_scene_node_set_enabled(&(*node)->node, true);
            }
        } else {
            alttab_tile_clear(s, i);
            alttab_draw_icon(cr, view_app_id(v), tx + ATB_TILE_W / 2.0,
                             ty + ATB_TILE_H / 2.0, 56);
        }

        /* Label row: the app's icon, then as much of the title as fits. */
        alttab_draw_icon(cr, view_app_id(v),
                         tx + ATB_INSET + ATB_BADGE / 2.0,
                         ty + ATB_TILE_H + ATB_LABEL_H / 2.0, ATB_BADGE);

        const char *title = view_title(v);
        if (!title || !title[0]) title = view_app_id(v);
        if (!title || !title[0]) title = "(untitled)";

        /* A title is whatever bytes the client felt like sending. Invalid UTF-8
         * puts the cairo context into a permanent error state, which would
         * silently blank every tile drawn after this one. */
        char label[192];
        syn_utf8_copy(label, sizeof(label), title);
        if (!label[0]) snprintf(label, sizeof(label), "(untitled)");

        cairo_set_font_size(cr, 12);
        double ly = ty + ATB_TILE_H + ATB_LABEL_H / 2.0 + 4;
        double lr = tx + ATB_TILE_W - ATB_INSET;

        /* The marker goes in the label row, not over the thumbnail: the
         * thumbnails are scene buffers in a tree raised above this cairo layer
         * (see the raise at the end of this function), so anything drawn into
         * the thumbnail area is behind the client's own picture. */
        char where[48];
        alttab_where(s, v, false, where, sizeof where);
        if (where[0]) {
            cairo_text_extents_t wext;
            cairo_text_extents(cr, where, &wext);
            cairo_set_source_rgba(cr, 0.55, 0.58, 0.70, cur ? 1.0 : 0.85);
            draw_right(cr, lr, ly, where);
            lr -= wext.width + 8;
        }

        if (cur) cairo_set_source_rgba(cr, 0.97, 1.00, 1.00, 1.0);
        else     set_ink(cr, 0.76, 1.0);
        double lx = tx + ATB_INSET + ATB_BADGE + 6;
        draw_clipped(cr, lx, ly, lr - lx, label);
    }

    /* Tiles the grid is not using this press still hold a client buffer from
     * the last one. */
    for (int i = shown; i < SYN_ALTTAB_TILES; i++)
        alttab_tile_clear(s, i);

    /* The selected window's title in full, because the tile's copy is clipped
     * to about twenty characters and every tab of one browser looks alike. */
    const char *ftitle = view_title(cands[sel]);
    if (!ftitle || !ftitle[0]) ftitle = view_app_id(cands[sel]);
    if (!ftitle || !ftitle[0]) ftitle = "(untitled)";
    char fbuf[256];
    syn_utf8_copy(fbuf, sizeof(fbuf), ftitle);

    /* Spelled out here because the footer has the room the tile label does not,
     * and because this is the line that says what letting go of Alt will do:
     * landing on one of these switches desktop or restores the window. */
    char fwhere[48];
    alttab_where(s, cands[sel], true, fwhere, sizeof fwhere);
    if (fwhere[0]) {
        size_t fl = strlen(fbuf);
        snprintf(fbuf + fl, sizeof(fbuf) - fl, "  \xc2\xb7  %s", fwhere);
    }

    const char *hint = "Alt+Tab next \xc2\xb7 Alt+Shift+Tab back";
    cairo_set_font_size(cr, 12);
    cairo_text_extents_t hext;
    cairo_text_extents(cr, hint, &hext);

    cairo_set_source_rgba(cr, 0.88, 0.90, 0.96, 1.0);
    draw_clipped(cr, ATB_PAD, ph - 13,
                 pw - 2 * ATB_PAD - hext.width - 24, fbuf);

    set_ink(cr, INK_DIM, 0.9);
    draw_right(cr, pw - ATB_PAD, ph - 13, hint);

    cairo_destroy(cr);
    set_scene_buffer(&s->alttab_ui.text_buf, s->alttab_ui.tree, buf);

    /* Above the cairo layer, which set_scene_buffer may have just created as a
     * later sibling of the thumbnail tree. */
    wlr_scene_node_raise_to_top(&s->alttab_ui.thumb_tree->node);
}

/* ── Initialize all UI scene trees ───────────────────────── */

void synui_ui_init(syn_server_t *s)
{
    /* Create scene trees — later children render on top */
    s->welcome_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->overlay_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->dispcfg_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->wppick_ui.tree  = wlr_scene_tree_create(&s->scene->tree);
    s->curpick_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->power_ui.tree   = wlr_scene_tree_create(&s->scene->tree);
    /* The dim overlay covers the scene, so it needs a tree of its own that
     * can be raised above every window without dragging the panel with it. */
    s->power_ui.dim_tree = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_set_enabled(&s->power_ui.dim_tree->node, true);
    s->taskmgr_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->news_ui.tree    = wlr_scene_tree_create(&s->scene->tree);
    s->filters_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->aimodel_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->widgets_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->sound_ui.tree   = wlr_scene_tree_create(&s->scene->tree);
    s->clock_ui.tree   = wlr_scene_tree_create(&s->scene->tree);
    s->cal_ui.tree     = wlr_scene_tree_create(&s->scene->tree);
    s->ctlpanel_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->thememgr_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->bt_ui.tree      = wlr_scene_tree_create(&s->scene->tree);
    s->notif_ui.tree   = wlr_scene_tree_create(&s->scene->tree);
    s->clip_ui.tree    = wlr_scene_tree_create(&s->scene->tree);
    s->alttab_ui.tree  = wlr_scene_tree_create(&s->scene->tree);
    /* Created with the panel, not lazily with the first tile: the thumbnails
     * have to be a later sibling than the cairo layer to sit on top of it, and
     * the cairo layer's node is itself created lazily on the first render. */
    s->alttab_ui.thumb_tree = wlr_scene_tree_create(s->alttab_ui.tree);
    s->dockmenu_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->deskmenu_ui.tree = wlr_scene_tree_create(&s->scene->tree);
    s->cmdbar_ui.tree  = wlr_scene_tree_create(&s->scene->tree);

    /* All hidden until explicitly shown */
    wlr_scene_node_set_enabled(&s->deskmenu_ui.tree->node, false);
    /* No icon is selected until one is clicked. Zero would mean the first
     * icon came up highlighted on a desktop nobody had touched yet. */
    s->deskicon_selected       = -1;
    s->deskicon_last_click_idx = -1;
    s->deskicon_drag.idx       = -1;

    wlr_scene_node_set_enabled(&s->welcome_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->overlay_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->dispcfg_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->wppick_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->taskmgr_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->news_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->filters_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->widgets_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->sound_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->clock_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->cal_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->ctlpanel_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->thememgr_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->bt_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->notif_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->clip_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->alttab_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->dockmenu_ui.tree->node, false);
    wlr_scene_node_set_enabled(&s->cmdbar_ui.tree->node, false);

    /* Render welcome screen (uses fallback 1920x1080 until output connects).
     * Opted out of via the menu's own "Show At Startup" row: leave the tree
     * empty and disabled — synui_render_welcome builds its nodes lazily, so
     * the first Super+Escape still brings up a complete menu. */
    if (s->config.welcome_at_startup)
        synui_render_welcome(s);
}
