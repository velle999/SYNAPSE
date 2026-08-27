/*
 * kbdlayout.c — which keyboard layout the desktop, the LOCK screen and the
 * LOGIN screen are typing in.
 *
 * `xkb_layout = us,no` has always compiled into a keymap with two groups, and
 * xkb's own `grp:` options have always been able to walk between them. What did
 * not exist was any way to SEE which one you were on, or to change it without
 * having guessed the right xkb option — and the one screen where that matters
 * most is the one screen with nothing on it to ask: the login screen, where a
 * wrong layout is indistinguishable from a wrong password. You type the
 * password, it is rejected, and nothing anywhere says the `y` you pressed went
 * in as a `z`.
 *
 * So this is deliberately not a second keymap mechanism. The keymap is still
 * compiled once, by keyboard_apply_config() in input.c, out of the same
 * synuirc fields; all this file does is
 *
 *   - name the groups that keymap already has, and
 *   - move every keyboard's LOCKED LAYOUT to one of them.
 *
 * ⚠ THE KEYMAP IS THE TRUTH, NOT THE CONFIG STRING. `xkb_layout = us,zz` gives
 * a keymap with ONE group (xkb drops what it cannot resolve), and a selector
 * that offered two would put the seat on a group the keymap does not have —
 * xkb clamps that silently, so the chip would say `zz` while the keys stayed
 * `us`. Counting comes off the keymap; the config string is consulted only for
 * the SHORT NAME to print, because "us" is what the user wrote and
 * "English (US)" is what xkb calls it.
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

#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "synui.h"

/* The keyboard whose keymap answers for the seat. Any of them will do — they
 * are all handed the same keymap by keyboard_apply_config — but there has to
 * be one, and on a headless rig with no input device there is not. */
static struct wlr_keyboard *kbd_any(syn_server_t *s)
{
    syn_keyboard_t *kb;
    wl_list_for_each(kb, &s->keyboards, link)
        if (kb->wlr_keyboard && kb->wlr_keyboard->keymap)
            return kb->wlr_keyboard;
    return NULL;
}

/*
 * No keyboard on the seat — the greeter's first paint, a seat whose only
 * keyboard was just unplugged, every headless rig — and the question still has
 * a right answer.
 *
 * ⚠ COUNTING THE CONFIG'S COMMAS IS NOT THAT ANSWER. `xkb_layout = us,zz`
 * compiles to ONE group, because xkb drops a layout it cannot resolve; a count
 * of two would put `zz` on the chip while the keys stayed `us`, which is the
 * one failure this chip exists to prevent. So the keymap is compiled here too,
 * and cached against the strings it was compiled from — this runs at most once
 * per config change on a keyboardless seat, and never at all once a keyboard
 * has attached.
 */
static int kbd_layout_count_from_config(const syn_config_t *cfg)
{
    static char last[512];
    static int  cached = -1;

    char key[512];
    snprintf(key, sizeof(key), "%s|%s|%s|%s|%s", cfg->xkb_rules, cfg->xkb_model,
             cfg->xkb_layout, cfg->xkb_variant, cfg->xkb_options);
    if (cached >= 0 && strcmp(key, last) == 0) return cached;

    struct xkb_rule_names names = {
        .rules   = cfg->xkb_rules[0]   ? cfg->xkb_rules   : NULL,
        .model   = cfg->xkb_model[0]   ? cfg->xkb_model   : NULL,
        .layout  = cfg->xkb_layout[0]  ? cfg->xkb_layout  : NULL,
        .variant = cfg->xkb_variant[0] ? cfg->xkb_variant : NULL,
        .options = cfg->xkb_options[0] ? cfg->xkb_options : NULL,
    };
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    int n = 1;
    if (ctx) {
        struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &names,
                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
        /* A keymap that will not compile at all is the one
         * keyboard_apply_config falls back from, and its fallback has exactly
         * one group. */
        if (km) {
            n = (int)xkb_keymap_num_layouts(km);
            xkb_keymap_unref(km);
        }
        xkb_context_unref(ctx);
    }
    if (n < 1) n = 1;

    snprintf(last, sizeof(last), "%s", key);
    cached = n;
    return n;
}

int kbd_layout_count(syn_server_t *s)
{
    struct wlr_keyboard *kb = kbd_any(s);
    if (kb)
        return (int)xkb_keymap_num_layouts(kb->keymap);
    return kbd_layout_count_from_config(&s->config);
}

/* The nth comma-separated field of `list`, trimmed, or "" past the end. */
static void csv_field(const char *list, int idx, char *buf, size_t n)
{
    buf[0] = '\0';
    if (!list || !*list || idx < 0) return;

    const char *start = list;
    for (int i = 0; i < idx; i++) {
        const char *c = strchr(start, ',');
        if (!c) return;                    /* fewer fields than groups */
        start = c + 1;
    }
    const char *end = strchr(start, ',');
    size_t len = end ? (size_t)(end - start) : strlen(start);

    while (len && (*start == ' ' || *start == '\t')) { start++; len--; }
    while (len && (start[len - 1] == ' ' || start[len - 1] == '\t')) len--;
    if (len >= n) len = n - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
}

void kbd_layout_label(syn_server_t *s, int idx, char *buf, size_t n)
{
    /* What the user wrote: "us", "no", "de(neo)". Short by construction, and
     * the only spelling that matches what they would go looking for in
     * synuirc. */
    csv_field(s->config.xkb_layout, idx, buf, n);
    if (buf[0]) {
        char var[32];
        csv_field(s->config.xkb_variant, idx, var, sizeof(var));
        if (var[0]) {
            char both[64];
            snprintf(both, sizeof(both), "%s(%s)", buf, var);
            snprintf(buf, n, "%s", both);
        }
        return;
    }

    /* Nothing named — an empty xkb_layout means the XKB_DEFAULT_* environment
     * and the system default decided, and the keymap is the only place the
     * answer exists. */
    struct wlr_keyboard *kb = kbd_any(s);
    const char *km = kb ? xkb_keymap_layout_get_name(kb->keymap,
                                                     (xkb_layout_index_t)idx)
                        : NULL;
    snprintf(buf, n, "%s", (km && *km) ? km : "default");
}

/*
 * Which layout is typing.
 *
 * A KEYBOARD's own group is the answer whenever there is one — it is what the
 * next keystroke will actually be resolved against, and xkb's `grp:` options
 * move it without going through this file. `s->kbd_layout` answers only when
 * there is no keyboard on the seat at all, which is every headless rig and the
 * moment before the first device attaches.
 */
int kbd_layout_active(syn_server_t *s)
{
    int n = kbd_layout_count(s);
    struct wlr_keyboard *kb = kbd_any(s);
    int g = kb ? (int)kb->modifiers.group : s->kbd_layout;
    return (g >= 0 && g < n) ? g : 0;
}

/* Put one keyboard on the session's layout. Its own keymap bounds the index:
 * a virtual keyboard (wtype, the waybar menu) carries whatever keymap its
 * client handed it, which need not have as many groups as the seat's. */
void kbd_layout_apply(syn_server_t *s, struct wlr_keyboard *k)
{
    if (!k || !k->keymap) return;
    int idx = s->kbd_layout;
    if (idx <= 0) return;                  /* group 0 is what set_keymap left */
    if ((int)xkb_keymap_num_layouts(k->keymap) <= idx) return;

    struct wlr_keyboard_modifiers *m = &k->modifiers;
    wlr_keyboard_notify_modifiers(k, m->depressed, m->latched, m->locked,
                                  (uint32_t)idx);
}

void kbd_layout_observe(syn_server_t *s, struct wlr_keyboard *k)
{
    if (!k || !k->keymap) return;
    int g = (int)k->modifiers.group;
    if (g >= 0 && g < (int)xkb_keymap_num_layouts(k->keymap))
        s->kbd_layout = g;
}

void kbd_layout_set(syn_server_t *s, int idx)
{
    int n = kbd_layout_count(s);
    if (n <= 0) return;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;

    /* Recorded before the push, and recorded even when the push reaches
     * nothing: a keyboard that attaches a second later has to arrive on this
     * layout, not on the one set_keymap leaves it. */
    s->kbd_layout = idx;

    /* EVERY keyboard, not just the seat's current one: a laptop with an
     * external keyboard plugged in has two, and a layout that moved on one of
     * them is a layout that changes depending on which keyboard you type the
     * next character on. */
    syn_keyboard_t *kb;
    wl_list_for_each(kb, &s->keyboards, link) {
        struct wlr_keyboard *k = kb->wlr_keyboard;
        if (!k || !k->keymap) continue;
        if ((int)xkb_keymap_num_layouts(k->keymap) <= idx) continue;
        struct wlr_keyboard_modifiers *m = &k->modifiers;
        /* The group goes in as the LOCKED layout — the same slot xkb's own
         * `grp:` toggles use, so a switch made here and one made with the
         * configured chord are the same state and cannot disagree. */
        wlr_keyboard_notify_modifiers(k, m->depressed, m->latched, m->locked,
                                      (uint32_t)idx);
    }

    char lab[64];
    kbd_layout_label(s, idx, lab, sizeof(lab));
    wlr_log(WLR_INFO, "synui: keyboard layout: %s (group %d of %d)", lab, idx, n);
}

void kbd_layout_cycle(syn_server_t *s, int dir)
{
    int n = kbd_layout_count(s);
    if (n <= 1) return;                    /* one layout: nothing to cycle to */
    int next = (kbd_layout_active(s) + dir) % n;
    if (next < 0) next += n;
    kbd_layout_set(s, next);
}

/* Resolve a name — "no", "de(neo)", or a plain index — to a group, or -1.
 * Used by `synctl layout <name>`; the panel and the lock chip step by index. */
int kbd_layout_from_name(syn_server_t *s, const char *name)
{
    if (!name || !*name) return -1;

    int n = kbd_layout_count(s);
    char *end = NULL;
    long v = strtol(name, &end, 10);
    if (end && *end == '\0' && v >= 0 && v < n) return (int)v;

    for (int i = 0; i < n; i++) {
        char lab[64];
        kbd_layout_label(s, i, lab, sizeof(lab));
        if (strcasecmp(lab, name) == 0) return i;
    }
    /* A bare layout name against a labelled variant: "de" should find
     * "de(neo)" when that is the only de in the list. */
    for (int i = 0; i < n; i++) {
        char lab[64];
        kbd_layout_label(s, i, lab, sizeof(lab));
        char *paren = strchr(lab, '(');
        if (paren) *paren = '\0';
        if (strcasecmp(lab, name) == 0) return i;
    }
    return -1;
}
