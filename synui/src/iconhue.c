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
 * then gives way by exactly this much: identical to the drawn colour on 11 of
 * the 14 themes, and never closer than 37 dE on any of them. */
#define COLLIDE      (30.0f / 360.0f)

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

static float hue_channel(float p, float q, float t)
{
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

static void hsl_to_rgb(float h, float s, float l, float *r, float *g, float *b)
{
    if (s <= 0.0f) {
        *r = *g = *b = l;
        return;
    }
    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    *r = hue_channel(p, q, h + 1.0f / 3.0f);
    *g = hue_channel(p, q, h);
    *b = hue_channel(p, q, h - 1.0f / 3.0f);
}

/* Shortest distance between two hues on the circle, both in turns. */
static float hue_dist(float a, float b)
{
    float d = fabsf(a - b);
    if (d > 1.0f) d = fmodf(d, 1.0f);
    return (d > 0.5f) ? 1.0f - d : d;
}

static float hex_hue(unsigned hex)
{
    float h, s, l;
    rgb_to_hsl(((hex >> 16) & 0xff) / 255.0f,
               ((hex >>  8) & 0xff) / 255.0f,
               ( hex        & 0xff) / 255.0f, &h, &s, &l);
    return h;
}

/* Where the teal detail should land for a given accent: exactly where it was
 * drawn, unless the accent has walked into it. */
static float second_hue(float accent_h, float teal_h)
{
    if (hue_dist(teal_h, accent_h) >= COLLIDE)
        return teal_h;

    /* Step off the accent on whichever side the teal already sits, so the
     * detail moves the short way and stays recognisably itself. */
    float fwd = teal_h - accent_h;
    if (fwd < 0.0f) fwd += 1.0f;
    float out = (fwd > 0.5f) ? accent_h - COLLIDE : accent_h + COLLIDE;
    if (out < 0.0f) out += 1.0f;
    if (out >= 1.0f) out -= 1.0f;
    return out;
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
    const float teal_h  = hex_hue(SECOND_HEX);

    float accent_h, accent_s, accent_l;
    rgb_to_hsl(accent_rgb[0], accent_rgb[1], accent_rgb[2],
               &accent_h, &accent_s, &accent_l);

    /* A greyscale accent has no hue to give. Leave the icons as drawn rather
     * than rotating the whole family to red. */
    if (accent_s < SAT_FLOOR)
        return;

    const float second_h = second_hue(accent_h, teal_h);

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

            float target;
            if (ss < SAT_FLOOR)                            target = -1.0f;
            else if (hue_dist(hh, brand_h) <= HUE_WINDOW)  target = accent_h;
            else if (hue_dist(hh, teal_h) <= SECOND_WINDOW) target = second_h;
            else                                           target = -1.0f;

            if (target < 0.0f) continue;

            hsl_to_rgb(target, ss, ll, &r, &g, &b);

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
