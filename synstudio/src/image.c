/* image.c — the pixel buffer and resampling.
 *
 * One format everywhere inside the engine: interleaved float32 RGBA, straight
 * alpha, scene-referred linear. Every decoder converts to it and every encoder
 * converts out of it, so no op in the pipeline ever asks what the file was.
 *
 * Resampling is separable and picks its filter from the DIRECTION: area
 * averaging when shrinking, bilinear when growing. A bilinear downscale to a
 * preview size samples a handful of source pixels and drops the rest, which
 * aliases — and aliasing in a preview is worse than slow, because it makes
 * sharpening and noise reduction impossible to judge at the size the user is
 * actually looking at.
 */
#include "synstudio.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

int ss_image_init(ss_image *im, int w, int h)
{
    size_t n;

    im->px = NULL; im->w = 0; im->h = 0;
    if (w <= 0 || h <= 0) return -1;
    /* Refuse a size whose byte count would wrap. */
    if ((size_t)w > (size_t)-1 / 4 / sizeof(float) / (size_t)h) return -1;

    n = (size_t)w * (size_t)h * 4u * sizeof(float);
    im->px = malloc(n);
    if (!im->px) return -1;
    memset(im->px, 0, n);
    im->w = w; im->h = h;
    return 0;
}

void ss_image_free(ss_image *im)
{
    if (!im) return;
    free(im->px);
    im->px = NULL; im->w = im->h = 0;
}

int ss_image_copy(ss_image *dst, const ss_image *src)
{
    if (ss_image_init(dst, src->w, src->h) != 0) return -1;
    memcpy(dst->px, src->px, (size_t)src->w * src->h * 4u * sizeof(float));
    return 0;
}

int ss_image_scale(ss_image *dst, const ss_image *src, int w, int h)
{
    int x, y, c;
    float sx, sy;

    if (ss_image_init(dst, w, h) != 0) return -1;
    sx = (float)src->w / w;
    sy = (float)src->h / h;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            float *o = dst->px + ((size_t)y * w + x) * 4;

            if (sx >= 1.0f || sy >= 1.0f) {
                /* Area average over the source rectangle this pixel covers. */
                int x0 = (int)(x * sx), x1 = (int)((x + 1) * sx);
                int y0 = (int)(y * sy), y1 = (int)((y + 1) * sy);
                float acc[4] = {0,0,0,0};
                int n = 0, ix, iy;

                if (x1 <= x0) x1 = x0 + 1;
                if (y1 <= y0) y1 = y0 + 1;
                if (x1 > src->w) x1 = src->w;
                if (y1 > src->h) y1 = src->h;

                for (iy = y0; iy < y1; iy++)
                    for (ix = x0; ix < x1; ix++) {
                        const float *p = src->px + ((size_t)iy * src->w + ix) * 4;
                        acc[0] += p[0]; acc[1] += p[1];
                        acc[2] += p[2]; acc[3] += p[3];
                        n++;
                    }
                if (n == 0) n = 1;
                for (c = 0; c < 4; c++) o[c] = acc[c] / n;
            } else {
                /* Bilinear, sampling at pixel centres. */
                float fx = (x + 0.5f) * sx - 0.5f;
                float fy = (y + 0.5f) * sy - 0.5f;
                int   x0 = (int)floorf(fx), y0 = (int)floorf(fy);
                float tx = fx - x0, ty = fy - y0;
                int   x1 = x0 + 1, y1 = y0 + 1;

                if (x0 < 0) x0 = 0;
                if (y0 < 0) y0 = 0;
                if (x1 < 0) x1 = 0;
                if (y1 < 0) y1 = 0;
                if (x0 >= src->w) x0 = src->w - 1;
                if (x1 >= src->w) x1 = src->w - 1;
                if (y0 >= src->h) y0 = src->h - 1;
                if (y1 >= src->h) y1 = src->h - 1;

                for (c = 0; c < 4; c++) {
                    const float *a = src->px + ((size_t)y0 * src->w + x0) * 4;
                    const float *b = src->px + ((size_t)y0 * src->w + x1) * 4;
                    const float *d = src->px + ((size_t)y1 * src->w + x0) * 4;
                    const float *e = src->px + ((size_t)y1 * src->w + x1) * 4;
                    float top = a[c] * (1 - tx) + b[c] * tx;
                    float bot = d[c] * (1 - tx) + e[c] * tx;
                    o[c] = top * (1 - ty) + bot * ty;
                }
            }
        }
    }
    return 0;
}

int ss_image_fit(ss_image *im, int max_edge)
{
    ss_image tmp;
    int w, h, longest;

    if (max_edge <= 0) return 0;
    longest = im->w > im->h ? im->w : im->h;
    if (longest <= max_edge) return 0;

    if (im->w >= im->h) {
        w = max_edge;
        h = (int)((double)im->h * max_edge / im->w + 0.5);
    } else {
        h = max_edge;
        w = (int)((double)im->w * max_edge / im->h + 0.5);
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (ss_image_scale(&tmp, im, w, h) != 0) return -1;
    ss_image_free(im);
    *im = tmp;
    return 0;
}
