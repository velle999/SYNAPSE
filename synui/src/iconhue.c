/*
 * iconhue.c — see iconhue.h.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "iconhue.h"

/* The two colours the house icons are drawn from. Written as the hex a designer
 * would type, and turned into hues below, so that grepping the palette out of
 * an SVG and grepping it out of this file find the same string. */
#define BRAND_HEX   0xa78bfau   /* the violet: bodies, glyphs, every shade      */
#define SECOND_HEX  0x4ec9b0u   /* the teal: slider knobs, pad bars, disk LED   */

/* How far off-brand a hue may sit and still be treated as part of the family.
 * The house palette spans 254-261 deg, and the nearest colour that is NOT part
 * of it is 57 deg away, so this has room either side and needs none of it. */
#define HUE_WINDOW   (40.0f / 360.0f)

/* Below this saturation a pixel is a neutral — the near-whites, the greys, the
 * near-black bodies. They carry no hue worth moving and moving them tints the
 * icon's whites, which reads as a colour cast rather than a theme. */
#define SAT_FLOOR    0.10f

/* The teal is not decoration: it is the one detail in syn-settings, syn-arcade
 * and syn-disks that is deliberately NOT the brand colour, and its whole job is
 * to be distinguishable from it. On most themes it needs no help. On SYNAPSE
 * and Prism — whose accent IS teal — leaving it exactly as drawn collides at
 * 19.5 dE against the recoloured glyph and the knobs vanish into the body they
 * sit on. So it holds its ground until the accent comes within this angle, and
 * then gives way by exactly this much.
 *
 * This one is measured in OKLab hue, not HSL, and is the only constant here
 * that is: it is not asking which pixels are the teal (that is SECOND_WINDOW,
 * below, and the palette is written in HSL) but how far apart two colours end
 * up LOOKING, which is the question OKLab exists to answer. It also has to buy
 * the whole separation on its own now. It used to be flattered by an accident:
 * the glyph beside it was coming out at whatever lightness an HSL rotation left
 * it, which on a teal accent was 27 L* brighter than the icon was drawn, and
 * most of the 37 dE between them was that brightness rather than any decision
 * made here. With the glyph back at its drawn lightness the hue step is all
 * there is, and it buys about 0.8 dE per degree: 45 deg holds the detail 34 dE
 * or better off the glyph on every theme, against 25 dE for the 30 deg this
 * used to be. */
#define COLLIDE      (45.0f * (float)(M_PI / 180.0))

/* The teal gets a TIGHTER window than the brand violet, and the difference is
 * not fussiness. The violet is a family: five documented members spanning
 * 254-261 deg plus every lightness between them, so it needs room. The teal is
 * one specific detail colour with no shades at all, and widening its window to
 * match the violet's reaches 30.6 deg out and swallows #38bdf8 — the sky blue
 * of the Resolve clapperboard, which is a different colour with a different
 * meaning. Measured on the three icons that carry the detail, 83-86% of their
 * teal-ish pixels sit at distance 0 and the remainder is a thin antialias ramp
 * running continuously into the body, which no boundary separates cleanly. */
#define SECOND_WINDOW (20.0f / 360.0f)

/* Fraction of opaque pixels that must be brand-hued before an icon counts as
 * ours. The two populations are nowhere near each other — 96% and up for the
 * house icons, 0% for syn-resolve-gui — so this sits in open space. */
#define BRAND_SHARE  0.25f

/* ── colour helpers ──────────────────────────────────────── */

static void rgb_to_hsl(float r, float g, float b, float *h, float *s, float *l)
{
    float mx = fmaxf(r, fmaxf(g, b));
    float mn = fminf(r, fminf(g, b));
    float d  = mx - mn;

    *l = (mx + mn) * 0.5f;

    if (d <= 0.0f) {          /* a true grey has no hue to speak of */
        *h = 0.0f;
        *s = 0.0f;
        return;
    }

    *s = (*l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn);

    if (mx == r)      *h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) *h = (b - r) / d + 2.0f;
    else              *h = (r - g) / d + 4.0f;
    *h /= 6.0f;
}

/* Shortest distance between two hues on the circle, both in turns. */
static float hue_dist(float a, float b)
{
    float d = fabsf(a - b);
    if (d > 1.0f) d = fmodf(d, 1.0f);
    return (d > 0.5f) ? 1.0f - d : d;
}

/* The same question in OKLab, where hue is an angle in radians. */
static float angle_gap(float a, float b)
{
    float d = fmodf(fabsf(a - b), 2.0f * (float)M_PI);
    return (d > (float)M_PI) ? 2.0f * (float)M_PI - d : d;
}

static void hex_rgb(unsigned hex, float *r, float *g, float *b)
{
    *r = ((hex >> 16) & 0xff) / 255.0f;
    *g = ((hex >>  8) & 0xff) / 255.0f;
    *b = ( hex        & 0xff) / 255.0f;
}

static void hex_hsl(unsigned hex, float *h, float *s, float *l)
{
    float r, g, b;
    hex_rgb(hex, &r, &g, &b);
    rgb_to_hsl(r, g, b, h, s, l);
}

static float hex_hue(unsigned hex)
{
    float h, s, l;
    hex_hsl(hex, &h, &s, &l);
    return h;
}

/* Where the teal detail should land for a given accent, as an OKLab hue:
 * exactly where it was drawn, unless the accent has walked into it. */
static float second_hue(float accent_h, float teal_h)
{
    if (angle_gap(teal_h, accent_h) >= COLLIDE)
        return teal_h;

    /* Step off the accent on whichever side the teal already sits, so the
     * detail moves the short way and stays recognisably itself. */
    float fwd = fmodf(teal_h - accent_h + 2.0f * (float)M_PI, 2.0f * (float)M_PI);
    return (fwd > (float)M_PI) ? accent_h - COLLIDE : accent_h + COLLIDE;
}

/* ── OKLab: the space the recolour actually happens in ───── */

/* HSL's L is a channel average, not a brightness: #a78bfa and a yellow-green at
 * the same L = 0.76 are 30 points of CIE L* apart, because green carries five
 * times the luminance of blue. Rotating the hue with L held is therefore not
 * "the same icon in another colour" — it is the same icon several stops
 * brighter, and how much brighter depends entirely on where the accent landed.
 * The violet is 64.6 L*; held at its own HSL lightness it comes out 94.9 on the
 * wallpaper's yellow-green, 91.7 on SYNAPSE's cyan and 85.2 on Nord's frost,
 * and 62-72 on every purple, pink and blue. That is why this looked right on
 * most themes and shipped a highlighter on a green one.
 *
 * OKLab's L IS perceived lightness, so rotating there gives every accent the
 * lightness the icon was drawn at, and the drawing's internal light/dark
 * structure survives the move instead of being flattened by whichever hue the
 * theme happens to use. */

static float srgb_to_linear(float u)
{
    return (u <= 0.04045f) ? u / 12.92f : powf((u + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float u)
{
    if (u <= 0.0f) return 0.0f;
    if (u >= 1.0f) return 1.0f;
    return (u <= 0.0031308f) ? u * 12.92f
                             : 1.055f * powf(u, 1.0f / 2.4f) - 0.055f;
}

static void rgb_to_oklch(float r, float g, float b,
                         float *L, float *C, float *hh)
{
    float lr = srgb_to_linear(r), lg = srgb_to_linear(g), lb = srgb_to_linear(b);

    float l = cbrtf(0.4122214708f * lr + 0.5363325363f * lg + 0.0514459929f * lb);
    float m = cbrtf(0.2119034982f * lr + 0.6806995451f * lg + 0.1073969566f * lb);
    float s = cbrtf(0.0883024619f * lr + 0.2817188376f * lg + 0.6299787005f * lb);

    float a_ = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    float b_ = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;

    *L  = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    *C  = hypotf(a_, b_);
    *hh = atan2f(b_, a_);
}

static void oklch_to_linear(float L, float C, float hh,
                            float *r, float *g, float *b)
{
    float a_ = C * cosf(hh), b2 = C * sinf(hh);

    float l_ = L + 0.3963377774f * a_ + 0.2158037573f * b2;
    float m_ = L - 0.1055613458f * a_ - 0.0638541728f * b2;
    float s_ = L - 0.0894841775f * a_ - 1.2914855480f * b2;
    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;

    *r =  4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    *g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    *b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

static bool in_gamut(float r, float g, float b)
{
    const float e = 1.0f / 512.0f;   /* under half a step at 8 bits */
    return r >= -e && g >= -e && b >= -e &&
           r <= 1.0f + e && g <= 1.0f + e && b <= 1.0f + e;
}

/* sRGB for an OKLCh colour. Hues do not all reach the same distance out — a
 * violet's chroma is simply not available at that lightness in yellow — and
 * something has to give when it does not fit. It is always the CHROMA: give up
 * lightness instead and the icon's drawing changes shape, which is the failure
 * this whole file exists to avoid. */
static void oklch_to_rgb(float L, float C, float hh,
                         float *r, float *g, float *b)
{
    oklch_to_linear(L, C, hh, r, g, b);

    if (!in_gamut(*r, *g, *b)) {
        float lo = 0.0f, hi = C;
        for (int i = 0; i < 16; i++) {
            float mid = 0.5f * (lo + hi), tr, tg, tb;
            oklch_to_linear(L, mid, hh, &tr, &tg, &tb);
            if (in_gamut(tr, tg, tb)) lo = mid; else hi = mid;
        }
        oklch_to_linear(L, lo, hh, r, g, b);
    }

    *r = linear_to_srgb(*r);
    *g = linear_to_srgb(*g);
    *b = linear_to_srgb(*b);
}

/* ── public ──────────────────────────────────────────────── */

bool syn_iconhue_wants(const char *icon_name, const unsigned char *data,
                       int w, int h, int stride)
{
    if (!icon_name || strncmp(icon_name, "syn", 3) != 0)
        return false;
    if (!data)
        return true;          /* name-only question; see the header */
    if (w <= 0 || h <= 0)
        return false;

    const float brand_h = hex_hue(BRAND_HEX);
    long opaque = 0, brand = 0;

    for (int y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)(data + (size_t)y * stride);
        for (int x = 0; x < w; x++) {
            uint32_t px = row[x];
            unsigned a = (px >> 24) & 0xff;
            if (a < 128) continue;          /* edges and holes do not vote */
            opaque++;

            /* Un-premultiply before asking what colour this is. */
            float r = fminf(1.0f, (float)((px >> 16) & 0xff) / (float)a);
            float g = fminf(1.0f, (float)((px >>  8) & 0xff) / (float)a);
            float b = fminf(1.0f, (float)( px        & 0xff) / (float)a);

            float hh, ss, ll;
            rgb_to_hsl(r, g, b, &hh, &ss, &ll);
            if (ss >= SAT_FLOOR && hue_dist(hh, brand_h) <= HUE_WINDOW)
                brand++;
        }
    }

    if (opaque == 0) return false;
    return (float)brand / (float)opaque >= BRAND_SHARE;
}

void syn_iconhue_apply(unsigned char *data, int w, int h, int stride,
                       const float accent_rgb[3])
{
    if (!data || !accent_rgb || w <= 0 || h <= 0) return;

    const float brand_h = hex_hue(BRAND_HEX);

    const float teal_h = hex_hue(SECOND_HEX);

    float accent_h, accent_s, accent_l;
    rgb_to_hsl(accent_rgb[0], accent_rgb[1], accent_rgb[2],
               &accent_h, &accent_s, &accent_l);

    /* A greyscale accent has no hue to give. Leave the icons as drawn rather
     * than rotating the whole family to red. */
    if (accent_s < SAT_FLOOR)
        return;

    /* Where the family is going, and how much colour it gets to carry when it
     * arrives. The chroma ratio is what makes a themed icon belong to its
     * theme: the violet is a vivid colour and Nord's frost blue is a quiet one,
     * and an icon that keeps the violet's chroma on a quiet theme is a poster
     * pinned to a muted desktop. It is a ratio and not the accent's chroma
     * outright, because the family has members at several chromas and their
     * spacing is part of the drawing. */
    float acc_L, acc_C, acc_hue;
    rgb_to_oklch(accent_rgb[0], accent_rgb[1], accent_rgb[2],
                 &acc_L, &acc_C, &acc_hue);

    float brand_r, brand_g, brand_b, brand_L, brand_C, brand_hue;
    hex_rgb(BRAND_HEX, &brand_r, &brand_g, &brand_b);
    rgb_to_oklch(brand_r, brand_g, brand_b, &brand_L, &brand_C, &brand_hue);

    const float chroma_k = (brand_C > 0.0f) ? acc_C / brand_C : 1.0f;

    float teal_r, teal_g, teal_b, teal_L, teal_C, teal_ok_hue;
    hex_rgb(SECOND_HEX, &teal_r, &teal_g, &teal_b);
    rgb_to_oklch(teal_r, teal_g, teal_b, &teal_L, &teal_C, &teal_ok_hue);

    const float second_ok_hue = second_hue(acc_hue, teal_ok_hue);
    const bool  teal_moves    = (second_ok_hue != teal_ok_hue);

    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * stride);

        for (int x = 0; x < w; x++) {
            uint32_t px = row[x];
            unsigned a = (px >> 24) & 0xff;
            if (a == 0) continue;

            float r = fminf(1.0f, (float)((px >> 16) & 0xff) / (float)a);
            float g = fminf(1.0f, (float)((px >>  8) & 0xff) / (float)a);
            float b = fminf(1.0f, (float)( px        & 0xff) / (float)a);

            float hh, ss, ll;
            rgb_to_hsl(r, g, b, &hh, &ss, &ll);

            float target, chroma_scale;
            if (ss < SAT_FLOOR) {
                continue;
            } else if (hue_dist(hh, brand_h) <= HUE_WINDOW) {
                target = acc_hue;
                chroma_scale = chroma_k;
            } else if (hue_dist(hh, teal_h) <= SECOND_WINDOW) {
                /* On the 11 themes where nothing collides, the detail is left
                 * exactly as it was drawn — not rotated onto itself, which
                 * would still nudge the antialias ramp around it. And when it
                 * does give way it keeps its own chroma: its entire job is to
                 * NOT be the accent, so it is not the accent's to mute. */
                if (!teal_moves) continue;
                target = second_ok_hue;
                chroma_scale = 1.0f;
            } else {
                continue;
            }

            float pix_L, pix_C, pix_hue;
            rgb_to_oklch(r, g, b, &pix_L, &pix_C, &pix_hue);
            oklch_to_rgb(pix_L, pix_C * chroma_scale, target, &r, &g, &b);

            /* Back to premultiplied, the way cairo wants it. */
            unsigned nr = (unsigned)lrintf(r * a);
            unsigned ng = (unsigned)lrintf(g * a);
            unsigned nb = (unsigned)lrintf(b * a);
            if (nr > a) nr = a;
            if (ng > a) ng = a;
            if (nb > a) nb = a;

            row[x] = ((uint32_t)a << 24) | (nr << 16) | (ng << 8) | nb;
        }
    }
}
