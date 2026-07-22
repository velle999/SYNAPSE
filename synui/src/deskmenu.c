/*
 * deskmenu.c — the desktop right-click menu, and optional desktop icons.
 *
 * Right-clicking the wallpaper (no window, no panel, no dock under the cursor)
 * opens a context menu of the things you can only otherwise reach from a
 * keybind: a terminal, the wallpaper picker, display settings, and so on. It
 * is the same idiom as dock.c's dockmenu_* — pointer-driven, modal while up,
 * Escape closes — because a second context menu that behaved differently would
 * be its own bug.
 *
 * Desktop icons are off by default (`desktop_icons = on` in synuirc). They are
 * read from ~/Desktop: .desktop files resolve through the same icons.c lookup
 * the dock uses, and anything else is opened with xdg-open. Drawing them is
 * render.c's job (synui_render_deskicons); this file owns the model and the
 * hit-testing.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "synui.h"

#define DESKMENU_ITEM_H 30
#define DESKMENU_W      210
#define DESKMENU_SEP_H  9      /* separator rows are shorter than items */

const char *deskact_label(syn_deskact_t a)
{
    switch (a) {
    case SYN_DESKACT_TERMINAL:  return "Open Terminal";
    case SYN_DESKACT_FILES:     return "Open File Manager";
    case SYN_DESKACT_APPS:      return "Applications…";
    case SYN_DESKACT_SEP:       return "";
    case SYN_DESKACT_WALLPAPER: return "Change Wallpaper…";
    case SYN_DESKACT_THEME:     return "Appearance…";
    case SYN_DESKACT_DISPLAY:   return "Display Settings…";
    case SYN_DESKACT_ICONS:     return "Show Desktop Icons";
    case SYN_DESKACT_TASKMGR:   return "Task Manager";
    }
    return "";
}

/* Separators are not selectable and are drawn as a rule, not a row. */
static bool deskact_is_sep(syn_deskact_t a) { return a == SYN_DESKACT_SEP; }

/* Row height for item i — separators are short. */
static int deskmenu_row_h(syn_server_t *s, int i)
{
    return deskact_is_sep(s->deskmenu.actions[i]) ? DESKMENU_SEP_H
                                                  : DESKMENU_ITEM_H;
}

void deskmenu_open(syn_server_t *s, double lx, double ly)
{
    int n = 0;
    s->deskmenu.actions[n++] = SYN_DESKACT_TERMINAL;
    s->deskmenu.actions[n++] = SYN_DESKACT_FILES;
    s->deskmenu.actions[n++] = SYN_DESKACT_APPS;
    s->deskmenu.actions[n++] = SYN_DESKACT_SEP;
    s->deskmenu.actions[n++] = SYN_DESKACT_WALLPAPER;
    s->deskmenu.actions[n++] = SYN_DESKACT_THEME;
    s->deskmenu.actions[n++] = SYN_DESKACT_DISPLAY;
    s->deskmenu.actions[n++] = SYN_DESKACT_ICONS;
    s->deskmenu.actions[n++] = SYN_DESKACT_SEP;
    s->deskmenu.actions[n++] = SYN_DESKACT_TASKMGR;
    s->deskmenu.action_count = n;

    int h = 8;
    for (int i = 0; i < n; i++) h += deskmenu_row_h(s, i);
    int w = DESKMENU_W;

    /* Drop down-and-right of the cursor like every other desktop, then clamp
     * inside the output under it so a right-click near an edge stays whole. */
    int x = (int)lx, y = (int)ly;
    struct wlr_output *wo =
        wlr_output_layout_output_at(s->output_layout, lx, ly);
    if (wo && wo->data) {
        struct wlr_box ob;
        output_box_of(s, (syn_output_t *)wo->data, &ob);
        if (x + w > ob.x + ob.width)  x = ob.x + ob.width  - w;
        if (y + h > ob.y + ob.height) y = ob.y + ob.height - h;
        if (x < ob.x) x = ob.x;
        if (y < ob.y) y = ob.y;
    }

    s->deskmenu.x = x; s->deskmenu.y = y;
    s->deskmenu.w = w; s->deskmenu.h = h;
    s->deskmenu.selected = -1;
    s->deskmenu.visible = 1;
    synui_render_deskmenu(s);
}

/* Item index under (lx,ly), or -1 if outside the menu or over a separator. */
static int deskmenu_item_at(syn_server_t *s, double lx, double ly)
{
    if (lx < s->deskmenu.x || lx >= s->deskmenu.x + s->deskmenu.w ||
        ly < s->deskmenu.y || ly >= s->deskmenu.y + s->deskmenu.h)
        return -1;

    int rel = (int)(ly - s->deskmenu.y - 4);
    int top = 0;
    for (int i = 0; i < s->deskmenu.action_count; i++) {
        int rh = deskmenu_row_h(s, i);
        if (rel >= top && rel < top + rh)
            return deskact_is_sep(s->deskmenu.actions[i]) ? -1 : i;
        top += rh;
    }
    return -1;
}

/* Pixel offset of item i from the top of the menu — render.c needs the same
 * walk to draw the rows, so it lives here with the geometry that defines it. */
int deskmenu_row_top(syn_server_t *s, int i)
{
    int top = 4;
    for (int k = 0; k < i; k++) top += deskmenu_row_h(s, k);
    return top;
}

int deskmenu_row_height(syn_server_t *s, int i) { return deskmenu_row_h(s, i); }

void deskmenu_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->deskmenu.visible) return;
    int idx = deskmenu_item_at(s, lx, ly);
    if (idx != s->deskmenu.selected) {
        s->deskmenu.selected = idx;
        synui_render_deskmenu(s);
    }
}

void deskmenu_close(syn_server_t *s)
{
    if (!s->deskmenu.visible) return;
    s->deskmenu.visible = 0;
    synui_render_deskmenu(s);
}

void deskmenu_click(syn_server_t *s, double lx, double ly)
{
    if (!s->deskmenu.visible) return;
    int idx = deskmenu_item_at(s, lx, ly);
    if (idx < 0) { deskmenu_close(s); return; }   /* outside → dismiss */

    syn_deskact_t act = s->deskmenu.actions[idx];
    deskmenu_close(s);   /* before acting: panels below raise over the menu */

    switch (act) {
    case SYN_DESKACT_TERMINAL:
        synui_spawn(s->config.terminal[0] ? s->config.terminal : "foot");
        break;
    case SYN_DESKACT_FILES:
        synui_spawn("dolphin");
        break;
    case SYN_DESKACT_APPS:
        menu_toggle(s);
        break;
    case SYN_DESKACT_WALLPAPER:
        wppick_toggle(s);
        break;
    case SYN_DESKACT_THEME:
        theme_toggle(s);
        break;
    case SYN_DESKACT_DISPLAY:
        dispcfg_toggle(s);
        break;
    case SYN_DESKACT_ICONS:
        s->config.desktop_icons = !s->config.desktop_icons;
        deskicons_reload(s);
        break;
    case SYN_DESKACT_TASKMGR:
        taskmgr_toggle(s);
        break;
    case SYN_DESKACT_SEP:
        break;
    }
}

/* ── Desktop icons ───────────────────────────────────────── */

static int name_cmp(const void *a, const void *b)
{
    const syn_deskicon_t *x = a, *y = b;
    return strcasecmp(x->label, y->label);
}

/*
 * Rescan ~/Desktop into s->deskicons. Cheap enough to redo whenever the
 * setting is flipped or an output changes; there is no inotify watch, so a
 * file added behind our back appears on the next reload.
 */
void deskicons_reload(syn_server_t *s)
{
    s->deskicon_count = 0;
    if (!s->config.desktop_icons) { synui_render_deskicons(s); return; }

    const char *home = getenv("HOME");
    if (!home || !home[0]) { synui_render_deskicons(s); return; }

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/Desktop", home);

    DIR *d = opendir(dir);
    if (!d) { synui_render_deskicons(s); return; }

    struct dirent *de;
    while ((de = readdir(d)) && s->deskicon_count < SYN_DESKICON_MAX) {
        if (de->d_name[0] == '.') continue;   /* dotfiles stay hidden */

        syn_deskicon_t *ic = &s->deskicons[s->deskicon_count];
        memset(ic, 0, sizeof(*ic));
        snprintf(ic->path, sizeof(ic->path), "%s/%s", dir, de->d_name);

        struct stat st;
        if (stat(ic->path, &st) != 0) continue;
        ic->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;

        size_t len = strlen(de->d_name);
        const syn_icon_entry_t *ent = NULL;
        if (!ic->is_dir && len > 8 &&
            strcmp(de->d_name + len - 8, ".desktop") == 0)
            ent = icon_lookup_desktop_path(ic->path);

        if (ent) {
            /* A .desktop names and draws itself; icons.c did the parsing and
             * the icon-theme search. */
            ic->is_desktop = 1;
            snprintf(ic->label, sizeof(ic->label), "%s", ent->display_name);
            snprintf(ic->exec,  sizeof(ic->exec),  "%s", ent->exec);
            ic->icon_surface = ent->icon_surface;
        } else {
            snprintf(ic->label, sizeof(ic->label), "%s", de->d_name);
        }

        s->deskicon_count++;
    }
    closedir(d);

    /* Stable, human order: readdir returns them in whatever order the
     * filesystem feels like, which would reshuffle the desktop every reload. */
    qsort(s->deskicons, s->deskicon_count, sizeof(s->deskicons[0]), name_cmp);

    deskicons_layout(s);
    synui_render_deskicons(s);
}

/*
 * Place the icons in a top-down, left-to-right grid on the primary output,
 * inside its usable area so the dock and bars do not sit on top of them.
 */
void deskicons_layout(syn_server_t *s)
{
    if (s->deskicon_count <= 0) return;

    syn_output_t *o = server_primary_output(s);
    if (!o) o = server_focused_output(s);
    if (!o) return;

    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    const int cw = SYN_DESKICON_W, ch = SYN_DESKICON_H;
    int rows = (area.height - 2 * SYN_DESKICON_PAD) / ch;
    if (rows < 1) rows = 1;

    for (int i = 0; i < s->deskicon_count; i++) {
        int col = i / rows, row = i % rows;
        s->deskicons[i].x = area.x + SYN_DESKICON_PAD + col * cw;
        s->deskicons[i].y = area.y + SYN_DESKICON_PAD + row * ch;
    }
}

/* Icon index under (lx,ly), or -1. */
int deskicon_at(syn_server_t *s, double lx, double ly)
{
    if (!s->config.desktop_icons) return -1;
    for (int i = 0; i < s->deskicon_count; i++) {
        syn_deskicon_t *ic = &s->deskicons[i];
        if (lx >= ic->x && lx < ic->x + SYN_DESKICON_W &&
            ly >= ic->y && ly < ic->y + SYN_DESKICON_H)
            return i;
    }
    return -1;
}

/* Open icon i — its Exec for a .desktop, xdg-open for anything else. */
void deskicon_activate(syn_server_t *s, int i)
{
    if (i < 0 || i >= s->deskicon_count) return;
    syn_deskicon_t *ic = &s->deskicons[i];

    if (ic->is_desktop && ic->exec[0]) {
        synui_spawn(ic->exec);
        return;
    }

    /* Quote the path: ~/Desktop routinely holds names with spaces, and this
     * string goes through /bin/sh. Single quotes with the standard '\'' escape
     * so even an apostrophe in a filename cannot break out. */
    char cmd[1200];
    size_t n = 0;
    n += snprintf(cmd + n, sizeof(cmd) - n, "xdg-open '");
    for (const char *p = ic->path; *p && n + 8 < sizeof(cmd); p++) {
        if (*p == '\'') n += snprintf(cmd + n, sizeof(cmd) - n, "'\\''");
        else            cmd[n++] = *p;
    }
    snprintf(cmd + n, sizeof(cmd) - n, "'");
    synui_spawn(cmd);
}

void deskicon_select(syn_server_t *s, int i)
{
    if (s->deskicon_selected == i) return;
    s->deskicon_selected = i;
    synui_render_deskicons(s);
}
