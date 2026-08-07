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
