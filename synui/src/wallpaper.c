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
 * SynapseOS Project — GPLv2
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

#include <wlr/types/wlr_scene.h>
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

/* ── JPEG decode (libjpeg-turbo) ─────────────────────────── */

struct wp_jpeg_err {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};

static void wp_jpeg_error_exit(j_common_ptr cinfo)
{
    /* libjpeg's default error manager calls exit(1) here, which would take
     * the whole compositor down on a single corrupt JPEG. Long-jump out
     * instead so the caller can fail closed. */
    struct wp_jpeg_err *e = (struct wp_jpeg_err *)cinfo->err;
    longjmp(e->jb, 1);
}

static cairo_surface_t *decode_jpeg(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        wlr_log(WLR_ERROR, "synui: wallpaper: cannot open '%s': %s",
                path, strerror(errno));
        return NULL;
    }

    struct jpeg_decompress_struct cinfo;
    struct wp_jpeg_err jerr;
    unsigned char *data = NULL;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = wp_jpeg_error_exit;
    if (setjmp(jerr.jb)) {
        wlr_log(WLR_ERROR, "synui: wallpaper: JPEG decode failed for '%s'", path);
        jpeg_destroy_decompress(&cinfo);
        free(data);
        fclose(fp);
        return NULL;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);

    /* JCS_EXT_BGRA decodes straight into 32-bit BGRA words with alpha forced
     * to 0xFF — on little-endian this is bit-for-bit identical to cairo's
     * CAIRO_FORMAT_RGB24 packing, so no manual RGB->ARGB swizzle is needed.
     * Must be set after jpeg_read_header(), before jpeg_start_decompress(). */
    cinfo.out_color_space = JCS_EXT_BGRA;
    jpeg_start_decompress(&cinfo);

    int w = (int)cinfo.output_width, h = (int)cinfo.output_height;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, w);
    data = malloc((size_t)stride * (size_t)h);
    if (!data) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *row = data + (size_t)cinfo.output_scanline * stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);

    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_RGB24, w, h, stride);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        free(data);
        return NULL;
    }
    /* Tie the decoded buffer's lifetime to the surface so a plain
     * cairo_surface_destroy() frees it. */
    static cairo_user_data_key_t data_key;
    cairo_surface_set_user_data(surf, &data_key, data, free);
    return surf;
}

/* ── Format detection + top-level decode ────────────────────── */

static cairo_surface_t *wallpaper_decode(const char *path)
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
        surf = decode_jpeg(resolved);
    } else {
        wlr_log(WLR_ERROR, "synui: wallpaper: '%s' is not a recognized PNG/JPEG",
                resolved);
    }
    free(resolved);
    return surf;
}

/* ── Scaling + paint ─────────────────────────────────────── */

static void wallpaper_paint_box(cairo_t *cr, cairo_surface_t *src,
                                 int dst_w, int dst_h,
                                 syn_wallpaper_mode_t mode)
{
    int sw = cairo_image_surface_get_width(src);
    int sh = cairo_image_surface_get_height(src);
    if (sw <= 0 || sh <= 0) return;

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

/* Paint (or clear) a single output's wallpaper buffer from the server's
 * currently-decoded source image. Used both when an output first appears
 * and when the layout is reflowed (resize/rotate/move). */
static void paint_output(syn_output_t *o)
{
    syn_server_t *s = o->server;

    if (!s->wallpaper.src) {
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
    wallpaper_paint_box(cr, s->wallpaper.src, pw, ph, s->config.wallpaper_mode);
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

    if (s->config.wallpaper[0] != '\0')
        s->wallpaper.src = wallpaper_decode(s->config.wallpaper);
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
    if (s->config.wallpaper[0] != '\0')
        s->wallpaper.src = wallpaper_decode(s->config.wallpaper);

    wallpaper_relayout(s);
}
