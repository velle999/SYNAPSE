/*
 * keys.c — the shortcut palette (Super+/, and Super+? for the same key without
 * having to notice the Shift).
 *
 * synui has grown past forty keybindings, and the only place that listed them
 * was the control panel's Shortcuts category: a page you have to open the
 * control panel and arrow down to, that scrolls but does not filter, and that
 * cannot run anything it lists. Fine as documentation, no use as the thing you
 * reach for when you know synui does something and cannot remember which key.
 *
 * So this is that list as a palette. One key from anywhere, type to narrow it,
 * Enter to run the shortcut you landed on. Typing is the whole interaction —
 * there is no mode to enter first, which is why the navigation keys here are
 * Up/Down and Ctrl+N/P rather than the j/k every other panel uses: j and k have
 * to type a j and a k.
 *
 * It is NOT a second list of shortcuts. Both this and the control panel's
 * column come out of ctlpanel_shortcuts(), which reads the live bind table —
 * the reason that function exists is that a hand-maintained shortcut list is a
 * list that drifts, and this project has already shipped that bug once, in the
 * waybar start menu. A bind added in config.c appears here with no edit.
 *
 * Enter runs through synui_binding_execute(), the same path a keypress takes,
 * so a row cannot do something subtly different from the key it is describing.
 * The panel hides FIRST: half the actions are panel toggles, and running one
 * with the palette still up would leave two modal panels stacked, with this one
 * on top swallowing the keys meant for the panel it just opened.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "synui.h"

/* ── Filtering ───────────────────────────────────────────────
 *
 * Case-insensitive substring, over the combo AND the description, so both
 * "super+w" and "wallpaper" find the wallpaper picker. Multiple words are
 * ANDed and order-independent ("float super" matches "Super+F  Float window"),
 * because the alternative is having to remember which column comes first.
 */

/* strcasestr() without the GNU extension's locale surprises, on strings this
 * short. Returns 1 for a match, and for an empty needle. */
static int fold_contains(const char *hay, const char *needle, size_t nlen)
{
    if (nlen == 0) return 1;
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen) return 1;
        if (!p[i]) break;   /* the haystack ran out mid-compare */
    }
    return 0;
}

static int keys_matches(const syn_ctl_shortcut_t *sc, const char *query)
{
    /* Walk the query a whitespace-separated word at a time; every word has to
     * be found somewhere in either column. */
    const char *w = query;
    for (;;) {
        while (*w == ' ') w++;
        if (!*w) return 1;
        const char *end = w;
        while (*end && *end != ' ') end++;
        size_t len = (size_t)(end - w);

        if (!fold_contains(sc->combo, w, len) &&
            !fold_contains(sc->desc,  w, len))
            return 0;
        w = end;
    }
}

/* Rebuild view[] from all[] under the current query, and keep the cursor on
 * something. Called on every edit — the list is at most ~50 rows, so there is
 * nothing to be clever about.
 *
 * The cursor goes back to the top rather than trying to follow the row it was
 * on: after a keystroke the row under it is a different shortcut, and a
 * selection that lands on whatever happens to be at index 7 is worse than one
 * that is predictably at the top of the results. */
static void keys_filter(syn_server_t *s)
{
    syn_keys_t *k = &s->keys;

    k->n_view = 0;
    for (int i = 0; i < k->n; i++)
        if (keys_matches(&k->all[i], k->query))
            k->view[k->n_view++] = i;

    k->selected = 0;
    k->scroll   = 0;
}

/* Keep the selection inside the drawn window. */
static void keys_scroll_to_selection(syn_server_t *s)
{
    syn_keys_t *k = &s->keys;

    if (k->selected < k->scroll)
        k->scroll = k->selected;
    if (k->selected > k->scroll + KEYS_ROWS - 1)
        k->scroll = k->selected - (KEYS_ROWS - 1);

    int max_scroll = k->n_view - KEYS_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    if (k->scroll > max_scroll) k->scroll = max_scroll;
    if (k->scroll < 0)          k->scroll = 0;
}

static void keys_move(syn_server_t *s, int delta)
{
    syn_keys_t *k = &s->keys;
    if (k->n_view == 0) return;

    k->selected += delta;
    if (k->selected < 0)           k->selected = 0;
    if (k->selected >= k->n_view)  k->selected = k->n_view - 1;
    keys_scroll_to_selection(s);
}

/* ── Running one ─────────────────────────────────────────── */

/* The rows with no action behind them — Super-tap has one, the two collapsed
 * workspace rows do not (see ctlpanel_shortcuts). */
static const syn_ctl_shortcut_t *keys_selected(syn_server_t *s)
{
    syn_keys_t *k = &s->keys;
    if (k->selected < 0 || k->selected >= k->n_view) return NULL;
    return &k->all[k->view[k->selected]];
}

static void keys_activate(syn_server_t *s)
{
    const syn_ctl_shortcut_t *sc = keys_selected(s);
    if (!sc || !sc->action[0]) return;

    /* Copy before hiding: keys_hide() does not clear all[], but the action
     * about to run can reload the config, and a pointer into a table that a
     * reload rebuilds is not worth the risk for two small strings. */
    char action[SYN_BIND_ACTION_LEN], arg[SYN_BIND_ARG_LEN];
    snprintf(action, sizeof(action), "%s", sc->action);
    snprintf(arg,    sizeof(arg),    "%s", sc->arg);

    /* Down first — see the file header: most of these open a panel, and this
     * one must not still be on top of it. */
    keys_hide(s);
    synui_binding_execute(s, action, arg);
}

/* ── Panel ───────────────────────────────────────────────── */

void keys_show(syn_server_t *s)
{
    syn_keys_t *k = &s->keys;

    /* Snapshotted per open, so a `bind =` added to synuirc and reloaded shows
     * up the next time the palette is opened rather than at the next login. */
    k->n = ctlpanel_shortcuts(s, k->all, KEYS_MAX);

    k->query[0]  = '\0';
    k->query_len = 0;
    k->visible   = 1;
    keys_filter(s);
    synui_render_keys(s);
}

void keys_hide(syn_server_t *s)
{
    s->keys.visible = 0;
    synui_render_keys(s);
    ctlpanel_child_closed(s, "keys");
}

void keys_toggle(syn_server_t *s)
{
    if (s->keys.visible) keys_hide(s);
    else                 keys_show(s);
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h. A left click on a row runs it,
 * which is what Enter does — a list of things you can do has only ever meant
 * one thing by a click. */

int keys_motion(syn_server_t *s, double lx, double ly)
{
    syn_keys_t *k = &s->keys;
    if (!k->visible) return 0;

    int idx = hit_index_at(&k->hit, lx, ly);
    if (idx < 0 || idx >= k->n_view || idx == k->selected) return 1;
    k->selected = idx;
    synui_render_keys(s);
    return 1;
}

int keys_click(syn_server_t *s, double lx, double ly, uint32_t button,
               uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    syn_keys_t *k = &s->keys;
    if (!k->visible) return 0;

    if (!hit_in_panel(&k->hit, lx, ly)) {
        keys_hide(s);
        return 1;
    }

    keys_motion(s, lx, ly);

    if (button != BTN_LEFT) return 1;
    if (hit_index_at(&k->hit, lx, ly) < 0) return 1;   /* chrome */

    keys_activate(s);
    return 1;
}

int keys_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->keys.visible) return 0;
    if (delta == 0) return 1;

    keys_move(s, delta > 0 ? 1 : -1);
    synui_render_keys(s);
    return 1;
}

/* ── Keys ────────────────────────────────────────────────────
 *
 * Modal for everything except Super and Ctrl-with-a-letter-that-is-not-N/P.
 * Super has to stay live: Super+/ is how you close this, and Super+C has to
 * still reach the control panel — a search box that swallowed the compositor's
 * own chords would be a box you are stuck in.
 *
 * Shift IS claimed, unlike most panels: the box types capitals. Super+Shift+…
 * still falls through, because the LOGO test below runs first.
 */
int keys_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_keys_t *k = &s->keys;
    if (!k->visible) return 0;

    /* Super and Alt combos belong to the compositor, always. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT))
        return 0;

    if (mods & WLR_MODIFIER_CTRL) {
        /* The two readline moves, so the hands never leave the letters to walk
         * the list. Anything else with Ctrl held is not ours. */
        switch (sym) {
        case XKB_KEY_n: case XKB_KEY_N:
            keys_move(s, +1); synui_render_keys(s); return 1;
        case XKB_KEY_p: case XKB_KEY_P:
            keys_move(s, -1); synui_render_keys(s); return 1;
        default:
            return 0;
        }
    }

    switch (sym) {
    case XKB_KEY_Escape:
        /* One level of backing out before the panel closes: a query that found
         * nothing is the case where you want the list back, not the desktop. */
        if (k->query_len > 0) {
            k->query[0]  = '\0';
            k->query_len = 0;
            keys_filter(s);
            synui_render_keys(s);
            return 1;
        }
        keys_hide(s);
        return 1;

    case XKB_KEY_BackSpace:
        /* Deliberately does NOT close the panel when the query is empty. The
         * control panel's search box does, because there the box is a mode over
         * a list that is still there underneath; here the box IS the panel, and
         * one backspace too many closing it would be a keystroke that throws
         * away what you were reading. */
        if (k->query_len > 0) {
            k->query[--k->query_len] = '\0';
            keys_filter(s);
            synui_render_keys(s);
        }
        return 1;

    case XKB_KEY_Up:      keys_move(s, -1);          synui_render_keys(s); return 1;
    case XKB_KEY_Down:    keys_move(s, +1);          synui_render_keys(s); return 1;
    case XKB_KEY_Page_Up: keys_move(s, -KEYS_ROWS);  synui_render_keys(s); return 1;
    case XKB_KEY_Page_Down: keys_move(s, +KEYS_ROWS); synui_render_keys(s); return 1;
    case XKB_KEY_Home:    keys_move(s, -k->n_view);  synui_render_keys(s); return 1;
    case XKB_KEY_End:     keys_move(s, +k->n_view);  synui_render_keys(s); return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        keys_activate(s);
        return 1;

    /* Tab would be the obvious "next row" — it is also a shortcut the list
     * describes (Alt+Tab), and stepping the cursor is the more useful of the
     * two here. Shift+Tab goes back, as it does everywhere else. */
    case XKB_KEY_Tab:      keys_move(s, +1); synui_render_keys(s); return 1;
    case XKB_KEY_ISO_Left_Tab:
                           keys_move(s, -1); synui_render_keys(s); return 1;

    default:
        break;
    }

    /* Printable ASCII types. Both columns are ASCII by construction (xkb key
     * names and our own descriptions), so anything outside this range could not
     * match a row anyway — and letting it through would put bytes into a buffer
     * that goes to cairo. */
    if (sym >= 0x20 && sym <= 0x7e) {
        if (k->query_len < KEYS_QUERY_MAX - 1) {
            k->query[k->query_len++] = (char)sym;
            k->query[k->query_len]   = '\0';
            keys_filter(s);
            synui_render_keys(s);
        }
        return 1;
    }

    return 1;   /* modal: swallow everything else while open */
}
