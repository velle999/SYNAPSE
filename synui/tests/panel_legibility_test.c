/*
 * panel_legibility_test.c — can you actually READ a panel, on every theme, over
 * anything a panel can open on top of.
 *
 * panel_contrast_test already asks a version of this and it did not catch the
 * bug this file was written for, which is the interesting part. That one asks
 * whether the palette works on the theme's OWN panel surface. Every panel synui
 * draws is glass now, and a glass panel's surface is not the theme's: it is the
 * theme's colour composited over whatever the panel opened on top of, which on a
 * desktop is a window far more often than the wallpaper.
 *
 * Reported as the emoji picker's unhighlighted categories and the keyboard
 * shortcuts panel being hard to read. Measured off the screenshot, on stock
 * Prism with the shortcuts panel over a white web page — the panel composites to
 * L=0.135:
 *
 *     key combos and title (accent) ....... 1.49:1
 *     the hint line (INK_DIM) ............. 1.24:1
 *     the count (INK_LABEL) ............... 1.70:1
 *     descriptions (INK_BODY) ............. 3.20:1
 *
 * Both correctors open with `if (surface_lum <= SURFACE_PALE) return`, because
 * both were written when a panel surface was the theme's own opaque colour —
 * dark (nothing to do) or pale (correct downward). 0.135 is neither, so both
 * returned at the first line and every derived colour kept its dark-theme
 * tuning on a mid-tone surface.
 *
 * So this asks the question the other file cannot: for every theme, at the alpha
 * a panel really draws at, over each of the backdrops a panel really opens on —
 * does every colour that is TEXT still separate from the surface?
 *
 * Two claims, and the second is the one that constrains the fix:
 *
 *   1. EVERY TEXT COLOUR CLEARS ITS GOAL on a glass composite.
 *   2. AN OPAQUE PANEL IS UNTOUCHED, BIT-FOR-BIT. The goal is relative — what
 *      the theme's own surface would have given, capped at the target — so on
 *      an opaque panel it is met by construction and no corrector may move a
 *      single channel. This is what stops a legibility fix from repainting
 *      eleven working themes, which is the failure panel_contrast_test's own
 *      header describes and the reason its claim 2 exists.
 *
 * Pure arithmetic, so it links contrast.c alone and needs no compositor.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <math.h>
#include <stdio.h>

#include "contrast.h"

static int fails;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); fails++; } \
} while (0)
#define NOTE(...) do { printf("  ok    "); printf(__VA_ARGS__); printf("\n"); } while (0)

/*
 * The ink ladder, transcribed from render.c. Only the rungs that are TEXT: at or
 * above INK_TEXT the level takes the floor, and below it a level is a rule, a
 * wash or a fill — something drawn rather than read — which is exactly the
 * distinction set_ink() makes.
 */
#define INK_TEXT      0.30
#define INK_TEXT_MIN  4.0
static const struct { const char *name; double level; } rungs[] = {
    { "INK_TITLE", 0.85 }, { "INK_BODY",  0.81 }, { "INK_MUTED", 0.72 },
    { "INK_LABEL", 0.55 }, { "INK_DIM",   0.44 },
};
#define NRUNGS ((int)(sizeof rungs / sizeof rungs[0]))

/* render.c's stat_dark[] — the four status colours a panel draws readings in. */
static const struct { const char *name; float rgb[3]; } stats[] = {
    { "nominal", { 0.00f, 0.85f, 0.75f } },
    { "warn",    { 0.95f, 0.75f, 0.25f } },
    { "crit",    { 0.90f, 0.30f, 0.35f } },
    { "good",    { 0.45f, 0.80f, 0.55f } },
};
#define NSTATS ((int)(sizeof stats / sizeof stats[0]))

/* theme_presets[] from theme.c, the same transcription panel_contrast_test
 * carries and duplicated for the same reason: edit a preset and this keeps
 * asserting the old contract until somebody comes here and says otherwise. */
static const struct {
    const char *name;
    float bg[3], ink[3], accent[3];
} themes[] = {
    { "synapse",    { 0.060f, 0.060f, 0.120f }, { 0.950f, 0.950f, 1.000f }, { 0.00f,  0.85f,  0.75f  } },
    { "dark",       { 0.118f, 0.118f, 0.141f }, { 0.922f, 0.922f, 0.949f }, { 0.24f,  0.49f,  1.00f  } },
    { "winxp",      { 0.925f, 0.914f, 0.847f }, { 0.000f, 0.000f, 0.000f }, { 0.36f,  0.62f,  1.00f  } },
    { "win95",      { 0.753f, 0.753f, 0.753f }, { 0.000f, 0.000f, 0.000f }, { 0.45f,  0.60f,  0.95f  } },
    { "catppuccin", { 0.118f, 0.118f, 0.180f }, { 0.804f, 0.839f, 0.957f }, { 0.796f, 0.651f, 0.969f } },
    { "gruvbox",    { 0.157f, 0.157f, 0.157f }, { 0.922f, 0.859f, 0.698f }, { 0.996f, 0.502f, 0.098f } },
    { "tokyonight", { 0.141f, 0.157f, 0.231f }, { 0.753f, 0.792f, 0.961f }, { 0.733f, 0.604f, 0.969f } },
    { "nord",       { 0.180f, 0.204f, 0.251f }, { 0.847f, 0.871f, 0.914f }, { 0.533f, 0.753f, 0.816f } },
    { "dracula",    { 0.157f, 0.165f, 0.212f }, { 0.973f, 0.973f, 0.949f }, { 1.000f, 0.475f, 0.776f } },
    { "bubblegum",  { 1.000f, 0.914f, 0.949f }, { 0.239f, 0.102f, 0.165f }, { 1.000f, 0.518f, 0.741f } },
    { "macos26",    { 0.961f, 0.961f, 0.969f }, { 0.114f, 0.114f, 0.122f }, { 0.000f, 0.478f, 1.000f } },
    { "aqua",       { 0.925f, 0.925f, 0.925f }, { 0.000f, 0.000f, 0.000f }, { 0.208f, 0.424f, 0.737f } },
    { "platinum",   { 0.867f, 0.867f, 0.867f }, { 0.000f, 0.000f, 0.000f }, { 0.239f, 0.239f, 0.561f } },
    { "prism",      { 0.098f, 0.110f, 0.137f }, { 0.902f, 0.918f, 0.945f }, { 0.000f, 0.839f, 0.898f } },
    { "prism-light",{ 0.933f, 0.945f, 0.965f }, { 0.102f, 0.114f, 0.141f }, { 0.000f, 0.447f, 0.494f } },
};
#define NTHEMES ((int)(sizeof themes / sizeof themes[0]))

/*
 * What a panel opens on top of. These are not hypotheticals — each is a thing
 * somebody does, and the mid ones are the band both correctors used to skip.
 */
static const struct { const char *name; double lo, hi, mean; } backs[] = {
    /* The reported case: a panel over a white web page. */
    { "white page",     0.90, 0.95, 0.92 },
    /* The stock wallpaper's bottom strip, measured — a night photograph. */
    { "night photo",    0.0255, 0.1083, 0.0561 },
    /* A dark editor, which is most of what is on this desktop. */
    { "dark editor",    0.01, 0.04, 0.02 },
    /* Mid-tone, the band where neither black nor white ink clears on its own. */
    { "mid grey",       0.19, 0.22, 0.205 },
    /* A photograph spanning both extremes at once — one panel, one colour of
     * text, and it has to survive the worst cell rather than the mean. */
    { "sky over ground", 0.02, 0.85, 0.40 },
};
#define NBACKS ((int)(sizeof backs / sizeof backs[0]))

/* The alphas panels are tuned at in render.c's call sites — a menu is glassier
 * than the task manager's dense table — and what the Glass slider replaces them
 * with at the house level of 100 (SYN_BAR_ALPHA_FROSTED). */
static const double alphas[] = { 0.05, 0.86, 0.94, 1.00 };
#define NALPHAS ((int)(sizeof alphas / sizeof alphas[0]))

static double lum3_fwd(const float c[3])
{
    return syn_rel_luminance(c[0], c[1], c[2]);
}

/*
 * render.c's panel_alpha_floor(), transcribed.
 *
 * ⚠ THE FIRST DRAFT OF THIS FILE LEFT IT OUT AND EVERY GLASS CASE "FAILED".
 * That was the test being wrong, not the code: no panel with the legibility
 * correction on ever draws at its tuned alpha over a bright window. It walks up
 * until the FULL-STRENGTH ink clears CONTRAST_TARGET at BOTH extremes of what
 * is behind it, and only then are the colours resolved.
 *
 * It is also the exact shape of the bug. The walk asks about ONE colour —
 * g_panel_ink — and every other thing the panel draws is derived afterwards
 * from a surface chosen to suit that one colour. The ink clears 4.75:1 while the
 * accent sits at 1.49:1, and nothing in the walk can see that.
 */
static double alpha_floor(double want, const float ink[3], double own,
                          double lo, double hi)
{
    if (want >= 1.0) return want;
    double i = lum3_fwd(ink);
    for (double a = want; a < 1.0; a += 0.02)
        if (syn_contrast_lum(i, syn_lum_over(own, a, lo)) >= CONTRAST_TARGET &&
            syn_contrast_lum(i, syn_lum_over(own, a, hi)) >= CONTRAST_TARGET)
            return a;
    return 1.0;
}

static void ladder_at(float out[3], const float bg[3], const float ink[3], double t)
{
    for (int i = 0; i < 3; i++) out[i] = (float)(bg[i] + (ink[i] - bg[i]) * t);
}
static double lum3(const float c[3])
{
    return syn_rel_luminance(c[0], c[1], c[2]);
}
static int same(const float a[3], const float b[3])
{
    for (int i = 0; i < 3; i++) if (fabsf(a[i] - b[i]) > 1e-6f) return 0;
    return 1;
}

/*
 * render.c's panel_fix_color(), in the same order: the pale-surface corrector,
 * then the composite one. Transcribed rather than shared because the shipped
 * one is a static in a file that needs the compositor — the two FUNCTIONS it
 * calls are the shipped ones, which is where the bug was.
 */
static void fix(const float in[3], float out[3], double own, double surf)
{
    syn_contrast_fix(in, out, surf);
    syn_glass_restore(out, out, own, surf, CONTRAST_TARGET);
}

/* The goal a colour is held to: what the theme's own surface gives it, capped
 * at the target. Mirrors syn_glass_restore()'s own rule, which is what makes
 * "an opaque panel is untouched" true by arithmetic rather than by a gate. */
static double goal_for(const float c[3], double own, double target)
{
    double g = syn_contrast_lum(lum3(c), own);
    return g > target ? target : g;
}

int main(void)
{
    int checks = 0, glass_cases = 0;

    printf("== the reported panel: stock Prism over a white page ==\n");
    {
        /*
         * ⚠ THE SURFACE IS MEASURED OFF THE SCREENSHOT, NOT DERIVED. Sampling
         * synapse-20260824-010053.png inside the shortcuts panel gives
         * #646A6E — L=0.1354 — and the accent's glyph cores give #D66318, which
         * is exactly the `accent=` in palette.state. Deriving it from a guessed
         * alpha instead put the first draft of this test at L=0.043 and had it
         * assert the bug did not exist.
         */
        const int t = 13;                        /* prism */
        const float wp[3] = { 0.839f, 0.388f, 0.094f };   /* #D66318, measured */
        double own  = lum3(themes[t].bg);
        double surf = 0.1354;

        float before[3], after[3];
        syn_contrast_fix(wp, before, surf);      /* what shipped */
        fix(wp, after, own, surf);               /* what ships now */
        double c0 = syn_contrast_lum(lum3(before), surf);
        double c1 = syn_contrast_lum(lum3(after),  surf);
        double goal = goal_for(wp, own, CONTRAST_TARGET);

        CHECK(fabs(c0 - 1.53) < 0.05,
              "the old accent should measure 1.53:1 as it does on screen (got %.2f)",
              c0);
        CHECK(c1 >= goal - 0.01,
              "the accent must reach its goal (%.2f:1, goal %.2f)", c1, goal);
        NOTE("accent on the measured surface: %.2f:1 -> %.2f:1 (goal %.2f)",
             c0, c1, goal);

        /* The hint line, which measured 1.24:1 on screen. */
        float dim[3];
        ladder_at(dim, themes[t].bg, themes[t].ink, 0.44);   /* INK_DIM */
        double d0 = syn_contrast_lum(lum3(dim), surf);
        double fl = syn_ink_floor_glass(themes[t].bg, themes[t].ink,
                                        INK_TEXT, surf, INK_TEXT_MIN);
        float lifted[3];
        ladder_at(lifted, themes[t].bg, themes[t].ink, fl > 0.44 ? fl : 0.44);
        double d1 = syn_contrast_lum(lum3(lifted), surf);
        CHECK(d0 < 1.6, "the old hint line should measure near 1.24:1 (got %.2f)", d0);
        CHECK(d1 > d0 * 1.5, "the ladder floor must lift it materially "
                             "(%.2f:1 -> %.2f:1)", d0, d1);
        NOTE("INK_DIM: %.2f:1 -> %.2f:1 (floor lifts the rung to %.2f)", d0, d1, fl);
    }

    printf("\n== claim 1: every text colour clears its goal, everywhere ==\n");
    for (int i = 0; i < NTHEMES; i++) {
        double own = lum3(themes[i].bg);
        for (int b = 0; b < NBACKS; b++) {
            for (int a = 0; a < NALPHAS; a++) {
                /* The WORST cell, not the mean — a panel draws one colour of
                 * text and has to survive both ends of what it lies on. */
                /* What the panel ACTUALLY draws at — the walk runs first. */
                double drawn = alpha_floor(alphas[a], themes[i].ink, own,
                                           backs[b].lo, backs[b].hi);
                double s_lo = syn_lum_over(own, drawn, backs[b].lo);
                double s_hi = syn_lum_over(own, drawn, backs[b].hi);
                const double surfs[2] = { s_lo, s_hi };

                for (int e = 0; e < 2; e++) {
                    double surf = surfs[e];
                    if (drawn < 1.0) glass_cases++;

                    /* The accent. */
                    float out[3];
                    fix(themes[i].accent, out, own, surf);
                    double got  = syn_contrast_lum(lum3(out), surf);
                    double goal = goal_for(themes[i].accent, own, CONTRAST_TARGET);
                    CHECK(got >= goal - 0.01,
                          "%-11s / %-15s a=%.2f: accent %.2f:1 < goal %.2f:1",
                          themes[i].name, backs[b].name, drawn, got, goal);
                    checks++;

                    /* The four status colours, same contract. */
                    for (int k = 0; k < NSTATS; k++) {
                        fix(stats[k].rgb, out, own, surf);
                        got  = syn_contrast_lum(lum3(out), surf);
                        goal = goal_for(stats[k].rgb, own, CONTRAST_TARGET);
                        CHECK(got >= goal - 0.01,
                              "%-11s / %-15s a=%.2f: %s %.2f:1 < goal %.2f:1",
                              themes[i].name, backs[b].name, drawn,
                              stats[k].name, got, goal);
                        checks++;
                    }

                    /* Every TEXT rung of the ink ladder, after the clamp. */
                    double floor = syn_ink_floor_glass(themes[i].bg, themes[i].ink,
                                                       INK_TEXT, surf, INK_TEXT_MIN);
                    float ref[3];
                    ladder_at(ref, themes[i].bg, themes[i].ink, INK_TEXT);
                    double rgoal = goal_for(ref, own, INK_TEXT_MIN);
                    for (int r = 0; r < NRUNGS; r++) {
                        double lv = rungs[r].level < floor ? floor : rungs[r].level;
                        float c[3];
                        ladder_at(c, themes[i].bg, themes[i].ink, lv);
                        got = syn_contrast_lum(lum3(c), surf);
                        CHECK(got >= rgoal - 0.01,
                              "%-11s / %-15s a=%.2f: %s %.2f:1 < goal %.2f:1",
                              themes[i].name, backs[b].name, drawn,
                              rungs[r].name, got, rgoal);
                        checks++;
                    }
                }
            }
        }
    }
    NOTE("%d colour/surface pairs checked, %d of them on glass", checks, glass_cases);

    printf("\n== claim 2: an OPAQUE panel is untouched, bit-for-bit ==\n");
    for (int i = 0; i < NTHEMES; i++) {
        double own = lum3(themes[i].bg);
        /* alpha 1.0 over anything: the composite IS the theme's own surface, so
         * the goal is met by construction and nothing may move. */
        for (int b = 0; b < NBACKS; b++) {
            double surf = syn_lum_over(own, 1.0, backs[b].mean);
            float legacy[3], now[3];
            syn_contrast_fix(themes[i].accent, legacy, surf);
            fix(themes[i].accent, now, own, surf);
            CHECK(same(legacy, now),
                  "%-11s opaque over %-15s: the accent moved", themes[i].name,
                  backs[b].name);
            for (int k = 0; k < NSTATS; k++) {
                syn_contrast_fix(stats[k].rgb, legacy, surf);
                fix(stats[k].rgb, now, own, surf);
                CHECK(same(legacy, now), "%-11s opaque: %s moved",
                      themes[i].name, stats[k].name);
            }
            CHECK(syn_ink_floor_glass(themes[i].bg, themes[i].ink, INK_TEXT,
                                      surf, INK_TEXT_MIN) == 0.0,
                  "%-11s opaque: the ladder was clamped when it need not be",
                  themes[i].name);
        }
    }
    NOTE("all %d themes are byte-identical on an opaque panel", NTHEMES);

    printf("\n== the hue survives the correction ==\n");
    {
        /* The wallpaper's #D66318 on the reported panel. It has to come back a
         * lighter ORANGE — an accent corrected to grey is an accent thrown
         * away, and the whole point of scaling channels together is that it
         * cannot happen. */
        const float wp[3] = { 0.839f, 0.388f, 0.094f };   /* #D66318 */
        const int t = 13;
        double own  = lum3(themes[t].bg);
        double surf = syn_lum_over(own, 0.86, 0.92);
        float out[3];
        fix(wp, out, own, surf);
        CHECK(out[0] > out[1] && out[1] > out[2],
              "the channel order is not orange any more (%.3f %.3f %.3f)",
              out[0], out[1], out[2]);
        CHECK(out[0] - out[2] > 0.15f,
              "the correction desaturated it to near-grey (spread %.3f)",
              out[0] - out[2]);
        NOTE("#D66318 -> %.3f %.3f %.3f, still orange", out[0], out[1], out[2]);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all checks passed");
    return fails ? 1 : 0;
}
