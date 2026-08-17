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
 *   3. Two monitors that need different answers have no shared answer — and
 *      THE BAR NO LONGER ASKS FOR ONE. syn_ink_combine still vetoes, because a
 *      single surface lying across a dark screen and a pale one really does
 *      have no ink that reads on both; that is what a menu dragged over the
 *      seam needs. But the bar is not one surface. It is a separate layer
 *      surface on every output, over its own strip of its own wallpaper, so it
 *      reads a per-output answer and the veto never reaches it.
 *
 *      Measured on a three-monitor desk: two desktops at 0.67 wanted dark ink
 *      and a television showing the same wallpaper letterboxed — its top row of
 *      cells being the black band — wanted light. The fold said `none`, and
 *      macOS 26 and Prism, the two presets whose whole look is a bar that is
 *      not there, put an opaque strip back on all three screens. One black band
 *      on one television turned the glass off for the desktop.
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

    /*
     * …and the per-output answers the bar actually reads, which the fold above
     * must not be able to reach.
     *
     * This is the letterboxed-television case in arithmetic: two screens whose
     * top strips are a pale 0.67 and a black 0.00. Every one of them has a
     * perfectly good ink of its own; only the FOLD has none. A bar that asked
     * the fold went opaque on all three screens, which is the bug — so what is
     * asserted here is that the three answers are genuinely independent, and
     * that the veto is the odd one out rather than the verdict.
     */
    const double desktop_lum = 0.67;   /* two 1440p desktops */
    const double tv_lum      = 0.00;   /* the letterbox band under the bar   */
    syn_ink_t on_desktop = syn_ink_for_backdrop(desktop_lum, CONTRAST_TARGET);
    syn_ink_t on_tv      = syn_ink_for_backdrop(tv_lum,      CONTRAST_TARGET);

    check(on_desktop == SYN_INK_DARK,  "a pale desktop's own strip takes dark ink");
    check(on_tv      == SYN_INK_LIGHT, "a black letterbox band takes light ink");
    check(syn_ink_combine(on_desktop, on_tv) == SYN_INK_NONE,
          "…and folded together they have no answer — which is why the bar "
          "stopped asking");
    /* The closest ink is per-output too, or the scrim would flip direction on
     * the screen that did not cause the veto. */
    check(syn_ink_best(desktop_lum) == SYN_INK_DARK &&
          syn_ink_best(tv_lum)      == SYN_INK_LIGHT,
          "the closest ink is per-output as well");

    /* ── 4. The tokens the bar parses ─────────────────────────────────── */
    /* backdrop.state is read by a regex in Theme.qml, which accepts exactly
     * "dark" and "light" and treats everything else as "no safe answer". These
     * three strings are that interface. */
    check(strcmp(syn_ink_name(SYN_INK_DARK),  "dark")  == 0, "token: dark");
    check(strcmp(syn_ink_name(SYN_INK_LIGHT), "light") == 0, "token: light");
    check(strcmp(syn_ink_name(SYN_INK_NONE),  "none")  == 0, "token: none");

    /* ── 5. The band, and the scrim that gets out of it ───────────────── */
    /*
     * Claim 2 above says the band is real and that the bar must not go clear in
     * it. What the bar USED to do there was put its whole background back, and
     * from the outside that is "my transparent bar stops being transparent on
     * some wallpapers and starts again if I change it" — because the band is
     * narrow enough that one photograph is inside it and the next is not.
     *
     * So the bar dims the backdrop instead: a wash the opposite way from the
     * ink, at SCRIM_ALPHA, and then draws the ink it was going to draw. This
     * section is what says that actually works — that in the WHOLE band, the
     * scrimmed backdrop clears the target. Without it, "0.34" is a number
     * somebody liked the look of.
     */
    syn_ink_t best_dark  = syn_ink_best(0.85);
    syn_ink_t best_light = syn_ink_best(0.001);
    check(best_dark  == SYN_INK_DARK,  "a pale wallpaper's closest ink is dark");
    check(best_light == SYN_INK_LIGHT, "a dark wallpaper's closest ink is light");
    check(syn_ink_best(-1.0) == SYN_INK_NONE,
          "an unmeasured backdrop still has no ink, closest or otherwise");

    /* Wherever an ink is SAFE, the closest one is that same ink. The scrim must
     * never flip the ink over — it is there to help the answer the bar already
     * had, and a bar that swapped black for white as a wallpaper drifted across
     * the band would flicker in exactly the case this is fixing. */
    for (int i = 0; i <= 1000; i++) {
        double lum = i / 1000.0;
        syn_ink_t safe = syn_ink_for_backdrop(lum, CONTRAST_TARGET);
        if (safe != SYN_INK_NONE)
            check(syn_ink_best(lum) == safe,
                  "the closest ink agrees with the safe one wherever there is one");
    }

    /*
     * Theme.qml's `scrimAlpha`. Spelt here because a C test cannot read a QML
     * property — so if that number is changed, this is the file that says
     * whether the new one still clears, and it has to be changed with it.
     */
    const double SCRIM_ALPHA = 0.34;

    int scrimmed = 0;
    for (int i = 0; i <= 1000; i++) {
        double lum = i / 1000.0;
        if (syn_ink_for_backdrop(lum, CONTRAST_TARGET) != SYN_INK_NONE) continue;

        syn_ink_t ink = syn_ink_best(lum);
        check(ink != SYN_INK_NONE, "the band always has a closest ink");

        /* Compositing happens in sRGB, luminance is linear, so the backdrop has
         * to be taken back to sRGB, washed, and re-measured. An APPROXIMATION —
         * `lum` is the mean luminance of thousands of pixels and the mean of a
         * non-linear function is not that function of the mean — but a wash is
         * monotonic in every pixel, so a strip whose mean clears it is not
         * hiding a region that does not. */
        double v = lum <= 0.0031308 ? 12.92 * lum
                                    : 1.055 * pow(lum, 1.0 / 2.4) - 0.055;
        /* Light ink darkens the backdrop; dark ink lightens it. */
        double wash = (ink == SYN_INK_LIGHT) ? 0.0 : 1.0;
        double out  = v * (1.0 - SCRIM_ALPHA) + wash * SCRIM_ALPHA;
        double lin  = out <= 0.04045 ? out / 12.92
                                     : pow((out + 0.055) / 1.055, 2.4);

        double ink_lum = (ink == SYN_INK_LIGHT) ? SYN_INK_LIGHT_LUM
                                                : SYN_INK_DARK_LUM;
        check(ratio(ink_lum, lin) >= CONTRAST_TARGET,
              "the scrim clears the target everywhere in the band");
        scrimmed++;
    }
    check(scrimmed == band_seen, "every unreadable backdrop gets a scrim");

    if (failures) {
        printf("bar_ink_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("bar_ink_test: OK\n");
    return 0;
}
