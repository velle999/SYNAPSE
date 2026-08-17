/*
 * state_reload_test.c — the state files a config RELOAD must not throw away.
 *
 * synui_config_reload() does `s->config = fresh` after synui_config_load(&fresh).
 * So every setting that lives in syn_config_t comes back from whatever
 * synui_config_load() reads, and from nowhere else: a .state file loaded once
 * from synui_main() is a setting the desktop loses on every SIGHUP, every
 * super+shift+w, and every Ctrl+Shift+R in the shortcut palette (which resets
 * the binds THROUGH a reload).
 *
 * theme.state was the first to be caught doing this. filters.state and
 * uifx.state were the next two, and the CRT one was worse than a reset to the
 * compiled defaults: synuirc ships `effects = on`, so a reload did not turn the
 * shader off, it turned it ON — in whatever phosphor settings.state was
 * carrying. Reported as "I hit Ctrl+Shift+R in the keybind menu to reset it and
 * it's turning on window effects with amber tint".
 *
 * The rig is the whole point. Every source that had a say in that bug is
 * present and says something DIFFERENT — synuirc `effects = on`, settings.state
 * `effect_phosphor = amber`, filters.state off — so a load that skips the last
 * of them cannot accidentally produce the right answer.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

/* ── Stubs ───────────────────────────────────────────────────
 *
 * Only the load path is under test, so everything the panels call to make a
 * change visible is absent here. filters.c and uifx.c are linked whole — the
 * point is to exercise the REAL readers, since a test that reimplemented the
 * file format would pass while production read a different one.
 */
void synui_render_filters(syn_server_t *s) { (void)s; }
void view_update_decorations(syn_view_t *v) { (void)v; }
void transparency_set_enabled(syn_server_t *s, int on) { (void)s; (void)on; }
void transparency_set_opacity(syn_server_t *s, float o) { (void)s; (void)o; }
void theme_load_colors(syn_config_t *c, syn_theme_t t) { c->theme = t; }
void theme_state_load_config(syn_config_t *c) { (void)c; }
/* Stubbed for the same reason theme.state is, one line up: its real reader
 * lives in notif.c, which drags in sd-bus and the scene graph. dnd.state's
 * reload survival — the property this file exists to guard — is asserted
 * end to end against a live compositor in tests/dnd.sh instead. */
void notif_dnd_state_load_config(syn_config_t *c) { (void)c; }
void wallpaper_state_load(syn_config_t *c)  { (void)c; }
void cursor_state_load(syn_config_t *c)     { (void)c; }
void dock_state_load(syn_config_t *c)       { (void)c; }
void power_state_load(syn_config_t *c)      { (void)c; }
void welcome_state_load(syn_config_t *c)    { (void)c; }
void launcher_state_load(syn_config_t *c)   { (void)c; }
void record_state_load(syn_config_t *c)     { (void)c; }
void deskicons_state_load(syn_config_t *c)  { (void)c; }
void binds_state_load(syn_config_t *c)      { (void)c; }
void wallpaper_output_apply(syn_config_t *c, const char *n, const char *t, int m)
{ (void)c; (void)n; (void)t; (void)m; }
int  lid_action_from_name(const char *n)     { (void)n; return 0; }

/* dispcfg.c is not linked here; config.c parses `display_mode` through it.
 * Stubbed the same way lid_action_from_name() is, and for the same reason. */
int  display_mode_from_name(const char *n)   { (void)n; return -1; }

int  wallpaper_mode_from_name(const char *n) { (void)n; return 0; }
bool syn_arrange_parse(const char *s, syn_arrange_t *out)
{ (void)s; (void)out; return false; }
void ctlpanel_child_closed(syn_server_t *s, const char *action)
{ (void)s; (void)action; }
void anim_apply_alpha_all(syn_server_t *s)   { (void)s; }
int  hit_in_panel(const syn_hit_t *g, double lx, double ly)
{ (void)g; (void)lx; (void)ly; return 0; }
int  hit_row_at(const syn_hit_t *g, double lx, double ly)
{ (void)g; (void)lx; (void)ly; return -1; }

/* Real tables, not stubs of the wrong length: config.c resolves `theme =` and
 * `wallpaper_mode =` against them by index, and a short table turns a valid
 * synuirc line into a silent parse failure inside the code under test. */
const char *const syn_theme_names[SYN_THEME_COUNT] = {
    "synapse", "dark", "winxp", "win95",
};
const char *const syn_wallpaper_mode_names[SYN_WALLPAPER_MODE_COUNT] = {
    "fill", "fit", "stretch", "center", "tile",
};

/* ── An isolated ~/.config/synui ─────────────────────────── */

static char g_dir[256];

static void write_file(const char *name, const char *body)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/synui/%s", g_dir, name);
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(body, f);
    fclose(f);
}

static void rig_up(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/synui-state-reload-%d", (int)getpid());
    char sub[512];
    snprintf(sub, sizeof(sub), "%s/synui", g_dir);
    assert(mkdir(g_dir, 0700) == 0 || errno == EEXIST);
    assert(mkdir(sub, 0700) == 0 || errno == EEXIST);
    setenv("XDG_CONFIG_HOME", g_dir, 1);
    unsetenv("SYNUI_CONFIG");
}

static void rig_down(void)
{
    const char *names[] = { "synuirc", "settings.state", "filters.state",
                            "uifx.state", "saver.state" };
    char path[512];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s/synui/%s", g_dir, names[i]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/synui", g_dir);
    rmdir(path);
    rmdir(g_dir);
}

/* ── 1. The reported bug ─────────────────────────────────────
 *
 * Three sources, three different answers, and the one that must win is the one
 * that was being skipped.
 */
static void test_filters_state_survives_a_load(void)
{
    write_file("synuirc",
               "effects           = on\n"
               "effect_scanline   = 0.35\n");
    write_file("settings.state", "effect_phosphor = amber\n");
    write_file("filters.state",
               "enabled=0\n"
               "scanline=0.300\n"
               "phosphor=off\n"
               "bloom=0.150\n");

    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    synui_config_load(&cfg);

    if (cfg.effects) {
        printf("    effects came back ON — filters.state was not read, so a "
               "reload puts synuirc's `effects = on` back on the desktop\n");
        assert(0);
    }
    if (cfg.effect_phosphor != SYN_PHOSPHOR_OFF) {
        printf("    phosphor is %d, not off — settings.state won, which means "
               "filters.state was read too early or not at all\n",
               cfg.effect_phosphor);
        assert(0);
    }

    /* The strengths too, and one of them disagrees with synuirc on purpose:
     * 0.300 in filters.state against 0.35 in the rc. */
    assert(cfg.effect_scanline > 0.29f && cfg.effect_scanline < 0.31f);
    assert(cfg.effect_bloom > 0.14f && cfg.effect_bloom < 0.16f);

    printf("  filters.state survives a load ... ok\n");
}

/* ── 2. Page two of the same panel ───────────────────────────
 *
 * uifx.state is read from the same tail and loses the same way. It also has to
 * beat settings.state, which the control panel writes for the same fields
 * whenever an older synui wrote one there.
 */
static void test_uifx_state_survives_a_load(void)
{
    write_file("synuirc",
               "blur          = on\n"
               "blur_passes   = 1\n"
               "corner_radius = 12\n");
    write_file("settings.state", "blur_passes = 2\ncorner_radius = 20\n");
    write_file("uifx.state",
               "corner_radius=6\n"
               "blur=1\n"
               "blur_passes=3\n"
               "glass_halo=2\n");

    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    synui_config_load(&cfg);

    if (cfg.blur_passes != 3 || cfg.corner_radius != 6) {
        printf("    blur_passes %d (want 3), corner_radius %d (want 6) — "
               "uifx.state must be read, and read after settings.state\n",
               cfg.blur_passes, cfg.corner_radius);
        assert(0);
    }
    assert(cfg.glass_halo == 2);

    printf("  uifx.state survives a load ...... ok\n");
}

/* ── 3. No file is not an error ──────────────────────────────
 *
 * A desktop that has never opened Super+E has neither file, and must come up on
 * what synuirc says rather than on zeroes.
 */
static void test_absent_files_leave_the_rc_alone(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/synui/filters.state", g_dir); unlink(path);
    snprintf(path, sizeof(path), "%s/synui/uifx.state", g_dir);    unlink(path);
    snprintf(path, sizeof(path), "%s/synui/settings.state", g_dir); unlink(path);

    write_file("synuirc", "effects = on\nblur_passes = 4\n");

    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    synui_config_load(&cfg);

    assert(cfg.effects == 1);
    assert(cfg.blur_passes == 4);

    printf("  absent state files ............. ok\n");
}

/* ── 4. saver.state ──────────────────────────────────────────
 *
 * The screensaver and the lock-screen appearance live in syn_config_t, so they
 * lose to a reload the same way filters.state did — and this one is worse than
 * a cosmetic reset: a saver_timeout that comes back as 0 means the screensaver
 * silently stops appearing, and a lock_bg that comes back as the default means
 * a locked machine shows a wallpaper the user turned off.
 *
 * Same rig discipline as the two above: synuirc says something DIFFERENT from
 * saver.state on every field, so a load that skips the state file cannot
 * accidentally produce the right answer.
 */
static void test_saver_state_survives_a_load(void)
{
    write_file("synuirc",
               "screensaver = starfield\n"
               "screensaver_timeout = 60\n"
               "screensaver_lock = off\n"
               "lock_background = black\n"
               "lock_dim = 10\n"
               "lock_blur = 4\n");
    write_file("saver.state",
               "mode=slideshow\n"
               "timeout=300\n"
               "lock=1\n"
               "interval=45\n"
               "lock_bg=desktop\n"
               "lock_dim=70\n"
               "lock_blur=24\n"
               "lock_follow=0\n");

    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    synui_config_load(&cfg);

    if (cfg.saver_timeout == 60) {
        printf("    saver_timeout is still the rc's 60 — saver.state was not "
               "read, so every Super+Z choice dies on the next reload\n");
        assert(0);
    }

    assert(cfg.saver_mode == SYN_SAVER_SLIDESHOW);
    assert(cfg.saver_timeout == 300);
    assert(cfg.saver_lock == 1);
    assert(cfg.saver_interval == 45);
    assert(cfg.lock_bg == SYN_LOCK_BG_DESKTOP);
    assert(cfg.lock_bg_dim == 70);
    assert(cfg.lock_bg_blur == 24);
    assert(cfg.lock_theme_follow == 0);

    printf("  saver.state survives a load .... ok\n");
}

/* ── 5. Out-of-range values are refused, not clamped silently into nonsense ──
 *
 * A hand-edited saver.state is the case here. The rule the readers follow is
 * that a value they cannot honour leaves the previous one standing rather than
 * becoming 0 — an interval of 0 would spin the slideshow at the frame rate, and
 * a mode index that no longer exists would select whatever now sits at that
 * slot. That last one is why modes are stored as NAMES.
 */
static void test_bad_saver_state_leaves_the_rc_alone(void)
{
    write_file("synuirc",
               "screensaver = clock\n"
               "screensaver_timeout = 120\n"
               "screensaver_interval = 30\n"
               "lock_dim = 40\n");
    write_file("saver.state",
               "mode=nosuchmode\n"       /* unknown name */
               "interval=99999\n"        /* out of range */
               "lock_bg=nosuchbg\n"      /* unknown name */
               "lock_dim=999\n");        /* clamped, not wrapped */

    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    synui_config_load(&cfg);

    assert(cfg.saver_mode == SYN_SAVER_CLOCK);     /* rc stands */
    assert(cfg.saver_interval == 30);              /* rc stands */
    assert(cfg.lock_bg_dim == 100);                /* clamped to the maximum */
    assert(cfg.saver_timeout == 120);              /* untouched key */

    printf("  bad saver.state values ......... ok\n");
}

int main(void)
{
    printf("state reload test\n");
    rig_up();

    test_filters_state_survives_a_load();
    test_uifx_state_survives_a_load();
    test_absent_files_leave_the_rc_alone();
    test_saver_state_survives_a_load();
    test_bad_saver_state_leaves_the_rc_alone();

    rig_down();
    printf("all state reload tests passed\n");
    return 0;
}
