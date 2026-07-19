/*
 * ctlpanel.c — the control panel (Super+C, and the first entry of the waybar
 * start menu).
 *
 * synui grew its settings one panel at a time — displays on Super+D, filters on
 * Super+E, power on Super+P — each reachable only by already knowing its key.
 * This is the front door: the left column lists every shortcut, the right column
 * carries the handful of toggles worth having in one place, plus a jump-off into
 * each panel that owns the rest.
 *
 * The shortcuts column is *generated from the live bind table* rather than
 * written out here. That is the whole point of it: a hand-maintained list is a
 * list that drifts, and this project has already shipped that bug once, in the
 * waybar start menu, where a stale entry list mapped menu items to the wrong
 * commands. Read the binds and there is nothing to keep in step.
 *
 * Rows are bind *actions*, executed through synui_binding_execute() — the same
 * path a keypress takes. A panel that reimplemented "open the power panel"
 * would be a second definition of it, free to disagree with the first.
 *
 * Keys follow filters.c/power.c (Up/Down select, Enter/Space activate, Esc
 * close), because a third panel that worked a third way would be its own bug.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* ── Settings column ─────────────────────────────────────── */

const char *ctlpanel_row_label(int row)
{
    switch (row) {
    case CTL_ROW_EFFECTS:    return "CRT effects";
    case CTL_ROW_GAME:       return "Game mode";
    case CTL_ROW_AI_BACKEND: return "AI backend";
    case CTL_ROW_DOCK:       return "Dock";
    case CTL_ROW_DOCK_AUTOHIDE: return "Dock auto-hide";
    case CTL_ROW_TITLEBARS:  return "Titlebars";
    case CTL_ROW_LAUNCHER:   return "Start button";
    case CTL_ROW_TRANSPARENCY: return "Transparency";
    case CTL_ROW_SEP:        return "";
    case CTL_ROW_THEME:      return "Theme \xe2\x80\xa6";
    case CTL_ROW_DISPLAYS:   return "Display settings";
    case CTL_ROW_FILTERS:    return "CRT filters \xe2\x80\xa6";
    case CTL_ROW_WALLPAPER:  return "Wallpaper \xe2\x80\xa6";
    case CTL_ROW_POWER:      return "Power saving \xe2\x80\xa6";
    case CTL_ROW_TASKMGR:    return "Task manager \xe2\x80\xa6";
    case CTL_ROW_NETWORK:    return "Network / Wi-Fi \xe2\x80\xa6";
    case CTL_ROW_BLUETOOTH:  return "Bluetooth \xe2\x80\xa6";
    case CTL_ROW_PRINTERS:   return "Printers \xe2\x80\xa6";
    case CTL_ROW_LOCK:       return "Lock screen";
    default:                 return "?";
    }
}

int ctlpanel_row_selectable(int row)
{
    return row != CTL_ROW_SEP;
}

/* synapd's current inference device, as recorded by synui-ai-backend. Absent
 * file means nothing has toggled it yet, so synapd's own auto-detect stands. */
static const char *ai_backend_label(void)
{
    FILE *f = fopen("/run/synapd/backend", "r");
    if (!f) return "auto";
    char b[16] = {0};
    size_t n = fread(b, 1, sizeof(b) - 1, f);
    fclose(f);
    while (n > 0 && (b[n - 1] == '\n' || b[n - 1] == ' ')) b[--n] = 0;
    if (strcmp(b, "gpu") == 0) return "GPU";
    if (strcmp(b, "cpu") == 0) return "CPU";
    if (strcmp(b, "off") == 0) return "off";
    return "auto";
}

void ctlpanel_row_value(syn_server_t *s, int row, char *buf, size_t n)
{
    switch (row) {
    case CTL_ROW_EFFECTS:
        /* Two ways for the effects to be off, and they are not the same thing:
         * the master switch, or no GLES pass to switch on (pixman/VM). */
        if (!s->effects)      snprintf(buf, n, "n/a");
        else                  snprintf(buf, n, "%s", s->config.effects ? "on" : "off");
        break;
    case CTL_ROW_GAME:
        snprintf(buf, n, "%s", s->game.active ? "on" : "off");
        break;
    case CTL_ROW_AI_BACKEND:
        snprintf(buf, n, "%s", ai_backend_label());
        break;
    case CTL_ROW_DOCK:
        snprintf(buf, n, "%s", s->config.dock_enabled ? "on" : "off");
        break;
    case CTL_ROW_DOCK_AUTOHIDE:
        /* "n/a" when the dock is off entirely: hide-behaviour is moot. */
        if (!s->config.dock_enabled) snprintf(buf, n, "n/a");
        else snprintf(buf, n, "%s", s->config.dock_autohide ? "on" : "off");
        break;
    case CTL_ROW_TITLEBARS:
        /* The row is the titlebars, not the hiding of them: "on" means shown. */
        snprintf(buf, n, "%s", s->titlebars_hidden ? "off" : "on");
        break;
    case CTL_ROW_LAUNCHER:
        snprintf(buf, n, "%s",
                 s->config.launcher_style == SYN_LAUNCHER_LOGO ? "logo" : "text");
        break;
    case CTL_ROW_TRANSPARENCY:
        /* Show the slider position too, so the row reads "on · 90%" — Left/Right
         * adjust it. Off hides the number: the levels are dormant then. */
        if (s->config.transparency)
            snprintf(buf, n, "on \xc2\xb7 %d%%",
                     (int)(s->config.active_opacity * 100 + 0.5f));
        else
            snprintf(buf, n, "off");
        break;
    case CTL_ROW_THEME:
        /* A jump-off, but showing the active theme here saves opening the panel
         * just to read which one is on. */
        snprintf(buf, n, "%s", theme_name(s->config.theme));
        break;
    default:
        buf[0] = '\0';   /* jump-offs have no state of their own */
        break;
    }
}

/* ── Shortcuts column ────────────────────────────────────── */

/* What a bind action does, in words. An action with no entry here still lists —
 * it falls back to the action name — so a bind added to input.c and forgotten
 * here degrades to "slightly terse", not "missing from the panel". */
static const char *action_desc(const char *action, const char *arg)
{
    static const struct { const char *action, *desc; } tbl[] = {
        { "term",              "Terminal" },
        { "cmdbar",            "AI command bar" },
        { "ai_ask",            "Ask the AI about the window" },
        { "overlay",           "Neural overlay" },
        { "menu",              "Welcome menu" },
        { "control",           "Control panel" },
        { "bluetooth",         "Bluetooth" },
        { "printers",          "Printers" },
        { "night_light",       "Night light" },
        { "record",            "Record screen" },
        { "clipboard",         "Clipboard history" },
        { "brightness_up",     "Brightness up" },
        { "brightness_down",   "Brightness down" },
        { "start_menu",        "Start menu" },
        { "launcher_style",    "Start button: text/logo" },
        { "close",             "Close window" },
        { "quit",              "Quit synui" },
        { "layout_cycle",      "Cycle layout" },
        { "master_shrink",     "Shrink master area" },
        { "master_grow",       "Grow master area" },
        { "focus_next",        "Focus next window" },
        { "focus_prev",        "Focus previous window" },
        { "stack_next",        "Move window down the stack" },
        { "stack_prev",        "Move window up the stack" },
        { "float_toggle",      "Float window" },
        { "fullscreen_toggle", "Fullscreen window" },
        { "maximize_toggle",   "Maximize window" },
        { "minimize_toggle",   "Minimize window" },
        { "minimize_restore",  "Restore minimized window" },
        { "decorations_toggle","Titlebars on/off" },
        { "displays",          "Display settings" },
        { "wallpaper",         "Wallpaper picker" },
        { "wallpaper_reload",  "Reload wallpaper / config" },
        { "filters",           "CRT filter panel" },
        { "effects_toggle",    "CRT effects on/off" },
        { "power",             "Power saving panel" },
        { "taskmgr",           "Task manager" },
        { "network",           "Network / Wi-Fi" },
        { "game",              "Game mode" },
        { "lock",              "Lock screen" },
        { "ai_backend",        "AI backend (GPU/CPU/off)" },
        { "move_output",       "Move window to next output" },
    };

    /* A spawn bind is only meaningful as the thing it spawns. */
    if (strcmp(action, "spawn") == 0 && arg && *arg) return arg;

    for (unsigned i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++)
        if (strcmp(action, tbl[i].action) == 0) {
            /* move_output takes a direction; "prev" is a different line. */
            if (strcmp(action, "move_output") == 0 && arg && strcmp(arg, "prev") == 0)
                return "Move window to previous output";
            return tbl[i].desc;
        }
    return action;
}

/* xkbcommon spells keys for machines ("Return", "space", "e"). Spell them the
 * way a keycap does, or the column reads like a config file. */
static void key_name(xkb_keysym_t sym, char *out, size_t n)
{
    switch (sym) {
    case XKB_KEY_Return:    snprintf(out, n, "Enter");     return;
    case XKB_KEY_KP_Enter:  snprintf(out, n, "KP Enter");  return;
    case XKB_KEY_space:     snprintf(out, n, "Space");     return;
    case XKB_KEY_Escape:    snprintf(out, n, "Esc");       return;
    case XKB_KEY_Delete:    snprintf(out, n, "Del");       return;
    case XKB_KEY_BackSpace: snprintf(out, n, "Backspace"); return;
    case XKB_KEY_Tab:       snprintf(out, n, "Tab");       return;
    default: break;
    }
    char raw[64] = {0};
    if (xkb_keysym_get_name(sym, raw, sizeof(raw)) <= 0) {
        snprintf(out, n, "?");
        return;
    }
    /* Single letters read as keycaps: "e" -> "E". */
    if (raw[1] == '\0' && raw[0] >= 'a' && raw[0] <= 'z')
        raw[0] = (char)(raw[0] - 'a' + 'A');
    snprintf(out, n, "%s", raw);
}

/* Modifier order matches how the binds are written in synuirc (super+shift+q),
 * so the panel and the config spell the same combo the same way. */
static void combo_str(uint32_t mods, xkb_keysym_t sym, char *out, size_t n)
{
    char key[64];
    key_name(sym, key, sizeof(key));
    snprintf(out, n, "%s%s%s%s%s",
             (mods & WLR_MODIFIER_LOGO)  ? "Super+" : "",
             (mods & WLR_MODIFIER_CTRL)  ? "Ctrl+"  : "",
             (mods & WLR_MODIFIER_ALT)   ? "Alt+"   : "",
             (mods & WLR_MODIFIER_SHIFT) ? "Shift+" : "",
             key);
}

int ctlpanel_shortcuts(syn_server_t *s, syn_ctl_shortcut_t *out, int max)
{
    int n = 0;
    int saw_ws = 0, saw_movews = 0;

    /* Super-tap is the one shortcut that is not a bind — it is defined by the
     * absence of a chord (see syn_server::super_armed), so it appears in no
     * bind table and would otherwise be the one feature this panel hid. */
    if (n < max) {
        snprintf(out[n].combo, sizeof(out[n].combo), "Super (tap)");
        snprintf(out[n].desc,  sizeof(out[n].desc),  "Start menu");
        n++;
    }

    for (int i = 0; i < s->config.bind_count && n < max; i++) {
        const syn_bind_t *b = &s->config.binds[i];

        /* The nine workspace binds and the nine move-to-workspace binds are
         * collapsed into one row each below: listed one per line they are 18
         * of ~40 rows, and they bury everything else in the column. */
        if (strcmp(b->action, "ws") == 0)     { saw_ws = 1;     continue; }
        if (strcmp(b->action, "movews") == 0) { saw_movews = 1; continue; }

        combo_str(b->mods, b->sym, out[n].combo, sizeof(out[n].combo));
        snprintf(out[n].desc, sizeof(out[n].desc), "%s",
                 action_desc(b->action, b->arg));
        n++;
    }

    if (saw_ws && n < max) {
        snprintf(out[n].combo, sizeof(out[n].combo), "Super+1\xe2\x80\x93""9");
        snprintf(out[n].desc,  sizeof(out[n].desc),  "Switch to workspace");
        n++;
    }
    if (saw_movews && n < max) {
        snprintf(out[n].combo, sizeof(out[n].combo), "Super+Shift+1\xe2\x80\x93""9");
        snprintf(out[n].desc,  sizeof(out[n].desc),  "Move window to workspace");
        n++;
    }
    return n;
}

/* ── Panel ───────────────────────────────────────────────── */

/* Toggling a config float changes no scene node, so nothing would repaint on
 * its own. Force it, or the panel shows a state the screen disagrees with. */
static void ctlpanel_repaint(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (o->scene_output)
            wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
        wlr_output_schedule_frame(o->wlr_output);
    }
}

void ctlpanel_show(syn_server_t *s)
{
    s->ctlpanel.visible   = 1;
    s->ctlpanel.selected  = CTL_ROW_EFFECTS;
    s->ctlpanel.scroll    = 0;
    s->ctlpanel.status[0] = '\0';
    wlr_log(WLR_INFO, "synui: control panel shown");
    synui_render_ctlpanel(s);
}

void ctlpanel_hide(syn_server_t *s)
{
    s->ctlpanel.visible = 0;
    synui_render_ctlpanel(s);
}

void ctlpanel_toggle(syn_server_t *s)
{
    if (s->ctlpanel.visible) ctlpanel_hide(s);
    else                     ctlpanel_show(s);
}

/* Skip the separator in both directions rather than letting the cursor land on
 * a rule that does nothing when you press Enter. */
static void ctlpanel_move(syn_server_t *s, int dir)
{
    int row = s->ctlpanel.selected;
    do {
        row += dir;
        if (row < 0 || row >= CTL_ROW_COUNT) return;   /* stop at the ends */
    } while (!ctlpanel_row_selectable(row));
    s->ctlpanel.selected = row;
}

static void ctlpanel_activate(syn_server_t *s)
{
    switch (s->ctlpanel.selected) {
    case CTL_ROW_EFFECTS:
        if (!s->effects) {   /* no GLES pass — say so rather than lie */
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "no GLES renderer \xc2\xb7 effects unavailable here");
            return;
        }
        s->config.effects = !s->config.effects;
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "CRT effects %s", s->config.effects ? "on" : "off");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_GAME:
        game_toggle(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "game mode %s", s->game.active ? "on" : "off");
        return;

    case CTL_ROW_DOCK:
        s->config.dock_enabled = !s->config.dock_enabled;
        dock_rebuild(s);
        dock_relayout(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "dock %s", s->config.dock_enabled ? "on" : "off");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_DOCK_AUTOHIDE:
        /* No-op while the dock is off — the row reads "n/a" then, and toggling
         * a hidden dock's hide-behaviour would only confuse. */
        if (!s->config.dock_enabled) {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "dock is off");
            ctlpanel_repaint(s);
            return;
        }
        s->config.dock_autohide = !s->config.dock_autohide;
        dock_state_save(s);   /* persist to dock.state, like edge/pins */
        dock_wake(s);         /* pin or release the bar on the next frame */
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "dock auto-hide %s", s->config.dock_autohide ? "on" : "off");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_TITLEBARS:
        /* Same call the Super+Shift+D bind makes — it re-runs the layout for
         * every view, which a bare repaint here would not: the clients have to
         * be resized for the titlebar that just came or went. */
        deco_toggle_titlebars(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "titlebars %s", s->titlebars_hidden ? "off" : "on");
        return;

    case CTL_ROW_LAUNCHER:
        /* Same call the start-menu Settings row and any bind make: it flips the
         * style, redraws the button on every output, and persists to
         * launcher.state. Repaint after so this row's value reads the new style. */
        launcher_toggle_style(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "start button: %s",
                 s->config.launcher_style == SYN_LAUNCHER_LOGO ? "logo" : "text");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_TRANSPARENCY:
        /* Enter flips the master switch; Left/Right (below) drive the level. The
         * helper re-pushes alpha to every window and persists, and turns a still-
         * opaque desktop visibly translucent so "on" is not a no-op. */
        transparency_set_enabled(s, !s->config.transparency);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 s->config.transparency ? "transparency on \xc2\xb7 %d%%" : "transparency off",
                 (int)(s->config.active_opacity * 100 + 0.5f));
        return;

    case CTL_ROW_THEME:
        ctlpanel_hide(s);
        synui_binding_execute(s, "theme", NULL);
        return;

    case CTL_ROW_AI_BACKEND:
        /* The helper owns the work (systemd drop-in, restart synapd); it is
         * not instant, so the row still reads the old device until it lands. */
        synui_binding_execute(s, "ai_backend", NULL);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "switching AI backend \xe2\x80\xa6");
        return;

    /* Jump-offs: the panel that owns the setting is the one that should edit
     * it, so hand over rather than grow a second set of controls here. */
    case CTL_ROW_DISPLAYS:  ctlpanel_hide(s); synui_binding_execute(s, "displays",  NULL); return;
    case CTL_ROW_FILTERS:   ctlpanel_hide(s); synui_binding_execute(s, "filters",   NULL); return;
    case CTL_ROW_WALLPAPER: ctlpanel_hide(s); synui_binding_execute(s, "wallpaper", NULL); return;
    case CTL_ROW_POWER:     ctlpanel_hide(s); synui_binding_execute(s, "power",     NULL); return;
    case CTL_ROW_TASKMGR:   ctlpanel_hide(s); synui_binding_execute(s, "taskmgr",   NULL); return;
    case CTL_ROW_NETWORK:   ctlpanel_hide(s); synui_binding_execute(s, "network",   NULL); return;
    case CTL_ROW_BLUETOOTH: ctlpanel_hide(s); synui_binding_execute(s, "bluetooth", NULL); return;
    case CTL_ROW_PRINTERS:  ctlpanel_hide(s); synui_binding_execute(s, "printers",  NULL); return;
    case CTL_ROW_LOCK:      ctlpanel_hide(s); synui_binding_execute(s, "lock",      NULL); return;

    default:
        return;
    }
}

/* Clamp the shortcuts scroll to the list — the panel draws CTL_SHORTCUT_ROWS
 * of it at a time, so scrolling past the end would show empty space. */
static void ctlpanel_scroll(syn_server_t *s, int dir)
{
    syn_ctl_shortcut_t sc[CTL_SHORTCUTS_MAX];
    int n = ctlpanel_shortcuts(s, sc, CTL_SHORTCUTS_MAX);
    int max_scroll = n - CTL_SHORTCUT_ROWS;
    if (max_scroll < 0) max_scroll = 0;

    int next = s->ctlpanel.scroll + dir;
    if (next < 0) next = 0;
    if (next > max_scroll) next = max_scroll;
    s->ctlpanel.scroll = next;
}

/* Left/Right on the Transparency row are a slider: nudge the focused-window
 * opacity in 5% steps, turning transparency on first if it is off (you would not
 * be adjusting it otherwise). Everywhere else Left/Right scroll the shortcuts. */
static void ctlpanel_adjust_opacity(syn_server_t *s, int dir)
{
    if (!s->config.transparency) transparency_set_enabled(s, 1);
    transparency_set_opacity(s, s->config.active_opacity + dir * 0.05f);
    snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
             "transparency %d%%", (int)(s->config.active_opacity * 100 + 0.5f));
}

int ctlpanel_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->ctlpanel.visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table, so Super+C
     * closes the panel it opened and Super+P still opens the power panel. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        ctlpanel_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        ctlpanel_move(s, -1);
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        ctlpanel_move(s, +1);
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        ctlpanel_activate(s);
        /* A jump-off already hid the panel and opened another one; re-rendering
         * here would draw this panel back over the top of it. */
        if (s->ctlpanel.visible) synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Left:
    case XKB_KEY_h:
        if (s->ctlpanel.selected == CTL_ROW_TRANSPARENCY)
            ctlpanel_adjust_opacity(s, -1);
        else
            ctlpanel_scroll(s, -CTL_SHORTCUT_ROWS / 2);
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        if (s->ctlpanel.selected == CTL_ROW_TRANSPARENCY)
            ctlpanel_adjust_opacity(s, +1);
        else
            ctlpanel_scroll(s, +CTL_SHORTCUT_ROWS / 2);
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Prior:     /* Page Up — always the shortcuts scroll */
        ctlpanel_scroll(s, -CTL_SHORTCUT_ROWS / 2);
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Next:      /* Page Down */
        ctlpanel_scroll(s, +CTL_SHORTCUT_ROWS / 2);
        synui_render_ctlpanel(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
