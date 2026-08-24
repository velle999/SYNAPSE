/*
 * glass_sync_test.c — one slider for the whole desktop's glass, and the rows
 * that are allowed to walk away from it.
 *
 * The Glass row has always written some of the per-surface alphas and never
 * been able to be overruled on any of them. Both halves were wrong, in opposite
 * directions, and they are what this file pins:
 *
 *   1. IT MOVES EVERYTHING. config_apply_glass_level() wrote three fields — the
 *      two window opacities and the bar — and left the dock and the terminal
 *      behind. So a desktop dialled in from one control had two surfaces at a
 *      different amount of glass from the other three, which is precisely the
 *      "glass in three different amounts" the one-slider design was for.
 *
 *   2. …UNTIL YOU TAKE HOLD OF ONE. Dragging a driven row pins it, and a pinned
 *      row is left exactly where it was put however far the slider then travels.
 *      Switching the sync back on releases every pin at once.
 *
 *   3. AND THE SLIDER CAN LET GO. Auto used to be a one-way door: it stopped
 *      writing the five alphas and left the last values it wrote sitting in the
 *      config with nothing recording that anybody chose them. The screen kept
 *      them for the session; the next login, rebuilt from settings.state, did
 *      not have them at all. Screen and file disagreed and the file won later.
 *
 *   4. AND THE PINS SURVIVE A LOGIN. A pin held only in memory is a row that
 *      quietly rejoins the slider some days after it was taken off it, which is
 *      the same class of bug as 3 and harder to see.
 *
 * No compositor and no panel: this is config.c's resolution against the pin set,
 * which is where the decision actually lives (syn_glass_drives). What the
 * control panel does with it — that dragging a row is what sets its pin — is
 * ctlpanel_table_test's, which drives the real key handler.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synui.h"

/* ── config.c's neighbours, stubbed ──────────────────────────
 *
 * synui_config_load() reads a dozen .state files on its way past, each through
 * its own module. None of them is under test here and every one of them would
 * make the run non-hermetic by reading the developer's own desktop, so they are
 * no-ops — exactly as settings_test.c and state_reload_test.c stub them. What
 * this file drives is synui_config_apply_glass_sync() against a config built
 * from the compiled defaults, which touches none of them.
 */
void wallpaper_state_load(syn_config_t *c)    { (void)c; }
void cursor_state_load(syn_config_t *c)       { (void)c; }
void dock_state_load(syn_config_t *c)         { (void)c; }
void power_state_load(syn_config_t *c)        { (void)c; }
void saver_state_load(syn_config_t *c)        { (void)c; }
void welcome_state_load(syn_config_t *c)      { (void)c; }
void launcher_state_load(syn_config_t *c)     { (void)c; }
void record_state_load(syn_config_t *c)       { (void)c; }
void deskicons_state_load(syn_config_t *c)    { (void)c; }
void binds_state_load(syn_config_t *c)        { (void)c; }
void theme_state_load_config(syn_config_t *c) { (void)c; }
void notif_dnd_state_load_config(syn_config_t *c) { (void)c; }
void filters_state_load_config(syn_config_t *c)   { (void)c; }
void uifx_state_load_config(syn_config_t *c)      { (void)c; }
void theme_load_colors(syn_config_t *c, syn_theme_t t) { c->theme = t; }
void wallpaper_output_apply(syn_config_t *c, const char *name,
                            const char *tok, int mode)
{ (void)c; (void)name; (void)tok; (void)mode; }
void syn_text_set_ui_font(const char *f)      { (void)f; }

int  lid_action_from_name(const char *n)      { (void)n; return 0; }
int  display_mode_from_name(const char *n)    { (void)n; return -1; }
int  wallpaper_mode_from_name(const char *n)  { (void)n; return 0; }
int  saver_mode_from_name(const char *n)      { (void)n; return 0; }
int  lock_bg_from_name(const char *n)         { (void)n; return 0; }
bool syn_arrange_parse(const char *s, syn_arrange_t *out)
{ (void)s; (void)out; return false; }

const char *const syn_theme_names[SYN_THEME_COUNT] = {
    "synapse", "dark", "winxp", "win95",
};
const char *const syn_wallpaper_mode_names[SYN_WALLPAPER_MODE_COUNT] = {
    "fill", "fit", "stretch", "center", "tile",
};
const char *const cat_breed_names[] = { "neon" };

static int fails;

static void check(int ok, const char *what)
{
    if (ok) { printf("  ok    %s\n", what); return; }
    printf("  FAIL  %s\n", what);
    fails++;
}

static int near(float a, float b)
{
    return fabsf(a - b) < 0.0005f;
}

/* A config at the shipped defaults, with the level set — the state a desktop is
 * in the moment somebody moves the Glass row off Auto. */
static void at_level(syn_config_t *cfg, int level, int pins)
{
    memcpy(cfg, synui_config_defaults(), sizeof(*cfg));
    cfg->glass_level = level;
    cfg->glass_pins  = pins;
    synui_config_apply_glass_sync(cfg);
}

int main(void)
{
    printf("glass sync\n");

    const syn_config_t *def = synui_config_defaults();

    /* ── 0. The defaults are the ones the design assumes ──────────────── */
    /*
     * The sync being ON out of the box is what makes the Glass row a master
     * rather than a fourth opinion, and it costs nothing on a desktop that has
     * never set a level — every assertion below turns on glass_level, and unset
     * is a no-op. A default of off would be a master control nobody finds.
     */
    check(def->glass_sync == 1,       "the sync ships on");
    check(def->glass_pins == 0,       "…with nothing pinned");
    check(def->glass_legibility == 1, "and the legibility correction ships on");
    check(def->glass_level == SYN_GLASS_UNSET,
          "…while the level itself ships unset, so none of this fires "
          "on a desktop that has not asked");

    /* ── 1. Unset is still a no-op ────────────────────────────────────── */
    /*
     * The twelve retro presets have hand-tuned opacities and no level. If the
     * sync touched them, adding it would have re-skinned every one of those
     * desktops on the way past.
     */
    {
        syn_config_t cfg;
        memcpy(&cfg, def, sizeof(cfg));
        cfg.dock_opacity = 0.33f;      /* a synuirc line, by hand */
        synui_config_apply_glass_sync(&cfg);
        check(near(cfg.dock_opacity, 0.33f),
              "no level: a hand-written value is left alone");
        check(near(cfg.active_opacity, def->active_opacity) &&
              near(cfg.bar_opacity,    def->bar_opacity),
              "…and so is everything else");
    }

    /* ── 2. A level moves ALL FIVE ────────────────────────────────────── */
    {
        syn_config_t cfg;
        at_level(&cfg, 55, 0);

        check(near(cfg.active_opacity,   syn_glass_window_alpha(&cfg)),
              "level 55 moves the focused window");
        check(cfg.inactive_opacity < cfg.active_opacity,
              "…and the unfocused one trails it");
        check(near(cfg.foot_alpha,  syn_glass_foot_alpha(&cfg)),
              "…and the terminal, which used to be left behind");
        check(near(cfg.bar_opacity, syn_glass_bar_alpha(&cfg)),
              "…and the bar");
        check(near(cfg.dock_opacity, syn_glass_dock_alpha(&cfg)),
              "…and the dock, which used to be left behind too");

        /* The bar and the dock are the same KIND of surface — a strip of chrome
         * floating on the wallpaper with opaque glyphs over it — and a desktop
         * whose top strip and bottom strip are see-through by different amounts
         * is the thing one slider is for. */
        check(near(cfg.bar_opacity, cfg.dock_opacity),
              "…and those two land on the SAME number");

        /* Every one of them actually moved off its default, or the assertions
         * above would pass on a sync that does nothing. */
        check(!near(cfg.dock_opacity, def->dock_opacity),
              "the dock really left its default");
        check(!near(cfg.foot_alpha, def->foot_alpha),
              "…and so did the terminal");
    }

    /* ── 3. More glass means more see-through, monotonically ──────────── */
    /*
     * Each curve is its own line and they are not proportional to each other,
     * which is the whole reason syn_glass_* is five functions. What they must
     * all share is DIRECTION: a row that went the other way at some level would
     * be a slider that made the desktop solider halfway along.
     */
    {
        float prev_win = 2.0f, prev_bar = 2.0f, prev_foot = 2.0f;
        int monotonic = 1;
        for (int lvl = 0; lvl <= 100; lvl += 5) {
            syn_config_t cfg;
            at_level(&cfg, lvl, 0);
            if (cfg.active_opacity > prev_win + 0.0005f ||
                cfg.bar_opacity    > prev_bar + 0.0005f ||
                cfg.foot_alpha     > prev_foot + 0.0005f)
                monotonic = 0;
            prev_win  = cfg.active_opacity;
            prev_bar  = cfg.bar_opacity;
            prev_foot = cfg.foot_alpha;
        }
        check(monotonic, "every curve is monotonic across 0..100");
    }

    /* ── 4. The bar reaches the frost floor; the window does not ──────── */
    /*
     * With the correction ON these two ends are deliberately different, and the
     * difference is the design: the bar goes far further than a window, because
     * a window you can see straight through is one whose edges you cannot find.
     *
     * ⚠ FAR FURTHER IS NOT ALL THE WAY, AND THAT IS THE POINT OF THIS CHECK.
     * The slider used to bottom the bar out at 0.00, on the argument that the
     * bar has no content of its own to lose — its modules draw onto the
     * wallpaper and backdrop.state says which ink survives there. What that
     * missed is that at 0.00 there is no SURFACE, and three separate mechanisms
     * need one: the backdrop blur masks to what the client painted (so it frosts
     * the glyphs instead of the strip), the legibility walk has no alpha to walk
     * from, and the dock takes this number exactly — a dock body at 0.00 frosts
     * its ICONS. See SYN_BAR_ALPHA_FROSTED.
     *
     * Asserted against the constant rather than the literal 0.05, so moving the
     * floor moves the test with it — but asserted STRICTLY ABOVE ZERO as well,
     * because "the surface still exists" is the promise, and a floor quietly
     * edited back to 0.0f would satisfy the first check and not the second.
     */
    {
        syn_config_t cfg;
        at_level(&cfg, 100, 0);
        check(near(cfg.bar_opacity, SYN_BAR_ALPHA_FROSTED),
              "at 100 the bar is as thin as it goes");
        check(cfg.bar_opacity > 0.0f,
              "…and it is still a surface, which is what the blur masks to");
        check(near(cfg.dock_opacity, SYN_BAR_ALPHA_FROSTED),
              "…and the dock is there with it, as it is at every other level");
        check(cfg.active_opacity > 0.5f,
              "…and a window still has findable edges");
    }

    /* ── 5. …unless the correction is off, and then clear means clear ── */
    /*
     * The floor is measured rather than guessed, and it is still the default.
     * Being measured is not a reason for it to be unreachable — "I said clear, I
     * meant clear" is a legitimate thing to want, and a guard that cannot be
     * switched off is a guard that argues with its user in silence.
     */
    {
        syn_config_t cfg;
        memcpy(&cfg, def, sizeof(cfg));
        cfg.glass_legibility = 0;
        cfg.glass_level      = 100;
        synui_config_apply_glass_sync(&cfg);

        check(near(cfg.active_opacity, 0.0f),
              "correction off, level 100: the window goes fully clear");
        check(near(cfg.foot_alpha, 0.0f),
              "…and so does the terminal's background");
        /* ⚠ AND THE BAR DOES NOT, WHICH IS THE ONE ASYMMETRY HERE.
         * glass_legibility opens the window and terminal curves all the way to
         * nothing because their whole floor is a legibility floor. The bar's is
         * not: SYN_BAR_ALPHA_FROSTED is there so the surface EXISTS — for the
         * blur to mask to and the dock to be drawn as — and none of that is a
         * correction anybody switched off. A desktop that wants no bar
         * background asks for one, through the row or Make it all clear. */
        check(near(cfg.bar_opacity, SYN_BAR_ALPHA_FROSTED),
              "…but not the bar: its floor is a surface, not a correction");
    }

    /* ── 6. A pin is not moved, and its neighbours still are ──────────── */
    {
        syn_config_t cfg;
        memcpy(&cfg, def, sizeof(cfg));
        cfg.glass_level  = 55;
        cfg.glass_pins   = SYN_GLASS_PIN_DOCK;
        cfg.dock_opacity = 0.20f;              /* what the user dragged it to */
        synui_config_apply_glass_sync(&cfg);

        check(near(cfg.dock_opacity, 0.20f),
              "a pinned dock keeps the number it was dragged to");
        check(near(cfg.bar_opacity, syn_glass_bar_alpha(&cfg)),
              "…while the bar beside it still follows the slider");
        check(near(cfg.active_opacity, syn_glass_window_alpha(&cfg)),
              "…and so do the windows");

        /* And the slider can then travel without dragging the pin with it. */
        cfg.glass_level = 90;
        synui_config_apply_glass_sync(&cfg);
        check(near(cfg.dock_opacity, 0.20f),
              "…and it stays put however far the slider goes");
    }

    /* ── 7. Sync off means nobody follows ─────────────────────────────── */
    {
        syn_config_t cfg;
        memcpy(&cfg, def, sizeof(cfg));
        cfg.glass_sync  = 0;
        cfg.glass_level = 55;
        synui_config_apply_glass_sync(&cfg);

        check(near(cfg.dock_opacity, def->dock_opacity) &&
              near(cfg.bar_opacity,  def->bar_opacity)  &&
              near(cfg.active_opacity, def->active_opacity),
              "sync off: a level moves nothing");
    }

    /* ── 8. Releasing hands the unpinned rows back ────────────────────── */
    /*
     * Auto was a one-way door. This is the assertion that it is not: what the
     * release restores is the COMPILED default, which is exactly right and only
     * because a driven row is by definition one the user has not set — the
     * moment they set it, it is pinned, and a pinned row keeps what
     * settings.state holds for it.
     */
    {
        syn_config_t cfg;
        at_level(&cfg, 55, SYN_GLASS_PIN_DOCK);
        cfg.dock_opacity = 0.20f;

        synui_config_glass_release(&cfg);

        check(near(cfg.active_opacity, def->active_opacity),
              "release: the window goes back to its default");
        check(near(cfg.inactive_opacity, def->inactive_opacity),
              "…and the unfocused one");
        check(near(cfg.foot_alpha, def->foot_alpha),
              "…and the terminal");
        check(near(cfg.bar_opacity, def->bar_opacity),
              "…and the bar");
        check(near(cfg.dock_opacity, 0.20f),
              "…and the PINNED dock keeps the value it was given, because "
              "nothing else is recording it");
    }

    /* ── 9. syn_glass_drives is the one question, and 0 is not a pin ──── */
    /*
     * Every caller asks this rather than testing the two halves itself. The
     * zero case is not hypothetical: syn_glass_pin_by_name() answers 0 for every
     * row that is not one of the five, `!(pins & 0)` is true, and without the
     * guard the control panel marked all hundred-odd rows "synced".
     */
    {
        syn_config_t cfg;
        at_level(&cfg, 55, SYN_GLASS_PIN_BAR);

        check(syn_glass_drives(&cfg, SYN_GLASS_PIN_DOCK),
              "drives: an unpinned row on a synced desktop");
        check(!syn_glass_drives(&cfg, SYN_GLASS_PIN_BAR),
              "…not a pinned one");
        check(!syn_glass_drives(&cfg, (syn_glass_pin_t)0),
              "…and not a row that has no pin at all");

        cfg.glass_sync = 0;
        check(!syn_glass_drives(&cfg, SYN_GLASS_PIN_DOCK),
              "…nor anything, with the sync off");

        cfg.glass_sync  = 1;
        cfg.glass_level = SYN_GLASS_UNSET;
        check(!syn_glass_drives(&cfg, SYN_GLASS_PIN_DOCK),
              "…nor with no level to follow");
    }

    /* ── 10. The pin set survives its own spelling ────────────────────── */
    /*
     * Pins are persisted by NAME, not as a number, so that the file stays
     * readable and adding a sixth pin cannot renumber the five already written
     * into settings.state on every desktop out there. That only holds if the
     * two directions agree, which is what this walks.
     */
    {
        char buf[256];

        syn_glass_pins_format(0, buf, sizeof(buf));
        check(buf[0] == '\0',
              "no pins formats to nothing, so the key is dropped rather than "
              "written empty");

        int all = 0;
        for (int bit = 1; bit <= SYN_GLASS_PIN_DOCK; bit <<= 1) {
            if (!(SYN_GLASS_PIN_ALL & bit)) continue;
            syn_glass_pins_format(bit, buf, sizeof(buf));
            if (buf[0] == '\0' || syn_glass_pin_by_name(buf) != bit) {
                printf("  FAIL  pin 0x%x formats to '%s' and does not read "
                       "back\n", bit, buf);
                fails++;
            }
            all |= bit;
        }
        check(all == SYN_GLASS_PIN_ALL,
              "every pin in SYN_GLASS_PIN_ALL round trips through its name");

        /* An unknown name is IGNORED rather than refused: a settings.state
         * written by a later synui that pins a row this one does not have is a
         * file to read what you can out of. */
        check(syn_glass_pin_by_name("no_such_row") == 0 &&
              syn_glass_pin_by_name(NULL) == 0,
              "an unknown name is 0, and so is no name at all");

        /* And the whole set, as one line, in the shape settings.state carries. */
        syn_glass_pins_format(SYN_GLASS_PIN_BAR | SYN_GLASS_PIN_DOCK,
                              buf, sizeof(buf));
        check(strstr(buf, "bar_opacity") && strstr(buf, "dock_opacity"),
              "two pins are one space-separated line");
    }

    /* ── 11. The names ARE the rows' synuirc keys ─────────────────────── */
    /*
     * Not decoration: it is what lets a pin be looked up straight off the
     * ctl_item table with no second mapping, and it is why the file can be read
     * without this table in front of you. A rename on one side and not the other
     * is a pin that silently stops being set.
     */
    check(syn_glass_pin_by_name("dock_opacity")     == SYN_GLASS_PIN_DOCK &&
          syn_glass_pin_by_name("bar_opacity")      == SYN_GLASS_PIN_BAR &&
          syn_glass_pin_by_name("foot_alpha")       == SYN_GLASS_PIN_FOOT &&
          syn_glass_pin_by_name("active_opacity")   == SYN_GLASS_PIN_ACTIVE &&
          syn_glass_pin_by_name("inactive_opacity") == SYN_GLASS_PIN_INACTIVE,
          "each pin is named for the synuirc key it pins");

    /* ── 12. The 62% that came back every login ───────────────────────── */
    /*
     * Reported 2026-08-18: the window opacity went back to 62% at every login,
     * overriding what had been set, and 62% looked like nobody's default.
     * It is not a default and never was: 0.62 is where
     * syn_glass_window_alpha() lands at the TOP of the slider —
     * 1.00 - 0.38 * 1.00 — and this desktop had `glass_level = 100`.
     *
     * The route in was that transparency_set_opacity() — the funnel for BOTH
     * Super+E's Window opacity row and the theme manager's -/= keys — edited
     * this driven row without claiming it. So the value went to theme.state,
     * synui_config_load() read theme.state, and one line later the sync
     * overwrote it. The pin is the whole fix, so the number is nailed down here
     * in both directions.
     */
    {
        syn_config_t cfg;

        at_level(&cfg, 100, 0);
        check(near(cfg.active_opacity, 0.62f),
              "level 100, unpinned: the window collapses to exactly 0.62 — "
              "the curve's floor, not anybody's default");

        /* The same desktop with the row claimed, which is what the slider now
         * does. 0.86 is a value only a person would pick. */
        memcpy(&cfg, def, sizeof(cfg));
        cfg.glass_level    = 100;
        cfg.glass_pins     = SYN_GLASS_PIN_ACTIVE;
        cfg.active_opacity = 0.86f;
        synui_config_apply_glass_sync(&cfg);
        check(near(cfg.active_opacity, 0.86f),
              "…and pinned, it survives the sync the login runs");

        /* The unfocused window is deliberately NOT pinned with it: it trails
         * whatever the focused one ended up at, so a pinned 0.86 must pull it
         * to 0.80 rather than leaving it on the slider's own 0.56. */
        check(near(cfg.inactive_opacity, 0.80f),
              "…and the unfocused window trails the PINNED value, not the "
              "slider's");

        /* The default level is unset, so a desktop that never touched the Glass
         * row cannot be reset to 0.62 by any of this. */
        check(def->glass_level == SYN_GLASS_UNSET &&
              !near(def->active_opacity, 0.62f),
              "and 0.62 is reachable only by setting the slider to 100");
    }

    /* ── 9. What a theme asks for, and what the blur gate makes of it ─── */
    /*
     * The other half of "take the transparency out of the Prism defaults". The
     * slider's floor above is what a desktop that MOVED the slider gets; this is
     * what one that never touched it gets, and the two have to agree or a fresh
     * install looks nothing like the same install after one nudge.
     *
     * ⚠ macOS 26 IS THE EXCEPTION AND STAYS ONE. Tahoe's menu bar has no
     * background in the operating system it is named after; a frosted strip
     * across the top would be a different desktop wearing the name. The two
     * Prisms were never that — they are built on the compositor's own glass, and
     * glass is a surface.
     */
    {
        syn_config_t cfg;
        memcpy(&cfg, def, sizeof(cfg));

        cfg.theme = SYN_THEME_PRISM;
        check(near(theme_bar_alpha(&cfg), SYN_BAR_ALPHA_FROSTED),
              "Prism asks for the thinnest surface, not for none");
        check(syn_bar_has_background(&cfg),
              "…so there is something for the backdrop blur to mask to");

        cfg.theme = SYN_THEME_PRISM_LIGHT;
        check(near(theme_bar_alpha(&cfg), SYN_BAR_ALPHA_FROSTED),
              "…and light Prism with it — one theme in two schemes");

        cfg.theme = SYN_THEME_MACOS26;
        check(near(theme_bar_alpha(&cfg), 0.0f),
              "macOS 26's bar still has no background at all");
        check(!syn_bar_has_background(&cfg),
              "…so the blur must NOT frost it: there is nothing there but glyphs");

        /* Every other preset has no view, which is what leaves the twelve retro
         * chromes on the scheme's own 0.85/0.95 exactly as they always were. */
        cfg.theme = SYN_THEME_WIN95;
        check(theme_bar_alpha(&cfg) < 0.0f,
              "a theme with no opinion still says so out of band");
        check(syn_bar_has_background(&cfg),
              "…and 'no opinion' resolves to a background, never to nothing");

        /* ⚠ THE USER'S ROW WINS OUTRIGHT, INCLUDING AT ZERO — which is the only
         * way anybody reaches a clear bar now that the presets have stopped
         * asking for one. "Make it all clear" is this line, committed as a row.
         * If bar_opacity ever stopped overriding the theme here, that action
         * would silently do nothing on a Prism desktop. */
        cfg.theme = SYN_THEME_PRISM;
        cfg.bar_opacity = 0.0f;
        check(near(syn_bar_alpha_asked(&cfg), 0.0f) && !syn_bar_has_background(&cfg),
              "asking for 0.00 by hand still gets a bar with no background");
        cfg.bar_opacity = 1.0f;
        check(syn_bar_has_background(&cfg), "…and asking for solid gets solid");
        cfg.bar_opacity = -1.0f;
        check(near(syn_bar_alpha_asked(&cfg), SYN_BAR_ALPHA_FROSTED),
              "…and handing the row back returns the theme's answer");
    }

    if (fails == 0) { printf("glass_sync_test: OK\n"); return 0; }
    printf("glass_sync_test: %d check(s) failed\n", fails);
    return 1;
}
