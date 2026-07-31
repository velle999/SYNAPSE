/*
 * ctlpanel.c — the control panel (Super+C, and the first entry of the waybar
 * start menu).
 *
 * synui grew its settings one panel at a time — displays on Super+D, filters on
 * Super+E, power on Super+P — each reachable only by already knowing its key.
 * This is the front door, and since the categories landed it is the *whole*
 * front door rather than an index to twenty other ones: a category list down the
 * left, that category's rows on the right, and the panels that own the details
 * opening from it and handing control back to it when they close.
 *
 * Two things make it one system rather than a launcher:
 *
 *   - One item table (ctl_items[] below) is the only place a setting is
 *     declared. The sidebar, the row pane, the cursor and the key handler all
 *     walk it, so a row cannot appear in one and be missing from another.
 *
 *   - A row that opens a panel arms syn_ctlpanel_t::child, and that panel's
 *     hide path calls ctlpanel_child_closed(). Esc in Displays therefore lands
 *     back on Display ▸ Display settings, not on the desktop. Opened by their
 *     own keybind the same panels close to the desktop as they always did —
 *     child is empty then, and the call is a no-op.
 *
 * The shortcuts category is *generated from the live bind table* rather than
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
 * Left/Right and Tab are the addition categories needed: they move between the
 * two columns, and Esc in the right column steps back to the left before it
 * closes anything — the same "back out one level" every menu tree has.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wlr/types/wlr_damage_ring.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* How long to keep repainting the panel after an AI-backend switch, waiting for
 * the helper to restart synapd and write /run/synapd/backend. Generous: it is a
 * service restart, and the poll stops early the moment the value changes. */
#define CTL_BACKEND_POLL_SECS  8.0

static double ctl_now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ── The item table ──────────────────────────────────────────
 *
 * Every row in the panel, once. `action` is the bind action a PANEL/LAUNCH/
 * ACTION row fires; the in-place toggles have none, because there is nothing to
 * hand to — they are handled by id in ctlpanel_activate().
 *
 * Rows are listed in display order and grouped by category. Nothing enforces
 * that grouping (the walk filters on .cat), but keeping the literal order and
 * the drawn order the same means one read of this table tells you what the
 * panel looks like.
 */
static const struct {
    int             row;
    syn_ctl_cat_t   cat;
    syn_ctl_kind_t  kind;
    const char     *label;
    const char     *action;
} ctl_items[] = {
    /* Appearance */
    { CTL_ROW_THEME,        CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "Theme",            "theme"     },
    { CTL_ROW_WALLPAPER,    CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "Wallpaper",        "wallpaper" },
    { CTL_ROW_CURSOR,       CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "Cursor theme",     "cursor"    },
    { CTL_ROW_EFFECTS,      CTL_CAT_APPEARANCE, CTL_KIND_TOGGLE, "CRT effects",      NULL        },
    { CTL_ROW_FILTERS,      CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "CRT filters",      "filters"   },
    { CTL_ROW_TRANSPARENCY, CTL_CAT_APPEARANCE, CTL_KIND_SLIDER, "Transparency",     NULL        },
    { CTL_ROW_TITLEBARS,    CTL_CAT_APPEARANCE, CTL_KIND_TOGGLE, "Titlebars",        NULL        },

    /* Desktop */
    { CTL_ROW_DOCK,          CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Dock",             NULL      },
    { CTL_ROW_DOCK_AUTOHIDE, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Dock auto-hide",   NULL      },
    { CTL_ROW_LAUNCHER,      CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Start button",     NULL      },
    { CTL_ROW_WIDGETS,       CTL_CAT_DESKTOP, CTL_KIND_PANEL,  "Desktop widgets",  "widgets" },

    /* Display */
    { CTL_ROW_DISPLAYS,   CTL_CAT_DISPLAY, CTL_KIND_PANEL,  "Display settings", "displays" },
    { CTL_ROW_NIGHTLIGHT, CTL_CAT_DISPLAY, CTL_KIND_TOGGLE, "Night light",      NULL       },
    { CTL_ROW_CLOCK,      CTL_CAT_DISPLAY, CTL_KIND_PANEL,  "Date & time",      "clock"    },

    /* Sound */
    { CTL_ROW_SOUNDS, CTL_CAT_SOUND, CTL_KIND_PANEL, "Event sounds", "sounds" },

    /* Network. Two of the three hand off to something synui does not own —
     * nmtui in a terminal, cups in a browser — so they close the panel rather
     * than arming a return to it. */
    { CTL_ROW_NETWORK,   CTL_CAT_NETWORK, CTL_KIND_LAUNCH, "Network / Wi-Fi", "network"   },
    { CTL_ROW_BLUETOOTH, CTL_CAT_NETWORK, CTL_KIND_PANEL,  "Bluetooth",       "bluetooth" },
    { CTL_ROW_PRINTERS,  CTL_CAT_NETWORK, CTL_KIND_LAUNCH, "Printers",        "printers"  },

    /* Power */
    { CTL_ROW_POWER, CTL_CAT_POWER, CTL_KIND_PANEL,  "Power saving", "power" },
    { CTL_ROW_GAME,  CTL_CAT_POWER, CTL_KIND_TOGGLE, "Game mode",    NULL    },
    { CTL_ROW_LOCK,  CTL_CAT_POWER, CTL_KIND_ACTION, "Lock screen",  "lock"  },

    /* System */
    { CTL_ROW_TASKMGR,    CTL_CAT_SYSTEM, CTL_KIND_PANEL,  "Task manager",      "taskmgr"   },
    { CTL_ROW_AI_BACKEND, CTL_CAT_SYSTEM, CTL_KIND_TOGGLE, "AI backend",        NULL        },
    { CTL_ROW_NEWS,       CTL_CAT_SYSTEM, CTL_KIND_PANEL,  "News",              "news"      },
    { CTL_ROW_CLIPBOARD,  CTL_CAT_SYSTEM, CTL_KIND_PANEL,  "Clipboard history", "clipboard" },
};

#define CTL_ITEM_COUNT ((int)(sizeof(ctl_items) / sizeof(ctl_items[0])))

static int ctl_item_index(int row)
{
    for (int i = 0; i < CTL_ITEM_COUNT; i++)
        if (ctl_items[i].row == row) return i;
    return -1;
}

const char *ctlpanel_cat_name(int cat)
{
    switch (cat) {
    case CTL_CAT_APPEARANCE: return "Appearance";
    case CTL_CAT_DESKTOP:    return "Desktop";
    case CTL_CAT_DISPLAY:    return "Display";
    case CTL_CAT_SOUND:      return "Sound";
    case CTL_CAT_NETWORK:    return "Network";
    case CTL_CAT_POWER:      return "Power";
    case CTL_CAT_SYSTEM:     return "System";
    case CTL_CAT_SHORTCUTS:  return "Shortcuts";
    default:                 return "?";
    }
}

int ctlpanel_cat_items(int cat, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < CTL_ITEM_COUNT && n < max; i++)
        if ((int)ctl_items[i].cat == cat) out[n++] = ctl_items[i].row;
    return n;
}

syn_ctl_kind_t ctlpanel_row_kind(int row)
{
    int i = ctl_item_index(row);
    return i < 0 ? CTL_KIND_TOGGLE : ctl_items[i].kind;
}

static const char *ctl_row_action(int row)
{
    int i = ctl_item_index(row);
    return i < 0 ? NULL : ctl_items[i].action;
}

const char *ctlpanel_row_label(int row)
{
    int i = ctl_item_index(row);
    return i < 0 ? "?" : ctl_items[i].label;
}

/* The shortcuts category has no rows of its own — it is one scrolling list, and
 * the cursor there drives the scroll instead. -1 says so, and every caller that
 * would act on a row has to check it. */
int ctlpanel_selected_row(syn_server_t *s)
{
    int rows[CTL_CAT_ITEMS_MAX];
    int n = ctlpanel_cat_items(s->ctlpanel.cat, rows, CTL_CAT_ITEMS_MAX);
    if (n == 0) return -1;

    int i = s->ctlpanel.item;
    if (i < 0)  i = 0;
    if (i >= n) i = n - 1;
    return rows[i];
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

/* Read the desktop-widget toggles the same way the bar does — straight out of
 * widgets.state. synui does not own that state (synui-widgets writes it and
 * quickshell watches it), so there is nothing in syn_server_t to read; the file
 * IS the state, and a missing file means nothing has been switched on yet.
 *
 * "partial" is a real answer, not a fudge: the four widgets toggle
 * independently from the CLI, so "on"/"off" alone would misreport a desktop
 * with only the clock up. */
static const char *widgets_label(void)
{
    char path[256];
    if (!syn_config_path(path, sizeof(path), "widgets.state")) return "off";

    FILE *f = fopen(path, "r");
    if (!f) return "off";

    int on = 0, total = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        const char *eq = strchr(line, '=');
        if (!eq) continue;
        total++;
        /* Skip the spaces the writer puts either side of the '='. */
        const char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        if (strncmp(v, "on", 2) == 0) on++;
    }
    fclose(f);

    if (total == 0 || on == 0) return "off";
    return on == total ? "on" : "partial";
}

/* Event sounds, summarised the same way. Unlike the widgets this state IS
 * mirrored in syn_server_t (sound.c caches it to skip a fork per event), so the
 * summary is read from the cache — refreshed first, because the panel is one of
 * the few places that must show what is on disk right now. "off" covers both a
 * silent master switch and every event being off: from this row's height they
 * are the same desktop. */
static const char *sounds_label(syn_server_t *s)
{
    sound_state_refresh(s);
    if (!s->sound.enabled) return "off";

    int on = 0;
    for (int i = 0; i < SOUND_EVT_COUNT; i++)
        if (s->sound.on[i]) on++;

    if (on == 0)               return "off";
    if (s->sound.volume <= 0)  return "muted";
    return on == SOUND_EVT_COUNT ? "on" : "partial";
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
    case CTL_ROW_NIGHTLIGHT:
        /* The temperature only means anything while it is on — off is the
         * identity ramp, and "off · 4000K" would read as a warm screen. */
        if (s->config.night_light)
            snprintf(buf, n, "on \xc2\xb7 %dK", s->config.night_light_temp);
        else
            snprintf(buf, n, "off");
        break;
    case CTL_ROW_WIDGETS:
        snprintf(buf, n, "%s", widgets_label());
        break;
    case CTL_ROW_SOUNDS:
        snprintf(buf, n, "%s", sounds_label(s));
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
        { "alt_tab",           "Switch window (Alt+Tab)" },
        { "alt_tab_prev",      "Switch window, backwards" },
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
        { "filters",           "Visual effects (CRT + window)" },
        { "widgets",           "Desktop widget manager" },
        { "sounds",            "Event sounds" },
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

/*
 * Called once per frame from output_frame. Returns 1 while it wants more frames.
 *
 * Only the AI-backend row needs this: every other value on the panel is state
 * synui itself owns and changes synchronously, so the keypress that changed it
 * also repaints. That one is read back from a file the synui-ai-backend helper
 * writes after restarting synapd, which lands whenever it lands — so the panel
 * has to look again rather than be told. Stops as soon as the label changes
 * (the common case, well under a second) or the deadline passes, so an idle
 * panel costs nothing.
 */
int ctlpanel_tick(syn_server_t *s)
{
    if (s->ctlpanel.backend_poll_until == 0.0)
        return 0;

    /* Closing the panel abandons the poll: there is nothing left to repaint. */
    if (!s->ctlpanel.visible) {
        s->ctlpanel.backend_poll_until = 0.0;
        return 0;
    }

    /* Only the AI backend polls now: it is the one row whose value is set by a
     * helper this panel cannot wait on. The desktop-widget row used to, back
     * when it flipped the widgets in place; it opens the manager instead. */
    const char *poll_name = "AI backend";

    char now_val[16];
    ctlpanel_row_value(s, s->ctlpanel.poll_row, now_val, sizeof(now_val));

    if (strcmp(now_val, s->ctlpanel.backend_before) != 0) {
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "%s: %s", poll_name, now_val);
        s->ctlpanel.backend_poll_until = 0.0;   /* landed — stop polling */
        synui_render_ctlpanel(s);
        return 0;
    }

    if (ctl_now_secs() >= s->ctlpanel.backend_poll_until) {
        /* Timed out. Say so rather than leaving "switching …" on screen for
         * good — a helper that failed (no polkit, synapd wedged) is exactly the
         * case where a stuck spinner reads as "it worked". */
        s->ctlpanel.backend_poll_until = 0.0;
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "%s: still %s \xc2\xb7 switch did not land", poll_name, now_val);
        synui_render_ctlpanel(s);
        return 0;
    }

    return 1;   /* keep the frames coming */
}

void ctlpanel_show(syn_server_t *s)
{
    s->ctlpanel.visible   = 1;
    s->ctlpanel.cat       = CTL_CAT_APPEARANCE;
    s->ctlpanel.item      = 0;
    s->ctlpanel.focus     = CTL_FOCUS_CATS;
    s->ctlpanel.scroll    = 0;
    s->ctlpanel.status[0] = '\0';
    s->ctlpanel.child[0]  = '\0';
    s->ctlpanel.backend_poll_until = 0.0;
    s->ctlpanel.poll_row  = CTL_ROW_AI_BACKEND;
    wlr_log(WLR_INFO, "synui: control panel shown");
    synui_render_ctlpanel(s);
}

void ctlpanel_hide(syn_server_t *s)
{
    s->ctlpanel.visible  = 0;
    /* Dismissing the panel abandons any return it was holding: the sub-panel may
     * still be up, and it closing later must not resurrect a panel the user has
     * already put away. */
    s->ctlpanel.child[0] = '\0';
    synui_render_ctlpanel(s);
}

void ctlpanel_toggle(syn_server_t *s)
{
    if (s->ctlpanel.visible) ctlpanel_hide(s);
    else                     ctlpanel_show(s);
}

int ctlpanel_cat_from_name(const char *name)
{
    if (!name || !*name) return -1;
    for (int c = 0; c < CTL_CAT_COUNT; c++) {
        if (strcasecmp(name, ctlpanel_cat_name(c)) == 0) return c;
    }
    return -1;
}

/*
 * Open straight onto a category — what `synctl dispatch control display` does,
 * and with it the start menu's Settings submenu.
 *
 * That submenu used to be its own hand-written list of thirteen bind actions in
 * StartMenu.qml, which is the failure this file's header warns about in the
 * other direction: it had drifted to missing Theme, Printers, Task manager and
 * six more, and nothing could have told you. Naming a *category* leaves the
 * contents to the one item table, so the two menus cannot disagree about what
 * settings exist.
 *
 * Shows rather than toggles: a menu row that closed the panel when it happened
 * to be open already would be a row that does the opposite of what it says.
 */
void ctlpanel_show_cat(syn_server_t *s, const char *name)
{
    int cat = ctlpanel_cat_from_name(name);
    if (cat < 0) {                 /* unknown name: the plain front door */
        ctlpanel_toggle(s);
        return;
    }

    if (!s->ctlpanel.visible) ctlpanel_show(s);
    s->ctlpanel.cat    = cat;
    s->ctlpanel.item   = 0;
    s->ctlpanel.scroll = 0;
    /* Focus lands in the rows: the caller already chose the category, and
     * putting the cursor back on the sidebar would make them choose it again. */
    s->ctlpanel.focus  = CTL_FOCUS_ITEMS;
    synui_render_ctlpanel(s);
}

/* Hide the panel *without* forgetting where the cursor was or which child is
 * out — the difference between "the user closed it" and "it stepped aside for
 * the panel it just opened". */
static void ctlpanel_conceal(syn_server_t *s)
{
    s->ctlpanel.visible = 0;
    synui_render_ctlpanel(s);
}

/* Bring it back on the category and row it was left on, rather than resetting to
 * the top the way ctlpanel_show() does. */
static void ctlpanel_resume(syn_server_t *s, int cat, int item)
{
    s->ctlpanel.visible = 1;
    s->ctlpanel.cat     = cat;
    s->ctlpanel.item    = item;
    s->ctlpanel.focus   = CTL_FOCUS_ITEMS;
    synui_render_ctlpanel(s);
}

/* Is the panel `action` opens actually on screen? Asked immediately after firing
 * the action, to tell "it opened" from "it was already open and the toggle just
 * closed it" — only the first arms a return, and the second has to reopen this
 * panel or the keypress would look like it did nothing.
 *
 * A row whose panel is not listed here simply never arms a return. That is the
 * safe direction to fail: a missed hook costs one Esc, a wrong one pops the
 * control panel open at some unrelated moment. */
static int ctl_child_is_up(syn_server_t *s, const char *action)
{
    if (strcmp(action, "theme") == 0)     return s->thememgr.visible;
    if (strcmp(action, "wallpaper") == 0) return s->wppick.visible;
    if (strcmp(action, "cursor") == 0)    return s->curpick.visible;
    if (strcmp(action, "filters") == 0)   return s->filters.visible;
    if (strcmp(action, "widgets") == 0)   return s->widgets.visible;
    if (strcmp(action, "displays") == 0)  return s->dispcfg.visible;
    if (strcmp(action, "clock") == 0)     return s->clock.visible;
    if (strcmp(action, "sounds") == 0)    return s->sound.visible;
    if (strcmp(action, "bluetooth") == 0) return s->bt.visible;
    if (strcmp(action, "power") == 0)     return s->power.visible;
    if (strcmp(action, "taskmgr") == 0)   return s->taskmgr.visible;
    if (strcmp(action, "news") == 0)      return s->news.visible;
    if (strcmp(action, "clipboard") == 0) return s->clipboard.visible;
    return 0;
}

/* Step aside for the panel that owns this setting, and arrange to come back. */
static void ctlpanel_open_child(syn_server_t *s, const char *action)
{
    int cat = s->ctlpanel.cat, item = s->ctlpanel.item;

    s->ctlpanel.child[0] = '\0';   /* nothing armed while the action runs */
    ctlpanel_conceal(s);
    synui_binding_execute(s, action, NULL);

    if (!ctl_child_is_up(s, action)) {
        /* The action toggled a panel that was already open, so it is now shut
         * and there is nothing to come back from. Come back immediately. */
        ctlpanel_resume(s, cat, item);
        return;
    }

    snprintf(s->ctlpanel.child, sizeof(s->ctlpanel.child), "%s", action);
    s->ctlpanel.child_cat  = cat;
    s->ctlpanel.child_item = item;
}

void ctlpanel_child_closed(syn_server_t *s, const char *action)
{
    if (!action || s->ctlpanel.child[0] == '\0') return;
    if (strcmp(s->ctlpanel.child, action) != 0) return;

    s->ctlpanel.child[0] = '\0';

    /* A lock that came down while a sub-panel was open closes it as part of
     * clearing the screen. Popping the control panel up behind the lock screen
     * would leave it there for whoever unlocks. */
    if (s->locked) return;

    ctlpanel_resume(s, s->ctlpanel.child_cat, s->ctlpanel.child_item);
}

/* ── Navigation ──────────────────────────────────────────── */

static int ctlpanel_item_count(syn_server_t *s)
{
    int rows[CTL_CAT_ITEMS_MAX];
    return ctlpanel_cat_items(s->ctlpanel.cat, rows, CTL_CAT_ITEMS_MAX);
}

/* Moving to another category resets the row cursor and the shortcuts scroll:
 * carrying row 4 into a category with two rows, or a scroll offset into a list
 * that is not the one it was measured against, are the two ways this drifts. */
static void ctlpanel_set_cat(syn_server_t *s, int cat)
{
    if (cat < 0 || cat >= CTL_CAT_COUNT || cat == s->ctlpanel.cat) return;
    s->ctlpanel.cat    = cat;
    s->ctlpanel.item   = 0;
    s->ctlpanel.scroll = 0;
}

static void ctlpanel_move(syn_server_t *s, int dir)
{
    if (s->ctlpanel.focus == CTL_FOCUS_CATS) {
        ctlpanel_set_cat(s, s->ctlpanel.cat + dir);
        return;
    }

    int n = ctlpanel_item_count(s);
    if (n == 0) return;                    /* shortcuts: Up/Down scroll instead */

    int next = s->ctlpanel.item + dir;
    if (next < 0 || next >= n) return;     /* stop at the ends, as before */
    s->ctlpanel.item = next;
}

/* Enter the row pane. Refused for a category with nothing in the pane at all, so
 * focus can never sit in a column with nothing to drive.
 *
 * The shortcuts category has no *rows* but is not empty — it is one scrolling
 * list, and focus there is what puts Up/Down on the scroll. Gating purely on the
 * row count locked that category out of the pane entirely. */
static void ctlpanel_focus_items(syn_server_t *s)
{
    if (ctlpanel_item_count(s) == 0 && s->ctlpanel.cat != CTL_CAT_SHORTCUTS)
        return;
    s->ctlpanel.focus = CTL_FOCUS_ITEMS;
}

static void ctlpanel_activate(syn_server_t *s)
{
    int row = ctlpanel_selected_row(s);
    if (row < 0) return;

    /* Every row that hands off does it the same way, by kind, so a new panel row
     * is one table line rather than another case here. Only the in-place toggles
     * below need to know which row they are. */
    switch (ctlpanel_row_kind(row)) {
    case CTL_KIND_PANEL:
        ctlpanel_open_child(s, ctl_row_action(row));
        return;
    case CTL_KIND_LAUNCH:
    case CTL_KIND_ACTION:
        ctlpanel_hide(s);
        synui_binding_execute(s, ctl_row_action(row), NULL);
        return;
    default:
        break;
    }

    switch (row) {
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

    case CTL_ROW_NIGHTLIGHT:
        /* Same call the bind makes: it re-commits the colour transform on every
         * output, so there is nothing to repaint by hand here. */
        nightlight_toggle(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "night light %s", s->config.night_light ? "on" : "off");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_AI_BACKEND:
        /* The helper owns the work (systemd drop-in, restart synapd) and it is
         * not instant, so the row still reads the old device when this returns.
         * Poll the panel for a few seconds so the new value appears on its own
         * — see backend_poll_until. Without this the row only refreshed when
         * some other keypress happened to repaint the panel. */
        ctlpanel_row_value(s, CTL_ROW_AI_BACKEND, s->ctlpanel.backend_before,
                           sizeof(s->ctlpanel.backend_before));
        synui_binding_execute(s, "ai_backend", NULL);
        s->ctlpanel.poll_row = CTL_ROW_AI_BACKEND;
        s->ctlpanel.backend_poll_until = ctl_now_secs() + CTL_BACKEND_POLL_SECS;
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "switching AI backend \xe2\x80\xa6");
        return;

    default:
        return;
    }
}

/* Clamp the shortcuts scroll to the list — the panel draws CTL_SHORTCUT_ROWS
 * of it at a time, so scrolling past the end would show empty space. */
static void ctlpanel_scroll_by(syn_server_t *s, int dir)
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
 * be adjusting it otherwise). Everywhere else Left/Right change column, and Tab
 * does that from here too — a slider row is not a dead end. */
static void ctlpanel_adjust_opacity(syn_server_t *s, int dir)
{
    if (!s->config.transparency) transparency_set_enabled(s, 1);
    transparency_set_opacity(s, s->config.active_opacity + dir * 0.05f);
    snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
             "transparency %d%%", (int)(s->config.active_opacity * 100 + 0.5f));
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * The panel a user is most likely to open first was the one with no pointer at
 * all: every row here says "Enter activate" and none of them could be clicked.
 *
 * The contract is the one every panel in synui now follows, so that knowing one
 * of them is knowing all of them:
 *
 *   - Moving over a row selects it, and over the sidebar selects the category
 *     *and* moves focus there — hovering the categories while focus is stuck in
 *     the rows would highlight a column that the keys are not driving, which is
 *     precisely the confusion Tab exists to avoid.
 *   - A left click does what Enter does on that row. Not "select, then press
 *     Enter": a settings row that needs two gestures to flip is the reason
 *     people say a panel is keyboard-only.
 *   - A click anywhere off the panel closes it, exactly like Esc from the
 *     category column.
 *   - The wheel scrolls the shortcuts list and moves the selection everywhere
 *     else, which is the same split Up/Down already have.
 *
 * Everything routes through ctlpanel_activate()/ctlpanel_move(), the functions
 * the keys call. A pointer path with its own copy of "what this row does" is a
 * second definition of the item table, and the whole point of this panel is
 * that there is only one.
 */

int ctlpanel_motion(syn_server_t *s, double lx, double ly)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;
    if (!cp->visible) return 0;

    int cat = hit_row_at(&cp->hit, lx, ly);
    if (cat >= 0 && cat < CTL_CAT_COUNT) {
        if (cat == cp->cat && cp->focus == CTL_FOCUS_CATS) return 1;
        ctlpanel_set_cat(s, cat);
        cp->focus = CTL_FOCUS_CATS;
        synui_render_ctlpanel(s);
        return 1;
    }

    int i = hit_row_at(&cp->hit_items, lx, ly);
    if (i >= 0) {
        if (i == cp->item && cp->focus == CTL_FOCUS_ITEMS) return 1;
        cp->item  = i;
        cp->focus = CTL_FOCUS_ITEMS;
        synui_render_ctlpanel(s);
    }
    return 1;
}

int ctlpanel_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    syn_ctlpanel_t *cp = &s->ctlpanel;
    if (!cp->visible) return 0;

    if (!hit_in_panel(&cp->hit, lx, ly)) {
        ctlpanel_hide(s);
        return 1;
    }

    /* Put the cursor where the pointer is first, so the activation below acts on
     * the row that was pointed at even if no motion event preceded this press
     * (a tap, a tablet, a warped cursor). */
    ctlpanel_motion(s, lx, ly);

    if (button != BTN_LEFT) return 1;   /* swallowed: the panel is modal */

    /* In the sidebar a click opens the category — the same "step into the
     * submenu" Enter and Right mean there. Only in the row pane does it fire. */
    if (hit_row_at(&cp->hit, lx, ly) >= 0) {
        ctlpanel_focus_items(s);
        synui_render_ctlpanel(s);
        return 1;
    }

    if (hit_row_at(&cp->hit_items, lx, ly) >= 0) {
        ctlpanel_activate(s);
        /* A row that handed off has already hidden this panel and opened
         * another; re-rendering would draw this one back over the top of it. */
        if (cp->visible) synui_render_ctlpanel(s);
    }
    return 1;
}

int ctlpanel_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;
    if (!cp->visible) return 0;
    if (delta == 0) return 1;

    int dir = delta > 0 ? 1 : -1;

    /* Which column the wheel drives is decided by where the POINTER is, not by
     * which column has keyboard focus. On a two-column panel those are routinely
     * different — focus follows Tab, the pointer follows the hand — and a wheel
     * that scrolled the far column while sitting over this one would be the
     * single most confusing thing in the panel. Off the panel entirely (the
     * chrome, the footer) the focused column is the only sensible answer. */
    int over_cats = hit_row_at(&cp->hit, lx, ly) >= 0;
    int over_rows = hit_in_panel(&cp->hit_items, lx, ly) && !over_cats;

    if (over_cats) {
        ctlpanel_set_cat(s, cp->cat + dir);
        cp->focus = CTL_FOCUS_CATS;
    } else if (cp->cat == CTL_CAT_SHORTCUTS && over_rows) {
        /* The shortcuts pane is one long list with no rows to step through, so
         * there the wheel is the scroll — the same thing Up/Down do in it. */
        ctlpanel_scroll_by(s, dir * 3);
    } else if (over_rows) {
        cp->focus = CTL_FOCUS_ITEMS;
        ctlpanel_move(s, dir);
    } else if (cp->focus == CTL_FOCUS_ITEMS && ctlpanel_item_count(s) == 0) {
        ctlpanel_scroll_by(s, dir * 3);
    } else {
        ctlpanel_move(s, dir);
    }

    synui_render_ctlpanel(s);
    return 1;
}

int ctlpanel_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->ctlpanel.visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table, so Super+C
     * closes the panel it opened and Super+P still opens the power panel. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    int row = ctlpanel_selected_row(s);
    int in_items = (s->ctlpanel.focus == CTL_FOCUS_ITEMS);
    /* Up/Down mean "scroll" in the shortcuts list, which has no rows to step
     * through — the one category where the row pane is a single object. */
    int list_only = in_items && ctlpanel_item_count(s) == 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        /* Back out one level before closing: from a row pane to the category
         * list, and only from the category list to the desktop. Closing outright
         * from anywhere would make Esc mean two different things depending on
         * where you happened to be. */
        if (in_items) {
            s->ctlpanel.focus = CTL_FOCUS_CATS;
            synui_render_ctlpanel(s);
        } else {
            ctlpanel_hide(s);
        }
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (list_only) ctlpanel_scroll_by(s, -1);
        else           ctlpanel_move(s, -1);
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (list_only) ctlpanel_scroll_by(s, +1);
        else           ctlpanel_move(s, +1);
        synui_render_ctlpanel(s);
        return 1;

    case XKB_KEY_Tab:
        /* The one key that always swaps columns, whatever the row is doing with
         * Left/Right. */
        if (in_items) s->ctlpanel.focus = CTL_FOCUS_CATS;
        else          ctlpanel_focus_items(s);
        synui_render_ctlpanel(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        /* In the sidebar, Enter opens the category — the submenu step. In the
         * row pane it activates the row. */
        if (!in_items) ctlpanel_focus_items(s);
        else           ctlpanel_activate(s);
        /* A row that handed off already hid the panel and opened another one;
         * re-rendering here would draw this panel back over the top of it. */
        if (s->ctlpanel.visible) synui_render_ctlpanel(s);
        return 1;

    case XKB_KEY_Left:
    case XKB_KEY_h:
        /* On a slider row Left/Right are the slider; everywhere else they are
         * the column move, which is what makes the two panes feel like one menu
         * and its submenu. */
        if (in_items && row >= 0 && ctlpanel_row_kind(row) == CTL_KIND_SLIDER)
            ctlpanel_adjust_opacity(s, -1);
        else
            s->ctlpanel.focus = CTL_FOCUS_CATS;
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        if (in_items && row >= 0 && ctlpanel_row_kind(row) == CTL_KIND_SLIDER)
            ctlpanel_adjust_opacity(s, +1);
        else
            ctlpanel_focus_items(s);
        synui_render_ctlpanel(s);
        return 1;

    case XKB_KEY_Prior:     /* Page Up — always the shortcuts scroll */
        ctlpanel_scroll_by(s, -CTL_SHORTCUT_ROWS / 2);
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Next:      /* Page Down */
        ctlpanel_scroll_by(s, +CTL_SHORTCUT_ROWS / 2);
        synui_render_ctlpanel(s);
        return 1;

    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
