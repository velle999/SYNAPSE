/*
 * panel_glass_test.c — how see-through synui's OWN chrome is, and what it
 * follows.
 *
 * The control panel, the task manager, the desktop and dock menus, the clock,
 * the calculator — thirty compositor-drawn panels, each a coloured scene rect
 * with a cairo buffer of ink on top. On a glass desktop every one of them used
 * to open as a solid slab in front of windows that were themselves frosted;
 * then they became glass in an AMOUNT OF THEIR OWN, which is the second half of
 * the same report — the bar could go to nothing, the panels stopped at 0.66 and
 * the shell's menus never moved off 0.97, so one desktop was see-through in
 * three different quantities.
 *
 * WHAT IS ACTUALLY BEING CHECKED, and why it is worth a test at all:
 *
 *   1. THE UNSET CASE. glass_level lives in synuirc and is written there by
 *      syn-install on a FRESH install. A machine that reached Prism through the
 *      theme manager has no such line — so a rule that keyed off the level
 *      alone left the house glass theme with solid panels on exactly the
 *      desktops most likely to be running it. Unset means "nobody chose a
 *      level", not "nobody wanted glass".
 *
 *   2. THE CHROME FOLLOWS THE BAR. bar_opacity is the desktop's one answer to
 *      "how see-through is the furniture" — config_apply_glass_level() resolves
 *      the slider into it, and the Bar opacity row writes it directly — so it
 *      is what the panels take. This is the half that makes the menus match.
 *
 *   3. THE LADDER SURVIVES where there is no such answer. Each panel passes the
 *      alpha it was tuned at and the fallback SCALES it, so a dense table stays
 *      denser than a six-row menu.
 *
 *   4. NOTHING GETS MORE SOLID, EVER. A one-directional claim that a future
 *      tweak to the curve could break silently in one direction while looking
 *      fine in the other, so it is swept over the whole range rather than
 *      sampled.
 *
 *   5. OFF IS BETTER THAN HALF. Blur off means no glass rather than translucent
 *      panels with a sharp wallpaper behind them, which is the one outcome
 *      worse than a slab.
 *
 * ⚠ THERE IS NO LONGER A FLOOR HERE, and an earlier version of this file
 * asserted one. SYN_GLASS_PANEL_FLOOR was a single 0.62 standing in for "will
 * this text read" — asked once for every panel, every theme and every wallpaper
 * at once, and therefore pinned to the worst case any of them might hit. It is
 * why the panels could not follow the bar down however low the slider went.
 * render.c's panel_alpha_floor() asks the real question against the measured
 * backdrop instead, so the floor moved to where the wallpaper is known and this
 * layer hands back whatever was asked for. backdrop_test.c covers that half.
 *
 * No server, no scene graph and no config file: these are pure functions of a
 * syn_config_t, and the test says so by building one by hand.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "synui.h"

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

static bool near(float a, float b) { return fabsf(a - b) < 0.0005f; }

/*
 * A desktop in the state the argument describes, and nothing else set: these
 * helpers read no file and touch no global, so each case is exactly its fields.
 *
 * ⚠ bar_opacity IS SET TO -1 EXPLICITLY, and it has to be. memset() would leave
 * it 0.0, and 0.0 is now a real and very loud answer — "the furniture has no
 * background at all" — rather than the absence of one. -1 is the sentinel
 * config_set_defaults() uses for exactly this reason; a test that let the zero
 * stand would be measuring a fully transparent desktop in every case and
 * passing, because everything below would agree with itself.
 */
static syn_config_t cfg_make(syn_theme_t theme, int transparency, int blur,
                             int glass_level)
{
    syn_config_t c;
    memset(&c, 0, sizeof(c));
    c.theme        = theme;
    c.transparency = transparency;
    c.blur         = blur;
    c.glass_level  = glass_level;
    c.bar_opacity  = -1.0f;
    return c;
}

/* The alphas real panels pass, taken off the call sites in render.c. Named
 * because the ORDER between them is what case 3 is about. */
#define A_MENU   0.88f    /* the sparsest — synui_render_overlay()            */
#define A_MID    0.94f    /* the control panel, the clock, most of them       */
#define A_DENSE  0.985f   /* the task manager's table, the news list          */

int main(void)
{
    printf("panel_glass_test\n");

    /* ── 1. A theme that is not glass is untouched ──────────── */
    {
        syn_config_t c = cfg_make(SYN_THEME_SYNAPSE, 1, 1, SYN_GLASS_UNSET);
        syn_glass_t  g = syn_glass_resolve(&c);
        CHECK(near(g.factor, 1.0f) && g.alpha < 0.0f,
              "SYNAPSE with no level should be solid, got alpha %.3f factor %.3f",
              g.alpha, g.factor);
        CHECK(!syn_glass_active(&c), "SYNAPSE should not be drawing glass");

        CHECK(near(syn_glass_apply(g, A_DENSE), A_DENSE), "dense unchanged");
        CHECK(near(syn_glass_apply(g, A_MENU),  A_MENU),  "menu unchanged");
        CHECK(near(syn_glass_apply(g, 0.50f), 0.50f),
              "a 0.50 panel on a solid theme must stay 0.50");
    }

    /* ── 2. Prism with no glass_level is still glass ──────────
     *
     * theme.state says prism, synuirc names no level, because only a fresh
     * install writes one. */
    {
        syn_config_t c = cfg_make(SYN_THEME_PRISM, 1, 1, SYN_GLASS_UNSET);
        syn_glass_t  g = syn_glass_resolve(&c);

        CHECK(g.factor < 1.0f,
              "PRISM without a glass_level must still be glass — the theme "
              "manager never writes that key");
        CHECK(syn_glass_active(&c), "PRISM should be drawing glass");
        /* Pinned to the default level's arithmetic, not merely "less than 1":
         * a fallback that silently became 5 would pass a looser assertion and
         * look identical to no fallback at all. */
        CHECK(near(g.factor, 1.00f - 0.30f * (SYN_GLASS_PANEL_DEFAULT / 100.0f)),
              "the fallback must be SYN_GLASS_PANEL_DEFAULT, got %.3f", g.factor);

        /* And the same for the other glass preset, so this is a property of
         * theme_is_glass() rather than a special case for the house theme. */
        syn_config_t m = cfg_make(SYN_THEME_MACOS26, 1, 1, SYN_GLASS_UNSET);
        CHECK(near(syn_glass_resolve(&m).factor, g.factor),
              "macOS 26 must get the same fallback as Prism");
    }

    /* ── 3. THE MATCH: the chrome takes the bar's number ──────
     *
     * The report this half exists for. A desktop whose bar is at 0.45 had
     * panels it could not push below 0.66 and menus stuck at 0.97, and no
     * setting anywhere would bring the three together. */
    {
        syn_config_t c = cfg_make(SYN_THEME_PRISM, 1, 1, SYN_GLASS_UNSET);
        c.bar_opacity = 0.45f;
        syn_glass_t g = syn_glass_resolve(&c);

        CHECK(near(g.alpha, 0.45f),
              "the chrome must take the bar's own number, got %.3f", g.alpha);
        CHECK(syn_glass_active(&c), "…and that is glass");

        /* EVERY panel lands on it, whatever it was tuned at. That is what
         * "match" means here and it is the deliberate loss of the ladder:
         * a menu and the bar at two different alphas is the complaint. */
        CHECK(near(syn_glass_apply(g, A_MENU),  0.45f), "sparsest panel matches");
        CHECK(near(syn_glass_apply(g, A_MID),   0.45f), "typical panel matches");
        CHECK(near(syn_glass_apply(g, A_DENSE), 0.45f), "densest panel matches");
        CHECK(near(syn_glass_apply(g, 1.0f),    0.45f), "and an opaque one too");

        /* It beats the theme's own fallback rather than being averaged with
         * it: an explicit answer is an answer. */
        syn_config_t retro = cfg_make(SYN_THEME_GRUVBOX, 1, 1, SYN_GLASS_UNSET);
        retro.bar_opacity = 0.30f;
        CHECK(near(syn_glass_resolve(&retro).alpha, 0.30f),
              "a theme that is not glass still follows an explicit bar opacity");
    }

    /* ── 4. An opaque bar is not glass ────────────────────────
     *
     * bar_opacity = 1 has answered the question, and the answer was "not at
     * all". Frosting behind a surface nothing shows through is invisible work,
     * and reporting it as glass would have the shell draw see-through menus. */
    {
        syn_config_t c = cfg_make(SYN_THEME_PRISM, 1, 1, SYN_GLASS_UNSET);
        c.bar_opacity = 1.0f;
        CHECK(!syn_glass_active(&c), "bar_opacity 1.0 must not be glass");
        CHECK(near(syn_glass_apply(syn_glass_resolve(&c), A_MID), 1.0f),
              "…and every panel draws solid");
    }

    /* ── 5. The ladder between panels survives the fallback ─── */
    {
        syn_config_t c = cfg_make(SYN_THEME_PRISM, 1, 1, SYN_GLASS_UNSET);
        c.bar_opacity = -1.0f;          /* no desktop-wide answer: use the ladder */
        syn_glass_t g = syn_glass_resolve(&c);
        float menu  = syn_glass_apply(g, A_MENU);
        float mid   = syn_glass_apply(g, A_MID);
        float dense = syn_glass_apply(g, A_DENSE);

        CHECK(menu < mid && mid < dense,
              "the tuned order must survive glass: %.3f / %.3f / %.3f",
              menu, mid, dense);
        CHECK(dense < A_DENSE, "the densest panel must actually become glass");
    }

    /* ── 6. THE SLIDER IS READ BEFORE bar_opacity ─────────────
     *
     * ⚠ THE REGRESSION THIS PINS IS SILENT AND POINTS THE WRONG WAY.
     * config_apply_glass_level() writes the level into bar_opacity — that is
     * how one slider moves the bar — so a resolver that read bar_opacity first
     * would be reading the slider's own output back as an independent choice.
     * The level's bottom rung is the case that bites: OFF resolves to
     * syn_glass_bar_alpha(0) = 0.95, because 0.95 is what a NORMAL bar draws
     * at. Read naively, the one setting whose entire meaning is "no glass"
     * turns the chrome 0.95-translucent and switches the blur ON.
     *
     * Simulated exactly as config.c leaves it, rather than by calling the
     * resolver on a half-built config: the bug only exists once both fields
     * are populated, so a case that set the level and left bar_opacity at -1
     * would pass against the broken order too. */
    {
        syn_config_t off = cfg_make(SYN_THEME_PRISM, 1, 1, 0);
        off.bar_opacity = syn_glass_bar_alpha(&off);      /* = 0.95, as config.c writes */
        CHECK(near(off.bar_opacity, 0.95f),
              "the premise: level 0 leaves bar_opacity at 0.95, got %.3f",
              off.bar_opacity);
        CHECK(!syn_glass_active(&off),
              "glass_level = 0 is OFF, and must stay off once config.c has "
              "resolved it into bar_opacity");
        CHECK(near(syn_glass_apply(syn_glass_resolve(&off), A_MID), A_MID),
              "…leaving every panel at the alpha it was tuned at");

        /* And a level that is NOT off still lands on the bar's own number, so
         * the slider and the row are the same setting arriving two ways. */
        syn_config_t half = cfg_make(SYN_THEME_PRISM, 1, 1, 50);
        half.bar_opacity = syn_glass_bar_alpha(&half);
        CHECK(near(syn_glass_resolve(&half).alpha, syn_glass_bar_alpha(&half)),
              "a set level must give the chrome the bar's alpha for that level");
    }

    /* ── 7. Monotonic and bounded — swept ────────────────────
     *
     * Every level against every panel alpha in use, resolved the way config.c
     * leaves it. Swept rather than sampled because the claim is one-directional:
     * a curve that started INCREASING alpha at high levels would look fine at
     * whatever single level a spot check happened to pick. No floor assertion —
     * see the header. */
    {
        const float bases[] = { A_MENU, A_MID, A_DENSE, 1.0f };
        float prev_for[4];
        for (size_t b = 0; b < 4; b++) prev_for[b] = 2.0f;

        for (int level = 1; level <= 100; level++) {
            syn_config_t c = cfg_make(SYN_THEME_PRISM, 1, 1, level);
            c.bar_opacity  = syn_glass_bar_alpha(&c);
            syn_glass_t  g = syn_glass_resolve(&c);

            for (size_t b = 0; b < 4; b++) {
                float a = syn_glass_apply(g, bases[b]);
                CHECK(a >= 0.0f && a <= 1.0f,
                      "level %d took base %.3f out of range (%.3f)",
                      level, bases[b], a);
                CHECK(a <= prev_for[b] + 0.0005f,
                      "level %d is less see-through than %d for base %.3f",
                      level, level - 1, bases[b]);
                /* ⚠ EVERY BASE LANDS ON THE SAME NUMBER, and the sweep asserts
                 * that rather than the "never more solid than its tuned alpha"
                 * this file used to check.
                 *
                 * That older invariant belonged to the FACTOR, which could only
                 * ever multiply a panel's own alpha downward. An absolute alpha
                 * has no such guarantee and must not pretend to: at level 5 the
                 * bar draws at 0.90, and the sparsest panel — the overlay, tuned
                 * at 0.88 — becomes 0.90 with it. It got MORE solid, and that is
                 * correct. "Match the bar" is the requirement; a panel that
                 * stayed more see-through than the bar because it happened to be
                 * tuned that way is the split this change exists to close. */
                CHECK(near(a, syn_glass_apply(g, bases[0])),
                      "level %d: base %.3f landed on %.3f but base %.3f on %.3f "
                      "— every surface must take the same number",
                      level, bases[b], a, bases[0], syn_glass_apply(g, bases[0]));
                prev_for[b] = a;
            }
        }

        /* The top of the range reaches the bar's own bottom — which is the
         * point of the whole change. Before it, the panels stopped at 0.62
         * while the bar went to nothing. */
        syn_config_t full = cfg_make(SYN_THEME_PRISM, 1, 1, 100);
        full.bar_opacity  = syn_glass_bar_alpha(&full);
        CHECK(near(syn_glass_apply(syn_glass_resolve(&full), A_DENSE), 0.0f),
              "at level 100 the chrome must reach the bar's own 0.00");
    }

    /* ── 8. Off is better than half ──────────────────────────── */
    {
        syn_config_t no_trans = cfg_make(SYN_THEME_PRISM, 0, 1, 55);
        CHECK(near(syn_glass_resolve(&no_trans).factor, 1.0f),
              "transparency off must mean solid panels");
        CHECK(!syn_glass_active(&no_trans), "…and no blur behind them");

        /* The one that is easy to get wrong, because the alpha and the blur are
         * set in different files: translucent panels with a SHARP wallpaper
         * behind them is worse than a slab, so blur gates both. */
        syn_config_t no_blur = cfg_make(SYN_THEME_PRISM, 1, 0, 55);
        CHECK(near(syn_glass_resolve(&no_blur).factor, 1.0f),
              "blur off must mean solid panels, not see-through ones");
        CHECK(!syn_glass_active(&no_blur), "…and glass reported as off");

        /* ⚠ AND THEY BEAT THE BAR'S NUMBER. transparency and blur are the
         * user's master switches; a desktop with a bar_opacity set and blur
         * switched off must go solid, not to that number with nothing frosting
         * behind it. This is the case the bar-follows path could most easily
         * have let through, because the number is right there. */
        syn_config_t bar_no_blur = cfg_make(SYN_THEME_PRISM, 1, 0, SYN_GLASS_UNSET);
        bar_no_blur.bar_opacity = 0.20f;
        CHECK(!syn_glass_active(&bar_no_blur),
              "blur off must beat an explicit bar opacity");
        CHECK(near(syn_glass_apply(syn_glass_resolve(&bar_no_blur), A_MID), A_MID),
              "…and leave the panel at its tuned alpha");
    }

    /* ── 9. An explicit level answers for ANY theme ───────────
     *
     * The same slider already moves the windows on every preset
     * (config_apply_glass_level), so a Gruvbox desktop that sets one and finds
     * its windows frosted and its panels solid would be the same half-applied
     * complaint one theme over. */
    {
        syn_config_t c = cfg_make(SYN_THEME_GRUVBOX, 1, 1, 40);
        c.bar_opacity  = syn_glass_bar_alpha(&c);
        CHECK(syn_glass_active(&c),
              "an explicit glass_level must work on a theme that is not glass");
        CHECK(near(syn_glass_resolve(&c).alpha, syn_glass_bar_alpha(&c)),
              "and use the level it was given, got %.3f want %.3f",
              syn_glass_resolve(&c).alpha, syn_glass_bar_alpha(&c));

        /* Zero is a real answer and it means SOLID — not "unset", which is what
         * SYN_GLASS_UNSET is for. Distinguishing them is the reason the sentinel
         * is -1 rather than 0, and the reason the control panel now draws the
         * two rungs as "Off" and "Auto" rather than as "0 %" and "-1 %". */
        syn_config_t zero = cfg_make(SYN_THEME_PRISM, 1, 1, 0);
        CHECK(near(syn_glass_resolve(&zero).factor, 1.0f),
              "glass_level = 0 must be solid even on a glass theme");
        CHECK(!syn_glass_active(&zero), "…and report glass as off");
    }

    if (failures) {
        printf("panel_glass_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("panel_glass_test: all checks passed\n");
    return 0;
}
