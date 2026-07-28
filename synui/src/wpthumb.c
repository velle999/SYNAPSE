/*
 * wpthumb.c — thumbnails for the wallpaper picker
 *
 * The Super+W panel lists built-in wallpapers, images found on disk and every
 * Steam Workshop subscription. Titles alone do not tell you what any of them
 * look like, and a Workshop title is whatever its publisher typed — "Sky",
 * "final2", ".". So the panel draws the highlighted entry.
 *
 * This matters more than it did. Highlighting a Workshop row has always been
 * deferred to Enter (starting the engine is a GPU process), and highlighting
 * anything else is now deferred too whenever a Workshop wallpaper is on screen
 * — see wppick.c. So for those rows the thumbnail is the ONLY way to see what
 * you are about to pick before committing to it.
 *
 * One image is decoded per highlight move, not one per row: the panel shows a
 * single preview pane rather than a thumbnail on all ten visible rows, which
 * keeps a 139-entry Workshop list from costing 139 decodes to scroll through.
 * A small cache absorbs arrowing back and forth over the same few entries.
 *
 * Formats: PNG and JPEG via the same decoders the wallpaper itself uses, and
 * GIF via giflib. GIF is not optional — 62 of the 139 Workshop items here name
 * a .gif preview, so without it nearly half the list previews blank, which
 * reads as the feature being broken rather than as a missing codec.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <gif_lib.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Decoded thumbnails held at once. Arrowing up and down the same stretch of
 * the list is the motion to absorb; anything larger just holds decoded pixmaps
 * for rows nobody is looking at. */
#define WPTHUMB_CACHE_MAX 8

/* Long edge the decode is scaled to. The preview pane is drawn smaller than
 * this; the headroom keeps the downscale sharp if the pane ever grows, and
 * caps a 4K preview.jpg at something reasonable to hold eight of. */
#define WPTHUMB_MAX_EDGE 480

typedef struct {
    char             path[512];
    time_t           mtime;      /* re-decode when the file is replaced */
    cairo_surface_t *surf;       /* NULL = decode failed; cached to stop retries */
    unsigned         used;       /* LRU stamp */
} wpthumb_entry_t;

static wpthumb_entry_t cache[WPTHUMB_CACHE_MAX];
static unsigned        cache_clock = 0;

/* ── decode ──────────────────────────────────────────────── */

static cairo_surface_t *decode_png(const char *path)
{
    cairo_surface_t *surf = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return NULL;
    }
    return surf;
}

/* First frame of a GIF into an ARGB32 surface.
 *
 * Only the first frame: these are preview loops, the panel redraws on keypress
 * rather than on a timer, and animating one would mean holding a decode context
 * and a frame timer for something the user is about to arrow past.
 *
 * The first frame is also the honest one to show — a GIF preview of an animated
 * wallpaper opens on the wallpaper as it appears at rest.
 */
static cairo_surface_t *decode_gif(const char *path)
{
    int err = 0;
    GifFileType *gif = DGifOpenFileName(path, &err);
    if (!gif) return NULL;

    cairo_surface_t *surf = NULL;
    GifRowType     *rows  = NULL;

    /* DGifSlurp reads every frame. The alternative (walking the record types by
     * hand to stop after one) is a lot more code for a saving that does not
     * matter on a file this size, and it is the path giflib's own utilities
     * take. */
    if (DGifSlurp(gif) != GIF_OK || gif->ImageCount < 1) goto out;

    const SavedImage *img = &gif->SavedImages[0];
    const int iw = img->ImageDesc.Width, ih = img->ImageDesc.Height;
    if (iw <= 0 || ih <= 0) goto out;

    /* A frame carries its own palette when it has one; otherwise the file's. A
     * GIF with neither is malformed, and drawing it as garbage would be worse
     * than drawing nothing. */
    const ColorMapObject *cmap = img->ImageDesc.ColorMap
                               ? img->ImageDesc.ColorMap : gif->SColorMap;
    if (!cmap) goto out;

    /* Transparency is per-frame, in a graphics-control extension. Without
     * honouring it, every preview with a transparent background comes out with
     * a block of whatever palette index 0 happens to be. */
    int transparent = -1;
    for (int i = 0; i < img->ExtensionBlockCount; i++) {
        const ExtensionBlock *e = &img->ExtensionBlocks[i];
        if (e->Function == GRAPHICS_EXT_FUNC_CODE && e->ByteCount >= 4 &&
            (e->Bytes[0] & 0x01))
            transparent = (unsigned char)e->Bytes[3];
    }

    surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        surf = NULL;
        goto out;
    }

    cairo_surface_flush(surf);
    unsigned char *data   = cairo_image_surface_get_data(surf);
    const int      stride = cairo_image_surface_get_stride(surf);

    for (int y = 0; y < ih; y++) {
        uint32_t *out = (uint32_t *)(data + (size_t)y * stride);
        const GifByteType *in = img->RasterBits + (size_t)y * iw;
        for (int x = 0; x < iw; x++) {
            int idx = in[x];
            if (idx == transparent || idx >= cmap->ColorCount) {
                out[x] = 0;         /* fully transparent, premultiplied */
                continue;
            }
            const GifColorType *c = &cmap->Colors[idx];
            /* CAIRO_FORMAT_ARGB32 is native-endian premultiplied ARGB; at
             * alpha 255 premultiplication is the identity. */
            out[x] = 0xff000000u | ((uint32_t)c->Red   << 16)
                                 | ((uint32_t)c->Green <<  8)
                                 |  (uint32_t)c->Blue;
        }
    }
    cairo_surface_mark_dirty(surf);

out:
    (void)rows;
    DGifCloseFile(gif, &err);
    return surf;
}

/* Scale to fit inside WPTHUMB_MAX_EDGE, preserving aspect. Returns the
 * original when it already fits, so a small preview is not blown up here and
 * then drawn down again by the panel. */
static cairo_surface_t *shrink(cairo_surface_t *src)
{
    const int w = cairo_image_surface_get_width(src);
    const int h = cairo_image_surface_get_height(src);
    if (w <= 0 || h <= 0) return src;
    if (w <= WPTHUMB_MAX_EDGE && h <= WPTHUMB_MAX_EDGE) return src;

    const double sc = (double)WPTHUMB_MAX_EDGE / (w > h ? w : h);
    const int tw = (int)(w * sc + 0.5), th = (int)(h * sc + 0.5);

    cairo_surface_t *dst =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw < 1 ? 1 : tw,
                                                        th < 1 ? 1 : th);
    if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(dst);
        return src;
    }

    cairo_t *cr = cairo_create(dst);
    cairo_scale(cr, sc, sc);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);

    cairo_surface_destroy(src);
    return dst;
}

static cairo_surface_t *decode_any(const char *path)
{
    const char *dot = strrchr(path, '.');
    cairo_surface_t *surf = NULL;

    /* Extension first, then the other decoders: Workshop previews are named by
     * project.json and a publisher who writes "preview.jpg" over a PNG is not
     * a case worth failing on. */
    if (dot && strcasecmp(dot, ".png") == 0)                       surf = decode_png(path);
    else if (dot && (strcasecmp(dot, ".jpg")  == 0 ||
                     strcasecmp(dot, ".jpeg") == 0))               surf = syn_decode_jpeg(path);
    else if (dot && strcasecmp(dot, ".gif") == 0)                  surf = decode_gif(path);

    if (!surf) surf = decode_png(path);
    if (!surf) surf = syn_decode_jpeg(path);
    if (!surf) surf = decode_gif(path);

    return surf ? shrink(surf) : NULL;
}

/* ── cache ───────────────────────────────────────────────── */

void wpthumb_clear(void)
{
    for (int i = 0; i < WPTHUMB_CACHE_MAX; i++) {
        if (cache[i].surf) cairo_surface_destroy(cache[i].surf);
        cache[i].surf    = NULL;
        cache[i].path[0] = '\0';
        cache[i].used    = 0;
    }
}

cairo_surface_t *wpthumb_get(const char *path)
{
    if (!path || !*path) return NULL;

    struct stat st;
    const time_t mtime = stat(path, &st) == 0 ? st.st_mtime : 0;

    int slot = -1;
    for (int i = 0; i < WPTHUMB_CACHE_MAX; i++) {
        if (!cache[i].path[0]) { if (slot < 0) slot = i; continue; }
        if (strcmp(cache[i].path, path) != 0) continue;

        /* Same path, newer file: a wallpaper replaced in place must not keep
         * showing the picture it used to be. */
        if (cache[i].mtime != mtime) {
            if (cache[i].surf) cairo_surface_destroy(cache[i].surf);
            cache[i].surf = decode_any(path);
            cache[i].mtime = mtime;
        }
        cache[i].used = ++cache_clock;
        return cache[i].surf;
    }

    if (slot < 0) {                       /* evict the least recently used */
        slot = 0;
        for (int i = 1; i < WPTHUMB_CACHE_MAX; i++)
            if (cache[i].used < cache[slot].used) slot = i;
        if (cache[slot].surf) cairo_surface_destroy(cache[slot].surf);
        cache[slot].surf = NULL;
    }

    snprintf(cache[slot].path, sizeof(cache[slot].path), "%s", path);
    cache[slot].mtime = mtime;
    /* A NULL result is cached too. Without that, a row whose preview cannot be
     * decoded retries the decode on every single repaint of the panel. */
    cache[slot].surf  = decode_any(path);
    cache[slot].used  = ++cache_clock;

    if (!cache[slot].surf)
        wlr_log(WLR_DEBUG, "synui: wpthumb: no preview from '%s'", path);

    return cache[slot].surf;
}
