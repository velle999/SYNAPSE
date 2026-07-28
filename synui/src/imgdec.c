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
