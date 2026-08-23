/*
 * icons.c — .desktop lookup and app icon loading
 *
 * Resolves an app_id (as reported by view_app_id()/foreign-toplevel) to a
 * display name, a launch command, and an icon, for the dock (dock.c) and
 * anything else that wants to show an app by app_id.
 *
 * Icons are looked up as a rasterized PNG first (hicolor's fixed-size dirs,
 * then /usr/share/pixmaps), then as a scalable SVG rendered through librsvg.
 * The SVG path is not optional polish: KDE and GNOME apps increasingly ship
 * scalable/ only — org.kde.dolphin has no PNG on disk at any size — so
 * without it those apps fall back to icon_draw_monogram() forever.
 *
 * One thing remains deliberately out of scope, documented rather than worked
 * around: .desktop matching is a direct "<app_id>.desktop" basename lookup,
 * not a real StartupWMClass/heuristic match. This misses apps whose app_id
 * doesn't equal their .desktop file's basename — the same fuzzy problem real
 * desktop shells only partially solve.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <unistd.h>

#include <cairo.h>
#include <librsvg/rsvg.h>
#include <wlr/util/log.h>

#include "synui.h"
#include "iconhue.h"

#define ICON_CACHE_MAX 64

/* SVGs have no natural pixel size, so pick one. The dock scales whatever it
 * gets down to DOCK_ICON_SIZE (48) with CAIRO_FILTER_GOOD; rasterizing at 128
 * leaves headroom for larger consumers and downsamples cleanly. */
#define ICON_RASTER_SIZE 128

static syn_icon_entry_t icon_cache[ICON_CACHE_MAX];
static int icon_cache_count = 0;

/* Bumped whenever any cached icon_surface is replaced — a retint, or a new
 * entry decoded into the table. Consumers that keep their OWN derived copy of
 * an icon (the dock keeps one pre-scaled to its cell) compare this and throw
 * theirs away when it moves, so a theme change cannot leave a stale picture on
 * screen. Cheaper and far safer than comparing surface POINTERS: cairo is free
 * to hand a freed surface's address straight back for the next one. */
static unsigned icon_gen = 1;
unsigned icon_generation(void) { return icon_gen; }

/* Throw the whole table away — every entry was resolved against a theme that is
 * no longer the theme. Bumps icon_gen on the way out, which is what makes every
 * consumer holding a derived copy (the dock's pre-scaled cells) drop it. */
static void icon_cache_flush_all(void)
{
    for (int i = 0; i < icon_cache_count; i++) {
        if (icon_cache[i].icon_surface)
            cairo_surface_destroy(icon_cache[i].icon_surface);
        if (icon_cache[i].icon_base)
            cairo_surface_destroy(icon_cache[i].icon_base);
    }
    icon_cache_count = 0;
    icon_gen++;
}

/* The accent our own icons are currently drawn in. Initialised to SYNAPSE's
 * cyan so an icon resolved before any theme has been pushed looks the same as
 * one resolved after — a dock that themed its icons only after the first theme
 * switch would be a worse bug than not theming them at all. */
static float g_icon_accent[3] = { 0.00f, 0.85f, 0.75f };

/* ── PNG decode ───────────────────────────────────────────── */

/*
 * A PNG, rasterized down to ICON_RASTER_SIZE if the file is bigger than that.
 *
 * The cap is not cosmetic. The theme walk below prefers the LARGEST raster it
 * can find, because a 512px picture resampled into a 64px dock cell looks far
 * better than a 32px one blown up to fill it — but the application grid holds
 * up to APPGRID_MAX entries and OWNS every decode it asks for (icon_decode_named
 * hands the surface over and keeps no cache of its own). An uncapped preference
 * for the largest file is a megabyte of ARGB per tile, resident for as long as
 * the page has been opened once.
 *
 * ICON_RASTER_SIZE is already the size every SVG is rendered at and no consumer
 * draws an icon larger, so this costs nothing on screen and bounds an icon at
 * 64KB. Aspect ratio is kept: a few icons in the wild are not square, and
 * squashing them to a box is a worse picture than the one we were given.
 */
static cairo_surface_t *decode_png(const char *path)
{
    cairo_surface_t *surf = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return NULL;
    }

    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    if (w <= ICON_RASTER_SIZE && h <= ICON_RASTER_SIZE) return surf;

    int nw, nh;
    if (w >= h) { nw = ICON_RASTER_SIZE; nh = h * ICON_RASTER_SIZE / w; }
    else        { nh = ICON_RASTER_SIZE; nw = w * ICON_RASTER_SIZE / h; }
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;

    cairo_surface_t *small =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, nw, nh);
    if (cairo_surface_status(small) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(small);
        return surf;   /* not fatal: the oversized surface still draws */
    }

    cairo_t *cr = cairo_create(small);
    cairo_scale(cr, (double)nw / w, (double)nh / h);
    cairo_set_source_surface(cr, surf, 0, 0);
    /* GOOD rather than the default, for the same reason the dock and the grid
     * ask for it at paint time: a downsampled icon aliases badly enough to look
     * like the wrong picture. */
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(small);

    cairo_surface_destroy(surf);
    return small;
}

/* Render an SVG into a square ARGB32 surface. Same contract as decode_png:
 * NULL on any failure, caller owns the surface. A missing file is the common
 * case (callers probe several paths), so it is not logged. */
static cairo_surface_t *decode_svg(const char *path, int size)
{
    GError *err = NULL;
    RsvgHandle *h = rsvg_handle_new_from_file(path, &err);
    if (!h) {
        g_clear_error(&err);
        return NULL;
    }

    cairo_surface_t *surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        g_object_unref(h);
        return NULL;
    }

    cairo_t *cr = cairo_create(surf);
    RsvgRectangle viewport = { .x = 0, .y = 0,
                               .width = size, .height = size };
    gboolean ok = rsvg_handle_render_document(h, cr, &viewport, &err);
    cairo_destroy(cr);
    g_object_unref(h);

    if (!ok) {
        wlr_log(WLR_ERROR, "synui: icons: SVG render failed '%s': %s",
                path, err && err->message ? err->message : "unknown error");
        g_clear_error(&err);
        cairo_surface_destroy(surf);
        return NULL;
    }

    cairo_surface_flush(surf);
    return surf;
}

/* ── .desktop file location + parsing ────────────────────── */

/* Try "<dir>/applications/<app_id>.desktop" across XDG_DATA_DIRS (falling
 * back to the spec's default) plus ~/.local/share. Returns a heap path the
 * caller must free(), or NULL if no candidate exists. */
static char *find_desktop_file(const char *app_id)
{
    char candidates[8][256];
    int n = 0;

    const char *home = getenv("HOME");
    if (home && n < 8)
        snprintf(candidates[n++], sizeof(candidates[0]),
                 "%s/.local/share", home);

    const char *xdg_dirs = getenv("XDG_DATA_DIRS");
    if (!xdg_dirs || !*xdg_dirs)
        xdg_dirs = "/usr/local/share:/usr/share";
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", xdg_dirs);
    char *save = NULL;
    for (char *tok = strtok_r(buf, ":", &save); tok && n < 8;
         tok = strtok_r(NULL, ":", &save))
        snprintf(candidates[n++], sizeof(candidates[0]), "%s", tok);

    for (int i = 0; i < n; i++) {
        char path[320];
        snprintf(path, sizeof(path), "%s/applications/%s.desktop",
                 candidates[i], app_id);
        FILE *f = fopen(path, "r");
        if (f) {
            fclose(f);
            return strdup(path);
        }
    }
    return NULL;
}

/* Strip Exec= field codes (%f %F %u %U %d %D %n %N %i %c %k %v %m, %%) so the
 * remainder is a plain shell-spawnable command. */
static void strip_exec_field_codes(char *exec)
{
    char *out = exec;
    for (char *p = exec; *p; p++) {
        if (p[0] == '%' && p[1] != '\0') {
            p++;           /* skip the placeholder char too */
            continue;
        }
        *out++ = *p;
    }
    *out = '\0';

    /* Trim the trailing space(s) left behind by a removed trailing code. */
    size_t len = strlen(exec);
    while (len > 0 && isspace((unsigned char)exec[len - 1]))
        exec[--len] = '\0';
}

/* Parse Name=/Icon=/Exec= from the unlocalized main [Desktop Entry] section
 * (first match wins; [Desktop Action ...] sub-sections are skipped). */
static void parse_desktop_file(const char *path, syn_icon_entry_t *e)
{
    FILE *f = fopen(path, "r");
    if (!f) return;

    int in_main_section = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        char *end = s + strlen(s);
        while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
        if (!*s || *s == '#') continue;

        if (*s == '[') {
            in_main_section = strcmp(s, "[Desktop Entry]") == 0;
            continue;
        }
        if (!in_main_section) continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = s;
        const char *val = eq + 1;

        if (strcmp(key, "Name") == 0 && !e->display_name[0])
            snprintf(e->display_name, sizeof(e->display_name), "%s", val);
        else if (strcmp(key, "Icon") == 0 && !e->icon_hint[0])
            snprintf(e->icon_hint, sizeof(e->icon_hint), "%s", val);
        else if (strcmp(key, "Exec") == 0 && !e->exec[0])
            snprintf(e->exec, sizeof(e->exec), "%s", val);
    }
    fclose(f);

    if (e->exec[0])
        strip_exec_field_codes(e->exec);
}

/* ── Icon file lookup ─────────────────────────────────────── */

/*
 * The icon theme to look in first, pushed by config load — `bar_icon_theme` in
 * synuirc, which is the one place this desktop records which set of pictures it
 * is using (it is exported to quickshell as QS_ICON_THEME). Empty means "no
 * opinion", and the fallback list below stands.
 *
 * A file-static rather than a config read because icons.c deliberately knows
 * nothing about syn_server_t: it is called from the dock, the desktop icons,
 * the start menu and the app grid, and two of those run before there is a
 * server to ask.
 */
static char g_icon_theme[64];

void icon_set_theme(const char *name)
{
    char want[64] = {0};
    if (name) snprintf(want, sizeof(want), "%s", name);
    if (strcmp(want, g_icon_theme) == 0) return;

    snprintf(g_icon_theme, sizeof(g_icon_theme), "%s", want);
    /* Every cached decode was resolved against the OLD theme, so they are all
     * now the wrong pictures. Dropping the cache is the whole update: the next
     * lookup re-resolves, and icon_gen moving is what tells the dock to throw
     * away its own scaled copies. */
    icon_cache_flush_all();
}

/*
 * Decode whatever `path` is, if it is there at all.
 *
 * The access() is what makes the walk below affordable. It probes several
 * thousand candidate paths for a MISS, and handing each one straight to
 * rsvg_handle_new_from_file() builds a GFile and a parser per attempt; a stat
 * that fails costs a few microseconds and skips all of it.
 */
static cairo_surface_t *try_icon_file(const char *path)
{
    if (access(path, R_OK) != 0) return NULL;
    size_t len = strlen(path);
    if (len > 4 && strcasecmp(path + len - 4, ".svg") == 0)
        return decode_svg(path, ICON_RASTER_SIZE);
    return decode_png(path);
}

/*
 * ── Where icon themes live ──────────────────────────────────────────────────
 *
 * `~/.local/share/icons` and `/usr/share/icons` were hardcoded here, and that
 * is not where all of them are. XDG_DATA_DIRS is a LIST, and on this desktop it
 * carries `/var/lib/flatpak/exports/share` — which is the only place a flatpak's
 * icon exists. Every flatpak on the box therefore drew a letter monogram in the
 * dock and in the application grid, and nothing said why: a missing icon is not
 * an error, it is the fallback working.
 *
 * So the roots come from the environment, exactly the way appgrid.c already
 * derives the applications/ directories it scans — same list, same order, one
 * suffix different. `~/.icons` is added because it is the pre-XDG location and
 * hand-installed themes still land there.
 *
 * Built once: the environment does not change under a running compositor, and
 * icon_set_theme() flushes decoded surfaces rather than this.
 */
#define ICON_ROOTS_MAX 12
static char g_icon_roots[ICON_ROOTS_MAX][320];
static int  g_icon_roots_count = -1;

static void icon_roots_add(const char *dir)
{
    if (!dir || !*dir || g_icon_roots_count >= ICON_ROOTS_MAX) return;
    for (int i = 0; i < g_icon_roots_count; i++)
        if (strcmp(g_icon_roots[i], dir) == 0) return;   /* XDG lists repeat */
    snprintf(g_icon_roots[g_icon_roots_count++], sizeof(g_icon_roots[0]),
             "%s", dir);
}

static void icon_roots_build(void)
{
    if (g_icon_roots_count >= 0) return;
    g_icon_roots_count = 0;

    char dir[320];
    const char *home = getenv("HOME");
    const char *data_home = getenv("XDG_DATA_HOME");

    if (data_home && *data_home) {
        snprintf(dir, sizeof(dir), "%s/icons", data_home);
        icon_roots_add(dir);
    } else if (home && *home) {
        snprintf(dir, sizeof(dir), "%s/.local/share/icons", home);
        icon_roots_add(dir);
    }
    if (home && *home) {
        snprintf(dir, sizeof(dir), "%s/.icons", home);   /* the legacy one */
        icon_roots_add(dir);
    }

    const char *dirs = getenv("XDG_DATA_DIRS");
    if (!dirs || !*dirs) dirs = "/usr/local/share:/usr/share";
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", dirs);
    for (char *tok = strtok(buf, ":"); tok; tok = strtok(NULL, ":")) {
        if (!*tok) continue;
        snprintf(dir, sizeof(dir), "%s/icons", tok);
        icon_roots_add(dir);
    }
    /* Named outright as well as taken from the list: a session started without
     * XDG_DATA_DIRS set at all still has to find the system theme, and the
     * default this falls back to above is a spec quotation rather than a
     * promise about this box. icon_roots_add() de-duplicates. */
    icon_roots_add("/usr/share/icons");
}

static cairo_surface_t *find_and_decode_icon(const char *name)
{
    if (!name || !*name) return NULL;

    /* Icon= may already be an absolute path. */
    if (name[0] == '/') return try_icon_file(name);

    /*
     * ── The theme walk ──────────────────────────────────────────────────────
     *
     * This used to be hicolor/<size>/apps only, plus /usr/share/pixmaps, and
     * that was survivable while the dock was the only caller: a pinned
     * application almost always ships its own hicolor icon. The application
     * GRID broke it immediately — a third of a stock box's 90 entries drew a
     * letter monogram, because they name a THEME icon (`accessories-calculator`,
     * `preferences-system-bluetooth`, `printer`) which lives in Adwaita, under
     * `legacy/` or `devices/`, and in none of the four places this looked.
     *
     * So: every theme this desktop might be using, every root, every size, every
     * category subdirectory the spec allows. It is a lot of candidate paths —
     * but only for a MISS, only once per icon per session (both callers cache),
     * and try_icon_file() reduces each one to an access() that fails.
     *
     * ⚠ TWO DIRECTORY LAYOUTS, and the second is not an edge case. The spec's
     * shape is <theme>/<size>/<category>; breeze inverts it to
     * <theme>/<category>/<size>, and breeze is where most of SYNAPSE's own
     * panels resolve — `transform-crop`, `office-calendar`, `view-media-equalizer`,
     * `preferences-system-power-management`, `preferences-desktop-cursors`,
     * `message-news`, `viewimage`, `view-grid`. Only the `apps` category was
     * tried the inverted way round, so eight of this desktop's own entries drew
     * monograms in its own application grid. Both orders, every category.
     *
     * Ordered so the first hit is the best one: scalable before raster (an SVG
     * rasterizes to exactly the size asked for), large before small (decode_png
     * caps the result, so a big source is free), the configured theme before the
     * fallbacks, hicolor last because it is the per-application drop rather than
     * a designed set — an app that ships both usually wants its own, and gets
     * it, because `apps` is the first category tried at every size.
     */
    const char *themes[6];
    int nthemes = 0;
    /* Whatever the shell was told to use. The bar exports it as QS_ICON_THEME,
     * so naming it here is what keeps the compositor's own pictures and the
     * bar's from being drawn out of two different sets. */
    if (g_icon_theme[0]) themes[nthemes++] = g_icon_theme;
    themes[nthemes++] = "Adwaita";
    themes[nthemes++] = "AdwaitaLegacy";
    themes[nthemes++] = "Papirus";
    themes[nthemes++] = "breeze";
    themes[nthemes++] = "hicolor";

    icon_roots_build();

    /*
     * "scalable" is spelt among the sizes and handled by extension below.
     *
     * 512/256/192 are here because they are the ONLY size some applications
     * ship — waydroid, cliamp and the Blackmagic tools each have exactly one
     * PNG, at 512 or 256, and drew monograms for want of these three rows.
     *
     * ⚠ THE TWO LAYOUTS ALSO SPELL THE SIZE DIFFERENTLY, which is the reason
     * the inverted probe that was already here never once hit: it asked breeze
     * for `apps/48x48`, and breeze's directory is `apps/48`. A dead branch that
     * looks like coverage is worse than no branch, so the two spellings are
     * named side by side and neither can be changed without the other.
     */
    static const struct { const char *sized, *bare; } sizes[] = {
        { "scalable", "scalable" },
        { "512x512",  "512" }, { "256x256", "256" }, { "192x192", "192" },
        { "128x128",  "128" }, { "96x96",   "96"  }, { "64x64",   "64"  },
        { "48x48",    "48"  }, { "32x32",   "32"  }, { "24x24",   "24"  },
        { "22x22",    "22"  }, { "16x16",   "16"  },
    };
    /* `apps` is where an application icon belongs; the rest are where the
     * themes actually put the ones applications ask for. `legacy` is Adwaita's,
     * and `preferences`/`actions` are breeze's — between them they hold most of
     * what SYNAPSE's own .desktop files name. */
    static const char *const cats[] = {
        "apps", "legacy", "preferences", "actions", "devices", "categories",
        "status", "places", "applets", "mimetypes",
    };

    char path[512];
    for (int t = 0; t < nthemes; t++) {
        for (int r = 0; r < g_icon_roots_count; r++) {
            for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
                bool scalable = strcmp(sizes[z].sized, "scalable") == 0;
                for (size_t c = 0; c < sizeof(cats) / sizeof(cats[0]); c++) {
                    cairo_surface_t *s;

                    /* <theme>/<size>/<category>/ — the spec's layout. */
                    if (!scalable) {
                        snprintf(path, sizeof(path), "%s/%s/%s/%s/%s.png",
                                 g_icon_roots[r], themes[t], sizes[z].sized,
                                 cats[c], name);
                        s = try_icon_file(path);
                        if (s) return s;
                    }
                    snprintf(path, sizeof(path), "%s/%s/%s/%s/%s.svg",
                             g_icon_roots[r], themes[t], sizes[z].sized,
                             cats[c], name);
                    s = try_icon_file(path);
                    if (s) return s;

                    /* <theme>/<category>/<size>/ — breeze's, the other way up
                     * and with the size spelt as a bare number. */
                    snprintf(path, sizeof(path), "%s/%s/%s/%s/%s.svg",
                             g_icon_roots[r], themes[t], cats[c],
                             sizes[z].bare, name);
                    s = try_icon_file(path);
                    if (s) return s;
                    if (!scalable) {
                        snprintf(path, sizeof(path), "%s/%s/%s/%s/%s.png",
                                 g_icon_roots[r], themes[t], cats[c],
                                 sizes[z].bare, name);
                        s = try_icon_file(path);
                        if (s) return s;
                    }
                }
            }
        }
    }

    /* The legacy drop, which belongs to no theme at all. */
    snprintf(path, sizeof(path), "/usr/share/pixmaps/%s.png", name);
    cairo_surface_t *s = try_icon_file(path);
    if (s) return s;
    snprintf(path, sizeof(path), "/usr/share/pixmaps/%s.svg", name);
    return try_icon_file(path);
}

/* ── Theme tinting (iconhue.c) ───────────────────────────── */

/* Paint e->icon_surface from e->icon_base at the current accent. Cheap enough
 * to run on every theme switch: the icons are 128x128 and there are a dozen. */
static void icon_tint_from_base(syn_icon_entry_t *e)
{
    if (!e->icon_base || !e->icon_surface) return;

    int w = cairo_image_surface_get_width(e->icon_base);
    int h = cairo_image_surface_get_height(e->icon_base);
    int src_stride = cairo_image_surface_get_stride(e->icon_base);
    int dst_stride = cairo_image_surface_get_stride(e->icon_surface);

    cairo_surface_flush(e->icon_base);
    cairo_surface_flush(e->icon_surface);
    const unsigned char *src = cairo_image_surface_get_data(e->icon_base);
    unsigned char *dst = cairo_image_surface_get_data(e->icon_surface);
    if (!src || !dst) return;

    /* Start from the pristine decode every time — see icon_base's note. */
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * dst_stride, src + (size_t)y * src_stride,
               (size_t)w * 4);

    syn_iconhue_apply(dst, w, h, dst_stride, g_icon_accent);
    cairo_surface_mark_dirty(e->icon_surface);
}

/* If this icon is one of ours, split it into a pristine base plus a tinted
 * surface to draw. Anything else — every third-party app — is left exactly as
 * decoded, with icon_base NULL. */
static void icon_theme_adopt(syn_icon_entry_t *e, const char *icon_name)
{
    if (!e->icon_surface || e->icon_base) return;
    if (cairo_image_surface_get_format(e->icon_surface) != CAIRO_FORMAT_ARGB32)
        return;

    int w = cairo_image_surface_get_width(e->icon_surface);
    int h = cairo_image_surface_get_height(e->icon_surface);
    int stride = cairo_image_surface_get_stride(e->icon_surface);
    cairo_surface_flush(e->icon_surface);

    if (!syn_iconhue_wants(icon_name, cairo_image_surface_get_data(e->icon_surface),
                           w, h, stride))
        return;

    cairo_surface_t *tinted =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(tinted) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(tinted);
        return;   /* not fatal: the icon simply stays the colour it was drawn */
    }

    /* The decode becomes the base; the fresh surface becomes what is drawn. */
    e->icon_base = e->icon_surface;
    e->icon_surface = tinted;
    icon_tint_from_base(e);
}

void icon_set_accent(const float rgb[3])
{
    if (!rgb) return;
    if (rgb[0] == g_icon_accent[0] && rgb[1] == g_icon_accent[1] &&
        rgb[2] == g_icon_accent[2])
        return;                      /* same colour: nothing to repaint */

    g_icon_accent[0] = rgb[0];
    g_icon_accent[1] = rgb[1];
    g_icon_accent[2] = rgb[2];

    for (int i = 0; i < icon_cache_count; i++)
        icon_tint_from_base(&icon_cache[i]);
    icon_gen++;
}

/* ── Public API ──────────────────────────────────────────── */

const syn_icon_entry_t *icon_lookup(const char *app_id)
{
    for (int i = 0; i < icon_cache_count; i++)
        if (strcmp(icon_cache[i].app_id, app_id) == 0)
            return &icon_cache[i];

    syn_icon_entry_t e = {0};
    snprintf(e.app_id, sizeof(e.app_id), "%s", app_id);

    char *desktop_path = find_desktop_file(app_id);
    if (desktop_path) {
        parse_desktop_file(desktop_path, &e);
        free(desktop_path);
    }

    /* Unresolved fields fall back to the app_id itself — a bare binary name
     * is a valid spawn()able command and a reasonable display label. */
    if (!e.display_name[0])
        snprintf(e.display_name, sizeof(e.display_name), "%s", app_id);
    if (!e.exec[0])
        snprintf(e.exec, sizeof(e.exec), "%s", app_id);

    e.icon_surface = find_and_decode_icon(e.icon_hint[0] ? e.icon_hint : app_id);
    if (!e.icon_surface && e.icon_hint[0])
        /* Icon= pointed nowhere useful (SVG-only theme, etc) — try the bare
         * app_id too, since it's occasionally also the icon name. */
        e.icon_surface = find_and_decode_icon(app_id);

    /* Either name can be the one that identifies it as ours: Icon= is what
     * resolved the file, but an app whose .desktop names a generic icon is
     * still ours by app_id. adopt() is guarded, so the second call is a no-op
     * once the first has taken. */
    if (e.icon_hint[0]) icon_theme_adopt(&e, e.icon_hint);
    icon_theme_adopt(&e, app_id);

    /* Realistic pinned+running counts never approach ICON_CACHE_MAX; on the
     * rare overflow, fall back to a static scratch slot (last-lookup-wins,
     * no caching) rather than growing the table. */
    static syn_icon_entry_t overflow_scratch;
    syn_icon_entry_t *slot;
    if (icon_cache_count < ICON_CACHE_MAX) {
        slot = &icon_cache[icon_cache_count++];
        icon_gen++;
    } else {
        wlr_log(WLR_ERROR, "synui: icons: cache full (%d), not caching '%s'",
                ICON_CACHE_MAX, app_id);
        slot = &overflow_scratch;
    }
    *slot = e;
    return slot;
}

/*
 * Same resolution as icon_lookup(), but for a .desktop file we already have
 * the path of rather than an app_id to search for — what the desktop icons in
 * deskmenu.c hold. Cached under the path so repeated reloads don't re-decode
 * the PNG, and so the returned icon_surface stays valid for the caller.
 *
 * Returns NULL only if the file could not be parsed into a runnable entry;
 * the caller then treats it as a plain file.
 */
const syn_icon_entry_t *icon_lookup_desktop_path(const char *path)
{
    for (int i = 0; i < icon_cache_count; i++)
        if (strcmp(icon_cache[i].app_id, path) == 0)
            return &icon_cache[i];

    syn_icon_entry_t e = {0};
    /* Key the cache by path; app_id has no meaning for a loose .desktop. */
    snprintf(e.app_id, sizeof(e.app_id), "%s", path);
    parse_desktop_file(path, &e);
    if (!e.exec[0]) return NULL;   /* nothing to run — not an app entry */

    if (!e.display_name[0]) {
        /* Fall back to the basename without .desktop. */
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        snprintf(e.display_name, sizeof(e.display_name), "%s", base);
        size_t n = strlen(e.display_name);
        if (n > 8 && strcmp(e.display_name + n - 8, ".desktop") == 0)
            e.display_name[n - 8] = '\0';
    }

    if (e.icon_hint[0]) {
        e.icon_surface = find_and_decode_icon(e.icon_hint);
        /* Keyed by path here, so Icon= is the only name that means anything. */
        icon_theme_adopt(&e, e.icon_hint);
    }

    static syn_icon_entry_t overflow_scratch;
    syn_icon_entry_t *slot;
    if (icon_cache_count < ICON_CACHE_MAX) {
        slot = &icon_cache[icon_cache_count++];
        icon_gen++;
    } else {
        wlr_log(WLR_ERROR, "synui: icons: cache full (%d), not caching '%s'",
                ICON_CACHE_MAX, path);
        slot = &overflow_scratch;
    }
    *slot = e;
    return slot;
}

/*
 * The decoder, for a caller that keeps its own cache.
 *
 * Everything else in this file goes through the 64-entry icon_cache, which is
 * sized for the dock's pinned-plus-running set. The app grid holds three
 * hundred entries and pages through them, so routing it here would evict the
 * dock's icons on every arrow key and re-decode them on the next frame. It gets
 * the decode and owns the result.
 */
cairo_surface_t *icon_decode_named(const char *name)
{
    return find_and_decode_icon(name);
}

void icon_provide_name(const char *app_id, const char *icon_name)
{
    if (!app_id || !app_id[0] || !icon_name || !icon_name[0]) return;

    /* Resolve through the normal path first: that either finds the app's
     * existing entry or builds one from its .desktop file. */
    const syn_icon_entry_t *found = icon_lookup(app_id);
    if (!found) return;

    /* A .desktop icon is the more authoritative source (it's what the app was
     * installed as); only fill a gap the theme lookup couldn't. */
    syn_icon_entry_t *e = (syn_icon_entry_t *)found;
    if (e->icon_surface) return;

    e->icon_surface = find_and_decode_icon(icon_name);
    if (e->icon_surface) {
        snprintf(e->icon_hint, sizeof(e->icon_hint), "%s", icon_name);
        icon_theme_adopt(e, icon_name);
    }
}

void icon_draw_monogram(cairo_t *cr, const char *app_id,
                        double x, double y, double size)
{
    char letter = (app_id && app_id[0]) ? (char)toupper((unsigned char)app_id[0]) : '?';

    /* Deterministic colour from the app_id so the same app always gets the
     * same chip colour across renders, without needing a persistent palette
     * assignment table. */
    unsigned h = 5381;
    for (const char *p = app_id; p && *p; p++) h = h * 33 + (unsigned char)*p;
    float hue = (float)(h % 360) / 360.0f;
    /* Cheap HSV(hue, 0.55, 0.85)->RGB for a muted, readable chip colour. */
    float r, g, b;
    {
        float hh = hue * 6.0f;
        int i = (int)hh;
        float f = hh - i;
        float sv = 0.55f, vv = 0.85f;
        float p1 = vv * (1 - sv), q = vv * (1 - sv * f), t = vv * (1 - sv * (1 - f));
        switch (i % 6) {
        case 0: r=vv; g=t;  b=p1; break;
        case 1: r=q;  g=vv; b=p1; break;
        case 2: r=p1; g=vv; b=t;  break;
        case 3: r=p1; g=q;  b=vv; break;
        case 4: r=t;  g=p1; b=vv; break;
        default:r=vv; g=p1; b=q;  break;
        }
    }

    cairo_save(cr);
    double radius = size * 0.18;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + size - radius, y + radius, radius, -M_PI_2, 0);
    cairo_arc(cr, x + size - radius, y + size - radius, radius, 0, M_PI_2);
    cairo_arc(cr, x + radius, y + size - radius, radius, M_PI_2, M_PI);
    cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, r, g, b, 1.0);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
    cairo_set_font_size(cr, size * 0.5);
    cairo_text_extents_t ext;
    char label[2] = { letter, '\0' };
    syn_text_extents(cr, label, &ext);
    cairo_move_to(cr, x + size / 2.0 - ext.width / 2.0 - ext.x_bearing,
                      y + size / 2.0 - ext.height / 2.0 - ext.y_bearing);
    syn_show_text(cr, label);
    cairo_restore(cr);
}
