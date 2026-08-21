/* spatial.c — the half of the stack that needs a NEIGHBOUR or a COORDINATE.
 *
 * Nothing in this file can be baked into a 3D LUT, and that is the whole
 * reason the pipeline is split in two. When a clip is exported, colour.c
 * becomes a .cube and ffmpeg applies it; the ops here become ffmpeg filters
 * (unsharp, vignette, ...) chosen in timeline.c. Keeping the boundary honest
 * is what stops a still and a frame of video drifting apart.
 *
 * Blurs are three box passes, not a true Gaussian kernel. Three passes of a
 * box filter converge on a Gaussian closely enough that no one can see the
 * difference in a halo, and it costs O(1) per pixel per pass regardless of
 * radius — which matters because clarity wants a 25-pixel radius and a real
 * kernel there is 51 taps per axis on a 24-megapixel image.
 */
#include "synstudio.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------- helpers -- */

/* Single-channel plane, used for the luminance work. */
typedef struct { int w, h; float *v; } plane;

static int plane_init(plane *p, int w, int h)
{
    p->w = w; p->h = h;
    p->v = calloc((size_t)w * h, sizeof(float));
    return p->v ? 0 : -1;
}

static void plane_free(plane *p) { free(p->v); p->v = NULL; }

/* One horizontal box pass with a running sum, edges clamped. */
static void box_h(float *dst, const float *src, int w, int h, int r)
{
    int y, x;
    float inv = 1.0f / (2 * r + 1);

    for (y = 0; y < h; y++) {
        const float *s = src + (size_t)y * w;
        float *d = dst + (size_t)y * w;
        float sum = s[0] * (r + 1);
        for (x = 1; x <= r && x < w; x++) sum += s[x];
        if (w <= r) sum += s[w-1] * (r + 1 - w);
        for (x = 0; x < w; x++) {
            d[x] = sum * inv;
            {
                int add = x + r + 1, sub = x - r;
                sum += s[add < w ? add : w - 1];
                sum -= s[sub > 0 ? sub : 0];
            }
        }
    }
}

static void transpose(float *dst, const float *src, int w, int h)
{
    int y, x;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            dst[(size_t)x * h + y] = src[(size_t)y * w + x];
}

/* Separable blur by transposing between the two axes: one cache-friendly
 * kernel handles both directions and there is no strided inner loop. */
static int blur(plane *p, float radius)
{
    int r = (int)(radius + 0.5f), i;
    float *a, *b;
    size_t n = (size_t)p->w * p->h;

    if (r < 1) return 0;
    a = malloc(n * sizeof(float));
    b = malloc(n * sizeof(float));
    if (!a || !b) { free(a); free(b); return -1; }

    for (i = 0; i < 3; i++) {
        box_h(a, p->v, p->w, p->h, r);
        transpose(b, a, p->w, p->h);
        box_h(a, b, p->h, p->w, r);
        transpose(p->v, a, p->h, p->w);
    }
    free(a); free(b);
    return 0;
}

static void extract_luma(plane *p, const ss_image *im)
{
    long i, n = (long)im->w * im->h;
    for (i = 0; i < n; i++) {
        const float *px = im->px + i * 4;
        p->v[i] = ss_luma(px[0], px[1], px[2]);
    }
}

/* Apply a per-pixel luma DELTA as a ratio, so colour is untouched. Below the
 * floor the ratio is meaningless (dividing near-zero by near-zero), so add
 * instead — otherwise sharpening a dark frame produces coloured speckle. */
static void apply_luma_delta(ss_image *im, const float *delta)
{
    long i, n = (long)im->w * im->h;
    for (i = 0; i < n; i++) {
        float *px = im->px + i * 4;
        float l = ss_luma(px[0], px[1], px[2]);
        float d = delta[i];
        if (d == 0.0f) continue;
        if (fabsf(l) < 1e-4f) {
            px[0] += d; px[1] += d; px[2] += d;
        } else {
            float g = (l + d) / l;
            px[0] *= g; px[1] *= g; px[2] *= g;
        }
    }
}

/* --------------------------------------------- local contrast + detail -- */

/* Texture, clarity and sharpening are the same operation at three scales:
 * subtract a blurred copy, scale the difference, add it back. What separates
 * them is the radius and the weighting.
 *
 *   sharpen  ~1px    edge acutance
 *   texture  ~3px    fine detail — skin pores, fabric, foliage
 *   clarity  ~25px   midtone punch
 *
 * Clarity is weighted toward the midtones on purpose. Applied flat it eats
 * highlight detail and blocks up the shadows, which is the "HDR" look nobody
 * asks for by name.
 */
static int unsharp(ss_image *im, float radius, float amount, int midtone_only)
{
    plane l, b;
    long i, n = (long)im->w * im->h;
    float *delta;

    if (amount == 0.0f) return 0;
    if (plane_init(&l, im->w, im->h) != 0) return -1;
    extract_luma(&l, im);
    if (plane_init(&b, im->w, im->h) != 0) { plane_free(&l); return -1; }
    memcpy(b.v, l.v, (size_t)n * sizeof(float));
    if (blur(&b, radius) != 0) { plane_free(&l); plane_free(&b); return -1; }

    delta = malloc((size_t)n * sizeof(float));
    if (!delta) { plane_free(&l); plane_free(&b); return -1; }

    for (i = 0; i < n; i++) {
        float d = (l.v[i] - b.v[i]) * amount;
        if (midtone_only) {
            float v = ss_clampf(ss_linear_to_srgb(l.v[i]), 0.0f, 1.0f);
            d *= 4.0f * v * (1.0f - v);     /* peaks at mid, zero at both ends */
        }
        delta[i] = d;
    }
    apply_luma_delta(im, delta);

    free(delta);
    plane_free(&l);
    plane_free(&b);
    return 0;
}

/* -------------------------------------------------------------- dehaze -- */

/* Dark channel prior (He, Sun, Tang). Haze is additive airlight, so in a hazy
 * region EVERY channel is lifted off zero; the per-pixel minimum over a local
 * window ("dark channel") is therefore near zero in clear areas and high in
 * hazy ones, and that is a usable transmission estimate.
 *
 * The min filter is separable and the transmission map is blurred rather than
 * matted, which is the cheap version — good enough that the slider behaves,
 * and it never has to be exact because it is a look control, not a
 * measurement. */
static void min_h(float *dst, const float *src, int w, int h, int r)
{
    int y, x, i;
    for (y = 0; y < h; y++) {
        const float *s = src + (size_t)y * w;
        float *d = dst + (size_t)y * w;
        for (x = 0; x < w; x++) {
            float m = s[x];
            int a = x - r, b = x + r;
            if (a < 0) a = 0;
            if (b >= w) b = w - 1;
            for (i = a; i <= b; i++) if (s[i] < m) m = s[i];
            d[x] = m;
        }
    }
}

static int dehaze(ss_image *im, float amount)
{
    plane dc, tmp;
    long i, n = (long)im->w * im->h;
    int r = (im->w > im->h ? im->w : im->h) / 100;
    float A = 0.0f, w = ss_clampf(amount / 100.0f, -1.0f, 1.0f);
    float *t;

    if (amount == 0.0f) return 0;
    if (r < 3) r = 3;

    if (plane_init(&dc, im->w, im->h) != 0) return -1;
    for (i = 0; i < n; i++) {
        const float *p = im->px + i * 4;
        dc.v[i] = fminf(p[0], fminf(p[1], p[2]));
    }
    if (plane_init(&tmp, im->w, im->h) != 0) { plane_free(&dc); return -1; }
    min_h(tmp.v, dc.v, im->w, im->h, r);
    {
        float *tr = malloc((size_t)n * sizeof(float));
        if (!tr) { plane_free(&dc); plane_free(&tmp); return -1; }
        transpose(tr, tmp.v, im->w, im->h);
        min_h(dc.v, tr, im->h, im->w, r);
        transpose(tr, dc.v, im->h, im->w);
        memcpy(dc.v, tr, (size_t)n * sizeof(float));
        free(tr);
    }

    /* Airlight: the brightest of the dark channel, not the brightest pixel —
     * a specular highlight is not the sky. */
    for (i = 0; i < n; i++) if (dc.v[i] > A) A = dc.v[i];
    if (A < 1e-4f) { plane_free(&dc); plane_free(&tmp); return 0; }

    blur(&dc, (float)r);

    t = malloc((size_t)n * sizeof(float));
    if (!t) { plane_free(&dc); plane_free(&tmp); return -1; }
    for (i = 0; i < n; i++)
        t[i] = ss_clampf(1.0f - 0.95f * w * dc.v[i] / A, 0.1f, 1.0f);

    for (i = 0; i < n; i++) {
        float *p = im->px + i * 4;
        int c;
        for (c = 0; c < 3; c++) p[c] = (p[c] - A) / t[i] + A;
    }

    free(t);
    plane_free(&dc);
    plane_free(&tmp);
    return 0;
}

/* ----------------------------------------------------- noise reduction -- */

/* Luma NR blends toward a blurred copy only where the local detail is small
 * relative to the threshold — a one-sided edge-stop. Real bilateral filtering
 * is better and much slower; this keeps edges because an edge produces a large
 * difference and therefore almost no blending.
 *
 * Chroma NR blurs hard and unconditionally. Colour noise has no detail worth
 * keeping and the eye cannot resolve chroma at that frequency anyway, which is
 * the same fact JPEG subsampling exploits. */
static int denoise(ss_image *im, float luma_amt, float chroma_amt)
{
    long i, n = (long)im->w * im->h;

    if (luma_amt > 0.0f) {
        plane l, b;
        float thresh = 0.002f + 0.02f * (luma_amt / 100.0f);
        float *delta;

        if (plane_init(&l, im->w, im->h) != 0) return -1;
        extract_luma(&l, im);
        if (plane_init(&b, im->w, im->h) != 0) { plane_free(&l); return -1; }
        memcpy(b.v, l.v, (size_t)n * sizeof(float));
        blur(&b, 1.0f + 2.0f * (luma_amt / 100.0f));

        delta = malloc((size_t)n * sizeof(float));
        if (!delta) { plane_free(&l); plane_free(&b); return -1; }
        for (i = 0; i < n; i++) {
            float d = b.v[i] - l.v[i];
            float k = thresh / (thresh + fabsf(d));   /* 1 on noise, 0 on edges */
            delta[i] = d * k;
        }
        apply_luma_delta(im, delta);
        free(delta);
        plane_free(&l); plane_free(&b);
    }

    if (chroma_amt > 0.0f) {
        plane cb, cr;
        float mix = ss_clampf(chroma_amt / 100.0f, 0.0f, 1.0f);
        if (plane_init(&cb, im->w, im->h) != 0) return -1;
        if (plane_init(&cr, im->w, im->h) != 0) { plane_free(&cb); return -1; }
        for (i = 0; i < n; i++) {
            const float *p = im->px + i * 4;
            float y = ss_luma(p[0], p[1], p[2]);
            cb.v[i] = p[2] - y;
            cr.v[i] = p[0] - y;
        }
        blur(&cb, 2.0f + 6.0f * mix);
        blur(&cr, 2.0f + 6.0f * mix);
        for (i = 0; i < n; i++) {
            float *p = im->px + i * 4;
            float y = ss_luma(p[0], p[1], p[2]);
            float ob = p[2] - y, orr = p[0] - y;
            float nb = ob + (cb.v[i] - ob) * mix;
            float nr = orr + (cr.v[i] - orr) * mix;
            /* Rebuild green from the luma identity so Y is preserved exactly. */
            p[0] = y + nr;
            p[2] = y + nb;
            p[1] = (y - 0.2126f * p[0] - 0.0722f * p[2]) / 0.7152f;
        }
        plane_free(&cb); plane_free(&cr);
    }
    return 0;
}

/* ------------------------------------------------- vignette and grain -- */

static void vignette(ss_image *im, const ss_develop *d)
{
    int x, y;
    float amt = d->vignette / 100.0f;
    float mid = 0.2f + 1.3f * (d->vignette_mid / 100.0f);
    float fth = 0.05f + 0.95f * (d->vignette_feather / 100.0f);
    float cx = im->w * 0.5f, cy = im->h * 0.5f;
    float norm = sqrtf(cx * cx + cy * cy);

    if (d->vignette == 0.0f || norm <= 0.0f) return;

    for (y = 0; y < im->h; y++) {
        for (x = 0; x < im->w; x++) {
            float dx = (x + 0.5f - cx) / norm;
            float dy = (y + 0.5f - cy) / norm;
            float r = sqrtf(dx * dx + dy * dy) / mid;
            /* smoothstep from the midpoint outward */
            float t = ss_clampf((r - (1.0f - fth)) / (fth + 1e-6f), 0.0f, 1.0f);
            float s = t * t * (3.0f - 2.0f * t);
            float g = exp2f(amt * s * 2.0f);
            float *p = im->px + ((size_t)y * im->w + x) * 4;
            p[0] *= g; p[1] *= g; p[2] *= g;
        }
    }
}

/* Deterministic value noise: a hash of the grain CELL, not of the pixel. The
 * cell size is what the size slider changes, and hashing the cell is what
 * makes grain look like grain instead of like per-pixel static. It is seeded
 * from position only, so re-rendering the same image gives the same grain and
 * an export matches the preview. */
static unsigned hash2(unsigned x, unsigned y)
{
    unsigned h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void grain(ss_image *im, const ss_develop *d)
{
    int x, y;
    float amt = d->grain / 100.0f;
    float cell = 1.0f + 3.0f * (d->grain_size / 100.0f);

    if (d->grain <= 0.0f) return;

    for (y = 0; y < im->h; y++) {
        for (x = 0; x < im->w; x++) {
            unsigned gx = (unsigned)(x / cell), gy = (unsigned)(y / cell);
            float nse = (float)(hash2(gx, gy) & 0xffff) / 65535.0f - 0.5f;
            float *p = im->px + ((size_t)y * im->w + x) * 4;
            float l = ss_clampf(ss_linear_to_srgb(ss_luma(p[0], p[1], p[2])),
                                0.0f, 1.0f);
            /* Film grain is strongest in the midtones and vanishes in clean
             * highlights, which is what keeps a graded sky from fizzing. */
            float w = 4.0f * l * (1.0f - l);
            float dv = nse * amt * 0.15f * w;
            p[0] = ss_srgb_to_linear(ss_linear_to_srgb(p[0]) + dv);
            p[1] = ss_srgb_to_linear(ss_linear_to_srgb(p[1]) + dv);
            p[2] = ss_srgb_to_linear(ss_linear_to_srgb(p[2]) + dv);
        }
    }
}

/* ------------------------------------------------------------ the half -- */

void ss_apply_spatial(ss_image *im, const ss_develop *d)
{
    float scale;

    if (!im || !im->px) return;

    /* Radii are quoted for a full-resolution frame. A preview is a smaller
     * image of the SAME scene, so a 25-pixel clarity radius on a 1500px
     * preview is a completely different look from the export — the preview
     * would lie. Scale every radius by the frame size. */
    scale = (float)(im->w > im->h ? im->w : im->h) / 4000.0f;
    if (scale < 0.15f) scale = 0.15f;

    if (d->dehaze  != 0.0f) dehaze(im, d->dehaze);
    if (d->nr_luma > 0.0f || d->nr_chroma > 0.0f)
        denoise(im, d->nr_luma, d->nr_chroma);
    if (d->clarity != 0.0f)
        unsharp(im, 25.0f * scale, d->clarity / 100.0f, 1);
    if (d->texture != 0.0f)
        unsharp(im, 3.0f * scale, d->texture / 100.0f * 1.5f, 0);
    if (d->sharpen > 0.0f)
        unsharp(im, d->sharpen_radius * scale, d->sharpen / 100.0f, 0);

    vignette(im, d);
    grain(im, d);
}
