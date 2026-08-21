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

/* The accent our own icons are currently drawn in. Initialised to SYNAPSE's
 * cyan so an icon resolved before any theme has been pushed looks the same as
 * one resolved after — a dock that themed its icons only after the first theme
 * switch would be a worse bug than not theming them at all. */
static float g_icon_accent[3] = { 0.00f, 0.85f, 0.75f };

/* ── PNG decode (mirrors wallpaper.c's decode_png; not shared — see the
 * rationale in the implementation plan: both are 5-line cairo wrappers, and
 * duplicating avoids coupling icons.c's lifecycle to wallpaper.c's). ── */
static cairo_surface_t *decode_png(const char *path)
{
    cairo_surface_t *surf = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return NULL;
    }
    return surf;
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

/* Find an icon for `name` (either an Icon= value or, as a last resort, the
 * bare app_id), preferring a pre-rasterized PNG at a fixed hicolor size, then
 * the scalable SVG, then the legacy /usr/share/pixmaps drop. */
static cairo_surface_t *find_and_decode_icon(const char *name)
{
    if (!name || !*name) return NULL;

    /* Icon= may already be an absolute path. */
    if (name[0] == '/') {
        size_t len = strlen(name);
        if (len > 4 && strcmp(name + len - 4, ".png") == 0)
            return decode_png(name);
        if (len > 4 && strcmp(name + len - 4, ".svg") == 0)
            return decode_svg(name, ICON_RASTER_SIZE);
        return NULL;
    }

    static const char *sizes[] = { "64x64", "48x48", "128x128", "32x32" };
    const char *home = getenv("HOME");
    char path[320];

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        snprintf(path, sizeof(path),
                 "/usr/share/icons/hicolor/%s/apps/%s.png", sizes[i], name);
        cairo_surface_t *s = decode_png(path);
        if (s) return s;

        if (home) {
            snprintf(path, sizeof(path),
                     "%s/.local/share/icons/hicolor/%s/apps/%s.png",
                     home, sizes[i], name);
            s = decode_png(path);
            if (s) return s;
        }
    }

    /* No fixed-size raster — try the scalable theme dir. */
    snprintf(path, sizeof(path),
             "/usr/share/icons/hicolor/scalable/apps/%s.svg", name);
    cairo_surface_t *s = decode_svg(path, ICON_RASTER_SIZE);
    if (s) return s;

    if (home) {
        snprintf(path, sizeof(path),
                 "%s/.local/share/icons/hicolor/scalable/apps/%s.svg",
                 home, name);
        s = decode_svg(path, ICON_RASTER_SIZE);
        if (s) return s;
    }

    snprintf(path, sizeof(path), "/usr/share/pixmaps/%s.png", name);
    return decode_png(path);
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
