/*
 * panel_glass_test.c — how see-through synui's OWN panels are on a glass theme.
 *
 * The control panel, the task manager, the desktop and dock menus, the clock,
 * the calculator — thirty compositor-drawn panels, each a coloured scene rect
 * with a cairo buffer of ink on top. On a glass desktop every one of them used
 * to open as a solid slab in front of windows that were themselves frosted,
 * which is the report this pins: "the glass should reach the system menus too".
 *
 * WHAT IS ACTUALLY BEING CHECKED, and why it is worth a test at all:
 *
 *   1. THE UNSET CASE, which is the bug. glass_level lives in synuirc and is
 *      written there by syn-install on a FRESH install. A machine that reached
 *      Prism through the theme manager has no such line — so a rule that keyed
 *      off the level alone left the house glass theme with solid panels on
 *      exactly the desktops most likely to be running it. Unset means "nobody
 *      chose a level", not "nobody wanted glass".
 *
 *   2. THE LADDER SURVIVES. Each panel passes the alpha it was tuned at and
 *      glass SCALES it, so a dense table stays denser than a six-row menu. A
 *      single alpha for all thirty would have to be readable at the density of
 *      the task manager and see-through at the density of a menu, and there is
 *      no number that is both.
 *
 *   3. NOTHING GETS MORE SOLID, EVER, and nothing falls through the floor. Both
 *      are one-directional claims that a future tweak to the curve could break
 *      silently in one direction while looking fine in the other, so they are
 *      swept over the whole range rather than sampled.
 *
 *   4. OFF IS BETTER THAN HALF. Blur off means no glass rather than translucent
 *      panels with a sharp wallpaper behind them, which is the one outcome
 *      worse than a slab.
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

/* A desktop in the state the argument describes, and nothing else set: these
 * helpers read no file and touch no global, so each case is exactly its four
 * fields. */
static syn_config_t cfg_make(syn_theme_t theme, int transparency, int blur,
                             int glass_level)
{
    syn_config_t c;
    memset(&c, 0, sizeof(c));
    c.theme        = theme;
    c.transparency = transparency;
    c.blur         = blur;
    c.glass_level  = glass_level;
    return c;
}

/* The alphas real panels pass, taken off the call sites in render.c. Named
 * because the ORDER between them is what case 2 is about. */
#define A_MENU   0.88f    /* the sparsest — synui_render_overlay()            */
#define A_MID    0.94f    /* the control panel, the clock, most of them       */
#define A_DENSE  0.985f   /* the task manager's table, the news list          */

int main(void)
{
    printf("panel_glass_test\n");

    /* ── 1. A theme that is not glass is untouched ──────────── */
    {
        syn_config_t c = cfg_make(SYN_THEME_SYNAPSE, 1, 1, SYN_GLASS_UNSET);
        CHECK(near(syn_panel_glass_factor(&c), 1.0f),
              "SYNAPSE with no level should be solid, got %.3f",
              syn_panel_glass_factor(&c));
        CHECK(!syn_glass_active(&c), "SYNAPSE should not be drawing glass");

        float f = syn_panel_glass_factor(&c);
        CHECK(near(syn_glass_apply(f, A_DENSE), A_DENSE), "dense unchanged");
        CHECK(near(syn_glass_apply(f, A_MENU),  A_MENU),  "menu unchanged");
        /* Below the floor and still untouched: the floor is a GLASS rule, and a
         * preset that deliberately tuned a panel to 0.50 keeps it. */
        CHECK(near(syn_glass_apply(f, 0.50f), 0.50f),
              "a 0.50 panel on a solid theme must stay 0.50, not rise to the floor");
    }

    /* ── 2. THE BUG: Prism with no glass_level is still glass ─
     *
     * velle's machine exactly — theme.state says prism, synuirc names no level,
     * because only a fresh install writes one. */
    {
        syn_config_t c = cfg_make(SYN_THEME_PRISM, 1, 1, SYN_GLASS_UNSET);
        float f = syn_panel_glass_factor(&c);

        CHECK(f < 1.0f,
              "PRISM without a glass_level must still be glass — this is the "
              "whole report, and the theme manager never writes that key");
        CHECK(syn_glass_active(&c), "PRISM should be drawing glass");
        /* Pinned to the default level's arithmetic, not merely "less than 1":
         * a fallback that silently became 5 would pass a looser assertion and
         * look identical to no fallback at all. */
        CHECK(near(f, 1.00f - 0.30f * (SYN_GLASS_PANEL_DEFAULT / 100.0f)),
              "the fallback must be SYN_GLASS_PANEL_DEFAULT, got %.3f", f);

        /* And the same for the other glass preset, so this is a property of
         * theme_is_glass() rather than a special case for the house theme. */
        syn_config_t m = cfg_make(SYN_THEME_MACOS26, 1, 1, SYN_GLASS_UNSET);
        CHECK(near(syn_panel_glass_factor(&m), f),
              "macOS 26 must get the same fallback as Prism");
    }

    /* ── 3. The ladder between panels survives ──────────────── */
    {
        syn_config_t c = cfg_make(SYN_THEME_PRISM, 1, 1, 55);
        float f = syn_panel_glass_factor(&c);
        float menu = syn_glass_apply(f, A_MENU);
        float mid  = syn_glass_apply(f, A_MID);
        float dense = syn_glass_apply(f, A_DENSE);

        CHECK(menu < mid && mid < dense,
              "the tuned order must survive glass: %.3f / %.3f / %.3f",
              menu, mid, dense);
        CHECK(dense < A_DENSE, "the densest panel must actually become glass");
    }

    /* ── 4. Monotonic, bounded, floored — swept ──────────────
     *
     * Every level against every panel alpha in use. Swept rather than sampled
     * because both claims are one-directional: a curve that started INCREASING
     * alpha at high levels, or one that undershot the floor only for the
     * sparsest panel, would each look fine at whatever single level a spot
     * check happened to pick. */
    {
        const float bases[] = { A_MENU, A_MID, A_DENSE, 1.0f };
        float prev_for[4];
        for (size_t b = 0; b < 4; b++) prev_for[b] = 2.0f;

        for (int level = 0; level <= 100; level++) {
            syn_config_t c = cfg_make(SYN_THEME_PRISM, 1, 1, level);
            float f = syn_panel_glass_factor(&c);

            for (size_t b = 0; b < 4; b++) {
                float a = syn_glass_apply(f, bases[b]);
                CHECK(a <= bases[b] + 0.0005f,
                      "level %d made base %.3f MORE solid (%.3f)",
                      level, bases[b], a);
                CHECK(a >= SYN_GLASS_PANEL_FLOOR - 0.0005f,
                      "level %d took base %.3f under the floor (%.3f)",
                      level, bases[b], a);
                CHECK(a <= prev_for[b] + 0.0005f,
                      "level %d is less see-through than %d for base %.3f",
                      level, level - 1, bases[b]);
                prev_for[b] = a;
            }
        }

        /* The floor is REACHED, not merely respected — an assertion that only
         * ever tested values above it would pass on a curve that never got
         * anywhere near being glass. */
        syn_config_t full = cfg_make(SYN_THEME_PRISM, 1, 1, 100);
        CHECK(near(syn_glass_apply(syn_panel_glass_factor(&full), A_MENU),
                   SYN_GLASS_PANEL_FLOOR),
              "the sparsest panel at level 100 should sit on the floor");
    }

    /* ── 5. Off is better than half ──────────────────────────── */
    {
        syn_config_t no_trans = cfg_make(SYN_THEME_PRISM, 0, 1, 55);
        CHECK(near(syn_panel_glass_factor(&no_trans), 1.0f),
              "transparency off must mean solid panels");
        CHECK(!syn_glass_active(&no_trans), "…and no blur behind them");

        /* The one that is easy to get wrong, because the alpha and the blur are
         * set in different files: translucent panels with a SHARP wallpaper
         * behind them is worse than a slab, so blur gates both. */
        syn_config_t no_blur = cfg_make(SYN_THEME_PRISM, 1, 0, 55);
        CHECK(near(syn_panel_glass_factor(&no_blur), 1.0f),
              "blur off must mean solid panels, not see-through ones");
        CHECK(!syn_glass_active(&no_blur), "…and glass reported as off");
    }

    /* ── 6. An explicit level answers for ANY theme ───────────
     *
     * The same slider already moves the windows and the bar on every preset
     * (config_apply_glass_level), so a Gruvbox desktop that sets one and finds
     * its windows frosted and its panels solid would be the same half-applied
     * complaint one theme over. */
    {
        syn_config_t c = cfg_make(SYN_THEME_GRUVBOX, 1, 1, 40);
        CHECK(syn_glass_active(&c),
              "an explicit glass_level must work on a theme that is not glass");
        CHECK(near(syn_panel_glass_factor(&c), 1.00f - 0.30f * 0.40f),
              "and use the level it was given, got %.3f",
              syn_panel_glass_factor(&c));

        /* Zero is a real answer and it means SOLID — not "unset", which is what
         * SYN_GLASS_UNSET is for. Distinguishing them is the reason the sentinel
         * is -1 rather than 0. */
        syn_config_t zero = cfg_make(SYN_THEME_PRISM, 1, 1, 0);
        CHECK(near(syn_panel_glass_factor(&zero), 1.0f),
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
