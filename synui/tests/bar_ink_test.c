/*
 * bar_ink_test.c — which ink a bar with no background of its own must use, and
 * the case where the honest answer is "do not draw one".
 *
 * macOS 26's menu bar is clear: its clock and its menus are drawn straight onto
 * the wallpaper. That makes the WALLPAPER the surface, and a surface the theme
 * cannot know — the same #1D1D1F that measures 12.6:1 on Tahoe's own pale
 * desktop measures 1.2:1 on a near-black one. So the ink is chosen from the
 * backdrop instead of shipped with the palette.
 *
 * Three claims, and the third is the one worth a test file:
 *
 *   1. A dark backdrop takes light ink and a pale one takes dark ink, each
 *      clearing CONTRAST_TARGET. That is the easy half.
 *
 *   2. There is a BAND in the middle where neither does. It is real and it is
 *      not narrow enough to ignore: black needs the backdrop above ~0.230 and
 *      white needs it below ~0.183, and an evenly-lit photograph lands between
 *      them. A clear bar cannot tint its way out of that — it has no background
 *      to tint — so the answer is SYN_INK_NONE and the caller keeps its
 *      background. Rounding that band to the nearer side is exactly the bug
 *      this exists to stop: it would ship an unreadable bar and call it Tahoe.
 *
 *   3. Two monitors that need different answers have no shared answer. The
 *      bar's palette is a QML singleton — one value for every screen — so
 *      picking a side means the other screen's clock is the one that vanishes.
 *
 * The boundaries are asserted as PROPERTIES (this luminance clears target with
 * this ink) rather than as the numbers 0.183 and 0.230, so a change to
 * CONTRAST_TARGET moves them instead of breaking the file.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "contrast.h"

static int failures = 0;

static void check(int ok, const char *what)
{
    if (!ok) { printf("  FAIL: %s\n", what); failures++; }
}

/* Contrast of a fixed ink against a backdrop of this luminance — the same
 * arithmetic syn_ink_for_backdrop() runs, spelt out here so the test measures
 * the claim rather than re-calling the function under test. */
static double ratio(double ink_lum, double bg_lum)
{
    double hi = ink_lum > bg_lum ? ink_lum : bg_lum;
    double lo = ink_lum > bg_lum ? bg_lum  : ink_lum;
    return (hi + 0.05) / (lo + 0.05);
}

int main(void)
{
    printf("bar ink over a wallpaper\n");

    /* ── 1. The two ends ──────────────────────────────────────────────── */
    /* velle's own wallpaper, measured off the file: 0.001 across the top strip.
     * This is the case the whole change exists for — the theme's #1D1D1F ink is
     * 1.2:1 here, which is a bar that is drawn and cannot be seen. */
    syn_ink_t black_wall = syn_ink_for_backdrop(0.001, CONTRAST_TARGET);
    check(black_wall == SYN_INK_LIGHT, "near-black wallpaper takes light ink");
    check(ratio(SYN_INK_DARK_LUM, 0.001) < 1.5,
          "…and the theme's own dark ink would have been invisible on it");

    /* Tahoe's own desktop, near enough: pale, and the theme's ink is right. */
    check(syn_ink_for_backdrop(0.85, CONTRAST_TARGET) == SYN_INK_DARK,
          "a pale wallpaper takes dark ink");

    check(syn_ink_for_backdrop(0.0, CONTRAST_TARGET) == SYN_INK_LIGHT, "pure black");
    check(syn_ink_for_backdrop(1.0, CONTRAST_TARGET) == SYN_INK_DARK,  "pure white");

    /* ── 2. Whatever it answers, the answer is LEGIBLE ─────────────────── */
    /* The property that matters, swept across the whole range: an ink is only
     * ever returned when it clears the target, and NONE is only ever returned
     * when neither does. Either half failing is a bar somebody cannot read. */
    int band_seen = 0;
    for (int i = 0; i <= 1000; i++) {
        double lum = i / 1000.0;
        syn_ink_t ink = syn_ink_for_backdrop(lum, CONTRAST_TARGET);
        double c_dark  = ratio(SYN_INK_DARK_LUM,  lum);
        double c_light = ratio(SYN_INK_LIGHT_LUM, lum);

        if (ink == SYN_INK_DARK) {
            check(c_dark >= CONTRAST_TARGET, "returned dark ink that passes");
        } else if (ink == SYN_INK_LIGHT) {
            check(c_light >= CONTRAST_TARGET, "returned light ink that passes");
        } else {
            check(c_dark < CONTRAST_TARGET && c_light < CONTRAST_TARGET,
                  "returned NONE only where neither ink passes");
            band_seen++;
        }
    }

    /* And the band is not hypothetical. If this ever reads 0 the two inks have
     * been changed to a pair that covers the whole range, and the "keep your
     * background" path below has quietly become dead code — which is worth
     * knowing, because the bar still has it. */
    check(band_seen > 0, "the band where neither ink passes exists");
    printf("  (%d of 1001 sampled luminances have no legible ink)\n", band_seen);

    /* A wallpaper that could not be measured — no image, the matrix backend, a
     * video played by wallpaperengine — is not a dark one. */
    check(syn_ink_for_backdrop(-1.0, CONTRAST_TARGET) == SYN_INK_NONE,
          "an unmeasured backdrop has no ink");

    /* ── 3. Two monitors ──────────────────────────────────────────────── */
    check(syn_ink_combine(SYN_INK_DARK, SYN_INK_DARK) == SYN_INK_DARK,
          "two pale wallpapers agree");
    check(syn_ink_combine(SYN_INK_LIGHT, SYN_INK_LIGHT) == SYN_INK_LIGHT,
          "two dark wallpapers agree");
    check(syn_ink_combine(SYN_INK_DARK, SYN_INK_LIGHT) == SYN_INK_NONE,
          "a pale monitor and a dark one have no shared answer");
    check(syn_ink_combine(SYN_INK_LIGHT, SYN_INK_DARK) == SYN_INK_NONE,
          "…in either order");
    /* NONE absorbs, so a monitor showing a video wallpaper vetoes the clear bar
     * for the desktop rather than being skipped and leaving the bar clear over
     * a backdrop nothing measured. */
    check(syn_ink_combine(SYN_INK_LIGHT, SYN_INK_NONE) == SYN_INK_NONE,
          "an unmeasured monitor vetoes");
    check(syn_ink_combine(SYN_INK_NONE, SYN_INK_LIGHT) == SYN_INK_NONE,
          "…in either order");

    /* ── 4. The tokens the bar parses ─────────────────────────────────── */
    /* backdrop.state is read by a regex in Theme.qml, which accepts exactly
     * "dark" and "light" and treats everything else as "no safe answer". These
     * three strings are that interface. */
    check(strcmp(syn_ink_name(SYN_INK_DARK),  "dark")  == 0, "token: dark");
    check(strcmp(syn_ink_name(SYN_INK_LIGHT), "light") == 0, "token: light");
    check(strcmp(syn_ink_name(SYN_INK_NONE),  "none")  == 0, "token: none");

    if (failures) {
        printf("bar_ink_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("bar_ink_test: OK\n");
    return 0;
}
