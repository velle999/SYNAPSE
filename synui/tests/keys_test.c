/*
 * keys_test.c — the shortcut palette (Super+/), driven by keysym.
 *
 * The palette's whole value is that it is not a second list of shortcuts: it
 * reads the live bind table through ctlpanel_shortcuts(), so a bind added in
 * config.c appears in it with no second edit. That claim is invisible to the
 * compiler and is exactly the kind of thing that rots, so it is pinned here by
 * seeding a bind and looking for it — never by reading the palette's own table
 * back out of itself.
 *
 * The rest is the search box, which is the one part of this panel with logic in
 * it worth breaking:
 *
 *   - Typing filters on EITHER column, so "super+w" and "wallpaper" both find
 *     the wallpaper picker. The two-column search is the point — a list you can
 *     only query by key is a list you have to already know the key for.
 *   - Multiple words are ANDed and order-independent, or "float super" would be
 *     a query that silently finds nothing.
 *   - Enter runs the SELECTED row through synui_binding_execute — the same path
 *     a keypress takes — and the panel is down BEFORE it does. Half these
 *     actions open a modal panel, and running one with the palette still up
 *     leaves two panels stacked with the wrong one swallowing the keys.
 *   - A row with no action behind it (the collapsed "Super+1–9") must dispatch
 *     NOTHING. It is the one row where Enter has nothing honest to do.
 *   - Escape backs out of the query before it closes the panel, and Backspace
 *     on an empty query does not close it at all.
 *   - Super and Alt combos fall through (answer 0), or Super+/ could not close
 *     the panel it opened and Super+C would be unreachable from it.
 *
 * Driven by calling keys_key() with keysyms exactly as input.c's chain does,
 * for the reason panel_pointer_test.c gives at length: nothing can synthesise
 * input into a headless synui, and uinput would be picked up by the LIVE
 * session.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon.h>

#include "synui.h"

static int failures;
static int checks;

#define CHECK(cond, ...) do {                                    \
        checks++;                                                \
        if (cond) {                                              \
            printf("  ok   \xe2\x80\x94 ");                      \
            printf(__VA_ARGS__); printf("\n");                   \
        } else {                                                 \
            failures++;                                          \
            printf("  FAIL \xe2\x80\x94 ");                      \
            printf(__VA_ARGS__); printf("\n");                   \
            fprintf(stderr, "FAIL %s:%d\n", __FILE__, __LINE__); \
        }                                                        \
    } while (0)

/* ── The compositor, stubbed ─────────────────────────────────
 *
 * keys.c and ctlpanel.c are linked alone. Only the two the tests assert on
 * record anything: the render count (proof the panel repaints on an edit) and
 * the dispatched action.
 */
static int  renders;
static int  dispatches;
static char last_action[64];
static char last_arg[128];
/* Was the panel already down when the action ran? The ordering is the bug this
 * records, so it is captured inside the dispatch rather than checked after. */
static int  hidden_before_dispatch;

static syn_server_t g_s;

void synui_render_keys(syn_server_t *s)      { (void)s; renders++; }
void synui_render_ctlpanel(syn_server_t *s)  { (void)s; }
void synui_render_aimodel(syn_server_t *s)   { (void)s; }
void synmon_want_refresh(syn_server_t *s)    { (void)s; }
void synui_child_reset_signals(void)         { }

/* The Bar row shells out to stop or start the bar — it is a separate
 * process, so the row cannot just flip a flag the way the Dock row does. */
void synui_spawn(const char *cmd)             { (void)cmd; }

/* power.c is not linked here; the Screen audio row resolves `auto` through it.
 * "no battery" is the desktop answer and keeps the row's value deterministic. */
bool power_has_battery(void)                 { return false; }


/* dispcfg.c is not linked here. The Screens row in ctlpanel.c calls into it to
 * change the arrangement; what is under test is the row, not the re-flow. */
void dispcfg_set_mode_cfg(syn_server_t *s, int mode) { (void)s; (void)mode; }

/* dispcfg.c is not linked here; config.c parses `display_mode` through it.
 * Stubbed the same way lid_action_from_name() is, and for the same reason. */
int  display_mode_from_name(const char *n)   { (void)n; return -1; }


/* fontpick.c is not linked here. The two font.state rows in ctlpanel.c reach
 * it for the values they display and for the apply, so the panel needs these
 * three to link. Fixed answers rather than a file read: what is under test is
 * the TABLE, not the state file. */
void fontpick_state_read(int *size, int *scale)
{
    if (size)  *size  = 10;
    if (scale) *scale = 100;
}
void fontpick_push_size(syn_server_t *s, int size)   { (void)s; (void)size; }
void fontpick_push_scale(syn_server_t *s, int scale) { (void)s; (void)scale; }


/* panel.c dispatches a repaint to whichever panel is being dragged, which pulls
 * in the other two panels' renderers. Neither is exercised here. */
void synui_render_calc(syn_server_t *s)    { (void)s; }
void synui_render_taskmgr(syn_server_t *s) { (void)s; }

bool synui_binding_execute(syn_server_t *s, const char *action, const char *arg)
{
    (void)s;
    dispatches++;
    hidden_before_dispatch = !g_s.keys.visible;
    snprintf(last_action, sizeof(last_action), "%s", action ? action : "");
    snprintf(last_arg,    sizeof(last_arg),    "%s", arg    ? arg    : "");
    return true;
}

int synmon_send_reload(const char *m, char *o, size_t n)
{ (void)m; (void)o; (void)n; return 0; }

/* Everything else ctlpanel.c reaches for. None of it runs here. */
void uifx_apply(syn_server_t *s)                     { (void)s; }
/* The two the Glass rows reach for. Neither is exercised here — what the sync
 * resolves is ctlpanel_table_test's and backdrop_test's — but both have to
 * resolve for this to link. */
void theme_glass_refresh(syn_server_t *s)            { (void)s; }
void synui_config_apply_glass_sync(syn_config_t *c)  { (void)c; }
void synui_config_glass_release(syn_config_t *c)     { (void)c; }
void input_reload_config(syn_server_t *s)            { (void)s; }
void deco_refresh_all(syn_server_t *s)               { (void)s; }
void deco_toggle_titlebars(syn_server_t *s)          { (void)s; }
void dock_rebuild(syn_server_t *s)                   { (void)s; }
void dock_relayout(syn_server_t *s)                  { (void)s; }
void dock_state_save(syn_server_t *s)                { (void)s; }
void dock_wake(syn_server_t *s)                      { (void)s; }
void game_toggle(syn_server_t *s)                    { (void)s; }
void launcher_toggle_style(syn_server_t *s)          { (void)s; }
void nightlight_apply(syn_server_t *s)               { (void)s; }
void notif_dnd_toggle(syn_server_t *s)               { (void)s; }
void nightlight_toggle(syn_server_t *s)              { (void)s; }
void cursor_reload(syn_server_t *s)                  { (void)s; }
void deskicons_reload(syn_server_t *s)               { (void)s; }
void wallpaper_relayout(syn_server_t *s)             { (void)s; }
void record_audio_toggle(syn_server_t *s)            { (void)s; }
void record_edit_toggle(syn_server_t *s)             { (void)s; }
void sound_state_refresh(syn_server_t *s)            { (void)s; }
void transparency_set_enabled(syn_server_t *s, int on)  { (void)s; (void)on; }
void transparency_set_opacity(syn_server_t *s, float o) { (void)s; (void)o; }
void layout_apply(syn_server_t *s, syn_workspace_t *ws) { (void)s; (void)ws; }
void synui_config_apply_launcher_binds(syn_config_t *c) { (void)c; }
const char *layout_label(syn_layout_t l)             { (void)l; return "stack"; }
const char *theme_name(syn_theme_t t)                { (void)t; return "synapse"; }
syn_workspace_t *server_active_workspace(syn_server_t *s) { (void)s; return NULL; }
bool syn_config_path(char *buf, size_t n, const char *leaf)
{ (void)buf; (void)n; (void)leaf; return false; }

void settings_state_set(const char *k, const char *v) { (void)k; (void)v; }
void settings_state_clear(const char *k)              { (void)k; }
/* The control panel's CRT and window-effect rows are stored in the Super+E
 * panels' own state files rather than settings.state, so ctl_persist() reaches
 * these two. Stubbed: a test must not write the developer's ~/.config. */
void filters_state_save(syn_server_t *s) { (void)s; }
void uifx_state_save(syn_server_t *s)    { (void)s; }
int  settings_state_has(const char *k)                { (void)k; return 0; }

/* ── The bind primitives, RECORDED rather than stubbed ───────
 *
 * config.c stays unlinked (see the rig note below), so these stand in for it —
 * but the rebind tests are about WHICH calls keys.c makes and in what order, so
 * a no-op would make every one of them vacuous. They keep a little table
 * instead, which is enough for keys.c's own logic (find the chord, refuse a
 * conflict, bind the new one, unbind the old) to be a real assertion.
 *
 * The formatter is the real one's contract in miniature: lower case, modifiers
 * in synuirc's order. keys.c only ever puts its output in front of the user, so
 * this does not have to be xkb-exact — the round trip that DOES have to be
 * exact is asserted against the real pair in ctlpanel_table_test.
 */
static int  binds_set, binds_unbound;
static char last_bound[64], last_unbound[64];

void syn_bind_format_combo(uint32_t mods, xkb_keysym_t sym, char *out, size_t n)
{
    char key[32];
    if (xkb_keysym_get_name(sym, key, sizeof(key)) <= 0)
        snprintf(key, sizeof(key), "?");
    for (char *p = key; *p; p++) *p = (char)tolower((unsigned char)*p);
    snprintf(out, n, "%s%s%s%s%s",
             (mods & WLR_MODIFIER_LOGO)  ? "super+" : "",
             (mods & WLR_MODIFIER_CTRL)  ? "ctrl+"  : "",
             (mods & WLR_MODIFIER_ALT)   ? "alt+"   : "",
             (mods & WLR_MODIFIER_SHIFT) ? "shift+" : "", key);
}

void config_bind_set(syn_config_t *cfg, uint32_t mods, xkb_keysym_t sym,
                     const char *action, const char *arg)
{
    for (int i = 0; i < cfg->bind_count; i++) {
        if (cfg->binds[i].mods != mods || cfg->binds[i].sym != sym) continue;
        snprintf(cfg->binds[i].action, sizeof(cfg->binds[i].action), "%s", action);
        snprintf(cfg->binds[i].arg, sizeof(cfg->binds[i].arg), "%s", arg ? arg : "");
        binds_set++;
        return;
    }
    syn_bind_t *b = &cfg->binds[cfg->bind_count++];
    b->mods = mods; b->sym = sym;
    snprintf(b->action, sizeof(b->action), "%s", action);
    snprintf(b->arg, sizeof(b->arg), "%s", arg ? arg : "");
    binds_set++;
    syn_bind_format_combo(mods, sym, last_bound, sizeof(last_bound));
}

bool config_unbind_combo(syn_config_t *cfg, uint32_t mods, xkb_keysym_t sym)
{
    for (int i = 0; i < cfg->bind_count; i++) {
        if (cfg->binds[i].mods != mods || cfg->binds[i].sym != sym) continue;
        memmove(&cfg->binds[i], &cfg->binds[i + 1],
                (size_t)(cfg->bind_count - i - 1) * sizeof(cfg->binds[0]));
        cfg->bind_count--;
        binds_unbound++;
        syn_bind_format_combo(mods, sym, last_unbound, sizeof(last_unbound));
        return true;
    }
    return false;
}

bool syn_bind_parse_combo(const char *c, uint32_t *m, xkb_keysym_t *s)
{ (void)c; (void)m; (void)s; return false; }

/* The tap key's mask↔name pair, config.c's again. Real rather than stubbed for
 * the same reason as the formatter above: the tap rebind tests are about which
 * modifier a keysym resolves to, and a stub returning 0 would make every one of
 * them assert the refusal instead. Four modifiers is the whole of it. */
uint32_t syn_tap_mod_from_sym(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Super_L:   case XKB_KEY_Super_R:   return WLR_MODIFIER_LOGO;
    case XKB_KEY_Control_L: case XKB_KEY_Control_R: return WLR_MODIFIER_CTRL;
    case XKB_KEY_Alt_L:     case XKB_KEY_Alt_R:     return WLR_MODIFIER_ALT;
    case XKB_KEY_Shift_L:   case XKB_KEY_Shift_R:   return WLR_MODIFIER_SHIFT;
    default:                                        return 0;
    }
}

const char *syn_tap_mod_name(uint32_t mod)
{
    switch (mod) {
    case WLR_MODIFIER_LOGO:  return "super";
    case WLR_MODIFIER_CTRL:  return "ctrl";
    case WLR_MODIFIER_ALT:   return "alt";
    case WLR_MODIFIER_SHIFT: return "shift";
    default:                 return "none";
    }
}
void syn_config_ensure_dir(void) { }
void config_parse_kv(syn_config_t *c, const char *k, char *v)
{ (void)c; (void)k; (void)v; }

/* Reset-all reloads rather than un-applying its own diff, so this is the whole
 * of it as far as keys.c is concerned. */
static int reloads;
void synui_config_reload(syn_server_t *s) { (void)s; reloads++; }

const syn_config_t *synui_config_defaults(void)
{
    static syn_config_t def;
    return &def;
}

/* ── Rig ─────────────────────────────────────────────────────
 *
 * config.c is deliberately NOT linked: seeding the bind table by hand is what
 * makes "the palette lists whatever is in the table" a real assertion rather
 * than a restatement of the defaults. The combos below are the shipped ones, so
 * the strings the palette builds are the strings a user sees.
 */
static void bind_add(uint32_t mods, xkb_keysym_t sym,
                     const char *action, const char *arg)
{
    syn_bind_t *b = &g_s.config.binds[g_s.config.bind_count++];
    b->mods = mods;
    b->sym  = sym;
    snprintf(b->action, sizeof(b->action), "%s", action);
    snprintf(b->arg,    sizeof(b->arg),    "%s", arg ? arg : "");
}

static void rig_init(void)
{
    memset(&g_s, 0, sizeof(g_s));

    /* The shipped default, seeded by hand like the binds: a zeroed config would
     * mean "no tap at all", and the tap row would come up as "Off" in every
     * test that never mentions it. */
    g_s.config.tap_mod = WLR_MODIFIER_LOGO;
    /* And what it opens, for the same reason — an empty tap_action would make
     * the row's description come out of action_desc("") in every test. */
    snprintf(g_s.config.tap_action, sizeof(g_s.config.tap_action), "start_menu");
    g_s.config.tap_arg[0] = '\0';

    bind_add(WLR_MODIFIER_LOGO, XKB_KEY_w,     "wallpaper", "");
    bind_add(WLR_MODIFIER_LOGO, XKB_KEY_f,     "float_toggle", "");
    bind_add(WLR_MODIFIER_LOGO, XKB_KEY_c,     "control", "");
    bind_add(WLR_MODIFIER_LOGO, XKB_KEY_slash, "keys", "");
    bind_add(WLR_MODIFIER_LOGO, XKB_KEY_o,     "move_output", "prev");
    bind_add(WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT, XKB_KEY_s,
             "spawn", "synui-screenshot region");
    /* One of the nine. ctlpanel_shortcuts() collapses these into a single
     * actionless "Super+1–9" row, which is the row Enter must refuse. */
    bind_add(WLR_MODIFIER_LOGO, XKB_KEY_1,     "ws", "1");

    renders = dispatches = 0;
    last_action[0] = last_arg[0] = '\0';
    hidden_before_dispatch = -1;
}

/* Type a string into the open palette, a keysym at a time — printable ASCII is
 * its own keysym, which is exactly what input.c hands keys_key(). */
static void type(const char *text)
{
    for (const char *p = text; *p; p++)
        keys_key(&g_s, (xkb_keysym_t)(unsigned char)*p, 0);
}

/* The description on the row the cursor is on, or "" when nothing matched. */
static const char *selected_desc(void)
{
    syn_keys_t *k = &g_s.keys;
    if (k->selected < 0 || k->selected >= k->n_view) return "";
    return k->all[k->view[k->selected]].desc;
}

/* The combo column of the row the cursor is on — the string the user reads as
 * "this shortcut's key". The tap tests assert on it because it is the only
 * place the tap's modifier is visible. */
static const char *selected_combo(void)
{
    syn_keys_t *k = &g_s.keys;
    if (k->selected < 0 || k->selected >= k->n_view) return "";
    return k->all[k->view[k->selected]].combo;
}

static int view_count(const char *desc)
{
    syn_keys_t *k = &g_s.keys;
    int n = 0;
    for (int i = 0; i < k->n_view; i++)
        if (strcmp(k->all[k->view[i]].desc, desc) == 0) n++;
    return n;
}

static int view_has(const char *desc)
{
    return view_count(desc) > 0;
}

/* ── The list comes from the bind table ──────────────────── */

static void test_list_is_the_bind_table(void)
{
    printf("keys: the list is the bind table\n");

    rig_init();
    keys_show(&g_s);

    /* Seven binds, less the eight collapsed into "Super+1–9" (one of which was
     * seeded), plus the tap row and the collapsed row itself. */
    CHECK(g_s.keys.n == 8, "every bind is listed, workspaces collapsed (got %d)",
          g_s.keys.n);
    CHECK(g_s.keys.n_view == g_s.keys.n,
          "an empty query shows all of them (got %d)", g_s.keys.n_view);

    CHECK(view_has("Wallpaper picker"), "a bind's description comes from its action");
    CHECK(view_has("Keyboard shortcuts (this list)"), "the palette lists itself");
    CHECK(view_has("Switch to workspace"), "the collapsed workspace row is there");
    CHECK(view_has("Start menu"), "the tap is listed though it is not a bind");
    CHECK(view_has("Move window to previous output"),
          "an arg that changes the meaning changes the description");
    CHECK(view_has("synui-screenshot region"),
          "a spawn bind is listed as the thing it spawns");

    /* A bind nothing knew about when this test was written must still list —
     * the fallback is what keeps a new action from going missing entirely. */
    rig_init();
    bind_add(WLR_MODIFIER_LOGO, XKB_KEY_z, "some_new_action", "");
    keys_show(&g_s);
    CHECK(view_has("some_new_action"),
          "an action with no description falls back to its name");

    keys_hide(&g_s);
}

/* ── The search box ──────────────────────────────────────── */

static void test_search(void)
{
    printf("keys: typing filters both columns\n");

    rig_init();
    keys_show(&g_s);
    int base = renders;

    type("wallp");
    CHECK(renders > base, "an edit repaints the panel");
    CHECK(g_s.keys.n_view == 1 && view_has("Wallpaper picker"),
          "a description matches (got %d rows)", g_s.keys.n_view);

    /* Same row, found by its key instead. */
    keys_key(&g_s, XKB_KEY_Escape, 0);
    CHECK(g_s.keys.query_len == 0 && g_s.keys.n_view == g_s.keys.n,
          "Escape clears the query before it closes anything");
    CHECK(g_s.keys.visible, "…and the panel is still up");

    type("super+w");
    CHECK(g_s.keys.n_view == 1 && view_has("Wallpaper picker"),
          "a combo matches (got %d rows)", g_s.keys.n_view);

    /* Case folds. */
    keys_key(&g_s, XKB_KEY_Escape, 0);
    type("SUPER+W");
    CHECK(g_s.keys.n_view == 1 && view_has("Wallpaper picker"),
          "the match is case-insensitive");

    /* Words are ANDed, in any order. "float super" is the case that a naive
     * substring search over the concatenation would silently miss. */
    keys_key(&g_s, XKB_KEY_Escape, 0);
    type("float super");
    CHECK(g_s.keys.n_view == 1 && view_has("Float window"),
          "words are ANDed across both columns (got %d rows)", g_s.keys.n_view);

    keys_key(&g_s, XKB_KEY_Escape, 0);
    type("super float");
    CHECK(g_s.keys.n_view == 1 && view_has("Float window"),
          "…and the order does not matter");

    keys_key(&g_s, XKB_KEY_Escape, 0);
    type("zzzz");
    CHECK(g_s.keys.n_view == 0, "a query that matches nothing shows nothing");
    CHECK(g_s.keys.visible, "…and does not close the panel");

    /* Backspace walks it back rather than closing. This is deliberate and
     * differs from the control panel's search box; see keys.c. */
    for (int i = 0; i < 4; i++) keys_key(&g_s, XKB_KEY_BackSpace, 0);
    CHECK(g_s.keys.query_len == 0 && g_s.keys.n_view == g_s.keys.n,
          "backspacing the query out restores the list");
    keys_key(&g_s, XKB_KEY_BackSpace, 0);
    CHECK(g_s.keys.visible, "…and one backspace too many does NOT close it");

    keys_key(&g_s, XKB_KEY_Escape, 0);
    CHECK(!g_s.keys.visible, "Escape on an empty query closes it");
}

/* ── Enter runs the shortcut ─────────────────────────────── */

static void test_enter_runs_it(void)
{
    printf("keys: Enter runs the selected shortcut\n");

    rig_init();
    keys_show(&g_s);

    type("wallp");
    CHECK(strcmp(selected_desc(), "Wallpaper picker") == 0,
          "the cursor lands on the first result");

    keys_key(&g_s, XKB_KEY_Return, 0);
    CHECK(dispatches == 1, "exactly one action ran (got %d)", dispatches);
    CHECK(strcmp(last_action, "wallpaper") == 0,
          "…and it is the row's action (got \"%s\")", last_action);
    CHECK(!g_s.keys.visible, "the palette closed");
    CHECK(hidden_before_dispatch == 1,
          "…BEFORE the action ran, or it would sit on top of what it opened");

    /* The arg travels with the action, or "previous output" would move the
     * window the other way. */
    rig_init();
    keys_show(&g_s);
    type("previous output");
    keys_key(&g_s, XKB_KEY_Return, 0);
    CHECK(strcmp(last_action, "move_output") == 0 &&
          strcmp(last_arg, "prev") == 0,
          "the argument goes with it (got \"%s %s\")", last_action, last_arg);

    /* The collapsed workspace row names nine binds and no single one of them. */
    rig_init();
    keys_show(&g_s);
    type("switch to workspace");
    CHECK(g_s.keys.n_view == 1, "the collapsed row is selectable");
    keys_key(&g_s, XKB_KEY_Return, 0);
    CHECK(dispatches == 0, "…but Enter on it dispatches nothing (got %d)",
          dispatches);
    CHECK(g_s.keys.visible, "…and leaves the palette up");

    /* Nothing matched: Enter has no row at all. */
    rig_init();
    keys_show(&g_s);
    type("zzzz");
    keys_key(&g_s, XKB_KEY_Return, 0);
    CHECK(dispatches == 0, "Enter on an empty result does nothing");

    keys_hide(&g_s);
}

/* ── Moving the cursor ───────────────────────────────────── */

static void test_cursor(void)
{
    printf("keys: moving the cursor\n");

    rig_init();
    keys_show(&g_s);

    CHECK(g_s.keys.selected == 0, "the cursor starts at the top");

    keys_key(&g_s, XKB_KEY_Up, 0);
    CHECK(g_s.keys.selected == 0, "Up at the top stays put");

    keys_key(&g_s, XKB_KEY_Down, 0);
    CHECK(g_s.keys.selected == 1, "Down steps one row");

    /* Ctrl+N/P exist because j/k have to type a j and a k. */
    keys_key(&g_s, XKB_KEY_n, WLR_MODIFIER_CTRL);
    CHECK(g_s.keys.selected == 2, "Ctrl+N steps down");
    keys_key(&g_s, XKB_KEY_p, WLR_MODIFIER_CTRL);
    CHECK(g_s.keys.selected == 1, "Ctrl+P steps up");

    keys_key(&g_s, XKB_KEY_End, 0);
    CHECK(g_s.keys.selected == g_s.keys.n_view - 1, "End goes to the last row");
    keys_key(&g_s, XKB_KEY_Down, 0);
    CHECK(g_s.keys.selected == g_s.keys.n_view - 1, "Down at the end stays put");
    keys_key(&g_s, XKB_KEY_Home, 0);
    CHECK(g_s.keys.selected == 0, "Home goes back to the top");

    /* An edit resets the cursor: after a keystroke the row it was on is a
     * different shortcut, and running "whatever is now at index 3" is the one
     * way this panel could do something you did not ask for. */
    keys_key(&g_s, XKB_KEY_End, 0);
    type("s");
    CHECK(g_s.keys.selected == 0, "an edit puts the cursor back at the top");

    keys_hide(&g_s);
}

/* ── The modal contract ──────────────────────────────────── */

static void test_modal_contract(void)
{
    printf("keys: what the palette does and does not claim\n");

    rig_init();
    CHECK(keys_key(&g_s, XKB_KEY_a, 0) == 0,
          "a shut palette claims nothing, so the chain falls through");

    keys_show(&g_s);
    CHECK(keys_key(&g_s, XKB_KEY_q, 0) == 1 && g_s.keys.visible,
          "q types a q rather than closing, unlike every other panel");
    CHECK(g_s.keys.query_len == 1 && g_s.keys.query[0] == 'q',
          "…and it is in the box");

    /* The compositor's own chords have to survive, or Super+/ could not close
     * the panel it opened. */
    CHECK(keys_key(&g_s, XKB_KEY_slash, WLR_MODIFIER_LOGO) == 0,
          "Super+/ falls through to the bind table");
    CHECK(keys_key(&g_s, XKB_KEY_c, WLR_MODIFIER_LOGO) == 0,
          "Super+C falls through too");
    CHECK(keys_key(&g_s, XKB_KEY_question,
                   WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT) == 0,
          "so does Super+? — Shift is claimed, but not with Super held");
    CHECK(keys_key(&g_s, XKB_KEY_Tab, WLR_MODIFIER_ALT) == 0,
          "Alt+Tab falls through");

    /* Shift alone belongs to the box: the query types capitals. */
    CHECK(keys_key(&g_s, XKB_KEY_W, WLR_MODIFIER_SHIFT) == 1 &&
          g_s.keys.query_len == 2 && g_s.keys.query[1] == 'W',
          "Shift+W types a capital W");

    /* A key with nothing to do is still swallowed — the palette is modal, and a
     * stray F5 must not reach the window underneath it. */
    CHECK(keys_key(&g_s, XKB_KEY_F5, 0) == 1, "an unhandled key is swallowed");

    keys_hide(&g_s);
    CHECK(keys_motion(&g_s, 10, 10) == 0 &&
          keys_click(&g_s, 10, 10, BTN_LEFT, 0) == 0 &&
          keys_scroll(&g_s, 10, 10, 1) == 0,
          "and the pointer handlers fall through when it is shut");
}

/* ── Rebinding ───────────────────────────────────────────── */

/* Move the cursor onto the row whose description is `desc`. */
static int select_desc(const char *desc)
{
    syn_keys_t *k = &g_s.keys;
    for (int i = 0; i < k->n_view; i++)
        if (strcmp(k->all[k->view[i]].desc, desc) == 0) { k->selected = i; return 1; }
    return 0;
}

static void test_rebind(void)
{
    printf("keys: rebinding a shortcut\n");

    rig_init();
    keys_show(&g_s);
    binds_set = binds_unbound = 0;

    CHECK(select_desc("Wallpaper picker"), "found the row to rebind");
    keys_key(&g_s, XKB_KEY_F2, 0);
    CHECK(g_s.keys.capturing, "F2 arms a capture");

    /* While armed, the compositor's OWN chords are captured rather than
     * dispatched — moving a shortcut onto Super+Y has to be possible, and
     * letting Super through would run whatever Super+Y already does. */
    CHECK(keys_key(&g_s, XKB_KEY_Super_L, WLR_MODIFIER_LOGO) == 1 &&
          g_s.keys.capturing,
          "a held modifier is not the answer — it is half of one");

    keys_key(&g_s, XKB_KEY_y, WLR_MODIFIER_LOGO);
    CHECK(!g_s.keys.capturing, "the chord ends the capture");
    CHECK(binds_set == 1 && strcmp(last_bound, "super+y") == 0,
          "the new chord is bound (got '%s')", last_bound);
    CHECK(binds_unbound == 1 && strcmp(last_unbound, "super+w") == 0,
          "…and the OLD one is taken away, or it answers to both (got '%s')",
          last_unbound);
    CHECK(dispatches == 0, "capturing Super+Y did not RUN anything");

    /* The list is rebuilt from the table, so the row now shows its new key.
     * Nothing else moved. */
    CHECK(view_has("Wallpaper picker"), "the shortcut is still listed");
    CHECK(g_s.keys.n == 8, "and the list is the same length (got %d)", g_s.keys.n);

    keys_hide(&g_s);
}

static void test_rebind_refusals(void)
{
    printf("keys: the rebinds that are refused\n");

    rig_init();
    keys_show(&g_s);
    binds_set = binds_unbound = 0;

    /* Esc is the way out. With every key taken as input, a capture that could
     * not be cancelled would be a trap. */
    CHECK(select_desc("Float window"), "found a row");
    keys_key(&g_s, XKB_KEY_F2, 0);
    keys_key(&g_s, XKB_KEY_Escape, 0);
    CHECK(!g_s.keys.capturing && binds_set == 0, "Esc cancels and binds nothing");

    /* A bare letter. Bound to `close`, `q` would close the focused window every
     * time it was typed anywhere that did not claim the key first, and the way
     * back would be a text editor. */
    keys_key(&g_s, XKB_KEY_F2, 0);
    keys_key(&g_s, XKB_KEY_q, 0);
    CHECK(!g_s.keys.capturing && binds_set == 0,
          "a bare letter is refused — it would type itself");
    CHECK(g_s.keys.status[0], "…and the panel says why");

    /* A chord already in use. Refused rather than stolen: handle_keybinding
     * takes the FIRST match, so an overwrite would not look like a conflict, it
     * would look like the other feature quietly going dead. */
    keys_key(&g_s, XKB_KEY_F2, 0);
    keys_key(&g_s, XKB_KEY_c, WLR_MODIFIER_LOGO);
    CHECK(!g_s.keys.capturing && binds_set == 0 && binds_unbound == 0,
          "a chord that is already taken is refused");
    CHECK(strstr(g_s.keys.status, "Control panel") != NULL,
          "…naming what has it (got '%s')", g_s.keys.status);

    /* Its own current key is a no-op rather than an unbind-then-rebind, which
     * would briefly leave the shortcut unreachable for no reason. */
    keys_key(&g_s, XKB_KEY_F2, 0);
    keys_key(&g_s, XKB_KEY_f, WLR_MODIFIER_LOGO);
    CHECK(!g_s.keys.capturing && binds_set == 0 && binds_unbound == 0,
          "rebinding a shortcut to the key it already has changes nothing");

    /* The one row shape that is not one bind and cannot be moved: "Super+1–9"
     * stands for nine of them and names none. (The tap row is the other shape
     * that is not a chord, and it IS rebindable — see the test below.) */
    CHECK(select_desc("Switch to workspace"), "found the collapsed row");
    keys_key(&g_s, XKB_KEY_F2, 0);
    CHECK(!g_s.keys.capturing,
          "a row that stands for nine binds and names none refuses a capture");

    keys_hide(&g_s);
}

/* ── The tap, which is a rebind of a different shape ─────────
 *
 * Every other row is a chord: two halves, and the modifier half is thrown away
 * while it is held. The tap row is one modifier and nothing else, so the key
 * every other capture ignores is the only key this one accepts — which is why
 * syn_rebind_capture_ignores() takes the row and not just the keysym, and why
 * this is worth its own test rather than a line in the one above.
 */
static void test_rebind_tap(void)
{
    printf("keys: moving the start-menu tap\n");

    rig_init();
    keys_show(&g_s);

    CHECK(select_desc("Start menu"), "found the tap row");
    CHECK(strcmp(selected_combo(), "Super (tap)") == 0,
          "…listed with the modifier the config says (got '%s')",
          selected_combo());

    keys_key(&g_s, XKB_KEY_F2, 0);
    CHECK(g_s.keys.capturing, "the tap row arms a capture like any other");

    /* A chord is not an answer here: the row takes one modifier, and taking
     * "Super+K" would leave the tap on a key that is half of something else.
     * Refused the way every other refusal in this panel is — the capture ends
     * and the status line says what was wanted, rather than the panel sitting
     * there re-asking a question it has already had the wrong answer to. */
    keys_key(&g_s, XKB_KEY_k, WLR_MODIFIER_LOGO);
    CHECK(!g_s.keys.capturing && g_s.config.tap_mod == WLR_MODIFIER_LOGO,
          "a chord does not move the tap");
    CHECK(strstr(g_s.keys.status, "Tap Super") != NULL,
          "…and says what it wants instead (got '%s')", g_s.keys.status);

    /* The modifier press every other capture throws away. */
    CHECK(select_desc("Start menu"), "still on the tap row");
    keys_key(&g_s, XKB_KEY_F2, 0);
    keys_key(&g_s, XKB_KEY_Alt_L, 0);
    CHECK(!g_s.keys.capturing && g_s.config.tap_mod == WLR_MODIFIER_ALT,
          "a bare modifier IS the answer on the tap row");
    CHECK(binds_set == 0 && binds_unbound == 0,
          "…and it touches no bind, because the tap is not one");
    CHECK(select_desc("Start menu") && strcmp(selected_combo(), "Alt (tap)") == 0,
          "the list is re-read, so the row shows the new key (got '%s')",
          selected_combo());

    /* Off is a value. Without it the only way to stop a tapped Super opening
     * the menu would be to hand-edit synuirc. */
    keys_key(&g_s, XKB_KEY_F2, 0);
    keys_key(&g_s, XKB_KEY_Delete, 0);
    CHECK(!g_s.keys.capturing && g_s.config.tap_mod == 0,
          "Delete turns the tap off");
    CHECK(select_desc("Start menu") && strcmp(selected_combo(), "Off") == 0,
          "…and the row says so (got '%s')", selected_combo());

    /* Still runnable with the tap off: the row has an action, and Enter on it
     * has always been "open the start menu" rather than "press the key". */
    dispatches = 0;
    keys_key(&g_s, XKB_KEY_Return, 0);
    CHECK(dispatches == 1 && strcmp(last_action, "start_menu") == 0,
          "Enter still opens the start menu with no tap key at all");

    /* And back onto the same key it already has, which is a no-op. */
    rig_init();
    keys_show(&g_s);
    CHECK(select_desc("Start menu"), "back on the tap row");
    keys_key(&g_s, XKB_KEY_F2, 0);
    keys_key(&g_s, XKB_KEY_Super_L, 0);
    CHECK(!g_s.keys.capturing && g_s.config.tap_mod == WLR_MODIFIER_LOGO,
          "re-tapping the key it already has changes nothing");
    CHECK(strcmp(g_s.keys.status, "Unchanged") == 0,
          "…and says so (got '%s')", g_s.keys.status);

    keys_hide(&g_s);
}

/* ── The tap's OTHER half: what it opens ─────────────────────
 *
 * F2 on the tap row answers "which key"; F3 on any row answers "which feature",
 * and the two are independent — the whole point of the pair is that the tap
 * could only ever open the start menu before, no matter which key it was moved
 * onto. Assignment is by ROW rather than by capture, because an action is not a
 * keystroke and the list on screen already names every one of them.
 */
static void test_tap_action(void)
{
    printf("keys: pointing the tap at another action\n");

    rig_init();
    keys_show(&g_s);
    binds_set = binds_unbound = 0;

    int before = g_s.keys.n;
    CHECK(select_desc("Wallpaper picker"), "found a row to put on the tap");
    keys_key(&g_s, XKB_KEY_F3, 0);
    CHECK(strcmp(g_s.config.tap_action, "wallpaper") == 0 &&
          g_s.config.tap_arg[0] == '\0',
          "F3 puts the selected row's action on the tap (got '%s')",
          g_s.config.tap_action);

    /* And TAKES the chord. Leaving it bound is what put rofi on both the Super
     * tap and Super+Space: one feature answering two keys, one of which cannot
     * be rebound afterwards because syn_rebind_apply() refuses a taken combo. */
    CHECK(binds_unbound == 1 && strcmp(last_unbound, "super+w") == 0,
          "…and frees the chord it came from (%d unbound, last '%s')",
          binds_unbound, last_unbound);
    CHECK(binds_set == 0, "…without binding anything (got %d)", binds_set);
    CHECK(strstr(g_s.keys.status, "Super tap") != NULL &&
          strstr(g_s.keys.status, "Wallpaper picker") != NULL &&
          strstr(g_s.keys.status, "freed") != NULL,
          "…and names both halves and the freed key (got '%s')", g_s.keys.status);

    /* The list is re-read, so the tap row now DESCRIBES what it opens. A row
     * that always said "Start menu" would be the list disagreeing with the
     * keyboard, which is the same bug the combo column was fixed for.
     *
     * Exactly ONE row names it now, and it is the tap: the list IS the bind
     * table, so the freed chord's row is gone from it. Two rows reading
     * "Wallpaper picker" would be the duplicate still on screen. */
    CHECK(view_count("Wallpaper picker") == 1 && g_s.keys.n == before - 1,
          "the row it came from is gone (%d named it, %d rows vs %d)",
          view_count("Wallpaper picker"), g_s.keys.n, before);
    CHECK(select_desc("Wallpaper picker") &&
          strcmp(selected_combo(), "Super (tap)") == 0,
          "the tap row now reads as the thing it opens (got '%s')",
          selected_combo());

    /* An argument is part of the action: "spawn" alone would run nothing, and
     * this is the shape velle wanted the tap in — rofi is a spawn with an arg. */
    CHECK(select_desc("synui-screenshot region"), "found a spawn row");
    keys_key(&g_s, XKB_KEY_F3, 0);
    CHECK(strcmp(g_s.config.tap_action, "spawn") == 0 &&
          strcmp(g_s.config.tap_arg, "synui-screenshot region") == 0,
          "the argument goes with it (got '%s' / '%s')",
          g_s.config.tap_action, g_s.config.tap_arg);
    CHECK(strcmp(last_unbound, "super+shift+s") == 0,
          "the freed chord is that row's, modifiers and all (got '%s')",
          last_unbound);

    /* The state velle was actually in: the tap already opens this feature AND
     * the chord is still live. F3 is the key that clears it up, so it has to
     * act here — "Unchanged" would be true of the tap and leave the duplicate
     * keybinding it was pressed to remove. */
    rig_init();
    snprintf(g_s.config.tap_action, sizeof(g_s.config.tap_action), "wallpaper");
    keys_show(&g_s);
    binds_set = binds_unbound = 0;
    /* Row 0 is the tap, which now reads "Wallpaper picker" too — take the
     * SECOND one, the chord, or this presses F3 on the tap row. */
    CHECK(view_count("Wallpaper picker") == 2, "the duplicate is on screen");
    CHECK(select_desc("Wallpaper picker"), "found the tap row");
    g_s.keys.selected++;
    CHECK(strcmp(selected_combo(), "Super+W") == 0,
          "…and the row under it is the chord (got '%s')", selected_combo());
    keys_key(&g_s, XKB_KEY_F3, 0);
    CHECK(binds_unbound == 1 && view_count("Wallpaper picker") == 1,
          "F3 clears a duplicate the tap already runs (%d unbound)",
          binds_unbound);
    CHECK(strstr(g_s.keys.status, "freed") != NULL &&
          strstr(g_s.keys.status, "already opens") != NULL,
          "…and says it was the key, not the action, that moved (got '%s')",
          g_s.keys.status);

    /* With no tap key the chord STAYS. `tap_key = none` runs nothing, so
     * freeing it here would leave the feature on no key at all — reachable from
     * neither a chord nor a tap, and recoverable only by Ctrl+Shift+R. */
    rig_init();
    g_s.config.tap_mod = 0;
    keys_show(&g_s);
    binds_set = binds_unbound = 0;

    CHECK(select_desc("Wallpaper picker"), "found the row again");
    keys_key(&g_s, XKB_KEY_F3, 0);
    CHECK(strcmp(g_s.config.tap_action, "wallpaper") == 0 && binds_unbound == 0,
          "a tap with no key takes the action but not the chord (%d unbound)",
          binds_unbound);
    CHECK(strstr(g_s.keys.status, "no tap key yet") != NULL,
          "…and sends you to F2 first (got '%s')", g_s.keys.status);

    /* Which leaves the one case that really is a no-op: same action, and no
     * chord left to take either. The cursor has not moved — nothing was removed
     * from the list — so this is the same row a second time. */
    keys_key(&g_s, XKB_KEY_F3, 0);
    CHECK(strcmp(g_s.keys.status, "Unchanged") == 0 && binds_unbound == 0,
          "the same row twice with nothing to free is 'Unchanged' (got '%s')",
          g_s.keys.status);

    /* The tap row cannot be pointed at itself — that is the one row whose
     * action IS whatever this key is setting, so it would be a no-op that looks
     * like a working assignment. */
    rig_init();
    keys_show(&g_s);
    CHECK(select_desc("Start menu") &&
          strcmp(selected_combo(), "Super (tap)") == 0, "on the tap row");
    keys_key(&g_s, XKB_KEY_F3, 0);
    CHECK(strcmp(g_s.config.tap_action, "start_menu") == 0,
          "F3 on the tap row changes nothing");
    CHECK(strstr(g_s.keys.status, "IS the tap") != NULL,
          "…and says why (got '%s')", g_s.keys.status);

    /* And the collapsed workspace row, which names nine actions and none of
     * them — the same refusal F2 makes on it, for the same reason. */
    CHECK(select_desc("Switch to workspace"), "found the collapsed row");
    keys_key(&g_s, XKB_KEY_F3, 0);
    CHECK(strcmp(g_s.config.tap_action, "start_menu") == 0 &&
          strstr(g_s.keys.status, "no single action") != NULL,
          "a row that is nine binds cannot go on the tap (got '%s')",
          g_s.keys.status);

    keys_hide(&g_s);
}

static void test_rebind_reset(void)
{
    printf("keys: putting every shortcut back\n");

    rig_init();
    keys_show(&g_s);
    reloads = 0;

    /* Ctrl+Shift+R, told apart from Ctrl+R by the SYMBOL rather than the mask:
     * xkb reports the shifted letter, and matching on WLR_MODIFIER_SHIFT would
     * make every Ctrl+R a reset with caps lock on.
     *
     * Off the top row first anyway — the palette opens on the tap row, whose
     * capture takes a bare modifier and would not read Ctrl+R as an arm. */
    CHECK(select_desc("Wallpaper picker"), "found a rebindable row");
    keys_key(&g_s, XKB_KEY_r, WLR_MODIFIER_CTRL);
    CHECK(g_s.keys.capturing, "Ctrl+R arms a capture, like F2");
    keys_key(&g_s, XKB_KEY_Escape, 0);
    CHECK(!g_s.keys.capturing && g_s.keys.visible,
          "Esc out of a capture cancels it WITHOUT closing the palette");

    keys_key(&g_s, XKB_KEY_R, WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT);
    CHECK(reloads == 1, "Ctrl+Shift+R reloads the config rather than resetting by hand");
    CHECK(!g_s.keys.capturing, "and does not leave a capture armed");

    keys_hide(&g_s);
}

/* ── The SAME rebind, from the control panel ─────────────────
 *
 * Rebinding shipped reachable only from Super+/, which left the panel with a
 * category literally called "Shortcuts" showing you every binding and letting
 * you change none of them. The Shortcuts pane now takes the same three keys.
 *
 * What is pinned here is that it is the same MACHINERY, not a second copy: the
 * assertions below are on the recorded config_bind_set/config_unbind_combo
 * pair — keys.c's syn_rebind_apply() — reached through ctlpanel_key(). Both
 * files are linked for real in this test, so a control panel that grew its own
 * private notion of what is bindable would show up as these numbers changing
 * while the palette's stayed the same.
 *
 * Driven by keysym for panel_pointer_test.c's reason: nothing can synthesise
 * input into a headless synui, and uinput would land on the LIVE session.
 */
static void ctl_open_shortcuts(void)
{
    ctlpanel_show(&g_s);
    g_s.ctlpanel.cat   = CTL_CAT_SHORTCUTS;
    g_s.ctlpanel.focus = CTL_FOCUS_ITEMS;
    g_s.ctlpanel.sc_sel = 0;
    g_s.ctlpanel.scroll = 0;
}

/* Put the shortcuts cursor on the row holding this chord. By chord rather than
 * by description because the description is prose and the chord is the thing
 * the rebind acts on. */
static int ctl_select_chord(uint32_t mods, xkb_keysym_t sym)
{
    int n = ctlpanel_shortcut_count(&g_s);
    for (int i = 0; i < n; i++) {
        syn_ctl_shortcut_t sc;
        g_s.ctlpanel.sc_sel = i;
        if (ctlpanel_shortcut_selected(&g_s, &sc) &&
            sc.mods == mods && sc.sym == sym)
            return 1;
    }
    return 0;
}

static void test_ctlpanel_rebind(void)
{
    printf("ctlpanel: the Shortcuts pane rebinds too\n");

    rig_init();
    ctl_open_shortcuts();
    binds_set = binds_unbound = 0;

    CHECK(ctlpanel_shortcut_count(&g_s) > 0,
          "the pane lists the bind table (%d rows)",
          ctlpanel_shortcut_count(&g_s));

    /* ── F2 arms, and the chord lands ───────────────────── */
    CHECK(ctl_select_chord(WLR_MODIFIER_LOGO, XKB_KEY_w),
          "found Super+W to rebind");
    ctlpanel_key(&g_s, XKB_KEY_F2, 0);
    CHECK(g_s.ctlpanel.sc_capturing, "F2 arms a capture");

    /* THE claim of the capture branch: while armed, a Super combo is taken as
     * the answer instead of falling through to the global bind table. The panel
     * returns 0 for modified combos everywhere else, so this is the one place
     * the ordering of that test actually matters. */
    CHECK(ctlpanel_key(&g_s, XKB_KEY_Super_L, WLR_MODIFIER_LOGO) == 1 &&
          g_s.ctlpanel.sc_capturing,
          "a held modifier is half a chord, not the answer");

    CHECK(ctlpanel_key(&g_s, XKB_KEY_y, WLR_MODIFIER_LOGO) == 1,
          "the chord is SWALLOWED, not passed to the bind table");
    CHECK(!g_s.ctlpanel.sc_capturing, "…and it ends the capture");
    CHECK(binds_set == 1 && strcmp(last_bound, "super+y") == 0,
          "the new chord is bound (got '%s')", last_bound);
    CHECK(binds_unbound == 1 && strcmp(last_unbound, "super+w") == 0,
          "…and the old one is taken away (got '%s')", last_unbound);

    /* ── The refusals are shared, not reimplemented ─────── */
    binds_set = binds_unbound = 0;
    CHECK(ctl_select_chord(WLR_MODIFIER_LOGO, XKB_KEY_f), "found Super+F");
    ctlpanel_key(&g_s, XKB_KEY_F2, 0);
    ctlpanel_key(&g_s, XKB_KEY_k, 0);          /* a bare letter */
    CHECK(binds_set == 0 && !g_s.ctlpanel.sc_capturing,
          "a bare letter is refused — it would type itself");
    CHECK(strstr(g_s.ctlpanel.status, "Super") != NULL,
          "…and the panel says why (got '%s')", g_s.ctlpanel.status);

    CHECK(ctl_select_chord(WLR_MODIFIER_LOGO, XKB_KEY_f), "still on Super+F");
    ctlpanel_key(&g_s, XKB_KEY_F2, 0);
    ctlpanel_key(&g_s, XKB_KEY_c, WLR_MODIFIER_LOGO);   /* Super+C = control */
    CHECK(binds_set == 0,
          "a chord already in use is refused, never stolen");
    CHECK(strstr(g_s.ctlpanel.status, "already") != NULL,
          "…by name (got '%s')", g_s.ctlpanel.status);

    /* The collapsed nine-workspace row stands for nine binds and names none, so
     * it has no single chord to move. */
    {
        int n = ctlpanel_shortcut_count(&g_s), found = 0;
        for (int i = 0; i < n; i++) {
            syn_ctl_shortcut_t sc;
            g_s.ctlpanel.sc_sel = i;
            if (!ctlpanel_shortcut_selected(&g_s, &sc) || sc.rebindable) continue;
            found = 1;
            ctlpanel_key(&g_s, XKB_KEY_F2, 0);
            CHECK(!g_s.ctlpanel.sc_capturing,
                  "a row that is not one bind refuses to arm ('%s')", sc.desc);
            break;
        }
        CHECK(found, "the rig has a non-rebindable row to refuse");
    }

    /* ── The tap row is the same shape here too ──────────
     *
     * The one place the two panels could still diverge: the modifier press this
     * one throws away on every other row has to be the answer on this one, and
     * that decision is syn_rebind_capture_ignores()'s rather than each panel's.
     * Asserted through ctlpanel_key(), so a control panel that kept its own
     * "ignore every modifier" line fails here and nowhere else. */
    g_s.ctlpanel.sc_sel = 0;   /* the tap row */
    {
        syn_ctl_shortcut_t sc;
        CHECK(ctlpanel_shortcut_selected(&g_s, &sc) && sc.tap,
              "row 0 is the tap");
    }
    ctlpanel_key(&g_s, XKB_KEY_F2, 0);
    CHECK(g_s.ctlpanel.sc_capturing, "the tap row arms in the control panel");
    ctlpanel_key(&g_s, XKB_KEY_Control_L, 0);
    CHECK(!g_s.ctlpanel.sc_capturing &&
          g_s.config.tap_mod == WLR_MODIFIER_CTRL,
          "…and takes the bare modifier, the same as the palette does");

    /* ── Esc cancels the capture, not the panel ─────────── */
    CHECK(ctl_select_chord(WLR_MODIFIER_LOGO, XKB_KEY_f), "back on Super+F");
    ctlpanel_key(&g_s, XKB_KEY_F2, 0);
    CHECK(g_s.ctlpanel.sc_capturing, "armed again");
    ctlpanel_key(&g_s, XKB_KEY_Escape, 0);
    CHECK(!g_s.ctlpanel.sc_capturing, "Esc disarms");
    CHECK(g_s.ctlpanel.visible,
          "…and does NOT also close the panel — one Esc, one level");

    /* ── Closing while armed must not leave it armed ────── */
    ctlpanel_key(&g_s, XKB_KEY_F2, 0);
    CHECK(g_s.ctlpanel.sc_capturing, "armed before the close");
    ctlpanel_hide(&g_s);
    CHECK(!g_s.ctlpanel.sc_capturing,
          "closing disarms, or the next key typed at the DESKTOP rebinds");

    /* ── Ctrl+Shift+R resets, Ctrl+R arms ───────────────── */
    rig_init();
    ctl_open_shortcuts();
    reloads = 0;

    /* On a CHORD row: row 0 of the list is the "Start menu" tap, whose capture
     * answers to a bare modifier — landing there would make this assert the tap
     * rules rather than the chord ones. */
    CHECK(ctl_select_chord(WLR_MODIFIER_LOGO, XKB_KEY_w), "on Super+W");
    ctlpanel_key(&g_s, XKB_KEY_r, WLR_MODIFIER_CTRL);
    CHECK(g_s.ctlpanel.sc_capturing, "Ctrl+R arms, as it does in the palette");

    /* Disarm with the capture branch's own Esc — the panel's other Esc means
     * "back out to the sidebar", which would take the focus this needs. */
    ctlpanel_key(&g_s, XKB_KEY_Escape, 0);
    CHECK(!g_s.ctlpanel.sc_capturing && g_s.ctlpanel.focus == CTL_FOCUS_ITEMS,
          "Esc while armed cancels the capture and leaves the focus alone");

    ctlpanel_key(&g_s, XKB_KEY_R, WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT);
    CHECK(reloads == 1,
          "Ctrl+Shift+R puts every shortcut back through the config reload");

    /* Shift is read off the SYMBOL, not the mask: this is the case that would
     * break if someone matched WLR_MODIFIER_SHIFT instead. */
    reloads = 0;
    CHECK(ctl_select_chord(WLR_MODIFIER_LOGO, XKB_KEY_w), "back on Super+W");
    ctlpanel_key(&g_s, XKB_KEY_r, WLR_MODIFIER_CTRL);
    CHECK(g_s.ctlpanel.sc_capturing && reloads == 0,
          "…and plain Ctrl+r is still the arm, not the reset");
    ctlpanel_key(&g_s, XKB_KEY_Escape, 0);

    /* ── The rebind keys belong to the Shortcuts pane ───── */
    g_s.ctlpanel.cat = CTL_CAT_APPEARANCE;
    reloads = 0;
    ctlpanel_key(&g_s, XKB_KEY_F2, 0);
    CHECK(!g_s.ctlpanel.sc_capturing,
          "F2 in a settings category arms nothing");
    CHECK(ctlpanel_key(&g_s, XKB_KEY_R,
                       WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT) == 0 &&
          reloads == 0,
          "…and Ctrl+Shift+R there falls through to the bind table");

    ctlpanel_hide(&g_s);
}

/* ── The cursor the rebind needs ─────────────────────────────
 *
 * The pane was a pure scroll with no cursor until rebinding needed a row to
 * act on. Up/Down move the selection and drag the scroll behind them, which is
 * what every other pane on this panel already does.
 */
static void test_ctlpanel_shortcut_cursor(void)
{
    printf("ctlpanel: the shortcuts pane has a cursor\n");

    rig_init();
    ctl_open_shortcuts();

    int n = ctlpanel_shortcut_count(&g_s);
    CHECK(n > 1, "more than one row to move between (%d)", n);

    ctlpanel_key(&g_s, XKB_KEY_Down, 0);
    CHECK(g_s.ctlpanel.sc_sel == 1, "Down moves the cursor (got %d)",
          g_s.ctlpanel.sc_sel);
    ctlpanel_key(&g_s, XKB_KEY_Up, 0);
    CHECK(g_s.ctlpanel.sc_sel == 0, "Up moves it back");

    /* Both ends hold rather than wrapping or running off the list. */
    ctlpanel_key(&g_s, XKB_KEY_Up, 0);
    CHECK(g_s.ctlpanel.sc_sel == 0, "the top holds");
    ctlpanel_key(&g_s, XKB_KEY_End, 0);
    CHECK(g_s.ctlpanel.sc_sel == n - 1, "End goes to the last row");
    ctlpanel_key(&g_s, XKB_KEY_Down, 0);
    CHECK(g_s.ctlpanel.sc_sel == n - 1, "the bottom holds");
    ctlpanel_key(&g_s, XKB_KEY_Home, 0);
    CHECK(g_s.ctlpanel.sc_sel == 0, "Home goes back to the first");

    /* The scroll follows the cursor: a highlight outside the drawn window is a
     * cursor the user cannot see. */
    CHECK(g_s.ctlpanel.scroll <= g_s.ctlpanel.sc_sel &&
          g_s.ctlpanel.sc_sel < g_s.ctlpanel.scroll + CTL_SHORTCUT_ROWS,
          "the cursor is inside the drawn window");

    /* Leaving the category drops the cursor AND any armed capture with it. */
    ctlpanel_key(&g_s, XKB_KEY_Down, 0);
    ctlpanel_key(&g_s, XKB_KEY_F2, 0);
    CHECK(g_s.ctlpanel.sc_capturing, "armed on row 1");
    ctlpanel_key(&g_s, XKB_KEY_Tab, 0);          /* out to the sidebar */
    ctlpanel_key(&g_s, XKB_KEY_Down, 0);         /* a different category */
    CHECK(!g_s.ctlpanel.sc_capturing,
          "changing category disarms — the row it aimed at is gone");

    ctlpanel_hide(&g_s);
}

int main(void)
{
    test_list_is_the_bind_table();
    test_search();
    test_enter_runs_it();
    test_cursor();
    test_rebind();
    test_rebind_refusals();
    test_rebind_tap();
    test_tap_action();
    test_rebind_reset();
    test_modal_contract();
    test_ctlpanel_shortcut_cursor();
    test_ctlpanel_rebind();

    printf("%s: %d checked, %d failed\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
