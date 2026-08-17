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

#include <scenefx/types/wlr_scene.h>
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
static double strip_luminance(cairo_surface_t *dst, int rows, syn_bar_edge_t edge)
{
    if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) return -1.0;
    if (cairo_image_surface_get_format(dst) != CAIRO_FORMAT_ARGB32) return -1.0;

    cairo_surface_flush(dst);
    const unsigned char *data = cairo_image_surface_get_data(dst);
    if (!data) return -1.0;

    int w      = cairo_image_surface_get_width(dst);
    int h      = cairo_image_surface_get_height(dst);
    int stride = cairo_image_surface_get_stride(dst);
    if (w <= 0 || h <= 0 || rows <= 0) return -1.0;
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
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (o->wp_palette.ok) return &o->wp_palette;
    return NULL;
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

    /* Written only on a CHANGE, exactly like backdrop.state: the bar watches
     * this path and every relayout would otherwise have it reload. */
    static syn_palette_t last;
    static bool have_last = false;
    syn_palette_t now;
    memset(&now, 0, sizeof(now));
    if (p) now = *p;
    if (have_last && memcmp(&now, &last, sizeof(now)) == 0) return;

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
    fprintf(f, "# Generated by synui — the small palette SYNAPSE Prism takes\n"
               "# off the wallpaper. Hand edits are overwritten on the next\n"
               "# wallpaper change; set `theme = ` to something else to stop it.\n");
    /* `ok=no` is published rather than the file being deleted or left stale:
     * "this wallpaper has no colour to give" is an answer the bar has to be
     * able to act on, and an absent file is indistinguishable from a synui too
     * old to write one. */
    fprintf(f, "ok=%s\n", now.ok ? "yes" : "no");
    if (now.ok) {
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
    have_last = true;

    /* And into the running desktop. Published first, applied second: the file
     * is what the bar and the widgets read, and this is what synui's own panels
     * read — a failure to write the file must not stop the compositor's own
     * colours from following the wallpaper. */
    theme_refresh_wallpaper_accent(s);

    if (now.ok)
        wlr_log(WLR_INFO, "synui: palette: accent #%02X%02X%02X off the wallpaper",
                (int)lround(now.accent[0] * 255.0),
                (int)lround(now.accent[1] * 255.0),
                (int)lround(now.accent[2] * 255.0));
    else
        wlr_log(WLR_INFO, "synui: palette: the wallpaper has no usable hue — "
                          "the theme's own accent stands");
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
 */
void wallpaper_backdrop_for_box(syn_server_t *s, const struct wlr_box *box,
                                double target, syn_backdrop_t *out)
{
    out->lum  = -1.0;
    out->ink  = SYN_INK_NONE;
    out->best = SYN_INK_NONE;
    if (!s || !box) return;

    double sum  = 0.0;
    double area = 0.0;
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

        syn_backdrop_t part;
        syn_backdrop_for_box(o->wp_lum_grid,
                             (double)(ix0 - ob.x) / ob.width,
                             (double)(iy0 - ob.y) / ob.height,
                             (double)(ix1 - ix0)  / ob.width,
                             (double)(iy1 - iy0)  / ob.height,
                             target, &part);

        /* Weighted by how much of the panel is on this screen, so the mean is
         * the mean over the panel rather than over the monitors it touches. */
        double w = (double)(ix1 - ix0) * (double)(iy1 - iy0);
        if (part.lum >= 0.0) { sum += part.lum * w; area += w; }

        ink  = seen ? syn_ink_combine(ink,  part.ink)  : part.ink;
        best = seen ? syn_ink_combine(best, part.best) : part.best;
        seen = true;
    }

    if (!seen || area <= 0.0) return;
    out->lum  = sum / area;
    out->ink  = ink;
    out->best = best;
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
        syn_ink_t this_one = syn_ink_for_backdrop(o->wp_top_lum, CONTRAST_TARGET);
        syn_ink_t this_best = syn_ink_best(o->wp_top_lum);
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
    char grids[SYN_LUM_CELLS * 6 * 4 + 2048];
    size_t gl = 0;
    grids[0] = '\0';
    wl_list_for_each(o, &s->outputs, link) {
        int used = snprintf(grids + gl, sizeof(grids) - gl,
                            "bar_ink.%s=%s\nbar_ink_best.%s=%s\n",
                            o->wlr_output->name,
                            syn_ink_name(syn_ink_for_backdrop(o->wp_top_lum,
                                                              CONTRAST_TARGET)),
                            o->wlr_output->name,
                            syn_ink_name(syn_ink_best(o->wp_top_lum)));
        if (used < 0 || (size_t)used >= sizeof(grids) - gl) break;
        gl += (size_t)used;
    }

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
                            i ? "," : "", o->wp_lum_grid[i]);
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
               "# -1 in either form means the wallpaper could not be measured.\n",
            SYN_LUM_COLS, SYN_LUM_ROWS);
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
    /* ⚠ THE FOLDED ANSWER IS NOT WHAT THE BAR DRAWS ANY MORE, so the per-output
     * pairs are logged beside it. A three-monitor desktop reads "none" here and
     * still has a clear bar on two of them, and a line that showed only the fold
     * would look like the bug it is the fix for. */
    wlr_log(WLR_INFO, "synui: wallpaper: bar ink is %s (best %s)",
            syn_ink_name(ink), syn_ink_name(best));
    wl_list_for_each(o, &s->outputs, link)
        wlr_log(WLR_INFO, "synui: wallpaper: bar ink on %s is %s (best %s)",
                o->wlr_output->name,
                syn_ink_name(syn_ink_for_backdrop(o->wp_top_lum, CONTRAST_TARGET)),
                syn_ink_name(syn_ink_best(o->wp_top_lum)));
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
static void grid_luminance(cairo_surface_t *dst, double grid[SYN_LUM_CELLS])
{
    grid_clear(grid);

    if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) return;
    if (cairo_image_surface_get_format(dst) != CAIRO_FORMAT_ARGB32) return;

    cairo_surface_flush(dst);
    const unsigned char *data = cairo_image_surface_get_data(dst);
    if (!data) return;

    int w      = cairo_image_surface_get_width(dst);
    int h      = cairo_image_surface_get_height(dst);
    int stride = cairo_image_surface_get_stride(dst);
    if (w <= 0 || h <= 0) return;

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
    paint_output(o);
    /* A monitor arriving is a monitor the bar has to be legible on too, and it
     * can carry a wallpaper of its own. wallpaper_relayout() covers the reverse
     * (one leaving) — the layout change that follows a disconnect repaints. */
    backdrop_export(o->server);
    palette_export(o->server);
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

void wallpaper_reload(syn_server_t *s)
{
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
