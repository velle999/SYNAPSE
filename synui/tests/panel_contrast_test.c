/*
 * panel_contrast_test.c — the panel legibility correction, against every
 * shipped theme's actual surface and accent.
 *
 * synui's panels take the theme's own surface, so the accents and status
 * colours in render.c — all of which were chosen against the near-black panel
 * synui used to draw everywhere — land on beige under XP and on silver under
 * 95. The correction in contrast.c exists for that, and it has to satisfy TWO
 * claims at once, pulling in opposite directions:
 *
 *   1. On a PALE surface every one of those colours clears 4.5:1. This is the
 *      reported bug: the task manager's meter readings measured 1.04:1 on
 *      #ECE9D8 — the numbers were being drawn, in beige, on beige.
 *
 *   2. On a DARK surface nothing moves. AT ALL. This is the harder claim and
 *      the reason the file is a test rather than a printf: several rice reds
 *      are already under 4.5:1 on their own backgrounds (Gruvbox 3.89:1,
 *      Catppuccin 4.33:1) because that is the palette their authors shipped.
 *      A correction that "fixes" those repaints four working dark themes to
 *      settle a complaint about two light ones. The first draft of
 *      syn_contrast_fix() did precisely that, and this test is what caught it
 *      before it shipped.
 *
 * The surfaces and accents below are transcribed from theme_presets[] in
 * theme.c. They are duplicated deliberately: if someone edits a preset, this
 * test should keep asserting the OLD contract until they come here and say the
 * new colour is what they meant.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "contrast.h"

/* panel_bg is theme.c's base_* pair unless a preset sets panel_bg explicitly
 * (SYNAPSE does); panel_accent is the preset's own field. */
static const struct {
    const char *name;
    float bg[3];            /* panel_bg: base_*, or the explicit field         */
    float ink[3];           /* panel_ink: text_*, or the explicit field        */
    float accent[3];        /* panel_accent                                    */
    int   pale;             /* what the theme's `scheme` says it is            */
} themes[] = {
    { "synapse",    { 0.060f, 0.060f, 0.120f }, { 0.950f, 0.950f, 1.000f },
                    { 0.00f,  0.85f,  0.75f  }, 0 },
    { "dark",       { 0.118f, 0.118f, 0.141f }, { 0.922f, 0.922f, 0.949f },
                    { 0.24f,  0.49f,  1.00f  }, 0 },
    { "winxp",      { 0.925f, 0.914f, 0.847f }, { 0.000f, 0.000f, 0.000f },
                    { 0.36f,  0.62f,  1.00f  }, 1 },
    { "win95",      { 0.753f, 0.753f, 0.753f }, { 0.000f, 0.000f, 0.000f },
                    { 0.45f,  0.60f,  0.95f  }, 1 },
    { "catppuccin", { 0.118f, 0.118f, 0.180f }, { 0.804f, 0.839f, 0.957f },
                    { 0.796f, 0.651f, 0.969f }, 0 },
    { "gruvbox",    { 0.157f, 0.157f, 0.157f }, { 0.922f, 0.859f, 0.698f },
                    { 0.996f, 0.502f, 0.098f }, 0 },
    { "tokyonight", { 0.141f, 0.157f, 0.231f }, { 0.753f, 0.792f, 0.961f },
                    { 0.733f, 0.604f, 0.969f }, 0 },
    { "nord",       { 0.180f, 0.204f, 0.251f }, { 0.847f, 0.871f, 0.914f },
                    { 0.533f, 0.753f, 0.816f }, 0 },
    { "dracula",    { 0.157f, 0.165f, 0.212f }, { 0.973f, 0.973f, 0.949f },
                    { 1.000f, 0.475f, 0.776f }, 0 },
    /* Bubblegum is the third PALE theme and the one nobody remembers: #FFE9F2
     * is a near-white pink, and its accent still carries the comment "on dark
     * chrome" from when every panel was one. It was as unreadable as XP — and
     * its #3D1A2A ink gives it the least headroom of the three. */
    { "bubblegum",  { 1.000f, 0.914f, 0.949f }, { 0.239f, 0.102f, 0.165f },
                    { 1.000f, 0.518f, 0.741f }, 1 },
    /* The three Macs, and all three are PALE — which triples the population of
     * the branch that has actually shipped bugs. macOS 26's #F5F5F7 is the
     * palest surface any preset ships, so it is also the least forgiving to
     * every status colour in render.c. */
    { "macos26",    { 0.961f, 0.961f, 0.969f }, { 0.114f, 0.114f, 0.122f },
                    { 0.000f, 0.478f, 1.000f }, 1 },
    { "aqua",       { 0.925f, 0.925f, 0.925f }, { 0.000f, 0.000f, 0.000f },
                    { 0.208f, 0.424f, 0.737f }, 1 },
    { "platinum",   { 0.867f, 0.867f, 0.867f }, { 0.000f, 0.000f, 0.000f },
                    { 0.239f, 0.239f, 0.561f }, 1 },
};

/* render.c's stat_dark[] — the status colours a panel draws with. */
static const struct { const char *name; float rgb[3]; } stats[] = {
    { "nominal", { 0.00f, 0.85f, 0.75f } },
    { "warn",    { 0.95f, 0.75f, 0.25f } },
    { "crit",    { 0.90f, 0.30f, 0.35f } },
    { "good",    { 0.45f, 0.80f, 0.55f } },
};

static int same(const float a[3], const float b[3])
{
    for (int i = 0; i < 3; i++)
        if (fabsf(a[i] - b[i]) > 1e-6f) return 0;
    return 1;
}

/* One colour, against one theme. Returns 0 on failure. */
static int check(const char *theme, const char *what, const float in[3],
                 double lum, int pale)
{
    float out[3];
    syn_contrast_fix(in, out, lum);

    double before = syn_contrast(in[0], in[1], in[2], lum);
    double after  = syn_contrast(out[0], out[1], out[2], lum);

    if (!pale) {
        /* Claim 2. Not "close enough" — identical. */
        if (!same(in, out)) {
            printf("  FAIL %s/%s: dark theme was repainted "
                   "(%.2f:1 -> %.2f:1)\n", theme, what, before, after);
            return 0;
        }
        return 1;
    }

    /* Claim 1. The bisection converges from below, so allow the last step's
     * worth of slack rather than demanding the target exactly. */
    if (after < CONTRAST_TARGET - 0.01) {
        printf("  FAIL %s/%s: %.2f:1 after correction, want %.2f:1\n",
               theme, what, after, CONTRAST_TARGET);
        return 0;
    }
    printf("  %-10s %-8s %5.2f:1 -> %5.2f:1  #%02x%02x%02x\n",
           theme, what, before, after,
           (int)(out[0] * 255 + 0.5f), (int)(out[1] * 255 + 0.5f),
           (int)(out[2] * 255 + 0.5f));
    return 1;
}

int main(void)
{
    int ok = 1;

    printf("pale surfaces — every colour must clear %.1f:1\n", CONTRAST_TARGET);
    for (size_t t = 0; t < sizeof themes / sizeof *themes; t++) {
        double lum = syn_rel_luminance(themes[t].bg[0], themes[t].bg[1],
                                       themes[t].bg[2]);

        /* The theme's own scheme and the measured luminance have to agree, or
         * the correction fires on the wrong themes and every other assertion
         * here is checking the wrong branch. */
        int measured_pale = lum > SURFACE_PALE;
        if (measured_pale != themes[t].pale) {
            printf("  FAIL %s: scheme says %s, surface measures %.3f\n",
                   themes[t].name, themes[t].pale ? "light" : "dark", lum);
            ok = 0;
            continue;
        }
        if (!themes[t].pale) continue;

        ok &= check(themes[t].name, "accent", themes[t].accent, lum, 1);
        for (size_t i = 0; i < sizeof stats / sizeof *stats; i++)
            ok &= check(themes[t].name, stats[i].name, stats[i].rgb, lum, 1);
    }

    printf("dark surfaces — nothing may move\n");
    for (size_t t = 0; t < sizeof themes / sizeof *themes; t++) {
        if (themes[t].pale) continue;
        double lum = syn_rel_luminance(themes[t].bg[0], themes[t].bg[1],
                                       themes[t].bg[2]);
        ok &= check(themes[t].name, "accent", themes[t].accent, lum, 0);
        for (size_t i = 0; i < sizeof stats / sizeof *stats; i++)
            ok &= check(themes[t].name, stats[i].name, stats[i].rgb, lum, 0);
    }
    printf("  7 dark themes untouched\n");

    /* ── The ink ladder ──────────────────────────────────────
     * Text is drawn as a POSITION between the surface and the ink, which is
     * what makes it flip with the theme — but a position is not a contrast.
     * The mapping is sRGB-linear, so the same rung buys far less separation
     * travelling toward black from silver than toward white from near-black:
     * 0.44 is 4.06:1 on SYNAPSE and 2.89:1 on 95's #C0C0C0. That is what
     * "Windows 95 secondary text is grey on a grey background" measures as.
     *
     * Every level render.c actually passes is listed, bare numbers included —
     * most set_ink() calls do not use a named rung, and a floor that only
     * covered INK_* would have missed the majority of them. */
    static const double used_levels[] = {
        1.00, 0.97, 0.89, 0.85, 0.81, 0.76, 0.72, 0.61,
        0.55, 0.49, 0.44, 0.40, 0.38, 0.33,     /* text */
        0.27, 0.21, 0.18, 0.11, 0.09, 0.04, 0.00, /* rules, washes, fills */
    };
    const double INK_TEXT = 0.30, INK_TEXT_MIN = 4.0;

    printf("ink ladder — text levels on a pale panel\n");
    for (size_t t = 0; t < sizeof themes / sizeof *themes; t++) {
        const float *bg = themes[t].bg, *ink = themes[t].ink;

        double floor = syn_ink_floor(bg, ink, INK_TEXT_MIN);
        double lum   = syn_rel_luminance(bg[0], bg[1], bg[2]);

        if (!themes[t].pale) {
            /* Same claim as above, and the same reason: a dark theme's ladder
             * is its own design. A non-zero floor here would silently rewrite
             * every panel on seven themes. */
            if (floor != 0.0) {
                printf("  FAIL %s: dark theme got an ink floor of %.3f\n",
                       themes[t].name, floor);
                ok = 0;
            }
            continue;
        }

        double worst = 99.0;
        for (size_t i = 0; i < sizeof used_levels / sizeof *used_levels; i++) {
            double lv = used_levels[i];
            if (lv < INK_TEXT) continue;               /* not text; left alone */
            if (lv < floor)    lv = floor;             /* what set_ink() does */
            double c = syn_contrast(bg[0] + (ink[0] - bg[0]) * lv,
                                    bg[1] + (ink[1] - bg[1]) * lv,
                                    bg[2] + (ink[2] - bg[2]) * lv, lum);
            if (c < worst) worst = c;
        }
        printf("  %-10s floor=%.3f  worst text level %5.2f:1\n",
               themes[t].name, floor, worst);
        if (worst < INK_TEXT_MIN - 0.01) {
            printf("  FAIL %s: secondary text still at %.2f:1\n",
                   themes[t].name, worst);
            ok = 0;
        }
    }

    printf("%s\n", ok ? "PASS" : "FAILED");
    assert(ok);
    return ok ? 0 : 1;
}
