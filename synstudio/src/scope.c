/* Scopes — the picture, measured.
 *
 * A waveform, an RGB parade and a vectorscope, computed HERE and not by an
 * ffmpeg filter. The histogram set the precedent and the reason is the same:
 * these are read to decide whether a shot is legal and whether two shots
 * match, and an answer that came from a different renderer than the picture
 * is an answer about something else. `ss_edit_apply` produces the pixels, and
 * these count exactly those pixels.
 *
 * Everything is measured in the DISPLAY encoding, like the histogram, because
 * that is the encoding a deliverable is clipped in. A waveform in linear
 * light would put middle grey at 18% and nobody reads it there.
 */
#include "synstudio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *scope_names[] = { "waveform", "parade", "vector" };

int ss_scope_value(const char *s)
{
    int i;
    if (!s) return -1;
    for (i = 0; i < 3; i++) if (!strcmp(s, scope_names[i])) return i;
    /* The two spellings everybody types for the third one. */
    if (!strcmp(s, "vectorscope")) return SS_SCOPE_VECTOR;
    if (!strcmp(s, "rgb")) return SS_SCOPE_PARADE;
    return -1;
}

const char *ss_scope_name(int v)
{
    return (v >= 0 && v < 3) ? scope_names[v] : scope_names[0];
}

/* Density to brightness.
 *
 * ⚠ NOT normalised by the busiest cell. The first version was, and a
 * vectorscope of colour bars came out almost black: the greys pile into a few
 * cells at the centre, that peak is enormous, and every hue around it divides
 * down to nothing. The interesting part of a scope is its FAINT parts — a
 * highlight rolling off is a few pixels a column — so a scale that the
 * brightest thing in frame sets is the wrong scale.
 *
 * Referenced to the mean of the OCCUPIED cells instead, through a curve that
 * saturates rather than clips: a cell of average business is half lit,
 * anything busier approaches white, and one cell holding a third of the frame
 * lifts nothing but itself. Scale-invariant — the same picture at any scope
 * size, and any picture at any resolution, reads the same.
 *
 * Deterministic, because the tests compare scopes to each other.
 */
static float density(float count, float mean)
{
    if (count <= 0.0f || mean <= 0.0f) return 0.0f;
    return 1.0f - expf(-0.7f * count / mean);
}

/* The mean of the cells that have anything in them. */
static float occupied_mean(const float *acc, long n)
{
    long i, occ = 0;
    double sum = 0;
    for (i = 0; i < n; i++)
        if (acc[i] > 0.0f) { sum += acc[i]; occ++; }
    return occ > 0 ? (float)(sum / occ) : 0.0f;
}

static void put(ss_image *im, int x, int y, float r, float g, float b)
{
    float *p;
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) return;
    p = im->px + ((size_t)y * im->w + x) * 4;
    if (r > p[0]) p[0] = r;
    if (g > p[1]) p[1] = g;
    if (b > p[2]) p[2] = b;
    p[3] = 1.0f;
}

/* The lines a scope is READ against. Without them a waveform is a shape; with
 * them it is a measurement — the 0 and 100 lines are what "legal" means, and
 * the vectorscope's circle is what "too saturated" means. */
static void graticule_h(ss_image *im, int x0, int x1, float frac)
{
    int y = (int)((1.0f - frac) * (im->h - 1) + 0.5f), x;
    for (x = x0; x < x1; x++) {
        float *p;
        if (x < 0 || x >= im->w || y < 0 || y >= im->h) continue;
        p = im->px + ((size_t)y * im->w + x) * 4;
        /* Under the trace, never over it: a graticule that hides the pixel it
         * crosses is a graticule that hides a clip. */
        if (p[0] < 0.16f && p[1] < 0.16f && p[2] < 0.16f) {
            p[0] = p[1] = 0.16f; p[2] = 0.20f; p[3] = 1.0f;
        }
    }
}

/* One waveform column set, over a band of the output. `chan` is 0..2 for a
 * single channel or 3 for luma. */
static void waveform_band(const ss_image *in, ss_image *out, int chan,
                          int x0, int bandw, float cr, float cg, float cb)
{
    float *acc, mean;
    int x, y, bins = out->h;
    long i;

    if (bandw <= 0 || bins <= 0) return;
    acc = calloc((size_t)bandw * bins, sizeof(float));
    if (!acc) return;

    for (i = 0; i < (long)in->w * in->h; i++) {
        const float *p = in->px + i * 4;
        int sx = (int)(i % in->w);
        int bx = (int)((long)sx * bandw / in->w);
        float v = chan == 3 ? ss_luma(p[0], p[1], p[2]) : p[chan];
        int by;
        v = ss_linear_to_srgb(v);
        by = (int)(ss_clampf(v, 0.0f, 1.0f) * (bins - 1) + 0.5f);
        acc[(size_t)bx * bins + by] += 1.0f;
    }
    mean = occupied_mean(acc, (long)bandw * bins);

    for (x = 0; x < bandw; x++)
        for (y = 0; y < bins; y++) {
            float d = density(acc[(size_t)x * bins + y], mean);
            if (d <= 0.0f) continue;
            /* The scope is drawn bottom-up: black at the bottom, white at the
             * top, which is how every scope in every suite reads. */
            put(out, x0 + x, bins - 1 - y, d * cr, d * cg, d * cb);
        }
    free(acc);
}

int ss_scope_render(const ss_image *in, int kind, int w, int h, ss_image *out)
{
    if (!in || !out || w < 16 || h < 16 || in->w < 1 || in->h < 1) return -1;
    if (ss_image_init(out, w, h) != 0) return -1;
    memset(out->px, 0, (size_t)w * h * 4 * sizeof(float));
    {   /* An opaque black field, so the PNG is not a transparent one. */
        long i;
        for (i = 0; i < (long)w * h; i++) out->px[i * 4 + 3] = 1.0f;
    }

    if (kind == SS_SCOPE_PARADE) {
        int band = w / 3, k;
        static const float tint[3][3] = { {1.0f, 0.22f, 0.22f},
                                          {0.22f, 1.0f, 0.30f},
                                          {0.35f, 0.45f, 1.0f} };
        for (k = 0; k < 3; k++)
            waveform_band(in, out, k, k * band, band,
                          tint[k][0], tint[k][1], tint[k][2]);
        for (k = 0; k < 3; k++) {
            graticule_h(out, k * band, k * band + band, 0.0f);
            graticule_h(out, k * band, k * band + band, 0.5f);
            graticule_h(out, k * band, k * band + band, 1.0f);
        }
        return 0;
    }

    if (kind == SS_SCOPE_VECTOR) {
        float *acc, mean;
        int cx = w / 2, cy = h / 2, r = (w < h ? w : h) / 2 - 2, x, y;
        long i;

        acc = calloc((size_t)w * h, sizeof(float));
        if (!acc) return -1;
        for (i = 0; i < (long)in->w * in->h; i++) {
            const float *p = in->px + i * 4;
            float R = ss_linear_to_srgb(p[0]);
            float G = ss_linear_to_srgb(p[1]);
            float B = ss_linear_to_srgb(p[2]);
            /* BT.709 chroma. The scope is a picture of U against V and
             * nothing else — the luma is the waveform's job, and mixing the
             * two is what makes a vectorscope unreadable. */
            float U = -0.1146f * R - 0.3854f * G + 0.5f * B;
            float V =  0.5f    * R - 0.4542f * G - 0.0458f * B;
            int px2 = cx + (int)(U * 2.0f * r + 0.5f);
            int py2 = cy - (int)(V * 2.0f * r + 0.5f);
            if (px2 < 0 || py2 < 0 || px2 >= w || py2 >= h) continue;
            acc[(size_t)py2 * w + px2] += 1.0f;
        }
        mean = occupied_mean(acc, (long)w * h);

        /* The 100% saturation circle, drawn first so the trace goes over it. */
        for (x = 0; x < 360; x++) {
            double a = x * M_PI / 180.0;
            int gx = cx + (int)(cos(a) * r * 0.75 + 0.5);
            int gy = cy - (int)(sin(a) * r * 0.75 + 0.5);
            if (gx < 0 || gy < 0 || gx >= w || gy >= h) continue;
            {
                float *p = out->px + ((size_t)gy * w + gx) * 4;
                p[0] = p[1] = 0.16f; p[2] = 0.20f; p[3] = 1.0f;
            }
        }
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                float d = density(acc[(size_t)y * w + x], mean);
                float U, V, ang, sat;
                if (d <= 0.0f) continue;
                /* Coloured by WHERE it is, not by the pixel that put it
                 * there: the position IS the hue, so a trace reaching into
                 * the red corner is drawn red whatever made it. */
                U = ((float)x - cx) / (2.0f * r);
                V = ((float)cy - y) / (2.0f * r);
                ang = atan2f(V, U);
                sat = sqrtf(U * U + V * V) * 2.0f;
                if (sat > 1.0f) sat = 1.0f;
                {
                    float rr = 0.5f + 0.5f * cosf(ang);
                    float gg = 0.5f + 0.5f * cosf(ang - 2.0944f);
                    float bb = 0.5f + 0.5f * cosf(ang + 2.0944f);
                    float m = 1.0f - sat, cr, cg, cb;
                    cr = rr + m * (1 - rr);
                    cg = gg + m * (1 - gg);
                    cb = bb + m * (1 - bb);
                    put(out, x, y, d * cr, d * cg, d * cb);
                    /* One pixel of body, and nothing more.
                     *
                     * A vectorscope's grid is as fine as its output, so a
                     * frame of colour bars lights barely one per cent of it
                     * and reads as an empty box with a graticule in it. The
                     * cross is a DISPLAY choice — the position of every
                     * sample is still exactly where it was measured, and the
                     * neighbours are lit at under half — not a change to what
                     * was counted. */
                    put(out, x - 1, y, d * cr * 0.45f, d * cg * 0.45f, d * cb * 0.45f);
                    put(out, x + 1, y, d * cr * 0.45f, d * cg * 0.45f, d * cb * 0.45f);
                    put(out, x, y - 1, d * cr * 0.45f, d * cg * 0.45f, d * cb * 0.45f);
                    put(out, x, y + 1, d * cr * 0.45f, d * cg * 0.45f, d * cb * 0.45f);
                }
            }
        free(acc);
        return 0;
    }

    /* Luma waveform, the default. */
    waveform_band(in, out, 3, 0, w, 0.85f, 1.0f, 0.85f);
    graticule_h(out, 0, w, 0.0f);
    graticule_h(out, 0, w, 0.25f);
    graticule_h(out, 0, w, 0.5f);
    graticule_h(out, 0, w, 0.75f);
    graticule_h(out, 0, w, 1.0f);
    return 0;
}
