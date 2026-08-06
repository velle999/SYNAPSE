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

void synui_binding_execute(syn_server_t *s, const char *action, const char *arg)
{
    (void)s;
    dispatches++;
    hidden_before_dispatch = !g_s.keys.visible;
    snprintf(last_action, sizeof(last_action), "%s", action ? action : "");
    snprintf(last_arg,    sizeof(last_arg),    "%s", arg    ? arg    : "");
}

int synmon_send_reload(const char *m, char *o, size_t n)
{ (void)m; (void)o; (void)n; return 0; }

/* Everything else ctlpanel.c reaches for. None of it runs here. */
void uifx_apply(syn_server_t *s)                     { (void)s; }
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
void nightlight_toggle(syn_server_t *s)              { (void)s; }
void cursor_reload(syn_server_t *s)                  { (void)s; }
void deskicons_reload(syn_server_t *s)               { (void)s; }
void record_audio_toggle(syn_server_t *s)            { (void)s; }
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
int  settings_state_has(const char *k)                { (void)k; return 0; }

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

static int view_has(const char *desc)
{
    syn_keys_t *k = &g_s.keys;
    for (int i = 0; i < k->n_view; i++)
        if (strcmp(k->all[k->view[i]].desc, desc) == 0) return 1;
    return 0;
}

/* ── The list comes from the bind table ──────────────────── */

static void test_list_is_the_bind_table(void)
{
    printf("keys: the list is the bind table\n");

    rig_init();
    keys_show(&g_s);

    /* Seven binds, less the eight collapsed into "Super+1–9" (one of which was
     * seeded), plus the Super-tap row and the collapsed row itself. */
    CHECK(g_s.keys.n == 8, "every bind is listed, workspaces collapsed (got %d)",
          g_s.keys.n);
    CHECK(g_s.keys.n_view == g_s.keys.n,
          "an empty query shows all of them (got %d)", g_s.keys.n_view);

    CHECK(view_has("Wallpaper picker"), "a bind's description comes from its action");
    CHECK(view_has("Keyboard shortcuts (this list)"), "the palette lists itself");
    CHECK(view_has("Switch to workspace"), "the collapsed workspace row is there");
    CHECK(view_has("Start menu"), "Super-tap is listed though it is not a bind");
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

int main(void)
{
    test_list_is_the_bind_table();
    test_search();
    test_enter_runs_it();
    test_cursor();
    test_modal_contract();

    printf("%s: %d checked, %d failed\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
