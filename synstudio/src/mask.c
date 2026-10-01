/* mask.c — local adjustments.
 *
 * A mask is a develop stack plus a coverage function. It renders the masked
 * stack over a COPY of the whole image and blends the two by coverage, rather
 * than trying to run the pipeline on the covered pixels only.
 *
 * That is deliberate and it costs memory. The spatial ops need neighbours, and
 * a neighbour just outside the mask is a real pixel with a real value; running
 * clarity on an isolated island of covered pixels would read zeroes across the
 * boundary and draw a bright seam exactly along the mask edge. Rendering the
 * full frame and blending afterwards has no boundary at all.
 */
#include "synstudio.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

void ss_mask_reset(ss_mask *m, int type)
{
    memset(m, 0, sizeof(*m));
    m->type = type;
    ss_develop_reset(&m->dev);
    if (type == SS_MASK_LINEAR) {
        m->x0 = 0.5f; m->y0 = 0.2f;
        m->x1 = 0.5f; m->y1 = 0.8f;
    } else {
        m->x0 = 0.5f; m->y0 = 0.5f;
        m->x1 = 0.3f; m->y1 = 0.3f;
    }
    m->feather = 0.5f;
}

/* Smoothstep, so coverage reaches 0 and 1 with zero slope. A linear ramp
 * leaves a visible crease at both ends of a gradient — the eye finds a
 * discontinuity in the DERIVATIVE, not just in the value (Mach banding). */
static float smooth(float t)
{
    t = ss_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float ss_mask_at(const ss_mask *m, float fx, float fy)
{
    float c;

    if (m->type == SS_MASK_LINEAR) {
        float dx = m->x1 - m->x0, dy = m->y1 - m->y0;
        float len2 = dx * dx + dy * dy;
        float t;
        if (len2 < 1e-9f) return m->invert ? 0.0f : 1.0f;
        /* Projection of the pixel onto the gradient axis. */
        t = ((fx - m->x0) * dx + (fy - m->y0) * dy) / len2;
        c = smooth(t);
    } else {
        float rx = m->x1 > 1e-6f ? m->x1 : 1e-6f;
        float ry = m->y1 > 1e-6f ? m->y1 : 1e-6f;
        float dx = (fx - m->x0) / rx, dy = (fy - m->y0) / ry;
        float r = sqrtf(dx * dx + dy * dy);
        float f = ss_clampf(m->feather, 0.01f, 1.0f);
        /* Full coverage inside (1-feather) of the radius, falling to 0 at 1. */
        c = 1.0f - smooth((r - (1.0f - f)) / f);
    }

    if (m->invert) c = 1.0f - c;
    return ss_clampf(c, 0.0f, 1.0f);
}

/* ss_mask_at as a geq expression, for a mask on a clip's grade.
 *
 * ⚠ THE SAME FUNCTION TWICE, so change both or neither: the darkroom asks
 * the C above per pixel, the video graph asks this per pixel of a matte. The
 * pixel centre is (X+0.5)/W as above, the smoothstep is the same polynomial,
 * and the constants are folded here rather than left to the expression.
 *
 * Scaled to 0..255 for an 8-bit plane and ROUNDED: geq truncates what it
 * stores, which would put every coverage up to half a code low. */
int ss_mask_expr(const ss_mask *m, char *out, size_t n)
{
    const char *ss = "ld(0)*ld(0)*(3-2*ld(0))";
    int r;

    if (m->type == SS_MASK_LINEAR) {
        double dx = (double)m->x1 - m->x0, dy = (double)m->y1 - m->y0;
        double len2 = dx * dx + dy * dy;
        if (len2 < 1e-9)
            r = snprintf(out, n, "%s", m->invert ? "0" : "255");
        else
            r = snprintf(out, n, "st(0,clip(%.9g*(X+0.5)/W%+.9g*(Y+0.5)/H%+.9g,0,1));"
                                 "255*(%s%s)+0.5",
                         dx / len2, dy / len2,
                         -((double)m->x0 * dx + (double)m->y0 * dy) / len2,
                         m->invert ? "1-" : "", ss);
    } else {
        double rx = m->x1 > 1e-6f ? m->x1 : 1e-6, ry = m->y1 > 1e-6f ? m->y1 : 1e-6;
        double f = ss_clampf(m->feather, 0.01f, 1.0f);
        /* Covered inside, so the smoothstep of the distance is what is NOT. */
        r = snprintf(out, n, "st(0,clip((hypot(((X+0.5)/W%+.9g)/%.9g,((Y+0.5)/H%+.9g)/%.9g)"
                             "%+.9g)/%.9g,0,1));255*(%s%s)+0.5",
                     -(double)m->x0, rx, -(double)m->y0, ry, -(1.0 - f), f,
                     m->invert ? "" : "1-", ss);
    }
    return (r < 0 || (size_t)r >= n) ? -1 : 0;
}

int ss_apply_mask(ss_image *im, const ss_mask *m)
{
    ss_image work;
    int x, y, c;

    if (ss_develop_is_identity(&m->dev)) return 0;
    if (ss_image_copy(&work, im) != 0) return -1;

    /* Geometry is excluded from a mask on purpose: a local adjustment that
     * cropped the frame would change the meaning of every other mask's
     * coordinates. Masks colour and detail only. */
    ss_apply_pointwise(&work, &m->dev);
    ss_apply_spatial(&work, &m->dev);

    for (y = 0; y < im->h; y++) {
        float fy = (y + 0.5f) / im->h;
        for (x = 0; x < im->w; x++) {
            float fx = (x + 0.5f) / im->w;
            float k = ss_mask_at(m, fx, fy);
            float *a = im->px + ((size_t)y * im->w + x) * 4;
            const float *b = work.px + ((size_t)y * im->w + x) * 4;
            if (k <= 0.0f) continue;
            for (c = 0; c < 3; c++) a[c] += (b[c] - a[c]) * k;
        }
    }
    ss_image_free(&work);
    return 0;
}
