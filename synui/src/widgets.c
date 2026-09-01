/*
 * widgets.c — the Super+Shift+A panel for the desktop widgets.
 *
 * The quickshell widgets (audio visualiser, system monitor, big clock, quick
 * launch, post-it note, pizza, the pet) have always been individually
 * switchable —
 * `synui-widgets sysmon on` has worked since they landed. What was missing was
 * a way to reach that from the desktop: Super+Shift+A and the control panel's
 * row both ran the GROUP form, so "the clock but not the visualiser" meant
 * opening a terminal.
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

#include "i18n.h"
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
    case WIDGET_ROW_POSTIT:     return "postit";
    case WIDGET_ROW_PIZZA:      return "pizza";
    case WIDGET_ROW_TUX:        return "tux";
    case WIDGET_ROW_ANALOG:     return "analog";
    case WIDGET_ROW_MUSIC:      return "music";
    case WIDGET_ROW_WEATHER:    return "weather";
    default:                    return NULL;
    }
}

const char *widget_row_label(int row)
{
    switch (row) {
    case WIDGET_ROW_ALL:        return _("All widgets");
    case WIDGET_ROW_VISUALIZER: return _("Audio visualiser");
    case WIDGET_ROW_SYSMON:     return _("System monitor");
    case WIDGET_ROW_CLOCK:      return _("Desktop clock");
    case WIDGET_ROW_LAUNCHER:   return _("Quick launch");
    case WIDGET_ROW_POSTIT:     return _("Post-it note");
    case WIDGET_ROW_PIZZA:      return _("Pizza");
    case WIDGET_ROW_TUX:        return _("Tuxagotchi");
    case WIDGET_ROW_ANALOG:     return _("Analog clock");
    case WIDGET_ROW_MUSIC:      return _("Music player");
    case WIDGET_ROW_WEATHER:    return _("Weather");
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

    /* OPEN FIRST, THEN ASK THE DESCRIPTOR. stat(path) followed by fopen(path)
     * resolves the name twice, so the mtime recorded could describe a file
     * other than the bytes actually read — and this state is written by a
     * temp-file rename, which swaps the name underneath exactly here. One
     * open, one fstat, and "is it there" is the open failing. */
    FILE *f = fopen(path, "r");
    if (!f) {
        /* Gone: everything is off. Only act on the transition, or a desktop
         * with no file would clear the optimism of the toggle that is about to
         * create one. */
        if (s->widgets.mtime != 0) {
            for (int i = 0; i < WIDGET_ROW_COUNT; i++) s->widgets.on[i] = 0;
            s->widgets.mtime = 0;
        }
        return;
    }

    struct stat st;
    if (fstat(fileno(f), &st) != 0) { fclose(f); return; }
    if (s->widgets.mtime == (long)st.st_mtime) { fclose(f); return; }
    s->widgets.mtime = (long)st.st_mtime;

    for (int i = 0; i < WIDGET_ROW_COUNT; i++)
        s->widgets.on[i] = 0;

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

/*
 * Every widget back to its designed corner.
 *
 * Widgets are dragged by the grip in their top-right corner, and a widget can
 * be dropped somewhere its grip is hard to get back to — under a permanently
 * fullscreen window, say. Double-clicking a grip sends that one home, which is
 * no use for the case where the grip is what you cannot reach; this is the way
 * out that does not need the pointer to find anything.
 *
 * Spawned like every other change here rather than done in-process: positions
 * live in widgets.pos, written by the bar, and the panel has never been an
 * author of the files it toggles.
 */
static void widgets_home(syn_server_t *s)
{
    synui_spawn("synui-widgets home");
    snprintf(s->widgets.status, sizeof(s->widgets.status),
             "every widget back in its designed corner");
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
    ctlpanel_child_closed(s, "widgets");
}

void widgets_toggle(syn_server_t *s)
{
    if (s->widgets.visible) widgets_hide(s);
    else                    widgets_show(s);
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h. Enter acts on the row here rather
 * than closing, so a left click is Enter: every row is an on/off and clicking
 * one flips it, which is the only thing a click on a checkbox has ever meant. */

int widgets_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->widgets.visible) return 0;

    int row = hit_row_at(&s->widgets.hit, lx, ly);
    if (row < 0 || row == s->widgets.selected) return 1;
    s->widgets.selected = row;
    synui_render_widgets(s);
    return 1;
}

int widgets_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    if (!s->widgets.visible) return 0;

    if (!hit_in_panel(&s->widgets.hit, lx, ly)) {
        widgets_hide(s);
        return 1;
    }

    widgets_motion(s, lx, ly);

    if (button != BTN_LEFT) return 1;
    if (hit_row_at(&s->widgets.hit, lx, ly) < 0) return 1;   /* chrome */

    /* Same refresh the key path does first, and for the same reason: the group
     * row decides which way to flip from the live state, so a change made in a
     * terminal has to be in hand before it does. */
    widgets_state_refresh(s);
    widgets_activate(s);
    synui_render_widgets(s);
    return 1;
}

int widgets_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->widgets.visible) return 0;
    if (delta == 0) return 1;

    int next = s->widgets.selected + (delta > 0 ? 1 : -1);
    if (next < 0 || next >= WIDGET_ROW_COUNT) return 1;
    s->widgets.selected = next;
    synui_render_widgets(s);
    return 1;
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
    case XKB_KEY_r:
        /* Positions, not toggles — the one key here that does not change what
         * is on. From any row, because it acts on all of them. */
        widgets_home(s);
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
