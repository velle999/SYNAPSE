/*
 * imgdec.c — JPEG decode, shared by the wallpaper and the picker thumbnails
 *
 * Split out of wallpaper.c when wpthumb.c needed the same decoder. The PNG
 * decoders elsewhere in the tree are duplicated on purpose (each is a five-line
 * cairo wrapper), but this one carries a libjpeg error manager and a longjmp,
 * and a second copy of that is a second place for a corrupt JPEG to take the
 * compositor down.
 *
 * Keeping it free of compositor state is what lets the wpthumb test link it
 * without pulling in the whole of wallpaper.c.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>    /* uint32_t — the transpose moves whole pixels */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>
#include <jpeglib.h>
#include <wlr/util/log.h>

#include "synui.h"

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

cairo_surface_t *syn_decode_jpeg(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        wlr_log(WLR_ERROR, "synui: imgdec: cannot open '%s': %s",
                path, strerror(errno));
        return NULL;
    }

    struct jpeg_decompress_struct cinfo;
    struct wp_jpeg_err jerr;
    unsigned char *data = NULL;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = wp_jpeg_error_exit;
    if (setjmp(jerr.jb)) {
        wlr_log(WLR_ERROR, "synui: imgdec: JPEG decode failed for '%s'", path);
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

/* ── Box blur ────────────────────────────────────────────────
 *
 * For the lock screen background: a wallpaper behind a clock panel has to be
 * quiet enough to read a password over, and dimming alone leaves a busy photo
 * busy. Three box passes approximate a Gaussian well enough for something
 * nobody is looking at directly, at a fraction of the cost.
 *
 * Separable — a horizontal pass then a vertical one per iteration — so the
 * work is O(w*h) per pass regardless of radius rather than O(w*h*r²). That
 * matters: this runs on a 3840×2160 surface at a radius of 24.
 *
 * Operates on the premultiplied ARGB32/RGB24 bytes directly. Blurring
 * premultiplied channels is correct: the blur is linear and premultiplication
 * is a per-pixel scale, so the two commute.
 */

/* One separable pass over `src` into `dst`, horizontally.
 *
 * `src` and `dst` MUST NOT alias: the running sum subtracts the pixel at
 * x - radius, which in an in-place pass would already have been overwritten
 * with a blurred value. Every caller below passes two distinct buffers. */
static void blur_pass(const unsigned char *src, unsigned char *dst,
                      int w, int h, int src_stride, int dst_stride, int radius)
{
    const int span = radius * 2 + 1;

    for (int y = 0; y < h; y++) {
        const unsigned char *srow = src + (size_t)y * src_stride;
        unsigned char *drow = dst + (size_t)y * dst_stride;

        /* Running sum per channel. Seeded with the leftmost pixel repeated
         * `radius` times, which is a clamp-to-edge boundary — the alternative,
         * treating off-image as transparent black, draws a dark halo around
         * every edge of the wallpaper. */
        int sum[4] = { 0, 0, 0, 0 };
        for (int c = 0; c < 4; c++) sum[c] += srow[c] * (radius + 1);
        for (int x = 1; x <= radius; x++) {
            int sx = x < w ? x : w - 1;
            for (int c = 0; c < 4; c++) sum[c] += srow[sx * 4 + c];
        }

        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 4; c++) drow[x * 4 + c] = (unsigned char)(sum[c] / span);

            int add = x + radius + 1; if (add > w - 1) add = w - 1;
            int sub = x - radius;     if (sub < 0)     sub = 0;
            for (int c = 0; c < 4; c++)
                sum[c] += srow[add * 4 + c] - srow[sub * 4 + c];
        }
    }
}

/* Transpose w×h premultiplied pixels. Blurring vertically is the horizontal
 * pass over the transpose, which keeps the inner loop walking memory forwards
 * in both directions — a strided vertical pass over a 4K surface misses cache
 * on essentially every access. */
static void blur_transpose(const unsigned char *src, unsigned char *dst,
                           int w, int h, int src_stride, int dst_stride)
{
    for (int y = 0; y < h; y++) {
        const uint32_t *srow = (const uint32_t *)(src + (size_t)y * src_stride);
        for (int x = 0; x < w; x++)
            *(uint32_t *)(dst + (size_t)x * dst_stride + (size_t)y * 4) = srow[x];
    }
}

void syn_surface_blur(cairo_surface_t *surf, int radius)
{
    if (!surf || radius <= 0) return;
    if (cairo_surface_get_type(surf) != CAIRO_SURFACE_TYPE_IMAGE) return;

    cairo_format_t fmt = cairo_image_surface_get_format(surf);
    if (fmt != CAIRO_FORMAT_ARGB32 && fmt != CAIRO_FORMAT_RGB24) return;

    /* Any pending drawing has to land in the pixels before they are read. */
    cairo_surface_flush(surf);

    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    int stride = cairo_image_surface_get_stride(surf);
    unsigned char *data = cairo_image_surface_get_data(surf);
    if (!data || w <= 0 || h <= 0) return;

    /* A radius past half the image blurs it to a flat colour and costs the
     * same as one that does something. */
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius <= 0) return;

    /* Three scratch buffers: one w×h for the horizontal pass, and a pair of
     * transposed h×w ones so the vertical pass never blurs in place (see
     * blur_pass). Tight strides, since nothing but this code reads them. */
    int t_stride = h * 4;
    unsigned char *tmp    = malloc((size_t)stride * h);
    unsigned char *trans  = malloc((size_t)t_stride * w);
    unsigned char *trans2 = malloc((size_t)t_stride * w);
    if (!tmp || !trans || !trans2) {
        free(tmp); free(trans); free(trans2);
        return;
    }

    for (int pass = 0; pass < 3; pass++) {
        /* Horizontal: data → tmp, then back. */
        blur_pass(data, tmp, w, h, stride, stride, radius);
        memcpy(data, tmp, (size_t)stride * h);

        /* Vertical = transpose, blur horizontally into the second transposed
         * buffer, transpose back. */
        blur_transpose(data, trans, w, h, stride, t_stride);
        blur_pass(trans, trans2, h, w, t_stride, t_stride, radius);
        blur_transpose(trans2, data, h, w, t_stride, stride);
    }

    free(tmp);
    free(trans);
    free(trans2);

    /* Tell cairo the pixels changed behind its back, or a cached copy of the
     * un-blurred image is what actually gets painted. */
    cairo_surface_mark_dirty(surf);
}
