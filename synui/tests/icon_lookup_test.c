/*
 * icon_lookup_test — where icons.c looks for an icon, and what it does with it
 *
 * Every failure in this file is SILENT. An icon that cannot be found is not an
 * error: icon_draw_monogram() puts a coloured letter chip in its place and the
 * dock and the application grid carry on. So a whole class of pictures can go
 * missing — every flatpak on the box, every entry whose theme spells its
 * directories the other way up — and the only symptom is a page of letters
 * nobody can prove used to be pictures.
 *
 * Which is what had happened. Three separate reasons, all found together:
 *
 *   1. THE ROOTS WERE HARDCODED to ~/.local/share/icons and /usr/share/icons.
 *      XDG_DATA_DIRS is a list, and on this desktop it carries
 *      /var/lib/flatpak/exports/share — the ONLY place a flatpak's icon exists.
 *      Every flatpak drew a monogram.
 *
 *   2. TWO DIRECTORY LAYOUTS. The spec says <theme>/<size>/<category>; breeze
 *      says <theme>/<category>/<size>, with the size spelt as a bare number.
 *      Only `apps` was tried the other way up, and only as "48x48" — which is
 *      not a directory breeze has, so that branch had never once hit. Eight of
 *      SYNAPSE's own settings panels resolve in breeze and drew monograms in
 *      SYNAPSE's own application grid.
 *
 *   3. NO SIZE ABOVE 128. waydroid, cliamp and the Blackmagic tools each ship
 *      exactly one PNG, at 512 or 256, and so had no icon at all.
 *
 * Driven against a mkdtemp sandbox with HOME, XDG_DATA_HOME and XDG_DATA_DIRS
 * pointed into it, so nothing here depends on what the machine running the test
 * has installed — and nothing here reads the live desktop's icon themes.
 *
 * ⚠ THE ROOTS ARE BUILT ONCE, on the first lookup, and cached for the process.
 * That is right for a compositor (the environment does not change under it) and
 * it means this test must set the environment before it asks for anything.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cairo.h>

#include "synui.h"

static int failures;

static void check(bool ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static char root[256];

/* mkdir -p, because every path here is several levels deep and the layouts
 * under test are exactly the shape of those levels. */
static void mkdirs(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') { *p = '\0'; mkdir(buf, 0755); *p = '/'; }
    mkdir(buf, 0755);
}

/* A real PNG of a given size — cairo writes it, so what the decoder reads is a
 * genuine image file and not a fixture that only looks like one. */
static void put_png(const char *rel, int size)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, rel);

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdirs(dir); }

    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                    size, size);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgba(cr, 0.2, 0.7, 0.9, 1.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_write_to_png(s, path);
    cairo_surface_destroy(s);
}

static void put_svg(const char *rel)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, rel);

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdirs(dir); }

    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
          "<rect width='16' height='16' fill='#38b'/></svg>\n", f);
    fclose(f);
}

/* Resolved, and at what size. -1 means "not found at all". */
static int decoded_size(const char *name)
{
    cairo_surface_t *s = icon_decode_named(name);
    if (!s) return -1;
    int w = cairo_image_surface_get_width(s);
    cairo_surface_destroy(s);
    return w;
}

int main(void)
{
    printf("icons: where a theme keeps its files, and what comes back\n");

    char tmpl[] = "/tmp/synui-icons-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 1; }
    snprintf(root, sizeof(root), "%s", tmpl);

    /*
     * Three roots, deliberately in three different places:
     *
     *   home/       via XDG_DATA_HOME
     *   doticons/   the pre-XDG ~/.icons, which is not in any list
     *   flat/, sys/ via XDG_DATA_DIRS — `flat` stands in for the flatpak export
     *               directory, the root whose absence started this.
     */
    char home[512], data_home[512], data_dirs[1024];
    snprintf(home, sizeof(home), "%s/h", root);
    snprintf(data_home, sizeof(data_home), "%s/home", root);
    snprintf(data_dirs, sizeof(data_dirs), "%s/flat:%s/sys", root, root);
    mkdirs(home);
    setenv("HOME", home, 1);
    setenv("XDG_DATA_HOME", data_home, 1);
    setenv("XDG_DATA_DIRS", data_dirs, 1);

    /* The sandbox theme is named, so the walk tries it before the five stock
     * fallbacks — none of which exist under these roots anyway. */
    icon_set_theme("Sandbox");

    /* ── 1. The spec's layout ──────────────────────────────────────────── */
    put_png("sys/icons/Sandbox/48x48/apps/spec-app.png", 48);
    check(decoded_size("spec-app") == 48,
          "<theme>/<size>/<category> — the spec's layout");

    /* ── 2. breeze's layout ────────────────────────────────────────────── */
    /* ⚠ THE SIZE IS A BARE NUMBER HERE. This is the whole bug: the inverted
     * probe that already existed asked for `apps/48x48`, and no theme on earth
     * has that directory, so it never hit once. */
    put_png("sys/icons/Sandbox/preferences/22/breeze-style.png", 22);
    check(decoded_size("breeze-style") == 22,
          "<theme>/<category>/<size> — breeze's, with a bare-number size");

    put_png("sys/icons/Sandbox/actions/16/breeze-action.png", 16);
    check(decoded_size("breeze-action") == 16,
          "…including `actions`, not just `apps`");

    /* ── 3. Every root, not two of them ────────────────────────────────── */
    put_png("flat/icons/hicolor/128x128/apps/org.example.Flatpak.png", 128);
    check(decoded_size("org.example.Flatpak") == 128,
          "a root that only appears in XDG_DATA_DIRS is searched");

    put_png("home/icons/Sandbox/48x48/apps/user-installed.png", 48);
    check(decoded_size("user-installed") == 48,
          "…so is XDG_DATA_HOME's");

    put_png("h/.icons/Sandbox/48x48/apps/legacy-root.png", 48);
    check(decoded_size("legacy-root") == 48,
          "…and the pre-XDG ~/.icons, which is in no list at all");

    /* ── 4. Sizes past 128, and the cap that makes them affordable ─────── */
    put_png("sys/icons/hicolor/512x512/apps/big-only.png", 512);
    int big = decoded_size("big-only");
    check(big > 0, "an application that ships ONLY a 512px PNG is found");
    check(big == 128,
          "…and comes back capped, not as a megabyte of ARGB per grid tile");

    put_png("sys/icons/hicolor/256x256/apps/mid-only.png", 256);
    check(decoded_size("mid-only") == 128, "…same for a 256-only one");

    /* Under the cap it is handed back exactly as it was decoded: the resample
     * is for the oversized ones, and rewriting a 64px icon would cost a decode
     * and a paint for a picture nobody asked to change. */
    put_png("sys/icons/hicolor/64x64/apps/small-only.png", 64);
    check(decoded_size("small-only") == 64, "…and a small one is left alone");

    /* ── 5. Vectors, both ways up ──────────────────────────────────────── */
    put_svg("sys/icons/Sandbox/scalable/apps/vector-spec.svg");
    check(decoded_size("vector-spec") == 128,
          "a scalable SVG rasterizes at the raster size");
    put_svg("sys/icons/Sandbox/apps/scalable/vector-breeze.svg");
    check(decoded_size("vector-breeze") == 128,
          "…and so does one under <category>/scalable");

    /* ── 6. The two ends ───────────────────────────────────────────────── */
    char abs_path[1024];
    snprintf(abs_path, sizeof(abs_path), "%s/sys/icons/Sandbox/48x48/apps/spec-app.png", root);
    check(decoded_size(abs_path) == 48, "an absolute Icon= path is taken as one");

    check(decoded_size("nothing-of-this-name-exists") == -1,
          "and a name nothing matches resolves to nothing, not to something");

    printf("icon_lookup_test: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
