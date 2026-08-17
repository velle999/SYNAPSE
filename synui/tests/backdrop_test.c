/*
 * backdrop_test.c — what a see-through surface is actually drawn ON.
 *
 * The bar has always known: wallpaper.c measures the strip it covers, and
 * backdrop.state names the ink that survives there. Nothing else did. Once the
 * panels and the shell's menus followed the bar down to the same alpha, "which
 * ink is legible on the wallpaper" stopped being one answer per monitor — a
 * menu opens where the pointer is — and the strip grew into a grid.
 *
 * WHAT IS BEING CHECKED, and why each of these is worth pinning:
 *
 *   1. THE MIX IS IN THE RIGHT SPACE. Relative luminance is linear-light and
 *      the GPU blends 8-bit sRGB without linearising, so mixing the two
 *      luminances directly is not the pixel that lands on screen. It is out by
 *      several hundredths in the midtones — the whole width of the band where
 *      the ink flips — so this is the difference between a scrim and no scrim,
 *      not a rounding preference. Pinned against the encode/mix/decode done
 *      the long way round.
 *
 *   2. DISAGREEING CELLS VETO. One surface draws one colour of text. A menu
 *      lying half on sky and half on shadow has no single ink that reads on
 *      both, so the fold must answer NONE rather than picking a side — the same
 *      rule syn_ink_combine() already applies to two monitors, one scale down.
 *
 *   3. UNMEASURED IS NOT DARK. -1 means the compositor cannot see what is back
 *      there (wallpaper-engine paints its own surface over everything). Reading
 *      that as a legitimate black backdrop would pick white ink for a wallpaper
 *      that might be white, which is the failure the sentinel exists to stop —
 *      and it has to survive the fold, not just the single-cell case.
 *
 *   4. A BOX OFF THE EDGE STILL ANSWERS. Panels hang off the screen (the dock
 *      menu at a corner), and the cells they do cover are the honest answer for
 *      the part that is on it. The alternative — refusing — would put the
 *      opaque slab back on exactly the menus that open near an edge, which is
 *      most of them.
 *
 * Pure arithmetic over a grid and two luminances: no compositor, no outputs, no
 * cairo. contrast.c is the only thing linked.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "contrast.h"

static int failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("  FAIL: ");                                               \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

/* A grid where every cell holds the same luminance — the flat wallpaper, and
 * the shape most of these cases want. */
static void grid_flat(double g[SYN_LUM_CELLS], double lum)
{
    for (int i = 0; i < SYN_LUM_CELLS; i++) g[i] = lum;
}

/* Dark on the left half, pale on the right: the wallpaper that has no single
 * answer, and the one case 2 is about. */
static void grid_split(double g[SYN_LUM_CELLS], double left, double right)
{
    for (int r = 0; r < SYN_LUM_ROWS; r++)
        for (int c = 0; c < SYN_LUM_COLS; c++)
            g[r * SYN_LUM_COLS + c] = c < SYN_LUM_COLS / 2 ? left : right;
}

int main(void)
{
    printf("backdrop_test\n");

    /* ── 1. syn_lum_over mixes where the GPU mixes ──────────── */
    {
        /* The ends are exact and need no encoding argument at all. */
        CHECK(near(syn_lum_over(0.8, 1.0, 0.1), 0.8), "alpha 1 is the surface");
        CHECK(near(syn_lum_over(0.8, 0.0, 0.1), 0.1), "alpha 0 is the backdrop");

        /* The middle is the whole point. Done the long way here — out through
         * the transfer function, mixed, and back — so a change to syn_lum_over
         * that silently reverted to a linear mix of luminances fails rather
         * than merely looking different. */
        const double s = 0.05, b = 0.60, a = 0.45;
        double enc_s = 1.055 * pow(s, 1.0 / 2.4) - 0.055;
        double enc_b = 1.055 * pow(b, 1.0 / 2.4) - 0.055;
        double mixed = a * enc_s + (1.0 - a) * enc_b;
        double want  = pow((mixed + 0.055) / 1.055, 2.4);
        CHECK(fabs(syn_lum_over(s, a, b) - want) < 1e-6,
              "the mix must happen in sRGB, got %.6f want %.6f",
              syn_lum_over(s, a, b), want);

        /* And it must NOT be the linear mix, or case 1 proves nothing: if the
         * two agreed to within the band's width there would be no bug to
         * prevent. 0.05/0.60 at 0.45 is a midtone pair chosen to separate. */
        double linear = a * s + (1.0 - a) * b;
        CHECK(fabs(want - linear) > 0.02,
              "the two mixes must actually differ here (%.4f vs %.4f) or this "
              "case is not testing anything", want, linear);

        /* Unmeasured passes the surface through untouched. */
        CHECK(near(syn_lum_over(0.8, 0.5, -1.0), 0.8),
              "an unmeasured backdrop must not move the surface");
    }

    /* ── 2. A flat grid gives the flat answer ────────────────── */
    {
        double g[SYN_LUM_CELLS];
        syn_backdrop_t bd;

        /* Near-black: white ink, comfortably. */
        grid_flat(g, 0.02);
        syn_backdrop_for_box(g, 0.0, 0.0, 1.0, 1.0, 4.5, &bd);
        CHECK(near(bd.lum, 0.02), "flat 0.02 folds to 0.02, got %.4f", bd.lum);
        CHECK(bd.ink == SYN_INK_LIGHT, "a near-black wallpaper takes light ink");
        CHECK(bd.best == SYN_INK_LIGHT, "…and that is also the closer one");

        /* Near-white: black ink. */
        grid_flat(g, 0.90);
        syn_backdrop_for_box(g, 0.0, 0.0, 1.0, 1.0, 4.5, &bd);
        CHECK(bd.ink == SYN_INK_DARK, "a near-white wallpaper takes dark ink");

        /* The band where neither passes: a real answer, and the important one.
         * `ink` refuses; `best` still names the closer side, which is what lets
         * the caller lay a scrim instead of putting an opaque slab back. */
        grid_flat(g, 0.21);
        syn_backdrop_for_box(g, 0.0, 0.0, 1.0, 1.0, 4.5, &bd);
        CHECK(bd.ink == SYN_INK_NONE, "the midtone band must refuse both inks");
        CHECK(bd.best != SYN_INK_NONE,
              "…but still name the closer one, or there is no scrim to lay");
    }

    /* ── 3. Disagreeing cells veto ───────────────────────────── */
    {
        double g[SYN_LUM_CELLS];
        syn_backdrop_t bd;
        grid_split(g, 0.02, 0.90);

        /* A box wholly on the dark half gets the dark half's answer. */
        syn_backdrop_for_box(g, 0.0, 0.0, 0.4, 1.0, 4.5, &bd);
        CHECK(bd.ink == SYN_INK_LIGHT, "the left half alone takes light ink");

        /* …and wholly on the pale half, the other. */
        syn_backdrop_for_box(g, 0.6, 0.0, 0.4, 1.0, 4.5, &bd);
        CHECK(bd.ink == SYN_INK_DARK, "the right half alone takes dark ink");

        /* Straddling both, neither — one surface draws one colour of text. */
        syn_backdrop_for_box(g, 0.3, 0.0, 0.4, 1.0, 4.5, &bd);
        CHECK(bd.ink == SYN_INK_NONE,
              "a box across the seam has no single legible ink");
        CHECK(bd.best == SYN_INK_NONE,
              "…and no single closer one either, so the scrim is vetoed too");
        /* The MEAN is still reported, because the alpha floor can use it even
         * where the ink cannot: raising a panel's opacity works whichever way
         * the disagreement went. */
        CHECK(bd.lum > 0.0, "the mean must still be measured, got %.4f", bd.lum);
    }

    /* ── 4. One unmeasured cell vetoes the box ───────────────── */
    {
        double g[SYN_LUM_CELLS];
        syn_backdrop_t bd;

        grid_flat(g, 0.02);
        g[0] = -1.0;                    /* the top-left cell alone is unknown */

        /* A box that avoids it is unaffected — an unmeasured corner must not
         * cost the whole desktop its glass. */
        syn_backdrop_for_box(g, 0.5, 0.5, 0.4, 0.4, 4.5, &bd);
        CHECK(bd.lum >= 0.0 && bd.ink == SYN_INK_LIGHT,
              "a box clear of the unmeasured cell keeps its answer");

        /* A box that covers it gets nothing, rather than a mean over the cells
         * that happened to be known. */
        syn_backdrop_for_box(g, 0.0, 0.0, 0.5, 0.5, 4.5, &bd);
        CHECK(bd.lum < 0.0, "one unmeasured cell vetoes the box, got %.4f", bd.lum);
        CHECK(bd.ink == SYN_INK_NONE && bd.best == SYN_INK_NONE,
              "…and names no ink at all");

        /* ⚠ NOT THE SAME AS BLACK. This is the whole reason the sentinel is -1
         * and not 0: a wholly unmeasured grid must not read as a dark wallpaper
         * and hand back the light ink that would suit one. */
        double unknown[SYN_LUM_CELLS];
        for (int i = 0; i < SYN_LUM_CELLS; i++) unknown[i] = -1.0;
        syn_backdrop_for_box(unknown, 0.0, 0.0, 1.0, 1.0, 4.5, &bd);
        CHECK(bd.ink != SYN_INK_LIGHT,
              "an unmeasured wallpaper must not be mistaken for a dark one");
    }

    /* ── 5. Boxes off the edge, and boxes of no size ─────────── */
    {
        double g[SYN_LUM_CELLS];
        syn_backdrop_t bd;
        grid_flat(g, 0.02);

        /* Hanging off the left and top, as a menu at a screen corner does. */
        syn_backdrop_for_box(g, -0.2, -0.2, 0.5, 0.5, 4.5, &bd);
        CHECK(near(bd.lum, 0.02),
              "a box off the corner answers for the part that is on screen");

        /* Entirely off the right edge: clamped to the last column rather than
         * refusing, for the same reason. */
        syn_backdrop_for_box(g, 1.5, 0.5, 0.2, 0.2, 4.5, &bd);
        CHECK(bd.lum >= 0.0, "a box past the edge still answers");

        /* Zero area — a panel that has not been laid out yet. It samples the
         * cell it lands in rather than reporting "nothing behind me", which
         * would flash the surface solid on its first frame. */
        syn_backdrop_for_box(g, 0.5, 0.5, 0.0, 0.0, 4.5, &bd);
        CHECK(near(bd.lum, 0.02), "a zero-area box samples the cell it is in");

        /* A negative extent is normalised rather than trusted. */
        syn_backdrop_for_box(g, 0.6, 0.6, -0.3, -0.3, 4.5, &bd);
        CHECK(bd.lum >= 0.0, "a negative extent must not fall through");

        /* No grid at all: the older-synui path, and it must be the sentinel
         * rather than a zero-initialised struct that reads as black. */
        syn_backdrop_for_box(NULL, 0.0, 0.0, 1.0, 1.0, 4.5, &bd);
        CHECK(bd.lum < 0.0 && bd.ink == SYN_INK_NONE,
              "no grid means unmeasured, not dark");
    }

    /* ── 6. The alpha floor converges ────────────────────────
     *
     * render.c walks the alpha up until full-strength ink clears AA on the
     * composite. The walk terminates because the composite is monotonic in
     * alpha, which is the property worth pinning here rather than in a file
     * that would need a compositor to test: at alpha 1 the surface is exactly
     * itself, so a panel whose own colours pass can always get there. */
    {
        const double surf = 0.02, ink = 0.90, back = 0.85;   /* dark panel, pale wall */
        double prev = syn_lum_over(surf, 0.0, back);
        for (int i = 1; i <= 50; i++) {
            double a = i / 50.0;
            double now = syn_lum_over(surf, a, back);
            CHECK(now <= prev + 1e-9,
                  "the composite must move monotonically toward the surface "
                  "(alpha %.2f gave %.4f after %.4f)", a, now, prev);
            prev = now;
        }
        CHECK(near(syn_lum_over(surf, 1.0, back), surf),
              "and reach the surface exactly at alpha 1");
        CHECK(syn_contrast_lum(ink, surf) >= 4.5,
              "the panel's own colours must pass, or the walk has no target");
    }

    if (failures) {
        printf("backdrop_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("backdrop_test: all checks passed\n");
    return 0;
}
