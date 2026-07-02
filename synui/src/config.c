/*
 * config.c — Parse synuirc configuration
 *
 * Reads ~/.config/synui/synuirc or /etc/synui/synuirc.
 * Format: key = value (one per line), # comments.
 *
 * Keybindings:
 *   bind = <mod>+<key> <action> [arg]
 * e.g.
 *   bind = super+return term
 *   bind = super+shift+e spawn wofi --show drun
 *   bind = super+ctrl+3 movews 3
 * Modifiers: super/logo/mod4, shift, ctrl/control, alt/mod1. Keys are XKB
 * keysym names (case-insensitive: q, return, space, tab, backspace, f1…).
 * Actions: spawn <cmd>, term, cmdbar, overlay, close, quit, layout_cycle,
 * focus_next/prev, stack_next/prev, master_shrink/grow, float_toggle,
 * maximize_toggle, ai_ask, ws <1-9>, movews <1-9>.
 * A bind with the same combo as a default replaces it.
 *
 * SynapseOS Project — GPLv2
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "synui.h"

static char *strip(char *s)
{
    while (isspace(*s)) s++;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace(*e)) *e-- = '\0';
    return s;
}

/* ── Keybindings ─────────────────────────────────────────── */
static uint32_t parse_mod(const char *name)
{
    if (!strcasecmp(name, "super") || !strcasecmp(name, "logo") ||
        !strcasecmp(name, "mod4"))
        return WLR_MODIFIER_LOGO;
    if (!strcasecmp(name, "shift"))
        return WLR_MODIFIER_SHIFT;
    if (!strcasecmp(name, "ctrl") || !strcasecmp(name, "control"))
        return WLR_MODIFIER_CTRL;
    if (!strcasecmp(name, "alt") || !strcasecmp(name, "mod1"))
        return WLR_MODIFIER_ALT;
    return 0;
}

/* Register "<mod>+…+<key>" → "<action> [arg]". Same-combo binds replace the
 * earlier entry so user config overrides the seeded defaults. */
static void config_bind(syn_config_t *cfg, const char *combo,
                        const char *action_and_arg)
{
    uint32_t mods = 0;
    xkb_keysym_t sym = XKB_KEY_NoSymbol;

    char buf[128];
    snprintf(buf, sizeof(buf), "%s", combo);
    char *save = NULL;
    for (char *tok = strtok_r(buf, "+", &save); tok;
         tok = strtok_r(NULL, "+", &save)) {
        uint32_t m = parse_mod(tok);
        if (m) { mods |= m; continue; }
        sym = xkb_keysym_from_name(tok, XKB_KEYSYM_CASE_INSENSITIVE);
    }
    if (sym == XKB_KEY_NoSymbol) {
        wlr_log(WLR_ERROR, "synui: bind: bad key in '%s'", combo);
        return;
    }
    sym = xkb_keysym_to_lower(sym);

    /* Split the action from its argument on the first whitespace. */
    char action[SYN_BIND_ACTION_LEN] = {0};
    const char *sp = action_and_arg;
    while (*sp && !isspace(*sp)) sp++;
    size_t alen = (size_t)(sp - action_and_arg);
    if (alen == 0 || alen >= sizeof(action)) {
        wlr_log(WLR_ERROR, "synui: bind %s: bad action '%s'",
                combo, action_and_arg);
        return;
    }
    memcpy(action, action_and_arg, alen);
    while (isspace(*sp)) sp++;

    syn_bind_t *b = NULL;
    for (int i = 0; i < cfg->bind_count; i++) {
        if (cfg->binds[i].mods == mods && cfg->binds[i].sym == sym) {
            b = &cfg->binds[i];
            break;
        }
    }
    if (!b) {
        if (cfg->bind_count >= SYN_BINDS_MAX) {
            wlr_log(WLR_ERROR, "synui: bind table full (%d)", SYN_BINDS_MAX);
            return;
        }
        b = &cfg->binds[cfg->bind_count++];
    }
    b->mods = mods;
    b->sym  = sym;
    snprintf(b->action, sizeof(b->action), "%s", action);
    snprintf(b->arg, sizeof(b->arg), "%s", sp);
}

static void seed_default_binds(syn_config_t *cfg)
{
    static const struct { const char *combo, *action; } defaults[] = {
        { "super+return",    "term" },
        { "super+space",     "cmdbar" },
        { "super+a",         "overlay" },
        { "super+q",         "close" },
        { "super+shift+q",   "quit" },
        { "super+tab",       "layout_cycle" },
        { "super+h",         "master_shrink" },
        { "super+l",         "master_grow" },
        { "super+j",         "focus_next" },
        { "super+k",         "focus_prev" },
        { "super+shift+j",   "stack_next" },
        { "super+shift+k",   "stack_prev" },
        { "super+f",         "float_toggle" },
        { "super+m",         "maximize_toggle" },
        { "super+backspace", "ai_ask" },
    };
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
        config_bind(cfg, defaults[i].combo, defaults[i].action);

    for (int i = 1; i <= WORKSPACE_MAX; i++) {
        char combo[32], act[16];
        snprintf(combo, sizeof(combo), "super+%d", i);
        snprintf(act, sizeof(act), "ws %d", i);
        config_bind(cfg, combo, act);
        snprintf(combo, sizeof(combo), "super+shift+%d", i);
        snprintf(act, sizeof(act), "movews %d", i);
        config_bind(cfg, combo, act);
    }
}

void synui_config_load(syn_config_t *cfg)
{
    /* Defaults */
    strncpy(cfg->terminal, "foot", sizeof(cfg->terminal) - 1);
    cfg->autostart_count = 1;
    strncpy(cfg->autostart[0], "foot", sizeof(cfg->autostart[0]) - 1);
    cfg->border_width = BORDER_WIDTH_DEFAULT;
    cfg->gap = GAP_DEFAULT;
    cfg->master_factor = 0.60f;
    cfg->ai_layout = 1;
    cfg->ai_ctx_decor = 1;
    cfg->start_overlay = 0;

    /* Input defaults: keymap fields stay empty (XKB_DEFAULT_* env / system
     * default); libinput tri-states -1 = leave the device alone. */
    cfg->repeat_rate    = 25;
    cfg->repeat_delay   = 600;
    cfg->tap_to_click   = -1;
    cfg->natural_scroll = -1;
    cfg->left_handed    = -1;
    cfg->accel_speed    = 0.0f;
    cfg->accel_speed_set = 0;

    cfg->bind_count = 0;
    seed_default_binds(cfg);

    /* SYNUI_CONFIG overrides everything (used by the test harness for a
     * hermetic run), then user config, then system-wide. */
    const char *paths[3] = { getenv("SYNUI_CONFIG"), NULL, "/etc/synui/synuirc" };
    char user_path[256] = {0};
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg)
        snprintf(user_path, sizeof(user_path), "%s/synui/synuirc", xdg);
    else if (home)
        snprintf(user_path, sizeof(user_path), "%s/.config/synui/synuirc", home);
    paths[1] = user_path;

    FILE *f = NULL;
    for (int i = 0; i < 3; i++) {
        if (!paths[i] || !paths[i][0]) continue;
        f = fopen(paths[i], "r");
        if (f) break;
    }
    if (!f) return;

    /* Config file found — reset autostart so file entries replace defaults */
    cfg->autostart_count = 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *s = strip(line);
        if (!*s) continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = strip(s);
        char *val = strip(eq + 1);

        if (strcmp(key, "terminal") == 0)
            strncpy(cfg->terminal, val, sizeof(cfg->terminal) - 1);
        else if (strcmp(key, "autostart") == 0 && cfg->autostart_count < SYN_AUTOSTART_MAX)
            strncpy(cfg->autostart[cfg->autostart_count++], val, 127);
        else if (strcmp(key, "border_width") == 0) {
            cfg->border_width = atoi(val);
            if (cfg->border_width < 0)  cfg->border_width = 0;
            if (cfg->border_width > 32) cfg->border_width = 32;
        }
        else if (strcmp(key, "gap") == 0) {
            cfg->gap = atoi(val);
            if (cfg->gap < 0)   cfg->gap = 0;
            if (cfg->gap > 128) cfg->gap = 128;
        }
        else if (strcmp(key, "master_factor") == 0)
            cfg->master_factor = strtof(val, NULL);
        else if (strcmp(key, "ai_layout") == 0)
            cfg->ai_layout = strcmp(val, "on") == 0;
        else if (strcmp(key, "ai_ctx_decor") == 0)
            cfg->ai_ctx_decor = strcmp(val, "on") == 0;
        else if (strcmp(key, "start_overlay") == 0)
            cfg->start_overlay = strcmp(val, "on") == 0;
        else if (strcmp(key, "xkb_rules") == 0)
            strncpy(cfg->xkb_rules, val, sizeof(cfg->xkb_rules) - 1);
        else if (strcmp(key, "xkb_model") == 0)
            strncpy(cfg->xkb_model, val, sizeof(cfg->xkb_model) - 1);
        else if (strcmp(key, "xkb_layout") == 0)
            strncpy(cfg->xkb_layout, val, sizeof(cfg->xkb_layout) - 1);
        else if (strcmp(key, "xkb_variant") == 0)
            strncpy(cfg->xkb_variant, val, sizeof(cfg->xkb_variant) - 1);
        else if (strcmp(key, "xkb_options") == 0)
            strncpy(cfg->xkb_options, val, sizeof(cfg->xkb_options) - 1);
        else if (strcmp(key, "repeat_rate") == 0)
            cfg->repeat_rate = atoi(val);
        else if (strcmp(key, "repeat_delay") == 0)
            cfg->repeat_delay = atoi(val);
        else if (strcmp(key, "tap") == 0)
            cfg->tap_to_click = strcmp(val, "on") == 0;
        else if (strcmp(key, "natural_scroll") == 0)
            cfg->natural_scroll = strcmp(val, "on") == 0;
        else if (strcmp(key, "left_handed") == 0)
            cfg->left_handed = strcmp(val, "on") == 0;
        else if (strcmp(key, "accel_speed") == 0) {
            cfg->accel_speed = strtof(val, NULL);
            if (cfg->accel_speed < -1.0f) cfg->accel_speed = -1.0f;
            if (cfg->accel_speed >  1.0f) cfg->accel_speed =  1.0f;
            cfg->accel_speed_set = 1;
        }
        else if (strcmp(key, "bind") == 0) {
            /* value = "<combo> <action> [arg]" — split on first whitespace */
            char *sp = val;
            while (*sp && !isspace(*sp)) sp++;
            if (*sp) { *sp++ = '\0'; while (isspace(*sp)) sp++; }
            if (*sp)
                config_bind(cfg, val, sp);
            else
                wlr_log(WLR_ERROR, "synui: bind '%s': missing action", val);
        }
    }

    fclose(f);
}
