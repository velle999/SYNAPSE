/*
 * emoji.c — the emoji picker panel (Super+;)
 *
 * A compositor-drawn grid of every emoji the Unicode blocks in emoji_data.c
 * carry, with a search box over their Unicode names, category tabs and a
 * most-recently-used row.
 *
 * WHY IT CAN EXIST AT ALL
 *
 * It could not, until text.c. Every panel here draws through cairo's toy font
 * API, which resolves to ONE face with no per-glyph fallback — so before the
 * fallback landed, a grid of emoji was a grid of question marks, and the only
 * way to show one was to hand the job to a toolkit that does its own font
 * matching. text.c asks fontconfig for a font that can draw each character and,
 * for these, asks specifically for a COLOUR one. That is the whole reason this
 * is a native panel rather than a rofi script.
 *
 * It still needs an emoji font installed to be anything but tofu, which is why
 * noto-fonts-emoji is a hard depend rather than an optdepend.
 *
 * INSERTING
 *
 * Two steps, and the order matters:
 *
 *   1. the emoji goes on the clipboard (wl-copy), always;
 *   2. it is typed into whatever had focus (wtype), best-effort.
 *
 * Typing alone would be wrong: wtype drives the virtual-keyboard protocol, and
 * how an application handles a synthetic key burst is entirely up to the
 * application — terminals and Electron apps are both routinely unhappy with it.
 * The clipboard always works, so the emoji is never lost even when the typing
 * does nothing, and "it pasted nothing" is a failure mode this must not have.
 *
 * The panel closes BEFORE either runs. Focus returns to the window underneath
 * on close, and typing into a window that does not have focus yet types into
 * the panel that is still up — which is to say, nowhere.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/types/wlr_damage_ring.h>
#include <wlr/util/log.h>

#include "synui.h"

/* ── Recents ─────────────────────────────────────────────────
 *
 * Kept in ~/.config/synui/emoji.recent, one emoji per line, most recent first.
 * A file rather than memory because the whole value of a recents row is that it
 * survives the session — an emoji picker that forgets what you use every day is
 * a search box with extra steps.
 */

static bool emoji_recent_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "emoji.recent");
}

static void emoji_recent_load(syn_server_t *s)
{
    s->emoji.recent_count = 0;

    char path[256];
    if (!emoji_recent_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no file yet is the normal first-run state */

    char line[32];
    while (s->emoji.recent_count < EMOJI_RECENT_MAX && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        /* Through syn_utf8_copy: this file is on disk and can be edited, and a
         * truncated sequence in it would poison the cairo context of whatever
         * panel drew it — see text.c. */
        syn_utf8_copy(s->emoji.recent[s->emoji.recent_count],
                      sizeof(s->emoji.recent[0]), line);
        if (s->emoji.recent[s->emoji.recent_count][0])
            s->emoji.recent_count++;
    }
    fclose(f);
}

static void emoji_recent_save(syn_server_t *s)
{
    char path[256];
    if (!emoji_recent_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: emoji: cannot write '%s'", path);
        return;
    }
    for (int i = 0; i < s->emoji.recent_count; i++)
        fprintf(f, "%s\n", s->emoji.recent[i]);
    fclose(f);
}

/* Move `ch` to the front of the recents, dropping a duplicate rather than
 * letting the same emoji occupy the row twice.
 *
 * TAKES A COPY FIRST, and must. When the picker is showing the Recents tab,
 * the emoji being re-inserted is one of these very entries — emoji_at() hands
 * back a pointer INTO recent[] — so the rotation below overwrites the string
 * it is about to read. Re-inserting the third recent used to put the SECOND
 * one at the front, because by the time it was copied its source slot had
 * already been shifted over. Caught by emoji_test's ordering case.
 */
static void emoji_recent_push(syn_server_t *s, const char *ch)
{
    char keep[sizeof(s->emoji.recent[0])];
    snprintf(keep, sizeof(keep), "%s", ch);

    int existing = -1;
    for (int i = 0; i < s->emoji.recent_count; i++)
        if (strcmp(s->emoji.recent[i], keep) == 0) { existing = i; break; }

    if (existing == 0) return;   /* already at the front */

    /* Where the shift starts. An entry already in the list is pulled out of
     * its slot; a new one pushes the oldest off the end. */
    int from = existing >= 0 ? existing : s->emoji.recent_count;
    if (from >= EMOJI_RECENT_MAX) from = EMOJI_RECENT_MAX - 1;

    for (int i = from; i > 0; i--)
        memcpy(s->emoji.recent[i], s->emoji.recent[i - 1],
               sizeof(s->emoji.recent[0]));

    snprintf(s->emoji.recent[0], sizeof(s->emoji.recent[0]), "%s", keep);
    if (existing < 0 && s->emoji.recent_count < EMOJI_RECENT_MAX)
        s->emoji.recent_count++;

    emoji_recent_save(s);
}

/* ── The filtered view ───────────────────────────────────────
 *
 * filt[] holds indices into syn_emoji_table[], rebuilt whenever the search text
 * or the category changes. An index list rather than a copy: the table is 1779
 * entries of pointers and rebuilding it on every keystroke would be pointless
 * copying, and the picker only ever needs to know WHICH entries to draw.
 */
static void emoji_rebuild(syn_server_t *s)
{
    syn_emoji_panel_t *e = &s->emoji;
    e->filt_count = 0;

    /* Category 0 is "Recents", which is not a slice of the table — it is the
     * recents file, drawn from its own array. Nothing to filter. */
    if (e->cat == EMOJI_CAT_RECENT && !e->search[0]) {
        e->selected = 0;
        e->scroll   = 0;
        return;
    }

    /* Category 1 is "All"; 2.. are the blocks in syn_emoji_cats[]. */
    const char *want = NULL;
    if (e->cat >= EMOJI_CAT_FIRST_BLOCK)
        want = syn_emoji_cats[e->cat - EMOJI_CAT_FIRST_BLOCK];

    for (int i = 0; i < syn_emoji_count && e->filt_count < EMOJI_FILT_MAX; i++) {
        if (want && strcmp(syn_emoji_table[i].cat, want) != 0)
            continue;
        /* Names in the table are already lowercased (see emoji_data.c) and the
         * search box only accepts printable ASCII, lowercased on the way in —
         * so this is a plain strstr rather than a case-folding search over
         * 1779 strings on every keystroke. */
        if (e->search[0] && !strstr(syn_emoji_table[i].name, e->search))
            continue;
        e->filt[e->filt_count++] = i;
    }

    if (e->selected >= e->filt_count) e->selected = e->filt_count - 1;
    if (e->selected < 0)              e->selected = 0;
    e->scroll = 0;
}

int emoji_total(syn_server_t *s)
{
    if (s->emoji.cat == EMOJI_CAT_RECENT && !s->emoji.search[0])
        return s->emoji.recent_count;
    return s->emoji.filt_count;
}

/* The character at view index `i`, or NULL. The recents row and the filtered
 * table are different arrays, and this is the one place that knows it. */
const char *emoji_at(syn_server_t *s, int i)
{
    if (i < 0) return NULL;

    if (s->emoji.cat == EMOJI_CAT_RECENT && !s->emoji.search[0]) {
        if (i >= s->emoji.recent_count) return NULL;
        return s->emoji.recent[i];
    }
    if (i >= s->emoji.filt_count) return NULL;
    return syn_emoji_table[s->emoji.filt[i]].ch;
}

/* …and its name, for the status line. Recents have no name to hand: they are
 * stored as characters, not indices, precisely so the file stays readable and
 * survives the table being regenerated. Looked up rather than stored. */
const char *emoji_name_at(syn_server_t *s, int i)
{
    const char *ch = emoji_at(s, i);
    if (!ch) return "";

    if (s->emoji.cat == EMOJI_CAT_RECENT && !s->emoji.search[0]) {
        for (int j = 0; j < syn_emoji_count; j++)
            if (strcmp(syn_emoji_table[j].ch, ch) == 0)
                return syn_emoji_table[j].name;
        return "";
    }
    return syn_emoji_table[s->emoji.filt[i]].name;
}

const char *emoji_cat_label(int cat)
{
    if (cat == EMOJI_CAT_RECENT) return "Recent";
    if (cat == EMOJI_CAT_ALL)    return "All";
    int b = cat - EMOJI_CAT_FIRST_BLOCK;
    if (b < 0 || b >= syn_emoji_cat_count) return "";
    return syn_emoji_cats[b];
}

int emoji_cat_total(void)
{
    return EMOJI_CAT_FIRST_BLOCK + syn_emoji_cat_count;
}

/* ── Show / hide ─────────────────────────────────────────── */

void emoji_show(syn_server_t *s)
{
    emoji_recent_load(s);

    /* Open on Recents when there are any — the emoji you want next is far more
     * often one you have used than one you have to go looking for. With an
     * empty history that row would be a blank panel, so fall through to All. */
    s->emoji.cat       = s->emoji.recent_count > 0 ? EMOJI_CAT_RECENT
                                                   : EMOJI_CAT_ALL;
    s->emoji.search[0] = '\0';
    s->emoji.search_len = 0;
    s->emoji.selected  = 0;
    s->emoji.scroll    = 0;
    s->emoji.cat_hover = -1;
    emoji_rebuild(s);

    s->emoji.visible = 1;
    synui_render_emoji(s);
}

void emoji_hide(syn_server_t *s)
{
    s->emoji.visible = 0;
    s->emoji.cat_hover = -1;
    synui_render_emoji(s);
    ctlpanel_child_closed(s, "emoji");
}

void emoji_toggle(syn_server_t *s)
{
    if (s->emoji.visible) emoji_hide(s);
    else                  emoji_show(s);
}

/* ── Inserting ───────────────────────────────────────────────
 *
 * See the header for why both halves run and why the panel closes first.
 *
 * THE STRING IS NOT UNTRUSTED, but it is still quoted: these characters come
 * from a generated table, not from the network, so nothing here can inject a
 * shell command today. It is quoted anyway because synui_spawn() runs
 * /bin/sh -c, and "the table is generated so it is safe" is a property of the
 * generator that a future edit could quietly remove. Single quotes make the
 * shell take every byte literally, which is exactly right for UTF-8.
 */
static void emoji_insert(syn_server_t *s, const char *ch)
{
    if (!ch || !ch[0]) return;

    /* Own the string before anything below can move it. On the Recents tab
     * `ch` points into recent[], which emoji_recent_push() is about to rotate —
     * see the note there. Copying once here means the rest of this function
     * cannot develop the same bug later. */
    char own[16];
    snprintf(own, sizeof(own), "%s", ch);
    ch = own;

    char q[64];
    size_t qi = 0;
    q[qi++] = '\'';
    for (size_t i = 0; ch[i] && qi + 8 < sizeof(q); i++) {
        if (ch[i] == '\'') {
            q[qi++] = '\''; q[qi++] = '\\'; q[qi++] = '\''; q[qi++] = '\'';
        } else {
            q[qi++] = ch[i];
        }
    }
    q[qi++] = '\'';
    q[qi]   = '\0';

    emoji_recent_push(s, ch);
    emoji_hide(s);            /* focus goes back to the window FIRST */

    /* printf %s rather than echo: echo in /bin/sh appends a newline, which in a
     * terminal or a chat box submits the line the emoji was just added to. */
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "printf %%s %s | wl-copy", q);
    synui_spawn(cmd);

    snprintf(cmd, sizeof(cmd), "wtype %s", q);
    synui_spawn(cmd);

    wlr_log(WLR_DEBUG, "synui: emoji: inserted %s", ch);
}

/* ── Pointer ─────────────────────────────────────────────────
 * The panel pointer contract in synui.h. A grid, so this is the one panel that
 * uses hit_set_grid()/hit_col_at(); everything else about it matches curpick. */

/* Switch to category `c` and rebuild the view. The one path: Tab, Shift+Tab and
 * a click on a tab all land here, so a category can never be changed by one of
 * them in a way the others do not do. Keeps the search text — a category is a
 * narrowing of what you already typed, and clearing it would undo the typing
 * that got you this far. */
static void emoji_set_cat(syn_server_t *s, int c)
{
    int n = emoji_cat_total();
    if (n <= 0) return;
    s->emoji.cat      = ((c % n) + n) % n;
    s->emoji.selected = 0;
    emoji_rebuild(s);
    synui_render_emoji(s);
}

int emoji_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->emoji.visible) return 0;

    /* The category strip first: it sits above the grid, so a hit there is never
     * a cell, and only the hover highlight moves — see cat_hover in synui.h for
     * why pointing at a tab must not switch to it. */
    int tab = hit_spot_at(&s->emoji.hit, lx, ly);
    if (tab != s->emoji.cat_hover) {
        s->emoji.cat_hover = tab;
        synui_render_emoji(s);
    }
    if (tab >= 0) return 1;

    /* Hover DOES move the selection here, unlike the pickers whose selection
     * applies something — moving it costs a repaint and nothing else, and on a
     * grid of 1779 cells "what is this one called" is the question the status
     * line exists to answer. */
    int i = hit_index_at(&s->emoji.hit, lx, ly);
    if (i >= 0 && i < emoji_total(s) && i != s->emoji.selected) {
        s->emoji.selected = i;
        synui_render_emoji(s);
    }
    return 1;
}

int emoji_click(syn_server_t *s, double lx, double ly, uint32_t button,
                uint32_t time_msec)
{
    (void)time_msec;
    if (!s->emoji.visible) return 0;

    if (!hit_in_panel(&s->emoji.hit, lx, ly)) {
        emoji_hide(s);
        return 1;
    }
    if (button != BTN_LEFT) return 1;

    /* A category tab. Above the grid and tested first, exactly as in _motion. */
    int tab = hit_spot_at(&s->emoji.hit, lx, ly);
    if (tab >= 0) {
        emoji_set_cat(s, tab);
        return 1;
    }

    int i = hit_index_at(&s->emoji.hit, lx, ly);
    if (i < 0 || i >= emoji_total(s)) return 1;   /* chrome */

    /* A single click inserts. No double-click-to-commit as the other pickers
     * have: those preview on the first click and need a second to mean it,
     * whereas clicking an emoji has exactly one possible intention. */
    s->emoji.selected = i;
    emoji_insert(s, emoji_at(s, i));
    return 1;
}

int emoji_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->emoji.visible) return 0;
    if (delta == 0) return 1;

    int total = emoji_total(s);
    int rows  = (total + EMOJI_COLS - 1) / EMOJI_COLS;
    if (rows <= EMOJI_ROWS) return 1;

    s->emoji.scroll += delta > 0 ? 1 : -1;
    if (s->emoji.scroll > rows - EMOJI_ROWS) s->emoji.scroll = rows - EMOJI_ROWS;
    if (s->emoji.scroll < 0) s->emoji.scroll = 0;
    synui_render_emoji(s);
    return 1;
}

/* ── Keys ────────────────────────────────────────────────────
 *
 * Typing searches — there is no "press / to search" step, because on a grid of
 * 1779 cells searching is the primary way in and arrowing is the exception.
 * That costs the single-letter shortcuts every other panel has (no 'q' to
 * close, no 'r' to rescan); Esc does both jobs instead.
 */

/* Keep the selection inside the scrolled window. */
static void emoji_scroll_to_selection(syn_server_t *s)
{
    int row = s->emoji.selected / EMOJI_COLS;
    if (row < s->emoji.scroll)                 s->emoji.scroll = row;
    if (row >= s->emoji.scroll + EMOJI_ROWS)   s->emoji.scroll = row - EMOJI_ROWS + 1;
    if (s->emoji.scroll < 0)                   s->emoji.scroll = 0;
}

int emoji_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->emoji.visible) return 0;

    syn_emoji_panel_t *e = &s->emoji;
    int total = emoji_total(s);

    /* Super and Ctrl still belong to the compositor, exactly as in the control
     * panel's search box: those are how you leave, and a picker that swallowed
     * Super+C would trap you in it. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
        /* Esc clears the search before it closes the panel — a search that
         * narrowed to nothing is the state you most want out of, and losing the
         * whole panel to get out of it is a step too many. */
        if (e->search_len > 0) {
            e->search[0]  = '\0';
            e->search_len = 0;
            emoji_rebuild(s);
            synui_render_emoji(s);
            return 1;
        }
        emoji_hide(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        if (total > 0) emoji_insert(s, emoji_at(s, e->selected));
        else           emoji_hide(s);
        return 1;

    case XKB_KEY_BackSpace:
        if (e->search_len > 0) {
            e->search[--e->search_len] = '\0';
            emoji_rebuild(s);
            synui_render_emoji(s);
        }
        return 1;

    case XKB_KEY_Tab:
        emoji_set_cat(s, e->cat + 1);
        return 1;

    case XKB_KEY_ISO_Left_Tab:   /* Shift+Tab */
        emoji_set_cat(s, e->cat - 1);
        return 1;

    case XKB_KEY_Left:
        if (e->selected > 0) e->selected--;
        emoji_scroll_to_selection(s);
        synui_render_emoji(s);
        return 1;

    case XKB_KEY_Right:
        if (e->selected < total - 1) e->selected++;
        emoji_scroll_to_selection(s);
        synui_render_emoji(s);
        return 1;

    case XKB_KEY_Up:
        if (e->selected >= EMOJI_COLS) e->selected -= EMOJI_COLS;
        emoji_scroll_to_selection(s);
        synui_render_emoji(s);
        return 1;

    case XKB_KEY_Down:
        if (e->selected + EMOJI_COLS < total) e->selected += EMOJI_COLS;
        emoji_scroll_to_selection(s);
        synui_render_emoji(s);
        return 1;

    case XKB_KEY_Page_Up:
        e->selected -= EMOJI_COLS * EMOJI_ROWS;
        if (e->selected < 0) e->selected = 0;
        emoji_scroll_to_selection(s);
        synui_render_emoji(s);
        return 1;

    case XKB_KEY_Page_Down:
        e->selected += EMOJI_COLS * EMOJI_ROWS;
        if (e->selected > total - 1) e->selected = total - 1;
        if (e->selected < 0)         e->selected = 0;
        emoji_scroll_to_selection(s);
        synui_render_emoji(s);
        return 1;

    case XKB_KEY_Home:
        e->selected = 0;
        emoji_scroll_to_selection(s);
        synui_render_emoji(s);
        return 1;

    case XKB_KEY_End:
        e->selected = total > 0 ? total - 1 : 0;
        emoji_scroll_to_selection(s);
        synui_render_emoji(s);
        return 1;

    default:
        break;
    }

    /* Printable ASCII goes to the search box. Lowercased on the way in so the
     * match against the (already lowercased) Unicode names is a plain strstr —
     * see emoji_rebuild(). */
    if (sym >= 0x20 && sym <= 0x7e) {
        if (e->search_len < (int)sizeof(e->search) - 1) {
            char c = (char)sym;
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            e->search[e->search_len++] = c;
            e->search[e->search_len]   = '\0';

            /* Typing while on Recents means searching the whole set: the
             * recents row is a shortcut, not a place you look things up, and a
             * search that returned nothing because you happened to be on that
             * tab would look broken. */
            if (e->cat == EMOJI_CAT_RECENT) e->cat = EMOJI_CAT_ALL;

            e->selected = 0;
            emoji_rebuild(s);
            synui_render_emoji(s);
        }
        return 1;
    }

    /* Modal: swallow the rest rather than letting it reach the window under. */
    return 1;
}
