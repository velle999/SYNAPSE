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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <wlr/util/log.h>

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

/* ── Rebinding ───────────────────────────────────────────────
 *
 * F2 on the selected row arms a capture; the next chord becomes that shortcut's
 * key. The palette is the right home for it because the palette is already the
 * list: "find the shortcut, then change it" is one journey, and a rebind window
 * of its own would be a SECOND list of shortcuts — the exact thing
 * ctlpanel_shortcuts() exists to make impossible.
 *
 * The change is applied to the live table immediately (so the new key works
 * before you have let go of it) and persisted as a DIFF against the config as
 * it was loaded, in binds.state. A diff rather than a dump of the whole table:
 * dumping would freeze the bind set, so a shortcut added in a future synui
 * would never reach anyone who had ever rebound a key — the trap settings.c's
 * header describes, in its most annoying form.
 */

/* The bind table as synuirc and the defaults left it, snapshotted at the end of
 * every config load. Everything binds.state writes is measured against this.
 *
 * A file-scope copy rather than a field on syn_config_t because it is not
 * configuration — it is the baseline the configuration is compared to, and
 * putting it in the struct would mean every scratch config the control panel
 * makes carried a second bind table it never reads. */
static syn_bind_t g_base_binds[SYN_BINDS_MAX];
static int        g_base_count;

static int binds_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "binds.state") ? 1 : 0;
}

/* Is this exact chord→action mapping in the baseline? */
static bool base_holds(const syn_bind_t *b)
{
    for (int i = 0; i < g_base_count; i++)
        if (g_base_binds[i].mods == b->mods && g_base_binds[i].sym == b->sym &&
            strcmp(g_base_binds[i].action, b->action) == 0 &&
            strcmp(g_base_binds[i].arg,    b->arg)    == 0)
            return true;
    return false;
}

/* Is this chord bound to anything at all in the live table? */
static bool live_holds_combo(const syn_config_t *cfg, uint32_t mods,
                             xkb_keysym_t sym)
{
    for (int i = 0; i < cfg->bind_count; i++)
        if (cfg->binds[i].mods == mods && cfg->binds[i].sym == sym)
            return true;
    return false;
}

/*
 * Rewrite binds.state as the difference between the live table and the
 * baseline. Called after every accepted rebind; writes nothing but the
 * difference, so a user who has moved one key has a one-line file.
 *
 * Two kinds of line, and both are needed for a MOVE:
 *
 *   unbind = <combo>   a baseline chord that is now bound to nothing. Without
 *                      it the old key keeps working, because the defaults are
 *                      seeded before any file is read — so the shortcut would
 *                      answer to both keys and the rebind would look like it
 *                      half-applied.
 *   bind = <combo> …   a chord the baseline does not have, or has pointing
 *                      somewhere else.
 *
 * A chord that is bound in both, to different actions, needs only the `bind`
 * line: config_bind_set() replaces by chord.
 *
 * Through a temp file and rename(), like settings.state: truncating the real
 * one and dying mid-write would lose every rebind the user has ever made.
 */
static void binds_state_save(syn_server_t *s)
{
    char path[256];
    if (!binds_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    char tmp[288];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: binds: cannot write '%s': %s",
                tmp, strerror(errno));
        return;
    }

    fprintf(f,
        "# synui binds.state — written by the rebind helper (Super+/, then F2).\n"
        "# synuirc's own language, read back by the same parser, and applied\n"
        "# AFTER synuirc so it wins. Only the difference from the defaults and\n"
        "# your synuirc is here, so a shortcut added by a future synui still\n"
        "# arrives. DELETE THIS FILE to hand every key back.\n");

    int changed = 0;

    for (int i = 0; i < g_base_count; i++) {
        if (live_holds_combo(&s->config, g_base_binds[i].mods,
                             g_base_binds[i].sym))
            continue;
        char combo[64];
        syn_bind_format_combo(g_base_binds[i].mods, g_base_binds[i].sym,
                              combo, sizeof(combo));
        fprintf(f, "unbind = %s\n", combo);
        changed++;
    }

    for (int i = 0; i < s->config.bind_count; i++) {
        const syn_bind_t *b = &s->config.binds[i];
        if (base_holds(b)) continue;
        char combo[64];
        syn_bind_format_combo(b->mods, b->sym, combo, sizeof(combo));
        fprintf(f, "bind = %s %s%s%s\n", combo, b->action,
                b->arg[0] ? " " : "", b->arg);
        changed++;
    }

    fclose(f);

    /* An empty diff means every key is back where it started, and the right
     * thing to leave behind is NO FILE — not a file full of comments that
     * overrides nothing but looks like it might. That also makes "reset it all"
     * and "delete binds.state" the same operation. */
    if (changed == 0) {
        unlink(tmp);
        unlink(path);
        return;
    }

    if (rename(tmp, path) != 0) {
        wlr_log(WLR_ERROR, "synui: binds: cannot rename '%s': %s",
                path, strerror(errno));
        unlink(tmp);
    }
}

/*
 * Snapshot the baseline, then lay binds.state over it.
 *
 * Called from synui_config_load() after synuirc and the other state files, so
 * the table it snapshots is exactly "the defaults plus every file the user
 * wrote by hand" — which is what the diff above has to be measured against for
 * it to stay a diff.
 */
void binds_state_load(syn_config_t *cfg)
{
    g_base_count = cfg->bind_count;
    memcpy(g_base_binds, cfg->binds,
           (size_t)g_base_count * sizeof(g_base_binds[0]));

    char path[256];
    if (!binds_state_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;   /* nothing rebound — the baseline stands */

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        p[strcspn(p, "\r\n")] = '\0';
        if (!*p || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = p;
        char *end = eq - 1;
        while (end >= key && (*end == ' ' || *end == '\t')) *end-- = '\0';
        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;

        /* Only the two keys this file is allowed to carry. It is generated, so
         * anything else in it is corruption or a hand edit that meant to be in
         * synuirc — and applying an arbitrary key from here would make a
         * generated file a second place settings can hide. */
        if (strcmp(key, "bind") != 0 && strcmp(key, "unbind") != 0) {
            wlr_log(WLR_ERROR, "synui: binds.state: ignoring '%s'", key);
            continue;
        }
        config_parse_kv(cfg, key, val);
    }
    fclose(f);
}

/* Refuse a chord that would make the keyboard unusable.
 *
 * A bare printable key IS a legal bind — `print` is one, and the volume knob's
 * three XF86 keysyms are three more — but a bare letter or digit is not: bound
 * to `close`, `q` would close the focused window every time it was typed into
 * anything that is not taking the key itself, and the way back would be a text
 * editor. Modifiers make it deliberate; a key that produces no text is
 * deliberate by construction.
 *
 * Deliberately does NOT refuse chords that shadow a client's own shortcuts.
 * Super+C is the control panel and there is no way to know what the focused
 * window wanted it for; taking keys from applications is what a compositor
 * keybind IS. */
static bool combo_is_bindable(uint32_t mods, xkb_keysym_t sym)
{
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return true;
    /* Shift alone is not enough: shift+a is how a capital A is typed. */
    return !(sym >= 0x20 && sym <= 0x7e);
}

/* True for the keys that are only ever half of a chord. Held down waiting for
 * the other half, they arrive as presses of their own, and taking the first one
 * as the answer would make every capture come out as "Super". */
static bool sym_is_modifier(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Super_L:   case XKB_KEY_Super_R:
    case XKB_KEY_Control_L: case XKB_KEY_Control_R:
    case XKB_KEY_Alt_L:     case XKB_KEY_Alt_R:
    case XKB_KEY_Shift_L:   case XKB_KEY_Shift_R:
    case XKB_KEY_Meta_L:    case XKB_KEY_Meta_R:
    case XKB_KEY_Hyper_L:   case XKB_KEY_Hyper_R:
    case XKB_KEY_ISO_Level3_Shift:
    case XKB_KEY_Caps_Lock: case XKB_KEY_Num_Lock:
        return true;
    default:
        return false;
    }
}

static void keys_capture_cancel(syn_server_t *s)
{
    s->keys.capturing = 0;
}

/* Arm a capture on the selected row, or explain why the row cannot take one. */
static void keys_capture_begin(syn_server_t *s)
{
    syn_keys_t *k = &s->keys;
    const syn_ctl_shortcut_t *sc = keys_selected(s);
    if (!sc) return;

    if (!sc->rebindable) {
        /* The two shapes that are not one bind, told apart by whether the row
         * has an action: Super-tap has one and no chord, the collapsed
         * workspace rows have a chord range and no action. */
        snprintf(k->status, sizeof(k->status), "%s",
                 sc->action[0]
                     ? "Super-tap is not a bind — it is the absence of one"
                     : "That row stands for nine binds; rebind them in synuirc");
        synui_render_keys(s);
        return;
    }

    k->capturing    = 1;
    k->capture_all  = k->view[k->selected];
    k->capture_mods = sc->mods;
    k->capture_sym  = sc->sym;
    k->status[0]    = '\0';
    synui_render_keys(s);
}

/* The captured chord lands here. Applies it to the live table, persists the
 * diff, and rebuilds the list so the row redraws with its new key. */
static void keys_capture_finish(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_keys_t *k = &s->keys;
    syn_ctl_shortcut_t *sc = &k->all[k->capture_all];

    /* xkb reports the shifted symbol; the bind table stores the unshifted one
     * with SHIFT in the mask, which is how synuirc spells it and how
     * handle_keybinding matches. Without this, Super+Shift+/ would be stored as
     * `question` and never match the `slash` the keyboard actually sends. */
    sym = xkb_keysym_to_lower(sym);

    k->capturing = 0;

    if (!combo_is_bindable(mods, sym)) {
        snprintf(k->status, sizeof(k->status),
                 "Needs Super, Ctrl or Alt — a bare letter would type it");
        goto done;
    }

    if (mods == sc->mods && sym == sc->sym) {
        snprintf(k->status, sizeof(k->status), "Unchanged");
        goto done;
    }

    /* Already taken. Refuse rather than steal it: handle_keybinding takes the
     * FIRST match, so a silent overwrite would not even look like a conflict —
     * it would look like the other feature quietly going dead, which is the bug
     * seed_default_binds() shouts about duplicates to prevent. */
    for (int i = 0; i < s->config.bind_count; i++) {
        const syn_bind_t *b = &s->config.binds[i];
        if (b->mods != mods || b->sym != sym) continue;
        char combo[64];
        syn_bind_format_combo(mods, sym, combo, sizeof(combo));
        snprintf(k->status, sizeof(k->status), "%s is already %s",
                 combo, ctlpanel_action_desc(b->action, b->arg));
        goto done;
    }

    /* Both halves, in this order. The new bind first so a failure to add it
     * (a full table) leaves the old key working rather than leaving the
     * shortcut unreachable. */
    config_bind_set(&s->config, mods, sym, sc->action, sc->arg);
    if (!live_holds_combo(&s->config, mods, sym)) {
        snprintf(k->status, sizeof(k->status), "Bind table is full");
        goto done;
    }
    config_unbind_combo(&s->config, sc->mods, sc->sym);

    binds_state_save(s);

    {
        char combo[64];
        syn_bind_format_combo(mods, sym, combo, sizeof(combo));
        wlr_log(WLR_INFO, "synui: rebound %s -> %s", sc->desc, combo);
    }

    /* Re-snapshot: the row the cursor is on has a new combo string, and every
     * other row is unchanged, so rebuilding from the live table is both the
     * simplest way to redraw it and the only one that cannot disagree with what
     * was actually bound. The query and the cursor survive it. */
    {
        int sel = k->selected, scroll = k->scroll;
        k->n = ctlpanel_shortcuts(s, k->all, KEYS_MAX);
        keys_filter(s);
        k->selected = sel < k->n_view ? sel : (k->n_view ? k->n_view - 1 : 0);
        k->scroll   = scroll;
        keys_scroll_to_selection(s);
    }

    {
        char combo[64];
        syn_bind_format_combo(mods, sym, combo, sizeof(combo));
        snprintf(k->status, sizeof(k->status), "Rebound to %s", combo);
    }

done:
    synui_render_keys(s);
}

/* Put every shortcut back: delete binds.state and reload. Reload rather than
 * un-applying the diff by hand — synui_config_reload() is the one path that
 * knows how to re-seat everything a config change touches, and a second
 * implementation of "go back to the defaults" would be free to miss a step. */
static void keys_reset_all(syn_server_t *s)
{
    char path[256];
    if (binds_state_path(path, sizeof(path)))
        unlink(path);

    synui_config_reload(s);

    syn_keys_t *k = &s->keys;
    k->n = ctlpanel_shortcuts(s, k->all, KEYS_MAX);
    keys_filter(s);
    snprintf(k->status, sizeof(k->status), "Every shortcut back to default");
    synui_render_keys(s);
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
    /* An armed capture must not survive a close: the panel comes back with the
     * cursor on a different row, and the next key pressed would land on
     * whatever that is. */
    k->capturing = 0;
    k->status[0] = '\0';
    keys_filter(s);
    synui_render_keys(s);
}

void keys_hide(syn_server_t *s)
{
    s->keys.visible   = 0;
    s->keys.capturing = 0;
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

    /* A capture is armed against ONE row. Letting the pointer move the cursor
     * while it is would leave the highlight on a shortcut the next chord is not
     * going to touch — and the panel would have said "press the new key for
     * this one" about a row that is no longer under it. Still swallowed, so the
     * pointer does not fall through to the desktop. */
    if (k->capturing) return 1;

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

    /* A click while armed cancels the capture rather than running the row: the
     * panel is asking for a key, and the honest answer to a mouse button is
     * "that is not one". Clicking away still closes, below. */
    if (k->capturing && hit_in_panel(&k->hit, lx, ly)) {
        keys_capture_cancel(s);
        snprintf(k->status, sizeof(k->status), "Rebind cancelled");
        synui_render_keys(s);
        return 1;
    }

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

    /* ── Capture mode ────────────────────────────────────────
     *
     * FIRST, and above the Super/Alt passthrough below. While a capture is
     * armed EVERY chord is the answer, including the compositor's own: moving a
     * shortcut onto Super+K has to be possible, and letting Super through here
     * would run whatever Super+K currently does instead of capturing it.
     *
     * That is also why Esc is the only escape: with every other key taken as
     * input, an armed capture that could not be cancelled would be a trap. */
    if (k->capturing) {
        /* A held modifier arrives as a press of its own while you reach for the
         * other half of the chord. Ignore them, or every capture comes out as
         * "Super". */
        if (sym_is_modifier(sym)) return 1;

        if (sym == XKB_KEY_Escape) {
            keys_capture_cancel(s);
            snprintf(k->status, sizeof(k->status), "Rebind cancelled");
            synui_render_keys(s);
            return 1;
        }
        keys_capture_finish(s, sym, mods);
        return 1;
    }

    /* The last rebind's outcome is about the key you just pressed, so the next
     * one retires it. Cleared before dispatching, not after: the handlers below
     * are what set the next message. */
    k->status[0] = '\0';

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
        /* R for rebind, and Ctrl+Shift+R to put everything back. The pair is
         * here as well as on F2 because F2 is a long reach from a search box
         * and half the keyboards this runs on need Fn to get to it.
         *
         * Shift is tested on the SYMBOL, not the mask: xkb reports Ctrl+Shift+r
         * as XKB_KEY_R, and matching on WLR_MODIFIER_SHIFT alone would make
         * every Ctrl+R a reset on a keyboard with caps lock on. */
        case XKB_KEY_r:
            keys_capture_begin(s); return 1;
        case XKB_KEY_R:
            keys_reset_all(s); return 1;
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

    /* The rebind key everything else in the world uses for "rename this".
     * Cannot be a letter: the box types letters. */
    case XKB_KEY_F2:
        keys_capture_begin(s);
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
