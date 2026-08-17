/*
 * palette.c — a small UI palette, taken off the wallpaper.
 *
 * SYNAPSE Prism draws its chrome as glass and takes its COLOUR from whatever is
 * behind that glass. This is the part that decides what that colour is.
 *
 * ── Why three colours and not one ─────────────────────────────────────────
 *
 * One accent is what every "pick a colour from the image" feature does, and it
 * is not enough to build an interface out of. A panel needs a colour for the
 * thing you are pointing at, a quieter one for the rules and the rows you are
 * not, and something that is plainly NOT the accent for the places where two
 * states have to be told apart at a glance. Derive the second and third from
 * the first by turning a knob and they all read as the same colour, which is
 * how a "themed" interface ends up looking like a single hue smeared over it.
 *
 * So: accent and secondary are both MEASURED, from different parts of the
 * image, and only accent_dim is derived. Three is also the ceiling — a fourth
 * measured hue means picking from the noise, and a wallpaper rarely has four
 * colours a person would name.
 *
 * ── Why hue bins and not k-means ──────────────────────────────────────────
 *
 * k-means on a 4K image is the textbook answer and it is the wrong tool here:
 * it is iterative, it is seeded randomly (so the same wallpaper can yield a
 * different accent on the next login, which is the one thing this must never
 * do), and it clusters in a space where "dark navy" and "bright navy" are far
 * apart while "grey" sits in the middle attracting everything. What is wanted
 * is the dominant HUE, which is one pass, deterministic, and ignores how light
 * that hue happens to be.
 *
 * ── What is thrown away, and why ──────────────────────────────────────────
 *
 * Near-black and unsaturated pixels are dropped before binning. They are the
 * majority of most wallpapers and they carry no hue: a photograph
 * that is 70% dark sky would otherwise elect "very slightly blue black" as the
 * accent, which is a colour nothing can be drawn in. Dropping them is also what
 * lets this answer HONESTLY on a greyscale wallpaper — with nothing chromatic
 * left, `ok` is false and the theme keeps its own accent rather than being
 * handed a beige.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <math.h>
#include <string.h>

#include "palette.h"
#include "contrast.h"

/* Hue bins. 24 is 15° each, which is finer than a person distinguishes on a
 * wallpaper and coarse enough that a gradient does not split its own colour
 * across two bins and lose to a smaller flat region. */
#define HUE_BINS 24

/* A pixel has to clear both to vote. Tuned against real wallpapers rather than
 * derived: the numbers that matter are that `V_MIN` is above JPEG's noise floor
 * in shadows and `S_MIN` is above the chroma a grey picks up from compression. */
#define S_MIN 0.18   /* below this it is a grey wearing a tint */
#define V_MIN 0.12   /* shadow noise */

/* ⚠ AND THERE IS NO BRIGHTNESS TEST, WHICH IS THE POINT.
 *
 * There was one: pixels above v = 0.96 carrying less than 0.45 saturation were
 * dropped as blown highlights, on the reasoning that clipping drains saturation
 * on its way to white. Both halves of that are true and the rule was still
 * wrong, because S_MIN had already made it redundant — a genuinely clipped
 * pixel is heading for s = 0 and never survives S_MIN to reach a brightness
 * test at all. The only pixels the rule actually removed were the ones between
 * the two thresholds: bright, and chromatic enough to have cleared S_MIN.
 * That band is not clipping. That band is PASTEL.
 *
 * ⚠ AND PASTEL IS WHERE PINK LIVES. This is the bug that found it: a wallpaper
 * that is 97% Sanrio pink came back "no usable hue" and the desktop stayed
 * cyan, while every vivid hue on the wheel answered correctly. The reason pink
 * alone showed it is that pink is the one colour whose ORDINARY form is a
 * pastel — say "blue" and a person means the vivid one, but say "pink" and they
 * already mean a pale, unsaturated red. Vivid pink has its own names, magenta
 * and fuchsia and hot pink, and those all worked. So the rule read as
 * hue-neutral and behaved as "no pink", and nothing in it mentions hue.
 *
 * The house wallpaper was being misread the same way and nobody noticed: the
 * dendrite mark is densest at v = 1.0, s = 0.3–0.5, so its brightest and most
 * characteristic pixels were the discarded ones and the accent came off the
 * dimmer remainder — #7084FF for a mark that measures hue 255. It reads #A080E8
 * now, which is the colour the picture actually is.
 *
 * Near-white pixels do not need a cliff in any case: the mid-tone bell in the
 * weight below already puts a v = 1.0 pixel at 0.47 of its saturation, so
 * brightness is discounted as a CURVE by the thing that was always going to
 * handle it better. Two mechanisms for one job, and the crude one was
 * overruling the careful one. */

/* How much chromatic signal it takes to name a colour at all.
 *
 * ⚠ LOW ON PURPOSE, and the first cut had it fifteen times higher on the
 * reasoning that "a logo in the corner should not repaint the desktop". That
 * reasoning is wrong, and SynapseOS's own default wallpaper is the proof: it is
 * a dendrite mark on black, the mark IS what the picture is of, and a person
 * asked what colour that wallpaper is answers instantly. A floor tuned to
 * reject it made the house theme fall back to its own fallback on the house
 * wallpaper.
 *
 * What actually separates "a logo" from "a greyscale photograph with JPEG
 * chroma noise" is not area, it is STRENGTH: the weight below is saturation
 * times a mid-tone bell, so a few strong pixels outweigh a lot of faint ones
 * and compression tint contributes almost nothing however much of it there is.
 * The floor only has to be above that noise. */
#define CHROMA_FLOOR 0.004

/* …and the other half of the same question, which is the half that actually
 * separates the two cases. A real colour is CONCENTRATED — a logo, a sky, a
 * sunset all put their weight in one or two adjacent bins — while compression
 * tint and sensor noise smear thinly across all twenty-four. Measured on the
 * shipped wallpapers: the dendrite mark on black puts 25% of its weight in one
 * bin and the electrified one 90%, while a genuinely greyscale wallpaper scores
 * no chromatic weight at all and never reaches this test.
 *
 * Two weak tests together beat one strong one here: a floor high enough to
 * reject noise on its own also rejected the house wallpaper, and a
 * concentration test on its own would accept a nearly-grey image whose faint
 * tint happens to be uniform. */
#define TOP_BIN_SHARE 0.20

/* How far round the wheel the secondary has to be to be worth having. Below
 * this the two read as the same colour in a 2px rule, which is the whole
 * reason there is a second one. */
#define SECOND_MIN_DEG 55.0

/* The band the accent is pushed into before it is used. A wallpaper's dominant
 * hue arrives at whatever saturation and lightness the photograph had; an
 * interface colour has to be visible on a panel and not shout. These clamp
 * rather than set, so a wallpaper already in the band comes through untouched
 * and the desktop still looks like the picture.
 *
 * ⚠ THE VALUE FLOOR DEPENDS ON THE SURFACE, and that is not symmetry for its
 * own sake. syn_contrast_fix() only ever DARKENS — it is a no-op on a dark
 * surface by design, so nothing downstream can brighten a colour. On a dark
 * panel this band is therefore the only thing standing between a navy
 * wallpaper and a #2C1B8C accent that is invisible on it; on a light panel the
 * corrector does the work and a lower floor leaves it room. */
#define UI_S_MIN 0.45
#define UI_S_MAX 0.90
#define UI_V_MIN_ON_DARK  0.72
#define UI_V_MIN_ON_LIGHT 0.50
#define UI_V_MAX 0.95

static void rgb_to_hsv(double r, double g, double b,
                       double *h, double *s, double *v)
{
    double mx = fmax(r, fmax(g, b));
    double mn = fmin(r, fmin(g, b));
    double d  = mx - mn;

    *v = mx;
    *s = mx <= 0.0 ? 0.0 : d / mx;

    if (d <= 0.0) { *h = 0.0; return; }
    if      (mx == r) *h = 60.0 * fmod((g - b) / d, 6.0);
    else if (mx == g) *h = 60.0 * ((b - r) / d + 2.0);
    else              *h = 60.0 * ((r - g) / d + 4.0);
    if (*h < 0.0) *h += 360.0;
}

static void hsv_to_rgb(double h, double s, double v, float out[3])
{
    double c = v * s;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double m = v - c;
    double r, g, b;
    if      (h <  60.0) { r = c; g = x; b = 0; }
    else if (h < 120.0) { r = x; g = c; b = 0; }
    else if (h < 180.0) { r = 0; g = c; b = x; }
    else if (h < 240.0) { r = 0; g = x; b = c; }
    else if (h < 300.0) { r = x; g = 0; b = c; }
    else                { r = c; g = 0; b = x; }
    out[0] = (float)(r + m);
    out[1] = (float)(g + m);
    out[2] = (float)(b + m);
}

/* Shortest way round the wheel, in degrees. */
static double hue_gap(double a, double b)
{
    double d = fabs(a - b);
    return d > 180.0 ? 360.0 - d : d;
}

/*
 * Lighten until it clears the target against a DARK surface.
 *
 * ⚠ THE ONE THING NOTHING ELSE DOES. syn_contrast_fix() only ever darkens — it
 * is a deliberate no-op on a dark surface, because every theme's own accent was
 * chosen to work there. A MEASURED accent has no such guarantee: a wallpaper
 * whose dominant hue is a deep blue hands back #3A24B7, which is 1.7:1 on
 * Prism's near-black panel — an accent that is simply not there. Blue is the
 * case that shows it, because luminance weights blue at 0.07 and a "bright"
 * blue is still dark.
 *
 * Raise value first, since that keeps the hue and the character of the colour.
 * Only when value is spent does saturation come down, which is what turns a
 * deep blue into a pale one rather than into grey. Bounded, and it gives up
 * rather than looping: on an impossible surface the band is still better than
 * white.
 */
static void lift_to_contrast(const float in[3], double surface_lum,
                             float out[3])
{
    double h, s, v;
    rgb_to_hsv(in[0], in[1], in[2], &h, &s, &v);

    for (int i = 0; i < 60; i++) {
        float c[3];
        hsv_to_rgb(h, s, v, c);
        if (syn_contrast(c[0], c[1], c[2], surface_lum) >= CONTRAST_TARGET)
            break;
        if (v < 1.0)      v = fmin(1.0, v + 0.02);
        else if (s > 0.2) s -= 0.02;
        else              break;
    }
    hsv_to_rgb(h, s, v, out);
}

/* Pull a measured colour into the band an interface can use, keeping its hue.
 * Hue is never touched: the hue IS the wallpaper, and moving it would make the
 * feature a lie. */
static void to_ui_band(const float in[3], double surface_lum, float out[3])
{
    double h, s, v;
    rgb_to_hsv(in[0], in[1], in[2], &h, &s, &v);
    double v_min = surface_lum > 0.5 ? UI_V_MIN_ON_LIGHT : UI_V_MIN_ON_DARK;
    if (s < UI_S_MIN) s = UI_S_MIN;
    if (s > UI_S_MAX) s = UI_S_MAX;
    if (v < v_min)    v = v_min;
    if (v > UI_V_MAX) v = UI_V_MAX;
    hsv_to_rgb(h, s, v, out);
}

bool syn_palette_from_pixels(const unsigned char *data, int w, int h,
                             int stride, double surface_lum,
                             syn_palette_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || w <= 0 || h <= 0 || stride < w * 4) return false;

    /* Sample rather than walk. A 4K wallpaper is 8.3M pixels and the answer
     * does not improve past a few tens of thousands — this is on the path of a
     * wallpaper change, which also repaints every output. The step is derived
     * from the image so the cost is flat across resolutions, and it is at
     * least 1 so a small image is walked whole. */
    long total = (long)w * h;
    int step = (int)(sqrt((double)total / 40000.0));
    if (step < 1) step = 1;

    double bin_w[HUE_BINS]  = {0};
    double bin_r[HUE_BINS]  = {0};
    double bin_g[HUE_BINS]  = {0};
    double bin_b[HUE_BINS]  = {0};
    double voted = 0.0;
    long   seen  = 0;

    for (int y = 0; y < h; y += step) {
        const unsigned char *row = data + (size_t)y * stride;
        for (int x = 0; x < w; x += step) {
            /* Byte order is native-endian within the 32-bit word: B, G, R, A
             * on little-endian, the same assumption strip_luminance() makes
             * about the same buffers. */
            const unsigned char *px = row + (size_t)x * 4;
            double b = px[0] / 255.0, g = px[1] / 255.0, r = px[2] / 255.0;
            seen++;

            double hu, sa, va;
            rgb_to_hsv(r, g, b, &hu, &sa, &va);
            if (sa < S_MIN || va < V_MIN) continue;

            /* Weight by saturation, so a vivid patch outvotes a large washed
             * one — which is what a person means by "the colour of that
             * wallpaper". Mid-tones count for more than either extreme for the
             * same reason they were not thrown away: they are the part of the
             * image that has colour rather than exposure. */
            double mid = 1.0 - fabs(va - 0.55) / 0.55;
            if (mid < 0.0) mid = 0.0;
            double wgt = sa * (0.35 + 0.65 * mid);

            int bin = (int)(hu / (360.0 / HUE_BINS));
            if (bin < 0) bin = 0;
            if (bin >= HUE_BINS) bin = HUE_BINS - 1;

            bin_w[bin] += wgt;
            bin_r[bin] += r * wgt;
            bin_g[bin] += g * wgt;
            bin_b[bin] += b * wgt;
            voted += wgt;
        }
    }

    if (seen <= 0 || voted <= 0.0) return false;
    /* Honest refusal: a greyscale or near-greyscale wallpaper has no colour to
     * give, and inventing one is worse than the theme's own. */
    if (voted / (double)seen < CHROMA_FLOOR) return false;

    int top = 0;
    for (int i = 1; i < HUE_BINS; i++)
        if (bin_w[i] > bin_w[top]) top = i;
    if (bin_w[top] <= 0.0) return false;
    /* Concentrated, not smeared — see TOP_BIN_SHARE. */
    if (bin_w[top] / voted < TOP_BIN_SHARE) return false;

    float raw_accent[3] = {
        (float)(bin_r[top] / bin_w[top]),
        (float)(bin_g[top] / bin_w[top]),
        (float)(bin_b[top] / bin_w[top]),
    };

    double acc_h, acc_s, acc_v;
    rgb_to_hsv(raw_accent[0], raw_accent[1], raw_accent[2],
               &acc_h, &acc_s, &acc_v);

    /* The secondary: the heaviest bin far enough round the wheel to read as a
     * different colour. Measured, not rotated — see the header. */
    int second = -1;
    for (int i = 0; i < HUE_BINS; i++) {
        if (bin_w[i] <= 0.0) continue;
        double hu = (i + 0.5) * (360.0 / HUE_BINS);
        if (hue_gap(hu, acc_h) < SECOND_MIN_DEG) continue;
        if (second < 0 || bin_w[i] > bin_w[second]) second = i;
    }

    float raw_second[3];
    if (second >= 0) {
        raw_second[0] = (float)(bin_r[second] / bin_w[second]);
        raw_second[1] = (float)(bin_g[second] / bin_w[second]);
        raw_second[2] = (float)(bin_b[second] / bin_w[second]);
        out->measured_secondary = true;
    } else {
        /* A single-hue wallpaper — a sunset, a plain gradient. There is no
         * second colour in the image, so rotate rather than refuse: a panel
         * still needs two colours, and saying so on the row it is drawn in is
         * what `measured_secondary` is for. 150° is far enough to be plainly
         * different without landing on the complement, which on a warm
         * wallpaper is the one hue that looks like a mistake. */
        hsv_to_rgb(fmod(acc_h + 150.0, 360.0), acc_s, acc_v, raw_second);
        out->measured_secondary = false;
    }

    float band_accent[3], band_second[3];
    to_ui_band(raw_accent, surface_lum, band_accent);
    to_ui_band(raw_second, surface_lum, band_second);

    /* ⚠ THE CORRECTOR, LAST AND ALWAYS. Everything above is about what colour
     * the wallpaper IS; this is the only step that asks whether it can be read
     * on the surface it will be drawn on. Skipping it is how a pale desktop
     * ends up with a yellow accent at 1.4:1 — text that is not there at all —
     * and it is a no-op on a dark surface, so it costs the usual case nothing.
     */
    if (surface_lum > SURFACE_PALE) {
        syn_contrast_fix(band_accent, out->accent, surface_lum);
        syn_contrast_fix(band_second, out->secondary, surface_lum);
    } else {
        lift_to_contrast(band_accent, surface_lum, out->accent);
        lift_to_contrast(band_second, surface_lum, out->secondary);
    }

    /* accent_dim: the accent, quieter. Derived on purpose — this one IS meant
     * to read as the same colour, for the rules and the rows you are not
     * pointing at, so measuring a third hue would be wrong rather than just
     * unnecessary. Toward the surface rather than toward black: on a light
     * panel "quieter" means paler, and a fixed darkening would make it louder.
     */
    float toward = surface_lum > 0.5 ? 1.0f : 0.0f;
    for (int i = 0; i < 3; i++)
        out->accent_dim[i] = out->accent[i] + (toward - out->accent[i]) * 0.45f;

    out->ok = true;
    return true;
}
