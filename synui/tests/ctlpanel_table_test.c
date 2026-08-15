/*
 * ctlpanel_table_test.c — the control panel's item table, checked against the
 * config parser it claims to speak the same language as.
 *
 * The panel went from thirty hand-written rows to a hundred table-driven ones,
 * and the whole design rests on three claims that nothing in the compiler can
 * check:
 *
 *   1. Every row's `key` is a real synuirc key. A typo is not a build error —
 *      it is a setting that adjusts fine, persists fine, and is silently gone
 *      at the next login, because config_parse_kv() never recognised the line
 *      settings.state wrote. This is the failure the shared parser was supposed
 *      to make impossible, and it is only impossible if the keys are right.
 *
 *   2. Every row's `off` points at the field that key actually writes. An
 *      offset copied from the row above compiles perfectly and adjusts the
 *      wrong setting — the row moves, and something else changes.
 *
 *   3. Every row's range agrees with the parser's clamp. Where they disagree
 *      the panel writes a value the parser silently rewrites, so the row shows
 *      one number this session and a different one after a reboot.
 *
 * Each is checked the same way, and the way is the point: drive the ROW, then
 * assert on the CONFIG after a real round trip through settings.state. No test
 * here reaches into the table and reads what it says about itself.
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
 * Everything the panel calls to make a change VISIBLE. None of it is under
 * test — what is under test is that the right one is called and that the value
 * landed — so each records that it ran and does nothing.
 */
static int applied_uifx, applied_input, applied_layout, applied_deco;
static int applied_dock, applied_nightlight, applied_cursor, applied_deskicons;
static int applied_wallpaper;

void uifx_apply(syn_server_t *s)              { (void)s; applied_uifx++; }
void input_reload_config(syn_server_t *s)     { (void)s; applied_input++; }
void deco_refresh_all(syn_server_t *s)        { (void)s; applied_deco++; }
void dock_rebuild(syn_server_t *s)            { (void)s; applied_dock++; }
void dock_relayout(syn_server_t *s)           { (void)s; }
void nightlight_apply(syn_server_t *s)        { (void)s; applied_nightlight++; }
void cursor_reload(syn_server_t *s)           { (void)s; applied_cursor++; }
void deskicons_reload(syn_server_t *s)        { (void)s; applied_deskicons++; }
void wallpaper_relayout(syn_server_t *s)      { (void)s; applied_wallpaper++; }
void layout_apply(syn_server_t *s, syn_workspace_t *ws)
{ (void)s; (void)ws; applied_layout++; }

void synui_render_ctlpanel(syn_server_t *s)   { (void)s; }
/* panel.c dispatches a repaint to whichever panel is being dragged, which pulls
 * in the other two panels' renderers. Neither is exercised here. */
void synui_render_calc(syn_server_t *s)    { (void)s; }
void synui_render_taskmgr(syn_server_t *s) { (void)s; }

void synui_render_aimodel(syn_server_t *s)    { (void)s; }
void synmon_want_refresh(syn_server_t *s)     { (void)s; }
void synui_child_reset_signals(void)          { }
/* The Bar row shells out to stop or start the bar — it is a separate process,
 * so the row cannot just flip a flag the way the Dock row does. */
void synui_spawn(const char *cmd)             { (void)cmd; }
void synui_binding_execute(syn_server_t *s, const char *a, const char *b)
{ (void)s; (void)a; (void)b; }
int  synmon_send_reload(const char *m, char *o, size_t n)
{ (void)m; (void)o; (void)n; return 0; }

/* The rebind core is keys.c's, which is not linked here — this test is about
 * the ctl_items[] table against the config parser. keys_test links keys.c and
 * ctlpanel.c together and is where rebinding is actually exercised. */
bool syn_rebind_capture_ignores(const syn_ctl_shortcut_t *sc, xkb_keysym_t sym)
{ (void)sc; (void)sym; return false; }
const char *syn_rebind_refusal(const syn_ctl_shortcut_t *sc) { (void)sc; return NULL; }
int syn_rebind_apply(syn_server_t *s, const syn_ctl_shortcut_t *sc,
                     xkb_keysym_t sym, uint32_t mods, char *status, size_t n)
{ (void)s; (void)sc; (void)sym; (void)mods; if (n) status[0] = '\0'; return 0; }
void syn_rebind_reset_all(syn_server_t *s, char *status, size_t n)
{ (void)s; if (n) status[0] = '\0'; }
int syn_rebind_set_tap_action(syn_server_t *s, const syn_ctl_shortcut_t *sc,
                              char *status, size_t n)
{ (void)s; (void)sc; if (n) status[0] = '\0'; return 0; }

void deco_toggle_titlebars(syn_server_t *s)   { (void)s; }
void dock_state_save(syn_server_t *s)         { (void)s; }
void dock_wake(syn_server_t *s)               { (void)s; }
void game_toggle(syn_server_t *s)             { (void)s; }
void launcher_toggle_style(syn_server_t *s)   { (void)s; }
void nightlight_toggle(syn_server_t *s)       { (void)s; }
void notif_dnd_toggle(syn_server_t *s)        { (void)s; }
void record_audio_toggle(syn_server_t *s)     { (void)s; }
void record_edit_toggle(syn_server_t *s)      { (void)s; }
void sound_state_refresh(syn_server_t *s)     { (void)s; }
void transparency_set_enabled(syn_server_t *s, int on)  { (void)s; (void)on; }
void transparency_set_opacity(syn_server_t *s, float o) { (void)s; (void)o; }
const char *layout_label(syn_layout_t l)      { (void)l; return "stack"; }
const char *theme_name(syn_theme_t t)         { (void)t; return "synapse"; }
syn_workspace_t *server_active_workspace(syn_server_t *s) { (void)s; return NULL; }

/* The eight older state files and the name helpers config.c reaches for.
 * binds.state's loader lives in keys.c (its owner, the rebind helper), which is
 * not linked here — the bind primitives it calls back into ARE, and they are
 * what the round-trip test below exercises. */
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
void record_state_load(syn_config_t *c)       { (void)c; }
void deskicons_state_load(syn_config_t *c)    { (void)c; }
void theme_load_colors(syn_config_t *c, syn_theme_t t) { c->theme = t; }
/* theme.state is now read by synui_config_load() like the other eight state
 * files (that is what stops a config reload resetting the desktop to stock).
 * Stubbed here for the same reason as the loaders above: this test owns no
 * theme.state, and reading the developer's would make the run non-hermetic. */
void theme_state_load_config(syn_config_t *c) { (void)c; }
void notif_dnd_state_load_config(syn_config_t *c) { (void)c; }

/*
 * The Super+E panels' two state files.
 *
 * These are not no-ops, because the rows they own round-trip THROUGH them: the
 * CRT and window-effect rows are stored in filters.state / uifx.state rather
 * than settings.state, since both files are read after settings.state and would
 * otherwise overwrite what this panel had just written (that is how a phosphor
 * picked here was back to off at the next login).
 *
 * Modelled on the contract of the real pair rather than on their file format:
 * each saver writes every field its file owns, straight from the live config,
 * and each loader puts all of them back. Holding the fields in memory keeps the
 * test hermetic — the real savers write the developer's ~/.config — while still
 * making the round-trip test below a real test of where the row was stored: a
 * row tagged with the wrong owner is one whose value does not come back.
 */
static struct { syn_config_t cfg; int saved; } g_filters_file, g_uifx_file;

void filters_state_save(syn_server_t *s)
{
    g_filters_file.cfg = s->config;
    g_filters_file.saved = 1;
}

void filters_state_load_config(syn_config_t *c)
{
    if (!g_filters_file.saved) return;
    const syn_config_t *f = &g_filters_file.cfg;
    c->effects           = f->effects;
    c->effect_scanline   = f->effect_scanline;
    c->effect_curvature  = f->effect_curvature;
    c->effect_aberration = f->effect_aberration;
    c->effect_glitch     = f->effect_glitch;
    c->effect_phosphor   = f->effect_phosphor;
    c->effect_mono       = f->effect_mono;
    c->effect_bloom      = f->effect_bloom;
}

void uifx_state_save(syn_server_t *s)
{
    g_uifx_file.cfg = s->config;
    g_uifx_file.saved = 1;
}

void uifx_state_load_config(syn_config_t *c)
{
    if (!g_uifx_file.saved) return;
    const syn_config_t *u = &g_uifx_file.cfg;
    c->corner_radius     = u->corner_radius;
    c->shadow            = u->shadow;
    c->shadow_blur_sigma = u->shadow_blur_sigma;
    c->shadow_spread     = u->shadow_spread;
    c->shadow_offset_y   = u->shadow_offset_y;
    c->shadow_color[3]   = u->shadow_color[3];
    c->blur              = u->blur;
    c->blur_radius       = u->blur_radius;
    c->blur_passes       = u->blur_passes;
    c->glass_halo        = u->glass_halo;
}
void wallpaper_output_apply(syn_config_t *c, const char *n, const char *t, int m)
{ (void)c; (void)n; (void)t; (void)m; }
int  lid_action_from_name(const char *n)      { (void)n; return 0; }
int  wallpaper_mode_from_name(const char *n)  { (void)n; return 0; }
/* A real implementation, not a no-op. The Icon order row round-trips THROUGH
 * this, so a stub that refused every name would fail that row for a reason
 * belonging entirely to the test — which it duly did, the first time it ran. */
bool syn_arrange_parse(const char *s, syn_arrange_t *out)
{
    static const char *const names[] = { "name", "type", "size", "date" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcmp(s, names[i]) == 0) { *out = (syn_arrange_t)i; return true; }
    return false;
}
const char *const syn_theme_names[SYN_THEME_COUNT] = {
    "synapse", "dark", "winxp", "win95",
};
const char *const syn_wallpaper_mode_names[SYN_WALLPAPER_MODE_COUNT] = {
    "fill", "fit", "stretch", "center", "tile",
};

/* ── Rig ─────────────────────────────────────────────────── */

static char g_dir[256];
static syn_server_t g_s;

static void rig_init(void)
{
    snprintf(g_dir, sizeof(g_dir), "/tmp/synui-ctltable-test-%d", (int)getpid());
    char sub[320];
    snprintf(sub, sizeof(sub), "%s/synui", g_dir);
    mkdir(g_dir, 0755);
    mkdir(sub, 0755);
    setenv("XDG_CONFIG_HOME", g_dir, 1);
    unsetenv("HOME");

    /* An empty synuirc, so every setting starts at its compiled default and a
     * row that turns out to be at a non-default value is this test's doing. */
    char p[512];
    snprintf(p, sizeof(p), "%s/synui/synuirc", g_dir);
    FILE *f = fopen(p, "w");
    assert(f);
    fputs("# empty\n", f);
    fclose(f);
    setenv("SYNUI_CONFIG", p, 1);

    memset(&g_s, 0, sizeof(g_s));
    synui_config_load(&g_s.config);

    /* The panel holds the keyboard, because that is the state a user is in when
     * they press Right on a row. The control panel now ships as a WINDOW
     * (syn_panel_close_t), and a windowed panel only answers for keys while it
     * has them — so a rig that skipped this would be driving a panel nobody had
     * focused, and every row would read as inert. */
    panel_take_kbd(&g_s, SYN_PDRAG_CTLPANEL);


    /* ctlpanel_repaint() damages every output, so the list has to be a list
     * even though this rig has no outputs — a zeroed wl_list is not an empty
     * one, it is a null pointer the first iteration dereferences. */
    wl_list_init(&g_s.outputs);
}

static void rig_cleanup(void)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/synui/settings.state", g_dir); unlink(p);
    snprintf(p, sizeof(p), "%s/synui/synuirc", g_dir);        unlink(p);
    snprintf(p, sizeof(p), "%s/synui", g_dir);                rmdir(p);
    rmdir(g_dir);
}

/* Put the cursor on a row, wherever in the panel it lives. */
static int select_row(int row)
{
    for (int c = 0; c < CTL_CAT_COUNT; c++) {
        int rows[CTL_CAT_ITEMS_MAX];
        int n = ctlpanel_cat_items(c, rows, CTL_CAT_ITEMS_MAX);
        for (int i = 0; i < n; i++) {
            if (rows[i] != row) continue;
            g_s.ctlpanel.visible = 1;
            g_s.ctlpanel.cat     = c;
            g_s.ctlpanel.item    = i;
            g_s.ctlpanel.focus   = CTL_FOCUS_ITEMS;
            return 1;
        }
    }
    return 0;
}

/* ── 1. Every row in the enum is in the table, exactly once ── */

static void test_table_covers_every_row(void)
{
    int missing = 0, dup = 0;

    for (int row = 0; row < CTL_ROW_COUNT; row++) {
        int seen = 0;
        for (int c = 0; c < CTL_CAT_COUNT; c++) {
            int rows[CTL_CAT_ITEMS_MAX];
            int n = ctlpanel_cat_items(c, rows, CTL_CAT_ITEMS_MAX);
            for (int i = 0; i < n; i++)
                if (rows[i] == row) seen++;
        }
        if (seen == 0) { printf("    row %d is in no category\n", row); missing++; }
        if (seen > 1)  { printf("    row %d is in %d categories\n", row, seen); dup++; }
    }

    assert(missing == 0);
    assert(dup == 0);
    printf("  every row placed .......... ok (%d rows)\n", CTL_ROW_COUNT);
}

/* ── 2. No category overflows the bound callers size arrays with ── */

static void test_no_category_overflows(void)
{
    int total = 0;
    for (int c = 0; c < CTL_CAT_COUNT; c++) {
        int rows[CTL_CAT_ITEMS_MAX];
        int n = ctlpanel_cat_items(c, rows, CTL_CAT_ITEMS_MAX);

        /* Shortcuts is generated from the bind table and legitimately has no
         * rows of its own; every other category having none would mean a
         * sidebar entry that opens onto nothing. */
        if (c != CTL_CAT_SHORTCUTS) assert(n > 0);

        assert(n <= CTL_CAT_ITEMS_MAX);
        total += n;
    }
    assert(total == CTL_ROW_COUNT);
    printf("  category bounds .......... ok (%d rows over %d categories)\n",
           total, CTL_CAT_COUNT - 1);
}

/* ── 2b. Every category has a name, and it is unique ─────────
 *
 * ctlpanel_cat_name() is a switch, so a category added to the enum without a
 * case falls to the "?" default and draws a question mark in the sidebar —
 * which is exactly how Windows and Input shipped in pkgrel 262. Worse, two
 * unnamed categories both answer to "?", so ctlpanel_cat_from_name() (what
 * synctl resolves against) silently hands out the first one for both.
 */
static void test_every_category_named(void)
{
    for (int c = 0; c < CTL_CAT_COUNT; c++) {
        const char *name = ctlpanel_cat_name(c);

        assert(name && *name);
        if (strcmp(name, "?") == 0) printf("    category %d has no name\n", c);
        assert(strcmp(name, "?") != 0);

        /* Unique, and reachable by the name it draws. */
        assert(ctlpanel_cat_from_name(name) == c);
    }
    printf("  every category named ..... ok (%d categories)\n", CTL_CAT_COUNT);
}

/* ── 3. The round trip, per row ──────────────────────────────
 *
 * The important one. For every row that names a config key: move it with the
 * same call the Right arrow makes, then reload the whole config from disk and
 * assert the field came back changed. Reloading is what makes this a test of
 * the KEY and not just of the offset — a wrong key adjusts the live config
 * perfectly and loses it here.
 */
static void test_every_row_round_trips(void)
{
    int checked = 0, skipped = 0;

    for (int row = 0; row < CTL_ROW_COUNT; row++) {
        if (!select_row(row)) continue;
        if (ctlpanel_row_kind(row) == CTL_KIND_CHOICE) { skipped++; continue; }

        /* Rows with no synuirc key have nothing to round trip: the jump-offs,
         * and the toggles whose state is not a config field. Asked of the panel
         * rather than guessed from the row's kind, because a jump-off can still
         * SHOW a value (the Theme row displays the active preset) and a value
         * on screen is not the same thing as a setting this code owns. */
        if (!ctlpanel_row_key(row)) { skipped++; continue; }

        char before_val[64];
        ctlpanel_row_value(&g_s, row, before_val, sizeof(before_val));
        assert(ctlpanel_row_is_default(&g_s, row));   /* rig starts at defaults */

        char before_cfg[sizeof(syn_config_t)];
        memcpy(before_cfg, &g_s.config, sizeof(g_s.config));

        /* Right, as the key handler calls it. */
        ctlpanel_key(&g_s, XKB_KEY_Right, 0);

        /* Some rows are booleans already at one end, some are numbers at their
         * max; either way SOMETHING must have moved, or the row is inert. */
        if (memcmp(before_cfg, &g_s.config, sizeof(g_s.config)) == 0) {
            /* Try the other direction before calling it inert: a value pinned
             * at its maximum by default (blur_brightness is not, but a future
             * row could be) legitimately cannot go up. */
            ctlpanel_key(&g_s, XKB_KEY_Left, 0);
            if (memcmp(before_cfg, &g_s.config, sizeof(g_s.config)) == 0) {
                printf("    row %d (%s) did not move in either direction\n",
                       row, ctlpanel_row_label(row));
                assert(0);
            }
        }

        /* It is no longer at its default, so the panel must say so — this is
         * what drives the modified dot and what tells Delete there is work. */
        assert(!ctlpanel_row_is_default(&g_s, row));

        char live[64];
        ctlpanel_row_value(&g_s, row, live, sizeof(live));

        /* The whole point: throw the live config away and rebuild it from
         * synuirc + settings.state, exactly as the next login would. */
        syn_server_t fresh;
        memset(&fresh, 0, sizeof(fresh));
        synui_config_load(&fresh.config);
        fresh.ctlpanel = g_s.ctlpanel;

        char reloaded[64];
        ctlpanel_row_value(&fresh, row, reloaded, sizeof(reloaded));

        if (strcmp(live, reloaded) != 0) {
            printf("    row %d (%s): live '%s' but reloaded '%s'"
                   " — key wrong, offset wrong, or range past the parser's clamp\n",
                   row, ctlpanel_row_label(row), live, reloaded);
            assert(0);
        }

        /* And Delete puts it back, clearing the override so the reload agrees
         * again. */
        ctlpanel_key(&g_s, XKB_KEY_Delete, 0);
        assert(ctlpanel_row_is_default(&g_s, row));

        memset(&fresh, 0, sizeof(fresh));
        synui_config_load(&fresh.config);
        char after_reset[64];
        ctlpanel_row_value(&fresh, row, after_reset, sizeof(after_reset));
        if (strcmp(before_val, after_reset) != 0) {
            printf("    row %d (%s): reset gave '%s', default is '%s'\n",
                   row, ctlpanel_row_label(row), after_reset, before_val);
            assert(0);
        }

        checked++;
    }

    printf("  round trip per row ....... ok (%d checked, %d have no value)\n",
           checked, skipped);
}

/* ── 3b. EVERY option of every enum row round trips ──────────
 *
 * test_every_row_round_trips presses Right once. For an enum that walks one
 * notch off the default and stops — so an option further down the list can
 * spell itself in a way the parser does not accept and nothing notices.
 *
 * That is not hypothetical. ctl_persist() stores an enum by lower-casing the
 * name the panel DRAWS, so "Russian Blue" reached settings.state as
 * "russian blue" while config.c spelled that coat "russian-blue": choosing it
 * worked all session and was back to Neon at the next login. It was the eighth
 * option of nine, which is precisely why one notch of Right never saw it.
 *
 * So: step each enum row through every one of its options, and after each one
 * reload the config from disk and require the panel to still show the option
 * that was chosen. The failure prints the two spellings, because the fix is
 * always to make one of them match the other.
 */
static void test_every_enum_option_round_trips(void)
{
    int rows = 0, options = 0;

    for (int row = 0; row < CTL_ROW_COUNT; row++) {
        if (!select_row(row)) continue;
        if (!ctlpanel_row_key(row)) continue;
        if (ctlpanel_row_options(row) <= 0) continue;   /* not an enum */

        int n = ctlpanel_row_options(row);

        /* Right wraps at the end of an enum, so n presses visit every option
         * and land back where they started. */
        for (int i = 0; i < n; i++) {
            ctlpanel_key(&g_s, XKB_KEY_Right, 0);

            char live[64];
            ctlpanel_row_value(&g_s, row, live, sizeof(live));

            syn_server_t fresh;
            memset(&fresh, 0, sizeof(fresh));
            synui_config_load(&fresh.config);
            fresh.ctlpanel = g_s.ctlpanel;

            char reloaded[64];
            ctlpanel_row_value(&fresh, row, reloaded, sizeof(reloaded));

            if (strcmp(live, reloaded) != 0) {
                printf("    row %d (%s, key %s): chose '%s', reloaded as '%s'"
                       " — the option's name, lower-cased, is not a word"
                       " config_parse_kv() accepts\n",
                       row, ctlpanel_row_label(row), ctlpanel_row_key(row),
                       live, reloaded);
                assert(0);
            }
            options++;
        }

        /* Put it back, so the next row starts from a config at its defaults
         * exactly as the round-trip test above requires. */
        ctlpanel_key(&g_s, XKB_KEY_Delete, 0);
        assert(ctlpanel_row_is_default(&g_s, row));
        rows++;
    }

    printf("  every enum option ........ ok (%d options over %d rows)\n",
           options, rows);
}

/* ── 3c. The rung BELOW the range: "nobody has chosen" ───────
 *
 * A `.vauto` row has one position under its minimum meaning synui defers to
 * something else — for Bar opacity, to whatever the theme asked for. Three
 * things about it are not reachable from the walk above, which only presses
 * Right once:
 *
 *   * it is where the row STARTS, and it must draw as words. A row that came up
 *     reading "-1.00" would be a number nobody asked for in a range that does
 *     not contain it
 *   * Left off the bottom of the range has to LAND there, and not on the
 *     minimum. Without that the row is a one-way door: scroll down to 0.00 once
 *     and the theme's own answer is gone until you know to press Delete
 *   * and getting back there must clear the key rather than write it, or "no
 *     opinion" is stored as an opinion and the row is pinned against every
 *     future change to the default (see ctl_persist)
 *
 * Driven through CTL_ROW_BAR_OPACITY, the row that has one. The mechanism is
 * general and the test is not, deliberately: a table-walking version would have
 * to ask the table which rows have a rung, and every test in this file is
 * written to drive the panel and never to read what it says about itself.
 */
static void test_auto_rung(void)
{
    const int row = CTL_ROW_BAR_OPACITY;
    assert(select_row(row));

    char v[64];
    ctlpanel_row_value(&g_s, row, v, sizeof(v));
    assert(ctlpanel_row_is_default(&g_s, row));
    /* Words, not a number — and specifically not one below the row's minimum. */
    assert(strstr(v, "theme") != NULL);

    /* Left at the bottom is a no-op, not a wrap to the maximum. */
    ctlpanel_key(&g_s, XKB_KEY_Left, 0);
    assert(ctlpanel_row_is_default(&g_s, row));

    /* In: the first real value, not the second. A rung that stepped to
     * vmin + vstep would make 0.00 — the clear bar, the whole point of the
     * row — reachable only by going up and coming back down. */
    ctlpanel_key(&g_s, XKB_KEY_Right, 0);
    ctlpanel_row_value(&g_s, row, v, sizeof(v));
    assert(strcmp(v, "0.00") == 0);
    assert(!ctlpanel_row_is_default(&g_s, row));

    /* Out again, by the arrow rather than by Delete: Delete resets every row and
     * would prove nothing about this one's bottom end. */
    ctlpanel_key(&g_s, XKB_KEY_Left, 0);
    assert(ctlpanel_row_is_default(&g_s, row));

    /* And the file agrees — the key is gone, not written as some spelling of
     * "auto" that config_parse_kv would have to be trusted to read back. */
    syn_server_t fresh;
    memset(&fresh, 0, sizeof(fresh));
    synui_config_load(&fresh.config);
    fresh.ctlpanel = g_s.ctlpanel;
    char reloaded[64];
    ctlpanel_row_value(&fresh, row, reloaded, sizeof(reloaded));
    assert(strstr(reloaded, "theme") != NULL);

    /* The other spelling of the same instruction: `auto` written by hand into
     * synuirc. It has to parse back to the rung, or a config file that says what
     * the panel writes would move the row off its default. */
    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    synui_config_load(&cfg);
    char word[8];
    snprintf(word, sizeof(word), "auto");
    config_parse_kv(&cfg, "bar_opacity", word);
    assert(cfg.bar_opacity < 0.0f);

    printf("  auto rung ................ ok\n");
}

/* ── 4. Search reaches rows by label and by synuirc key ────── */

static void test_search(void)
{
    g_s.ctlpanel.visible = 1;
    g_s.ctlpanel.focus   = CTL_FOCUS_ITEMS;

    int rows[CTL_CAT_ITEMS_MAX];

    /* Not searching: one category only. */
    g_s.ctlpanel.searching = 0;
    g_s.ctlpanel.cat = CTL_CAT_INPUT;
    int cat_n = ctlpanel_visible_rows(&g_s, rows, CTL_CAT_ITEMS_MAX);
    assert(cat_n > 0);

    /* By label. */
    g_s.ctlpanel.searching = 1;
    snprintf(g_s.ctlpanel.search, sizeof(g_s.ctlpanel.search), "blur");
    g_s.ctlpanel.search_len = 4;
    int n = ctlpanel_visible_rows(&g_s, rows, CTL_CAT_ITEMS_MAX);
    assert(n >= 5);   /* the blur family */

    /* By synuirc key — the spelling someone arrives with from the config file,
     * which is not the spelling on the row. */
    snprintf(g_s.ctlpanel.search, sizeof(g_s.ctlpanel.search), "alt_tab_minimized");
    g_s.ctlpanel.search_len = 17;
    n = ctlpanel_visible_rows(&g_s, rows, CTL_CAT_ITEMS_MAX);
    assert(n == 1);
    assert(rows[0] == CTL_ROW_ALT_TAB_MINIMIZED);

    /* Case-insensitively. */
    snprintf(g_s.ctlpanel.search, sizeof(g_s.ctlpanel.search), "ALT_TAB_MINIMIZED");
    n = ctlpanel_visible_rows(&g_s, rows, CTL_CAT_ITEMS_MAX);
    assert(n == 1);

    /* A miss is empty, not everything. */
    snprintf(g_s.ctlpanel.search, sizeof(g_s.ctlpanel.search), "zzzznope");
    n = ctlpanel_visible_rows(&g_s, rows, CTL_CAT_ITEMS_MAX);
    assert(n == 0);

    /* Results never include the read-only shortcuts list. */
    snprintf(g_s.ctlpanel.search, sizeof(g_s.ctlpanel.search), "");
    g_s.ctlpanel.search_len = 0;
    n = ctlpanel_visible_rows(&g_s, rows, CTL_CAT_ITEMS_MAX);
    for (int i = 0; i < n; i++)
        assert(ctlpanel_row_cat(rows[i]) != CTL_CAT_SHORTCUTS);

    g_s.ctlpanel.searching = 0;
    printf("  search ................... ok\n");
}

/* ── 5. The right apply hook runs ────────────────────────────
 *
 * A value that stores but never reaches the screen is the classic settings-panel
 * bug, and the table's `apply` column is the only thing preventing it. Spot-check
 * that the column is wired, per hook.
 */
static void test_apply_hooks(void)
{
    struct { int row; int *counter; const char *name; } cases[] = {
        { CTL_ROW_BORDER_WIDTH,  &applied_deco,       "deco"       },
        { CTL_ROW_GAP,           &applied_layout,     "layout"     },
        { CTL_ROW_BLUR_RADIUS,   &applied_uifx,       "uifx"       },
        { CTL_ROW_SHADOW_SIGMA,  &applied_uifx,       "uifx"       },
        { CTL_ROW_REPEAT_RATE,   &applied_input,      "input"      },
        { CTL_ROW_CURSOR_SIZE,   &applied_cursor,     "cursor"     },
        { CTL_ROW_DOCK_HEIGHT,   &applied_dock,       "dock"       },
        { CTL_ROW_NIGHTLIGHT_TEMP, &applied_nightlight, "nightlight" },
        { CTL_ROW_DESKTOP_ICONS, &applied_deskicons,  "deskicons"  },
        /* Bar edge was APPLY_NONE — the bar watches settings.state and moves
         * itself. It still does; what the compositor has to do now is repaint
         * the wallpaper, because a clear bar takes its ink from the strip it
         * covers and moving the bar moves which strip that is. Pinned here
         * because the failure is silent: the bar moves either way, and only the
         * one theme that draws a clear bar shows the stale answer. */
        { CTL_ROW_BAR_EDGE,      &applied_wallpaper,  "wallpaper"  },
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        assert(select_row(cases[i].row));
        int before = *cases[i].counter;
        ctlpanel_key(&g_s, XKB_KEY_Right, 0);
        if (*cases[i].counter == before) {
            printf("    row %d (%s) changed without running %s\n",
                   cases[i].row, ctlpanel_row_label(cases[i].row), cases[i].name);
            assert(0);
        }
        ctlpanel_key(&g_s, XKB_KEY_Delete, 0);
    }

    printf("  apply hooks .............. ok\n");
}

/*
 * The bind primitives the rebind helper is built on.
 *
 * The load-bearing property is that syn_bind_format_combo() writes a combo
 * syn_bind_parse_combo() reads back to the SAME chord. binds.state is generated
 * by the first and consumed — via config_parse_kv, via synuirc's own grammar —
 * by the second, so a disagreement between them is a shortcut that works for
 * the rest of the session and is silently gone at the next login. That is the
 * failure that is hardest to attribute, because the file on disk looks right.
 *
 * The chords here are chosen to cover what actually trips a formatter: every
 * modifier at once, a bare function key with no modifier at all, a key xkb
 * spells in mixed case ("Escape", "Print"), and the two whose synuirc spelling
 * is not what the keycap says ("equal", "space").
 */
static void test_bind_combo_round_trip(void)
{
    static const struct { uint32_t mods; xkb_keysym_t sym; const char *want; } cases[] = {
        { WLR_MODIFIER_LOGO, XKB_KEY_w, "super+w" },
        { WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT, XKB_KEY_q, "super+shift+q" },
        { WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_Delete, "ctrl+alt+delete" },
        { WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT,
          XKB_KEY_k, "super+ctrl+alt+shift+k" },
        { 0, XKB_KEY_Print, "print" },
        { 0, XKB_KEY_XF86AudioMute, "xf86audiomute" },
        { WLR_MODIFIER_LOGO, XKB_KEY_equal, "super+equal" },
        { WLR_MODIFIER_LOGO, XKB_KEY_space, "super+space" },
        { WLR_MODIFIER_LOGO, XKB_KEY_Escape, "super+escape" },
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char out[64];
        syn_bind_format_combo(cases[i].mods, cases[i].sym, out, sizeof(out));
        if (strcmp(out, cases[i].want) != 0) {
            printf("    formatted '%s', expected '%s'\n", out, cases[i].want);
            assert(0);
        }

        uint32_t mods = 0xffffffffu;
        xkb_keysym_t sym = XKB_KEY_NoSymbol;
        if (!syn_bind_parse_combo(out, &mods, &sym)) {
            printf("    '%s' formatted but did not parse back\n", out);
            assert(0);
        }
        if (mods != cases[i].mods || sym != cases[i].sym) {
            printf("    '%s' round-tripped to a different chord\n", out);
            assert(0);
        }
    }

    /* A combo with no key part at all is a config line to reject, not a bind on
     * NoSymbol — which would match a keypress that never happens and sit in the
     * table looking like it worked. */
    assert(!syn_bind_parse_combo("super+shift", NULL, NULL));
    assert(!syn_bind_parse_combo("super+notakey", NULL, NULL));

    printf("  bind combo round-trip .... ok\n");
}

/*
 * Moving a shortcut is TWO operations, and this is the one that is easy to
 * forget. Binding the new chord leaves the old one bound as well — the defaults
 * are seeded before any file is read — so without the unbind the shortcut
 * answers to both keys, which reads as a rebind that only half applied.
 */
static void test_bind_move(void)
{
    syn_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    config_bind_set(&cfg, WLR_MODIFIER_LOGO, XKB_KEY_w, "wallpaper", "");
    config_bind_set(&cfg, WLR_MODIFIER_LOGO, XKB_KEY_p, "power", "");
    assert(cfg.bind_count == 2);

    /* Same chord again replaces rather than appends: a synuirc line overriding
     * a default must not leave the default underneath, where handle_keybinding
     * takes the first match and would still find it. */
    config_bind_set(&cfg, WLR_MODIFIER_LOGO, XKB_KEY_w, "news", "");
    assert(cfg.bind_count == 2);
    assert(strcmp(cfg.binds[0].action, "news") == 0);

    /* The move. */
    config_bind_set(&cfg, WLR_MODIFIER_LOGO, XKB_KEY_y, "news", "");
    assert(config_unbind_combo(&cfg, WLR_MODIFIER_LOGO, XKB_KEY_w));
    assert(cfg.bind_count == 2);

    /* Order survives the removal: the shortcuts column and the palette list in
     * table order, so an unbind that swapped the last entry into the hole would
     * reshuffle the list under whoever was reading it. */
    assert(strcmp(cfg.binds[0].action, "power") == 0);
    assert(strcmp(cfg.binds[1].action, "news") == 0);

    /* And the old chord is genuinely gone. */
    assert(!config_unbind_combo(&cfg, WLR_MODIFIER_LOGO, XKB_KEY_w));

    printf("  bind move ................ ok\n");
}

int main(void)
{
    /* Unbuffered: every failure here prints WHICH row and why, immediately
     * before assert() aborts, and abort() does not flush stdio. Buffered, the
     * one line that says what went wrong is the one line you never see. */
    setvbuf(stdout, NULL, _IONBF, 0);

    rig_init();

    printf("ctlpanel_table_test\n");
    test_table_covers_every_row();
    test_no_category_overflows();
    test_every_category_named();
    test_every_row_round_trips();
    test_every_enum_option_round_trips();
    test_auto_rung();
    test_search();
    test_apply_hooks();
    test_bind_combo_round_trip();
    test_bind_move();

    rig_cleanup();
    printf("ctlpanel_table_test: all ok\n");
    return 0;
}
