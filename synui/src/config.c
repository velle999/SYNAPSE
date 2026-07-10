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
 * Actions: spawn <cmd>, term, cmdbar, overlay, displays, menu, close, quit,
 * layout_cycle, focus_next/prev, stack_next/prev, master_shrink/grow,
 * float_toggle, maximize_toggle, minimize_toggle, minimize_restore, ai_ask,
 * ws <1-9>, movews <1-9>, move_output [prev], wallpaper, wallpaper_reload,
 * effects_toggle, power.
 * A bind with the same combo as a default replaces it.
 *
 * Wallpaper (wallpaper.c):
 *   wallpaper = /path/to/image.png   (PNG or JPEG; ~ expands to $HOME)
 *   wallpaper_mode = fill|fit|stretch|center   (default fill)
 * Empty/absent path, or a decode failure, falls back to the solid
 * background color. Super+Shift+W (or a SIGHUP) reloads synuirc and
 * repaints from the current wallpaper path/mode.
 *
 * Dock (dock.c):
 *   dock_enabled = on|off       (default on)
 *   dock_height = 64            (px)
 *   dock_hover_margin = 4       (px trigger strip at the bottom edge)
 *   dock_pin = firefox foot ...  (space-separated app_ids/.desktop basenames)
 *
 * Power saving (power.c) — idle seconds per stage, 0 = never. Each is
 * measured from the last input event, so they are independent, not
 * cumulative. Super+P edits them live and writes power.state, which then
 * overrides these lines (delete it to hand control back to synuirc):
 *   power_enabled = on|off             (default on)
 *   power_dim_timeout = 240
 *   power_blank_timeout = 600
 *   power_lock_timeout = 900
 *   power_suspend_timeout = 0
 *   power_lock_cmd = swaylock -f -c 000000
 *   power_suspend_cmd = systemctl suspend
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

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* Parse "#rrggbb" (or "rrggbb") into RGBA floats; alpha fixed at 1.0.
 * Returns 0 and leaves out[] untouched on malformed input. */
static int parse_hex_color(const char *val, float out[4])
{
    if (val[0] == '#') val++;
    if (strlen(val) != 6) return 0;
    char *end;
    long v = strtol(val, &end, 16);
    if (end != val + 6) return 0;
    out[0] = (float)((v >> 16) & 0xff) / 255.0f;
    out[1] = (float)((v >>  8) & 0xff) / 255.0f;
    out[2] = (float)( v        & 0xff) / 255.0f;
    out[3] = 1.0f;
    return 1;
}

static void seed_default_binds(syn_config_t *cfg)
{
    static const struct { const char *combo, *action; } defaults[] = {
        { "super+return",    "term" },
        { "super+space",     "cmdbar" },
        { "super+a",         "overlay" },
        { "super+d",         "displays" },
        { "super+escape",    "menu" },
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
        { "super+n",         "minimize_toggle" },
        { "super+shift+n",   "minimize_restore" },
        { "super+backspace", "ai_ask" },
        { "super+w",         "wallpaper" },
        { "super+shift+w",   "wallpaper_reload" },
        { "super+e",         "effects_toggle" },
        { "super+p",         "power" },
        { "super+o",         "move_output" },
        { "super+shift+o",   "move_output prev" },
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

    /* GLES post-process: on by default, harmless on pixman (effects_init
     * refuses and the plain path is used). Strengths tuned for subtlety. */
    cfg->effects           = 1;
    cfg->effect_scanline   = 0.35f;
    cfg->effect_curvature  = 0.25f;
    cfg->effect_aberration = 0.40f;
    cfg->effect_glitch     = 0.60f;

    {
        static const float norm[4]  = COLOR_BORDER_NORM;
        static const float focus[4] = COLOR_BORDER_FOCUS;
        static const float ai[4]    = COLOR_BORDER_AI;
        static const float warn[4]  = COLOR_BORDER_WARN;
        memcpy(cfg->border_color_norm,  norm,  sizeof(norm));
        memcpy(cfg->border_color_focus, focus, sizeof(focus));
        memcpy(cfg->border_color_ai,    ai,    sizeof(ai));
        memcpy(cfg->border_color_warn,  warn,  sizeof(warn));
    }

    /* Input defaults: keymap fields stay empty (XKB_DEFAULT_* env / system
     * default); libinput tri-states -1 = leave the device alone. */
    cfg->repeat_rate    = 25;
    cfg->repeat_delay   = 600;
    cfg->tap_to_click   = -1;
    cfg->natural_scroll = -1;
    cfg->left_handed    = -1;
    cfg->accel_speed    = 0.0f;
    cfg->accel_speed_set = 0;

    cfg->wallpaper[0]   = '\0';
    cfg->wallpaper_mode = SYN_WALLPAPER_FILL;
    cfg->wallpaper_src  = SYN_WP_SRC_IMAGE;

    cfg->dock_enabled      = 1;
    cfg->dock_height       = 64;
    cfg->dock_hover_margin = 4;
    cfg->dock_edge         = SYN_DOCK_EDGE_BOTTOM;
    cfg->dock_pin_count    = 0;

    cfg->power_enabled = 1;
    cfg->power_dim     = 240;
    cfg->power_blank   = 600;
    cfg->power_lock    = 900;
    /* Suspending this box would take down anything it serves over the
     * network, so idle-suspend is opt-in from the panel, never a default. */
    cfg->power_suspend = 0;
    /* The pgrep guard keeps a second idle period from stacking another
     * swaylock on top of the one already covering the screen. */
    snprintf(cfg->power_lock_cmd, sizeof(cfg->power_lock_cmd),
             "pgrep -x swaylock >/dev/null || swaylock -f -c 000000");
    snprintf(cfg->power_suspend_cmd, sizeof(cfg->power_suspend_cmd),
             "systemctl suspend");

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
    if (!f) {
        /* No config file: still honour persisted picker/dock choices. */
        wallpaper_state_load(cfg);
        dock_state_load(cfg);
        power_state_load(cfg);
        return;
    }

    /* Config file found — reset autostart so file entries replace defaults */
    cfg->autostart_count = 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = strip(line);
        if (!*s || *s == '#') continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = strip(s);
        char *val = strip(eq + 1);

        /* Inline comments: a whitespace-preceded '#' ends the value —
         * unless it's the value's first character, so color values like
         * `border_color_focus = #ff296d` survive. */
        if (*val) {
            for (char *p = val + 1; (p = strchr(p, '#')); p++) {
                if (p[-1] == ' ' || p[-1] == '\t') {
                    *p = '\0';
                    break;
                }
            }
            val = strip(val);
        }

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
        else if (strcmp(key, "border_color_norm") == 0)
            parse_hex_color(val, cfg->border_color_norm);
        else if (strcmp(key, "border_color_focus") == 0)
            parse_hex_color(val, cfg->border_color_focus);
        else if (strcmp(key, "border_color_ai") == 0)
            parse_hex_color(val, cfg->border_color_ai);
        else if (strcmp(key, "border_color_warn") == 0)
            parse_hex_color(val, cfg->border_color_warn);
        else if (strcmp(key, "effects") == 0)
            cfg->effects = strcmp(val, "on") == 0;
        else if (strcmp(key, "effect_scanline") == 0)
            cfg->effect_scanline = clamp01(strtof(val, NULL));
        else if (strcmp(key, "effect_curvature") == 0)
            cfg->effect_curvature = clamp01(strtof(val, NULL));
        else if (strcmp(key, "effect_aberration") == 0)
            cfg->effect_aberration = clamp01(strtof(val, NULL));
        else if (strcmp(key, "effect_glitch") == 0)
            cfg->effect_glitch = clamp01(strtof(val, NULL));
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
        else if (strcmp(key, "wallpaper") == 0) {
            /* Built-in keywords select a bundled wallpaper; anything else is
             * a file path for the static wallpaper.c backend.
             *   matrix  → animated GLES2 kanji rain (matrix.c)
             *   default → the bundled Synapse image (/usr/share/synui)
             *   none    → solid bg_color
             * (see also the live wppick.c picker / wallpaper.state). */
            if (strcmp(val, "matrix") == 0) {
                cfg->wallpaper_src = SYN_WP_SRC_MATRIX;
            } else if (strcmp(val, "default") == 0) {
                cfg->wallpaper_src = SYN_WP_SRC_IMAGE;
                strncpy(cfg->wallpaper, SYNUI_DATADIR "/wallpaper.png",
                        sizeof(cfg->wallpaper) - 1);
            } else if (strcmp(val, "none") == 0) {
                cfg->wallpaper_src = SYN_WP_SRC_IMAGE;
                cfg->wallpaper[0] = '\0';
            } else {
                cfg->wallpaper_src = SYN_WP_SRC_IMAGE;
                strncpy(cfg->wallpaper, val, sizeof(cfg->wallpaper) - 1);
            }
        }
        else if (strcmp(key, "wallpaper_mode") == 0) {
            if      (strcmp(val, "fill")    == 0) cfg->wallpaper_mode = SYN_WALLPAPER_FILL;
            else if (strcmp(val, "fit")     == 0) cfg->wallpaper_mode = SYN_WALLPAPER_FIT;
            else if (strcmp(val, "stretch") == 0) cfg->wallpaper_mode = SYN_WALLPAPER_STRETCH;
            else if (strcmp(val, "center")  == 0) cfg->wallpaper_mode = SYN_WALLPAPER_CENTER;
            else wlr_log(WLR_ERROR, "synui: wallpaper_mode: unknown '%s'", val);
        }
        else if (strcmp(key, "power_enabled") == 0)
            cfg->power_enabled = strcmp(val, "on") == 0;
        else if (strcmp(key, "power_dim_timeout") == 0)
            cfg->power_dim = atoi(val) < 0 ? 0 : atoi(val);
        else if (strcmp(key, "power_blank_timeout") == 0)
            cfg->power_blank = atoi(val) < 0 ? 0 : atoi(val);
        else if (strcmp(key, "power_lock_timeout") == 0)
            cfg->power_lock = atoi(val) < 0 ? 0 : atoi(val);
        else if (strcmp(key, "power_suspend_timeout") == 0)
            cfg->power_suspend = atoi(val) < 0 ? 0 : atoi(val);
        else if (strcmp(key, "power_lock_cmd") == 0)
            snprintf(cfg->power_lock_cmd, sizeof(cfg->power_lock_cmd), "%s", val);
        else if (strcmp(key, "power_suspend_cmd") == 0)
            snprintf(cfg->power_suspend_cmd, sizeof(cfg->power_suspend_cmd), "%s", val);
        else if (strcmp(key, "dock_enabled") == 0)
            cfg->dock_enabled = strcmp(val, "on") == 0;
        else if (strcmp(key, "dock_edge") == 0) {
            if      (strcmp(val, "bottom") == 0) cfg->dock_edge = SYN_DOCK_EDGE_BOTTOM;
            else if (strcmp(val, "top")    == 0) cfg->dock_edge = SYN_DOCK_EDGE_TOP;
            else if (strcmp(val, "left")   == 0) cfg->dock_edge = SYN_DOCK_EDGE_LEFT;
            else if (strcmp(val, "right")  == 0) cfg->dock_edge = SYN_DOCK_EDGE_RIGHT;
            else wlr_log(WLR_ERROR, "synui: dock_edge: unknown '%s'", val);
        }
        else if (strcmp(key, "dock_height") == 0) {
            cfg->dock_height = atoi(val);
            if (cfg->dock_height < 32)  cfg->dock_height = 32;
            if (cfg->dock_height > 200) cfg->dock_height = 200;
        }
        else if (strcmp(key, "dock_hover_margin") == 0) {
            cfg->dock_hover_margin = atoi(val);
            if (cfg->dock_hover_margin < 1)  cfg->dock_hover_margin = 1;
            if (cfg->dock_hover_margin > 32) cfg->dock_hover_margin = 32;
        }
        else if (strcmp(key, "dock_pin") == 0) {
            /* space-separated app_ids/.desktop basenames */
            char buf[512];
            snprintf(buf, sizeof(buf), "%s", val);
            char *save = NULL;
            cfg->dock_pin_count = 0;
            for (char *tok = strtok_r(buf, " \t", &save);
                 tok && cfg->dock_pin_count < DOCK_PIN_MAX;
                 tok = strtok_r(NULL, " \t", &save))
                snprintf(cfg->dock_pin[cfg->dock_pin_count++], 128, "%s", tok);
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

    /* A live picker choice (wallpaper.state) is the most recent explicit
     * intent, so it overrides the synuirc `wallpaper` line. Delete the state
     * file to hand control back to synuirc. Same for the dock's edge/pins. */
    wallpaper_state_load(cfg);
    dock_state_load(cfg);
    power_state_load(cfg);
}
