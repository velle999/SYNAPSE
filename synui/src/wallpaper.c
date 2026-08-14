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

#include <cairo.h>
#include <jpeglib.h>

#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>

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

/* Paint (or clear) a single output's wallpaper buffer from the server's
 * currently-decoded source image. Used both when an output first appears
 * and when the layout is reflowed (resize/rotate/move). */
static void paint_output(syn_output_t *o)
{
    syn_server_t *s = o->server;

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
        return;
    }

    cairo_surface_t *img = wallpaper_surface(s, path);
    if (!img) {
        if (o->wallpaper_buf) {
            wlr_scene_node_destroy(&o->wallpaper_buf->node);
            o->wallpaper_buf = NULL;
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
