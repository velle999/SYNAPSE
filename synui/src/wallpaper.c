/*
 * wallpaper.c — native, compositor-drawn wallpaper
 *
 * Decodes a single PNG or JPEG image (config keys `wallpaper` /
 * `wallpaper_mode`) and paints it into every connected output's own
 * scene-graph buffer, scaled independently to that output's resolution
 * (fill/fit/stretch/center). No wallpaper configured, or a decode failure,
 * falls back to the compositor's existing solid bg_color — this feature is
 * strictly additive and never fatal.
 *
 * A monitor can override the global keys with its own image, scaling mode and
 * backend (`wallpaper_output` in synuirc, or the Super+W picker scoped to one
 * monitor). Overrides are keyed by connector name and live in the config;
 * wallpaper_effective() is the single place that resolves "what does this
 * output show", and every consumer — the painter below, matrix.c, the picker —
 * goes through it rather than reading cfg->wallpaper directly.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cairo.h>
#include <jpeglib.h>
#include <drm_fourcc.h>

#include <scenefx/types/wlr_scene.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/log.h>

#include "contrast.h"
#include "synui.h"

/* ── Path resolution ─────────────────────────────────────── */

/* Expand a leading "~/" using $HOME. Returns a heap string the caller must
 * free(); NULL on allocation failure. No other config key is a filesystem
 * path, so there's no existing precedent for this — but a bare fopen("~/...")
 * silently fails (it's a literal, nonexistent relative path), which would be
 * a confusing footgun for a wallpaper path typed by hand. */
static char *wallpaper_expand_path(const char *path)
{
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && *home) {
            char *out = NULL;
            if (asprintf(&out, "%s%s", home, path + 1) < 0)
                return NULL;
            return out;
        }
    }
    return strdup(path);
}

/* ── PNG decode ──────────────────────────────────────────── */

static cairo_surface_t *decode_png(const char *path)
{
    cairo_surface_t *surf = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "synui: wallpaper: PNG decode failed for '%s': %s",
                path, cairo_status_to_string(cairo_surface_status(surf)));
        cairo_surface_destroy(surf);
        return NULL;
    }
    return surf;
}

/* ── Format detection + top-level decode ────────────────────── */

/* Non-static since the lock background (lock.c) and the screensaver slideshow
 * (saver.c) both want a wallpaper decoded exactly the way the desktop decodes
 * one. A second decoder in either place would be a second set of format quirks
 * to keep in step — and the JPEG path already lives in imgdec.c for that same
 * reason. */
cairo_surface_t *wallpaper_decode(const char *path)
{
    char *resolved = wallpaper_expand_path(path);
    if (!resolved) return NULL;

    unsigned char magic[8] = {0};
    FILE *fp = fopen(resolved, "rb");
    if (!fp) {
        wlr_log(WLR_ERROR, "synui: wallpaper: cannot open '%s': %s",
                resolved, strerror(errno));
        free(resolved);
        return NULL;
    }
    size_t n = fread(magic, 1, sizeof(magic), fp);
    fclose(fp);

    static const unsigned char png_magic[8] =
        {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

    cairo_surface_t *surf = NULL;
    if (n >= 8 && memcmp(magic, png_magic, 8) == 0) {
        surf = decode_png(resolved);
    } else if (n >= 3 && magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
        surf = syn_decode_jpeg(resolved);
    } else {
        wlr_log(WLR_ERROR, "synui: wallpaper: '%s' is not a recognized PNG/JPEG",
                resolved);
    }
    free(resolved);
    return surf;
}

/* ── Scaling + paint ─────────────────────────────────────── */

/* Indexed by syn_wallpaper_mode_t — keep in step with the enum in synui.h.
 * Used by the config parser and by the Super+W picker's mode row. */
const char *const syn_wallpaper_mode_names[SYN_WALLPAPER_MODE_COUNT] = {
    [SYN_WALLPAPER_FILL]    = "fill",
    [SYN_WALLPAPER_FIT]     = "fit",
    [SYN_WALLPAPER_STRETCH] = "stretch",
    [SYN_WALLPAPER_CENTER]  = "center",
    [SYN_WALLPAPER_TILE]    = "tile",
};

/* Non-static for the same reason wallpaper_decode is: the lock background and
 * the saver slideshow must scale a picture the way the desktop does, or the
 * same wallpaper is framed differently on the lock screen than behind it. */
void wallpaper_paint_box(cairo_t *cr, cairo_surface_t *src,
                         int dst_w, int dst_h,
                         syn_wallpaper_mode_t mode)
{
    int sw = cairo_image_surface_get_width(src);
    int sh = cairo_image_surface_get_height(src);
    if (sw <= 0 || sh <= 0) return;

    if (mode == SYN_WALLPAPER_TILE) {
        /* Repeat at 1:1 from the top-left. This cannot go through the
         * translate+scale path below: the repeat belongs to the pattern, not
         * to the transform.
         *
         * NEAREST, not GOOD: a tile is meant to butt up seamlessly, and a
         * filter that samples across the wrap boundary draws a visible seam
         * along every tile edge. */
        cairo_save(cr);
        cairo_set_source_surface(cr, src, 0, 0);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_rectangle(cr, 0, 0, dst_w, dst_h);
        cairo_fill(cr);
        cairo_restore(cr);
        return;
    }

    double sx = 1.0, sy = 1.0, ox = 0.0, oy = 0.0;
    switch (mode) {
    case SYN_WALLPAPER_STRETCH:
        sx = (double)dst_w / sw;
        sy = (double)dst_h / sh;
        break;
    case SYN_WALLPAPER_CENTER:
        ox = (dst_w - sw) / 2.0;
        oy = (dst_h - sh) / 2.0;
        break;
    case SYN_WALLPAPER_FIT: {
        double sc = fmin((double)dst_w / sw, (double)dst_h / sh);
        sx = sy = sc;
        ox = (dst_w - sw * sc) / 2.0;
        oy = (dst_h - sh * sc) / 2.0;
        break;
    }
    case SYN_WALLPAPER_FILL:
    default: {
        double sc = fmax((double)dst_w / sw, (double)dst_h / sh);
        sx = sy = sc;
        ox = (dst_w - sw * sc) / 2.0;
        oy = (dst_h - sh * sc) / 2.0;
        break;
    }
    }

    cairo_save(cr);
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, sx, sy);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

/* ── Per-monitor overrides ───────────────────────────────── */

syn_wp_output_t *wallpaper_output_entry(syn_config_t *cfg, const char *name,
                                        bool create)
{
    if (!name || !*name) return NULL;

    for (int i = 0; i < cfg->wallpaper_out_n; i++)
        if (strcmp(cfg->wallpaper_out[i].output, name) == 0)
            return &cfg->wallpaper_out[i];

    if (!create || cfg->wallpaper_out_n >= SYN_WP_PEROUT_MAX) {
        if (create)
            wlr_log(WLR_ERROR, "synui: wallpaper: no room for a per-monitor "
                    "wallpaper on '%s' (max %d)", name, SYN_WP_PEROUT_MAX);
        return NULL;
    }

    /* Seed a new override from the global config: scoping the picker to a
     * monitor and then only cycling the scaling mode should change the mode
     * and nothing else. */
    syn_wp_output_t *e = &cfg->wallpaper_out[cfg->wallpaper_out_n++];
    snprintf(e->output, sizeof(e->output), "%s", name);
    snprintf(e->path, sizeof(e->path), "%s", cfg->wallpaper);
    e->mode = cfg->wallpaper_mode;
    e->src  = cfg->wallpaper_src;
    return e;
}

void wallpaper_effective(syn_config_t *cfg, const char *name,
                         syn_wallpaper_src_t *src, const char **path,
                         syn_wallpaper_mode_t *mode)
{
    const syn_wp_output_t *e = wallpaper_output_entry(cfg, name, false);

    if (src)  *src  = e ? e->src  : cfg->wallpaper_src;
    if (path) *path = e ? e->path : cfg->wallpaper;
    if (mode) *mode = e ? e->mode : cfg->wallpaper_mode;
}

void wallpaper_output_clear(syn_config_t *cfg, const char *name)
{
    if (!name) {
        cfg->wallpaper_out_n = 0;
        return;
    }
    for (int i = 0; i < cfg->wallpaper_out_n; i++) {
        if (strcmp(cfg->wallpaper_out[i].output, name) != 0) continue;
        cfg->wallpaper_out[i] = cfg->wallpaper_out[--cfg->wallpaper_out_n];
        return;
    }
}

/* ── Decoded-image cache ─────────────────────────────────── */

/* The decoded surface for `path`, or NULL when it is empty/undecodable. The
 * global wallpaper keeps its own long-standing s->wallpaper.src slot; every
 * per-monitor override shares the small path-keyed cache beside it. Both are
 * populated by wallpaper_reload and only read here, so a paint can never be
 * the thing that decodes a multi-megabyte JPEG. */
static cairo_surface_t *wallpaper_surface(syn_server_t *s, const char *path)
{
    if (!path || !*path) return NULL;
    if (strcmp(path, s->config.wallpaper) == 0) return s->wallpaper.src;

    for (int i = 0; i < s->wallpaper.per_n; i++)
        if (strcmp(s->wallpaper.per[i].path, path) == 0)
            return s->wallpaper.per[i].surf;
    return NULL;
}

/* True when some monitor actually shows the global image — either because it
 * has no override, or because its override names the same path. Keeps a
 * `wallpaper = matrix` session from decoding the bundled PNG that
 * cfg->wallpaper still holds but nothing displays. */
static bool wallpaper_global_used(syn_server_t *s)
{
    syn_config_t *cfg = &s->config;

    if (cfg->wallpaper_src == SYN_WP_SRC_IMAGE) return true;

    for (int i = 0; i < cfg->wallpaper_out_n; i++)
        if (cfg->wallpaper_out[i].src == SYN_WP_SRC_IMAGE &&
            strcmp(cfg->wallpaper_out[i].path, cfg->wallpaper) == 0)
            return true;
    return false;
}

/* Decode every distinct override path into the cache. Callers must have
 * cleared it first (wallpaper_cache_drop). */
static void wallpaper_cache_fill(syn_server_t *s)
{
    syn_config_t *cfg = &s->config;

    for (int i = 0; i < cfg->wallpaper_out_n; i++) {
        const syn_wp_output_t *e = &cfg->wallpaper_out[i];
        if (e->src != SYN_WP_SRC_IMAGE || !e->path[0]) continue;
        if (strcmp(e->path, cfg->wallpaper) == 0) continue;   /* the global slot has it */

        bool dup = false;
        for (int j = 0; j < s->wallpaper.per_n; j++)
            if (strcmp(s->wallpaper.per[j].path, e->path) == 0) { dup = true; break; }
        if (dup || s->wallpaper.per_n >= SYN_WP_PEROUT_MAX) continue;

        int n = s->wallpaper.per_n++;
        snprintf(s->wallpaper.per[n].path, sizeof(s->wallpaper.per[n].path),
                 "%s", e->path);
        s->wallpaper.per[n].surf = wallpaper_decode(e->path);
    }
}

static void wallpaper_cache_drop(syn_server_t *s)
{
    for (int i = 0; i < s->wallpaper.per_n; i++)
        if (s->wallpaper.per[i].surf)
            cairo_surface_destroy(s->wallpaper.per[i].surf);
    s->wallpaper.per_n = 0;
}

/* ── What the bar is drawn on ────────────────────────────── */
/*
 * A bar with no background of its own (macOS 26 — see theme_bar_alpha()) draws
 * its clock and its menus straight onto the wallpaper, so the wallpaper decides
 * whether they can be read. Nothing else in synui has ever had to ask what the
 * wallpaper LOOKS like; it paints it and forgets it.
 *
 * The measurement is taken from the PAINTED BUFFER rather than the source image,
 * because the two are not the same picture: `fill` crops, `fit` letterboxes into
 * bg_color, `center` on a small image is mostly not the image at all, and `tile`
 * repeats it. Sampling the buffer means the scaling question is already answered
 * by the code that answers it for the screen, and a mode change is picked up for
 * free. It also means this runs exactly where a repaint does — on a wallpaper
 * change, a mode change, a rotate — and nowhere else.
 */

/* Painted by synui_main.c into s->bg_rect; declared in synui.h. */
const float syn_bg_color[4] = { 0.07f, 0.07f, 0.12f, 1.0f };

/*
 * …and what it MEASURES, which is the only reason it is not still a literal at
 * the rect's creation.
 *
 * "No picture behind the bar" and "no way to know what is behind the bar" read
 * the same in the code (both leave the painter with nothing to sample) and are
 * not the same fact. The solid background is a colour synui itself chooses and
 * draws; it is more knowable than any wallpaper, not less. Answering it with
 * "unmeasured" is what made a clear bar put its whole opaque background back on
 * the two wallpaper choices that paint no image — `none`, and the matrix rain
 * before its first frame — while every photograph left it clear.
 */
static double solid_backdrop_lum(void)
{
    return syn_rel_luminance(syn_bg_color[0], syn_bg_color[1], syn_bg_color[2]);
}

/* Mean relative luminance of the strip the bar covers, or -1 when there is
 * nothing to measure. `edge` picks which end of the buffer — a bar moved to the
 * bottom is drawn on the bottom of the wallpaper, and measuring the top there
 * would answer a question nobody asked. */
/*
 * The pixels half, over a raw ARGB32 buffer.
 *
 * Split out from the cairo wrapper below because the LIVE wallpaper is not a
 * cairo surface and never can be: it is a client's texture sampled into a
 * buffer synui owns (live_read_argb32), and the whole point of measuring it is
 * that it is the picture actually on screen. Same arithmetic, same byte order,
 * one implementation — two would drift, and the drift would be invisible until
 * a desktop inked one way with a static wallpaper and the other way with a
 * live one showing the same image.
 */
static double strip_luminance_px(const unsigned char *data, int w, int h,
                                 int stride, int rows, syn_bar_edge_t edge)
{
    if (!data || w <= 0 || h <= 0 || rows <= 0) return -1.0;
    if (rows > h) rows = h;

    int y0 = edge == SYN_BAR_EDGE_BOTTOM ? h - rows : 0;

    double sum = 0.0;
    for (int y = y0; y < y0 + rows; y++) {
        const unsigned char *row = data + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            /* ARGB32 is premultiplied, but this buffer is a wallpaper painted
             * edge to edge over an opaque surface, so alpha is 255 throughout
             * and premultiplied equals straight. Byte order is native-endian
             * within the 32-bit word: B, G, R, A on little-endian. */
            const unsigned char *px = row + (size_t)x * 4;
            sum += 0.2126 * syn_srgb_lut(px[2]) +
                   0.7152 * syn_srgb_lut(px[1]) +
                   0.0722 * syn_srgb_lut(px[0]);
        }
    }
    return sum / ((double)w * rows);
}

static double strip_luminance(cairo_surface_t *dst, int rows, syn_bar_edge_t edge)
{
    if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) return -1.0;
    if (cairo_image_surface_get_format(dst) != CAIRO_FORMAT_ARGB32) return -1.0;

    cairo_surface_flush(dst);
    return strip_luminance_px(cairo_image_surface_get_data(dst),
                              cairo_image_surface_get_width(dst),
                              cairo_image_surface_get_height(dst),
                              cairo_image_surface_get_stride(dst), rows, edge);
}

/*
 * The SAME strip, binned into SYN_LUM_COLS columns.
 *
 * ⛔ THIS EXISTS BECAUSE A GRID ROW IS THE WRONG BAND OF PICTURE. The bar asks
 * per module, and the only per-column answer it had was wp_lum_grid's top row —
 * SYN_LUM_ROWS deep, which is 160 pixels of a 1440 screen standing in for a bar
 * 34 tall. Four fifths of that cell is picture the bar is not on, and on any
 * photograph that changes vertically inside it the cell describes a backdrop
 * that is not there: a column measuring 0.29 over its full 160 rows whose top
 * 34 are 0.08. The modules over it inked for the bright reading and came out
 * black on a black bar, beside neighbours that came out white.
 *
 * Same rows, same edge, same arithmetic as strip_luminance_px() — the two must
 * agree cell for cell with each other and with what barscan.c composites a
 * window over, or the fallback would splice two different questions together.
 */
static void strip_luminance_cols_px(const unsigned char *data, int w, int h,
                                    int stride, int rows, syn_bar_edge_t edge,
                                    double cols[SYN_LUM_COLS])
{
    for (int c = 0; c < SYN_LUM_COLS; c++) cols[c] = -1.0;
    if (!data || w <= 0 || h <= 0 || rows <= 0) return;
    if (rows > h) rows = h;

    int y0 = edge == SYN_BAR_EDGE_BOTTOM ? h - rows : 0;

    double sum[SYN_LUM_COLS] = { 0.0 };
    long   n[SYN_LUM_COLS]   = { 0 };

    for (int y = y0; y < y0 + rows; y++) {
        const unsigned char *row = data + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            int c = (int)((long)x * SYN_LUM_COLS / w);
            if (c >= SYN_LUM_COLS) c = SYN_LUM_COLS - 1;
            const unsigned char *px = row + (size_t)x * 4;
            sum[c] += 0.2126 * syn_srgb_lut(px[2]) +
                      0.7152 * syn_srgb_lut(px[1]) +
                      0.0722 * syn_srgb_lut(px[0]);
            n[c]++;
        }
    }
    for (int c = 0; c < SYN_LUM_COLS; c++)
        if (n[c] > 0) cols[c] = sum[c] / (double)n[c];
}

static void strip_luminance_cols(cairo_surface_t *dst, int rows,
                                 syn_bar_edge_t edge, double cols[SYN_LUM_COLS])
{
    for (int c = 0; c < SYN_LUM_COLS; c++) cols[c] = -1.0;
    if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) return;
    if (cairo_image_surface_get_format(dst) != CAIRO_FORMAT_ARGB32) return;

    cairo_surface_flush(dst);
    strip_luminance_cols_px(cairo_image_surface_get_data(dst),
                            cairo_image_surface_get_width(dst),
                            cairo_image_surface_get_height(dst),
                            cairo_image_surface_get_stride(dst), rows, edge,
                            cols);
}

/* Every column of a strip set to one number — the wallpaper choices that paint
 * something known but statistically flat (a solid colour, the matrix rain), for
 * the same reason grid_fill() exists for the grid. */
static void strip_fill(double cols[SYN_LUM_COLS], double lum)
{
    for (int c = 0; c < SYN_LUM_COLS; c++) cols[c] = lum;
}

/* Fold every output's answer into one and publish it. ONE value, because the
 * bar's palette is a QML singleton shared by every screen — see the comment on
 * syn_ink_combine(), which is where two monitors that disagree become "none".
 *
 * Its own file, not theme.state: that file holds facts that change exactly when
 * the theme does, and this one changes when the WALLPAPER does. Filing a
 * wallpaper fact under the theme would mean either rewriting theme.state from
 * the picker or leaving the bar reading a stale answer, and the second is the
 * kind that looks like it works. */
/* Measure one output's wallpaper into o->wp_palette. Split out so the two
 * callers that have a painted surface (here and matrix.c's GPU buffer) do the
 * same thing, and so the pure part stays in palette.c where it is testable. */
static void palette_measure(syn_output_t *o, cairo_surface_t *surf,
                            double surface_lum)
{
    memset(&o->wp_palette, 0, sizeof(o->wp_palette));
    /* ⚠ CLEARED FIRST, AND IT IS NOT `wp_palette.ok`. "This picture has no
     * colour in it" and "no picture has been looked at yet" are two different
     * answers that both leave `ok` false, and the monochrome fallback below is
     * correct for exactly one of them — see wallpaper_palette(). */
    o->wp_measured = false;
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) return;
    if (cairo_image_surface_get_format(surf) != CAIRO_FORMAT_ARGB32) return;

    cairo_surface_flush(surf);
    const unsigned char *data = cairo_image_surface_get_data(surf);
    if (!data) return;

    syn_palette_from_pixels(data,
                            cairo_image_surface_get_width(surf),
                            cairo_image_surface_get_height(surf),
                            cairo_image_surface_get_stride(surf),
                            surface_lum, &o->wp_palette);
    o->wp_measured = true;
}

/* The desktop-wide palette: the FIRST output that measured one.
 *
 * Not an average across monitors, and that is deliberate. Averaging two
 * wallpapers gives a colour that is in neither of them — the classic result is
 * a grey — and the palette drives ONE set of panel colours, so there is nothing
 * per-monitor for a second answer to do. First-with-an-answer is stable across
 * a relayout because the output list order is, and it degrades correctly: a
 * greyscale wallpaper on the first monitor simply hands the question to the
 * next one rather than dragging the answer toward grey.
 */
const syn_palette_t *wallpaper_palette(syn_server_t *s)
{
    /* Whether any screen's wallpaper was LOOKED AT, whatever it answered — the
     * question the monochrome fallback at the bottom turns on. */
    bool measured = false;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        /* ⚠ THE LIVE ONE ANSWERS FIRST, AND IT ANSWERS EVEN WHEN IT SAYS NO.
         *
         * A monitor showing a Workshop wallpaper is not showing the picture in
         * o->wp_palette — that image is painted, covered by the engine's own
         * BACKGROUND surface, and nobody can see it. Falling through to it on a
         * greyscale live wallpaper would take the accent off a hidden picture,
         * which is the whole bug this replaced. `wp_live_have` is the flag for
         * "this screen's wallpaper is somebody else's", and `.ok` inside it is
         * that wallpaper's own answer. */
        if (o->wp_live_have) {
            measured = true;
            if (o->wp_live.ok) return &o->wp_live;
            continue;
        }
        if (o->wp_measured) measured = true;
        if (o->wp_palette.ok) return &o->wp_palette;
    }

    /*
     * Every picture on this desktop was looked at and none of them had a colour
     * to give: a black-and-white photograph, a wallpaper too dark to name a hue
     * off, a live scene still painting black. WHITE AND GREY, not the theme's
     * own accent.
     *
     * ⚠ THE OTHER FALLBACK — the preset's cyan — IS A COLOUR FROM NOWHERE NEAR
     * THE SCREEN, and that is what this replaced. The whole promise of the
     * feature is that the chrome follows the picture, so the one case where the
     * picture has no colour is the case where it must not answer in one.
     * Monochrome is the honest reading of a grey wallpaper.
     *
     * ⚠ AND `measured` IS NOT `ok`. Publishing white for a desktop whose
     * wallpaper has not been painted yet, or whose live wallpaper is a buffer
     * synui cannot read (an external-only DMA-BUF fails SILENTLY), would turn
     * "I could not look" into "there is nothing to see" — the theme's accent is
     * the right answer for that one, and it is what `ok = false` still asks for.
     *
     * Static, and rebuilt on each call rather than cached: it is a dozen
     * floating-point operations, it has no state of its own, and the surface it
     * is corrected against moves with the theme.
     */
    if (!measured) return NULL;
    static syn_palette_t mono;
    syn_palette_monochrome(theme_panel_surface_lum(&s->config), &mono);
    return &mono;
}

/*
 * The same substitution, for the two luminance answers — see wallpaper_palette()
 * above, which is this choice made for the colour.
 *
 * Per OUTPUT rather than first-with-an-answer, because ink is a fact about a
 * screen: one monitor on a Workshop scene and two on the static picture is an
 * ordinary desktop, and folding them would be answering a question nobody asked
 * (the same mistake bar_ink's cross-monitor veto made).
 *
 * ⚠ NO `.ok`-STYLE FALLBACK, deliberately. A live wallpaper that measures 0.5
 * is a real answer; handing that case back to the painted buffer would be
 * exactly the bug — inking from a picture that is covered edge to edge. The
 * only way back to the static answer is the live wallpaper going away, which
 * wallpaper_live_gone() handles by clearing the flag.
 */
const double *wallpaper_lum_grid(const syn_output_t *o)
{
    return o->wp_live_lum_have ? o->wp_live_lum_grid : o->wp_lum_grid;
}

double wallpaper_strip_lum(const syn_output_t *o)
{
    return o->wp_live_lum_have ? o->wp_live_top_lum : o->wp_top_lum;
}

const double *wallpaper_strip_cols(const syn_output_t *o)
{
    return o->wp_live_lum_have ? o->wp_live_strip_lum : o->wp_strip_lum;
}

/* Publish it, for the bar and the widgets — the same contract backdrop.state
 * has, and for the same reason: quickshell cannot ask the compositor anything.
 *
 * ⚠ NOT through synui-apply-theme. That helper is ~20 seconds of shelling out
 * to kwriteconfig and gsettings, and this changes every time the wallpaper
 * does — including once per slide of a slideshow. The toolkit palette is a
 * theme-switch concern; this is a repaint concern, and they travel by different
 * roads on purpose.
 */
static void palette_export(syn_server_t *s)
{
    const syn_palette_t *p = wallpaper_palette(s);

    /* Whether this desktop USES what the picture offered — Control panel ▸
     * Appearance ▸ Wallpaper accent, resolved (wp_accent_on).
     *
     * ⚠ PUBLISHED BESIDE THE COLOURS RATHER THAN SUPPRESSING THEM. Writing
     * `ok=no` for a switched-off desktop would be the file telling the bar the
     * wallpaper has no colour in it, which is a different fact and one a picker
     * that wants to SHOW the colour it is not using would then be lied to
     * about. The measurement is the measurement; this is what to do with it.
     *
     * ⚠ AND IT IS THE HALF THE BAR NEVER HAD. The substitution into synui's own
     * panels was gated on the theme and this file was not, so quickshell took
     * the wallpaper's accent on every theme — a macOS 26 desktop drew systemBlue
     * panels beside a bar the colour of the picture, with nothing anywhere
     * saying that was a decision. */
    const bool use = wp_accent_on(&s->config);

    /* Written only on a CHANGE, exactly like backdrop.state: the bar watches
     * this path and every relayout would otherwise have it reload.
     *
     * ⚠ THE SWITCH IS PART OF WHAT "CHANGED" MEANS. It moves without the
     * wallpaper moving — that is the entire point of it — so a compare over the
     * colours alone would early-out on the one path the row has, and the file
     * would keep saying `use=yes` until the next time the picture happened to
     * change. */
    static syn_palette_t last;
    static bool last_use = false;
    static bool have_last = false;
    syn_palette_t now;
    memset(&now, 0, sizeof(now));
    if (p) now = *p;
    if (have_last && use == last_use && memcmp(&now, &last, sizeof(now)) == 0)
        return;

    char path[256];
    if (!syn_config_path(path, sizeof(path), "palette.state")) return;
    syn_config_ensure_dir();

    char tmp[288];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: palette: cannot write '%s': %s",
                tmp, strerror(errno));
        return;
    }
    fprintf(f, "# Generated by synui — the small palette taken off the\n"
               "# wallpaper. Hand edits are overwritten on the next wallpaper\n"
               "# change.\n"
               "#\n"
               "# `use` is whether this desktop draws with it, which is a\n"
               "# SETTING (wallpaper_accent = auto|off|on; auto is Prism,\n"
               "# light or dark, the theme built on it) and not a property of\n"
               "# the picture.\n"
               "# `ok` is whether there are colours here to draw with.\n"
               "# `mono` is whether they came from a hue in the picture or\n"
               "# from the ABSENCE of one: a greyscale or near-black wallpaper\n"
               "# is answered in white and greys rather than in the theme's\n"
               "# own accent, which is a colour from nowhere near the screen.\n");
    fprintf(f, "use=%s\n", use ? "yes" : "no");
    /* `ok=no` is published rather than the file being deleted or left stale:
     * "no wallpaper has been measured here" is an answer the bar has to be able
     * to act on, and an absent file is indistinguishable from a synui too old
     * to write one.
     *
     * ⚠ IT NO LONGER MEANS "GREYSCALE". That case is `ok=yes mono=yes` now, and
     * it is deliberately readable by every consumer that already gates on
     * `ok=yes` — the bar, the widgets and the eight app windows take the
     * monochrome palette with no change at all, which is the point: a desktop
     * does not have two ways of drawing with the wallpaper's answer. */
    fprintf(f, "ok=%s\n", now.ok ? "yes" : "no");
    if (now.ok) {
        fprintf(f, "mono=%s\n", now.monochrome ? "yes" : "no");
        fprintf(f, "accent=#%02X%02X%02X\n",
                (int)lround(now.accent[0] * 255.0),
                (int)lround(now.accent[1] * 255.0),
                (int)lround(now.accent[2] * 255.0));
        fprintf(f, "accent_dim=#%02X%02X%02X\n",
                (int)lround(now.accent_dim[0] * 255.0),
                (int)lround(now.accent_dim[1] * 255.0),
                (int)lround(now.accent_dim[2] * 255.0));
        fprintf(f, "secondary=#%02X%02X%02X\n",
                (int)lround(now.secondary[0] * 255.0),
                (int)lround(now.secondary[1] * 255.0),
                (int)lround(now.secondary[2] * 255.0));
        /* Said out loud, because a row that shows where its colour came from
         * must not claim a rotated stand-in came off the picture. */
        fprintf(f, "secondary_measured=%s\n",
                now.measured_secondary ? "yes" : "no");
    }
    fclose(f);

    if (rename(tmp, path) != 0) {
        wlr_log(WLR_ERROR, "synui: palette: cannot rename '%s': %s",
                tmp, strerror(errno));
        unlink(tmp);
        return;
    }
    last = now;
    last_use = use;
    have_last = true;

    /* And into the running desktop. Published first, applied second: the file
     * is what the bar and the widgets read, and this is what synui's own panels
     * read — a failure to write the file must not stop the compositor's own
     * colours from following the wallpaper. */
    theme_refresh_wallpaper_accent(s);

    /* Four states and four lines, because "the desktop is not on the
     * wallpaper's colour" has three quite different causes and the log is where
     * anybody asking why will look first. */
    if (!now.ok)
        wlr_log(WLR_INFO, "synui: palette: no wallpaper measured yet — "
                          "the theme's own accent stands");
    else if (now.monochrome)
        wlr_log(WLR_INFO, "synui: palette: the wallpaper has no usable hue — "
                          "monochrome, accent #%02X%02X%02X%s",
                (int)lround(now.accent[0] * 255.0),
                (int)lround(now.accent[1] * 255.0),
                (int)lround(now.accent[2] * 255.0),
                use ? "" : ", not in use (wallpaper_accent)");
    else if (!use)
        wlr_log(WLR_INFO, "synui: palette: accent #%02X%02X%02X off the "
                          "wallpaper, not in use (wallpaper_accent)",
                (int)lround(now.accent[0] * 255.0),
                (int)lround(now.accent[1] * 255.0),
                (int)lround(now.accent[2] * 255.0));
    else
        wlr_log(WLR_INFO, "synui: palette: accent #%02X%02X%02X off the wallpaper",
                (int)lround(now.accent[0] * 255.0),
                (int)lround(now.accent[1] * 255.0),
                (int)lround(now.accent[2] * 255.0));
}

/*
 * The answer changed without the picture changing — the control panel row, a
 * theme switch under `auto`, a hand-edited synuirc and a reload.
 *
 * Just the export: the per-output measurement is already cached (wp_palette,
 * filled when the wallpaper was painted), and palette_export() is what
 * publishes the decision AND hands it to synui's own panels. Re-decoding the
 * image to move a switch would be several hundred milliseconds of JPEG for a
 * colour that is already in memory.
 */
void wallpaper_accent_refresh(syn_server_t *s)
{
    palette_export(s);
}

/* ── The wallpaper synui did NOT paint ───────────────────── */
/*
 * A live wallpaper is a CLIENT, and the accent came off the picture underneath
 * it.
 *
 * synui-wpengine runs linux-wallpaperengine as a wlr-layer-shell BACKGROUND
 * surface, and synui creates layer_tree[BACKGROUND] above its own
 * wallpaper_tree — so the engine's output covers wallpaper.c's painted buffer
 * completely. Everything above this line still measured that buffer, so a
 * desktop running a Workshop wallpaper took its accent off whatever static
 * image happened to be configured: on this box the house logo's violet, on a
 * fresh install the shipped default, and in both cases a colour that is not
 * anywhere on the screen. It looked exactly like the feature being broken for
 * live wallpapers, because from the outside it is.
 *
 * ⚠ THE PIXELS ARE THE CLIENT'S, SO wlr_texture_from_buffer() IS THE WRONG
 * CALL — it returns NULL for a wlr_client_buffer, silently, which reads as "no
 * wallpaper there". The texture is already on the wrapper; borrow it and never
 * destroy it. barscan.c learnt this the expensive way (383) and its comment is
 * the long version.
 *
 * ── Why a timer, and why several ──────────────────────────────────────────
 *
 * The engine maps its surface before it has anything to draw: a scene
 * wallpaper spends a second or two compiling shaders and loading assets, and
 * the first committed frames are black. Black measures as "no usable hue",
 * which is indistinguishable from an honest greyscale wallpaper — so a single
 * read at map time answers correctly only for the wallpapers that happen to
 * start fast. Read on a settle timer, and keep reading while the answer is
 * still "nothing", up to WP_LIVE_TRIES. A wallpaper that genuinely has no hue
 * costs the retries and then stands as `have` with `ok = false`, which is the
 * right answer and is NOT the same as having no live wallpaper at all.
 *
 * ── Why not on every frame ────────────────────────────────────────────────
 *
 * Because the desktop would then be re-themed continuously. A video wallpaper's
 * dominant hue changes shot to shot, and panel colours that chase it are not a
 * theme, they are a strobe. One answer per wallpaper, exactly like a static
 * picture — the engine's wallpaper is chosen by a person, and that choice is
 * when the colour is allowed to move.
 */
#define WP_LIVE_SETTLE_MS 1200
#define WP_LIVE_TRIES     6

/* The BACKGROUND layer surface that covers this output, or NULL.
 *
 * Not keyed on a namespace. linux-wallpaperengine's is its own business and a
 * second engine would have another; what makes a surface the wallpaper is that
 * it is on the background layer and it covers the screen. The 3/4 slack is for
 * a client that leaves a margin or rounds its size down at a fractional scale —
 * a background surface that small is still the thing behind everything.
 */
static syn_layer_surface_t *live_wallpaper_surface(syn_output_t *o)
{
    struct wlr_box full;
    wlr_output_layout_get_box(o->server->output_layout, o->wlr_output, &full);
    if (full.width <= 0 || full.height <= 0) return NULL;

    syn_layer_surface_t *ls, *found = NULL;
    wl_list_for_each(ls, &o->layer_surfaces, link) {
        if (ls->layer != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) continue;
        struct wlr_surface *surf = ls->layer_surface->surface;
        if (!surf || !surf->mapped) continue;
        if (surf->current.width  * 4 < full.width  * 3) continue;
        if (surf->current.height * 4 < full.height * 3) continue;
        found = ls;
    }
    return found;
}

/*
 * Read a texture into the ARGB32 palette.c expects.
 *
 * ⚠ THE PREFERRED READ FORMAT IS NOT ALWAYS THE ONE THE PALETTE READS. palette.c
 * takes native-endian ARGB32 — B, G, R, A in memory on little-endian — and a
 * texture may prefer the byte-reversed XBGR/ABGR pair instead. Handing those
 * over unswapped does not fail: it measures a picture with its reds and blues
 * exchanged, so a warm wallpaper themes the desktop cold and every check of the
 * form "did we get a colour" passes. Swapped here, in place.
 */
static unsigned char *tex_read_argb32(struct wlr_texture *tex,
                                      int *w_out, int *h_out, size_t *stride_out,
                                      char *why, size_t whyn)
{
    int w = tex->width, h = tex->height;
    if (w <= 0 || h <= 0) {
        snprintf(why, whyn, "the texture is %dx%d", w, h);
        return NULL;
    }
    /* A guard against a client that committed something absurd, not a policy:
     * the read is one malloc of w*h*4 and this is the only place its size comes
     * from outside synui. */
    if ((long)w * h > 64L * 1024 * 1024) {
        snprintf(why, whyn, "the texture is absurdly large (%dx%d)", w, h);
        return NULL;
    }

    uint32_t fmt = wlr_texture_preferred_read_format(tex);
    int bpp;         /* bytes per pixel the read produces */
    bool swap;       /* its red and blue are the other way round */
    switch (fmt) {
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ARGB8888: bpp = 4; swap = false; break;
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_ABGR8888: bpp = 4; swap = true;  break;
    /* ⚠ AND THE 24-BIT PAIR, WHICH IS WHAT AN OPAQUE BUFFER ANSWERS. The copy
     * below is allocated in the output's swapchain format, which is XRGB8888 —
     * no alpha bits, so GLES reports GL_RGB/GL_UNSIGNED_BYTE and wlroots names
     * that BGR888: three bytes per pixel, not four. Declining it cost the whole
     * measurement once already. */
    case DRM_FORMAT_RGB888:   bpp = 3; swap = false; break;
    case DRM_FORMAT_BGR888:   bpp = 3; swap = true;  break;
    /* Anything else declines rather than guesses — same call barscan.c makes,
     * and for the same reason: a wrong measurement is worse than none, because
     * none falls back to the picture underneath and wrong repaints the desktop
     * confidently. */
    default:
        snprintf(why, whyn, "its read format %.4s (0x%08x) is not one palette.c reads",
                 (const char *)&fmt, fmt);
        return NULL;
    }

    size_t read_stride = (size_t)w * (size_t)bpp;
    unsigned char *raw = malloc(read_stride * (size_t)h);
    if (!raw) {
        snprintf(why, whyn, "out of memory for a %dx%d read", w, h);
        return NULL;
    }

    struct wlr_texture_read_pixels_options opts = {
        .data    = raw,
        .format  = fmt,
        .stride  = (uint32_t)read_stride,
        .dst_x   = 0,
        .dst_y   = 0,
        .src_box = { .x = 0, .y = 0, .width = w, .height = h },
    };
    if (!wlr_texture_read_pixels(tex, &opts)) {
        snprintf(why, whyn, "the renderer would not read the texture back "
                            "(%.4s, %dx%d)", (const char *)&fmt, w, h);
        free(raw);
        return NULL;
    }

    /* palette.c reads native-endian ARGB32 — B, G, R, A in memory on
     * little-endian.
     *
     * ⚠ THE SWAP IS NOT OPTIONAL AND ITS ABSENCE DOES NOT FAIL. Handing a
     * reversed buffer over measures the picture with its reds and blues
     * exchanged, so a warm wallpaper themes the desktop cold and every check of
     * the form "did we get a colour" still passes. */
    size_t stride = (size_t)w * 4;
    unsigned char *data = (bpp == 4) ? raw : malloc(stride * (size_t)h);
    if (!data) {
        snprintf(why, whyn, "out of memory for a %dx%d read", w, h);
        free(raw);
        return NULL;
    }
    for (int y = 0; y < h; y++) {
        const unsigned char *src = raw + (size_t)y * read_stride;
        unsigned char *dst = data + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            const unsigned char *sp = src + (size_t)x * bpp;
            unsigned char b, g, r, a;
            if (swap) { b = sp[2]; g = sp[1]; r = sp[0]; }
            else      { b = sp[0]; g = sp[1]; r = sp[2]; }
            a = bpp == 4 ? sp[3] : 0xff;
            unsigned char *dp = dst + (size_t)x * 4;
            dp[0] = b; dp[1] = g; dp[2] = r; dp[3] = a;
        }
    }
    if (data != raw) free(raw);

    *w_out = w;
    *h_out = h;
    *stride_out = stride;
    return data;
}

/* The measurement is a histogram over ~40k sampled pixels (palette.c), so the
 * copy it walks does not want to be a 4K frame: at 3840x2160 that is a 33 MB
 * malloc and a 33 MB glReadPixels, per output, per retry. Scaled to this on the
 * long edge, the GPU does the reduction and the answer is the same one. */
#define WP_LIVE_READ_MAX 640

/*
 * ⚠ A CLIENT'S TEXTURE IS OFTEN ONE THE COMPOSITOR CAN ONLY SAMPLE, NEVER READ.
 *
 * The pixels arrive as a DMA-BUF, and whether that import is a plain
 * GL_TEXTURE_2D or a GL_TEXTURE_EXTERNAL_OES is the driver's decision, per
 * format and modifier. NVIDIA reports LINEAR as external-only on this hardware,
 * and scenefx's fx_texture_bind() refuses to hang an FBO off an external
 * texture — which makes wlr_texture_preferred_read_format() answer
 * DRM_FORMAT_INVALID and wlr_texture_read_pixels() return false, NEITHER OF
 * THEM LOGGING ANYTHING. Reading the client's texture directly therefore worked
 * for a wl_shm wallpaper (swaybg, and the test that shipped with 387) and
 * failed for the real one, silently, landing back on the invisible static
 * picture — exactly the bug 387 was supposed to have fixed.
 *
 * Sampling, however, is the one thing every texture can do: it is what the
 * compositor does with this surface on every frame. So draw it into a small
 * buffer synui owns — an ordinary render buffer, never external — and read
 * THAT. The render pass also does the downscale for free.
 */
static unsigned char *live_read_argb32(syn_output_t *o, struct wlr_texture *tex,
                                       int *w_out, int *h_out, size_t *stride_out,
                                       char *why, size_t whyn)
{
    syn_server_t *s = o->server;
    if (!s->renderer || !s->allocator) {
        snprintf(why, whyn, "no renderer/allocator to sample it with");
        return NULL;
    }
    int tw = tex->width, th = tex->height;
    if (tw <= 0 || th <= 0) {
        snprintf(why, whyn, "the texture is %dx%d", tw, th);
        return NULL;
    }

    int w = tw, h = th;
    int longest = tw > th ? tw : th;
    if (longest > WP_LIVE_READ_MAX) {
        w = tw * WP_LIVE_READ_MAX / longest;
        h = th * WP_LIVE_READ_MAX / longest;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
    }

    /* The output's own swapchain format, because that is the one already known
     * to be both renderable and importable here — the same choice effects.c
     * makes for its offscreen pass. Asking the renderer for a format list is
     * not an option: wlroots 0.20 publishes only the sampling one. */
    if (!o->wlr_output->swapchain) {
        snprintf(why, whyn, "the output has no swapchain to borrow a format from");
        return NULL;
    }
    struct wlr_buffer *dst = wlr_allocator_create_buffer(
        s->allocator, w, h, &o->wlr_output->swapchain->format);
    if (!dst) {
        snprintf(why, whyn, "could not allocate a %dx%d buffer to sample into", w, h);
        return NULL;
    }

    struct wlr_render_pass *pass =
        wlr_renderer_begin_buffer_pass(s->renderer, dst, NULL);
    if (!pass) {
        snprintf(why, whyn, "could not open a render pass on the copy");
        wlr_buffer_drop(dst);
        return NULL;
    }
    /* BLEND_NONE: the wallpaper's own alpha must not mix the copy with whatever
     * the fresh buffer happens to hold. */
    wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
        .texture     = tex,
        .dst_box     = { .x = 0, .y = 0, .width = w, .height = h },
        .filter_mode = WLR_SCALE_FILTER_BILINEAR,
        .blend_mode  = WLR_RENDER_BLEND_MODE_NONE,
    });
    if (!wlr_render_pass_submit(pass)) {
        snprintf(why, whyn, "the render pass onto the copy failed");
        wlr_buffer_drop(dst);
        return NULL;
    }

    struct wlr_texture *copy = wlr_texture_from_buffer(s->renderer, dst);
    if (!copy) {
        snprintf(why, whyn, "the copy could not be imported back as a texture");
        wlr_buffer_drop(dst);
        return NULL;
    }

    unsigned char *data = tex_read_argb32(copy, w_out, h_out, stride_out, why, whyn);
    wlr_texture_destroy(copy);
    wlr_buffer_drop(dst);
    return data;
}

/* Both defined further down, beside the painted measurement they are the other
 * half of — declared here because the live path measures the same two things
 * off a different picture. */
static void grid_luminance_px(const unsigned char *data, int w, int h,
                              int stride, double grid[SYN_LUM_CELLS]);
static void backdrop_export(syn_server_t *s);

/*
 * The INK half of a live wallpaper measurement, off the same copy the palette
 * was taken from.
 *
 * ⚠ THE COPY IS IN THE OUTPUT'S OWN ORIENTATION ALREADY. It is the client's
 * surface texture, and a layer surface is sized and drawn in logical output
 * coordinates — the output transform is applied when the scene is composited,
 * not to the buffer the client committed. So a rotated monitor needs nothing
 * done here, and the grid lands cell for cell on the painted one it replaces,
 * which is measured at wlr_output_transformed_resolution() for the same reason.
 *
 * ⚠ The strip's row count has to be rescaled. `rows` is SYN_BAR_STRIP_LOGICAL
 * in the pixels of the buffer being measured, and this buffer is not the
 * output's size — live_read_argb32 scales the long edge to WP_LIVE_READ_MAX.
 * Passing the logical 34 would measure a band several times too deep on a 4K
 * screen and quietly average the bar's own strip together with what is below it.
 */
static void live_lum_measure(syn_output_t *o, const unsigned char *data,
                             int w, int h, int stride)
{
    struct wlr_box box;
    wlr_output_layout_get_box(o->server->output_layout, o->wlr_output, &box);
    if (box.height <= 0) return;

    grid_luminance_px(data, w, h, stride, o->wp_live_lum_grid);

    int rows = (int)lround(SYN_BAR_STRIP_LOGICAL * (double)h / box.height);
    if (rows < 1) rows = 1;
    o->wp_live_top_lum = strip_luminance_px(data, w, h, stride, rows,
                                            o->server->config.bar_edge);
    strip_luminance_cols_px(data, w, h, stride, rows,
                            o->server->config.bar_edge, o->wp_live_strip_lum);
    o->wp_live_lum_have = true;
}

/* One attempt. true when this output now has an answer worth keeping. */
static bool live_measure(syn_output_t *o, char *why, size_t whyn)
{
    syn_layer_surface_t *ls = live_wallpaper_surface(o);
    if (!ls) {
        snprintf(why, whyn, "no background surface covers the output");
        return false;
    }

    struct wlr_surface *surf = ls->layer_surface->surface;
    /* The wrapper wlroots made when the client attached — see the header
     * comment. Its texture is BORROWED. */
    if (!surf->buffer || !surf->buffer->texture) {
        snprintf(why, whyn, "the surface has no %s yet",
                 surf->buffer ? "texture" : "committed buffer");
        return false;
    }

    int w = 0, h = 0;
    size_t stride = 0;
    unsigned char *data = live_read_argb32(o, surf->buffer->texture,
                                           &w, &h, &stride, why, whyn);
    if (!data) return false;

    syn_palette_t p;
    /* Corrected against synui's own panel, exactly as the static measurement
     * is: these colours are drawn ON panels, and the surface they are extracted
     * from is not the surface they land on. */
    syn_palette_from_pixels(data, w, h, (int)stride,
                            theme_panel_surface_lum(&o->server->config), &p);

    /* The ink, off the same copy and before it goes. Two questions of one
     * picture and one readback — and, more to the point, one place where "this
     * screen's wallpaper is somebody else's" is established. Splitting them
     * would put the accent on the live picture and leave the ink on the hidden
     * one, which is the state this fixes. */
    live_lum_measure(o, data, w, h, (int)stride);

    free(data);

    o->wp_live = p;
    o->wp_live_have = true;
    return p.ok;
}

static int live_tick(void *data)
{
    syn_output_t *o = data;
    char why[160] = "";

    /* Not a wallpaper after all — a background-layer surface that does not
     * cover the screen — or one that has gone away since. Nothing measured and
     * nothing published: wallpaper_live_gone() owns the going-away half, and
     * an INFO line here would be one for every panel that ever mapped on the
     * background layer. It is worth a DEBUG one, though: "the engine is
     * running and the accent never moved" and "synui never saw a wallpaper" are
     * the two halves of the same complaint and nothing else tells them apart. */
    if (!live_wallpaper_surface(o)) {
        wlr_log(WLR_DEBUG, "synui: palette: %s live tick found no background "
                "surface covering the output", o->wlr_output->name);
        return 0;
    }

    /* Still black, or still loading its scene. Come back. */
    if (!live_measure(o, why, sizeof why) && --o->wp_live_tries > 0) {
        wl_event_source_timer_update(o->wp_live_timer, WP_LIVE_SETTLE_MS);
        return 0;
    }

    /* Three outcomes and three words, because "the desktop is not on the live
     * wallpaper's colour" has three quite different causes and this log line is
     * where anybody asking why will look first — with, for the unreadable case,
     * the reason. "nothing readable" on its own was true for a month and said
     * nothing about which of half a dozen gates the buffer fell at. */
    wlr_log(WLR_INFO, "synui: palette: %s live wallpaper measured %s%s%s",
            o->wlr_output->name,
            !o->wp_live_have ? "nothing readable"
                             : o->wp_live.ok ? "a hue" : "no usable hue",
            (!o->wp_live_have && why[0]) ? " — " : "",
            (!o->wp_live_have && why[0]) ? why : "");
    palette_export(o->server);
    /* ⚠ AND THE INK, which is not the palette. backdrop_export() writes only on
     * a change, so this costs nothing on the retries that measure the same
     * picture again — and without it the grid stays the painted one until the
     * next thing that happens to re-export, which on a desktop nobody is
     * touching is never. */
    backdrop_export(o->server);
    return 0;
}

/*
 * ⚠ ARMED OFF THE LAYER, NOT OFF THE MEASUREMENT. Asking
 * live_wallpaper_surface() here and returning early would tie this to
 * wlr_surface.mapped already being true at the instant the map signal fires,
 * which is wlroots' business and not a fact worth depending on; the tick asks
 * the same question 1.2s later, when the answer also accounts for a client that
 * committed its real size on the frame after it mapped.
 */
void wallpaper_live_appeared(syn_output_t *o)
{
    if (!o->wp_live_timer) {
        o->wp_live_timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(o->server->display), live_tick, o);
        if (!o->wp_live_timer) return;
    }
    o->wp_live_tries = WP_LIVE_TRIES;
    wl_event_source_timer_update(o->wp_live_timer, WP_LIVE_SETTLE_MS);
}

void wallpaper_live_gone(syn_output_t *o)
{
    if (o->wp_live_have || o->wp_live_lum_have) {
        o->wp_live_have = false;
        o->wp_live_lum_have = false;
        memset(&o->wp_live, 0, sizeof(o->wp_live));
        palette_export(o->server);
        /* …and the ink back to the painted picture, which is on screen again
         * the instant the engine's surface unmaps. */
        backdrop_export(o->server);
    }

    /* ⚠ AND THEN ASK AGAIN, rather than deciding here that there is no live
     * wallpaper left.
     *
     * This fires for every background-layer unmap, and two of them are not the
     * end of anything: a second background client, and an engine RESTART —
     * synui-wpengine stops one process and starts another whenever a single
     * monitor's wallpaper changes, so an unmap and a map arrive a frame or two
     * apart. Re-arming costs one timer that finds nothing and returns; reading
     * the unmapping surface's own `mapped` flag to tell the cases apart would
     * make this depend on whether wlroots clears it before or after it emits,
     * which is exactly the kind of ordering this file should not know about. */
    wallpaper_live_appeared(o);
}

void wallpaper_live_finish(syn_output_t *o)
{
    if (o->wp_live_timer) {
        wl_event_source_remove(o->wp_live_timer);
        o->wp_live_timer = NULL;
    }
    o->wp_live_have = false;
    o->wp_live_lum_have = false;
}

/*
 * What is behind a box, in LAYOUT coordinates — the question every surface that
 * is not the bar has to ask, and the only public way into the grid.
 *
 * Here rather than in contrast.c because it is the half that knows about
 * monitors: contrast.c does the arithmetic over one grid in 0..1 fractions and
 * deliberately depends on nothing in the tree, so the conversion from layout
 * pixels — which needs the output layout, and needs to handle a panel that
 * straddles two screens — lives on this side of that line.
 *
 * ⚠ TWO MONITORS FOLD THE SAME WAY TWO CELLS DO, with syn_ink_combine, so a
 * panel dragged across the seam between a dark screen and a pale one gets NONE
 * and takes the scrim. That is the same answer backdrop_export() gives the bar
 * for the same situation, and for the same reason: one surface, one ink.
 *
 * ⚠ AND THE GRID IT ASKS IS NOT THE WALLPAPER'S ANY MORE. It is the wallpaper
 * with barscan.c's scene measurement laid over the top, cell by cell — because
 * a panel over a browser is over the browser, and measuring the picture the
 * browser covers is how the control panel came up inked for a photograph nobody
 * could see. Resolved HERE and not in contrast.c for the same reason the layout
 * arithmetic is here: contrast.c takes one grid and knows nothing about
 * monitors, and which of two grids answers for a cell is a fact about a screen.
 */
void wallpaper_backdrop_for_box(syn_server_t *s, const struct wlr_box *box,
                                double target, syn_backdrop_t *out)
{
    out->lum     = -1.0;
    out->lum_min = -1.0;
    out->lum_max = -1.0;
    out->ink     = SYN_INK_NONE;
    out->best    = SYN_INK_NONE;
    if (!s || !box) return;

    double sum  = 0.0;
    double area = 0.0;
    double lo   = 0.0, hi = 0.0;
    bool   any  = false;
    bool   seen = false;
    syn_ink_t ink = SYN_INK_NONE, best = SYN_INK_NONE;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        struct wlr_box ob;
        wlr_output_layout_get_box(s->output_layout, o->wlr_output, &ob);
        if (ob.width <= 0 || ob.height <= 0) continue;

        /* The part of the box on THIS output. A panel entirely on another
         * screen contributes nothing and must not drag its backdrop in. */
        int ix0 = box->x > ob.x ? box->x : ob.x;
        int iy0 = box->y > ob.y ? box->y : ob.y;
        int ix1 = box->x + box->width  < ob.x + ob.width
                ? box->x + box->width  : ob.x + ob.width;
        int iy1 = box->y + box->height < ob.y + ob.height
                ? box->y + box->height : ob.y + ob.height;
        if (ix1 <= ix0 || iy1 <= iy0) continue;

        /* The two grids folded into the one contrast.c takes. -1 in scene_lum
         * is "nothing of ours covers this cell", which is the ordinary case and
         * the one where the wallpaper's own answer is not a second-best guess
         * but the correct measurement — the same per-cell rule Theme.qml's
         * barStripAt() applies to the bar's row. */
        const double *wp = wallpaper_lum_grid(o);
        double grid[SYN_LUM_CELLS];
        for (int i = 0; i < SYN_LUM_CELLS; i++)
            grid[i] = o->scene_lum[i] >= 0.0 ? o->scene_lum[i] : wp[i];

        syn_backdrop_t part;
        syn_backdrop_for_box(grid,
                             (double)(ix0 - ob.x) / ob.width,
                             (double)(iy0 - ob.y) / ob.height,
                             (double)(ix1 - ix0)  / ob.width,
                             (double)(iy1 - iy0)  / ob.height,
                             target, &part);

        /* Weighted by how much of the panel is on this screen, so the mean is
         * the mean over the panel rather than over the monitors it touches. */
        double w = (double)(ix1 - ix0) * (double)(iy1 - iy0);
        if (part.lum >= 0.0) { sum += part.lum * w; area += w; }

        /* The extremes are NOT weighted and NOT averaged — see syn_backdrop_t.
         * A worst case is a worst case however little of the panel is over it;
         * the whole point of carrying them is that the mean hides exactly this.
         * A part with no measurement contributes neither. */
        if (part.lum_min >= 0.0) {
            if (!any || part.lum_min < lo) lo = part.lum_min;
            if (!any || part.lum_max > hi) hi = part.lum_max;
            any = true;
        }

        ink  = seen ? syn_ink_combine(ink,  part.ink)  : part.ink;
        best = seen ? syn_ink_combine(best, part.best) : part.best;
        seen = true;
    }

    if (!seen || area <= 0.0) return;
    out->lum     = sum / area;
    out->lum_min = any ? lo : -1.0;
    out->lum_max = any ? hi : -1.0;
    out->ink     = ink;
    out->best    = best;
}

static void backdrop_export(syn_server_t *s)
{
    /* No synui_owns_seat() guard, deliberately, and it is worth saying why: this
     * is state under XDG_CONFIG_HOME, exactly like the wallpaper.state written a
     * few functions down and the theme.state beside it. A rig keeps out of the
     * live desktop's way by pointing HOME somewhere else — which is what makes
     * this measurable at all, since a headless synui has no session and would
     * fail that check on every run. The guard belongs on the pushes that reach
     * ANOTHER program's config (glass_push, the font picker), and this reaches
     * only the bar reading this HOME. */
    syn_ink_t ink  = SYN_INK_NONE;
    /*
     * The SECOND answer: the better of the two inks even where neither clears
     * the target — see syn_ink_best(). Folded across the monitors exactly like
     * the first, so two screens that want opposite scrims still veto each other.
     *
     * Both are published because they mean different things to the bar: `bar_ink`
     * says "go clear", `bar_ink_best` says "go clear once you have dimmed the
     * backdrop this way". Without the second, the only answer available for the
     * band where neither ink passes was the bar's whole background back, which
     * is what made a clear bar flick to opaque on some wallpapers and not others.
     */
    syn_ink_t best = SYN_INK_NONE;
    bool      seen = false;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        double lum = wallpaper_strip_lum(o);
        syn_ink_t this_one = syn_ink_for_backdrop(lum, CONTRAST_TARGET);
        syn_ink_t this_best = syn_ink_best(lum);
        ink  = seen ? syn_ink_combine(ink, this_one)   : this_one;
        best = seen ? syn_ink_combine(best, this_best) : this_best;
        seen = true;
    }
    if (!seen) { ink = SYN_INK_NONE; best = SYN_INK_NONE; }

    /*
     * The per-output half, and the reason the fold above is no longer the whole
     * answer.
     *
     * ⚠ THERE IS ONE BAR PER SCREEN, SO THE VETO WAS ANSWERING A QUESTION NOBODY
     * ASKED. `bar_ink` folds every monitor into a single ink and hands back NONE
     * when they disagree — correct for ONE surface spanning two screens, and
     * exactly wrong for the bar, which is a separate layer surface on each
     * output with its own strip of its own wallpaper underneath it.
     *
     * Measured on this box: two 0.67 desktops and a television whose wallpaper
     * is letterboxed, so its top row of cells is black. Two screens wanted dark
     * ink, one wanted light, the fold said NONE — and macOS 26 and Prism, the two
     * presets whose whole look is a bar that is not there, put an opaque strip
     * back on ALL THREE. One screen's black band turned the glass off everywhere.
     *
     * So each output publishes its own pair and the bar reads the one for the
     * screen it is on. The folded keys stay, unchanged and still first in the
     * file: they are what a bar older than this reads, and on a single-monitor
     * desktop — which is what every one of them was tested on — the fold and the
     * per-output answer are the same two values.
     */
    /* Per output: TWO grids of SYN_LUM_CELLS (the wallpaper's and the scene's)
     * and one bar strip of SYN_LUM_COLS, six characters a cell ("-1.00," is the
     * longest), times the four monitors this is sized for, plus room for the
     * key names. */
    char grids[(SYN_LUM_CELLS * 2 + SYN_LUM_COLS * 2) * 6 * 4 + 2048];
    size_t gl = 0;
    grids[0] = '\0';
    wl_list_for_each(o, &s->outputs, link) {
        int used = snprintf(grids + gl, sizeof(grids) - gl,
                            "bar_ink.%s=%s\nbar_ink_best.%s=%s\n",
                            o->wlr_output->name,
                            syn_ink_name(syn_ink_for_backdrop(wallpaper_strip_lum(o),
                                                              CONTRAST_TARGET)),
                            o->wlr_output->name,
                            syn_ink_name(syn_ink_best(wallpaper_strip_lum(o))));
        if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
        gl += (size_t)used;
    }

    /*
     * ⛔ WHERE THE INKS END AND THE MEASUREMENTS BEGIN, and the reason the two
     * are told apart at all: everything above is a DECISION and everything
     * below is a READING. The file has to be rewritten whenever either moves —
     * a popup reads the cells, not the bar's answer — but the LOG is a record
     * of decisions, and logging it on a reading turned a line worth seeing into
     * 480 identical ones a minute. See the log at the bottom of this function.
     */
    const size_t ink_len = gl;

    /*
     * The grid, as the text the shell reads it back as. Built before the
     * change test below because it is PART of that test: the two bar inks can
     * sit perfectly still across a wallpaper change that moves every cell — a
     * new picture with the same average darkness at the top — and the start
     * menu's ink keys off the cells, not off the bar's answer.
     *
     * One line per output, named by connector, because the grid is in that
     * output's own 0..1 coordinates and a popup asks about the screen it opened
     * on. Two decimals: the ink flips over a band about 0.05 wide, so the third
     * would be describing differences narrower than the decision it feeds.
     */
    wl_list_for_each(o, &s->outputs, link) {
        int used = snprintf(grids + gl, sizeof(grids) - gl, "grid.%s=",
                            o->wlr_output->name);
        if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
        gl += (size_t)used;
        for (int i = 0; i < SYN_LUM_CELLS && gl + 8 < sizeof(grids); i++) {
            used = snprintf(grids + gl, sizeof(grids) - gl, "%s%.2f",
                            i ? "," : "", wallpaper_lum_grid(o)[i]);
            if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
            gl += (size_t)used;
        }
        if (gl + 2 < sizeof(grids)) { grids[gl++] = '\n'; grids[gl] = '\0'; }
    }

    /*
     * …and the same grid measured off what is ACTUALLY THERE (barscan.c), which
     * is the wallpaper only until a window covers it.
     *
     * The bar's row below is this question asked of one strip, and it came
     * first only because the bar was the first surface a window could get
     * behind. Everything else the shell draws opens where it is put, so every
     * one of them had the same problem the moment it went see-through: a start
     * menu over a dark browser was choosing its ink from the picture the
     * browser covers.
     *
     * ⚠ SAME MEANING OF -1 AS THE BAR'S ROW, NOT AS grid.<output>'S. Here it is
     * "nothing of ours covers this cell" — the ordinary case, and the one where
     * grid.<output>'s cell is the right answer rather than a fallback. In
     * grid.<output> it means the wallpaper could not be measured at all, which
     * is a surface that must keep its background. Two rows, two vocabularies,
     * and a consumer that folds them the wrong way round would put an opaque
     * slab on every desktop with an empty screen.
     *
     * Emitted for every output unconditionally, all -1 included, so that a
     * reader can tell "nothing covers anything" from "this synui does not
     * measure that" — and so that switching `scene_ink` off publishes a row of
     * -1 rather than removing the row, which is the same distinction.
     */
    wl_list_for_each(o, &s->outputs, link) {
        int used = snprintf(grids + gl, sizeof(grids) - gl, "scene.%s=",
                            o->wlr_output->name);
        if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
        gl += (size_t)used;
        for (int i = 0; i < SYN_LUM_CELLS && gl + 8 < sizeof(grids); i++) {
            used = snprintf(grids + gl, sizeof(grids) - gl, "%s%.2f",
                            i ? "," : "", o->scene_lum[i]);
            if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
            gl += (size_t)used;
        }
        if (gl + 2 < sizeof(grids)) { grids[gl++] = '\n'; grids[gl] = '\0'; }
    }

    /*
     * And what is actually UNDER THE BAR, which is the wallpaper only until
     * something covers it — barscan.c, measured off the scene graph.
     *
     * One row of SYN_LUM_COLS, the same columns as the grid above, so the bar
     * can fold the two together cell for cell: a column reading -1 here is one
     * no window covers, and the bar takes the grid's top-row cell for it. That
     * is why this is published as a row of luminances rather than as a row of
     * inks — the fallback has to happen BEFORE the ink is decided, or a column
     * that means "ask the wallpaper" would first have to become an ink and then
     * be talked out of it.
     *
     * Emitted for every output unconditionally, all -1 included: a bar reading
     * a file whose row for its output is missing cannot tell "nothing covers
     * the bar" from "this synui does not measure that", and those want opposite
     * behaviour.
     */
    wl_list_for_each(o, &s->outputs, link) {
        int used = snprintf(grids + gl, sizeof(grids) - gl, "bar_strip.%s=",
                            o->wlr_output->name);
        if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
        gl += (size_t)used;
        for (int i = 0; i < SYN_LUM_COLS && gl + 8 < sizeof(grids); i++) {
            used = snprintf(grids + gl, sizeof(grids) - gl, "%s%.2f",
                            i ? "," : "", o->bar_strip_lum[i]);
            if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
            gl += (size_t)used;
        }
        if (gl + 2 < sizeof(grids)) { grids[gl++] = '\n'; grids[gl] = '\0'; }
    }

    /*
     * …and the WALLPAPER's own answer for that same strip, column by column.
     *
     * ⛔ THIS IS THE ROW THE FALLBACK ABOVE FALLS BACK TO, and until it existed
     * the fallback was wp_lum_grid's TOP ROW — the right columns of the wrong
     * band. A grid row is SYN_LUM_ROWS deep: 160 pixels of a 1440 screen
     * answering for a bar 34 tall, four fifths of it picture the bar is not on.
     * On a photograph that changes vertically inside that cell the two readings
     * are nothing alike — 0.29 for the cell against 0.08 for the strip itself —
     * and a module that took the cell's answer inked for a backdrop nobody
     * could see. It came out black on a black bar with its neighbours white.
     *
     * Same columns, same edge, same rows as bar_ink's own measurement, so the
     * bar can fold this with bar_strip.<output> column for column: -1 there
     * means "nothing covers this column", and THIS is what answers for it.
     *
     * Emitted unconditionally for every output, -1 included, for the same
     * reason bar_strip is: a bar whose row is missing cannot tell "not
     * measurable" from "this synui does not publish that", and the two want
     * opposite behaviour — the second has to fall back to the grid row it
     * always used.
     */
    wl_list_for_each(o, &s->outputs, link) {
        int used = snprintf(grids + gl, sizeof(grids) - gl, "wp_strip.%s=",
                            o->wlr_output->name);
        if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
        gl += (size_t)used;
        const double *cols = wallpaper_strip_cols(o);
        for (int i = 0; i < SYN_LUM_COLS && gl + 8 < sizeof(grids); i++) {
            used = snprintf(grids + gl, sizeof(grids) - gl, "%s%.2f",
                            i ? "," : "", cols[i]);
            if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
            gl += (size_t)used;
        }
        if (gl + 2 < sizeof(grids)) { grids[gl++] = '\n'; grids[gl] = '\0'; }
    }

    /* Written only on a CHANGE. Every relayout repaints, and the bar watches
     * this path — rewriting an identical file would have it reload and repaint
     * itself for each of them. All three values are compared: the safe ink can
     * sit still while the best one moves (a wallpaper drifting across the band),
     * and that move is exactly what changes which way the scrim goes — and the
     * grid can move while both inks hold still. */
    static syn_ink_t last = -1, last_best = -1;
    static char last_grids[sizeof(grids)] = "";
    /* The LOG's own memory, which is not the file's — see the bottom of this
     * function for why they must not be the same test. */
    static syn_ink_t last_logged = -1, last_logged_best = -1;
    if (ink == last && best == last_best && strcmp(grids, last_grids) == 0)
        return;

    char path[256];
    if (!syn_config_path(path, sizeof(path), "backdrop.state")) return;
    syn_config_ensure_dir();

    char tmp[288];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: wallpaper: cannot write '%s': %s",
                tmp, strerror(errno));
        return;
    }
    fprintf(f, "# Generated by synui — which ink a surface with no background of\n"
               "# its own must use to be legible on the wallpaper behind it.\n"
               "#\n"
               "# bar_ink/bar_ink_best answer for the BAR, whose position is a\n"
               "# constant. They are folded across every monitor, so two screens\n"
               "# that want opposite inks veto each other: bar_ink.<output> and\n"
               "# bar_ink_best.<output> are the same question asked of ONE screen,\n"
               "# and are what a bar reads for the output it is on.\n"
               "# grid.<output> answers for everything else: a %dx%d\n"
               "# grid of mean relative luminance over that output, row-major,\n"
               "# in the output's own 0..1 coordinates. A menu opens where the\n"
               "# pointer is, so it folds the cells it actually covers.\n"
               "# -1 in either form means the wallpaper could not be measured.\n"
               "#\n"
               "# scene.<output> and bar_strip.<output> are the rows that are\n"
               "# NOT about the wallpaper: the same %dx%d grid, and %d\n"
               "# luminances across the strip the bar occupies, measured off\n"
               "# what is actually on screen there — the window a menu opened\n"
               "# over, or one under an auto-hiding bar. -1 IN THESE TWO MEANS\n"
               "# NOTHING COVERS THAT CELL, which is the ordinary case and not\n"
               "# a failure: take wp_strip.<output>'s matching column for it.\n"
               "# Both are all -1 while `scene_ink` is off.\n"
               "#\n"
               "# wp_strip.<output> is the WALLPAPER's own answer for that same\n"
               "# strip: %d luminances across the %d logical rows the bar\n"
               "# covers, on the edge it is on. It is what bar_strip.<output>\n"
               "# falls back to, and it is NOT grid.<output>'s top row — a grid\n"
               "# row is a ninth of the screen and the bar is 34 pixels, so that\n"
               "# row answers for four times the picture the bar is standing on.\n"
               "# -1 means the wallpaper could not be measured.\n",
            SYN_LUM_COLS, SYN_LUM_ROWS,
            SYN_LUM_COLS, SYN_LUM_ROWS, SYN_LUM_COLS,
            SYN_LUM_COLS, SYN_BAR_STRIP_LOGICAL);
    fprintf(f, "bar_ink=%s\n", syn_ink_name(ink));
    fprintf(f, "bar_ink_best=%s\n", syn_ink_name(best));
    fputs(grids, f);
    fclose(f);

    /* Renamed rather than written in place, for the same reason theme.json is:
     * the bar watches this and must never read a half-written file. */
    if (rename(tmp, path) != 0) {
        wlr_log(WLR_ERROR, "synui: wallpaper: cannot rename '%s': %s",
                tmp, strerror(errno));
        unlink(tmp);
        return;
    }
    last = ink;
    last_best = best;
    snprintf(last_grids, sizeof(last_grids), "%s", grids);

    /*
     * ⛔ THE FILE IS WRITTEN ON EVERY CHANGE; THE LOG IS NOT.
     *
     * `scene.<output>` is measured off what is actually on screen, so it moves
     * whenever anything does — a clock's minute, a cursor, a video. That is a
     * real change and the bar must have it, so the write above is right to
     * happen. Logging it was not: four INFO lines every time, at the scan's
     * 400 ms, is **480 lines a minute for the length of the session**, every one
     * of them saying what the one before it said. Measured on this desk it was
     * ~3100 journal lines a minute together with barscan's own trace, against a
     * journal already 4 GB on disk.
     *
     * So the log fires on the INK — the decision — and stays quiet while only
     * the readings underneath it move. A line here now means the bar changed
     * how it draws, which is the only reason anybody greps for it.
     *
     * ⚠ THE PER-OUTPUT INKS ARE PART OF THAT TEST, not just the fold. A screen
     * flipping dark while another flips light leaves the fold on NONE both
     * times, and that is exactly the case the per-output pairs exist for.
     */
    static char last_inks[sizeof(grids)] = "";
    bool ink_moved = ink != last_logged || best != last_logged_best ||
                     strncmp(grids, last_inks, ink_len) != 0 ||
                     last_inks[ink_len] != '\0';
    if (!ink_moved) return;
    last_logged = ink;
    last_logged_best = best;
    memcpy(last_inks, grids, ink_len);
    last_inks[ink_len] = '\0';

    /* ⚠ THE FOLDED ANSWER IS NOT WHAT THE BAR DRAWS ANY MORE, so the per-output
     * pairs are logged beside it. A three-monitor desktop reads "none" here and
     * still has a clear bar on two of them, and a line that showed only the fold
     * would look like the bug it is the fix for. */
    wlr_log(WLR_INFO, "synui: wallpaper: bar ink is %s (best %s)",
            syn_ink_name(ink), syn_ink_name(best));
    wl_list_for_each(o, &s->outputs, link)
        wlr_log(WLR_INFO, "synui: wallpaper: bar ink on %s is %s (best %s)",
                o->wlr_output->name,
                syn_ink_name(syn_ink_for_backdrop(wallpaper_strip_lum(o),
                                                  CONTRAST_TARGET)),
                syn_ink_name(syn_ink_best(wallpaper_strip_lum(o))));
}

/* Every cell "not measured". The seed for an output that has no picture to look
 * at, and the value each of the early returns in paint_output() leaves behind —
 * same contract as wp_top_lum's -1, one per cell. */
static void grid_clear(double grid[SYN_LUM_CELLS])
{
    for (int i = 0; i < SYN_LUM_CELLS; i++) grid[i] = -1.0;
}

/* And every cell at one known luminance: the branches that KNOW what is on
 * screen without having painted a picture — the `none` wallpaper's solid rect
 * and the matrix rain's seed — for the same reason they set wp_top_lum rather
 * than leaving it -1. A flat backdrop is measured, it just happens to be
 * measured everywhere at once. */
static void grid_fill(double grid[SYN_LUM_CELLS], double lum)
{
    for (int i = 0; i < SYN_LUM_CELLS; i++) grid[i] = lum;
}

/*
 * The whole picture, as a coarse grid of mean luminances.
 *
 * One pass over the buffer, accumulating into whichever cell each pixel falls
 * in, rather than SYN_LUM_CELLS passes over the sub-rectangles: the buffer is
 * up to 3840x2160 and the strip measurement beside it already walks a slice of
 * the same pixels, so the cost that matters is how many times the image is
 * touched, not the arithmetic per pixel.
 *
 * ⚠ SUBSAMPLED, and that is safe here in a way it would not be for the palette.
 * Luminance is a MEAN, so every fourth pixel in each direction estimates it to
 * far better than the width of the band the ink flips in; a palette is a mode,
 * and dropping pixels can drop the very cluster it exists to find. That is why
 * palette_measure() walks the buffer whole and this does not.
 */
/* The pixels half, for the same reason strip_luminance_px() is one: the live
 * wallpaper arrives as a raw buffer, not a cairo surface. */
static void grid_luminance_px(const unsigned char *data, int w, int h,
                              int stride, double grid[SYN_LUM_CELLS])
{
    grid_clear(grid);
    if (!data || w <= 0 || h <= 0) return;

    /* Every fourth pixel, but never coarser than one sample per cell edge: on a
     * small output four could otherwise step clean over a whole column. */
    int stepx = w / (SYN_LUM_COLS * 4); if (stepx < 1) stepx = 1;
    int stepy = h / (SYN_LUM_ROWS * 4); if (stepy < 1) stepy = 1;

    double sum[SYN_LUM_CELLS] = { 0.0 };
    long   n[SYN_LUM_CELLS]   = { 0 };

    for (int y = 0; y < h; y += stepy) {
        const unsigned char *row = data + (size_t)y * stride;
        int r = (int)((long)y * SYN_LUM_ROWS / h);
        if (r >= SYN_LUM_ROWS) r = SYN_LUM_ROWS - 1;
        for (int x = 0; x < w; x += stepx) {
            int c = (int)((long)x * SYN_LUM_COLS / w);
            if (c >= SYN_LUM_COLS) c = SYN_LUM_COLS - 1;
            /* Premultiplied ARGB32 over an opaque wallpaper, so straight —
             * the same reasoning as strip_luminance() above, which see. */
            const unsigned char *px = row + (size_t)x * 4;
            sum[r * SYN_LUM_COLS + c] += 0.2126 * syn_srgb_lut(px[2]) +
                                         0.7152 * syn_srgb_lut(px[1]) +
                                         0.0722 * syn_srgb_lut(px[0]);
            n[r * SYN_LUM_COLS + c]++;
        }
    }

    for (int i = 0; i < SYN_LUM_CELLS; i++)
        if (n[i] > 0) grid[i] = sum[i] / (double)n[i];
}

static void grid_luminance(cairo_surface_t *dst, double grid[SYN_LUM_CELLS])
{
    grid_clear(grid);

    if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) return;
    if (cairo_image_surface_get_format(dst) != CAIRO_FORMAT_ARGB32) return;

    cairo_surface_flush(dst);
    grid_luminance_px(cairo_image_surface_get_data(dst),
                      cairo_image_surface_get_width(dst),
                      cairo_image_surface_get_height(dst),
                      cairo_image_surface_get_stride(dst), grid);
}

/* Paint (or clear) a single output's wallpaper buffer from the server's
 * currently-decoded source image. Used both when an output first appears
 * and when the layout is reflowed (resize/rotate/move). */
static void paint_output(syn_output_t *o)
{
    syn_server_t *s = o->server;

    /* Cleared up front so every early return below leaves "not measured"
     * rather than the previous wallpaper's answer — and so the calloc'd zero a
     * new output starts life with, which reads as a legitimately black
     * backdrop, is never what backdrop_export() sees.
     *
     * The two branches that go on to SET it rather than fall out of here (no
     * image, and the rain) say why where they do it. Everything else that
     * returns early is a failure — no resolution yet, no buffer — and has
     * genuinely measured nothing. */
    o->wp_top_lum = -1.0;
    /* And the grid, on exactly the same terms and in the same breath — every
     * branch below that answers one of them answers both, so a panel can never
     * be reading the previous wallpaper's backdrop while the bar is not. */
    grid_clear(o->wp_lum_grid);
    /* …and the strip's own columns, which every branch below answers with the
     * other two for that same reason. */
    strip_fill(o->wp_strip_lum, -1.0);

    syn_wallpaper_src_t src;
    const char *path;
    syn_wallpaper_mode_t mode;
    wallpaper_effective(&s->config, o->wlr_output->name, &src, &path, &mode);

    /* The animated matrix backend owns the background instead; keep the
     * static buffer torn down so the two never both paint into
     * wallpaper_tree. */
    if (src == SYN_WP_SRC_MATRIX) {
        if (o->wallpaper_buf) {
            wlr_scene_node_destroy(&o->wallpaper_buf->node);
            o->wallpaper_buf = NULL;
        }
        /* The rain measures its own strip off the GPU buffer it renders into,
         * once there is one (matrix.c). Seeded here with the solid colour
         * because that is what is actually on screen until then — and, if the
         * shader never builds, for good: matrix.c's fallback for every GL
         * failure is to leave the background to bg_rect. Seeding rather than
         * clearing to -1 also keeps a live switch to the rain from publishing
         * one frame of "unmeasured" on its way to the same answer, which the
         * bar would wear as a flash of its opaque background. */
        o->wp_top_lum = solid_backdrop_lum();
        grid_fill(o->wp_lum_grid, o->wp_top_lum);
        strip_fill(o->wp_strip_lum, o->wp_top_lum);
        return;
    }

    cairo_surface_t *img = wallpaper_surface(s, path);
    if (!img) {
        if (o->wallpaper_buf) {
            wlr_scene_node_destroy(&o->wallpaper_buf->node);
            o->wallpaper_buf = NULL;
        }
        /* Nothing painted, so bg_rect is the backdrop — the `none` wallpaper
         * choice, and equally a path that would not decode. NOT under
         * wallpaper-engine, which is an external client painting its own
         * surface over the top of all this: there the compositor genuinely
         * cannot see what the bar is drawn on, and -1 is the honest answer. */
        if (src != SYN_WP_SRC_WPENGINE) {
            o->wp_top_lum = solid_backdrop_lum();
            grid_fill(o->wp_lum_grid, o->wp_top_lum);
            strip_fill(o->wp_strip_lum, o->wp_top_lum);
        }
        return;
    }

    /* Decode/paint at the output's physical pixel size, then tell the scene
     * graph to display it at the logical box size via set_dest_size — this
     * keeps a HiDPI wallpaper crisp instead of being bilinear-upscaled from
     * logical to physical pixels at commit time. */
    int pw, ph;
    wlr_output_transformed_resolution(o->wlr_output, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;

    struct wlr_box box;
    wlr_output_layout_get_box(s->output_layout, o->wlr_output, &box);
    if (box.width <= 0 || box.height <= 0) return;

    cairo_t *cr = NULL;
    struct wlr_buffer *buf = create_cairo_buf(pw, ph, &cr);
    if (!buf) return;

    cairo_begin(cr);
    wallpaper_paint_box(cr, img, pw, ph, mode);

    /* Measured here, off the finished buffer and before the cairo context that
     * owns it goes away. `rows` is the logical strip converted to this output's
     * physical pixels — the buffer is painted at the physical size (above), so
     * on a 2x monitor the bar's 34 logical rows are 68 of these. */
    int rows = (int)lround(SYN_BAR_STRIP_LOGICAL * (double)ph / box.height);
    if (rows < 1) rows = 1;
    o->wp_top_lum = strip_luminance(cairo_get_target(cr), rows,
                                    s->config.bar_edge);
    /* …and the same rows per column, which is what a bar MODULE reads. Off the
     * same buffer in the same breath, so the strip's two answers can never
     * describe different frames. */
    strip_luminance_cols(cairo_get_target(cr), rows, s->config.bar_edge,
                         o->wp_strip_lum);

    /* And the rest of the screen, for every surface that is not the bar. Off
     * the same finished buffer and in the same breath as the strip: two walks
     * of one image beats painting it twice, and it keeps the bar's answer and
     * the panels' answers describing the same frame. */
    grid_luminance(cairo_get_target(cr), o->wp_lum_grid);

    /* And the palette, off the WHOLE image rather than the bar strip — the
     * colour of a wallpaper is not the colour of its top 34 rows, which on a
     * photograph is usually sky.
     *
     * The surface it is corrected against is synui's own panel, not the
     * wallpaper: these colours are drawn ON panels, and correcting them against
     * the thing they are extracted from would be answering the wrong question.
     * theme_panel_surface_lum() is that number. */
    palette_measure(o, cairo_get_target(cr), theme_panel_surface_lum(&s->config));

    cairo_destroy(cr);

    set_scene_buffer(&o->wallpaper_buf, s->wallpaper_tree, buf);
    wlr_scene_buffer_set_dest_size(o->wallpaper_buf, box.width, box.height);
    wlr_scene_node_set_position(&o->wallpaper_buf->node, box.x, box.y);
}

/* ── Public API ──────────────────────────────────────────── */

void wallpaper_init(syn_server_t *s)
{
    s->wallpaper_tree = wlr_scene_tree_create(&s->scene->tree);
    s->wallpaper.src = NULL;
    s->wallpaper.per_n = 0;

    if (s->config.wallpaper[0] != '\0' && wallpaper_global_used(s))
        s->wallpaper.src = wallpaper_decode(s->config.wallpaper);
    wallpaper_cache_fill(s);
}

void wallpaper_output_created(syn_output_t *o)
{
    /* ⚠ SEEDED BEFORE THE FIRST EXPORT, because a calloc'd syn_output_t reads
     * 0.0 here and 0.0 is not "nothing covers this column" — it is BLACK, the
     * one value that would flip a bar to white ink on a wallpaper it never
     * looked at. barscan.c's own scan re-clears this every tick; this is the
     * window between an output appearing and that tick landing. Both arrays,
     * for the same reason: a black grid would tell every menu on the new screen
     * to draw white text on a wallpaper nothing has measured. */
    for (int i = 0; i < SYN_LUM_COLS; i++)  o->bar_strip_lum[i] = -1.0;
    for (int i = 0; i < SYN_LUM_CELLS; i++) o->scene_lum[i]     = -1.0;

    paint_output(o);
    /* A monitor arriving is a monitor the bar has to be legible on too, and it
     * can carry a wallpaper of its own. wallpaper_relayout() covers the reverse
     * (one leaving) — the layout change that follows a disconnect repaints. */
    backdrop_export(o->server);
    palette_export(o->server);

    /* And the LOGIN screen's copy of it.
     *
     * ⚠ HERE RATHER THAN IN output_layout_changed(), which was the first
     * guess and does not fire at startup on a default desktop: a new output
     * reaches dispcfg_outputs_changed(), which returns immediately when the
     * arrangement is EXTEND and the panel is closed — so the publish never
     * ran and the login screen stayed black, which is the bug it was added to
     * fix. An output that has just been painted is the earliest moment the
     * background RESOLVES, since the answer is taken from the primary screen's
     * wallpaper, and it is the same moment on a first monitor as on a
     * fourth. */
    greeterbg_publish(o->server);
}

void wallpaper_output_destroy(syn_output_t *o)
{
    if (o->wallpaper_buf) {
        wlr_scene_node_destroy(&o->wallpaper_buf->node);
        o->wallpaper_buf = NULL;
    }
}

void wallpaper_relayout(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        paint_output(o);
    /* After the loop, not inside it: the published answer is a fold over every
     * monitor, so one written per output would flap through intermediate values
     * on a two-monitor desktop — and the bar watches this file. */
    backdrop_export(s);
    palette_export(s);
}

void wallpaper_backdrop_measured(syn_output_t *o, double lum)
{
    o->wp_top_lum = lum;
    /* The rain's strip is flat across the screen for the reason spelt out for
     * the grid below, so its columns are that one answer too. */
    strip_fill(o->wp_strip_lum, lum);
    /*
     * The rain gets the strip's answer for every cell, which is a stand-in
     * everywhere else in this file but is very nearly exact here: the matrix
     * backend draws the same falling columns over the same black at the same
     * density across the whole output, so one region of it measures like any
     * other. A photograph is the case that needs a real grid; a texture that is
     * statistically flat does not have one to find.
     */
    grid_fill(o->wp_lum_grid, lum);
    /* Straight to the fold, not through paint_output: the caller is a backend
     * that painted the background itself, and repainting the static wallpaper
     * here would tear down the buffer it just drew. backdrop_export writes only
     * on a CHANGE, so a backend that reports every frame costs a compare. */
    backdrop_export(o->server);
    palette_export(o->server);
}

/* Just the export, with no measuring and no painting.
 *
 * barscan.c's input is the SCENE, so what it changes is already on screen by
 * the time it notices — there is nothing to repaint and no picture to re-walk,
 * only a file to bring up to date. Not palette_export() with it: the palette
 * comes off the wallpaper image, which a window moving over the bar does not
 * touch. */
void wallpaper_backdrop_republish(syn_server_t *s)
{
    backdrop_export(s);
}

void wallpaper_reload(syn_server_t *s)
{
    /* `lock_background = desktop` is the default, so a new wallpaper is a new
     * LOGIN screen too — and the greeter cannot read this picture (a home is
     * 0700, and the default wallpaper lives in one), so the copy it reads has
     * to be refreshed here. Cheap: greeterbg_publish compares the source's
     * path, size and mtime and only copies when it has actually changed. */
    greeterbg_publish(s);

    if (s->wallpaper.src) {
        cairo_surface_destroy(s->wallpaper.src);
        s->wallpaper.src = NULL;
    }
    wallpaper_cache_drop(s);

    if (s->config.wallpaper[0] != '\0' && wallpaper_global_used(s))
        s->wallpaper.src = wallpaper_decode(s->config.wallpaper);
    wallpaper_cache_fill(s);

    wallpaper_relayout(s);
}

/* ── Persisted picker choice (~/.config/synui/wallpaper.state) ── */

/* Resolve the state-file path into buf. Returns false if the config dir can't
 * be resolved. Note this file OVERRIDES the synuirc `wallpaper` key on load
 * (config.c applies it last) — that is deliberate: it is what makes a Super+W
 * pick survive a restart. Delete it to hand control back to synuirc. */
static bool wallpaper_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "wallpaper.state");
}

/* Interpret one token the same way the synuirc `wallpaper` key does. */
static void wallpaper_apply_token(syn_config_t *cfg, const char *tok)
{
    if (strcmp(tok, "matrix") == 0) {
        cfg->wallpaper_src = SYN_WP_SRC_MATRIX;
    } else if (strcmp(tok, "default") == 0) {
        cfg->wallpaper_src = SYN_WP_SRC_IMAGE;
        strncpy(cfg->wallpaper, SYNUI_DATADIR "/wallpaper.png",
                sizeof(cfg->wallpaper) - 1);
        cfg->wallpaper[sizeof(cfg->wallpaper) - 1] = '\0';
    } else if (strcmp(tok, "none") == 0) {
        cfg->wallpaper_src = SYN_WP_SRC_IMAGE;
        cfg->wallpaper[0] = '\0';
    } else {
        cfg->wallpaper_src = SYN_WP_SRC_IMAGE;
        strncpy(cfg->wallpaper, tok, sizeof(cfg->wallpaper) - 1);
        cfg->wallpaper[sizeof(cfg->wallpaper) - 1] = '\0';
    }
}

/* Name → syn_wallpaper_mode_t, or -1. Shared by the state loader and the
 * synuirc parser's per-output key. */
int wallpaper_mode_from_name(const char *name)
{
    for (int m = 0; m < SYN_WALLPAPER_MODE_COUNT; m++)
        if (strcmp(name, syn_wallpaper_mode_names[m]) == 0)
            return m;
    return -1;
}

/* Apply one token to a per-monitor override, creating it if needed. Mirrors
 * wallpaper_apply_token's keyword handling against the override's own fields.
 * `mode` < 0 leaves the entry's scaling mode alone. */
void wallpaper_output_apply(syn_config_t *cfg, const char *name,
                            const char *tok, int mode)
{
    syn_wp_output_t *e = wallpaper_output_entry(cfg, name, true);
    if (!e) return;   /* table full — already logged */

    if (tok) {
        if (strcmp(tok, "matrix") == 0) {
            e->src = SYN_WP_SRC_MATRIX;
        } else if (strcmp(tok, "default") == 0) {
            e->src = SYN_WP_SRC_IMAGE;
            snprintf(e->path, sizeof(e->path), "%s", SYNUI_DATADIR "/wallpaper.png");
        } else if (strcmp(tok, "none") == 0) {
            e->src = SYN_WP_SRC_IMAGE;
            e->path[0] = '\0';
        } else {
            e->src = SYN_WP_SRC_IMAGE;
            snprintf(e->path, sizeof(e->path), "%s", tok);
        }
    }
    if (mode >= 0 && mode < SYN_WALLPAPER_MODE_COUNT)
        e->mode = (syn_wallpaper_mode_t)mode;
}

void wallpaper_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!wallpaper_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no persisted choice — synuirc stands */

    /* Line-typed rather than positional: the file started as "token, then an
     * optional `mode <name>`" and now also carries `output <NAME> …` lines, so
     * dispatching on the leading keyword is what keeps a state file written by
     * any of those versions loadable. An unrecognised line is skipped, not
     * fatal. */
    char line[320];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;

        if (strncmp(p, "mode ", 5) == 0) {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            int m = wallpaper_mode_from_name(p);
            if (m >= 0) cfg->wallpaper_mode = (syn_wallpaper_mode_t)m;
            continue;
        }

        /* "output <NAME> <token>" / "output <NAME> mode <name>" */
        if (strncmp(p, "output ", 7) == 0) {
            p += 7;
            while (*p == ' ' || *p == '\t') p++;
            char *sp = strchr(p, ' ');
            if (!sp) continue;
            *sp = '\0';
            char *val = sp + 1;
            while (*val == ' ' || *val == '\t') val++;
            if (!*val) continue;

            if (strncmp(val, "mode ", 5) == 0) {
                val += 5;
                while (*val == ' ' || *val == '\t') val++;
                wallpaper_output_apply(cfg, p, NULL, wallpaper_mode_from_name(val));
            } else {
                wallpaper_output_apply(cfg, p, val, -1);
            }
            continue;
        }

        wallpaper_apply_token(cfg, p);
    }
    fclose(f);
}

void wallpaper_state_save(syn_server_t *s)
{
    char path[256];
    if (!wallpaper_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: wallpaper: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    /* Persist a token round-trippable by wallpaper_apply_token(). "default"
     * is stored as the resolved bundled path, so a bare path is fine. */
    if (s->config.wallpaper_src == SYN_WP_SRC_MATRIX)
        fputs("matrix\n", f);
    else if (s->config.wallpaper[0] == '\0')
        fputs("none\n", f);
    else
        fprintf(f, "%s\n", s->config.wallpaper);
    /* Second line: the picker's scaling choice. wallpaper_state_load() ignores
     * this line if absent, so a state file written by an older synui still
     * loads. */
    if (s->config.wallpaper_mode >= 0 &&
        s->config.wallpaper_mode < SYN_WALLPAPER_MODE_COUNT)
        fprintf(f, "mode %s\n",
                syn_wallpaper_mode_names[s->config.wallpaper_mode]);

    /* Then one pair of lines per monitor that overrides the global choice.
     * These are written for every override, including ones for monitors that
     * are not currently connected: an unplugged monitor must get its own
     * wallpaper back when it returns, not silently inherit the global one
     * because a save happened while it was dark. */
    for (int i = 0; i < s->config.wallpaper_out_n; i++) {
        const syn_wp_output_t *e = &s->config.wallpaper_out[i];

        if (e->src == SYN_WP_SRC_MATRIX)
            fprintf(f, "output %s matrix\n", e->output);
        else if (e->path[0] == '\0')
            fprintf(f, "output %s none\n", e->output);
        else
            fprintf(f, "output %s %s\n", e->output, e->path);

        if (e->mode >= 0 && e->mode < SYN_WALLPAPER_MODE_COUNT)
            fprintf(f, "output %s mode %s\n", e->output,
                    syn_wallpaper_mode_names[e->mode]);
    }
    fclose(f);
}
