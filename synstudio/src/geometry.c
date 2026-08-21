/* geometry.c — orientation, straightening, cropping, and the render order.
 *
 * The order is fixed and is not a preference: quarter turns and flips first
 * (they are lossless and they define which way is up), then the straighten
 * rotation, then the crop. Cropping before straightening would rotate the
 * corners of the crop back out of the frame and leave transparent wedges in a
 * rectangle the user had already framed.
 */
#include "synstudio.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int rot90(ss_image *im, int turns)
{
    ss_image out;
    int x, y, t;

    turns = ((turns % 4) + 4) % 4;
    for (t = 0; t < turns; t++) {
        if (ss_image_init(&out, im->h, im->w) != 0) return -1;
        for (y = 0; y < im->h; y++)
            for (x = 0; x < im->w; x++) {
                const float *s = im->px + ((size_t)y * im->w + x) * 4;
                float *d = out.px + ((size_t)x * out.w + (out.w - 1 - y)) * 4;
                memcpy(d, s, 4 * sizeof(float));
            }
        ss_image_free(im);
        *im = out;
    }
    return 0;
}

static void flip(ss_image *im, int h, int v)
{
    int x, y, c;
    float t;

    if (h) for (y = 0; y < im->h; y++)
        for (x = 0; x < im->w / 2; x++) {
            float *a = im->px + ((size_t)y * im->w + x) * 4;
            float *b = im->px + ((size_t)y * im->w + (im->w - 1 - x)) * 4;
            for (c = 0; c < 4; c++) { t = a[c]; a[c] = b[c]; b[c] = t; }
        }
    if (v) for (y = 0; y < im->h / 2; y++)
        for (x = 0; x < im->w; x++) {
            float *a = im->px + ((size_t)y * im->w + x) * 4;
            float *b = im->px + ((size_t)(im->h - 1 - y) * im->w + x) * 4;
            for (c = 0; c < 4; c++) { t = a[c]; a[c] = b[c]; b[c] = t; }
        }
}

/* Rotate about the centre, same canvas size, bilinear, sampling BACKWARDS
 * from the destination. Forward-mapping every source pixel leaves unwritten
 * holes wherever the rotation stretches, which is the classic wrong way to do
 * this and shows up as a moire of transparent dots. */
static int straighten(ss_image *im, float degrees)
{
    ss_image out;
    float a = (float)(degrees * M_PI / 180.0), ca = cosf(a), sa = sinf(a);
    float cx = im->w * 0.5f, cy = im->h * 0.5f;
    int x, y, c;

    if (fabsf(degrees) < 1e-4f) return 0;
    if (ss_image_init(&out, im->w, im->h) != 0) return -1;

    for (y = 0; y < im->h; y++) {
        for (x = 0; x < im->w; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float sxf =  ca * dx + sa * dy + cx - 0.5f;
            float syf = -sa * dx + ca * dy + cy - 0.5f;
            int x0 = (int)floorf(sxf), y0 = (int)floorf(syf);
            float tx = sxf - x0, ty = syf - y0;
            float *o = out.px + ((size_t)y * out.w + x) * 4;

            /* Outside the source stays fully transparent — the crop is
             * expected to cut these wedges away, and leaving them black
             * would hide the fact that the crop is too big. */
            if (x0 < 0 || y0 < 0 || x0 + 1 >= im->w || y0 + 1 >= im->h)
                continue;

            for (c = 0; c < 4; c++) {
                const float *p = im->px + ((size_t)y0 * im->w + x0) * 4;
                const float *q = im->px + ((size_t)y0 * im->w + x0 + 1) * 4;
                const float *r = im->px + ((size_t)(y0+1) * im->w + x0) * 4;
                const float *s = im->px + ((size_t)(y0+1) * im->w + x0 + 1) * 4;
                float top = p[c] * (1 - tx) + q[c] * tx;
                float bot = r[c] * (1 - tx) + s[c] * tx;
                o[c] = top * (1 - ty) + bot * ty;
            }
        }
    }
    ss_image_free(im);
    *im = out;
    return 0;
}

static int crop_to(ss_image *im, const ss_crop *cr)
{
    ss_image out;
    int x0 = (int)(cr->x * im->w + 0.5f);
    int y0 = (int)(cr->y * im->h + 0.5f);
    int w  = (int)(cr->w * im->w + 0.5f);
    int h  = (int)(cr->h * im->h + 0.5f);
    int y;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (x0 + w > im->w) w = im->w - x0;
    if (y0 + h > im->h) h = im->h - y0;
    if (w < 1 || h < 1) return -1;
    if (x0 == 0 && y0 == 0 && w == im->w && h == im->h) return 0;

    if (ss_image_init(&out, w, h) != 0) return -1;
    for (y = 0; y < h; y++)
        memcpy(out.px + (size_t)y * w * 4,
               im->px + ((size_t)(y0 + y) * im->w + x0) * 4,
               (size_t)w * 4 * sizeof(float));
    ss_image_free(im);
    *im = out;
    return 0;
}

int ss_apply_geometry(ss_image *im, const ss_develop *d)
{
    if (d->rotate90 && rot90(im, d->rotate90) != 0) return -1;
    if (d->flip_h || d->flip_v) flip(im, d->flip_h, d->flip_v);
    if (d->crop.angle != 0.0f && straighten(im, d->crop.angle) != 0) return -1;
    if (d->crop.on && crop_to(im, &d->crop) != 0) return -1;
    return 0;
}

/* The whole stack. Geometry runs LAST so that every radius-based op above it
 * saw the frame the photographer shot: cropping first would change what
 * "25 pixels of clarity" covers relative to the subject, so tightening a crop
 * would silently restyle the picture. */
int ss_render(ss_image *im, const ss_develop *d)
{
    ss_apply_pointwise(im, d);
    ss_apply_spatial(im, d);
    return ss_apply_geometry(im, d);
}
