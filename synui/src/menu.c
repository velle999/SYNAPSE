/*
 * menu.c — the start menu (Super tap).
 *
 * Why this exists in synui at all: the menu used to be waybar's, opened by
 * synthesising a pointer click on the bar. It could not be arrow-navigated, and
 * not for want of trying — waybar calls set_keyboard_interactivity(NONE) once
 * at startup and never revises it, so it is handed no keyboard focus, and three
 * separate synui-side focus fixes each delivered a textbook key sequence that
 * GTK simply ignored. The wall was inside waybar/GDK, not here. A panel the
 * compositor draws is one the compositor can hand the keyboard to, so the menu
 * moved in-process and is keyboard-navigable by construction.
 *
 * Same idiom as ctlpanel.c/news.c/taskmgr.c: state on syn_server_t, a *_key()
 * that input.c consults while the panel is up, and a synui_render_*() in
 * render.c. Keys match those panels (Up/Down select, Enter activate, Esc close)
 * because a menu that worked a fourth way would be its own bug.
 *
 * The entry list is scanned from the installed .desktop files every time the
 * menu opens rather than read from a generated file — see syn_menu_t. The rules
 * for reading them (field codes, escapes, Terminal=, Path=, Wine) are the ones
 * waybar/synapse-menu-gen.py worked out the hard way over many bugs; this is a
 * port of that behaviour, not a fresh guess at it. That generator has since been
 * deleted — this file replaced it, and its history is the place to look for why
 * any given rule here exists.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* The XDG application dirs below ~/.local/share/applications, which is built
 * per-scan from $HOME and searched ahead of these. Precedence order: the first
 * entry with a given id wins, per the spec. */
static const char *const APP_DIRS_SYSTEM[] = {
    "/usr/local/share/applications",
    "/usr/share/applications",
};

/* An application dir is a tree, not a flat list, and Wine puts every one of its
 * shortcuts under applications/wine/Programs/…. Globbing one level deep is why
 * no Wine app ever appeared in the waybar menu for months. Bounded because a
 * symlink loop in a user-writable dir would otherwise hang the compositor. */
#define MENU_SCAN_DEPTH_MAX  8

/* The submenu the fixed rows that did not fit the root file under. Its name is
 * a display string like the categories', so nothing has to special-case it. */
#define MENU_PAGE_TOOLS  "System Tools"

/* XDG main categories → the submenu they file under, in test order. An app
 * usually lists several ("Game;Emulator;"), so precedence matters: the
 * catch-alls (Settings/System/Utility) must come last or they swallow half the
 * menu. Was CATEGORIES in the retired synapse-menu-gen.py. */
static const struct { const char *key, *display; } CATEGORIES[] = {
    { "Game",        "Games" },
    { "Development", "Development" },
    { "Graphics",    "Graphics" },
    { "AudioVideo",  "Multimedia" },
    { "Audio",       "Multimedia" },
    { "Video",       "Multimedia" },
    { "Office",      "Office" },
    { "Science",     "Science" },
    { "Education",   "Education" },
    { "Network",     "Internet" },
    { "Settings",    "Settings" },
    { "System",      "System" },
    { "Utility",     "Accessories" },
};

/* A scanned application, before grouping. */
typedef struct {
    char name[MENU_LABEL_MAX];
    char cmd[MENU_CMD_MAX];
    char cat[MENU_CAT_MAX];
} menu_app_t;

/* ── .desktop parsing ────────────────────────────────────── */

/* Escape sequences the desktop-entry spec defines inside a value. Wine's Exec
 * lines lean on them hard: a shortcut path is written "C:\\\\users\\\\velle",
 * which is "C:\\users\\velle" after this pass and C:\users\velle once the shell
 * strips the quotes. Skip the pass and wine gets doubled separators, cannot
 * resolve the path, and the entry dies silently. */
static void desktop_unescape(char *v)
{
    char *r = v, *w = v;
    while (*r) {
        if (r[0] == '\\' && r[1]) {
            switch (r[1]) {
            case 's':  *w++ = ' ';  r += 2; continue;
            case 'n':  *w++ = '\n'; r += 2; continue;
            case 't':  *w++ = '\t'; r += 2; continue;
            case 'r':  *w++ = '\r'; r += 2; continue;
            case '\\': *w++ = '\\'; r += 2; continue;
            default: break;
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

/* Drop the %f/%U/… field codes and squeeze runs of whitespace.
 *
 * Order matters and is not arbitrary: the squeeze runs while the escapes are
 * still two-character sequences, so a "\s" standing for a real space inside a
 * quoted argument survives it intact and only becomes a space afterwards. Doing
 * it the other way round eats spaces that were meant to be kept. */
static void exec_clean(char *cmd)
{
    char *r = cmd, *w = cmd;
    while (*r) {
        if (r[0] == '%' && r[1] && strchr("fFuUdDnNickvm", r[1])) { r += 2; continue; }
        if (r[0] == '%' && r[1] == '%') { *w++ = '%'; r += 2; continue; }
        if (isspace((unsigned char)r[0])) {
            while (isspace((unsigned char)*r)) r++;
            if (w != cmd) *w++ = ' ';
            continue;
        }
        *w++ = *r++;
    }
    while (w > cmd && w[-1] == ' ') w--;
    *w = '\0';
    desktop_unescape(cmd);
}

static int truthy(const char *v) { return v && strcasecmp(v, "true") == 0; }

/* Single-quote a string for /bin/sh, the way shlex.quote does. Used on Path=,
 * which is attacker-adjacent only in the sense that ~/.local/share/applications
 * is user-writable — but an unquoted directory with a space in it silently
 * breaks the cd, which is reason enough. */
static int sh_quote(const char *in, char *out, size_t n)
{
    size_t w = 0;
    if (w + 1 >= n) return 0;
    out[w++] = '\'';
    for (const char *p = in; *p; p++) {
        if (*p == '\'') {                      /* ' -> '\'' */
            if (w + 4 >= n) return 0;
            out[w++] = '\''; out[w++] = '\\'; out[w++] = '\''; out[w++] = '\'';
            continue;
        }
        if (w + 2 >= n) return 0;
        out[w++] = *p;
    }
    if (w + 2 >= n) return 0;
    out[w++] = '\'';
    out[w] = '\0';
    return 1;
}

/* Parse one .desktop into app. Returns 0 if it should not be listed.
 *
 * Keys are matched exactly, so the localised "Name[de]" forms are ignored and
 * the plain Name wins — which is what we want, and what configparser gave the
 * generator for free. Only the [Desktop Entry] group is read: a file's trailing
 * [Desktop Action …] groups have their own Name/Exec and would otherwise
 * overwrite the real ones as the parse walked past them. */
static int parse_desktop(const char *path, const char *entry_id, menu_app_t *app)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[1024];
    int  in_entry = 0;
    char name[MENU_LABEL_MAX] = {0};
    char exec[MENU_CMD_MAX]   = {0};
    char cats[512]            = {0};
    char workdir[512]         = {0};
    char type[64]             = {0};
    char nodisplay[16]        = {0};
    char hidden[16]           = {0};
    char terminal[16]         = {0};

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        size_t len = strlen(p);
        while (len && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = '\0';

        if (*p == '[') {
            in_entry = (strcmp(p, "[Desktop Entry]") == 0);
            continue;
        }
        if (!in_entry || *p == '#' || !*p) continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = p, *val = eq + 1;
        /* Keys may be padded either side of the '='. */
        for (char *k = key + strlen(key); k > key && (k[-1] == ' ' || k[-1] == '\t'); )
            *--k = '\0';
        while (*val == ' ' || *val == '\t') val++;

#define TAKE(k, dst) \
        if (strcmp(key, k) == 0 && !dst[0]) { snprintf(dst, sizeof(dst), "%s", val); continue; }
        TAKE("Type",       type)
        TAKE("Name",       name)
        TAKE("Exec",       exec)
        TAKE("Categories", cats)
        TAKE("Path",       workdir)
        TAKE("NoDisplay",  nodisplay)
        TAKE("Hidden",     hidden)
        TAKE("Terminal",   terminal)
#undef TAKE
    }
    fclose(f);

    if (type[0] && strcmp(type, "Application") != 0) return 0;
    if (truthy(nodisplay) || truthy(hidden)) return 0;
    if (!name[0] || !exec[0]) return 0;

    exec_clean(exec);
    if (!exec[0]) return 0;
    desktop_unescape(name);

    /* A name is arbitrary text from a third-party package and goes straight to
     * cairo_show_text, which poisons its whole context on invalid UTF-8 and
     * silently no-ops every later draw — one bad name would blank the rest of
     * the menu. news_utf8_trim is the codebase's one true cutter; anything that
     * does not survive it whole is not drawn. */
    size_t nl = strlen(name);
    if (news_utf8_trim(name, nl) != nl) {
        wlr_log(WLR_DEBUG, "synui: menu: skipping %s, Name is not valid UTF-8", path);
        return 0;
    }

    if (truthy(terminal)) {
        char t[MENU_CMD_MAX];
        if (snprintf(t, sizeof(t), "foot -e %s", exec) >= (int)sizeof(t)) return 0;
        memcpy(exec, t, sizeof(exec));
    }

    /* An entry may name the working directory it expects, and Wine's do: its
     * shortcuts point into the install dir the program reads its data out of. */
    if (workdir[0]) {
        desktop_unescape(workdir);
        char q[600], t[MENU_CMD_MAX];
        if (sh_quote(workdir, q, sizeof(q))) {
            if (snprintf(t, sizeof(t), "cd %s && exec %s", q, exec) < (int)sizeof(t))
                memcpy(exec, t, sizeof(exec));
        }
    }

    snprintf(app->name, sizeof(app->name), "%s", name);
    snprintf(app->cmd,  sizeof(app->cmd),  "%s", exec);

    /* Category: first match wins, then Wine's own bucket. Wine gives its
     * imported shortcuts no Categories= at all, so on category alone every one
     * of them would scatter into "Other" among the genuinely uncategorised. */
    const char *display = NULL;
    for (unsigned i = 0; i < sizeof(CATEGORIES) / sizeof(CATEGORIES[0]) && !display; i++) {
        /* Match a whole ";"-separated field, not a substring: "Utility" must
         * not match inside "X-Utility", and "Audio" must not match "AudioVideo"
         * ahead of AudioVideo's own row. */
        const char *k = CATEGORIES[i].key;
        size_t kl = strlen(k);
        for (const char *c = cats; (c = strstr(c, k)); c += kl) {
            char before = (c == cats) ? ';' : c[-1];
            char after  = c[kl] ? c[kl] : ';';
            if (before == ';' && after == ';') { display = CATEGORIES[i].display; break; }
        }
    }
    if (!display)
        display = (strncmp(entry_id, "wine-", 5) == 0) ? "Wine" : "Other";
    snprintf(app->cat, sizeof(app->cat), "%s", display);
    return 1;
}

/* ── Scan ────────────────────────────────────────────────── */

/* An entry id is a path relative to the application dir, so it is bounded by
 * the tree's depth rather than by anything we choose. Wine's are the longest
 * here (wine-Programs-<vendor>-<product>-<name>.desktop) and run to ~120 bytes;
 * 256 leaves room, and an id that still will not fit is skipped rather than
 * truncated — a truncated id could collide with an unrelated entry and shadow
 * it out of the menu. */
#define MENU_ID_MAX  256

typedef struct {
    menu_app_t apps[MENU_ENTRIES_MAX];
    int        count;
    char       ids[MENU_ENTRIES_MAX][MENU_ID_MAX];
    int        id_count;
} menu_scan_t;

static int id_seen(menu_scan_t *sc, const char *id)
{
    for (int i = 0; i < sc->id_count; i++)
        if (strcmp(sc->ids[i], id) == 0) return 1;
    return 0;
}

static void scan_dir(menu_scan_t *sc, const char *root, const char *rel, int depth)
{
    if (depth > MENU_SCAN_DEPTH_MAX || sc->count >= MENU_ENTRIES_MAX) return;

    char dir[1024];
    if (rel[0]) snprintf(dir, sizeof(dir), "%s/%s", root, rel);
    else        snprintf(dir, sizeof(dir), "%s", root);

    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) && sc->count < MENU_ENTRIES_MAX) {
        if (de->d_name[0] == '.') continue;

        char child_rel[1024];
        if (rel[0]) snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name);
        else        snprintf(child_rel, sizeof(child_rel), "%s", de->d_name);

        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", root, child_rel);

        /* stat rather than trust d_type: DT_UNKNOWN is legal on some
         * filesystems, and it follows the symlinks Wine's trees are full of. */
        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir(sc, root, child_rel, depth + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;

        size_t nl = strlen(de->d_name);
        if (nl < 9 || strcmp(de->d_name + nl - 8, ".desktop") != 0) continue;

        /* The id of a nested entry is its path relative to the dir with '/'
         * turned into '-'; that id, not the bare basename, is what shadows a
         * lower-precedence dir's entry. */
        char id[MENU_ID_MAX];
        if (snprintf(id, sizeof(id), "%s", child_rel) >= (int)sizeof(id)) continue;
        for (char *c = id; *c; c++) if (*c == '/') *c = '-';

        if (id_seen(sc, id)) continue;
        if (sc->id_count < MENU_ENTRIES_MAX)
            snprintf(sc->ids[sc->id_count++], sizeof(sc->ids[0]), "%s", id);

        if (parse_desktop(full, id, &sc->apps[sc->count])) sc->count++;
    }
    closedir(d);
}

static int app_cmp(const void *a, const void *b)
{
    const menu_app_t *x = a, *y = b;

    /* "Other" sinks to the bottom; the rest are alphabetical by category. */
    int xo = strcmp(x->cat, "Other") == 0;
    int yo = strcmp(y->cat, "Other") == 0;
    if (xo != yo) return xo - yo;

    int c = strcasecmp(x->cat, y->cat);
    if (c) return c;
    return strcasecmp(x->name, y->name);
}

/* ── Build ───────────────────────────────────────────────── */

/* Append a row to page. Rows of a page need not be contiguous in entries[] —
 * only in order relative to each other, since a page's view is entries[] in
 * array order with the other pages filtered out. */
static void menu_add(syn_menu_t *m, const char *page, int kind, const char *label,
                     const char *action, const char *cmd)
{
    if (m->count >= MENU_ENTRIES_MAX) return;
    syn_menu_entry_t *e = &m->entries[m->count++];
    e->kind = kind;
    snprintf(e->label, sizeof(e->label), "%s", label);
    snprintf(e->menu, sizeof(e->menu), "%s", page);
    e->menu_to[0] = '\0';
    snprintf(e->action, sizeof(e->action), "%s", action ? action : "");
    snprintf(e->cmd, sizeof(e->cmd), "%s", cmd ? cmd : "");
}

/* A root row that opens page `to`. */
static void menu_add_submenu(syn_menu_t *m, const char *label, const char *to)
{
    if (m->count >= MENU_ENTRIES_MAX) return;
    menu_add(m, "", MENU_ROW_SUBMENU, label, NULL, NULL);
    snprintf(m->entries[m->count - 1].menu_to,
             sizeof(m->entries[0].menu_to), "%s", to);
}

/* A submenu page's first row, going back to the root. It is the mouse's way
 * out — the arrow keys have Left/Esc, but the pointer had nothing to click, so
 * every page below the root gets one of these at the top. menu_to is "" so it
 * rides the same menu_open_page(m, "") that Left does; the kind is its own so
 * it can be drawn (and skipped over on landing) without being taken for a leaf. */
static void menu_add_back(syn_menu_t *m, const char *page)
{
    menu_add(m, page, MENU_ROW_BACK, "Back", NULL, NULL);
}

/* The fixed rows. Were STATIC_ITEMS/POWER_ITEMS in the retired
 * synapse-menu-gen.py, with one difference that matters: the panels are bind
 * *actions* here. waybar had to reach synui by running `wtype -M logo -k c` —
 * pressing the keybind from outside, because there is no other IPC into the
 * compositor. In-process there is no need to pretend to be a keyboard.
 *
 * The split across two pages is by how often a row is wanted, not by what it
 * is: the three at the root are the ones worth a keystroke, and the rest are a
 * drill-in. Search reaches all of them either way. */
static void menu_add_static(syn_menu_t *m)
{
    menu_add(m, "", MENU_ROW_HEADER, "SYSTEM", NULL, NULL);
    menu_add(m, "", MENU_ROW_ITEM, "Control Panel",   "control", NULL);
    menu_add(m, "", MENU_ROW_ITEM, "Task Manager",    "taskmgr", NULL);
    menu_add(m, "", MENU_ROW_ITEM, "Terminal",        NULL, "foot");
    menu_add_submenu(m, MENU_PAGE_TOOLS, MENU_PAGE_TOOLS);
}

static void menu_add_tools(syn_menu_t *m)
{
    const char *p = MENU_PAGE_TOOLS;
    menu_add_back(m, p);
    menu_add(m, p, MENU_ROW_ITEM, "AI Shell (synsh)", NULL, "foot synsh");
    menu_add(m, p, MENU_ROW_ITEM, "System Status",   NULL, "foot --hold syn status");
    menu_add(m, p, MENU_ROW_ITEM, "Network Setup",   NULL, "foot -e nmtui");
    /* cups ships a complete printer admin UI on localhost:631, so there is no
     * GUI to write or package — this is the whole printer story. cups.socket is
     * socket-activated, so opening the page is also what starts the daemon. */
    menu_add(m, p, MENU_ROW_ITEM, "Printers",         NULL, "xdg-open http://localhost:631/");
    /* A full -Syu, never a bare -Sy: syncing the databases and then installing
     * anything less than everything is a partial upgrade, which on Arch means a
     * 404 on some dependency at best and a half-upgraded system at worst.
     * --hold so the window survives the command; foot gives sudo a pty. */
    menu_add(m, p, MENU_ROW_ITEM, "Update System",   NULL, "foot --hold sudo pacman -Syu");
}

static void menu_add_power(syn_menu_t *m)
{
    menu_add(m, "", MENU_ROW_HEADER, "POWER", NULL, NULL);
    menu_add(m, "", MENU_ROW_ITEM, "Lock Screen", "lock", NULL);
    menu_add(m, "", MENU_ROW_ITEM, "Log Out",     "quit", NULL);
    menu_add(m, "", MENU_ROW_ITEM, "Reboot",      NULL, "sudo systemctl reboot");
    menu_add(m, "", MENU_ROW_ITEM, "Shut Down",   NULL, "sudo systemctl poweroff");
}

/* Root gets one row per category, each opening a page of that category's apps.
 * Flat, every app was a root row under one of ~10 headers: 126 apps against 22
 * drawn rows, so five screens of scrolling to reach the bottom of a list whose
 * shape you cannot see. The categories were already there as headers — this
 * makes them the thing you choose, so the root is one screen and no app is more
 * than one keystroke deeper than it was. */
static void menu_build(syn_server_t *s)
{
    syn_menu_t *m = &s->menu;
    m->count = 0;

    /* The scan is ~1500 dirents and ~170 opens; measured well under 10ms, which
     * is why it happens at open rather than being cached. A cache would be a
     * second source of truth that can disagree with what is installed — the
     * exact failure the generated waybar menu shipped. It runs before any row
     * is emitted now: the root's category rows are not knowable until the apps
     * have been grouped, and the root has to be built in the order it is drawn. */
    menu_scan_t *sc = calloc(1, sizeof(*sc));
    if (!sc) {                     /* out of memory: still give them the fixed rows */
        menu_add_static(m);
        menu_add_tools(m);
        menu_add_power(m);
        return;
    }

    /* ~/.local/share/applications first — it shadows the system dirs, and it is
     * where Wine writes every shortcut it imports. */
    const char *home = getenv("HOME");
    if (home && *home) {
        char home_dir[512];
        if (snprintf(home_dir, sizeof(home_dir), "%s/.local/share/applications",
                     home) < (int)sizeof(home_dir))
            scan_dir(sc, home_dir, "", 0);
    }
    for (unsigned i = 0; i < sizeof(APP_DIRS_SYSTEM) / sizeof(APP_DIRS_SYSTEM[0]); i++)
        scan_dir(sc, APP_DIRS_SYSTEM[i], "", 0);

    qsort(sc->apps, sc->count, sizeof(sc->apps[0]), app_cmp);

    menu_add_static(m);

    /* The apps are sorted by category, so a change of category is a new group.
     * The root's row for it goes in as the group opens; the apps themselves go
     * on that group's own page. */
    menu_add(m, "", MENU_ROW_HEADER, "APPLICATIONS", NULL, NULL);
    const char *cur = NULL;
    for (int i = 0; i < sc->count; i++) {
        if (!cur || strcmp(cur, sc->apps[i].cat) != 0) {
            cur = sc->apps[i].cat;
            menu_add_submenu(m, cur, cur);
        }
    }

    menu_add_power(m);
    menu_add_tools(m);
    /* Each category's page opens with a Back row. The apps are still grouped by
     * category, so a change of category is the start of a new page and the place
     * that row goes; a page's rows need only be in order relative to each other,
     * so emitting Back here — ahead of that category's first app — makes it the
     * page's first row. */
    const char *page = NULL;
    for (int i = 0; i < sc->count; i++) {
        if (!page || strcmp(page, sc->apps[i].cat) != 0) {
            page = sc->apps[i].cat;
            menu_add_back(m, page);
        }
        menu_add(m, sc->apps[i].cat, MENU_ROW_ITEM, sc->apps[i].name,
                 NULL, sc->apps[i].cmd);
    }

    wlr_log(WLR_DEBUG, "synui: menu: %d apps scanned, %d rows", sc->count, m->count);
    free(sc);
}

/* ── Filter ──────────────────────────────────────────────── */

static int label_matches(const char *label, const char *needle)
{
    if (!needle[0]) return 1;
    size_t nl = strlen(needle);
    for (const char *p = label; *p; p++)
        if (strncasecmp(p, needle, nl) == 0) return 1;
    return 0;
}

/* Rebuild view[] from filter[] and the open page.
 *
 * Unfiltered, the view is the current page's rows, headers included. Filtered,
 * it is every matching item on every page — searching only the page you are
 * looking at would make the submenus a place things can hide, which is the one
 * thing they must not be. The submenu rows themselves drop out: they launch
 * nothing, and "Games" matching "gam" ahead of the games is not an answer to
 * what was typed. Headers drop out too — a section heading floating above no
 * rows is noise, and the filtered list is short enough not to need signposting. */
static void menu_refilter(syn_menu_t *m)
{
    m->view_count = 0;
    for (int i = 0; i < m->count; i++) {
        const syn_menu_entry_t *e = &m->entries[i];
        if (m->filter[0]) {
            if (e->kind != MENU_ROW_ITEM) continue;
            if (!label_matches(e->label, m->filter)) continue;
        } else {
            if (strcmp(e->menu, m->page) != 0) continue;
        }
        m->view[m->view_count++] = i;
    }

    /* Land on something worth acting on. With no filter the first row is a
     * header, so an unconditional 0 would put the cursor on a rule that does
     * nothing; and a submenu page opens on a Back row, so landing there would
     * arm Enter to undo the very drill-in that got here. Skip both to the first
     * real item. */
    m->selected = 0;
    while (m->selected < m->view_count &&
           (m->entries[m->view[m->selected]].kind == MENU_ROW_HEADER ||
            m->entries[m->view[m->selected]].kind == MENU_ROW_BACK))
        m->selected++;
    if (m->selected >= m->view_count) m->selected = 0;
    m->scroll = 0;
}

/* Open a submenu page, or (page = "") go back to the root. Backing out restores
 * where the root was left, so drilling in to look at something and coming back
 * does not also lose your place in the list you came from. */
static void menu_open_page(syn_menu_t *m, const char *page)
{
    int to_root = !page[0];
    if (!m->page[0] && !to_root) {
        m->root_selected = m->selected;
        m->root_scroll   = m->scroll;
    }
    snprintf(m->page, sizeof(m->page), "%s", page);
    m->filter[0] = '\0';
    menu_refilter(m);
    if (to_root && m->root_selected < m->view_count) {
        m->selected = m->root_selected;
        m->scroll   = m->root_scroll;
    }
}

/* ── Panel ───────────────────────────────────────────────── */

/* Defined in the Pointer section below; menu_hide()/menu_key() disarm any
 * pending hover-open, and both come before it. */
static void menu_cancel_hover_open(syn_menu_t *m);

void menu_show(syn_server_t *s)
{
    s->menu.filter[0] = '\0';
    s->menu.page[0] = '\0';           /* always opens at the root */
    s->menu.root_selected = 0;
    s->menu.root_scroll   = 0;
    menu_build(s);
    menu_refilter(&s->menu);
    s->menu.visible = 1;
    wlr_log(WLR_INFO, "synui: start menu shown (%d rows)", s->menu.count);
    synui_render_menu(s);
}

void menu_hide(syn_server_t *s)
{
    s->menu.visible = 0;
    menu_cancel_hover_open(&s->menu);
    synui_render_menu(s);
}

void menu_toggle(syn_server_t *s)
{
    if (s->menu.visible) menu_hide(s);
    else                 menu_show(s);
}

/* The view[] row drawn at the top of the panel. render.c draws from here and the
 * pointer hit-test measures from here, so it is one function rather than the
 * same clamp written out in both — a hit-test that disagreed with the drawing by
 * a row would launch the neighbour of whatever was clicked. */
int menu_first_row(const syn_menu_t *m)
{
    int max_scroll = m->view_count - MENU_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    int first = m->scroll;
    if (first > max_scroll) first = max_scroll;
    if (first < 0) first = 0;
    return first;
}

/* Keep the selection inside the drawn window. */
static void menu_scroll_to_selected(syn_menu_t *m)
{
    if (m->selected < m->scroll) m->scroll = m->selected;
    if (m->selected >= m->scroll + MENU_ROWS) m->scroll = m->selected - MENU_ROWS + 1;

    int max_scroll = m->view_count - MENU_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    if (m->scroll > max_scroll) m->scroll = max_scroll;
    if (m->scroll < 0) m->scroll = 0;
}

/* Move by dir, skipping headers. Stops at the ends rather than wrapping, as in
 * ctlpanel_move. */
static void menu_move(syn_menu_t *m, int dir)
{
    int row = m->selected;
    for (;;) {
        row += dir;
        if (row < 0 || row >= m->view_count) return;
        if (m->entries[m->view[row]].kind != MENU_ROW_HEADER) break;
    }
    m->selected = row;
    menu_scroll_to_selected(m);
}

static void menu_activate(syn_server_t *s)
{
    syn_menu_t *m = &s->menu;
    if (m->selected < 0 || m->selected >= m->view_count) return;

    syn_menu_entry_t *e = &m->entries[m->view[m->selected]];

    if (e->kind == MENU_ROW_SUBMENU || e->kind == MENU_ROW_BACK) {
        menu_open_page(m, e->menu_to);   /* Back's menu_to is "" — the root */
        synui_render_menu(s);
        return;
    }
    if (e->kind != MENU_ROW_ITEM) return;

    /* Hide before acting. A bind action may open another panel, and rendering
     * this one afterwards would draw the menu back over the top of it — the
     * same ordering ctlpanel's jump-offs observe. */
    menu_hide(s);

    if (e->action[0]) synui_binding_execute(s, e->action, NULL);
    else if (e->cmd[0]) synui_spawn(e->cmd);
}

/* ── Pointer ─────────────────────────────────────────────── */
/* The menu was keyboard-only, which was never the intent: it went native to be
 * *able* to take the keyboard, not to stop taking the mouse. Nothing here is a
 * second way to work the menu — hover moves the same selection the arrows move,
 * and a click runs the same menu_activate() Enter does.
 *
 * The geometry these measure against is whatever the last render wrote to the
 * panel, so they cannot drift from what is on screen the way a second copy of
 * the layout constants would. */

static int menu_in_panel(const syn_menu_t *m, double lx, double ly)
{
    return lx >= m->x && lx < m->x + m->w && ly >= m->y && ly < m->y + m->h;
}

/* view[] index under (lx,ly), or -1 for the chrome, a header, or outside. */
static int menu_row_at(const syn_menu_t *m, double lx, double ly)
{
    if (!menu_in_panel(m, lx, ly)) return -1;

    /* A row's text sits on its baseline and its highlight is drawn around it,
     * so the band starts MENU_ROW_ASC above the first baseline. */
    double top = m->y + MENU_TOP - MENU_ROW_ASC;
    if (ly < top) return -1;                       /* the title and search line */

    int i = (int)((ly - top) / MENU_ROW_H);
    if (i < 0 || i >= MENU_ROWS) return -1;        /* the footer */

    int row = menu_first_row(m) + i;
    if (row >= m->view_count) return -1;
    if (m->entries[m->view[row]].kind == MENU_ROW_HEADER) return -1;
    return row;
}

/* Pull the selection back inside the drawn window after a wheel scroll. The
 * highlight is what Enter acts on, so a selection left on a row that has
 * scrolled away would arm Enter with something you cannot see. */
static void menu_select_into_view(syn_menu_t *m)
{
    int first = menu_first_row(m);
    int last  = first + MENU_ROWS - 1;
    if (last >= m->view_count) last = m->view_count - 1;
    if (m->selected >= first && m->selected <= last) return;

    int dir = m->selected < first ? +1 : -1;
    int row = m->selected < first ? first : last;
    while (row >= first && row <= last &&
           m->entries[m->view[row]].kind == MENU_ROW_HEADER)
        row += dir;
    if (row >= first && row <= last) m->selected = row;
}

/* Open a hovered submenu once the pointer has rested on it — the pointer's
 * equivalent of pressing Right. Deliberately re-derives the row under the cursor
 * instead of trusting the one armed: a slow drift onto the next row re-arms with
 * that one, but a drift off the panel entirely leaves the timer pending, and
 * opening a page the cursor has already left would be a surprise. */
static int menu_hover_open_cb(void *data)
{
    syn_server_t *s = data;
    syn_menu_t *m = &s->menu;
    if (!m->visible || m->filter[0]) return 0;

    int row = menu_row_at(m, s->cursor->x, s->cursor->y);
    if (row < 0 || m->entries[m->view[row]].kind != MENU_ROW_SUBMENU) return 0;

    m->selected = row;
    menu_open_page(m, m->entries[m->view[row]].menu_to);
    synui_render_menu(s);
    return 0;
}

static void menu_arm_hover_open(syn_server_t *s)
{
    syn_menu_t *m = &s->menu;
    if (!m->hover_timer) {
        struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
        m->hover_timer = wl_event_loop_add_timer(loop, menu_hover_open_cb, s);
        if (!m->hover_timer) return;
    }
    /* Re-arming pushes a pending open out, so passing over one submenu on the
     * way to another never opens the one you were only crossing. */
    wl_event_source_timer_update(m->hover_timer, MENU_HOVER_OPEN_MS);
}

static void menu_cancel_hover_open(syn_menu_t *m)
{
    if (m->hover_timer) wl_event_source_timer_update(m->hover_timer, 0);
}

void menu_motion(syn_server_t *s, double lx, double ly)
{
    syn_menu_t *m = &s->menu;
    if (!m->visible) return;

    /* Off a row, the highlight stays where it is rather than clearing: the
     * pointer crossing a header on its way somewhere should not disarm Enter.
     * A pending hover-open stays armed too — the cursor is still headed
     * somewhere, and the callback re-checks the row before it acts. */
    int row = menu_row_at(m, lx, ly);
    if (row < 0 || row == m->selected) return;
    m->selected = row;

    /* Resting on a submenu opens it after MENU_HOVER_OPEN_MS; moving onto any
     * other row cancels that, so an item or the Back row does not carry a stale
     * open. Searching disables it — the view is not a page to drill into. */
    if (!m->filter[0] && m->entries[m->view[row]].kind == MENU_ROW_SUBMENU)
        menu_arm_hover_open(s);
    else
        menu_cancel_hover_open(m);

    synui_render_menu(s);
}

void menu_click(syn_server_t *s, double lx, double ly)
{
    syn_menu_t *m = &s->menu;
    if (!m->visible) return;

    if (!menu_in_panel(m, lx, ly)) { menu_hide(s); return; }

    int row = menu_row_at(m, lx, ly);
    if (row < 0) return;             /* the panel's own chrome: swallowed */
    m->selected = row;
    menu_activate(s);
}

void menu_scroll(syn_server_t *s, double delta)
{
    syn_menu_t *m = &s->menu;
    if (!m->visible || delta == 0) return;
    if (m->view_count <= MENU_ROWS) return;      /* nothing to scroll to */

    /* Three rows a notch — the conventional step, and enough that a long
     * category is a few flicks rather than a grind. The delta's magnitude is
     * deliberately ignored: it is in the source's own units (a wheel notch, a
     * touchpad's pixels), and only its sign means the same thing in both. */
    m->scroll += delta > 0 ? 3 : -3;

    int max_scroll = m->view_count - MENU_ROWS;
    if (m->scroll > max_scroll) m->scroll = max_scroll;
    if (m->scroll < 0) m->scroll = 0;

    menu_select_into_view(m);
    synui_render_menu(s);
}

/* ── Keys ────────────────────────────────────────────────── */

int menu_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_menu_t *m = &s->menu;
    if (!m->visible) return 0;

    /* A keystroke means the keyboard is driving now; drop any hover-open the
     * pointer left armed so it cannot open a page out from under the keys. */
    menu_cancel_hover_open(m);

    /* Super+… still reaches the global bind table, so every other panel's
     * shortcut works with the menu open, as it does from ctlpanel. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
        /* Esc backs out one step at a time — filter, then submenu, then the
         * menu itself — so a typo or a wrong turn costs a keystroke rather than
         * everything you had done to get here. */
        if (m->filter[0]) { m->filter[0] = '\0'; menu_refilter(m); synui_render_menu(s); }
        else if (m->page[0]) { menu_open_page(m, ""); synui_render_menu(s); }
        else              menu_hide(s);
        return 1;

    /* Right/Left drill in and back out, as in any menu with submenus. Right on
     * a leaf does nothing rather than launching it — Enter is the only key that
     * runs anything, so walking the list with the arrows can never start a
     * program by accident. Left at the root does nothing for the same reason:
     * a stray press should not throw away everything you had open. */
    case XKB_KEY_Right:
        if (m->selected >= 0 && m->selected < m->view_count &&
            m->entries[m->view[m->selected]].kind == MENU_ROW_SUBMENU)
            menu_activate(s);
        return 1;
    case XKB_KEY_Left:
        if (m->page[0] && !m->filter[0]) { menu_open_page(m, ""); synui_render_menu(s); }
        return 1;

    case XKB_KEY_Up:
        menu_move(m, -1);
        synui_render_menu(s);
        return 1;
    case XKB_KEY_Down:
        menu_move(m, +1);
        synui_render_menu(s);
        return 1;

    case XKB_KEY_Prior:                       /* Page Up */
        for (int i = 0; i < MENU_ROWS / 2; i++) menu_move(m, -1);
        synui_render_menu(s);
        return 1;
    case XKB_KEY_Next:                        /* Page Down */
        for (int i = 0; i < MENU_ROWS / 2; i++) menu_move(m, +1);
        synui_render_menu(s);
        return 1;

    case XKB_KEY_Home:
        m->selected = 0;
        if (m->view_count && m->entries[m->view[0]].kind == MENU_ROW_HEADER)
            menu_move(m, +1);
        menu_scroll_to_selected(m);
        synui_render_menu(s);
        return 1;
    case XKB_KEY_End:
        m->selected = m->view_count ? m->view_count - 1 : 0;
        menu_scroll_to_selected(m);
        synui_render_menu(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        menu_activate(s);
        return 1;

    case XKB_KEY_BackSpace: {
        size_t n = strlen(m->filter);
        if (n) {
            /* Back off a whole UTF-8 character: lopping one byte off a
             * multi-byte sequence leaves a dangling lead that cairo_show_text
             * would reject, poisoning the context and blanking the panel. */
            n = news_utf8_trim(m->filter, n - 1);
            m->filter[n] = '\0';
            menu_refilter(m);
            synui_render_menu(s);
        }
        return 1;
    }

    default: break;
    }

    /* Type to search. xkb_keysym_to_utf8 resolves the keysym the seat actually
     * delivered, which matters because a virtual keyboard (wtype, and anything
     * driving synui over virtual-keyboard-v1) ships a one-level keymap and
     * sends the UNSHIFTED keysym plus a shift bit — there is no shifted level
     * for it to resolve to. Filtering is case-insensitive, so the shift bit
     * need not be reapplied here; it just must not be treated as a chord. */
    char buf[8];
    int n = xkb_keysym_to_utf8(sym, buf, sizeof(buf));
    if (n > 1) {                                   /* n counts the NUL */
        size_t len = strlen(m->filter);
        if (len + (size_t)(n - 1) < sizeof(m->filter)) {
            memcpy(m->filter + len, buf, (size_t)(n - 1));
            m->filter[len + n - 1] = '\0';
            menu_refilter(m);
            synui_render_menu(s);
        }
        return 1;
    }

    return 1;   /* modal: swallow other unmodified keys while open */
}
