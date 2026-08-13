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
/* And the tap key's baseline, snapshotted with them. The tap is not in the bind
 * table — it is one modifier mask — but it is rebound through the same helper
 * and has to go back on the same reset, so it is part of the same diff. */
static uint32_t   g_base_tap;
/* And what the tap RUNS, for the same reason: `tap_action` is rebound from the
 * same panels and has to come back on the same Ctrl+Shift+R. */
static char       g_base_tap_action[SYN_BIND_ACTION_LEN];
static char       g_base_tap_arg[SYN_BIND_ARG_LEN];

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
 *   tap_key = <mod>    the third line, and the only one that is not about a
 *                      chord — the modifier whose tap opens the start menu. It
 *                      is written the same way and for the same reason: a diff,
 *                      so a user who moved the tap onto Alt still gets whatever
 *                      the next synui makes the default of everything else.
 *   tap_action = …     the fourth, and the tap's other half: what it runs. Same
 *                      diff rule — a user who pointed the tap at rofi has said
 *                      nothing about the rest of the table.
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

    /* The tap, if it has moved. Written even when it has moved to `none`: "off"
     * is a decision, and the empty-file rule below would otherwise be unable to
     * tell it from "never touched". */
    if (s->config.tap_mod != g_base_tap) {
        fprintf(f, "tap_key = %s\n", syn_tap_mod_name(s->config.tap_mod));
        changed++;
    }

    /* The fourth line, and the tap's other half: what it opens. Written whole —
     * action and argument — because `spawn rofi -show drun` is one setting, and
     * because the parser this file is read back by splits them the same way. */
    if (strcmp(s->config.tap_action, g_base_tap_action) != 0 ||
        strcmp(s->config.tap_arg,    g_base_tap_arg)    != 0) {
        fprintf(f, "tap_action = %s%s%s\n", s->config.tap_action,
                s->config.tap_arg[0] ? " " : "", s->config.tap_arg);
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
    g_base_tap = cfg->tap_mod;
    snprintf(g_base_tap_action, sizeof(g_base_tap_action), "%s", cfg->tap_action);
    snprintf(g_base_tap_arg,    sizeof(g_base_tap_arg),    "%s", cfg->tap_arg);

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

        /* Only the four keys this file is allowed to carry. It is generated,
         * so anything else in it is corruption or a hand edit that meant to be
         * in synuirc — and applying an arbitrary key from here would make a
         * generated file a second place settings can hide. */
        if (strcmp(key, "bind")       != 0 && strcmp(key, "unbind") != 0 &&
            strcmp(key, "tap_key")    != 0 &&
            strcmp(key, "tap_action") != 0) {
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
 * as the answer would make every capture come out as "Super".
 *
 * Wider than syn_tap_mod_from_sym()'s four, deliberately: that one answers
 * "which modifier may the tap be moved to", and this one answers "is this key
 * still the half of a chord you are holding down" — Caps Lock and the level-3
 * shift are the second and not the first. */
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

/* ── The rebind core, shared with the control panel ──────────
 *
 * The four functions below are the whole of what a rebind MEANS — which chords
 * are legal, what is refused and why, what gets written, and how "put it all
 * back" works. They know nothing about either panel.
 *
 * They are exported because the control panel's Shortcuts category rebinds too,
 * and it must not do it a second way. The palette and that pane already share
 * the shortcut LIST (ctlpanel_shortcuts, which exists precisely so there is no
 * hand-kept copy of the bind table); sharing the list while forking the rules
 * would be the same bug one layer down — the refusals are the subtle half here,
 * and two copies of "a bare letter is not bindable" is one copy that will
 * eventually let `q` through and close a window on every keystroke.
 *
 * What each panel keeps for itself is only its own capture UI state: which row
 * is armed, and where to put the status line.
 */

/*
 * Should an armed capture throw this keysym away rather than take it as the
 * answer? See the header for why this takes the row and not just the keysym.
 */
bool syn_rebind_capture_ignores(const syn_ctl_shortcut_t *sc, xkb_keysym_t sym)
{
    if (sc && sc->tap) return false;
    return sym_is_modifier(sym);
}

/*
 * Why this row cannot take a capture, or NULL if it can.
 *
 * One shape is left: the collapsed workspace rows, which stand for nine binds
 * each and name none of them. The tap row used to be the other — it has no
 * chord at all — but "no chord" is what it IS rather than a reason it cannot be
 * moved, and it is now rebindable to another modifier through the tap branch of
 * syn_rebind_apply().
 */
const char *syn_rebind_refusal(const syn_ctl_shortcut_t *sc)
{
    if (!sc) return "No shortcut selected";
    if (sc->rebindable) return NULL;
    return "That row stands for nine binds; rebind them in synuirc";
}

/*
 * The tap row's half of syn_rebind_apply(), which is a different question from
 * the chord one all the way down: the answer is ONE modifier rather than a
 * mods+sym pair, it cannot collide with anything (every chord in the table has
 * a non-modifier key), and it has an "off" — which the chords do not, because a
 * chord you want gone is unbound in synuirc rather than captured.
 *
 * Delete/Backspace is that off switch. It is the one key a capture can be given
 * that is neither a modifier nor a mistake: the row is asking "which key", and
 * "none of them" has to be sayable, or the only way to stop a tapped Super
 * opening the menu would be to hand-edit synuirc.
 */
static int rebind_apply_tap(syn_server_t *s, const syn_ctl_shortcut_t *sc,
                            xkb_keysym_t sym, char *status, size_t status_n)
{
    uint32_t mod;

    if (sym == XKB_KEY_Delete || sym == XKB_KEY_KP_Delete ||
        sym == XKB_KEY_BackSpace) {
        mod = 0;
    } else if ((mod = syn_tap_mod_from_sym(sym)) == 0) {
        snprintf(status, status_n,
                 "Tap Super, Ctrl, Alt or Shift — or Delete for no tap");
        return 0;
    }

    if (mod == sc->mods) {
        snprintf(status, status_n, "Unchanged");
        return 0;
    }

    s->config.tap_mod = mod;
    binds_state_save(s);

    wlr_log(WLR_INFO, "synui: start-menu tap -> %s", syn_tap_mod_name(mod));
    if (mod)
        snprintf(status, status_n, "Start menu opens on a %s tap",
                 ctlpanel_tap_key_name(mod));
    else
        snprintf(status, status_n, "Start menu no longer opens on a tap");
    return 1;
}

/*
 * Apply a captured chord to `sc`. Returns 1 if the bind table actually changed,
 * 0 if the chord was refused or was the one already there; `status` gets the
 * human explanation in both cases.
 *
 * `sc` MUST NOT point into s->config.binds[]: this rewrites that table, and a
 * pointer into it is stale from the first line that does. Both callers pass a
 * row out of their own snapshot of the shortcut list, which is what makes that
 * safe — the palette's all[], the control panel's stack copy.
 */
int syn_rebind_apply(syn_server_t *s, const syn_ctl_shortcut_t *sc,
                     xkb_keysym_t sym, uint32_t mods,
                     char *status, size_t status_n)
{
    const char *refusal = syn_rebind_refusal(sc);
    if (refusal) {
        snprintf(status, status_n, "%s", refusal);
        return 0;
    }

    /* The tap is not a chord and none of the rules below are about it — the
     * modifiers held while it was captured are the answer itself, not context
     * for a key. */
    if (sc->tap)
        return rebind_apply_tap(s, sc, sym, status, status_n);

    /* xkb reports the shifted symbol; the bind table stores the unshifted one
     * with SHIFT in the mask, which is how synuirc spells it and how
     * handle_keybinding matches. Without this, Super+Shift+/ would be stored as
     * `question` and never match the `slash` the keyboard actually sends. */
    sym = xkb_keysym_to_lower(sym);

    if (!combo_is_bindable(mods, sym)) {
        snprintf(status, status_n,
                 "Needs Super, Ctrl or Alt — a bare letter would type it");
        return 0;
    }

    if (mods == sc->mods && sym == sc->sym) {
        snprintf(status, status_n, "Unchanged");
        return 0;
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
        snprintf(status, status_n, "%s is already %s",
                 combo, ctlpanel_action_desc(b->action, b->arg));
        return 0;
    }

    /* Both halves, in this order. The new bind first so a failure to add it
     * (a full table) leaves the old key working rather than leaving the
     * shortcut unreachable. */
    config_bind_set(&s->config, mods, sym, sc->action, sc->arg);
    if (!live_holds_combo(&s->config, mods, sym)) {
        snprintf(status, status_n, "Bind table is full");
        return 0;
    }
    config_unbind_combo(&s->config, sc->mods, sc->sym);

    binds_state_save(s);

    char combo[64];
    syn_bind_format_combo(mods, sym, combo, sizeof(combo));
    wlr_log(WLR_INFO, "synui: rebound %s -> %s", sc->desc, combo);
    snprintf(status, status_n, "Rebound to %s", combo);
    return 1;
}

/*
 * Point the tap at this row's action — F3 in both panels.
 *
 * The tap is the one shortcut with TWO things to choose: which key it is, and
 * what it does. syn_rebind_apply() above answers the first by capturing a
 * modifier; this answers the second, and it does it by naming a ROW rather than
 * by capturing anything, because an action is not a keystroke — the list on
 * screen already names every action this desktop has, so "put the tap on that
 * one" is a cursor and a keypress instead of a picker nobody would find.
 *
 * That the row's action is copied and not referenced is the point: the tap gets
 * a value, not a link, so moving a chord later does not drag the tap with it.
 *
 * It also TAKES the row's chord away (pkgrel 339). The tap is a key too, so
 * leaving the old one bound answers the same feature on two keys — and the one
 * it leaves occupied is a chord the user now has no use for and cannot rebind,
 * because syn_rebind_apply() refuses a combo that is already taken. That is
 * exactly how velle's Super+Space stayed on rofi after the tap was pointed at
 * it. The escape hatch is Ctrl+Shift+R, which puts every shortcut back.
 */
int syn_rebind_set_tap_action(syn_server_t *s, const syn_ctl_shortcut_t *sc,
                              char *status, size_t status_n)
{
    if (!sc || !sc->action[0]) {
        /* The collapsed workspace rows: nine binds, no single action to copy.
         * Same refusal as F2 makes on them, for the same reason. */
        snprintf(status, status_n,
                 "That row names no single action to put on the tap");
        return 0;
    }

    if (sc->tap) {
        snprintf(status, status_n,
                 "That IS the tap — pick the row you want it to open");
        return 0;
    }

    int changed = strcmp(s->config.tap_action, sc->action) != 0 ||
                  strcmp(s->config.tap_arg,    sc->arg)    != 0;

    /* Take the chord, but ONLY once there is a tap to take it onto. With
     * `tap_key = none` the tap runs nothing at all, so freeing the chord here
     * would leave the feature on no key whatever — the action would still be
     * spelled out in binds.state and be unreachable from the keyboard, which is
     * a worse answer than the duplicate this is here to remove. The status line
     * below already sends them to F2 on the tap row; the chord goes when they
     * come back. */
    int freed = 0;
    if (s->config.tap_mod && sc->sym != XKB_KEY_NoSymbol)
        freed = config_unbind_combo(&s->config, sc->mods, sc->sym);

    /* Twice on the same row is only "Unchanged" if there was also no chord left
     * to take. A tap that already opens this row's action while the row keeps
     * its own key is the very state this change exists to clear up, and F3 is
     * the key that clears it — refusing there would leave no way out of it. */
    if (!changed && !freed) {
        snprintf(status, status_n, "Unchanged");
        return 0;
    }

    snprintf(s->config.tap_action, sizeof(s->config.tap_action), "%s", sc->action);
    snprintf(s->config.tap_arg,    sizeof(s->config.tap_arg),    "%s", sc->arg);
    binds_state_save(s);

    wlr_log(WLR_INFO, "synui: tap action -> %s %s%s%s", sc->action, sc->arg,
            freed ? ", freed " : "", freed ? sc->combo : "");

    /* Named by what it opens, not by the action word: "spawn rofi -show drun"
     * is not what the row said, and the message has to be recognisable as the
     * line the cursor was on. The freed chord is named for a second reason —
     * the row it was on has just vanished from the list under the cursor, and a
     * disappearing line needs to be accounted for. */
    if (freed && changed)
        snprintf(status, status_n, "A %s tap now opens %s — %s freed",
                 ctlpanel_tap_key_name(s->config.tap_mod), sc->desc, sc->combo);
    else if (freed)
        snprintf(status, status_n, "%s freed — the %s tap already opens %s",
                 sc->combo, ctlpanel_tap_key_name(s->config.tap_mod), sc->desc);
    else if (s->config.tap_mod)
        snprintf(status, status_n, "A %s tap now opens %s",
                 ctlpanel_tap_key_name(s->config.tap_mod), sc->desc);
    else
        snprintf(status, status_n,
                 "Tap set to %s — no tap key yet, press F2 on the tap row",
                 sc->desc);
    return 1;
}

/* Put every shortcut back: delete binds.state and reload. Reload rather than
 * un-applying the diff by hand — synui_config_reload() is the one path that
 * knows how to re-seat everything a config change touches, and a second
 * implementation of "go back to the defaults" would be free to miss a step. */
void syn_rebind_reset_all(syn_server_t *s, char *status, size_t status_n)
{
    char path[256];
    if (binds_state_path(path, sizeof(path)))
        unlink(path);

    synui_config_reload(s);
    snprintf(status, status_n, "Every shortcut back to default");
}

/* ── The palette's own capture state ─────────────────────── */

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

    const char *refusal = syn_rebind_refusal(sc);
    if (refusal) {
        snprintf(k->status, sizeof(k->status), "%s", refusal);
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

/* The captured chord lands here. The rules are syn_rebind_apply()'s; what is
 * left here is disarming the capture and putting the list back on screen. */
static void keys_capture_finish(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_keys_t *k = &s->keys;
    /* By value: syn_rebind_apply() rewrites the bind table, and re-snapshotting
     * below overwrites all[] — so a pointer into it would not survive either. */
    syn_ctl_shortcut_t sc = k->all[k->capture_all];

    k->capturing = 0;

    if (syn_rebind_apply(s, &sc, sym, mods, k->status, sizeof(k->status))) {
        /* Re-snapshot: the row the cursor is on has a new combo string, and
         * every other row is unchanged, so rebuilding from the live table is
         * both the simplest way to redraw it and the only one that cannot
         * disagree with what was actually bound. Cursor and query survive it. */
        int sel = k->selected, scroll = k->scroll;
        k->n = ctlpanel_shortcuts(s, k->all, KEYS_MAX);
        keys_filter(s);
        k->selected = sel < k->n_view ? sel : (k->n_view ? k->n_view - 1 : 0);
        k->scroll   = scroll;
        keys_scroll_to_selection(s);
    }

    synui_render_keys(s);
}

/* F3: hand the selected row's action to the modifier tap. No capture — the row
 * IS the answer — so unlike a rebind this takes effect on the keypress. The
 * list is re-snapshotted because the tap row's own text changes: it names what
 * it opens, and that is what just moved. */
static void keys_tap_action_set(syn_server_t *s)
{
    syn_keys_t *k = &s->keys;
    const syn_ctl_shortcut_t *sel = keys_selected(s);
    if (!sel) return;

    /* By value, for syn_rebind_apply()'s reason one step removed: the re-snapshot
     * below overwrites all[], and `sel` points into it. */
    syn_ctl_shortcut_t sc = *sel;

    if (syn_rebind_set_tap_action(s, &sc, k->status, sizeof(k->status))) {
        int save_sel = k->selected, scroll = k->scroll;
        k->n = ctlpanel_shortcuts(s, k->all, KEYS_MAX);
        keys_filter(s);
        k->selected = save_sel < k->n_view ? save_sel
                                           : (k->n_view ? k->n_view - 1 : 0);
        k->scroll   = scroll;
        keys_scroll_to_selection(s);
    }
    synui_render_keys(s);
}

static void keys_reset_all(syn_server_t *s)
{
    syn_keys_t *k = &s->keys;

    syn_rebind_reset_all(s, k->status, sizeof(k->status));

    k->n = ctlpanel_shortcuts(s, k->all, KEYS_MAX);
    keys_filter(s);
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
         * "Super" — except on the tap row, where the modifier is the answer. */
        if (syn_rebind_capture_ignores(&k->all[k->capture_all], sym)) return 1;

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

    /* F3 puts the SELECTED row on the modifier tap. Next to F2 because it is
     * the same edit seen from the other side — F2 answers "which key runs this
     * row", F3 answers "which row does the tap run" — and it needs no capture
     * at all: the row under the cursor is the whole answer. */
    case XKB_KEY_F3:
        keys_tap_action_set(s);
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
