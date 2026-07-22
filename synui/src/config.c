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
 * A bind on a digit ("super+1") also answers to that digit on the *numpad* —
 * the keypad never sends a digit keysym (KP_1 with NumLock on, KP_End with it
 * off or with Shift held), so input.c maps them back. Bind "super+kp_1"
 * explicitly to give the keypad key its own action.
 * Actions: spawn <cmd>, term, cmdbar, overlay, displays, menu, close, quit,
 * layout_cycle, focus_next/prev, alt_tab, alt_tab_prev, stack_next/prev,
 * master_shrink/grow,
 * float_toggle, fullscreen_toggle, maximize_toggle, minimize_toggle,
 * minimize_restore, decorations_toggle, ai_ask,
 * ws <1-9>, movews <1-9>, move_output [prev], wallpaper, wallpaper_reload,
 * filters, effects_toggle, power, lock, game, taskmgr, network, news.
 * A bind with the same combo as a default replaces it.
 * "filters" (Super+E) opens the CRT filter panel; "effects_toggle" is the older
 * blind on/off flip, kept for anyone who bound it.
 * "decorations_toggle" (Super+Shift+D) hides every titlebar until you press it
 * again; `titlebar_height = 0` below is the permanent version.
 *
 * Wallpaper (wallpaper.c):
 *   wallpaper = /path/to/image.png   (PNG or JPEG; ~ expands to $HOME)
 *   wallpaper_mode = fill|fit|stretch|center   (default fill)
 * Empty/absent path, or a decode failure, falls back to the solid
 * background color. Super+Shift+W (or a SIGHUP) reloads synuirc and
 * repaints from the current wallpaper path/mode.
 *
 * Window snapping (snap.c):
 *   snap = on|off               (default on)
 * Drag a window to the top edge to maximize it, to a side for that half, into a
 * corner for that quarter; dragging it off again restores its old size.
 *
 * Cat mode (cat.c):
 *   cat = on|off                (default off; Super+Shift+C toggles at runtime)
 *
 * Welcome menu (render.c) — Super+Escape opens it regardless; this is only
 * whether it greets you on login. The menu's "Show At Startup" row toggles it
 * live and writes welcome.state, which then overrides this line:
 *   welcome_at_startup = on|off (default on)
 *
 * Keyboard (input.c):
 *   numlock = on|off            (default on — lock NumLock at attach so the
 *                                numpad types digits from login onwards,
 *                                including on the swaylock screen)
 *
 * Dock (dock.c):
 *   dock_enabled = on|off       (default on)
 *   dock_autohide = on|off      (default on; off = always on screen)
 *   night_light  = on|off       (default off)   Super+Shift+B toggles
 *   night_light_temp = 4000     (Kelvin, 1000-6500; 6500 is daylight)
 *   dock_height = 64            (px)
 *   dock_hover_margin = 4       (px trigger strip at the bottom edge)
 *   dock_pin = firefox foot ...  (space-separated app_ids/.desktop basenames)
 *
 * Launcher (launcher.c) — the "◢ SYNAPSE" start-menu button, top-left:
 *   launcher_style = text|logo  (default; logo = ◢ caret + the dendrite emblem)
 *     — the default only: the control panel and start-menu Settings toggle this
 *       at run time, persisting to launcher.state (laid back over synuirc here).
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
 * Network (Super+I / welcome menu) — nmtui in a terminal. synui has no text
 * entry to type a passphrase into, so there is nothing native to point at yet:
 *   network_cmd = foot -e nmtui
 *
 * News (news.c, Super+R) — RSS/Atom feeds. With no news_source lines the
 * built-in list is used (Hacker News, Lobsters, Arch news, Arch security,
 * kernel releases, LWN, Phoronix, GamingOnLinux). The *first* news_source line
 * replaces that list rather than adding to it, so listing your own feeds gives
 * you those and only those. TAG is the column the panel shows:
 *   news_source = LWN|https://lwn.net/headlines/newrss
 *   news_source = HN|https://news.ycombinator.com/rss
 *   news_refresh = 15                  (minutes; only while the panel is open)
 *
 * Game mode (game.c) — a fullscreen XWayland client is taken to be a game, and
 * while one runs synapd is stopped (it holds ~4GB of VRAM and has no unload
 * IPC) and the idle stages are held off (a gamepad is not seat input, so the
 * screen would otherwise dim mid-game). Super+G forces it on/off:
 *   game_mode = on|off                 (default on)
 *   game_suspend_ai = on|off           (default on — stop synapd while gaming)
 *   game_inhibit_idle = on|off         (default on — no dim/blank/lock)
 *   game_exclude = firefox chibi tepris nexus-chat foot
 *       Space-separated app_ids that are NOT games; REPLACES the built-in list.
 *       This is what keeps a fullscreen Firefox video from stopping the AI.
 *   game_ai_stop_cmd = sudo -n systemctl stop synapd
 *   game_ai_start_cmd = sudo -n systemctl start synapd
 *       synapd is a *system* unit, so a plain `systemctl stop` from the session
 *       user gets bounced by polkit ("interactive authentication required") —
 *       and synui_spawn is fire-and-forget, so that failure was invisible: game
 *       mode logged that it had suspended synapd while synapd kept running.
 *       sudo -n never prompts; the grant is /etc/sudoers.d/synapd-gamemode,
 *       exactly these two commands (see syn-install.sh).
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
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
        { "super+c",         "control" },
        { "super+b",         "bluetooth" },
        { "super+shift+b",   "night_light" },   /* "blue light" */
        { "super+shift+r",   "record" },        /* start/stop screen recording */
        { "super+v",         "clipboard" },     /* clipboard history */
        /* Brightness keys. No modifier: they are dedicated keys on every
         * laptop, and nothing else claims them. */
        { "xf86monbrightnessup",   "brightness_up" },
        { "xf86monbrightnessdown", "brightness_down" },
        { "super+q",         "close" },
        { "super+shift+q",   "quit" },
        { "super+tab",       "layout_cycle" },
        /* Alt+Tab is most-recently-used order (alt_tab), not the stacking-order
         * walk that super+j/k do — "the window I was just in" is the whole
         * reason the key exists. */
        { "alt+tab",         "alt_tab" },
        { "alt+shift+tab",   "alt_tab_prev" },
        { "super+h",         "master_shrink" },
        { "super+shift+l",   "master_grow" },
        { "super+l",         "lock" },
        { "super+j",         "focus_next" },
        { "super+k",         "focus_prev" },
        { "super+shift+j",   "stack_next" },
        { "super+shift+k",   "stack_prev" },
        { "super+f",         "float_toggle" },
        { "super+shift+f",   "fullscreen_toggle" },
        { "super+m",         "maximize_toggle" },
        { "super+shift+d",   "decorations_toggle" },
        { "super+n",         "minimize_toggle" },
        { "super+shift+n",   "minimize_restore" },
        { "super+backspace", "ai_ask" },
        { "super+w",         "wallpaper" },
        { "super+shift+w",   "wallpaper_reload" },
        { "super+e",         "filters" },
        { "super+p",         "power" },
        /* Themes, not the task manager. The task manager had two binds and needs
         * one — ctrl+alt+delete below is the one everybody already reaches for,
         * so super+t goes to the theme manager, which had only the far less
         * guessable super+shift+a. That freed super+shift+a entirely; it is
         * deliberately left unbound rather than reassigned. */
        { "super+t",         "theme" },
        { "super+shift+t",   "calendar" },
        { "super+i",         "network" },
        /* Not super+n: that is minimize, and has been since before there was
         * anything to read. R for RSS — the panel is a feed reader. */
        { "super+r",         "news" },
        /* The one shortcut everybody already has in their fingers. Nothing
         * below us claims it: logind's ctrl-alt-del handling is a VT/console
         * thing, so inside a Wayland session the key reaches the compositor. */
        { "ctrl+alt+delete", "taskmgr" },
        /* Print grabs the monitor you are looking at (the `screenshot` action
         * resolves it — grim cannot). Shift+Print drags out an area with
         * slurp; Ctrl+Print takes the whole layout, every monitor at once.
         * super+shift+s is the same area-select under the shortcut most people
         * already have in their fingers from elsewhere. */
        { "print",           "screenshot" },
        { "shift+print",     "spawn synui-screenshot region" },
        { "ctrl+print",      "spawn synui-screenshot full" },
        { "super+shift+s",   "spawn synui-screenshot region" },
        /* The USB volume knob (LCTECH LCKEY) reports plain KEY_VOLUMEUP /
         * KEY_VOLUMEDOWN / KEY_MUTE, which the evdev keymap turns into these
         * XF86Audio keysyms with no modifier held. synui had no audio binds at
         * all, so the knob's events reached the bind table and matched nothing.
         * Bare-key binds are fine here — `print` above is one. */
        { "xf86audioraisevolume", "volume up" },
        { "xf86audiolowervolume", "volume down" },
        { "xf86audiomute",        "volume mute" },
        { "super+g",         "game" },
        /* super+shift+a is intentionally FREE — the theme manager moved to
         * super+t. Left open for the next feature rather than filled to keep the
         * table tidy; `bind = super+shift+a <action>` in synuirc claims it. */
        { "super+shift+c",   "cat" },
        { "super+o",         "move_output" },
        { "super+shift+o",   "move_output prev" },
    };
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        /* Shout about a duplicate rather than shipping one. handle_keybinding
         * takes the FIRST match, so a second bind on a combo is not a conflict
         * anyone sees — it is the older feature silently going dead. Adding
         * night_light on super+shift+n did exactly that to minimize_restore and
         * only turned up because the table happened to get grepped. */
        for (size_t j = 0; j < i; j++) {
            if (strcmp(defaults[i].combo, defaults[j].combo) == 0)
                wlr_log(WLR_ERROR, "synui: config: DUPLICATE default bind %s "
                        "(%s shadows %s) — the second one is dead",
                        defaults[i].combo, defaults[j].action, defaults[i].action);
        }
        config_bind(cfg, defaults[i].combo, defaults[i].action);
    }

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

/* ── Config-dir paths ────────────────────────────────────── */

/* The single resolver for everything under synui's config dir: synuirc,
 * outputs.conf, and the *.state files (wallpaper/dock/power/filters/welcome).
 * Those state files each used to hardcode ~/.config/synui and ignore
 * XDG_CONFIG_HOME, which synuirc and outputs.conf honour — so pointing
 * XDG_CONFIG_HOME elsewhere read the settings from one directory and the
 * persisted picker/dock/power choices from another. Route every one of them
 * through here and they can't drift apart again. */
bool syn_config_path(char *buf, size_t n, const char *name)
{
    const char *xdg  = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(buf, n, "%s/synui/%s", xdg, name);
    else if (home && *home)
        snprintf(buf, n, "%s/.config/synui/%s", home, name);
    else
        return false;
    return true;
}

/* Create the config dir (and its parent) if absent, so a *_state_save() into
 * a fresh XDG_CONFIG_HOME writes instead of failing to a log line nobody
 * reads. Only writers need this; readers treat a missing file as "no
 * persisted choice". mkdir(2) errors other than EEXIST are left for the
 * caller's fopen() to report against the full path. */
void syn_config_ensure_dir(void)
{
    char dir[256];
    if (!syn_config_path(dir, sizeof(dir), "")) return;

    /* syn_config_path() left a trailing '/' from the empty name. */
    size_t len = strlen(dir);
    while (len > 1 && dir[len - 1] == '/') dir[--len] = '\0';

    /* mkdir -p: XDG_CONFIG_HOME can point at a path with any number of
     * missing components, so creating just the last one (or just its parent)
     * still fails with ENOENT. Walk it and create each in turn; intermediate
     * failures are left to the final mkdir to report against the full path. */
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(dir, 0755);
        *p = '/';
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST)
        wlr_log(WLR_ERROR, "synui: mkdir %s failed: %s", dir, strerror(errno));
}

void synui_config_load(syn_config_t *cfg)
{
    /* Defaults */
    strncpy(cfg->terminal, "foot", sizeof(cfg->terminal) - 1);
    cfg->night_light = 0;
    cfg->night_light_temp = 4000;
    cfg->autostart_count = 1;
    strncpy(cfg->autostart[0], "foot", sizeof(cfg->autostart[0]) - 1);
    cfg->border_width = BORDER_WIDTH_DEFAULT;
    cfg->gap = GAP_DEFAULT;
    cfg->master_factor = 0.60f;
    cfg->titlebar_height = TITLEBAR_HEIGHT_DEF;
    cfg->remember_geometry = true;
    cfg->animation_ms    = ANIMATION_MS_DEF;
    { static const float c[4] = COLOR_TITLEBAR_NORM;    memcpy(cfg->titlebar_color,       c, sizeof(c)); }
    { static const float c[4] = COLOR_TITLEBAR_FOCUS;   memcpy(cfg->titlebar_color_focus, c, sizeof(c)); }
    { static const float c[4] = COLOR_TITLE_TEXT;       memcpy(cfg->titlebar_text,        c, sizeof(c)); }
    { static const float c[4] = COLOR_TITLE_TEXT_FOCUS; memcpy(cfg->titlebar_text_focus,  c, sizeof(c)); }
    cfg->ai_layout = 1;
    cfg->ai_ctx_decor = 1;
    cfg->start_overlay = 0;
    cfg->snap = 1;

    /* Theme + transparency. SYNAPSE's colours ARE the border/titlebar defaults
     * set just above, so leaving theme = SYNAPSE changes nothing; a `theme =`
     * line (parsed below) or theme.state (applied at startup) reskins from here.
     * Transparency is opt-in: off, the opacity levels are dormant. */
    cfg->theme            = SYN_THEME_SYNAPSE;
    /* Flat chrome, with the gradient ends and the 3D face seeded from the
     * caption colours: deco.c always reads these, so they must be valid even for
     * a config that never sees a theme (a `theme =` line or theme.state then
     * overwrites the lot via theme_load_colors). */
    cfg->chrome           = SYN_CHROME_FLAT;
    memcpy(cfg->titlebar_grad,       cfg->titlebar_color,       sizeof(cfg->titlebar_grad));
    memcpy(cfg->titlebar_grad_focus, cfg->titlebar_color_focus, sizeof(cfg->titlebar_grad_focus));
    memcpy(cfg->chrome_face,         cfg->border_color_norm,    sizeof(cfg->chrome_face));
    cfg->transparency     = 0;
    cfg->active_opacity   = 1.00f;
    cfg->inactive_opacity = 0.92f;
    /* Unset: foot follows the slider exactly as before until someone sets a
     * foot_alpha, so this key changes nothing for a config that omits it. */
    cfg->foot_alpha       = -1.0f;

    /* scenefx glass defaults (Stage 5). Rounded corners are on out of the box —
     * they cost nothing on opaque windows and are the single most visible piece
     * of the "glass" look. Blur is on but only paints behind translucent windows. */
    cfg->corner_radius    = 12;
    cfg->blur             = 1;
    cfg->blur_passes      = 3;
    cfg->blur_radius      = 5;
    cfg->blur_noise       = 0.02f;
    cfg->blur_brightness  = 0.90f;
    cfg->blur_contrast    = 1.00f;
    cfg->blur_saturation  = 1.15f;
    /* Drop shadow: on, a soft dark halo dropped a touch downward. */
    cfg->shadow           = 1;
    cfg->shadow_blur_sigma = 18.0f;
    cfg->shadow_offset_x  = 0;
    cfg->shadow_offset_y  = 6;
    cfg->shadow_color[0]  = 0.00f; cfg->shadow_color[1] = 0.00f;
    cfg->shadow_color[2]  = 0.00f; cfg->shadow_color[3] = 0.45f;
    /* SYNAPSE's neon cyan — the panel accent render.c starts on; theme_apply()
     * (via theme.state at startup, or a synuirc `theme =`) reskins it. */
    cfg->panel_accent[0] = 0.00f; cfg->panel_accent[1] = 0.85f;
    cfg->panel_accent[2] = 0.75f; cfg->panel_accent[3] = 1.00f;

    /* GLES post-process: on by default, harmless on pixman (effects_init
     * refuses and the plain path is used). Strengths tuned for subtlety.
     * These are the defaults; the Super+E panel edits them live and saves to
     * ~/.config/synui/filters.state, which filters_state_load() lays over the
     * top at startup. */
    cfg->effects           = 1;
    cfg->effect_scanline   = 0.35f;
    cfg->effect_curvature  = 0.25f;
    cfg->effect_aberration = 0.40f;
    cfg->effect_glitch     = 0.60f;
    cfg->effect_phosphor   = SYN_PHOSPHOR_OFF;   /* colour, until a tint is picked */
    cfg->effect_mono       = 0.90f;              /* strong monochrome when it is */
    cfg->effect_bloom      = 0.55f;              /* phosphor glow, the way a real tube blooms */

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

    cfg->cat_start         = 0;   /* opt-in; Super+Shift+C toggles it live */

    cfg->welcome_at_startup = 1;
    cfg->numlock            = 1;

    cfg->dock_enabled      = 1;
    cfg->dock_autohide     = 1;
    cfg->dock_height       = 64;
    cfg->dock_hover_margin = 4;
    cfg->dock_edge         = SYN_DOCK_EDGE_BOTTOM;
    cfg->dock_pin_count    = 0;
    cfg->launcher_style    = SYN_LAUNCHER_TEXT;

    cfg->power_enabled = 1;
    cfg->power_dim     = 240;
    cfg->power_blank   = 600;
    cfg->power_lock    = 900;
    /* Suspending this box would take down anything it serves over the
     * network, so idle-suspend is opt-in from the panel, never a default. */
    cfg->power_suspend = 0;
    /* The pgrep guard keeps a second idle period from stacking another
     * swaylock on top of the one already covering the screen. */
    /* `-c 000000` alone drew a featureless black rectangle with no indicator
     * until a key was pressed, so a lock was indistinguishable from a dead or
     * blanked screen — "it didn't go to a lock screen, it blacked out". Give it
     * the SynapseOS neon and keep the ring on screen (--indicator-idle-visible)
     * so it is obviously asking for a password, and so a wrong one is visibly
     * wrong (--ring-wrong-color) rather than silent.
     *
     * swaylock 1.8.6 has no --clock. */
    /* NB: there is no bare `--indicator` in swaylock 1.8.6. getopt_long treats
     * it as an ambiguous prefix of --indicator-radius/-thickness/-idle-visible/
     * -caps-lock/-x-position/-y-position, prints usage and EXITS — so passing
     * it means no lock screen appears at all. --indicator-idle-visible alone is
     * what keeps the ring on screen. */
    snprintf(cfg->power_lock_cmd, sizeof(cfg->power_lock_cmd),
             "pgrep -x swaylock >/dev/null || swaylock -f -c 000814"
             " --indicator-idle-visible"
             " --indicator-radius 110 --indicator-thickness 8"
             " --ring-color 00ffff --ring-ver-color ff00c8"
             " --ring-wrong-color ff3355 --key-hl-color ff00c8"
             " --inside-color 080814 --text-color 00ffff"
             " --line-color 080814 --separator-color 080814");
    snprintf(cfg->power_suspend_cmd, sizeof(cfg->power_suspend_cmd),
             "systemctl suspend");

    snprintf(cfg->network_cmd, sizeof(cfg->network_cmd),
             "foot -e nmtui");

    /* News (news.c). No sources here: an empty list means "use the built-in
     * ones" (news.c owns that table), and the first `news_source =` line in
     * synuirc replaces the lot. */
    cfg->news_sources_n  = 0;
    cfg->news_refresh_min = 15;

    cfg->game_mode         = 1;
    cfg->game_suspend_ai   = 1;
    cfg->game_inhibit_idle = 1;
    snprintf(cfg->game_ai_stop_cmd,  sizeof(cfg->game_ai_stop_cmd),
             "sudo -n systemctl stop synapd");
    snprintf(cfg->game_ai_start_cmd, sizeof(cfg->game_ai_start_cmd),
             "sudo -n systemctl start synapd");
    /* The fullscreen X11 clients on this system that are NOT games. Without
     * these, going fullscreen on a YouTube video would stop synapd. The
     * firefox-app-mode apps (tepris, nexus-chat) report their own app_id via
     * MOZ_APP_REMOTINGNAME, so they need naming separately from firefox. */
    static const char *const defaults[] = {
        "firefox", "chibi", "tepris", "nexus-chat", "foot",
    };
    cfg->game_exclude_count = 0;
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
        snprintf(cfg->game_exclude[cfg->game_exclude_count++],
                 sizeof(cfg->game_exclude[0]), "%s", defaults[i]);

    cfg->bind_count = 0;
    seed_default_binds(cfg);

    /* SYNUI_CONFIG overrides everything (used by the test harness for a
     * hermetic run), then user config, then system-wide. */
    const char *paths[3] = { getenv("SYNUI_CONFIG"), NULL, "/etc/synui/synuirc" };
    char user_path[256] = {0};
    syn_config_path(user_path, sizeof(user_path), "synuirc");
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
        welcome_state_load(cfg);
        launcher_state_load(cfg);
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
        else if (strcmp(key, "animation_ms") == 0) {
            /* 0 = off. Cap it: a multi-second fade is a broken desktop, not a
             * preference. */
            int ms = atoi(val);
            if (ms < 0)   ms = 0;
            if (ms > 1000) ms = 1000;
            cfg->animation_ms = ms;
        }
        else if (strcmp(key, "titlebar_height") == 0) {
            /* 0 disables the titlebar entirely; clamp the rest to something a
             * button glyph can actually be drawn in. */
            int th = atoi(val);
            if (th < 0)  th = 0;
            if (th > 64) th = 64;
            if (th > 0 && th < 14) th = 14;
            cfg->titlebar_height = th;
        }
        else if (strcmp(key, "remember_geometry") == 0)
            cfg->remember_geometry = strcmp(val, "on") == 0 ||
                                     strcmp(val, "1") == 0;
        else if (strcmp(key, "titlebar_color") == 0)
            parse_hex_color(val, cfg->titlebar_color);
        else if (strcmp(key, "titlebar_color_focus") == 0)
            parse_hex_color(val, cfg->titlebar_color_focus);
        else if (strcmp(key, "titlebar_text") == 0)
            parse_hex_color(val, cfg->titlebar_text);
        else if (strcmp(key, "titlebar_text_focus") == 0)
            parse_hex_color(val, cfg->titlebar_text_focus);
        else if (strcmp(key, "ai_layout") == 0)
            cfg->ai_layout = strcmp(val, "on") == 0;
        else if (strcmp(key, "ai_ctx_decor") == 0)
            cfg->ai_ctx_decor = strcmp(val, "on") == 0;
        else if (strcmp(key, "start_overlay") == 0)
            cfg->start_overlay = strcmp(val, "on") == 0;
        else if (strcmp(key, "snap") == 0)
            cfg->snap = strcmp(val, "on") == 0;
        else if (strcmp(key, "theme") == 0) {
            /* Seeds the chrome colours from the preset; an explicit
             * border_color_* / titlebar_* line placed AFTER this still wins,
             * because it parses later and overwrites the field. */
            for (int t = 0; t < SYN_THEME_COUNT; t++)
                if (strcmp(val, syn_theme_names[t]) == 0) {
                    theme_load_colors(cfg, (syn_theme_t)t);
                    break;
                }
        }
        else if (strcmp(key, "transparency") == 0)
            cfg->transparency = strcmp(val, "on") == 0;
        else if (strcmp(key, "active_opacity") == 0)
            cfg->active_opacity = (float)atof(val);
        else if (strcmp(key, "inactive_opacity") == 0)
            cfg->inactive_opacity = (float)atof(val);
        else if (strcmp(key, "foot_alpha") == 0) {
            cfg->foot_alpha = (float)atof(val);
            if (cfg->foot_alpha < 0.0f) cfg->foot_alpha = 0.0f;
            if (cfg->foot_alpha > 1.0f) cfg->foot_alpha = 1.0f;
        }
        else if (strcmp(key, "corner_radius") == 0) {
            cfg->corner_radius = atoi(val);
            if (cfg->corner_radius < 0)  cfg->corner_radius = 0;
            if (cfg->corner_radius > 48) cfg->corner_radius = 48;
        }
        else if (strcmp(key, "blur") == 0)
            cfg->blur = strcmp(val, "on") == 0 || strcmp(val, "1") == 0;
        else if (strcmp(key, "blur_passes") == 0) {
            cfg->blur_passes = atoi(val);
            if (cfg->blur_passes < 1) cfg->blur_passes = 1;
            if (cfg->blur_passes > 5) cfg->blur_passes = 5;
        }
        else if (strcmp(key, "blur_radius") == 0) {
            cfg->blur_radius = atoi(val);
            if (cfg->blur_radius < 1)  cfg->blur_radius = 1;
            if (cfg->blur_radius > 20) cfg->blur_radius = 20;
        }
        else if (strcmp(key, "blur_noise") == 0)
            cfg->blur_noise = (float)atof(val);
        else if (strcmp(key, "blur_brightness") == 0)
            cfg->blur_brightness = (float)atof(val);
        else if (strcmp(key, "blur_contrast") == 0)
            cfg->blur_contrast = (float)atof(val);
        else if (strcmp(key, "blur_saturation") == 0)
            cfg->blur_saturation = (float)atof(val);
        else if (strcmp(key, "shadow") == 0)
            cfg->shadow = strcmp(val, "on") == 0 || strcmp(val, "1") == 0;
        else if (strcmp(key, "shadow_blur_sigma") == 0) {
            cfg->shadow_blur_sigma = (float)atof(val);
            if (cfg->shadow_blur_sigma < 0.0f)  cfg->shadow_blur_sigma = 0.0f;
            if (cfg->shadow_blur_sigma > 80.0f) cfg->shadow_blur_sigma = 80.0f;
        }
        else if (strcmp(key, "shadow_offset_x") == 0)
            cfg->shadow_offset_x = atoi(val);
        else if (strcmp(key, "shadow_offset_y") == 0)
            cfg->shadow_offset_y = atoi(val);
        else if (strcmp(key, "shadow_color") == 0) {
            /* RGB only; parse_hex_color forces alpha to 1, so keep the alpha the
             * separate shadow_opacity key (or the default) already set. */
            float rgb[4];
            if (parse_hex_color(val, rgb)) {
                cfg->shadow_color[0] = rgb[0];
                cfg->shadow_color[1] = rgb[1];
                cfg->shadow_color[2] = rgb[2];
            }
        }
        else if (strcmp(key, "shadow_opacity") == 0)
            cfg->shadow_color[3] = clamp01(strtof(val, NULL));
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
        else if (strcmp(key, "effect_phosphor") == 0) {
            if      (strcmp(val, "off")   == 0) cfg->effect_phosphor = SYN_PHOSPHOR_OFF;
            else if (strcmp(val, "green") == 0) cfg->effect_phosphor = SYN_PHOSPHOR_GREEN;
            else if (strcmp(val, "amber") == 0) cfg->effect_phosphor = SYN_PHOSPHOR_AMBER;
            else if (strcmp(val, "white") == 0) cfg->effect_phosphor = SYN_PHOSPHOR_WHITE;
            else wlr_log(WLR_ERROR, "synui: effect_phosphor: unknown '%s'", val);
        }
        else if (strcmp(key, "effect_mono") == 0)
            cfg->effect_mono = clamp01(strtof(val, NULL));
        else if (strcmp(key, "effect_bloom") == 0)
            cfg->effect_bloom = clamp01(strtof(val, NULL));
        else if (strcmp(key, "xkb_rules") == 0)
            strncpy(cfg->xkb_rules, val, sizeof(cfg->xkb_rules) - 1);
        else if (strcmp(key, "xkb_model") == 0)
            strncpy(cfg->xkb_model, val, sizeof(cfg->xkb_model) - 1);
        else if (strcmp(key, "night_light") == 0)
            cfg->night_light = strcmp(val, "on") == 0;
        else if (strcmp(key, "night_light_temp") == 0) {
            int k = atoi(val);
            /* Below ~1000K the ramp is essentially red-only and the screen is
             * unusable; above 6500K it is colder than daylight, which is not
             * what a night light is for. Clamp rather than obey. */
            if (k < 1000) k = 1000;
            if (k > 6500) k = 6500;
            cfg->night_light_temp = k;
        }
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
            /* Driven off syn_wallpaper_mode_names so a new mode only has to be
             * added to the enum and that table — this parser and the Super+W
             * picker both pick it up for free, instead of drifting apart. */
            int found = -1;
            for (int m = 0; m < SYN_WALLPAPER_MODE_COUNT; m++)
                if (strcmp(val, syn_wallpaper_mode_names[m]) == 0) { found = m; break; }
            if (found >= 0) cfg->wallpaper_mode = (syn_wallpaper_mode_t)found;
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
        else if (strcmp(key, "network_cmd") == 0)
            snprintf(cfg->network_cmd, sizeof(cfg->network_cmd), "%s", val);
        else if (strcmp(key, "news_refresh") == 0) {
            int m = atoi(val);
            cfg->news_refresh_min = m < 1 ? 1 : m;   /* never hammer a feed */
        } else if (strcmp(key, "news_source") == 0) {
            /* news_source = TAG|https://example.org/feed.xml
             * Repeatable. The first one replaces the built-in list rather than
             * adding to it: a user listing their own feeds means "these", not
             * "these as well as your eight". */
            char *bar = strchr(val, '|');
            if (!bar) {
                wlr_log(WLR_ERROR, "synui: config: news_source needs TAG|URL: %s",
                        val);
            } else if (cfg->news_sources_n >= NEWS_SOURCES_MAX) {
                wlr_log(WLR_ERROR, "synui: config: too many news_source lines "
                                   "(max %d)", NEWS_SOURCES_MAX);
            } else {
                *bar = '\0';
                syn_news_source_t *src = &cfg->news_sources[cfg->news_sources_n++];
                snprintf(src->name, sizeof(src->name), "%s", strip(val));
                snprintf(src->url,  sizeof(src->url),  "%s", strip(bar + 1));
            }
        }
        else if (strcmp(key, "cat") == 0)
            cfg->cat_start = strcmp(val, "on") == 0;
        else if (strcmp(key, "welcome_at_startup") == 0)
            cfg->welcome_at_startup = strcmp(val, "on") == 0;
        else if (strcmp(key, "numlock") == 0)
            cfg->numlock = strcmp(val, "on") == 0;
        else if (strcmp(key, "dock_enabled") == 0)
            cfg->dock_enabled = strcmp(val, "on") == 0;
        else if (strcmp(key, "dock_autohide") == 0)
            cfg->dock_autohide = strcmp(val, "on") == 0;
        else if (strcmp(key, "dock_edge") == 0) {
            if      (strcmp(val, "bottom") == 0) cfg->dock_edge = SYN_DOCK_EDGE_BOTTOM;
            else if (strcmp(val, "top")    == 0) cfg->dock_edge = SYN_DOCK_EDGE_TOP;
            else if (strcmp(val, "left")   == 0) cfg->dock_edge = SYN_DOCK_EDGE_LEFT;
            else if (strcmp(val, "right")  == 0) cfg->dock_edge = SYN_DOCK_EDGE_RIGHT;
            else wlr_log(WLR_ERROR, "synui: dock_edge: unknown '%s'", val);
        }
        else if (strcmp(key, "launcher_style") == 0) {
            if      (strcmp(val, "text") == 0) cfg->launcher_style = SYN_LAUNCHER_TEXT;
            else if (strcmp(val, "logo") == 0) cfg->launcher_style = SYN_LAUNCHER_LOGO;
            else wlr_log(WLR_ERROR, "synui: launcher_style: unknown '%s'", val);
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
        else if (strcmp(key, "game_mode") == 0)
            cfg->game_mode = strcmp(val, "on") == 0;
        else if (strcmp(key, "game_suspend_ai") == 0)
            cfg->game_suspend_ai = strcmp(val, "on") == 0;
        else if (strcmp(key, "game_inhibit_idle") == 0)
            cfg->game_inhibit_idle = strcmp(val, "on") == 0;
        else if (strcmp(key, "game_ai_stop_cmd") == 0)
            snprintf(cfg->game_ai_stop_cmd, sizeof(cfg->game_ai_stop_cmd), "%s", val);
        else if (strcmp(key, "game_ai_start_cmd") == 0)
            snprintf(cfg->game_ai_start_cmd, sizeof(cfg->game_ai_start_cmd), "%s", val);
        else if (strcmp(key, "game_exclude") == 0) {
            /* space-separated app_ids that are NOT games. Replaces the built-in
             * list rather than adding to it, so a user can widen or narrow it. */
            char buf[512];
            snprintf(buf, sizeof(buf), "%s", val);
            char *save = NULL;
            cfg->game_exclude_count = 0;
            for (char *tok = strtok_r(buf, " \t", &save);
                 tok && cfg->game_exclude_count < GAME_EXCLUDE_MAX;
                 tok = strtok_r(NULL, " \t", &save))
                snprintf(cfg->game_exclude[cfg->game_exclude_count++],
                         sizeof(cfg->game_exclude[0]), "%s", tok);
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
    welcome_state_load(cfg);
    launcher_state_load(cfg);
}
