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

/* Defined down with the Workshop scan, but the apply/preview paths above it
 * need to tell a Workshop row from an image row. */
static int wppick_we_index(syn_server_t *s, int row);

/* Which option currently matches the live config, so the panel opens with the
 * active wallpaper highlighted. */
/* The id synui-wpengine currently has applied, or "" when it is not running.
 * synui does not mirror that state — the script owns it — so the picker reads
 * it back rather than trusting a copy that a `synui-wpengine off` from a
 * terminal would have made stale. */
static void wppick_active_we(char *out, size_t n)
{
    out[0] = '\0';

    const char *home = getenv("HOME");
    if (!home || !*home) return;

    char path[256];
    if (snprintf(path, sizeof(path), "%s/.config/synui/wpengine.state", home)
            >= (int)sizeof(path))
        return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    /* Every line is "<output> <id>"; the picker applies to all outputs at
     * once, so the first line's id identifies the choice. */
    char line[256];
    if (fgets(line, sizeof(line), f)) {
        char id[24];
        if (sscanf(line, "%*s %23s", id) == 1)
            snprintf(out, n, "%s", id);
    }
    fclose(f);
}

static int current_index(syn_server_t *s)
{
    /* An engine wallpaper wins over anything wallpaper.c holds: its surface is
     * what is actually on screen. Checked against the state file rather than
     * wallpaper_src so a choice made before this synui started still lands. */
    char active[24];
    wppick_active_we(active, sizeof(active));
    if (active[0]) {
        for (int i = 0; i < s->wppick.we_count; i++)
            if (strcmp(s->wppick.we[i].id, active) == 0)
                return wppick_option_count + s->wppick.found_count + i;
    }

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

    /* A Workshop wallpaper is not painted here at all: hand the id to
     * synui-wpengine, which starts linux-wallpaperengine as a layer-shell
     * client above wallpaper_tree. It persists the choice itself, so there is
     * nothing for wallpaper_state_save to record. */
    int we = wppick_we_index(s, idx);
    if (we >= 0) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "synui-wpengine set %s all", s->wppick.we[we].id);
        synui_spawn(cmd);
        s->config.wallpaper_src = SYN_WP_SRC_WPENGINE;
        return;
    }

    /* Leaving a Workshop wallpaper: the engine holds an opaque surface over
     * the whole output, so repainting wallpaper.c under it would change
     * nothing visible until it is gone. */
    if (s->config.wallpaper_src == SYN_WP_SRC_WPENGINE)
        synui_spawn("synui-wpengine off");

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

/* Highlight moved. Images and built-ins are cheap enough to apply live, which
 * is the whole point of the panel; a Workshop wallpaper spawns a GPU process
 * and would thrash if it fired on every arrow key, so it is only remembered
 * here and committed on Enter. */
static void wppick_preview(syn_server_t *s, int idx)
{
    if (wppick_we_index(s, idx) >= 0) {
        s->wppick.pending_we = idx;
        return;
    }

    s->wppick.pending_we = -1;
    wppick_apply(s, idx);
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

/* ── Browse: Steam Workshop (Wallpaper Engine) ───────────── */

/* Read one JSON string token, the opening quote already consumed. Titles carry
 * escapes and plenty of non-ASCII; \" and \\ pass through as the literal
 * character and other escapes are dropped rather than emitting a stray
 * backslash. Everything else (UTF-8 included) is byte-copied. Always drains
 * the whole token even when it does not fit, so the caller stays in sync. */
static void wp_json_token(FILE *f, char *out, size_t n)
{
    size_t i = 0;
    int c;
    while ((c = fgetc(f)) != EOF && c != '"') {
        if (c == '\\') {
            c = fgetc(f);
            if (c == EOF) break;
            if (c != '"' && c != '\\') continue;
        }
        if (out && i + 1 < n) out[i++] = (char)c;
    }
    if (out && n) out[i] = '\0';
}

/* Pull the TOP-LEVEL "title" and "type" out of a project.json.
 *
 * No JSON library here, but a flat strstr for "type" is not good enough: a
 * project.json carries a general.properties object whose every entry has its
 * own "type" ("color", "bool", …) and often "text", and those come BEFORE the
 * top-level keys in the files Wallpaper Engine writes. Matching the first hit
 * labelled scene wallpapers "color" and left titles as bare ids. So track
 * brace/bracket depth and only accept keys sitting directly in the root
 * object. Streamed rather than slurped because the properties block can be
 * far larger than the two strings we actually want. */
static void wp_project_meta(const char *path, char *title, size_t tn,
                            char *type, size_t yn)
{
    title[0] = '\0';
    type[0]  = '\0';

    FILE *f = fopen(path, "r");
    if (!f) return;

    int depth = 0;
    char pending[32] = "";   /* the depth-1 key whose value comes next */
    int c;

    while ((c = fgetc(f)) != EOF) {
        if (c == '{' || c == '[') { depth++; pending[0] = '\0'; continue; }
        if (c == '}' || c == ']') { depth--; pending[0] = '\0'; continue; }
        if (c != '"') continue;

        /* A string. Whether it is a key or a value is decided by the next
         * non-space character: ':' means key. */
        char tok[256];
        wp_json_token(f, tok, sizeof(tok));

        int nxt;
        while ((nxt = fgetc(f)) != EOF && (nxt == ' ' || nxt == '\t' ||
                                           nxt == '\n' || nxt == '\r'))
            ;

        if (nxt == ':') {
            /* Only root-object keys are the ones we mean. */
            if (depth == 1) snprintf(pending, sizeof(pending), "%s", tok);
            else pending[0] = '\0';
            continue;
        }
        if (nxt != EOF) ungetc(nxt, f);

        if (depth != 1 || !pending[0]) continue;

        if (strcmp(pending, "title") == 0)
            snprintf(title, tn, "%s", tok);
        else if (strcmp(pending, "type") == 0)
            snprintf(type, yn, "%s", tok);
        pending[0] = '\0';

        if (title[0] && type[0]) break;   /* both found — stop reading */
    }
    fclose(f);
}

/* Wallpaper Engine is Steam AppID 431960; its subscriptions land in the usual
 * workshop content tree, one numbered directory per wallpaper. */
static void wppick_scan_workshop(syn_server_t *s)
{
    s->wppick.we_count = 0;

    const char *home = getenv("HOME");
    if (!home || !*home) return;

    char root[256];
    if (snprintf(root, sizeof(root),
                 "%s/.local/share/Steam/steamapps/workshop/content/431960",
                 home) >= (int)sizeof(root))
        return;

    DIR *d = opendir(root);
    if (!d) return;   /* Wallpaper Engine not installed — no rows, no error */

    struct dirent *e;
    while ((e = readdir(d)) && s->wppick.we_count < WPPICK_WE_MAX) {
        if (e->d_name[0] == '.') continue;

        char proj[512];
        if (snprintf(proj, sizeof(proj), "%s/%s/project.json", root, e->d_name)
                >= (int)sizeof(proj))
            continue;

        char title[96], type[12];
        wp_project_meta(proj, title, sizeof(title), type, sizeof(type));

        /* A stale or partial subscription (no project.json, or no title in
         * it) still gets a row — the id is enough to hand to the engine. */
        if (!title[0]) snprintf(title, sizeof(title), "%s", e->d_name);
        if (!type[0])  snprintf(type,  sizeof(type),  "?");

        /* project.json spells the type both "Web" and "web" depending on who
         * published it; fold it so the subtitle column does not look ragged. */
        for (char *c = type; *c; c++)
            if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';

        int i = s->wppick.we_count++;
        snprintf(s->wppick.we[i].id,    sizeof(s->wppick.we[i].id),    "%s", e->d_name);
        snprintf(s->wppick.we[i].title, sizeof(s->wppick.we[i].title), "%s", title);
        snprintf(s->wppick.we[i].type,  sizeof(s->wppick.we[i].type),  "%s", type);
    }
    closedir(d);

    wlr_log(WLR_INFO, "synui: wppick: %d Workshop wallpaper(s) found",
            s->wppick.we_count);
}

/* Built-ins first, then the images the scan turned up, then Workshop. */
int wppick_total(syn_server_t *s)
{
    return wppick_option_count + s->wppick.found_count + s->wppick.we_count;
}

/* Row index → Workshop entry, or -1 when the row is a built-in or an image. */
static int wppick_we_index(syn_server_t *s, int row)
{
    int base = wppick_option_count + s->wppick.found_count;
    if (row < base || row >= base + s->wppick.we_count) return -1;
    return row - base;
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

    int we = wppick_we_index(s, row);
    if (we >= 0) {
        /* The Workshop title is the only human-readable handle these have —
         * the id is a bare number. Type goes in the subtitle so the animated
         * ones are obvious before you commit to starting the engine. */
        *label = s->wppick.we[we].title;
        *desc  = s->wppick.we[we].type;
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
    wppick_scan_workshop(s);
    s->wppick.visible = 1;
    s->wppick.selected = current_index(s);
    s->wppick.scroll = 0;
    s->wppick.pending_we = -1;
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
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        /* Commit a deferred Workshop row. Everything else already applied on
         * the way past it, so Enter is just "close" for those. */
        if (s->wppick.pending_we >= 0) {
            wppick_apply(s, s->wppick.pending_we);
            s->wppick.pending_we = -1;
        }
        wppick_hide(s);
        return 1;
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        /* Esc abandons a deferred row rather than committing it — arrowing
         * through 131 Workshop entries must not leave one applied. */
        s->wppick.pending_we = -1;
        wppick_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->wppick.selected > 0) {
            s->wppick.selected--;
            wppick_preview(s, s->wppick.selected);
            wppick_scroll_to_selection(s);
            synui_render_wppick(s);
        }
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->wppick.selected < wppick_total(s) - 1) {
            s->wppick.selected++;
            wppick_preview(s, s->wppick.selected);
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
