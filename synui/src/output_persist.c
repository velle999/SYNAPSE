/*
 * output_persist.c — remember monitor settings across restarts
 *
 * Every real geometry change (the built-in display panel or a wlr-randr
 * client) ends up in output_layout_changed() (see output_mgmt.c), which
 * calls output_persist_save() here to write the current mode, transform,
 * scale, and position of every connected output to
 * ~/.config/synui/outputs.conf. On the next new_output — reboot or hotplug
 * — server_new_output() (synui_main.c) calls output_persist_apply() first;
 * if that connector name has a saved entry, its saved state is restored
 * instead of the preferred-mode / auto-placement default.
 *
 * Outputs are keyed by connector name (e.g. "DP-1"), matching how the
 * display panel already refers to them. Entries for outputs that aren't
 * currently connected are preserved on save (merged from the file already
 * on disk) so unplugging a monitor doesn't lose its settings.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

#include "synui.h"

#define OUTPUT_PERSIST_MAX 16

typedef struct {
    char  name[64];
    int   enabled;
    int   width, height;
    int   refresh;      /* mHz; 0 = don't care when matching modes */
    int   transform;
    float scale;
    int   grid_x, grid_y;
    int   x, y;
    int   primary;
    int   deep_color;   /* 10-bit scanout; see dispcfg_set_deep_color */
    /* HDR10 output, and the SDR white level it was driven at. ⚠ The flag is
     * a REQUEST when it is read back: output_persist_apply() runs while the
     * output is still being built, before hdr_probe() has asked the connector
     * anything, so it is parked in hdr_want and honoured by
     * hdr_output_added(). Applying it here would be applying it blind. */
    int   hdr;
    int   hdr_white;
} persist_entry_t;

static persist_entry_t table[OUTPUT_PERSIST_MAX];
static int table_count  = 0;
static int table_loaded = 0;

/*
 * Set by output_persist_adopt_file() and read in preference to everything
 * else. The greeter is the only caller: it runs as another account, cannot
 * read the user's ~/.config, and is handed a copy of their outputs.conf that
 * their own session published. See greeterbg.c.
 */
static char adopted_path[512];

static void outputs_path(char *buf, size_t len)
{
    buf[0] = '\0';
    if (adopted_path[0]) { snprintf(buf, len, "%s", adopted_path); return; }

    const char *env = getenv("SYNUI_OUTPUTS");
    if (env && env[0]) { snprintf(buf, len, "%s", env); return; }

    syn_config_path(buf, len, "outputs.conf");
}

/*
 * Read the layout from `path` from now on, and forget whatever was loaded.
 *
 * ⛔ THE FORGETTING IS THE POINT. table_load() is a once-per-process cache, and
 * by the time the greeter knows whose login screen it is drawing, every output
 * already exists and the table has already been loaded — empty, because the
 * greeter's own HOME is `/`. Setting the path without dropping that answer
 * would change nothing at all.
 */
void output_persist_adopt_file(const char *path)
{
    if (!path || !*path) return;
    snprintf(adopted_path, sizeof(adopted_path), "%s", path);
    table_count  = 0;
    table_loaded = 0;
}

static persist_entry_t *table_find(const char *name)
{
    for (int i = 0; i < table_count; i++)
        if (!strcmp(table[i].name, name))
            return &table[i];
    return NULL;
}

static void table_load(void)
{
    if (table_loaded) return;
    table_loaded = 1;

    char path[256];
    outputs_path(path, sizeof(path));
    if (!path[0]) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (table_count < OUTPUT_PERSIST_MAX && fgets(line, sizeof(line), f)) {
        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        if (strncmp(s, "output ", 7) != 0) continue;
        s += 7;

        char *tok = strtok(s, " \t\r\n");
        if (!tok) continue;

        persist_entry_t *e = &table[table_count];
        memset(e, 0, sizeof(*e));
        e->scale = 1.0f;
        snprintf(e->name, sizeof(e->name), "%s", tok);

        while ((tok = strtok(NULL, " \t\r\n"))) {
            char *eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = '\0';
            const char *val = eq + 1;
            if      (!strcmp(tok, "enabled"))   e->enabled   = atoi(val);
            else if (!strcmp(tok, "width"))     e->width     = atoi(val);
            else if (!strcmp(tok, "height"))    e->height    = atoi(val);
            else if (!strcmp(tok, "refresh"))   e->refresh   = atoi(val);
            else if (!strcmp(tok, "transform")) e->transform = atoi(val);
            else if (!strcmp(tok, "scale"))     e->scale     = strtof(val, NULL);
            else if (!strcmp(tok, "grid_x"))    e->grid_x    = atoi(val);
            else if (!strcmp(tok, "grid_y"))    e->grid_y    = atoi(val);
            else if (!strcmp(tok, "x"))         e->x         = atoi(val);
            else if (!strcmp(tok, "y"))         e->y         = atoi(val);
            else if (!strcmp(tok, "primary"))   e->primary   = atoi(val);
            else if (!strcmp(tok, "deep_color")) e->deep_color = atoi(val);
            else if (!strcmp(tok, "hdr"))       e->hdr       = atoi(val);
            else if (!strcmp(tok, "hdr_white")) e->hdr_white = atoi(val);
        }
        table_count++;
    }
    fclose(f);
}

/* Restore a saved mode/transform/scale/position for this connector, if we
 * have one. Returns the layout entry (as wlr_output_layout_add_auto()
 * would) on success, or NULL if there's no saved entry or the backend
 * rejected it — in either case the caller's own preferred-mode default
 * (already committed before this runs) and wlr_output_layout_add_auto()
 * fallback stand. */
struct wlr_output_layout_output *output_persist_apply(syn_server_t *s,
                                                       syn_output_t *output)
{
    table_load();
    persist_entry_t *e = table_find(output->wlr_output->name);
    if (!e) return NULL;

    struct wlr_output *wo = output->wlr_output;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, e->enabled);

    if (e->width > 0 && e->height > 0) {
        struct wlr_output_mode *mode, *match = NULL;
        wl_list_for_each(mode, &wo->modes, link) {
            if (mode->width == e->width && mode->height == e->height &&
                (e->refresh <= 0 || mode->refresh == e->refresh)) {
                match = mode;
                break;
            }
        }
        if (match)
            wlr_output_state_set_mode(&state, match);
        else
            wlr_output_state_set_custom_mode(&state, e->width, e->height,
                                             e->refresh);
    }
    wlr_output_state_set_transform(&state,
        (enum wl_output_transform)(e->transform & 7));
    if (e->scale > 0.0f)
        wlr_output_state_set_scale(&state, e->scale);

    if (!wlr_output_test_state(wo, &state) ||
        !wlr_output_commit_state(wo, &state)) {
        wlr_log(WLR_ERROR,
                "synui: saved output config rejected for %s, using defaults",
                wo->name);
        wlr_output_state_finish(&state);
        return NULL;
    }
    wlr_output_state_finish(&state);

    output->grid_x  = e->grid_x;
    output->grid_y  = e->grid_y;
    output->primary = e->primary;

    /* Re-apply 10-bit as its own modeset: it is a render-format change the
     * state above deliberately does not carry, and it is allowed to fail (a
     * different cable, a higher mode) without taking the rest of the saved
     * config down with it. */
    if (e->deep_color)
        dispcfg_set_deep_color(s, output, 1);

    /* HDR is a REQUEST at this point, not an apply — see the struct field. The
     * white level is carried across whether or not the mode was on, so turning
     * it back on finds the level it was left at rather than the default. */
    output->hdr_want  = e->hdr ? 1 : 0;
    output->hdr_white = hdr_white_clamp(e->hdr_white);

    return wlr_output_layout_add(s->output_layout, wo, e->x, e->y);
}

static void ensure_parent_dir(const char *path)
{
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = '\0';
    if (mkdir(dir, 0755) != 0 && errno != EEXIST)
        wlr_log(WLR_ERROR, "synui: mkdir %s failed: %s", dir, strerror(errno));
}

/* Snapshot every connected output's current state into the table (merging
 * with whatever's already on disk for disconnected ones) and rewrite the
 * file. Called from output_layout_changed() after any real apply. */
void output_persist_save(syn_server_t *s)
{
    table_load();

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        struct wlr_output *wo = o->wlr_output;
        persist_entry_t *e = table_find(wo->name);
        if (!e) {
            if (table_count >= OUTPUT_PERSIST_MAX) continue;
            e = &table[table_count++];
            memset(e, 0, sizeof(*e));
            snprintf(e->name, sizeof(e->name), "%s", wo->name);
        }

        struct wlr_box box;
        wlr_output_layout_get_box(s->output_layout, wo, &box);

        /* A DETACHED output is out of the layout on purpose (external-only
         * mode, or a lid close) and must not have that written down as its
         * saved state. Two things would go wrong if it were: the box is
         * {0,0,0,0} for an output the layout does not hold, so its position
         * would be saved as the origin — on top of whatever really is there —
         * and `enabled=0` would be restored at the next login, leaving the
         * laptop panel dark on a machine with no external screen plugged in
         * and no obvious way to get it back.
         *
         * The grid cell IS still saved: it is the arrangement the user chose
         * and it does not stop being true while the screen is off. Only the
         * facts that are consequences of being detached are skipped, so the
         * entry keeps describing the screen as it was last actually used. */
        e->grid_x    = o->grid_x;
        e->grid_y    = o->grid_y;
        e->primary    = o->primary;
        e->deep_color = o->deep_color;
        /* ⚠ hdr_on, not hdr_want: what is actually on screen. A connector that
         * was saved in HDR and refused it this session (a different cable, a
         * mode with no bandwidth for it) must not have the refusal written
         * down as a choice to stop wanting it — hdr_output_added() clears
         * hdr_want either way, so the live flag is the only honest source. */
        e->hdr        = o->hdr_on;
        e->hdr_white  = hdr_white_clamp(o->hdr_white);
        if (o->detached) continue;

        e->enabled   = wo->enabled;
        e->width     = wo->width;
        e->height    = wo->height;
        e->refresh   = wo->refresh;
        e->transform = (int)wo->transform;
        e->scale     = wo->scale;
        e->x         = box.x;
        e->y         = box.y;
    }

    /* At most one primary. If a connected output claims it, it wins and every
     * other entry — including saved ones for monitors that aren't plugged in
     * — is demoted, so unplugging the old primary and promoting a new one
     * can't leave two claims in the file. If no connected output is primary
     * we leave the saved flags alone, so unplugging the primary monitor and
     * plugging it back in restores it. */
    const char *primary_name = NULL;
    wl_list_for_each(o, &s->outputs, link)
        if (o->primary) { primary_name = o->wlr_output->name; break; }

    if (primary_name)
        for (int i = 0; i < table_count; i++)
            table[i].primary = !strcmp(table[i].name, primary_name);

    char path[256];
    outputs_path(path, sizeof(path));
    if (!path[0]) return;

    ensure_parent_dir(path);
    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: can't save output layout to %s: %s",
                path, strerror(errno));
        return;
    }

    fprintf(f, "# synui output layout — autogenerated by the display panel\n"
               "# (Super+D) and wlr-randr clients. Edit by hand if you like;\n"
               "# entries for disconnected monitors are kept so replugging\n"
               "# one restores it.\n"
               "#\n"
               "# primary=1 marks the monitor Xwayland reports as the X11\n"
               "# RandR primary — where X11 apps that ask for \"the default\n"
               "# display\" (SDL games, so Steam) open. Press p in the display\n"
               "# panel to move it.\n"
               "#\n"
               "# hdr=1 drives that monitor in HDR10 (Shift+D in the display\n"
               "# panel). hdr_white is the SDR white level in cd/m2 — where a\n"
               "# plain white window sits on PQ's absolute scale. It is only\n"
               "# restored if the connector still accepts HDR at next login.\n");
    for (int i = 0; i < table_count; i++) {
        persist_entry_t *e = &table[i];
        fprintf(f,
                "output %s enabled=%d width=%d height=%d refresh=%d "
                "transform=%d scale=%.6f grid_x=%d grid_y=%d x=%d y=%d "
                "primary=%d deep_color=%d hdr=%d hdr_white=%d\n",
                e->name, e->enabled, e->width, e->height, e->refresh,
                e->transform, (double)e->scale, e->grid_x, e->grid_y,
                e->x, e->y, e->primary, e->deep_color, e->hdr, e->hdr_white);
    }
    fclose(f);
}
