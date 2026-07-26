/*
 * widgets.c — the Super+Shift+A panel for the desktop widgets.
 *
 * The four quickshell widgets (audio visualiser, system monitor, big clock,
 * quick launch) have always been individually switchable — `synui-widgets sysmon
 * on` has worked since they landed. What was missing was a way to reach that
 * from the desktop: Super+Shift+A and the control panel's row both ran the GROUP
 * form, so "the clock but not the visualiser" meant opening a terminal.
 *
 * So this is a panel with one row per widget, and the old group toggle kept as
 * the master row — and, as in filters.c, on Space from ANY row, so the one-key
 * "clear the desktop" the bind used to be is still one key once the panel is up.
 *
 * synui-widgets stays the single writer of widgets.state (the bar watches that
 * file and repaints live). This panel never writes it: every change spawns the
 * helper. What it does keep is its own copy of the values, updated the moment a
 * key is pressed — re-reading the file after spawning would race the child and
 * draw the PREVIOUS value under the cursor, which is exactly the staleness the
 * control panel's AI-backend row had to grow a poll to work around.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

/* The name synui-widgets knows each row by. NULL for the master row, which is
 * the helper's `all`/group form rather than a widget. */
const char *widget_row_name(int row)
{
    switch (row) {
    case WIDGET_ROW_VISUALIZER: return "visualizer";
    case WIDGET_ROW_SYSMON:     return "sysmon";
    case WIDGET_ROW_CLOCK:      return "clock";
    case WIDGET_ROW_LAUNCHER:   return "launcher";
    default:                    return NULL;
    }
}

const char *widget_row_label(int row)
{
    switch (row) {
    case WIDGET_ROW_ALL:        return "All widgets";
    case WIDGET_ROW_VISUALIZER: return "Audio visualiser";
    case WIDGET_ROW_SYSMON:     return "System monitor";
    case WIDGET_ROW_CLOCK:      return "Desktop clock";
    case WIDGET_ROW_LAUNCHER:   return "Quick launch";
    default:                    return "?";
    }
}

/* ── Reading widgets.state ───────────────────────────────── */
/*
 * Re-read only when the file has actually changed, so the panel notices a
 * `synui-widgets` run in a terminal while it is open — and so it does not undo
 * its own change on the way. Absent file means everything is off, which is the
 * documented default and not an error.
 *
 * This matters for more than the display: the master row and Space both compute
 * "everything off if anything is on" FROM this copy, so a stale one flips the
 * group the wrong way, not merely draws the wrong word.
 */
static void widgets_state_refresh(syn_server_t *s)
{
    char path[256];
    if (!syn_config_path(path, sizeof(path), "widgets.state")) return;

    struct stat st;
    if (stat(path, &st) != 0) {
        /* Gone: everything is off. Only act on the transition, or a desktop
         * with no file would clear the optimism of the toggle that is about to
         * create one. */
        if (s->widgets.mtime != 0) {
            for (int i = 0; i < WIDGET_ROW_COUNT; i++) s->widgets.on[i] = 0;
            s->widgets.mtime = 0;
        }
        return;
    }
    if (s->widgets.mtime == (long)st.st_mtime) return;
    s->widgets.mtime = (long)st.st_mtime;

    for (int i = 0; i < WIDGET_ROW_COUNT; i++)
        s->widgets.on[i] = 0;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        /* `key = value` with any amount of space around either. */
        char *key = line;
        while (*key == ' ' || *key == '\t') key++;
        char *end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';

        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;

        for (int i = 0; i < WIDGET_ROW_COUNT; i++) {
            const char *name = widget_row_name(i);
            if (name && strcmp(key, name) == 0)
                s->widgets.on[i] = (strncmp(val, "on", 2) == 0);
        }
    }
    fclose(f);
}

/* Every widget on? Drives the master row's word, and what Space does next. */
static int widgets_all_on(syn_server_t *s)
{
    for (int i = WIDGET_ROW_ALL + 1; i < WIDGET_ROW_COUNT; i++)
        if (!s->widgets.on[i]) return 0;
    return 1;
}

static int widgets_any_on(syn_server_t *s)
{
    for (int i = WIDGET_ROW_ALL + 1; i < WIDGET_ROW_COUNT; i++)
        if (s->widgets.on[i]) return 1;
    return 0;
}

/* ── Applying a change ───────────────────────────────────── */

static void widgets_set(syn_server_t *s, int row, int on)
{
    const char *name = widget_row_name(row);
    if (!name) return;

    s->widgets.on[row] = on;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "synui-widgets %s %s", name, on ? "on" : "off");
    synui_spawn(cmd);

    snprintf(s->widgets.status, sizeof(s->widgets.status), "%s %s",
             widget_row_label(row), on ? "on" : "off");

    /* The visualiser is the one widget that can be on and still show nothing:
     * quickshell's PipeWire binding exposes a single peak level, no spectrum, so
     * the bars come from cava — an optdepend. Say so here rather than let the
     * toggle look broken. */
    if (row == WIDGET_ROW_VISUALIZER && on && !s->widgets.have_cava)
        snprintf(s->widgets.status, sizeof(s->widgets.status),
                 "%s on \xc2\xb7 cava is not installed, so it will stay dark",
                 widget_row_label(row));
}

/* The master row, and Space from anywhere: everything off if anything is on,
 * else everything on. The same rule `synui-widgets toggle` follows, so the panel
 * and the old bind cannot disagree about what one press does. */
static void widgets_set_all(syn_server_t *s, int on)
{
    for (int i = WIDGET_ROW_ALL + 1; i < WIDGET_ROW_COUNT; i++)
        s->widgets.on[i] = on;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "synui-widgets all %s", on ? "on" : "off");
    synui_spawn(cmd);

    snprintf(s->widgets.status, sizeof(s->widgets.status),
             "all widgets %s", on ? "on" : "off");
}

static void widgets_activate(syn_server_t *s)
{
    if (s->widgets.selected == WIDGET_ROW_ALL) {
        widgets_set_all(s, !widgets_any_on(s));
        return;
    }
    widgets_set(s, s->widgets.selected, !s->widgets.on[s->widgets.selected]);
}

/* ── Panel ───────────────────────────────────────────────── */

void widgets_show(syn_server_t *s)
{
    /* Force a read on open: the panel is the one place that has to show what is
     * on disk right now, not what it last saw. */
    s->widgets.mtime = -1;
    widgets_state_refresh(s);
    /* Probed per open, not cached for the session: cava can be installed while
     * synui is running, and the warning outliving the install would be worse
     * than the warning being a millisecond late. */
    s->widgets.have_cava   = (access("/usr/bin/cava", X_OK) == 0);
    s->widgets.visible     = 1;
    s->widgets.selected    = WIDGET_ROW_ALL + 1;   /* the first real widget */
    s->widgets.status[0]   = '\0';
    synui_render_widgets(s);
}

void widgets_hide(syn_server_t *s)
{
    s->widgets.visible = 0;
    synui_render_widgets(s);
}

void widgets_toggle(syn_server_t *s)
{
    if (s->widgets.visible) widgets_hide(s);
    else                    widgets_show(s);
}

int widgets_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->widgets.visible) return 0;

    /* Before acting, not after: a change made from a terminal while the panel is
     * open has to be in hand before Space decides which way to flip the group. */
    widgets_state_refresh(s);

    /* Modified combos (Super+…) still reach the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        widgets_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->widgets.selected > 0) s->widgets.selected--;
        synui_render_widgets(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->widgets.selected < WIDGET_ROW_COUNT - 1) s->widgets.selected++;
        synui_render_widgets(s);
        return 1;
    case XKB_KEY_Left:
    case XKB_KEY_h:
        if (s->widgets.selected == WIDGET_ROW_ALL) widgets_set_all(s, 0);
        else                                       widgets_set(s, s->widgets.selected, 0);
        synui_render_widgets(s);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        if (s->widgets.selected == WIDGET_ROW_ALL) widgets_set_all(s, 1);
        else                                       widgets_set(s, s->widgets.selected, 1);
        synui_render_widgets(s);
        return 1;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        widgets_activate(s);
        synui_render_widgets(s);
        return 1;
    case XKB_KEY_space:
        /* From any row: the group toggle Super+Shift+A used to be on its own.
         * Having it here is what keeps "hide it all" a single key. */
        widgets_set_all(s, !widgets_any_on(s));
        synui_render_widgets(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}

/* The word one row shows. The master row reports all/some/none, because "on"
 * against three widgets on and one off would be a lie. */
const char *widgets_row_value(syn_server_t *s, int row)
{
    if (row == WIDGET_ROW_ALL)
        return widgets_all_on(s) ? "on" : widgets_any_on(s) ? "some" : "off";
    return s->widgets.on[row] ? "on" : "off";
}
