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
} persist_entry_t;

static persist_entry_t table[OUTPUT_PERSIST_MAX];
static int table_count  = 0;
static int table_loaded = 0;

static void outputs_path(char *buf, size_t len)
{
    buf[0] = '\0';
    const char *env = getenv("SYNUI_OUTPUTS");
    if (env && env[0]) { snprintf(buf, len, "%s", env); return; }

    syn_config_path(buf, len, "outputs.conf");
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

        e->enabled   = wo->enabled;
        e->width     = wo->width;
        e->height    = wo->height;
        e->refresh   = wo->refresh;
        e->transform = (int)wo->transform;
        e->scale     = wo->scale;
        e->grid_x    = o->grid_x;
        e->grid_y    = o->grid_y;
        e->x         = box.x;
        e->y         = box.y;
        e->primary    = o->primary;
        e->deep_color = o->deep_color;
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
               "# panel to move it.\n");
    for (int i = 0; i < table_count; i++) {
        persist_entry_t *e = &table[i];
        fprintf(f,
                "output %s enabled=%d width=%d height=%d refresh=%d "
                "transform=%d scale=%.6f grid_x=%d grid_y=%d x=%d y=%d "
                "primary=%d deep_color=%d\n",
                e->name, e->enabled, e->width, e->height, e->refresh,
                e->transform, (double)e->scale, e->grid_x, e->grid_y,
                e->x, e->y, e->primary, e->deep_color);
    }
    fclose(f);
}
