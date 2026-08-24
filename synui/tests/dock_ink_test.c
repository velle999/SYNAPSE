/*
 * dock_ink_test.c — the colour the dock's own marks are drawn in, against every
 * shipped theme and against the wallpaper a fresh install boots onto.
 *
 * The clock, the apps grid, the power mark and the running dots are drawn by
 * synui rather than pulled from an icon theme, in `panel_ink` — the colour the
 * theme chose to read on `panel_bg`. That was the same colour the mark landed
 * on for as long as every PALE preset drew a SOLID dock, and it stopped being
 * true the moment a pale preset went glass.
 *
 * Reported as the clock, the apps button and the power icon being "dark and
 * washed out in prism light while fine in prism". They were: at the house
 * dock_opacity of 0.05 there is no surface under the mark at all, so Prism
 * Light's near-black ink was landing straight on a night photograph.
 *
 * syn_mark_ink() is the rescue, and it has to satisfy two claims that pull in
 * opposite directions — the same shape as panel_contrast_test, and for the same
 * reason:
 *
 *   1. THE BROKEN CASE IS FIXED. Prism Light's marks clear AA large text on the
 *      stock wallpaper at the stock dock alpha, where they measured 1.90:1.
 *
 *   2. NOTHING ELSE MOVES. AT ALL. Fourteen presets look right today, some of
 *      them at contrasts that are low by choice, and a "fix" that repaints them
 *      is a worse bug than the one being fixed. Every preset that still reads
 *      must come back bit-for-bit — same ink, same accent, rescued == false.
 *
 * Pure arithmetic over colours and one measured backdrop, so it links
 * contrast.c alone and needs no compositor.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "contrast.h"

static int fails;
#define CHECK(cond, ...) do { \
    if (cond) { printf("  ok    "); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)

/*
 * The presets, transcribed from theme_presets[] in theme.c exactly as
 * panel_contrast_test transcribes them, and duplicated for the same reason: if
 * somebody edits a preset this test should keep asserting the OLD contract
 * until they come here and say the new colour is what they meant.
 *
 * `glass` is theme_is_glass() in synui.h — the three presets whose dock is
 * see-through by default (dock_style AUTO resolves through it), and therefore
 * the only three that can reach the thin-surface case at all.
 */
static const struct {
    const char *name;
    float bg[3];
    float ink[3];
    float accent[3];
    int   glass;
} themes[] = {
    { "synapse",    { 0.060f, 0.060f, 0.120f }, { 0.950f, 0.950f, 1.000f },
                    { 0.00f,  0.85f,  0.75f  }, 0 },
    { "dark",       { 0.118f, 0.118f, 0.141f }, { 0.922f, 0.922f, 0.949f },
                    { 0.24f,  0.49f,  1.00f  }, 0 },
    { "winxp",      { 0.925f, 0.914f, 0.847f }, { 0.000f, 0.000f, 0.000f },
                    { 0.36f,  0.62f,  1.00f  }, 0 },
    { "win95",      { 0.753f, 0.753f, 0.753f }, { 0.000f, 0.000f, 0.000f },
                    { 0.45f,  0.60f,  0.95f  }, 0 },
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
    { "bubblegum",  { 1.000f, 0.914f, 0.949f }, { 0.239f, 0.102f, 0.165f },
                    { 1.000f, 0.518f, 0.741f }, 0 },
    { "macos26",    { 0.961f, 0.961f, 0.969f }, { 0.114f, 0.114f, 0.122f },
                    { 0.000f, 0.478f, 1.000f }, 1 },
    { "aqua",       { 0.925f, 0.925f, 0.925f }, { 0.000f, 0.000f, 0.000f },
                    { 0.208f, 0.424f, 0.737f }, 0 },
    { "platinum",   { 0.867f, 0.867f, 0.867f }, { 0.000f, 0.000f, 0.000f },
                    { 0.239f, 0.239f, 0.561f }, 0 },
    { "prism",      { 0.098f, 0.110f, 0.137f }, { 0.902f, 0.918f, 0.945f },
                    { 0.000f, 0.839f, 0.898f }, 1 },
    { "prism-light",{ 0.933f, 0.945f, 0.965f }, { 0.102f, 0.114f, 0.141f },
                    { 0.000f, 0.447f, 0.494f }, 1 },
};
#define NTHEMES ((int)(sizeof themes / sizeof themes[0]))

/* dock.c's trip point. AA large text, deliberately not CONTRAST_TARGET — see
 * syn_mark_ink()'s header. */
#define TRIP 3.0

/* dock_opacity: what the house desktop ships (syn-install's synuirc, and what
 * glass_level 100 resolves to), and the compiled default a solid dock keeps. */
#define ALPHA_GLASS 0.05
#define ALPHA_SOLID 0.72

/*
 * The backdrop under a centred floating pill on the wallpaper a fresh install
 * boots onto — /usr/share/backgrounds/commons-st-louis-night.jpg, filled onto a
 * 16:9 output, folded over the bottom row of the 16x9 luminance grid across the
 * columns the dock covers.
 *
 * Measured, not guessed: this is the picture the reported screenshots were
 * taken on, and it is a night photograph, so the whole strip is dark. That is
 * what makes it the discriminating case — a dark backdrop is the one a pale
 * theme's ink cannot survive and a dark theme's sails through.
 */
static const syn_backdrop_t stock = {
    .lum = 0.0561, .lum_min = 0.0255, .lum_max = 0.1083,
    .ink = SYN_INK_LIGHT, .best = SYN_INK_LIGHT,
};

/* A bright wallpaper — a snow field, a white beach — where the answer has to
 * run the other way or the rule is only half a rule. */
static const syn_backdrop_t bright = {
    .lum = 0.780, .lum_min = 0.700, .lum_max = 0.880,
    .ink = SYN_INK_DARK, .best = SYN_INK_DARK,
};

/* The mid-tone band where NEITHER black nor white clears, which is the case
 * that must produce no change rather than a coin flip. */
static const syn_backdrop_t midband = {
    .lum = 0.205, .lum_min = 0.200, .lum_max = 0.210,
    .ink = SYN_INK_NONE, .best = SYN_INK_LIGHT,
};

/* An external client painting the background — wallpaper-engine. Genuinely
 * unknowable, and an ink cannot be chosen for pixels nobody measured. */
static const syn_backdrop_t unmeasured = {
    .lum = -1.0, .lum_min = -1.0, .lum_max = -1.0,
    .ink = SYN_INK_NONE, .best = SYN_INK_NONE,
};

static int same(const float a[3], const float b[3])
{
    for (int i = 0; i < 3; i++)
        if (fabsf(a[i] - b[i]) > 1e-6f) return 0;
    return 1;
}

/* The mark's worst contrast over the composite, which is the number the whole
 * feature exists to move. */
static double worst(const float surface[3], double alpha,
                    const float mark[3], const syn_backdrop_t *bd)
{
    double surf = syn_rel_luminance(surface[0], surface[1], surface[2]);
    double m    = syn_rel_luminance(mark[0], mark[1], mark[2]);
    double a = syn_contrast_lum(m, syn_lum_over(surf, alpha, bd->lum_min));
    double b = syn_contrast_lum(m, syn_lum_over(surf, alpha, bd->lum_max));
    return a < b ? a : b;
}

static const int PRISM = 13, PRISM_LIGHT = 14;

int main(void)
{
    syn_mark_ink_t k;

    printf("== the reported bug ==\n");
    {
        /* Prism Light, glass dock, the stock wallpaper. */
        const int t = PRISM_LIGHT;
        double before = worst(themes[t].bg, ALPHA_GLASS, themes[t].ink, &stock);
        syn_mark_ink(themes[t].bg, ALPHA_GLASS, themes[t].ink,
                     themes[t].accent, &stock, TRIP, &k);
        double after = worst(themes[t].bg, ALPHA_GLASS, k.ink, &stock);

        CHECK(before < TRIP,
              "prism-light's own ink is illegible on the stock dock (%.2f:1)",
              before);
        CHECK(k.rescued, "…so the mark is rescued");
        CHECK(after >= TRIP,
              "…and now clears AA large text (%.2f:1, was %.2f:1)", after, before);
        CHECK(after > before, "…which is strictly better, never a sideways swap");
        /* A dark backdrop takes light ink. The rescue is two colours only. */
        CHECK(k.ink[0] > 0.9f && same(k.ink, k.ink) && k.ink[0] == k.ink[1] &&
              k.ink[1] == k.ink[2],
              "…in white, because the wallpaper under the dock is dark");
    }

    printf("\n== and its dark twin is untouched ==\n");
    {
        const int t = PRISM;
        double c = worst(themes[t].bg, ALPHA_GLASS, themes[t].ink, &stock);
        syn_mark_ink(themes[t].bg, ALPHA_GLASS, themes[t].ink,
                     themes[t].accent, &stock, TRIP, &k);
        CHECK(c >= TRIP, "prism's own ink already reads there (%.2f:1)", c);
        CHECK(!k.rescued, "…so nothing is rescued");
        CHECK(same(k.ink, themes[t].ink), "…and the ink is the theme's, exactly");
        CHECK(same(k.accent, themes[t].accent), "…and so is the accent");
    }

    printf("\n== it is the GLASS that broke it, not the theme ==\n");
    {
        /* The same theme at the dock alpha it had before the house desktop
         * went to Glass 100. If this trips, the diagnosis is wrong. */
        const int t = PRISM_LIGHT;
        double c = worst(themes[t].bg, ALPHA_SOLID, themes[t].ink, &stock);
        syn_mark_ink(themes[t].bg, ALPHA_SOLID, themes[t].ink,
                     themes[t].accent, &stock, TRIP, &k);
        CHECK(c >= TRIP,
              "prism-light on a 0.72 dock reads fine (%.2f:1) — the body carried it",
              c);
        CHECK(!k.rescued, "…so a solid dock is never rescued");
    }

    printf("\n== nothing else moves, on either wallpaper ==\n");
    /*
     * Every preset at the dock alpha it actually gets: the three glass ones at
     * 0.05, the rest at the 0.72 compiled default. A preset that still reads
     * must come back byte-identical — this is the claim that stops a legibility
     * fix from repainting the desktop.
     */
    for (int i = 0; i < NTHEMES; i++) {
        double a = themes[i].glass ? ALPHA_GLASS : ALPHA_SOLID;
        const syn_backdrop_t *bds[2] = { &stock, &bright };
        const char *nm[2] = { "night", "bright" };
        for (int b = 0; b < 2; b++) {
            double c = worst(themes[i].bg, a, themes[i].ink, bds[b]);
            syn_mark_ink(themes[i].bg, a, themes[i].ink, themes[i].accent,
                         bds[b], TRIP, &k);
            if (c >= TRIP) {
                CHECK(!k.rescued && same(k.ink, themes[i].ink) &&
                      same(k.accent, themes[i].accent),
                      "%-11s on a %-6s wallpaper reads (%.2f:1) and is untouched",
                      themes[i].name, nm[b], c);
            } else {
                /* Allowed to be rescued — but only upward. */
                double after = worst(themes[i].bg, a, k.ink, bds[b]);
                CHECK(after > c,
                      "%-11s on a %-6s wallpaper is illegible (%.2f:1) and is "
                      "rescued to %.2f:1", themes[i].name, nm[b], c, after);
            }
        }
    }

    printf("\n== the rule runs both ways ==\n");
    {
        /* A dark theme on a white beach. If only the pale case were handled
         * this would sit at 1.2:1 and the fix would be half a fix. */
        const int t = PRISM;
        double before = worst(themes[t].bg, ALPHA_GLASS, themes[t].ink, &bright);
        syn_mark_ink(themes[t].bg, ALPHA_GLASS, themes[t].ink,
                     themes[t].accent, &bright, TRIP, &k);
        double after = worst(themes[t].bg, ALPHA_GLASS, k.ink, &bright);
        CHECK(before < TRIP, "prism's white ink drowns on a bright wallpaper "
                             "(%.2f:1)", before);
        CHECK(k.rescued && k.ink[0] < 0.2f,
              "…and is rescued to near-black, not to more white");
        CHECK(after >= TRIP, "…clearing at %.2f:1", after);
    }

    printf("\n== the cases that must do nothing ==\n");
    {
        const int t = PRISM_LIGHT;
        syn_mark_ink(themes[t].bg, ALPHA_GLASS, themes[t].ink,
                     themes[t].accent, &unmeasured, TRIP, &k);
        CHECK(!k.rescued && same(k.ink, themes[t].ink),
              "an unmeasured backdrop passes the theme's ink straight through");

        syn_mark_ink(themes[t].bg, ALPHA_GLASS, themes[t].ink,
                     themes[t].accent, NULL, TRIP, &k);
        CHECK(!k.rescued && same(k.ink, themes[t].ink),
              "…and so does no backdrop at all");

        /* ⚠ The band where BOTH inks fail. Swapping an unreadable dark mark for
         * an unreadable white one is churn, and on a live wallpaper crossing
         * this band it is a dock that flickers. */
        double c = worst(themes[t].bg, ALPHA_GLASS, themes[t].ink, &midband);
        syn_mark_ink(themes[t].bg, ALPHA_GLASS, themes[t].ink,
                     themes[t].accent, &midband, TRIP, &k);
        double after = worst(themes[t].bg, ALPHA_GLASS, k.ink, &midband);
        CHECK(!k.rescued || after > c,
              "the mid-tone band never swaps one illegible ink for another "
              "(%.2f:1 -> %.2f:1)", c, after);
    }

    printf("\n== the hands follow the dial ==\n");
    {
        /*
         * The analog clock's hands are panel_accent, and Prism Light's #00727E
         * is a dark teal picked to read on a near-white panel — on a night
         * photograph it is exactly as invisible as the ink was. It has to move
         * WITH the ink and it has to keep its hue, which is why it is mixed
         * rather than replaced.
         */
        const int t = PRISM_LIGHT;
        syn_mark_ink(themes[t].bg, ALPHA_GLASS, themes[t].ink,
                     themes[t].accent, &stock, TRIP, &k);
        double was = syn_rel_luminance(themes[t].accent[0], themes[t].accent[1],
                                       themes[t].accent[2]);
        double now = syn_rel_luminance(k.accent[0], k.accent[1], k.accent[2]);
        CHECK(now > was, "the accent is lifted with the ink (%.3f -> %.3f)",
              was, now);
        /* Hue held: the teal's blue still leads its red, as it does in #00727E.
         * A replaced accent would be grey and this would be an equality. */
        CHECK(k.accent[2] > k.accent[0] + 0.05f,
              "…and it is still teal, not grey (r %.3f, b %.3f)",
              k.accent[0], k.accent[2]);
        /* Bounded: four quarter-steps can never overshoot past the ink. */
        CHECK(k.accent[0] <= k.ink[0] + 1e-6f && k.accent[2] <= 1.0f,
              "…and never past the ink it is walking toward");
    }

    printf("\n%s\n", fails ? "FAILURES" : "all checks passed");
    return fails ? 1 : 0;
}
