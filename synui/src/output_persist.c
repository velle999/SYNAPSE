/*
 * output_persist.c — remember monitor settings across restarts
 *
 * Every real geometry change (the built-in display panel or a wlr-randr
 * client) ends up in output_layout_changed() (see output_mgmt.c), which
 * calls output_persist_save() here to write the current mode, transform,
 * scale, and position of every connected output to
 * ~/.config/synui/outputs.conf. On the next new_output — reboot or hotplug
 * — server_new_output() (synui_main.c) calls output_persist_apply() first;
 * if this monitor has a saved entry, its saved state is restored instead of
 * the preferred-mode / auto-placement default.
 *
 * An entry is keyed by connector name (e.g. "DP-1") AND by the monitor on it
 * — make/model/serial from its EDID, see output_identity(). ⛔ THE CONNECTOR
 * ALONE IS NOT A MONITOR. A laptop's one HDMI port is a TV one evening and a
 * desk monitor the next morning: keyed by name, the desk monitor was handed
 * the TV's 3840x2160 — a mode a 1440p panel will take and downscale, so it
 * came up blurry at 60Hz instead of sharp at its own preferred mode, and
 * nothing said why. Now a monitor nothing was saved for gets the preferred
 * mode the caller already committed, and each one on the port keeps its own.
 *
 * Entries for outputs that aren't currently connected are preserved on save
 * (merged from the file already on disk) so unplugging a monitor doesn't
 * lose its settings.
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

/* Per MONITOR now, not per connector, so a laptop collects one for every
 * screen it has been plugged into. When the table is full the oldest entry
 * for a monitor that is not connected makes room — see table_new_entry(). */
#define OUTPUT_PERSIST_MAX 32

typedef struct {
    char  name[64];
    /* The monitor this entry describes (output_identity()); "" is a screen
     * that reports no make, model or serial. has_id is 0 only for a line
     * written before entries carried one: it is matched by connector alone,
     * as every entry used to be, and takes the identity of the first monitor
     * it is applied to — so an existing desk keeps its layout through the
     * upgrade. */
    char  id[192];
    int   has_id;
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

/*
 * Which monitor this is, as its EDID names it: "make/model/serial", each part
 * with whitespace and '/' turned into '_' so the whole thing is one token on
 * the line and the separators stay unambiguous. Empty when the output reports
 * none of the three — a headless output, or a screen whose EDID could not be
 * read — and such an output is matched by connector name alone.
 */
static void output_identity(const struct wlr_output *wo, char *buf, size_t len)
{
    const char *part[3] = { wo->make, wo->model, wo->serial };
    size_t n = 0;
    int any = 0;
    for (int i = 0; i < 3; i++) {
        if (i && n + 1 < len) buf[n++] = '/';
        for (const char *p = part[i]; p && *p && n + 1 < len; p++) {
            unsigned char c = (unsigned char)*p;
            buf[n++] = (isspace(c) || iscntrl(c) || c == '/') ? '_' : (char)c;
            any = 1;
        }
    }
    buf[any ? n : 0] = '\0';
}

/* Does any OTHER connected output carry this identity? Two monitors that
 * report the same serial — cloned EDIDs exist — cannot be told apart, so
 * neither may claim an entry saved under a different connector. */
static int identity_shared(syn_server_t *s, syn_output_t *self, const char *id)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (o == self || vdisplay_is(o)) continue;
        char other[192];
        output_identity(o->wlr_output, other, sizeof(other));
        if (!strcmp(other, id)) return 1;
    }
    return 0;
}

/*
 * The entry for this monitor on this connector, best evidence first:
 *
 *   1. saved for this monitor, on this connector;
 *   2. saved for this monitor on ANOTHER connector — it was moved, or a dock
 *      renumbered its ports (MST hands out DP-5/DP-6 by plug order). Only for
 *      a monitor with a serial that no other connected screen shares: two
 *      identical serial-less monitors are told apart by their ports, so for
 *      them the port has to agree;
 *   3. a line from before entries carried an identity, on this connector.
 *
 * ⚠ 2 and 3 ADOPT what they find — renamed to this connector, stamped with
 * this identity — so the next save rewrites that entry instead of adding a
 * second one, and so a legacy line is claimed by the first monitor it is
 * applied to and is never handed to a different one afterwards.
 *
 * NULL is a monitor nothing was saved for, and that is the autodetect path:
 * the caller's preferred mode and auto-placement stand.
 */
static persist_entry_t *table_lookup(syn_server_t *s, syn_output_t *output)
{
    struct wlr_output *wo = output->wlr_output;
    char id[192];
    output_identity(wo, id, sizeof(id));

    for (int i = 0; i < table_count; i++)
        if (table[i].has_id && !strcmp(table[i].id, id) &&
            !strcmp(table[i].name, wo->name))
            return &table[i];

    if (wo->serial && *wo->serial && !identity_shared(s, output, id))
        for (int i = 0; i < table_count; i++)
            if (table[i].has_id && !strcmp(table[i].id, id)) {
                wlr_log(WLR_INFO, "synui: %s is the monitor last saved on %s "
                                  "— its settings follow it", wo->name,
                        table[i].name);
                snprintf(table[i].name, sizeof(table[i].name), "%s", wo->name);
                return &table[i];
            }

    for (int i = 0; i < table_count; i++)
        if (!table[i].has_id && !strcmp(table[i].name, wo->name)) {
            snprintf(table[i].id, sizeof(table[i].id), "%s", id);
            table[i].has_id = 1;
            return &table[i];
        }

    return NULL;
}

/* Is this entry the one a connected output is using right now? */
static int entry_is_live(syn_server_t *s, const persist_entry_t *e)
{
    if (!e->has_id) return 0;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (vdisplay_is(o)) continue;
        char id[192];
        output_identity(o->wlr_output, id, sizeof(id));
        if (!strcmp(e->name, o->wlr_output->name) && !strcmp(e->id, id))
            return 1;
    }
    return 0;
}

/*
 * A blank entry at the end of the table. Full, the OLDEST entry for a monitor
 * that is not connected makes room: entries are only ever appended, so the
 * first one in the file is the one that has been there longest. NULL only if
 * every entry belongs to a connected screen.
 */
static persist_entry_t *table_new_entry(syn_server_t *s)
{
    if (table_count >= OUTPUT_PERSIST_MAX) {
        int victim = -1;
        for (int i = 0; i < table_count && victim < 0; i++)
            if (!entry_is_live(s, &table[i])) victim = i;
        if (victim < 0) return NULL;
        wlr_log(WLR_INFO, "synui: outputs.conf is full — forgetting the "
                          "oldest monitor, %s %s", table[victim].name,
                table[victim].has_id ? table[victim].id : "");
        memmove(&table[victim], &table[victim + 1],
                (size_t)(table_count - victim - 1) * sizeof(table[0]));
        table_count--;
    }
    persist_entry_t *e = &table[table_count++];
    memset(e, 0, sizeof(*e));
    return e;
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
            if      (!strcmp(tok, "id")) {
                /* "-" is how an identity-less screen is written down. */
                snprintf(e->id, sizeof(e->id), "%s",
                         strcmp(val, "-") ? val : "");
                e->has_id = 1;
            }
            else if (!strcmp(tok, "enabled"))   e->enabled   = atoi(val);
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

/* Restore a saved mode/transform/scale/position for this monitor, if we
 * have one. Returns the layout entry (as wlr_output_layout_add_auto()
 * would) on success, or NULL if there's no saved entry or the backend
 * rejected it — in either case the caller's own preferred-mode default
 * (already committed before this runs) and wlr_output_layout_add_auto()
 * fallback stand. */
struct wlr_output_layout_output *output_persist_apply(syn_server_t *s,
                                                       syn_output_t *output)
{
    /* ⛔ A VIRTUAL DISPLAY IS NOT REMEMBERED, in either direction. Whoever asks
     * for one states its size and refresh in the same call; a saved HEADLESS-1
     * entry would apply last session's mode over this request and the caller's
     * geometry would be dropped with nothing saying so. The names are recycled
     * (HEADLESS-1 is whichever one was made first, every session), so the entry
     * would not even be describing the same screen. */
    if (vdisplay_is(output)) return NULL;

    table_load();
    persist_entry_t *e = table_lookup(s, output);
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
        /* The other half of the rule in output_persist_apply(): a virtual
         * display is never read back, so writing it would only fill the file
         * with HEADLESS-n entries that outlive the thing they describe. */
        if (vdisplay_is(o)) continue;
        persist_entry_t *e = table_lookup(s, o);
        if (!e) {
            e = table_new_entry(s);
            if (!e) continue;
            snprintf(e->name, sizeof(e->name), "%s", wo->name);
            output_identity(wo, e->id, sizeof(e->id));
            e->has_id = 1;
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
        /* ⚠ Under Duplicate the mode on screen is BORROWED — the largest one
         * every screen shares — and saved_mode_* holds the screen's own.
         * Writing the borrowed one made it the screen's own at the next login,
         * where Duplicate saved it again as the mode to give back, so leaving
         * Duplicate (or picking "external only") left a TV at the mirror
         * resolution for good. */
        if (o->saved_mode_w > 0) {
            e->width   = o->saved_mode_w;
            e->height  = o->saved_mode_h;
            e->refresh = o->saved_mode_refresh;
        }
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
     * plugging it back in restores it.
     *
     * ⚠ By ENTRY, not by connector name: the TV and the desk monitor that
     * take turns on one HDMI port are two entries with the same name, and
     * only the one on the desk right now is the primary. Looked up after the
     * loop above, because making room in a full table moves entries. */
    syn_output_t *prim = NULL;
    wl_list_for_each(o, &s->outputs, link)
        if (o->primary) { prim = o; break; }

    if (prim) {
        const persist_entry_t *pe = vdisplay_is(prim) ? NULL
                                                       : table_lookup(s, prim);
        for (int i = 0; i < table_count; i++)
            table[i].primary = (&table[i] == pe);
    }

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
               "# restored if the connector still accepts HDR at next login.\n"
               "#\n"
               "# id= is the monitor (make/model/serial, from its EDID). Each\n"
               "# monitor keeps its own line, so a different one plugged into\n"
               "# the same port starts at its own preferred mode. A line with\n"
               "# no id applies to whichever monitor is on that connector.\n");
    for (int i = 0; i < table_count; i++) {
        persist_entry_t *e = &table[i];
        fprintf(f, "output %s ", e->name);
        /* A legacy line stays one until a monitor claims it — written with
         * an id it would stop matching anything. */
        if (e->has_id)
            fprintf(f, "id=%s ", e->id[0] ? e->id : "-");
        fprintf(f,
                "enabled=%d width=%d height=%d refresh=%d "
                "transform=%d scale=%.6f grid_x=%d grid_y=%d x=%d y=%d "
                "primary=%d deep_color=%d hdr=%d hdr_white=%d\n",
                e->enabled, e->width, e->height, e->refresh,
                e->transform, (double)e->scale, e->grid_x, e->grid_y,
                e->x, e->y, e->primary, e->deep_color, e->hdr, e->hdr_white);
    }
    fclose(f);
}
