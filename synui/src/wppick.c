/*
 * wppick.c — wallpaper selector panel
 *
 * A compositor-drawn modal picker (Super+W, or "wallpaper" bind) for
 * switching wallpaper without editing synuirc:
 *
 *   Up/Down (j/k)     move the highlight (applies live for instant preview)
 *   Enter / Esc / q   close
 *   r                 rescan for images
 *
 * Below the built-ins, the panel lists image files found in the usual wallpaper
 * directories (~/Pictures and friends, /usr/share/backgrounds) — that is the
 * "browse" option. synui has no file chooser and no text entry, so pointing it
 * at a directory the user already keeps images in beats either building one or
 * shelling out to a GTK dialog for one path. The scan re-runs each time the
 * panel opens, so an image dropped into ~/Pictures shows up without a restart.
 *
 * Selecting an entry applies it immediately (so you see the change while the
 * panel is still open) and persists it to ~/.config/synui/wallpaper.state, so
 * the choice survives a restart. The persisted choice overrides the synuirc
 * `wallpaper` line on the next load — delete the state file to hand control
 * back to synuirc.
 *
 * The panel itself follows dispcfg.c's modal pattern: state in the server
 * struct, a wlr_scene tree drawn by synui_render_wppick() (render.c), and a
 * key handler that swallows input while open.
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
#include <strings.h>
#include <sys/stat.h>

#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Built-in wallpapers offered by the picker. Order is the on-screen order. */
const struct wppick_option wppick_options[] = {
    { "Synapse", "Default image wallpaper",       "default" },
    { "Matrix",  "Animated kanji rain (GPU)",     "matrix"  },
    { "None",    "Solid background color",        "none"    },
};
const int wppick_option_count =
    (int)(sizeof(wppick_options) / sizeof(wppick_options[0]));

/* Which option currently matches the live config, so the panel opens with the
 * active wallpaper highlighted. */
static int current_index(syn_server_t *s)
{
    if (s->config.wallpaper_src == SYN_WP_SRC_MATRIX)
        return 1;   /* "matrix" */
    if (s->config.wallpaper[0] == '\0')
        return 2;   /* "none" */

    /* A browsed image: highlight the row it actually is, so reopening the
     * panel lands on the wallpaper you are looking at rather than on
     * "Synapse". */
    for (int i = 0; i < s->wppick.found_count; i++)
        if (strcmp(s->wppick.found[i], s->config.wallpaper) == 0)
            return wppick_option_count + i;

    return 0;       /* the bundled image (or a path from synuirc) */
}

/* Keep the highlight inside the visible window of rows. */
static void wppick_scroll_to_selection(syn_server_t *s)
{
    if (s->wppick.selected < s->wppick.scroll)
        s->wppick.scroll = s->wppick.selected;
    if (s->wppick.selected >= s->wppick.scroll + WPPICK_ROWS)
        s->wppick.scroll = s->wppick.selected - WPPICK_ROWS + 1;
    if (s->wppick.scroll < 0) s->wppick.scroll = 0;
}

/* Apply an option token to the live config and repaint. Mirrors the synuirc
 * `wallpaper` key semantics for the built-in keywords. */
static void wppick_apply(syn_server_t *s, int idx)
{
    if (idx < 0 || idx >= wppick_total(s)) return;

    /* A row past the built-ins is an image the scan found: point the config
     * straight at its path. wallpaper_state_save already persists an arbitrary
     * path, so a browsed choice survives a restart with no extra machinery. */
    if (idx >= wppick_option_count) {
        s->config.wallpaper_src = SYN_WP_SRC_IMAGE;
        snprintf(s->config.wallpaper, sizeof(s->config.wallpaper), "%s",
                 s->wppick.found[idx - wppick_option_count]);
        goto repaint;
    }

    const char *tok = wppick_options[idx].token;

    if (strcmp(tok, "matrix") == 0) {
        s->config.wallpaper_src = SYN_WP_SRC_MATRIX;
    } else if (strcmp(tok, "default") == 0) {
        s->config.wallpaper_src = SYN_WP_SRC_IMAGE;
        strncpy(s->config.wallpaper, SYNUI_DATADIR "/wallpaper.png",
                sizeof(s->config.wallpaper) - 1);
        s->config.wallpaper[sizeof(s->config.wallpaper) - 1] = '\0';
    } else { /* "none" */
        s->config.wallpaper_src = SYN_WP_SRC_IMAGE;
        s->config.wallpaper[0] = '\0';
    }

repaint:
    /* Repaint the static backend (decodes/clears wallpaper_buf). The matrix
     * backend picks up / tears down on the next frame via matrix_active(). */
    wallpaper_reload(s);

    /* Kick a frame on every output so the change is visible at once: the
     * matrix path renders its first frame (and self-sustains), and a switch
     * away from matrix runs matrix_output_frame() once to drop its buffer. */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        wlr_output_schedule_frame(o->wlr_output);

    wallpaper_state_save(s);
}

/* ── Browse: find images on disk ─────────────────────────── */

/* wallpaper.c decodes PNG and JPEG; anything else would just fail to load. */
static bool wp_is_image(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".png")  == 0 ||
           strcasecmp(dot, ".jpg")  == 0 ||
           strcasecmp(dot, ".jpeg") == 0;
}

static int wp_cmp(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

static void wppick_scan_dir(syn_server_t *s, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) && s->wppick.found_count < WPPICK_FOUND_MAX) {
        if (e->d_name[0] == '.') continue;
        if (!wp_is_image(e->d_name)) continue;

        char path[256];
        if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path))
            continue;   /* path too long to store — skip rather than truncate */

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        /* The bundled image is already offered as "Synapse"; listing it again
         * under its filename would just be a duplicate row. */
        if (strcmp(path, SYNUI_DATADIR "/wallpaper.png") == 0) continue;

        for (int i = 0; i < s->wppick.found_count; i++)
            if (strcmp(s->wppick.found[i], path) == 0) goto next;

        snprintf(s->wppick.found[s->wppick.found_count++],
                 sizeof(s->wppick.found[0]), "%s", path);
    next:
        ;
    }
    closedir(d);
}

/* Where people actually keep wallpapers. Scanned in order, deduped by path. */
void wppick_scan(syn_server_t *s)
{
    s->wppick.found_count = 0;

    const char *home = getenv("HOME");
    if (home && *home) {
        static const char *rel[] = {
            "/Pictures/Wallpapers",
            "/Pictures/wallpapers",
            "/Pictures",
            "/.local/share/wallpapers",
            "/Wallpapers",
        };
        for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
            char dir[256];
            if (snprintf(dir, sizeof(dir), "%s%s", home, rel[i]) < (int)sizeof(dir))
                wppick_scan_dir(s, dir);
        }
    }

    wppick_scan_dir(s, "/usr/share/backgrounds");
    wppick_scan_dir(s, "/usr/share/wallpapers");

    /* Stable, predictable order — readdir's is neither, and a list that
     * reshuffles between openings is miserable to use. */
    qsort(s->wppick.found, (size_t)s->wppick.found_count,
          sizeof(s->wppick.found[0]), wp_cmp);

    wlr_log(WLR_INFO, "synui: wppick: %d image(s) found", s->wppick.found_count);
}

/* Built-ins first, then whatever the scan turned up. */
int wppick_total(syn_server_t *s)
{
    return wppick_option_count + s->wppick.found_count;
}

/* One row's text. render.c draws; the labels live here so the built-in and
 * found rows cannot drift apart. */
void wppick_row(syn_server_t *s, int row, const char **label, const char **desc)
{
    if (row < wppick_option_count) {
        *label = wppick_options[row].label;
        *desc  = wppick_options[row].desc;
        return;
    }

    const char *path = s->wppick.found[row - wppick_option_count];

    /* Show the filename, with the directory as the subtitle: the basename is
     * what identifies the image, and a full path would not fit the column. */
    const char *slash = strrchr(path, '/');
    *label = slash ? slash + 1 : path;
    *desc  = path;
}

void wppick_show(syn_server_t *s)
{
    wppick_scan(s);
    s->wppick.visible = 1;
    s->wppick.selected = current_index(s);
    s->wppick.scroll = 0;
    wppick_scroll_to_selection(s);
    synui_render_wppick(s);
}

void wppick_hide(syn_server_t *s)
{
    s->wppick.visible = 0;
    synui_render_wppick(s);
}

void wppick_toggle(syn_server_t *s)
{
    if (s->wppick.visible) wppick_hide(s);
    else                   wppick_show(s);
}

int wppick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->wppick.visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        wppick_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->wppick.selected > 0) {
            s->wppick.selected--;
            wppick_apply(s, s->wppick.selected);   /* live preview */
            wppick_scroll_to_selection(s);
            synui_render_wppick(s);
        }
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->wppick.selected < wppick_total(s) - 1) {
            s->wppick.selected++;
            wppick_apply(s, s->wppick.selected);   /* live preview */
            wppick_scroll_to_selection(s);
            synui_render_wppick(s);
        }
        return 1;
    case XKB_KEY_m:
        /* Cycle fill → fit → stretch → center → tile. The mode was previously
         * only reachable by hand-editing synuirc's wallpaper_mode, which is why
         * nobody knew stretch and center already existed.
         *
         * Repaint every output rather than just the selected entry: the mode is
         * global, so a live preview has to show on all of them. */
        s->config.wallpaper_mode =
            (s->config.wallpaper_mode + 1) % SYN_WALLPAPER_MODE_COUNT;
        wallpaper_reload(s);
        syn_output_t *wo;
        wl_list_for_each(wo, &s->outputs, link)
            wlr_output_schedule_frame(wo->wlr_output);
        wallpaper_state_save(s);
        synui_render_wppick(s);
        return 1;
    case XKB_KEY_r:
        /* Rescan without closing — for when you have just saved an image into
         * ~/Pictures and want it in the list. */
        wppick_scan(s);
        if (s->wppick.selected >= wppick_total(s))
            s->wppick.selected = wppick_total(s) - 1;
        wppick_scroll_to_selection(s);
        synui_render_wppick(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
