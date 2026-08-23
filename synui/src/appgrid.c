/*
 * appgrid.c — the fullscreen application grid ("show all apps")
 *
 * GNOME's Show Applications, drawn by the compositor: the whole screen goes
 * dim, every application installed on the box lays out as a page of large
 * icons, you type to search, arrow to a tile and press Enter.
 *
 * ── Why this is the compositor's and not the bar's ──────────────────────────
 *
 * The bar already has a start menu, and it is a different thing: a categorised
 * LIST in a popup that belongs to one output, drawn by quickshell. Three
 * reasons this could not be that popup wearing a different stylesheet:
 *
 *   1. A page that covers the screen has to cover the BAR. The bar's own
 *      layer-shell surface cannot draw over the layer it is on.
 *   2. "Every application installed" has to be reachable on a desktop whose
 *      bar is switched off, and the bar's menu by definition is not.
 *   3. The dock's show-all-apps button is a compositor hit test. Routing it out
 *      to another process to ask for a window back is a round trip that fails
 *      silently when that process is not there — which is exactly what the
 *      first version of that button did.
 *
 * Mission control is here for the same three reasons, and this is shaped like
 * it: a scene rect over the whole output for the dim, one cairo buffer the size
 * of the output on top.
 *
 * ── The list is not the .desktop files ──────────────────────────────────────
 *
 * A .desktop file is not evidence that a human ever wants to launch the thing.
 * Two filters, and both of them are shared with the bar's menu on purpose:
 *
 *   menu-hidden.conf   ids, by name. THE SAME TWO FILES the start menu reads
 *                      (/usr/share/synui/ then ~/.config/synui/), so a line
 *                      added to hide something hides it in both places. This is
 *                      shared DATA, which is the only kind of sharing available
 *                      across the process boundary.
 *
 *   the Wine rules     patterns, for the shapes that cannot be enumerated —
 *                      uninstallers, readmes, "Visit our web site", SDK tooling
 *                      inside a prefix. Every Wine prefix invents new ones.
 *
 * ⚠ THE WINE RULES ARE A SECOND COPY. The first is `isNoise()` in
 * quickshell/StartMenu.qml, and there is no way to have one: that is QML in
 * another process and this is C in the compositor. They are kept deliberately
 * literal and in the same order as each other so a diff of the two reads
 * straight across, and both name the other. If you change one, change both —
 * the failure mode is not a crash, it is the same box listing 65 Wine entries
 * in one launcher and 15 in the other.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "synui.h"

/* ── Small string helpers ────────────────────────────────── */

/* Case-insensitive "does `hay` contain `needle`". strcasestr is GNU and we are
 * already _GNU_SOURCE, but it is wrapped so the null-tolerance is in one place:
 * every field here can legitimately be absent. */
static bool ci_has(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return false;
    return strcasestr(hay, needle) != NULL;
}

static bool ci_starts(const char *hay, const char *prefix)
{
    if (!hay || !prefix) return false;
    return strncasecmp(hay, prefix, strlen(prefix)) == 0;
}

/* True when `hay` ends with `suffix`, case-insensitively. */
static bool ci_ends(const char *hay, const char *suffix)
{
    if (!hay || !suffix) return false;
    size_t hl = strlen(hay), sl = strlen(suffix);
    return hl >= sl && strcasecmp(hay + hl - sl, suffix) == 0;
}

/* Is `list` — a `;`-separated .desktop list — carrying `want`? */
static bool list_has(const char *list, const char *want)
{
    if (!list || !*list) return false;
    const char *p = list;
    size_t wl = strlen(want);
    while (*p) {
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == wl && strncasecmp(p, want, wl) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

/* ── The hidden-id list ──────────────────────────────────────
 *
 * ⚠ THE SAME TWO FILES quickshell/MenuState.qml reads, in the same order and
 * with the same `!id` un-hide rule. Shared data is the only sharing there can
 * be between a compositor written in C and a shell written in QML, so the file
 * format is the contract — see data/menu-hidden.conf, which documents it for
 * whoever edits it.
 */
#define APPGRID_HIDDEN_MAX 512

static char hidden_ids[APPGRID_HIDDEN_MAX][128];
static int  hidden_count;

static void hidden_set(const char *id, bool hide)
{
    for (int i = 0; i < hidden_count; i++)
        if (strcmp(hidden_ids[i], id) == 0) {
            if (!hide) {
                /* Un-hidden: drop it, so a later file's `!id` really does undo
                 * an earlier file's line rather than leaving both present and
                 * the answer depending on which loop looks first. */
                memmove(hidden_ids[i], hidden_ids[i + 1],
                        (size_t)(hidden_count - i - 1) * sizeof(hidden_ids[0]));
                hidden_count--;
            }
            return;
        }
    if (hide && hidden_count < APPGRID_HIDDEN_MAX)
        snprintf(hidden_ids[hidden_count++], sizeof(hidden_ids[0]), "%s", id);
}

static bool hidden_has(const char *id)
{
    for (int i = 0; i < hidden_count; i++)
        if (strcmp(hidden_ids[i], id) == 0) return true;
    return false;
}

static void hidden_read(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;   /* neither file is required to exist */

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        char *end = p + strlen(p);
        while (end > p && isspace((unsigned char)end[-1])) *--end = '\0';
        if (!*p || *p == '#') continue;

        bool hide = true;
        if (*p == '!') { hide = false; p++; }
        if (*p) hidden_set(p, hide);
    }
    fclose(f);
}

static void hidden_load(void)
{
    hidden_count = 0;
    hidden_read("/usr/share/synui/menu-hidden.conf");

    char path[256];
    if (syn_config_path(path, sizeof(path), "menu-hidden.conf"))
        hidden_read(path);   /* the user's, second, so it wins */
}

/* ── The Wine noise rules ────────────────────────────────────
 *
 * ⚠ SECOND COPY. The first is `isNoise()` in quickshell/StartMenu.qml — see the
 * warning at the top of this file. Kept in that function's order so the two
 * read straight across.
 *
 * Hidden from SEARCH as well as from the pages, which is the one place this is
 * more than tidying: typing "sims" and being offered "Uninstall The Sims" one
 * row from "The Sims" is a genuinely dangerous list to arrow through.
 */

/* Runs a document or a URL, not a program. The only universal rule here —
 * everything below it is scoped to Wine, because outside a prefix these words
 * appear in the names of real applications ("Help" is a program on some
 * systems; "Documentation" is a category Qt Assistant legitimately claims). */
static bool exec_opens_a_document(const char *exec)
{
    static const char *const ext[] = {
        ".url", ".htm", ".html", ".txt", ".chm", ".hlp", ".pdf", ".rtf", ".nfo",
    };
    for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) {
        const char *p = exec;
        size_t len = strlen(ext[i]);
        /* The QML side spells this `\b` after the extension. The equivalent
         * here is "followed by a non-word character or the end of the line",
         * which matters: without it `.htm` also matches `.html`, and — worse —
         * a program genuinely called something like `nfoview` would be hidden
         * for having the letters in its name. */
        while ((p = strcasestr(p, ext[i])) != NULL) {
            char after = p[len];
            if (!after || !(isalnum((unsigned char)after) || after == '_'))
                return true;
            p += len;
        }
    }
    return false;
}

static bool app_is_noise(const char *id, const char *name, const char *exec)
{
    if (exec_opens_a_document(exec)) return true;
    if (ci_has(exec, "winebrowser")) return true;

    if (!ci_starts(id, "wine-")) return false;

    if (ci_has(exec, "uninst") || ci_has(exec, "unins0")) return true;

    /* The DirectX SDK installs ~30 tools into the Start Menu. They are real
     * programs, and they are developer tooling for a toolkit the user installed
     * in order to run a GAME. */
    if (ci_has(id, "DirectX SDK") || ci_has(id, "DirectX Utilities") ||
        ci_has(id, "DirectX Documentation") || ci_has(id, "Windows DirectX"))
        return true;

    static const char *const starts[] = {
        "uninstall", "register", "visit",
    };
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); i++)
        if (ci_starts(name, starts[i])) return true;

    static const char *const anywhere[] = {
        "readme", "documentation", "manual", "release notes", "help",
        "registration", "contact support", "technical support",
        "web page", "webpage", "web site", "website", "homepage",
    };
    for (size_t i = 0; i < sizeof(anywhere) / sizeof(anywhere[0]); i++)
        if (ci_has(name, anywhere[i])) return true;

    /* Multiplayer matchmaking services that shut down two decades ago. The
     * shortcut opens a dead URL. */
    if (ci_ends(name, "on mplayer.com")) return true;
    if (ci_ends(name, "on heat"))        return true;
    if (ci_ends(name, "update") || ci_ends(name, "updates")) return true;

    return false;
}

/* ── Reading one .desktop file ───────────────────────────── */

/* `%f %F %u %U %i %c %k %d %D %n %N %v %m` — the launcher is supposed to
 * substitute these and we have nothing to substitute, so they come out. `%%`
 * is a literal percent and survives as one.
 *
 * A near-copy of icons.c's strip_exec_field_codes(), and deliberately not a
 * call to it: that one is static, and exporting it to share eight lines would
 * put a second caller on a helper whose one job is to serve icon_lookup(). */
static void strip_field_codes(char *exec)
{
    char *w = exec;
    for (const char *r = exec; *r; r++) {
        if (*r != '%') { *w++ = *r; continue; }
        if (r[1] == '%') { *w++ = '%'; r++; continue; }
        if (r[1]) r++;              /* drop the code letter with the percent */
    }
    *w = '\0';

    /* A trailing code leaves a trailing space, and a run of them leaves several
     * in the middle. Collapse, then trim. */
    w = exec;
    bool sp = false;
    for (const char *r = exec; *r; r++) {
        if (*r == ' ') { sp = true; continue; }
        if (sp && w != exec) *w++ = ' ';
        sp = false;
        *w++ = *r;
    }
    *w = '\0';
}

/* Everything the filter needs, read in one pass. Returns false when the file is
 * not an application entry at all — which is not an error: .desktop is also how
 * a Link and a Directory are spelled. */
static bool desktop_read(const char *path, const char *id, syn_app_entry_t *e)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char type[32] = {0}, only_show[256] = {0}, not_show[256] = {0};
    char try_exec[256] = {0};
    bool no_display = false, hidden = false;
    int in_main = 0;

    memset(e, 0, sizeof(*e));
    snprintf(e->id, sizeof(e->id), "%s", id);

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        char *end = p + strlen(p);
        while (end > p && isspace((unsigned char)end[-1])) *--end = '\0';
        if (!*p || *p == '#') continue;

        if (*p == '[') { in_main = strcmp(p, "[Desktop Entry]") == 0; continue; }
        if (!in_main) continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = p, *val = eq + 1;

        /* ⚠ THE PLAIN KEY ONLY, never `Name[de]`. The localised variants sort
         * around the plain one arbitrarily, and taking the last-seen would give
         * a German name on an English desktop for any entry that ships one. A
         * real locale match belongs with the rest of i18n; this at least is
         * never wrong on purpose. */
        if      (!strcmp(key, "Type"))       snprintf(type, sizeof(type), "%s", val);
        else if (!strcmp(key, "Name") && !e->name[0])
                                             snprintf(e->name, sizeof(e->name), "%s", val);
        else if (!strcmp(key, "Exec") && !e->exec[0])
                                             snprintf(e->exec, sizeof(e->exec), "%s", val);
        else if (!strcmp(key, "Icon") && !e->icon_hint[0])
                                             snprintf(e->icon_hint, sizeof(e->icon_hint), "%s", val);
        else if (!strcmp(key, "TryExec"))    snprintf(try_exec, sizeof(try_exec), "%s", val);
        else if (!strcmp(key, "OnlyShowIn")) snprintf(only_show, sizeof(only_show), "%s", val);
        else if (!strcmp(key, "NotShowIn"))  snprintf(not_show, sizeof(not_show), "%s", val);
        else if (!strcmp(key, "NoDisplay"))  no_display = !strcasecmp(val, "true");
        else if (!strcmp(key, "Hidden"))     hidden = !strcasecmp(val, "true");
        else if (!strcmp(key, "Terminal"))   e->terminal = !strcasecmp(val, "true");
    }
    fclose(f);

    if (type[0] && strcmp(type, "Application") != 0) return false;
    if (no_display || hidden) return false;
    if (!e->exec[0]) return false;

    /*
     * ⚠ THIS DESKTOP HAS TWO NAMES — `synui` AND `SynapseOS` — and both have to
     * be tested. XDG_CURRENT_DESKTOP carries both, so an entry that names either
     * one is naming us. Testing only one is how a portal config went wrong; the
     * failure here would be quieter still, an application simply absent.
     */
    if (only_show[0] &&
        !list_has(only_show, "synui") && !list_has(only_show, "SynapseOS"))
        return false;
    if (list_has(not_show, "synui") || list_has(not_show, "SynapseOS"))
        return false;

    /* TryExec names a program whose ABSENCE means "do not show this entry" —
     * the spec's own way for a package to ship one .desktop for a binary that
     * may or may not be installed. Only an absolute path is checked: resolving
     * a bare name means walking PATH for every entry on every scan, and a bare
     * TryExec that is missing is rare enough to be worth showing wrongly rather
     * than paying for on every open. */
    if (try_exec[0] == '/' && access(try_exec, X_OK) != 0) return false;

    strip_field_codes(e->exec);
    if (!e->exec[0]) return false;

    if (!e->name[0]) snprintf(e->name, sizeof(e->name), "%s", id);
    return true;
}

/* ── The scan ────────────────────────────────────────────── */

static bool appgrid_have_id(syn_appgrid_t *g, const char *id)
{
    for (int i = 0; i < g->count; i++)
        if (strcmp(g->apps[i].id, id) == 0) return true;
    return false;
}

/*
 * One applications/ tree, recursively.
 *
 * RECURSIVE because Wine's shortcuts are three directories down
 * (applications/wine/Programs/Some Game/Some Game.desktop), and a flat readdir
 * finds none of them — which on a box with games installed is most of what a
 * person actually launches.
 *
 * `rel` is the path so far under the applications/ root, which is what makes
 * the entry id: the spec says the id is that relative path with '/' folded to
 * '-'. That is not cosmetic — it is the string menu-hidden.conf lists and the
 * string the Wine rules test with `wine-`.
 */
static void scan_dir(syn_appgrid_t *g, const char *root, const char *rel,
                     int depth)
{
    if (g->count >= APPGRID_MAX) return;
    /* A cycle through a symlinked directory would otherwise recurse until the
     * path buffer stopped it. Four is past anything real: Wine's deepest is
     * three. */
    if (depth > 4) return;

    char dir[1024];
    if (rel[0]) snprintf(dir, sizeof(dir), "%s/%s", root, rel);
    else        snprintf(dir, sizeof(dir), "%s", root);

    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL && g->count < APPGRID_MAX) {
        if (de->d_name[0] == '.') continue;

        char child_rel[512];
        if (rel[0]) snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name);
        else        snprintf(child_rel, sizeof(child_rel), "%s", de->d_name);

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);

        /* d_type is DT_UNKNOWN on some filesystems, so stat rather than trust
         * it — a scan that silently skipped every entry on one filesystem would
         * look like "those apps are not installed". */
        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir(g, root, child_rel, depth + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        if (!ci_ends(de->d_name, ".desktop")) continue;

        char id[128];
        snprintf(id, sizeof(id), "%s", child_rel);
        id[strlen(id) - 8] = '\0';            /* drop ".desktop" */
        for (char *c = id; *c; c++) if (*c == '/') *c = '-';

        /* First writer wins, and the walk order is XDG precedence — the user's
         * own applications/ directory is scanned before the system ones. This
         * is what lets ~/.local/share/applications/foo.desktop override the
         * packaged foo.desktop rather than appearing twice beside it. */
        if (appgrid_have_id(g, id)) continue;

        syn_app_entry_t e;
        if (!desktop_read(full, id, &e)) continue;
        if (hidden_has(id)) continue;
        if (app_is_noise(e.id, e.name, e.exec)) continue;

        g->apps[g->count++] = e;
    }
    closedir(d);
}

static int app_cmp(const void *a, const void *b)
{
    const syn_app_entry_t *x = a, *y = b;
    int c = strcasecmp(x->name, y->name);
    /* Ties broken on the id so the order is total: two applications genuinely
     * called the same thing would otherwise sort differently between runs, and
     * a grid whose tiles move when nothing changed is a grid you cannot learn
     * the shape of. */
    return c ? c : strcmp(x->id, y->id);
}

/* Every directory the spec says an application entry can live in, in
 * precedence order. Read from the environment rather than hardcoded: the test
 * points XDG_DATA_DIRS at a sandbox, and so does any nested session. */
static void appgrid_walk_xdg(syn_appgrid_t *g)
{
    char root[1024];

    const char *home_data = getenv("XDG_DATA_HOME");
    if (home_data && *home_data) {
        snprintf(root, sizeof(root), "%s/applications", home_data);
        scan_dir(g, root, "", 0);
    } else {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(root, sizeof(root), "%s/.local/share/applications", home);
            scan_dir(g, root, "", 0);
        }
    }

    const char *dirs = getenv("XDG_DATA_DIRS");
    if (!dirs || !*dirs) dirs = "/usr/local/share:/usr/share";

    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", dirs);
    for (char *tok = strtok(buf, ":"); tok; tok = strtok(NULL, ":")) {
        if (!*tok) continue;
        snprintf(root, sizeof(root), "%s/applications", tok);
        scan_dir(g, root, "", 0);
    }
}

/* Rebuild `filt` from `search`. Matched against the NAME and the id, not the
 * Exec: matching Exec means typing "python" offers every application that
 * happens to be written in it, which is a list nobody asked for. */
static void appgrid_refilter(syn_server_t *s)
{
    syn_appgrid_t *g = &s->appgrid;
    g->filt_count = 0;

    for (int i = 0; i < g->count; i++) {
        if (g->search_len > 0 &&
            !ci_has(g->apps[i].name, g->search) &&
            !ci_has(g->apps[i].id,   g->search))
            continue;
        g->filt[g->filt_count++] = i;
    }

    if (g->selected >= g->filt_count) g->selected = g->filt_count - 1;
    if (g->selected < 0)              g->selected = 0;
    g->page = g->filt_count > 0 ? g->selected / APPGRID_PER_PAGE : 0;
}

void appgrid_rescan(syn_server_t *s)
{
    syn_appgrid_t *g = &s->appgrid;

    /* The decoded icons belong to the entries being thrown away. Freed here
     * rather than left to the new scan, which would simply overwrite the
     * pointers — a leak of a few megabytes per rescan that nothing would ever
     * report, because the surfaces are reachable right up to the memcpy. */
    for (int i = 0; i < g->count; i++)
        if (g->apps[i].icon) cairo_surface_destroy(g->apps[i].icon);

    memset(g->apps, 0, sizeof(g->apps));
    g->count = 0;

    hidden_load();
    appgrid_walk_xdg(g);
    qsort(g->apps, (size_t)g->count, sizeof(g->apps[0]), app_cmp);

    g->scanned = 1;
    appgrid_refilter(s);

    wlr_log(WLR_INFO, "synui: appgrid: %d application(s)", g->count);
}

/* ── Queries the renderer asks ───────────────────────────── */

int appgrid_total(syn_server_t *s) { return s->appgrid.filt_count; }

syn_app_entry_t *appgrid_at(syn_server_t *s, int i)
{
    syn_appgrid_t *g = &s->appgrid;
    if (i < 0 || i >= g->filt_count) return NULL;
    return &g->apps[g->filt[i]];
}

cairo_surface_t *appgrid_icon(syn_app_entry_t *e)
{
    /* `icon_tried` and not a NULL check: an application whose Icon= names
     * nothing on disk would otherwise be re-searched — several stat()s across
     * every theme directory — on every frame it is on screen, which with a
     * hover repaint is every pointer motion. */
    if (!e->icon_tried) {
        e->icon_tried = 1;
        if (e->icon_hint[0]) e->icon = icon_decode_named(e->icon_hint);
        /* Falling back to the id is what icon_lookup() does, and it is worth
         * it: a great many entries name their icon after themselves and simply
         * omit the line. */
        if (!e->icon) e->icon = icon_decode_named(e->id);
    }
    return e->icon;
}

/* ── Show / hide ─────────────────────────────────────────── */

void appgrid_show(syn_server_t *s)
{
    syn_appgrid_t *g = &s->appgrid;

    /* Scanned once per session, on the first open. Applications do not appear
     * while you are looking at the page, and re-walking every XDG data
     * directory on each open puts a readdir storm on a keypress. `synctl
     * dispatch apps_rescan` is there for when something really was installed. */
    if (!g->scanned) appgrid_rescan(s);

    /* Opens CLEAN — no search, first tile, first page. A picker that came back
     * holding the query you last typed is one you have to clear before you can
     * use it, and the search here is the fast path rather than a filter you set
     * up and live with. */
    g->search[0]  = '\0';
    g->search_len = 0;
    g->selected   = 0;
    g->page       = 0;
    appgrid_refilter(s);

    g->visible = 1;
    synui_render_appgrid(s);
}

void appgrid_hide(syn_server_t *s)
{
    if (!s->appgrid.visible) return;
    s->appgrid.visible = 0;
    synui_render_appgrid(s);
}

void appgrid_toggle(syn_server_t *s)
{
    if (s->appgrid.visible) appgrid_hide(s);
    else                    appgrid_show(s);
}

/* ── Launching ───────────────────────────────────────────── */

static void appgrid_launch(syn_server_t *s, syn_app_entry_t *e)
{
    if (!e) return;

    char cmd[512];
    if (e->terminal) {
        /* Terminal=true means the entry is a command-line program and the
         * launcher owes it a terminal. synuirc's `terminal` is the one place
         * this desktop records which one — the same value the file manager and
         * the desktop menu use, so all three open the same program. */
        const char *term = s->config.terminal[0] ? s->config.terminal : "kitty";
        snprintf(cmd, sizeof(cmd), "%s -e %s", term, e->exec);
    } else {
        snprintf(cmd, sizeof(cmd), "%s", e->exec);
    }

    /* CLOSED FIRST, and it matters for the same reason it does in the emoji
     * picker: the grid is modal and holds the keyboard, so a window that maps
     * while it is still up arrives behind a panel and without focus. */
    appgrid_hide(s);
    synui_spawn(cmd);
}

/* ── Keyboard ────────────────────────────────────────────── */

static void appgrid_scroll_to_selection(syn_server_t *s)
{
    syn_appgrid_t *g = &s->appgrid;
    if (g->filt_count <= 0) { g->page = 0; return; }
    if (g->selected < 0) g->selected = 0;
    if (g->selected >= g->filt_count) g->selected = g->filt_count - 1;
    g->page = g->selected / APPGRID_PER_PAGE;
}

int appgrid_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->appgrid.visible) return 0;

    syn_appgrid_t *g = &s->appgrid;
    int total = g->filt_count;

    /* Super and Ctrl still belong to the compositor, exactly as in the emoji
     * picker and the control panel's search box: those are how you leave, and a
     * page that swallowed Super+C would trap you in it. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
        /* Esc clears the search before it closes, for the reason emoji_key
         * gives: a search narrowed to nothing is the state you most want out
         * of, and losing the whole page to get out of it is a step too many. */
        if (g->search_len > 0) {
            g->search[0]  = '\0';
            g->search_len = 0;
            g->selected   = 0;
            appgrid_refilter(s);
            synui_render_appgrid(s);
            return 1;
        }
        appgrid_hide(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        if (total > 0) appgrid_launch(s, appgrid_at(s, g->selected));
        else           appgrid_hide(s);
        return 1;

    case XKB_KEY_BackSpace:
        if (g->search_len > 0) {
            g->search[--g->search_len] = '\0';
            g->selected = 0;
            appgrid_refilter(s);
            synui_render_appgrid(s);
        }
        return 1;

    case XKB_KEY_Left:
        if (g->selected > 0) g->selected--;
        appgrid_scroll_to_selection(s);
        synui_render_appgrid(s);
        return 1;

    case XKB_KEY_Right:
        if (g->selected < total - 1) g->selected++;
        appgrid_scroll_to_selection(s);
        synui_render_appgrid(s);
        return 1;

    case XKB_KEY_Up:
        if (g->selected >= APPGRID_COLS) g->selected -= APPGRID_COLS;
        appgrid_scroll_to_selection(s);
        synui_render_appgrid(s);
        return 1;

    case XKB_KEY_Down:
        if (g->selected + APPGRID_COLS < total) g->selected += APPGRID_COLS;
        appgrid_scroll_to_selection(s);
        synui_render_appgrid(s);
        return 1;

    /* A PAGE, not a screenful of rows — this is the one place the pagination is
     * the user-visible unit, and the dots at the bottom are counting it. */
    case XKB_KEY_Page_Up:
        g->selected -= APPGRID_PER_PAGE;
        if (g->selected < 0) g->selected = 0;
        appgrid_scroll_to_selection(s);
        synui_render_appgrid(s);
        return 1;

    case XKB_KEY_Page_Down:
        g->selected += APPGRID_PER_PAGE;
        if (g->selected > total - 1) g->selected = total - 1;
        if (g->selected < 0)         g->selected = 0;
        appgrid_scroll_to_selection(s);
        synui_render_appgrid(s);
        return 1;

    case XKB_KEY_Home:
        g->selected = 0;
        appgrid_scroll_to_selection(s);
        synui_render_appgrid(s);
        return 1;

    case XKB_KEY_End:
        g->selected = total > 0 ? total - 1 : 0;
        appgrid_scroll_to_selection(s);
        synui_render_appgrid(s);
        return 1;

    default:
        break;
    }

    /* Printable ASCII goes to the search box. Stored as typed and matched
     * case-insensitively (ci_has), rather than lowercased on the way in the way
     * the emoji picker does it: this one DRAWS the query back at you in the
     * search field, and a box that turned your capitals into lower case as you
     * typed reads as a broken text field. */
    if (sym >= 0x20 && sym <= 0x7e) {
        if (g->search_len < (int)sizeof(g->search) - 1) {
            g->search[g->search_len++] = (char)sym;
            g->search[g->search_len]   = '\0';
            g->selected = 0;
            appgrid_refilter(s);
            synui_render_appgrid(s);
        }
        return 1;
    }

    /* Modal: swallow the rest rather than letting it reach the window under. */
    return 1;
}

/* ── Pointer ─────────────────────────────────────────────── */

/* The tile under the pointer as an index into filt[], or -1. hit_index_at()
 * answers in grid cells and hit_set_first() has been told which entry the
 * top-left cell holds, so this is already page-relative. */
static int appgrid_index_at(syn_server_t *s, double lx, double ly)
{
    syn_appgrid_t *g = &s->appgrid;
    int i = hit_index_at(&g->hit, lx, ly);
    if (i < 0 || i >= g->filt_count) return -1;
    return i;
}

int appgrid_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->appgrid.visible) return 0;

    int i = appgrid_index_at(s, lx, ly);
    /* Hover moves the selection, as it does on every other panel — but only ONTO
     * a tile. Sliding the pointer across the gap between two tiles must not
     * clear the selection, or the keyboard's idea of where it is jumps about
     * whenever the mouse is nudged. */
    if (i >= 0 && i != s->appgrid.selected) {
        s->appgrid.selected = i;
        synui_render_appgrid(s);
    }
    return 1;
}

/*
 * ⚠ THE FOURTH ARGUMENT IS `time_msec`, NOT A BUTTON STATE.
 *
 * SYN_PANEL_LIST's click macro is `fn##_click(s, lx, ly, button, time_msec)`
 * and it is only ever reached from the PRESSED branch of pointer_button(), so
 * every panel on the roster takes a timestamp there and none of them has a
 * state to test. This one was declared `uint32_t state` and opened by returning
 * early unless it equalled WL_POINTER_BUTTON_STATE_PRESSED — which is 1, and a
 * millisecond timestamp is never 1. Both are uint32_t, so nothing warned: the
 * grid swallowed every click (returning 1 keeps it off the window underneath)
 * and acted on none of them, which is exactly "the mouse does nothing in the
 * application page". Hover worked the whole time, because _motion's signature
 * happened to match.
 */
int appgrid_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;
    if (!s->appgrid.visible) return 0;
    if (button != BTN_LEFT) return 1;

    int i = appgrid_index_at(s, lx, ly);
    if (i >= 0) {
        s->appgrid.selected = i;
        appgrid_launch(s, appgrid_at(s, i));
        return 1;
    }

    /* A click on the page but not on a tile closes it. The grid is full-screen,
     * so "off the panel" is not reachable — the background IS the panel, and
     * clicking the empty space around the tiles is the only click-off there
     * can be. */
    appgrid_hide(s);
    return 1;
}

int appgrid_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->appgrid.visible) return 0;

    syn_appgrid_t *g = &s->appgrid;
    int pages = (g->filt_count + APPGRID_PER_PAGE - 1) / APPGRID_PER_PAGE;
    if (pages <= 1) return 1;

    int want = g->page + (delta > 0 ? 1 : -1);
    if (want < 0) want = 0;
    if (want > pages - 1) want = pages - 1;
    if (want == g->page) return 1;

    g->page = want;
    /* The selection follows the page rather than staying behind on a tile that
     * is no longer drawn: it is what Enter acts on, and a selection off screen
     * makes Enter launch something the user cannot see. */
    g->selected = want * APPGRID_PER_PAGE;
    if (g->selected >= g->filt_count) g->selected = g->filt_count - 1;
    synui_render_appgrid(s);
    return 1;
}
