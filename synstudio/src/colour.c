/* colour.c — the POINTWISE half of a develop stack.
 *
 * Pointwise means the output for a pixel depends on that pixel's value and
 * nothing else: no neighbours, no coordinates. That property is not an
 * accident of the implementation, it is the contract that lets lut.c bake
 * this whole file into a 3D LUT and hand it to ffmpeg for video. Anything
 * that needs a neighbour or an (x,y) belongs in spatial.c instead.
 *
 * Two domains are used on purpose:
 *
 *   LINEAR   white balance, exposure — these model light arriving, and light
 *            adds and multiplies linearly. Exposure in gamma space is not a
 *            stop.
 *   ENCODED  everything else. Contrast, the tone regions, curves, HSL,
 *            saturation are controls over a LOOK, and their sliders only feel
 *            evenly spaced in a perceptual encoding. sRGB's transfer function
 *            is the encoding, chosen because it is also what the file arrived
 *            in and what it leaves in.
 *
 * The tone controls all work the same way: compute a new ENCODED LUMA, then
 * apply the ratio as a gain to all three channels. Adjusting each channel
 * separately would swing hue as brightness changes, which is what makes a
 * naive levels control look plastic.
 */
#include "synstudio.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float ss_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Rec.709 luminance weights, correct for the sRGB primaries we work in. */
float ss_luma(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/* Both transfer functions are MIRRORED through the origin. White balance and
 * the wider-than-sRGB values that come out of raw files routinely produce
 * small negative channels, and clamping them at zero here would turn a
 * recoverable near-black into a hard colour cast. They survive the round trip
 * and are only clipped when the file is written. */
float ss_srgb_to_linear(float v)
{
    float s = v < 0.0f ? -1.0f : 1.0f, a = fabsf(v);
    a = (a <= 0.04045f) ? a / 12.92f : powf((a + 0.055f) / 1.055f, 2.4f);
    return s * a;
}

float ss_linear_to_srgb(float v)
{
    float s = v < 0.0f ? -1.0f : 1.0f, a = fabsf(v);
    a = (a <= 0.0031308f) ? a * 12.92f : 1.055f * powf(a, 1.0f / 2.4f) - 0.055f;
    return s * a;
}

/* ------------------------------------------------------- white balance -- */

/* Planckian locus, Kim et al.'s cubic fit, valid 1667K..25000K. Below 4000K
 * and above it are different polynomials; the join is continuous. */
static void locus_xy(float T, float *xc, float *yc)
{
    float t = ss_clampf(T, 1667.0f, 25000.0f);
    float t2 = t * t, t3 = t2 * t, x;

    if (t < 4000.0f)
        x = -0.2661239e9f / t3 - 0.2343589e6f / t2 + 0.8776956e3f / t + 0.179910f;
    else
        x = -3.0258469e9f / t3 + 2.1070379e6f / t2 + 0.2226347e3f / t + 0.240390f;

    {
        float x2 = x * x, x3 = x2 * x, y;
        if (t < 2222.0f)
            y = -1.1063814f * x3 - 1.34811020f * x2 + 2.18555832f * x - 0.20219683f;
        else if (t < 4000.0f)
            y = -0.9549476f * x3 - 1.37418593f * x2 + 2.09137015f * x - 0.16748867f;
        else
            y =  3.0817580f * x3 - 5.87338670f * x2 + 3.75112997f * x - 0.37001483f;
        *xc = x; *yc = y;
    }
}

/* Linear sRGB of the illuminant at T, normalised to unit luminance. */
static void illum_rgb(float T, float rgb[3])
{
    float x, y, X, Y, Z;
    locus_xy(T, &x, &y);
    if (y < 1e-6f) y = 1e-6f;
    Y = 1.0f;
    X = x / y;
    Z = (1.0f - x - y) / y;

    rgb[0] =  3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    rgb[1] = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    rgb[2] =  0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;
}

/* Channel multipliers that render a scene lit at temp_k as neutral.
 *
 * Direction check, because it is easy to get backwards and it is the most
 * noticeable control in the app: raising temp_k must make the picture WARMER.
 * A high T illuminant is BLUE, so illum_rgb returns a large blue; dividing by
 * it SHRINKS blue and the result is yellower. That is the right way round.
 *
 * Green is pinned to 1 so temperature never changes overall exposure; the
 * tint axis then trades green against magenta on top of it. */
static void wb_multipliers(const ss_develop *d, float m[3])
{
    float ref[3], cur[3], t;

    m[0] = m[1] = m[2] = 1.0f;
    if (d->temp_k > 0.0f) {
        illum_rgb(6500.0f, ref);
        illum_rgb(d->temp_k, cur);
        if (cur[0] > 1e-6f && cur[1] > 1e-6f && cur[2] > 1e-6f) {
            m[0] = ref[0] / cur[0];
            m[1] = ref[1] / cur[1];
            m[2] = ref[2] / cur[2];
            m[0] /= m[1]; m[2] /= m[1]; m[1] = 1.0f;
        }
    }
    /* Positive tint is magenta, which is green REMOVED. */
    t = ss_clampf(d->tint, -150.0f, 150.0f) / 150.0f;
    m[1] *= 1.0f / (1.0f + 0.35f * t);
}

/* --------------------------------------------------------- tone regions -- */

/* Four overlapping windows over the encoded range. blacks and whites are
 * anchored at the ends and fall off as a quartic; shadows and highlights are
 * Gaussians centred a quarter in from each end, which is where the eye reads
 * "the shadows" and "the highlights" of a normally exposed picture. */
static float w_blacks(float v)     { float a = 1.0f - v; a *= a; return a * a; }
static float w_whites(float v)     { float a = v * v;            return a * a; }
static float w_gauss(float v, float c)
{
    float dv = (v - c) / 0.18f;
    return expf(-0.5f * dv * dv);
}

/* Monotone by construction in both directions — see the note in the header.
 * Positive contrast is an S-curve whose derivative is 6v(1-v), never negative.
 * Negative contrast pulls linearly toward mid grey, derivative 1 - 0.5|s|. */
static float apply_contrast(float v, float s)
{
    if (s > 0.0f) {
        float sc = -4.0f * v * v * v + 6.0f * v * v - 2.0f * v;
        return v + s * 0.5f * sc;
    }
    return v + (-s) * (0.5f - v) * 0.5f;
}

/* ------------------------------------------------------------------ HSL -- */

static const float band_centre[SS_BANDS] = {
    0.0f, 30.0f, 60.0f, 120.0f, 180.0f, 240.0f, 285.0f, 320.0f
};

/* Triangular weight, wrapping at 360. Each band reaches its neighbours and no
 * further, so the eight weights sum to 1 everywhere and a slider can never
 * leave a gap of untouched hue between two bands. */
static float band_weight(int i, float hue)
{
    float c = band_centre[i];
    float prev = band_centre[(i + SS_BANDS - 1) % SS_BANDS];
    float next = band_centre[(i + 1) % SS_BANDS];
    float dl, dr, d = hue - c;

    while (d > 180.0f)  d -= 360.0f;
    while (d < -180.0f) d += 360.0f;

    dl = c - prev; while (dl < 0.0f) dl += 360.0f;
    dr = next - c; while (dr < 0.0f) dr += 360.0f;

    if (d < 0.0f) return (dl <= 0.0f) ? 0.0f : ss_clampf(1.0f + d / dl, 0.0f, 1.0f);
    return (dr <= 0.0f) ? 0.0f : ss_clampf(1.0f - d / dr, 0.0f, 1.0f);
}

static void rgb_to_hsl(const float rgb[3], float *h, float *s, float *l)
{
    float mx = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));
    float mn = fminf(rgb[0], fminf(rgb[1], rgb[2]));
    float c = mx - mn;

    *l = 0.5f * (mx + mn);
    if (c < 1e-7f) { *h = 0.0f; *s = 0.0f; return; }

    *s = c / (1.0f - fabsf(2.0f * *l - 1.0f) + 1e-7f);

    if (mx == rgb[0])      *h = 60.0f * fmodf(((rgb[1] - rgb[2]) / c) + 6.0f, 6.0f);
    else if (mx == rgb[1]) *h = 60.0f * (((rgb[2] - rgb[0]) / c) + 2.0f);
    else                   *h = 60.0f * (((rgb[0] - rgb[1]) / c) + 4.0f);
}

static void hsl_to_rgb(float h, float s, float l, float rgb[3])
{
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float hp, x, m, r = 0, g = 0, b = 0;

    h = fmodf(h, 360.0f); if (h < 0.0f) h += 360.0f;
    hp = h / 60.0f;
    x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    m = l - 0.5f * c;

    if      (hp < 1.0f) { r = c; g = x; }
    else if (hp < 2.0f) { r = x; g = c; }
    else if (hp < 3.0f) { g = c; b = x; }
    else if (hp < 4.0f) { g = x; b = c; }
    else if (hp < 5.0f) { r = x; b = c; }
    else                { r = c; b = x; }

    rgb[0] = r + m; rgb[1] = g + m; rgb[2] = b + m;
}

/* ------------------------------------------------------------ the stack -- */

void ss_pixel_pointwise(const ss_develop *d, float in[3], float out[3])
{
    float rgb[3], m[3], v, nv, gain, l0;
    int i;

    rgb[0] = in[0]; rgb[1] = in[1]; rgb[2] = in[2];

    /* --- linear domain ------------------------------------------------- */

    wb_multipliers(d, m);
    rgb[0] *= m[0]; rgb[1] *= m[1]; rgb[2] *= m[2];

    if (d->exposure != 0.0f) {
        float e = exp2f(ss_clampf(d->exposure, -8.0f, 8.0f));
        rgb[0] *= e; rgb[1] *= e; rgb[2] *= e;
    }

    /* --- encoded domain ------------------------------------------------ */

    rgb[0] = ss_linear_to_srgb(rgb[0]);
    rgb[1] = ss_linear_to_srgb(rgb[1]);
    rgb[2] = ss_linear_to_srgb(rgb[2]);

    /* Tone regions and contrast, as a luma gain so hue is preserved. */
    v = ss_luma(rgb[0], rgb[1], rgb[2]);
    nv = v;
    if (d->blacks || d->shadows || d->highlights || d->whites) {
        float cv = ss_clampf(v, 0.0f, 1.0f);
        nv += 0.25f * (d->blacks     / 100.0f) * w_blacks(cv);
        nv += 0.25f * (d->shadows    / 100.0f) * w_gauss(cv, 0.25f);
        nv += 0.25f * (d->highlights / 100.0f) * w_gauss(cv, 0.75f);
        nv += 0.25f * (d->whites     / 100.0f) * w_whites(cv);
    }
    if (d->contrast != 0.0f)
        nv = apply_contrast(nv, ss_clampf(d->contrast, -100.0f, 100.0f) / 100.0f);

    if (nv != v) {
        /* Below this the picture is black and the ratio is meaningless noise;
         * offset additively instead so lifting blacks still lifts true black. */
        if (fabsf(v) < 1e-4f) {
            rgb[0] += nv - v; rgb[1] += nv - v; rgb[2] += nv - v;
        } else {
            gain = nv / v;
            rgb[0] *= gain; rgb[1] *= gain; rgb[2] *= gain;
        }
    }

    /* Curves. The composite curve runs PER CHANNEL, not on luma: that is what
     * makes an S-curve add saturation the way every other editor's does. */
    if (!d->curve_rgb.identity) {
        rgb[0] = ss_curve_eval(&d->curve_rgb, rgb[0]);
        rgb[1] = ss_curve_eval(&d->curve_rgb, rgb[1]);
        rgb[2] = ss_curve_eval(&d->curve_rgb, rgb[2]);
    }
    if (!d->curve_r.identity) rgb[0] = ss_curve_eval(&d->curve_r, rgb[0]);
    if (!d->curve_g.identity) rgb[1] = ss_curve_eval(&d->curve_g, rgb[1]);
    if (!d->curve_b.identity) rgb[2] = ss_curve_eval(&d->curve_b, rgb[2]);

    /* HSL bands. */
    {
        int any = 0;
        for (i = 0; i < SS_BANDS; i++)
            if (d->hsl_hue[i] || d->hsl_sat[i] || d->hsl_lum[i]) { any = 1; break; }
        if (any) {
            float h, s, l, dh = 0, ds = 0, dl = 0, w;
            rgb_to_hsl(rgb, &h, &s, &l);
            for (i = 0; i < SS_BANDS; i++) {
                w = band_weight(i, h);
                if (w <= 0.0f) continue;
                dh += w * d->hsl_hue[i] * 0.30f;   /* +-30 degrees at full */
                ds += w * d->hsl_sat[i] / 100.0f;
                dl += w * d->hsl_lum[i] / 100.0f;
            }
            /* Weighted by existing saturation: a hue slider must not paint
             * colour onto a grey pixel, which has no hue to rotate. */
            h += dh * ss_clampf(s * 3.0f, 0.0f, 1.0f);
            s = ss_clampf(s * (1.0f + ds), 0.0f, 1.0f);
            l = ss_clampf(l + dl * 0.30f * ss_clampf(s * 3.0f, 0.0f, 1.0f), 0.0f, 1.0f);
            hsl_to_rgb(h, s, l, rgb);
        }
    }

    /* Colour grading: tint the shadows and the highlights apart. */
    if (d->shadow_sat != 0.0f || d->hilite_sat != 0.0f) {
        float bal = ss_clampf(d->grade_balance, -100.0f, 100.0f) / 200.0f;
        float lv = ss_clampf(ss_luma(rgb[0], rgb[1], rgb[2]), 0.0f, 1.0f);
        float ws = ss_clampf(1.0f - (lv - bal) * 2.0f, 0.0f, 1.0f);
        float wh = ss_clampf((lv - bal) * 2.0f, 0.0f, 1.0f);
        float t[3];
        if (d->shadow_sat != 0.0f) {
            hsl_to_rgb(d->shadow_hue, 1.0f, 0.5f, t);
            for (i = 0; i < 3; i++)
                rgb[i] += (t[i] - 0.5f) * (d->shadow_sat / 100.0f) * ws * 0.5f;
        }
        if (d->hilite_sat != 0.0f) {
            hsl_to_rgb(d->hilite_hue, 1.0f, 0.5f, t);
            for (i = 0; i < 3; i++)
                rgb[i] += (t[i] - 0.5f) * (d->hilite_sat / 100.0f) * wh * 0.5f;
        }
    }

    /* Vibrance before saturation: vibrance is the selective one, and running
     * it after a global saturation boost would leave it nothing dull to find.
     *
     * Two weightings make it "vibrance" rather than a weaker saturation:
     * already-saturated pixels are spared, and oranges are spared harder,
     * because that band is skin and skin is the first thing a saturation
     * slider ruins. */
    if (d->vibrance != 0.0f || d->saturation != 0.0f) {
        float h, s, l, sat;
        rgb_to_hsl(rgb, &h, &s, &l);
        sat = s;
        if (d->vibrance != 0.0f) {
            float skin = 1.0f - 0.7f * band_weight(SS_BAND_ORANGE, h);
            sat += (d->vibrance / 100.0f) * (1.0f - sat) * skin * sat;
        }
        if (d->saturation != 0.0f)
            sat *= 1.0f + d->saturation / 100.0f;
        hsl_to_rgb(h, ss_clampf(sat, 0.0f, 1.0f), l, rgb);
    }

    /* --- back to linear ------------------------------------------------ */

    out[0] = ss_srgb_to_linear(rgb[0]);
    out[1] = ss_srgb_to_linear(rgb[1]);
    out[2] = ss_srgb_to_linear(rgb[2]);
    (void)l0;
}

void ss_apply_pointwise(ss_image *im, const ss_develop *d)
{
    long i, n;
    float in[3], out[3];

    if (!im || !im->px) return;
    n = (long)im->w * im->h;
    for (i = 0; i < n; i++) {
        float *p = im->px + i * 4;
        in[0] = p[0]; in[1] = p[1]; in[2] = p[2];
        ss_pixel_pointwise(d, in, out);
        p[0] = out[0]; p[1] = out[1]; p[2] = out[2];
    }
}
