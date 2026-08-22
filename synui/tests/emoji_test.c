/*
 * emoji_test — the emoji picker's logic (src/emoji.c)
 *
 * Nothing can synthesise input into a headless synui — no virtual-pointer
 * protocol, no input devices on the headless backend — so emoji_key() is
 * driven directly, exactly as input.c's chain calls it. Same approach as
 * ctlpanel_choice_test and keys_test; the stubs are theirs.
 *
 * What is actually worth pinning:
 *
 *   - the RECENTS arithmetic. emoji_recent_push() rotates a fixed array and
 *     has to handle "already at the front", "already present further down" and
 *     "not present, list full" differently. Off-by-one here duplicates an entry
 *     or drops the oldest one early, and neither is visible until you have used
 *     the picker for a week.
 *
 *   - the two-array split. The recents row and a filtered slice of the table
 *     are different arrays behind one index space, and emoji_at() /
 *     emoji_name_at() / emoji_total() must agree about which one is showing.
 *
 *   - that search actually narrows, and that typing on the Recents tab moves
 *     to All rather than searching a 24-entry list.
 *
 * It writes only into its own mkdtemp scratch $HOME, so the live desktop's
 * emoji.recent is untouched.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* ── The compositor, stubbed ─────────────────────────────── */

static int  renders;
static char last_spawn[256];
static int  spawn_count;

void synui_render_emoji(syn_server_t *s)  { (void)s; renders++; }
void ctlpanel_child_closed(syn_server_t *s, const char *a) { (void)s; (void)a; }

void synui_spawn(const char *cmd)
{
    spawn_count++;
    snprintf(last_spawn, sizeof(last_spawn), "%s", cmd ? cmd : "");
}

static char scratch[256];

bool syn_config_path(char *buf, size_t n, const char *leaf)
{
    snprintf(buf, n, "%s/%s", scratch, leaf);
    return true;
}
void syn_config_ensure_dir(void) { mkdir(scratch, 0700); }

/* ── Helpers ─────────────────────────────────────────────── */

static syn_server_t *fresh(void)
{
    static syn_server_t s;
    memset(&s, 0, sizeof(s));
    return &s;
}

/* Index of a known emoji in the generated table, so the tests can name one
 * without hardcoding a codepoint that a regeneration might move. */
static const char *by_name(const char *name)
{
    for (int i = 0; i < syn_emoji_count; i++)
        if (strcmp(syn_emoji_table[i].name, name) == 0)
            return syn_emoji_table[i].ch;
    return NULL;
}

/* ── Cases ───────────────────────────────────────────────── */

static void test_table(void)
{
    CHECK(syn_emoji_count > 1000, "the table should carry the emoji blocks (got %d)",
          syn_emoji_count);
    CHECK(syn_emoji_cat_count >= 5, "there should be several categories (got %d)",
          syn_emoji_cat_count);
    CHECK(by_name("grinning face") != NULL, "grinning face should be in the table");
    CHECK(by_name("rocket") != NULL, "rocket should be in the table");

    /* Names are lowercased at generation time — emoji_rebuild() does a plain
     * strstr and would silently match nothing if any name carried capitals. */
    int caps = 0;
    for (int i = 0; i < syn_emoji_count; i++)
        for (const char *p = syn_emoji_table[i].name; *p; p++)
            if (*p >= 'A' && *p <= 'Z') { caps++; break; }
    CHECK(caps == 0, "%d table names still carry capitals; search would miss them", caps);

    /* Categories must all be one of the declared ones, or the tab filter drops
     * entries into a category nothing displays. */
    int orphan = 0;
    for (int i = 0; i < syn_emoji_count; i++) {
        bool found = false;
        for (int c = 0; c < syn_emoji_cat_count; c++)
            if (strcmp(syn_emoji_table[i].cat, syn_emoji_cats[c]) == 0) { found = true; break; }
        if (!found) orphan++;
    }
    CHECK(orphan == 0, "%d entries name a category not in syn_emoji_cats[]", orphan);
}

static void test_search(void)
{
    syn_server_t *s = fresh();
    emoji_show(s);

    /* No recents on a fresh scratch home, so it should open on All. */
    CHECK(s->emoji.cat == EMOJI_CAT_ALL,
          "with no recents the picker should open on All");
    int all = emoji_total(s);
    CHECK(all > 1000, "All should list the whole table (got %d)", all);

    /* Type "rocket". */
    for (const char *p = "rocket"; *p; p++)
        emoji_key(s, (xkb_keysym_t)*p, 0);

    int narrowed = emoji_total(s);
    CHECK(narrowed > 0 && narrowed < all,
          "searching should narrow the list (%d -> %d)", all, narrowed);

    bool found = false;
    for (int i = 0; i < narrowed; i++)
        if (strcmp(emoji_name_at(s, i), "rocket") == 0) { found = true; break; }
    CHECK(found, "\"rocket\" should be among the results for that search");

    /* Every result must actually match — the filter is the assertion. */
    int bad = 0;
    for (int i = 0; i < narrowed; i++)
        if (!strstr(emoji_name_at(s, i), "rocket")) bad++;
    CHECK(bad == 0, "%d results do not contain the search text", bad);

    /* Capitals are folded on the way in, so SHIFT+letters search the same. */
    syn_server_t *s2 = fresh();
    emoji_show(s2);
    for (const char *p = "ROCKET"; *p; p++)
        emoji_key(s2, (xkb_keysym_t)*p, 0);
    CHECK(emoji_total(s2) == narrowed,
          "an upper-case search should match the same entries (%d vs %d)",
          emoji_total(s2), narrowed);

    /* Backspace widens it again, and Esc clears rather than closing. */
    emoji_key(s, XKB_KEY_BackSpace, 0);
    CHECK(emoji_total(s) >= narrowed, "backspace should widen the results");

    emoji_key(s, XKB_KEY_Escape, 0);
    CHECK(s->emoji.visible, "the first Esc should clear the search, not close");
    CHECK(emoji_total(s) == all, "clearing the search should restore the full list");
    emoji_key(s, XKB_KEY_Escape, 0);
    CHECK(!s->emoji.visible, "a second Esc should close the panel");
}

static void test_nonsense_search(void)
{
    syn_server_t *s = fresh();
    emoji_show(s);
    for (const char *p = "zzzqqq"; *p; p++)
        emoji_key(s, (xkb_keysym_t)*p, 0);

    CHECK(emoji_total(s) == 0, "a nonsense search should match nothing");
    CHECK(emoji_at(s, 0) == NULL, "an empty view should have no cell 0");

    /* Enter on an empty view must not insert an empty string — it closes. */
    spawn_count = 0;
    emoji_key(s, XKB_KEY_Return, 0);
    CHECK(spawn_count == 0, "Enter on an empty result set should not insert");
    CHECK(!s->emoji.visible, "Enter on an empty result set should close the panel");
}

static void test_insert_and_recents(void)
{
    syn_server_t *s = fresh();
    emoji_show(s);
    for (const char *p = "rocket"; *p; p++)
        emoji_key(s, (xkb_keysym_t)*p, 0);

    const char *rocket = emoji_at(s, s->emoji.selected);
    CHECK(rocket && rocket[0], "there should be a selected emoji to insert");
    char picked[16];
    snprintf(picked, sizeof(picked), "%s", rocket ? rocket : "");

    spawn_count = 0;
    last_spawn[0] = '\0';
    emoji_key(s, XKB_KEY_Return, 0);

    CHECK(!s->emoji.visible, "inserting should close the panel");
    /* Both halves run: the clipboard always, the typing best-effort. */
    CHECK(spawn_count == 2, "insert should spawn wl-copy AND wtype (got %d)",
          spawn_count);
    CHECK(strstr(last_spawn, "wtype") != NULL,
          "the last spawn should be wtype, so the clipboard is claimed first");

    /* …and it landed in the recents file. */
    syn_server_t *s2 = fresh();
    emoji_show(s2);
    CHECK(s2->emoji.cat == EMOJI_CAT_RECENT,
          "with recents present the picker should open on Recent");
    CHECK(emoji_total(s2) == 1, "one emoji used should be one recent");
    CHECK(strcmp(emoji_at(s2, 0), picked) == 0,
          "the recent should be the emoji that was inserted");
    /* Recents are stored as characters, so the name has to be looked back up —
     * an empty name here means that lookup is broken. */
    CHECK(strcmp(emoji_name_at(s2, 0), "rocket") == 0,
          "a recent should still resolve to its Unicode name (got \"%s\")",
          emoji_name_at(s2, 0));
}

/* The rotation, driven straight through the file so the ordering rules are
 * checked as a user would actually accumulate them. */
static void test_recent_ordering(void)
{
    char p[320];
    snprintf(p, sizeof(p), "%s/emoji.recent", scratch);
    unlink(p);

    const char *a = by_name("rocket");
    const char *b = by_name("fire");
    const char *c = by_name("party popper");
    CHECK(a && b && c, "the three landmarks should be in the table");
    if (!a || !b || !c) return;

    /* Insert a, then b, then c. Newest first afterwards. */
    const char *order[] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        syn_server_t *s = fresh();
        emoji_show(s);
        /* Tab to All rather than assigning cat: the rebuild happens on the tab
         * change, so setting the field directly leaves the previous view's
         * filter in place — which from the second iteration onward (once there
         * ARE recents and the panel opens on them) is an empty list. */
        while (s->emoji.cat != EMOJI_CAT_ALL)
            emoji_key(s, XKB_KEY_Tab, 0);

        /* Find it in the current view and select it. */
        int idx = -1;
        for (int j = 0; j < emoji_total(s); j++)
            if (strcmp(emoji_at(s, j), order[i]) == 0) { idx = j; break; }
        CHECK(idx >= 0, "landmark %d should be findable in All", i);
        if (idx < 0) return;
        s->emoji.selected = idx;
        emoji_key(s, XKB_KEY_Return, 0);
    }

    syn_server_t *s = fresh();
    emoji_show(s);
    CHECK(emoji_total(s) == 3, "three distinct inserts should be three recents (got %d)",
          emoji_total(s));
    CHECK(strcmp(emoji_at(s, 0), c) == 0, "the newest should be first");
    CHECK(strcmp(emoji_at(s, 1), b) == 0, "then the one before it");
    CHECK(strcmp(emoji_at(s, 2), a) == 0, "then the oldest");

    /* Re-inserting an existing one moves it to the front WITHOUT duplicating. */
    int idx = -1;
    for (int j = 0; j < emoji_total(s); j++)
        if (strcmp(emoji_at(s, j), a) == 0) { idx = j; break; }
    s->emoji.selected = idx;
    emoji_key(s, XKB_KEY_Return, 0);

    syn_server_t *s2 = fresh();
    emoji_show(s2);
    CHECK(emoji_total(s2) == 3,
          "re-inserting an existing recent must not add a fourth (got %d)",
          emoji_total(s2));
    CHECK(strcmp(emoji_at(s2, 0), a) == 0,
          "a re-inserted recent should move to the front");
    CHECK(strcmp(emoji_at(s2, 1), c) == 0, "the rest should shift down in order");
    CHECK(strcmp(emoji_at(s2, 2), b) == 0, "the rest should shift down in order");
}

/* Typing on the Recents tab searches everything: the recents row is a
 * shortcut, not a place to look things up, and a search that returned nothing
 * because of the active tab would look broken. */
static void test_search_from_recents(void)
{
    syn_server_t *s = fresh();
    emoji_show(s);
    CHECK(s->emoji.cat == EMOJI_CAT_RECENT, "should open on Recent here");

    emoji_key(s, (xkb_keysym_t)'r', 0);
    CHECK(s->emoji.cat == EMOJI_CAT_ALL,
          "typing on the Recents tab should switch to All");
    CHECK(emoji_total(s) > EMOJI_RECENT_MAX,
          "the search should be running over the whole table");
}

static void test_categories(void)
{
    syn_server_t *s = fresh();
    emoji_show(s);

    /* Driven with Tab rather than by assigning s->emoji.cat, so the rebuild
     * happens the way it does in use. Tab from wherever it opened until All is
     * showing, then walk the blocks in order. */
    while (s->emoji.cat != EMOJI_CAT_ALL)
        emoji_key(s, XKB_KEY_Tab, 0);
    int all = emoji_total(s);
    CHECK(all > 1000, "All should list the whole table (got %d)", all);

    /* Each block must be non-empty, smaller than All, and labelled. */
    int sum = 0;
    for (int c = EMOJI_CAT_FIRST_BLOCK; c < emoji_cat_total(); c++) {
        emoji_key(s, XKB_KEY_Tab, 0);          /* All -> first block, then on */
        CHECK(s->emoji.cat == c, "Tab should land on category %d (got %d)",
              c, s->emoji.cat);
        int n = emoji_total(s);
        CHECK(n > 0, "category \"%s\" should not be empty", emoji_cat_label(c));
        CHECK(n < all, "category \"%s\" should be smaller than All", emoji_cat_label(c));
        CHECK(emoji_cat_label(c)[0], "category %d should have a label", c);
        sum += n;
    }
    /* The blocks must partition All exactly: an entry in no block would be
     * unreachable by tab, and one in two blocks would appear twice. */
    CHECK(sum == all, "the blocks should partition All exactly (%d vs %d)", sum, all);

    /* Tab wraps rather than running off the end. */
    s->emoji.cat = emoji_cat_total() - 1;
    emoji_key(s, XKB_KEY_Tab, 0);
    CHECK(s->emoji.cat == 0, "Tab past the last category should wrap to the first");
    emoji_key(s, XKB_KEY_ISO_Left_Tab, 0);
    CHECK(s->emoji.cat == emoji_cat_total() - 1,
          "Shift+Tab before the first should wrap to the last");
}

/* Super and Ctrl must reach the compositor even with the picker up, or the
 * panel traps you: those are how you leave. */
static void test_modifiers_pass_through(void)
{
    syn_server_t *s = fresh();
    emoji_show(s);
    CHECK(emoji_key(s, (xkb_keysym_t)'c', WLR_MODIFIER_LOGO) == 0,
          "Super+C must not be swallowed by the picker");
    CHECK(emoji_key(s, (xkb_keysym_t)'c', WLR_MODIFIER_CTRL) == 0,
          "Ctrl+C must not be swallowed by the picker");
    CHECK(emoji_key(s, (xkb_keysym_t)'c', 0) == 1,
          "a bare letter must go to the search box");
}

/* ── The pointer ─────────────────────────────────────────────
 *
 * The grid took the mouse when it was written; the CATEGORY STRIP did not, so
 * the nine tabs across the top were the one part of this panel you could see,
 * could read, and could not click. That is what is pinned here.
 *
 * synui_render_emoji() is stubbed, so the geometry it normally writes is
 * written here instead, with render.c's own numbers — the contract's division
 * of labour (render.c writes, the panel reads), exactly as panel_pointer_test
 * does it for the power panel.
 */
#define E_PX   400
#define E_PY   200
#define E_PAD   16
#define E_TOP   88
#define E_CELL  42
#define E_TAB_TOP 38
#define E_TAB_H   22
#define E_PW   (E_PAD * 2 + EMOJI_COLS * E_CELL)
#define E_PH   (E_TOP + EMOJI_ROWS * E_CELL + 54)

/* Lay the tabs out the way the draw loop does: left to right from pad+2, each
 * one as wide as its label. The real widths come from cairo; a fixed 60px
 * pitch is the same SHAPE and is what keeps this test out of the font stack. */
#define E_TAB_W  50
#define E_TAB_PITCH 64

static void emoji_geom(syn_server_t *s)
{
    hit_set_panel(&s->emoji.hit, E_PX, E_PY, E_PW, E_PH);
    hit_set_grid(&s->emoji.hit, E_PAD, E_TOP, E_CELL, E_CELL,
                 EMOJI_COLS, EMOJI_ROWS);
    hit_set_first(&s->emoji.hit, s->emoji.scroll * EMOJI_COLS);
    for (int c = 0; c < emoji_cat_total(); c++)
        hit_add_spot(&s->emoji.hit, E_PAD + 2 + c * E_TAB_PITCH, E_TAB_TOP,
                     E_TAB_W, E_TAB_H);
}

/* A point inside tab `c`, in layout coords. */
static double tab_x(int c) { return E_PX + E_PAD + 2 + c * E_TAB_PITCH + 10; }
static double tab_y(void)  { return E_PY + E_TAB_TOP + E_TAB_H / 2.0; }

/* A point inside grid cell (row, col). */
static double cell_x(int col) { return E_PX + E_PAD + col * E_CELL + E_CELL / 2.0; }
static double cell_y(int row) { return E_PY + E_TOP + row * E_CELL + E_CELL / 2.0; }

static void test_tab_pointer(void)
{
    syn_server_t *s = fresh();
    emoji_show(s);
    emoji_geom(s);

    const int target = EMOJI_CAT_FIRST_BLOCK + 1;   /* a block, not All/Recents */
    CHECK(s->emoji.cat != target, "the fixture should not already be on the target tab");

    /* Hover highlights and NOTHING else. Switching category on hover would
     * rebuild the view — and throw away a typed search — every time the pointer
     * crossed the strip on its way to the grid. */
    int before = s->emoji.cat;
    CHECK(emoji_motion(s, tab_x(target), tab_y()) == 1,
          "the strip is inside the panel, so motion belongs to the picker");
    CHECK(s->emoji.cat == before, "hovering a tab must not switch to it");
    CHECK(s->emoji.cat_hover == target, "hovering a tab should highlight it");

    /* Off the strip clears the highlight rather than leaving the last tab lit. */
    emoji_motion(s, cell_x(0), cell_y(0));
    CHECK(s->emoji.cat_hover == -1, "leaving the strip should clear the highlight");

    /* The click is the whole point. */
    s->emoji.selected = 5;
    CHECK(emoji_click(s, tab_x(target), tab_y(), BTN_LEFT, 0) == 1,
          "a click on a tab belongs to the picker");
    CHECK(s->emoji.cat == target, "a click on a tab should switch to it");
    CHECK(s->emoji.selected == 0,
          "switching category should put the selection back at the start");
    CHECK(emoji_total(s) > 0 && emoji_total(s) < syn_emoji_count,
          "the view should now be one block, not the whole table");

    /* Clicking the tab you are already on is a no-op you cannot tell from a
     * rebuild — what matters is that it does not close the panel. */
    emoji_geom(s);
    emoji_click(s, tab_x(target), tab_y(), BTN_LEFT, 0);
    CHECK(s->emoji.visible, "clicking the active tab must not close the picker");
    CHECK(s->emoji.cat == target, "…and must not move off it either");

    /* A click on the strip is never a click on a cell: the bands do not
     * overlap, and an emoji inserted by aiming at a tab would close the panel
     * and type a character nobody asked for. */
    spawn_count = 0;
    emoji_geom(s);
    emoji_click(s, tab_x(0), tab_y(), BTN_LEFT, 0);
    CHECK(spawn_count == 0, "a click on a tab must not insert anything");
    CHECK(s->emoji.visible, "…and must not close the picker");
}

/* The grid's own pointer, which the tabs now share a panel with — pinned here
 * so a change to the strip that swallowed the grid's clicks would be caught. */
static void test_grid_pointer(void)
{
    syn_server_t *s = fresh();
    /* The picker opens on Recents once anything has been inserted (the tests
     * above leave a recents file in the scratch home), and Recents is a short
     * row — so click through to All, which is the full grid this exercises.
     * Doubles as the check that the tab strip is how you get there. */
    emoji_show(s);
    emoji_geom(s);
    emoji_click(s, tab_x(EMOJI_CAT_ALL), tab_y(), BTN_LEFT, 0);
    CHECK(s->emoji.cat == EMOJI_CAT_ALL, "the All tab should switch to All");
    emoji_geom(s);

    emoji_motion(s, cell_x(3), cell_y(1));
    CHECK(s->emoji.selected == EMOJI_COLS + 3,
          "hover should select the cell under the pointer (got %d)",
          s->emoji.selected);

    spawn_count = 0;
    emoji_click(s, cell_x(3), cell_y(1), BTN_LEFT, 0);
    CHECK(spawn_count == 2, "a click on a cell should copy and type it (got %d)",
          spawn_count);
    CHECK(!s->emoji.visible, "inserting closes the picker");

    /* And a click off the panel closes it, which is every panel's contract. */
    emoji_show(s);
    emoji_geom(s);
    CHECK(emoji_click(s, E_PX - 40, E_PY - 40, BTN_LEFT, 0) == 1,
          "a click off the panel is still the picker's to swallow");
    CHECK(!s->emoji.visible, "a click off the panel closes it");
}

int main(void)
{
    char tmpl[] = "/tmp/synui-emoji-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }
    snprintf(scratch, sizeof(scratch), "%s", dir);

    test_table();
    test_search();
    test_nonsense_search();
    test_insert_and_recents();
    test_recent_ordering();
    test_search_from_recents();
    test_categories();
    test_modifiers_pass_through();
    test_tab_pointer();
    test_grid_pointer();

    char p[320];
    snprintf(p, sizeof(p), "%s/emoji.recent", scratch);
    unlink(p);
    rmdir(scratch);

    if (failures) {
        fprintf(stderr, "emoji_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("emoji_test: ok (%d emoji, %d categories)\n",
           syn_emoji_count, syn_emoji_cat_count);
    return 0;
}
