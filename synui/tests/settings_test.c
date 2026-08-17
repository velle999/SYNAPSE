/*
 * settings_test.c — the config parser and settings.state, without a compositor.
 *
 * Two things are pinned here, and the first one is why this file exists at all.
 *
 * synui_config_load() used to be one function: a block of defaults, then a
 * ~450-line `else if (strcmp(key, ...))` chain inside the read loop. Giving the
 * control panel every synuirc key meant that chain had to become callable from
 * somewhere else, so it was lifted out whole into config_parse_kv(). Moving 450
 * lines of key handling is exactly the change that silently drops one key, and
 * a dropped key does not crash — it reads as "that setting doesn't work", six
 * months later, from a user. So the spread below deliberately samples every
 * SHAPE the chain handles rather than every key: a clamped int, a float, an
 * `on`/`off` bool, a named enum, a hex colour, a string, and a bind, which is
 * the one case that mutates the value buffer in place.
 *
 * The second is the round trip. settings.state is written by the panel and read
 * back by config_parse_kv(), and the entire argument for that design is that a
 * key the writer emits cannot be a key the reader does not understand. A test
 * that only checked the writer would not notice the two drifting apart, so
 * every assertion here goes out through settings_state_set() and comes back
 * through a fresh synui_config_load().
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

/* ── Stubs ───────────────────────────────────────────────────
 *
 * config.c reaches out to the seven older per-subject state loaders and to a
 * few name-to-enum helpers. None of them is what is under test, and pulling in
 * the files that define them would drag in the compositor. They are no-ops.
 */
void binds_state_load(syn_config_t *c)        { (void)c; }
void wallpaper_state_load(syn_config_t *c)    { (void)c; }
void cursor_state_load(syn_config_t *c)       { (void)c; }
void dock_state_load(syn_config_t *c)         { (void)c; }
void power_state_load(syn_config_t *c)        { (void)c; }
void saver_state_load(syn_config_t *c)        { (void)c; }
/* Named-value parsers, not readers: config.c resolves the `screensaver` and
 * `lock_background` keys against saver.c's own spellings rather than keeping a
 * second copy of either list. Answering -1 is what an unknown name gets, which
 * leaves the config default standing — exactly what a test that never sets
 * those keys wants. */
int  saver_mode_from_name(const char *n)      { (void)n; return -1; }
int  lock_bg_from_name(const char *n)         { (void)n; return -1; }
void welcome_state_load(syn_config_t *c)      { (void)c; }
void launcher_state_load(syn_config_t *c)     { (void)c; }
void record_state_load(syn_config_t *c) { (void)c; }
void deskicons_state_load(syn_config_t *c)    { (void)c; }
/* Not a bare no-op: the real one records the preset it applied in cfg->theme,
 * and that assignment is the observable half of parsing a `theme =` line. A
 * stub that dropped it would make the theme case untestable here. The colour
 * seeding it also does is theme.c's business, and is not. */
void theme_load_colors(syn_config_t *c, syn_theme_t t) { c->theme = t; }
/* theme.state is now read by synui_config_load() like the other eight state
 * files (that is what stops a config reload resetting the desktop to stock).
 * Stubbed here for the same reason as the loaders above: this test owns no
 * theme.state, and reading the developer's would make the run non-hermetic. */
void theme_state_load_config(syn_config_t *c) { (void)c; }
/* Stubbed for the same reason theme.state is, one line up: its real reader
 * lives in notif.c, which drags in sd-bus and the scene graph. dnd.state's
 * reload survival — the property this file exists to guard — is asserted
 * end to end against a live compositor in tests/dnd.sh instead. */
void notif_dnd_state_load_config(syn_config_t *c) { (void)c; }
/* The Super+E panels' two state files, read from the same tail for the same
 * reason. Stubbed alongside the rest so the run stays hermetic. */
void filters_state_load_config(syn_config_t *c) { (void)c; }
void uifx_state_load_config(syn_config_t *c)    { (void)c; }
void wallpaper_output_apply(syn_config_t *c, const char *name,
                            const char *tok, int mode)
{ (void)c; (void)name; (void)tok; (void)mode; }

int  lid_action_from_name(const char *n)      { (void)n; return 0; }

/* dispcfg.c is not linked here; config.c parses `display_mode` through it.
 * Stubbed the same way lid_action_from_name() is, and for the same reason. */
int  display_mode_from_name(const char *n)   { (void)n; return -1; }

int  wallpaper_mode_from_name(const char *n)  { (void)n; return 0; }
bool syn_arrange_parse(const char *s, syn_arrange_t *out)
{ (void)s; (void)out; return false; }

/* The real name tables, because the theme test below asserts on one of them —
 * a stub of the wrong length would make `theme = win95` silently unparseable
 * and the assertion would be testing the stub. */
const char *const syn_theme_names[SYN_THEME_COUNT] = {
    "synapse", "dark", "winxp", "win95",
};
const char *const syn_wallpaper_mode_names[SYN_WALLPAPER_MODE_COUNT] = {
    "fill", "fit", "stretch", "center", "tile",
};

/* ── Rig ─────────────────────────────────────────────────────
 *
 * A scratch XDG_CONFIG_HOME so the real ~/.config/synui is never touched, and
 * SYNUI_CONFIG pointed at a synuirc this test writes. Both are how synui's own
 * headless harness runs, so the paths exercised are the real ones.
 */
static char g_dir[256];

static void rig_init(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/synui-settings-test-%d", (int)getpid());
    char sub[320];
    snprintf(sub, sizeof(sub), "%s/synui", g_dir);
    mkdir(g_dir, 0755);
    mkdir(sub, 0755);
    setenv("XDG_CONFIG_HOME", g_dir, 1);
    unsetenv("HOME");
}

static void rig_cleanup(void)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/synui/settings.state", g_dir); unlink(p);
    snprintf(p, sizeof(p), "%s/synui/synuirc", g_dir);        unlink(p);
    snprintf(p, sizeof(p), "%s/synui", g_dir);                rmdir(p);
    rmdir(g_dir);
}

static void write_synuirc(const char *body)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/synui/synuirc", g_dir);
    FILE *f = fopen(p, "w");
    assert(f);
    fputs(body, f);
    fclose(f);
    setenv("SYNUI_CONFIG", p, 1);
}

static int feq(float a, float b) { return fabsf(a - b) < 0.0001f; }

/* ── 1. The lifted chain still understands every shape ──────── */

static void test_parse_shapes(void)
{
    syn_config_t c;
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);   /* defaults, no file of consequence yet */

    char v[192];

    /* A clamped int. The clamp is the half of these that a careless move
     * loses: the assignment survives, the two bounds lines do not. */
    snprintf(v, sizeof(v), "7");     config_parse_kv(&c, "border_width", v);
    assert(c.border_width == 7);
    snprintf(v, sizeof(v), "9999");  config_parse_kv(&c, "border_width", v);
    assert(c.border_width == 32);    /* upper clamp */
    snprintf(v, sizeof(v), "-5");    config_parse_kv(&c, "border_width", v);
    assert(c.border_width == 0);     /* lower clamp */

    /* A float. */
    snprintf(v, sizeof(v), "0.75");  config_parse_kv(&c, "master_factor", v);
    assert(feq(c.master_factor, 0.75f));

    /* An on/off bool — and its negative, which is anything that is not "on".
     * A parser that only ever tested the true case would pass with `!= 0`. */
    snprintf(v, sizeof(v), "on");    config_parse_kv(&c, "snap", v);
    assert(c.snap == 1);
    snprintf(v, sizeof(v), "off");   config_parse_kv(&c, "snap", v);
    assert(c.snap == 0);

    /* A named enum. */
    snprintf(v, sizeof(v), "win95"); config_parse_kv(&c, "theme", v);
    assert(c.theme == SYN_THEME_WIN95);

    /* A hex colour. The leading '#' is also the character the inline-comment
     * stripper has to leave alone, so this value is load-bearing twice. */
    snprintf(v, sizeof(v), "#ff296d");
    config_parse_kv(&c, "border_color_focus", v);
    assert(feq(c.border_color_focus[0], 1.0f));
    assert(feq(c.border_color_focus[3], 1.0f));

    /* A string. */
    snprintf(v, sizeof(v), "kitty -e tmux");
    config_parse_kv(&c, "terminal", v);
    assert(strcmp(c.terminal, "kitty -e tmux") == 0);

    /* A bind — the one case that splits `val` in place, which is why
     * config_parse_kv takes a mutable pointer.
     *
     * The combo has to be one NOTHING ships a default for, or this asserts the
     * wrong thing: config_bind_set replaces by chord, so a bind on a defaulted
     * combo leaves bind_count where it was and the append assertion fails for a
     * reason that has nothing to do with parsing. This was super+shift+y until
     * the cascade layout took that key (2026-08-07). Ctrl+Alt+F12 is not a
     * chord this project is ever going to want. */
    int before = c.bind_count;
    snprintf(v, sizeof(v), "ctrl+alt+f12 term");
    config_parse_kv(&c, "bind", v);
    assert(c.bind_count == before + 1);

    /* An unknown key is ignored, not fatal: synuirc from a newer synui has to
     * be readable by an older one. */
    snprintf(v, sizeof(v), "1");
    config_parse_kv(&c, "not_a_real_key_at_all", v);

    printf("  parse shapes ....... ok\n");
}

/* ── 1b. Asking for the defaults must not repaint the desktop ─
 *
 * THIS TEST MUST RUN BEFORE test_defaults(). synui_config_defaults() builds its
 * static on the FIRST ask and never again, so the first ask is the only one
 * that can have a side effect — and it is the one this pins.
 *
 * The bug: config_set_defaults() ended with syn_text_set_ui_font(NULL). It runs
 * against a SCRATCH config, but text.c's copy of the family is process-global,
 * so that line reached past the scratch struct into the live desktop. The
 * control panel diffs every visible row against synui_config_defaults() on each
 * repaint, so the first open of the panel in a session silently reset the UI
 * font to monospace while settings.state still named the chosen family. The
 * font came back at the next login and vanished again at the next open, which
 * is the report this file now guards: "the control panel font resets itself".
 *
 * Generalised beyond the font: a defaults builder that touches anything outside
 * the struct it was handed is a defaults builder that changes the running
 * system just by being asked a question.
 */
static void test_defaults_have_no_side_effects(void)
{
    write_synuirc("ui_font = Liberation Serif\n");

    syn_config_t c;
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(strcmp(c.ui_font, "Liberation Serif") == 0);
    /* Parsed AND applied: a font in the config that text.c never hears about is
     * a setting that does nothing until the picker is opened. */
    assert(strcmp(syn_text_ui_font(), "Liberation Serif") == 0);

    /* The first ask. What the control panel does on every single repaint. */
    const syn_config_t *d = synui_config_defaults();
    assert(d->ui_font[0] == '\0');                      /* scratch says default */
    assert(strcmp(syn_text_ui_font(), "Liberation Serif") == 0);  /* live untouched */

    /* Repeat asks are no-ops by construction, but assert it rather than trust
     * the static. */
    (void)synui_config_defaults();
    assert(strcmp(syn_text_ui_font(), "Liberation Serif") == 0);

    /* The other half of the contract: with nothing naming a font, a load DOES
     * put the live copy back to the default. This is what the removed line was
     * originally there for — a reload must not leave the previous session's
     * font applied — and it is now done from the final resolved value at the
     * end of synui_config_load() instead of from the defaults block. */
    write_synuirc("# nothing here\n");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(c.ui_font[0] == '\0');
    assert(strcmp(syn_text_ui_font(), "monospace") == 0);

    /* And settings.state names it just as synuirc does — the picker writes
     * there, not into synuirc. */
    settings_state_set("ui_font", "DejaVu Sans");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(strcmp(c.ui_font, "DejaVu Sans") == 0);
    assert(strcmp(syn_text_ui_font(), "DejaVu Sans") == 0);

    settings_state_clear("ui_font");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(strcmp(syn_text_ui_font(), "monospace") == 0);

    printf("  defaults pure ...... ok\n");
}

/* ── 2. Defaults are a value, and they match a fresh load ───── */

static void test_defaults(void)
{
    const syn_config_t *d = synui_config_defaults();
    assert(d);

    /* Spot-check against what the header documents, so a default that moves
     * without its comment moving is caught here. */
    assert(d->remember_geometry == true);
    assert(d->snap == 1);
    assert(d->night_light == 0);
    assert(d->night_light_temp == 4000);
    assert(d->dock_enabled == 1);
    assert(d->transparency == 0);
    assert(d->clip_csd_margin == 1);
    assert(d->shadow == 1);
    assert(d->alt_tab_preview == 1);
    /* syntty since a1d0dc6 — this assertion still said kitty and had been
     * failing the suite ever since, which is exactly the "a default moved
     * without its comment moving" case the block above exists to catch. */
    assert(strcmp(d->terminal, "syntty") == 0);

    /* Asked twice, the same object: the panel diffs against this on every
     * repaint, so it must not be rebuilt (or worse, half-built) per call. */
    assert(synui_config_defaults() == d);

    printf("  defaults ........... ok\n");
}

/* ── 3. settings.state overrides synuirc, and survives a reload ── */

static void test_state_overrides(void)
{
    write_synuirc("border_width = 4\ngap = 10\nsnap = on\n");

    syn_config_t c;
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(c.border_width == 4);    /* synuirc, with no state file yet */
    assert(c.gap == 10);
    assert(c.snap == 1);

    /* What the panel does when a row changes. */
    settings_state_set("border_width", "11");
    settings_state_set("snap", "off");
    assert(settings_state_has("border_width"));
    assert(!settings_state_has("gap"));

    /* The load order is the whole claim: state is applied after synuirc. */
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(c.border_width == 11);   /* overridden */
    assert(c.gap == 10);            /* untouched — still synuirc's */
    assert(c.snap == 0);

    /* Overrides read back at load are REMEMBERED, not just applied. The panel
     * rewrites the whole file on every change, so a value that was applied but
     * not retained would be dropped by the next unrelated save — the setting
     * would work all session and vanish at the next login. */
    settings_state_set("gap", "22");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(c.border_width == 11);   /* still here after an unrelated save */
    assert(c.gap == 22);

    printf("  state overrides .... ok\n");
}

/* ── 4. Reset forgets the key rather than storing the default ── */

static void test_state_clear(void)
{
    write_synuirc("border_width = 4\n");

    settings_state_set("border_width", "11");
    syn_config_t c;
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(c.border_width == 11);

    settings_state_clear("border_width");
    assert(!settings_state_has("border_width"));

    /* Back to synuirc's value, NOT to the compiled-in default: reset means
     * "stop overriding", and synuirc is what is underneath. */
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(c.border_width == 4);

    /* And with no synuirc line either, the compiled default surfaces. */
    write_synuirc("# nothing here\n");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(c.border_width == synui_config_defaults()->border_width);

    /* Clearing a key that was never set is not an error. */
    settings_state_clear("gap");

    printf("  state clear ........ ok\n");
}

/* ── 5. Every key the panel can write, the parser can read ───
 *
 * The round trip in miniature, over the value shapes the panel emits. If this
 * ever fails it means settings_state_set() and config_parse_kv() have drifted,
 * which is the one failure the shared-parser design exists to prevent.
 */
static void test_round_trip(void)
{
    write_synuirc("# defaults only\n");

    static const struct { const char *key, *val; } spread[] = {
        { "border_width",      "9"       },
        { "gap",               "14"      },
        { "corner_radius",     "0"       },
        { "titlebar_height",   "28"      },
        { "master_factor",     "0.55"    },
        { "animation_ms",      "0"       },
        { "snap",              "off"     },
        { "remember_geometry", "off"     },
        { "alt_tab_preview",   "off"     },
        { "shadow",            "off"     },
        { "blur",              "off"     },
        { "night_light_temp",  "3200"    },
        { "repeat_rate",       "40"      },
        { "cursor_size",       "32"      },
        { "terminal",          "foot"    },
    };

    for (unsigned i = 0; i < sizeof(spread) / sizeof(spread[0]); i++)
        settings_state_set(spread[i].key, spread[i].val);

    syn_config_t c;
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);

    assert(c.border_width == 9);
    assert(c.gap == 14);
    assert(c.corner_radius == 0);
    assert(c.titlebar_height == 28);
    assert(feq(c.master_factor, 0.55f));
    assert(c.animation_ms == 0);
    assert(c.snap == 0);
    assert(c.remember_geometry == false);
    assert(c.alt_tab_preview == 0);
    assert(c.shadow == 0);
    assert(c.blur == 0);
    assert(c.night_light_temp == 3200);
    assert(c.repeat_rate == 40);
    assert(c.cursor_size == 32);
    assert(strcmp(c.terminal, "foot") == 0);

    printf("  round trip ......... ok (%u keys)\n",
           (unsigned)(sizeof(spread) / sizeof(spread[0])));
}

/* ── 6. Super+Space and Super+= are binds, and ONLY binds ───────────── */

/* The bind table has no lookup helper outside config.c, so find by combo here. */
static const syn_bind_t *bind_of(const syn_config_t *c, xkb_keysym_t sym)
{
    for (int i = 0; i < c->bind_count; i++)
        if (c->binds[i].mods == WLR_MODIFIER_LOGO && c->binds[i].sym == sym)
            return &c->binds[i];
    return NULL;
}

static int holds(const syn_bind_t *b, const char *action, const char *arg)
{
    return b && strcmp(b->action, action) == 0 && strcmp(b->arg, arg) == 0;
}

/*
 * There used to be a `super_space = launcher|cmdbar` setting (and a control
 * panel row) that exchanged these two actions at the END of every config load —
 * after binds.state, so it put back whatever the shortcuts palette had just
 * rebound. Two owners of one chord. This pins its removal from both directions:
 * the key does nothing, and a swap written as ordinary binds STAYS swapped.
 */
static void test_launcher_pair_is_just_binds(void)
{
    syn_config_t c;

    /* Shipped: the launcher on Space, the command bar on '='. */
    write_synuirc("");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(holds(bind_of(&c, XKB_KEY_space), "spawn_toggle", "rofi -show drun"));
    assert(holds(bind_of(&c, XKB_KEY_equal), "cmdbar", ""));

    /* The dead key moves nothing. Not merely unparsed — if a swap ever comes
     * back, THIS is the assertion that fails. */
    write_synuirc("super_space = cmdbar\n");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(holds(bind_of(&c, XKB_KEY_space), "spawn_toggle", "rofi -show drun"));
    assert(holds(bind_of(&c, XKB_KEY_equal), "cmdbar", ""));

    /* The supported way to swap them: two binds, and nothing re-seats them. */
    write_synuirc("bind = super+space cmdbar\n"
                  "bind = super+equal spawn_toggle rofi -show drun\n");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(holds(bind_of(&c, XKB_KEY_space), "cmdbar", ""));
    assert(holds(bind_of(&c, XKB_KEY_equal), "spawn_toggle", "rofi -show drun"));

    /* Same swap with the dead key ALSO asking for the other arrangement: the
     * binds win, because there is no longer anything to lose to. */
    write_synuirc("bind = super+space cmdbar\n"
                  "bind = super+equal spawn_toggle rofi -show drun\n"
                  "super_space = launcher\n");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(holds(bind_of(&c, XKB_KEY_space), "cmdbar", ""));
    assert(holds(bind_of(&c, XKB_KEY_equal), "spawn_toggle", "rofi -show drun"));

    printf("  launcher pair is binds ... ok\n");
}

/*
 * An upgrade finds `super_space` sitting in settings.state, because the row
 * that wrote it shipped for weeks. load() remembers every line it reads so the
 * next save cannot drop one, which would rewrite a dead key forever — so the
 * obsolete list drops it on the way in instead.
 */
static void test_obsolete_key_leaves_settings_state(void)
{
    syn_config_t c;

    write_synuirc("");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);

    /* Written the way the old row wrote it. */
    settings_state_set("super_space", "cmdbar");
    assert(settings_state_has("super_space"));

    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(!settings_state_has("super_space"));

    /* And a live key alongside it is untouched — the drop is by name, not a
     * "forget the file" fallback. */
    settings_state_set("gap", "13");
    settings_state_set("super_space", "cmdbar");
    memset(&c, 0, sizeof(c));
    synui_config_load(&c);
    assert(!settings_state_has("super_space"));
    assert(settings_state_has("gap"));
    assert(c.gap == 13);
    settings_state_clear("gap");

    printf("  obsolete key dropped ... ok\n");
}

int main(void)
{
    rig_init();

    printf("settings_test\n");
    test_parse_shapes();
    /* Before test_defaults(): synui_config_defaults() builds its static on the
     * first ask, so only the first ask can misbehave. See the function. */
    test_defaults_have_no_side_effects();
    test_defaults();
    test_state_overrides();
    test_state_clear();
    test_round_trip();
    test_launcher_pair_is_just_binds();
    test_obsolete_key_leaves_settings_state();

    rig_cleanup();
    printf("settings_test: all ok\n");
    return 0;
}
