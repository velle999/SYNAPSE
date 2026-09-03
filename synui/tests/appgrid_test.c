/*
 * appgrid_test — the application grid's scan, its filters and its keys
 *
 * src/appgrid.c decides what a "show all applications" page lists, and every
 * one of those decisions fails QUIETLY: an entry that should be there is simply
 * absent, and an entry that should not be there just sits among the real
 * applications. Neither errors, neither logs, and neither is visible without
 * counting a page of icons against what is installed.
 *
 * So the scan is driven against a SANDBOX tree — XDG_DATA_DIRS pointed at a
 * mkdtemp — with one .desktop file per rule, named for the rule it exercises.
 * Nothing here reads or writes the live desktop's applications.
 *
 * What is worth pinning, in the order it bit:
 *
 *   1. THE DESKTOP HAS TWO NAMES. `OnlyShowIn=SynapseOS` and `OnlyShowIn=synui`
 *      both mean us, and an entry naming either has to survive. Testing one
 *      spelling is how an application goes missing on a desktop that looks
 *      correct in every other way.
 *
 *   2. The Wine noise rules are SCOPED to Wine. "Help Viewer" outside a prefix
 *      is a real application; the same words inside one are a shortcut to a
 *      readme. A rule that forgot the scope would hide real programs, and the
 *      names it hides are exactly the ones nobody notices are gone.
 *
 *   3. `.nfo` must not match `nfoview`. The QML rules spell this `\b`; the C
 *      port has to spell it out, and getting it wrong hides a real application
 *      for having the letters in its Exec line.
 *
 *   4. The entry id is the path under applications/ with '/' folded to '-'.
 *      menu-hidden.conf lists ids and the Wine rules test `wine-`, so an id
 *      built any other way silently turns both filters off.
 *
 *   5. Field codes come out of Exec. `%U` left in is a literal argument handed
 *      to the program.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* ── The compositor, stubbed ─────────────────────────────── */

static int  renders;
static char last_spawn[512];
static int  spawn_count;

void synui_render_appgrid(syn_server_t *s) { (void)s; renders++; }

void synui_spawn(const char *cmd)
{
    spawn_count++;
    snprintf(last_spawn, sizeof(last_spawn), "%s", cmd ? cmd : "");
}

/* No icon theme in a sandbox, and none wanted: what this file tests is which
 * entries exist and in what order, and a decode would make every case depend on
 * whatever icons happen to be installed on the machine running it. */
cairo_surface_t *icon_decode_named(const char *name) { (void)name; return NULL; }

static char scratch[256];      /* the sandbox root */
static char cfg_dir[320];      /* its XDG_CONFIG_HOME/synui */

bool syn_config_path(char *buf, size_t n, const char *leaf)
{
    snprintf(buf, n, "%s/%s", cfg_dir, leaf);
    return true;
}
void syn_config_ensure_dir(void) { mkdir(cfg_dir, 0700); }

/* ── The sandbox ─────────────────────────────────────────── */

static void mkdirs(const char *path)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(buf, 0700);
        *p = '/';
    }
    mkdir(buf, 0700);
}

/* Write one .desktop file at `rel` under the sandbox's applications/ root. */
static void app_file(const char *rel, const char *body)
{
    char path[640];
    snprintf(path, sizeof(path), "%s/share/applications/%s", scratch, rel);

    char dir[640];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdirs(dir); }

    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fputs(body, f);
    fclose(f);
}

static void rm_rf(const char *path)
{
    DIR *d = opendir(path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char child[640];
            snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
            rm_rf(child);
        }
        closedir(d);
    }
    if (rmdir(path) != 0) unlink(path);
}

/* ── Helpers ─────────────────────────────────────────────── */

static syn_server_t *server(void)
{
    static syn_server_t s;
    return &s;
}

/* Is `id` in the scan at all — before any search narrows it. */
static bool scanned(syn_server_t *s, const char *id)
{
    for (int i = 0; i < s->appgrid.count; i++)
        if (!strcmp(s->appgrid.apps[i].id, id)) return true;
    return false;
}

static syn_app_entry_t *entry(syn_server_t *s, const char *id)
{
    for (int i = 0; i < s->appgrid.count; i++)
        if (!strcmp(s->appgrid.apps[i].id, id)) return &s->appgrid.apps[i];
    return NULL;
}

static void type(syn_server_t *s, const char *text)
{
    for (const char *p = text; *p; p++)
        appgrid_key(s, (xkb_keysym_t)*p, 0);
}

/* ── Cases ───────────────────────────────────────────────── */

static void build_tree(void)
{
    app_file("normal.desktop",
        "[Desktop Entry]\nType=Application\nName=Normal App\nExec=normal\n");

    /* Not applications, each in its own way. */
    app_file("nodisplay.desktop",
        "[Desktop Entry]\nType=Application\nName=Hidden By NoDisplay\n"
        "Exec=nd\nNoDisplay=true\n");
    app_file("hiddenkey.desktop",
        "[Desktop Entry]\nType=Application\nName=Hidden By Hidden\n"
        "Exec=hk\nHidden=true\n");
    app_file("alink.desktop",
        "[Desktop Entry]\nType=Link\nName=A Link\nURL=http://example.invalid\n");
    app_file("noexec.desktop",
        "[Desktop Entry]\nType=Application\nName=No Exec At All\n");

    /* ⚠ THE TWO DESKTOP NAMES. Both mean us; neither may be dropped. */
    app_file("onlyshow-synapseos.desktop",
        "[Desktop Entry]\nType=Application\nName=Only SynapseOS\nExec=oss\n"
        "OnlyShowIn=SynapseOS;\n");
    app_file("onlyshow-synui.desktop",
        "[Desktop Entry]\nType=Application\nName=Only Synui\nExec=osu\n"
        "OnlyShowIn=synui;\n");
    app_file("onlyshow-gnome.desktop",
        "[Desktop Entry]\nType=Application\nName=Only Gnome\nExec=og\n"
        "OnlyShowIn=GNOME;\n");
    app_file("notshow-us.desktop",
        "[Desktop Entry]\nType=Application\nName=Not For Us\nExec=nfu\n"
        "NotShowIn=KDE;synui;\n");

    /* A prefix match must NOT count as a list entry: `synuix` is not `synui`. */
    app_file("onlyshow-prefix.desktop",
        "[Desktop Entry]\nType=Application\nName=Only Synuix\nExec=osx\n"
        "OnlyShowIn=synuix;\n");

    app_file("tryexec-missing.desktop",
        "[Desktop Entry]\nType=Application\nName=Try Exec Missing\nExec=tem\n"
        "TryExec=/nonexistent/definitely/not/here\n");
    app_file("tryexec-present.desktop",
        "[Desktop Entry]\nType=Application\nName=Try Exec Present\nExec=tep\n"
        "TryExec=/bin/sh\n");

    app_file("fieldcodes.desktop",
        "[Desktop Entry]\nType=Application\nName=Field Codes\n"
        "Exec=runme %U --flag %i %%literal\n");

    app_file("terminalapp.desktop",
        "[Desktop Entry]\nType=Application\nName=Terminal App\nExec=htop\n"
        "Terminal=true\n");

    /* A localised Name must not beat the plain one. */
    app_file("localised.desktop",
        "[Desktop Entry]\nType=Application\nName=Plain Name\n"
        "Name[de]=Deutscher Name\nExec=loc\n");

    /* The universal document rules — these apply outside Wine too. */
    app_file("winebrowser.desktop",
        "[Desktop Entry]\nType=Application\nName=Some Web Link\n"
        "Exec=winebrowser http://example.invalid\n");
    app_file("opensdoc.desktop",
        "[Desktop Entry]\nType=Application\nName=Read The Manual\n"
        "Exec=xdg-open /opt/thing/manual.pdf\n");
    /* …and the boundary they must not cross. */
    app_file("nfoview.desktop",
        "[Desktop Entry]\nType=Application\nName=NFO Viewer\nExec=nfoview\n");

    /* Wine-scoped noise, at the depth Wine actually writes it. */
    app_file("wine/Programs/Great Game.desktop",
        "[Desktop Entry]\nType=Application\nName=Great Game\nExec=wine game.exe\n");
    app_file("wine/Programs/Uninstall Great Game.desktop",
        "[Desktop Entry]\nType=Application\nName=Uninstall Great Game\n"
        "Exec=wine unins000.exe\n");
    app_file("wine/Programs/Readme.desktop",
        "[Desktop Entry]\nType=Application\nName=Readme\nExec=wine notepad\n");
    app_file("wine/Programs/Visit Our Web Site.desktop",
        "[Desktop Entry]\nType=Application\nName=Visit Our Web Site\n"
        "Exec=wine start\n");
    app_file("wine/Programs/DirectX SDK/Tool.desktop",
        "[Desktop Entry]\nType=Application\nName=Some DX Tool\nExec=wine dx.exe\n");

    /* The SAME WORDS outside a prefix, which are real applications. */
    app_file("helpviewer.desktop",
        "[Desktop Entry]\nType=Application\nName=Help Viewer\nExec=helpview\n");
    app_file("documentation.desktop",
        "[Desktop Entry]\nType=Application\nName=Qt Documentation\nExec=assistant\n");

    /* For menu-hidden.conf. */
    app_file("hidemebyid.desktop",
        "[Desktop Entry]\nType=Application\nName=Hide Me By Id\nExec=hmbi\n");
    app_file("unhidemebyid.desktop",
        "[Desktop Entry]\nType=Application\nName=Unhide Me By Id\nExec=uhmbi\n");
}

static void test_scan(void)
{
    syn_server_t *s = server();
    appgrid_rescan(s);

    CHECK(scanned(s, "normal"), "a plain application should be listed");

    CHECK(!scanned(s, "nodisplay"), "NoDisplay=true must not be listed");
    CHECK(!scanned(s, "hiddenkey"), "Hidden=true must not be listed");
    CHECK(!scanned(s, "alink"),     "Type=Link is not an application");
    CHECK(!scanned(s, "noexec"),    "an entry with no Exec cannot be launched");

    /* ⚠ Both spellings of this desktop's name. */
    CHECK(scanned(s, "onlyshow-synapseos"),
          "OnlyShowIn=SynapseOS names this desktop and must be listed");
    CHECK(scanned(s, "onlyshow-synui"),
          "OnlyShowIn=synui names this desktop and must be listed");
    CHECK(!scanned(s, "onlyshow-gnome"),
          "OnlyShowIn=GNOME does not name this desktop");
    CHECK(!scanned(s, "notshow-us"),
          "NotShowIn naming synui must exclude the entry");
    CHECK(!scanned(s, "onlyshow-prefix"),
          "`synuix` is not `synui` — the list match must be whole-token");

    CHECK(!scanned(s, "tryexec-missing"),
          "TryExec naming a missing absolute path must exclude the entry");
    CHECK(scanned(s, "tryexec-present"),
          "TryExec naming a real program must not");

    syn_app_entry_t *fc = entry(s, "fieldcodes");
    CHECK(fc != NULL, "the field-code entry should be listed");
    if (fc)
        CHECK(!strcmp(fc->exec, "runme --flag %literal"),
              "field codes must come out of Exec and %%%% survive as one "
              "(got '%s')", fc->exec);

    syn_app_entry_t *loc = entry(s, "localised");
    CHECK(loc && !strcmp(loc->name, "Plain Name"),
          "a localised Name must not beat the plain one (got '%s')",
          loc ? loc->name : "(absent)");

    syn_app_entry_t *tt = entry(s, "terminalapp");
    CHECK(tt && tt->terminal, "Terminal=true must be carried through");
}

static void test_noise(void)
{
    syn_server_t *s = server();

    CHECK(!scanned(s, "winebrowser"),
          "an Exec that runs winebrowser opens a URL, not a program");
    CHECK(!scanned(s, "opensdoc"),
          "an Exec that opens a .pdf is a document shortcut");
    /* ⚠ The boundary the QML spells `\\b`. */
    CHECK(scanned(s, "nfoview"),
          "`.nfo` must not match `nfoview` — that is a real application");

    CHECK(scanned(s, "wine-Programs-Great Game"),
          "the game itself is the entry the user wants");
    CHECK(!scanned(s, "wine-Programs-Uninstall Great Game"),
          "an uninstaller one row from the game is a dangerous list entry");
    CHECK(!scanned(s, "wine-Programs-Readme"), "a readme is not an application");
    CHECK(!scanned(s, "wine-Programs-Visit Our Web Site"),
          "a shortcut to a web page is not an application");
    CHECK(!scanned(s, "wine-Programs-DirectX SDK-Tool"),
          "SDK tooling inside a prefix is developer tooling for a game");

    /* ⚠ SCOPE. The same words outside a Wine prefix are real programs. */
    CHECK(scanned(s, "helpviewer"),
          "\"Help Viewer\" outside a Wine prefix is a real application");
    CHECK(scanned(s, "documentation"),
          "Qt's documentation browser is a real application");
}

static void test_hidden_conf(void)
{
    syn_server_t *s = server();

    char path[400];
    snprintf(path, sizeof(path), "%s/menu-hidden.conf", cfg_dir);
    FILE *f = fopen(path, "w");
    CHECK(f != NULL, "should be able to write the sandbox menu-hidden.conf");
    if (!f) return;
    fputs("# a comment\n"
          "hidemebyid\n"
          "unhidemebyid\n"
          "!unhidemebyid\n", f);
    fclose(f);

    appgrid_rescan(s);
    CHECK(!scanned(s, "hidemebyid"),
          "an id named in menu-hidden.conf must not be listed");
    CHECK(scanned(s, "unhidemebyid"),
          "a later `!id` must UNDO the hide rather than sit beside it");

    unlink(path);
    appgrid_rescan(s);
    CHECK(scanned(s, "hidemebyid"),
          "removing the file must put the entry back");
}

static void test_order(void)
{
    syn_server_t *s = server();
    appgrid_rescan(s);

    /* Alphabetical, case-insensitively — the whole point of a grid you can
     * learn the shape of is that a tile stays where it was. */
    for (int i = 1; i < s->appgrid.count; i++) {
        int c = strcasecmp(s->appgrid.apps[i - 1].name, s->appgrid.apps[i].name);
        CHECK(c <= 0, "entries must be sorted by name ('%s' before '%s')",
              s->appgrid.apps[i - 1].name, s->appgrid.apps[i].name);
        if (c > 0) break;
    }
}

static void test_search_and_keys(void)
{
    syn_server_t *s = server();
    appgrid_show(s);

    int all = appgrid_total(s);
    CHECK(all > 0, "the grid should open with entries");
    CHECK(s->appgrid.search_len == 0,
          "it opens CLEAN — a picker holding last time's query is one you have "
          "to clear before you can use it");

    type(s, "great");
    CHECK(appgrid_total(s) == 1, "\"great\" should narrow to the one game (got %d)",
          appgrid_total(s));
    syn_app_entry_t *hit = appgrid_at(s, 0);
    CHECK(hit && !strcmp(hit->name, "Great Game"),
          "…and it should be the game");

    /* Case-insensitive, and against the NAME rather than the Exec line. */
    appgrid_key(s, XKB_KEY_Escape, 0);
    CHECK(s->appgrid.search_len == 0, "Esc clears the search first");
    CHECK(s->appgrid.visible, "…and does NOT close on that same press");
    type(s, "GREAT");
    CHECK(appgrid_total(s) == 1, "search is case-insensitive (got %d)",
          appgrid_total(s));

    appgrid_key(s, XKB_KEY_BackSpace, 0);
    CHECK(s->appgrid.search_len == 4, "backspace should shorten the query");

    /* Enter launches what is selected, and closes FIRST — a window that maps
     * while the grid is up arrives behind it and without focus. */
    spawn_count = 0;
    s->appgrid.search[0] = '\0';
    s->appgrid.search_len = 0;
    type(s, "great");
    appgrid_key(s, XKB_KEY_Return, 0);
    CHECK(!s->appgrid.visible, "Enter must close the grid");
    CHECK(spawn_count == 1, "…and launch exactly once (got %d)", spawn_count);
    CHECK(!strcmp(last_spawn, "wine game.exe"),
          "…the entry's own Exec (got '%s')", last_spawn);

    /* A Terminal=true entry is handed to the terminal instead. */
    appgrid_show(s);
    snprintf(s->config.terminal, sizeof(s->config.terminal), "%s", "footerm");
    spawn_count = 0;
    type(s, "terminal app");
    CHECK(appgrid_total(s) == 1, "\"terminal app\" should narrow to one (got %d)",
          appgrid_total(s));
    appgrid_key(s, XKB_KEY_Return, 0);
    CHECK(!strcmp(last_spawn, "footerm -e htop"),
          "a Terminal=true entry runs through the desktop's terminal (got '%s')",
          last_spawn);

    /* Esc on an empty query closes. */
    appgrid_show(s);
    appgrid_key(s, XKB_KEY_Escape, 0);
    CHECK(!s->appgrid.visible, "Esc on an empty query closes the grid");

    /*
     * ── The POINTER ──
     *
     * appgrid_click() is reached through SYN_PANEL_LIST's macro, which is
     * `fn##_click(s, lx, ly, button, time_msec)` — a TIMESTAMP in the fourth
     * slot, like every other panel on the roster. It was declared `uint32_t
     * state` and opened by returning early unless that equalled
     * WL_POINTER_BUTTON_STATE_PRESSED (1), so every real click — whose
     * timestamp is a millisecond counter and never 1 — was swallowed and
     * discarded. Both types are uint32_t, so nothing warned and the grid simply
     * did not respond to the mouse.
     *
     * Driven here with a plausible timestamp for exactly that reason: pass 1 and
     * this test passes against the bug it exists to catch.
     */
    {
        const uint32_t T = 51234567;   /* a real-looking time_msec, NOT 1 */

        appgrid_show(s);
        s->appgrid.search[0] = '\0';
        s->appgrid.search_len = 0;
        appgrid_rescan(s);

        /* The hit grid the renderer would have written: one 100x100 cell per
         * tile, laid out the way synui_render_appgrid() lays them out. */
        hit_clear(&s->appgrid.hit);
        hit_set_panel(&s->appgrid.hit, 0, 0, 1920, 1080);
        hit_set_grid(&s->appgrid.hit, 100, 100, 100, 100, 6, 4);
        hit_set_first(&s->appgrid.hit, 0);

        CHECK(appgrid_motion(s, 150, 150) == 1,
              "the grid takes pointer motion while it is up");
        CHECK(s->appgrid.selected == 0,
              "hovering the first tile selects it (got %d)", s->appgrid.selected);
        CHECK(appgrid_motion(s, 250, 150) == 1, "…and motion onto the next");
        CHECK(s->appgrid.selected == 1,
              "hovering the second tile selects it (got %d)", s->appgrid.selected);

        spawn_count = 0;
        CHECK(appgrid_click(s, 150, 150, BTN_LEFT, T) == 1,
              "a left click on a tile is taken by the grid");
        CHECK(spawn_count == 1,
              "…and LAUNCHES it (got %d spawns)", spawn_count);
        CHECK(!s->appgrid.visible, "…and closes the grid, as Enter does");

        /* A click on the page but off every tile is the only click-off a
         * full-screen panel has, and it closes. */
        appgrid_show(s);
        spawn_count = 0;
        CHECK(appgrid_click(s, 10, 10, BTN_LEFT, T) == 1,
              "a click off the tiles is taken too");
        CHECK(spawn_count == 0, "…without launching anything");
        CHECK(!s->appgrid.visible, "…and closes the grid");

        /* Closed, it must let the pointer through to the window underneath —
         * the panel roster walks every entry on every click. */
        CHECK(appgrid_click(s, 150, 150, BTN_LEFT, T) == 0,
              "a closed grid does not swallow clicks");
        CHECK(appgrid_motion(s, 150, 150) == 0,
              "…nor motion");
    }

    /* Super and Ctrl still belong to the compositor: a page that swallowed
     * Super+C would trap you in it. */
    appgrid_show(s);
    CHECK(appgrid_key(s, XKB_KEY_c, WLR_MODIFIER_LOGO) == 0,
          "Super+key must pass through to the compositor");
    CHECK(appgrid_key(s, XKB_KEY_c, WLR_MODIFIER_CTRL) == 0,
          "Ctrl+key must pass through too");
    appgrid_hide(s);
}

static void test_paging(void)
{
    syn_server_t *s = server();

    /* Enough entries to need more than one page, named so the order is known. */
    for (int i = 0; i < APPGRID_PER_PAGE + 5; i++) {
        char rel[64], body[256];
        snprintf(rel, sizeof(rel), "page%02d.desktop", i);
        snprintf(body, sizeof(body),
                 "[Desktop Entry]\nType=Application\nName=Page Item %02d\n"
                 "Exec=page%02d\n", i, i);
        app_file(rel, body);
    }

    appgrid_rescan(s);
    appgrid_show(s);
    type(s, "page item");

    int total = appgrid_total(s);
    CHECK(total == APPGRID_PER_PAGE + 5,
          "every page entry should match (got %d)", total);
    CHECK(s->appgrid.page == 0, "it opens on the first page");

    appgrid_key(s, XKB_KEY_Page_Down, 0);
    CHECK(s->appgrid.page == 1, "Page Down moves one PAGE (got %d)",
          s->appgrid.page);
    CHECK(s->appgrid.selected == APPGRID_PER_PAGE,
          "…and takes the selection with it (got %d)", s->appgrid.selected);

    appgrid_key(s, XKB_KEY_Page_Down, 0);
    CHECK(s->appgrid.selected == total - 1,
          "Page Down past the end clamps to the last entry (got %d)",
          s->appgrid.selected);

    appgrid_key(s, XKB_KEY_Home, 0);
    CHECK(s->appgrid.selected == 0 && s->appgrid.page == 0,
          "Home returns to the first tile on the first page");

    appgrid_key(s, XKB_KEY_End, 0);
    CHECK(s->appgrid.selected == total - 1, "End goes to the last");
    CHECK(s->appgrid.page == (total - 1) / APPGRID_PER_PAGE,
          "…and the page follows the selection (got %d)", s->appgrid.page);

    /* Down moves a ROW, and must not walk off the end. */
    appgrid_key(s, XKB_KEY_Down, 0);
    CHECK(s->appgrid.selected == total - 1,
          "Down at the end must not move past it (got %d)", s->appgrid.selected);

    /* The wheel turns pages, and the selection follows so Enter can never act
     * on a tile that is not on screen. */
    s->appgrid.page = 0;
    s->appgrid.selected = 0;
    appgrid_scroll(s, 0, 0, 1.0);
    CHECK(s->appgrid.page == 1, "the wheel turns the page (got %d)",
          s->appgrid.page);
    CHECK(s->appgrid.selected >= APPGRID_PER_PAGE,
          "…and the selection lands on the page being shown (got %d)",
          s->appgrid.selected);

    appgrid_hide(s);
}

/* ── main ────────────────────────────────────────────────── */

/* ── The right-click Uninstall menu ──────────────────────── */

/*
 * The menu is one row and that row removes software, so what is worth pinning
 * is not that it draws — it is everything that must NOT happen around it.
 *
 * ⚠ THE HIT RECTS BELONG TO THE RENDERER, which is stubbed out in this file —
 * so both tables are seeded here to stand in for it. That is honest about what
 * is being tested and what is not: the geometry below is INVENTED, so nothing
 * here can catch a menu drawn somewhere other than where it is clickable. That
 * is bar_module_menu.sh's kind of question and it needs a compositor. What this
 * file can prove is the behaviour around the rows — that right does not launch,
 * that the menu is modal to the keyboard and the pointer, what it closes on,
 * and, through the spawn stub, exactly what the row would run.
 */
static void test_uninstall_menu(void)
{
    syn_server_t *s = server();
    appgrid_show(s);
    CHECK(s->appgrid.filt_count > 0, "the sandbox grid is empty");

    /* Standing in for synui_render_appgrid(): one 100x100 tile at the origin,
     * and one menu row beside it at x=200 so a click can be aimed at either
     * without ambiguity. */
    hit_set_panel(&s->appgrid.hit, 0, 0, 1920, 1080);
    hit_set_grid(&s->appgrid.hit, 0, 0, 100, 100, 4, 4);
    hit_set_first(&s->appgrid.hit, 0);

    /* The entry the menu will be about, and the path it must carry. The id
     * folds '/' to '-' and cannot be inverted, so an entry without a path has
     * nothing exact to hand `synpkg remove --owner`. */
    syn_app_entry_t *e = &s->appgrid.apps[s->appgrid.filt[0]];
    CHECK(e->path[0] == '/', "the entry kept no .desktop path (%s)", e->path);

    /* ── it opens on RIGHT, and does not launch ── */
    spawn_count = 0;
    appgrid_click(s, 10, 10, BTN_RIGHT, 0);
    CHECK(s->appgrid.menu_open, "right-click did not open the menu");
    hit_add_spot(&s->appgrid.menu_hit, 200, 200, 260, APPGRID_MENU_ROW_H);
    CHECK(spawn_count == 0,
           "right-click LAUNCHED the application as well as opening its menu");
    CHECK(s->appgrid.menu_app == s->appgrid.filt[0],
           "the menu is about the wrong application");

    /* ⛔ MODAL FOR THE KEYBOARD. Left to fall through, typing would go on
     * editing the search box behind an open menu — refiltering the grid, and
     * with it moving the application the menu is about out from under it. */
    int before = s->appgrid.filt_count;
    type(s, "zzzz");
    CHECK(s->appgrid.search_len == 0,
           "typing reached the search box through an open menu");
    CHECK(s->appgrid.filt_count == before,
           "the grid refiltered under its own context menu");

    /* Escape closes the MENU and nothing else: the search and the page it was
     * opened on are still there. */
    appgrid_key(s, XKB_KEY_Escape, 0);
    CHECK(!s->appgrid.menu_open, "Escape did not close the menu");
    CHECK(s->appgrid.visible, "Escape closed the whole grid, not just the menu");

    /* ⛔ A CLICK THAT MISSES THE MENU MUST NOT REACH THE TILE UNDER IT. With
     * the menu up, falling through would launch an application from a menu
     * whose only row uninstalls one — the worst possible pair of outcomes to
     * get one pixel apart. */
    appgrid_click(s, 10, 10, BTN_RIGHT, 0);
    CHECK(s->appgrid.menu_open, "the menu did not reopen");
    hit_add_spot(&s->appgrid.menu_hit, 200, 200, 260, APPGRID_MENU_ROW_H);
    spawn_count = 0;
    /* (10,10) is ON A TILE and OFF the menu — exactly the click that must not
     * fall through. */
    appgrid_click(s, 10, 10, BTN_LEFT, 0);
    CHECK(spawn_count == 0, "a click past the menu launched the tile behind it");
    CHECK(!s->appgrid.menu_open, "a click past the menu did not close it");

    /* And the row itself, clicked where the renderer said it is. */
    appgrid_click(s, 10, 10, BTN_RIGHT, 0);
    hit_add_spot(&s->appgrid.menu_hit, 200, 200, 260, APPGRID_MENU_ROW_H);
    spawn_count = 0; last_spawn[0] = '\0';
    appgrid_click(s, 210, 210, BTN_LEFT, 0);
    CHECK(spawn_count == 1, "clicking the Uninstall row ran nothing");
    CHECK(strstr(last_spawn, "synpkg remove --owner") != NULL,
           "the clicked row does not run `synpkg remove --owner`: %s", last_spawn);

    /* ── what the row actually runs ── */
    appgrid_show(s);
    hit_set_panel(&s->appgrid.hit, 0, 0, 1920, 1080);
    hit_set_grid(&s->appgrid.hit, 0, 0, 100, 100, 4, 4);
    hit_set_first(&s->appgrid.hit, 0);
    appgrid_click(s, 10, 10, BTN_RIGHT, 0);
    hit_add_spot(&s->appgrid.menu_hit, 200, 200, 260, APPGRID_MENU_ROW_H);
    s->appgrid.menu_hover = 0;
    spawn_count = 0;
    last_spawn[0] = '\0';
    appgrid_key(s, XKB_KEY_Return, 0);
    CHECK(spawn_count == 1, "the Uninstall row ran nothing");
    CHECK(strstr(last_spawn, "synpkg remove --owner") != NULL,
           "the row does not run `synpkg remove --owner`: %s", last_spawn);
    /* ⛔ NEVER --noconfirm. synpkg's remove is -Rns: it takes the unneeded
     * dependencies and the config files with it. It has to print that list and
     * ask, and a front-end that silenced the question would uninstall an
     * application and everything it dragged out on one right-click and one
     * left click. */
    CHECK(strstr(last_spawn, "--noconfirm") == NULL,
           "the Uninstall row passes --noconfirm: %s", last_spawn);
    /* A terminal, and one that HOLDS: a refusal ("nothing owns this") has to be
     * read, not flashed. */
    CHECK(strstr(last_spawn, "syntty --hold") != NULL,
           "the row does not open a terminal that stays up: %s", last_spawn);
    CHECK(!s->appgrid.menu_open && !s->appgrid.visible,
           "the grid stayed up over the terminal it just opened");

    /* ⛔ THE PATH IS SHELL-QUOTED. synui_spawn runs /bin/sh -c, and a .desktop
     * file is trivially attacker-named — it survives a download or a USB stick.
     * The quoting is what stops `game'; curl … | sh; '.desktop` from executing
     * the moment somebody right-clicks it. */
    {
        char *q = strstr(last_spawn, "--owner ");
        CHECK(q != NULL && q[8] == '\'',
               "the .desktop path is not single-quoted: %s", last_spawn);
    }

    /* Reopening the grid must not bring a stale menu back with it. */
    appgrid_show(s);
    CHECK(!s->appgrid.menu_open,
           "the grid reopened with the previous menu still up");
}

int main(void)
{
    snprintf(scratch, sizeof(scratch), "%s", "/tmp/appgridtestXXXXXX");
    if (!mkdtemp(scratch)) { perror("mkdtemp"); return 1; }

    snprintf(cfg_dir, sizeof(cfg_dir), "%s/config", scratch);
    mkdirs(cfg_dir);

    char apps[512];
    snprintf(apps, sizeof(apps), "%s/share/applications", scratch);
    mkdirs(apps);

    /*
     * ⚠ XDG_DATA_HOME IS SET TOO, and to the sandbox — not left alone. Unset, the
     * scan falls back to $HOME/.local/share/applications, which on the machine
     * running this is the tester's real one: the counts would depend on what
     * they happen to have installed, and `test_order` would be asserting on
     * their desktop.
     */
    char data[512];
    snprintf(data, sizeof(data), "%s/share", scratch);
    setenv("XDG_DATA_HOME", data, 1);
    setenv("XDG_DATA_DIRS", data, 1);

    printf("appgrid: the scan, the filters and the keys\n");
    build_tree();
    test_scan();
    test_noise();
    test_hidden_conf();
    test_order();
    test_search_and_keys();
    test_paging();
    test_uninstall_menu();

    rm_rf(scratch);

    printf("appgrid_test: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
