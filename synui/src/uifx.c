/*
 * uifx.c — the window-effects page of the Super+E panel.
 *
 * filters.c owns the CRT shader's knobs; this owns the scenefx ones: rounded
 * corners, the drop shadow, backdrop blur, the glass halo and translucency.
 * They were reachable only by hand-editing synuirc and restarting the
 * compositor — which is a poor deal for the one class of setting whose value
 * you can only decide by looking at it. Half the shadow settings exist BECAUSE
 * they were tuned by eye (shadow_spread was added after measuring that a pure
 * gaussian caps the darkening at the border at ~50%); doing that from a config
 * file is a restart per notch.
 *
 * The two differences from the CRT page, both structural:
 *
 *   1. These are not all 0..1. Pixels, pass counts and percentages each need
 *      their own range and notch, so the rows carry a descriptor table instead
 *      of sharing one FILTER_STEP.
 *
 *   2. The CRT shader re-samples config every frame, so a filters slider only
 *      has to move a float. These live in the SCENE GRAPH — a shadow is a
 *      wlr_scene_shadow node sized at build time — so every change has to be
 *      pushed back out (uifx_apply). A knob that moved the number and not the
 *      window would be exactly the bug this panel exists to remove.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_damage_ring.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"

/* How a row reads and edits. SWITCH renders as a word and has no bar; the rest
 * differ only in their unit suffix, which is worth having because "8" means
 * nothing next to "8 px" when the row above it is a pass count. */
enum uifx_kind {
    UIFX_KIND_SWITCH = 0,
    UIFX_KIND_PX,       /* pixels */
    UIFX_KIND_NUM,      /* a bare count */
    UIFX_KIND_PCT,      /* a 0..1 float, shown as a percentage */
};

/*
 * Ranges are the same ones config.c clamps its parser to, so the panel cannot
 * reach a value a synuirc line could not. Notches are chosen so a full sweep of
 * the row is 20-40 presses: fine enough to land on the value you want, coarse
 * enough that crossing the range is not an endurance event.
 */
static const struct {
    const char *label;
    int   kind;
    float min, max, step;
} uifx_rows[UIFX_ROW_COUNT] = {
    [UIFX_ROW_CORNER]           = { "Rounded corners",  UIFX_KIND_PX,     0,  40,  2    },
    [UIFX_ROW_SHADOW]           = { "Drop shadow",      UIFX_KIND_SWITCH, 0,   1,  1    },
    [UIFX_ROW_SHADOW_SIZE]      = { "Shadow size",      UIFX_KIND_PX,     0,  80,  2    },
    [UIFX_ROW_SHADOW_SPREAD]    = { "Shadow spread",    UIFX_KIND_PX,     0,  64,  1    },
    [UIFX_ROW_SHADOW_DROP]      = { "Shadow drop",      UIFX_KIND_PX,   -32,  32,  1    },
    [UIFX_ROW_SHADOW_OPACITY]   = { "Shadow opacity",   UIFX_KIND_PCT,    0,   1,  0.05f},
    [UIFX_ROW_BLUR]             = { "Backdrop blur",    UIFX_KIND_SWITCH, 0,   1,  1    },
    [UIFX_ROW_BLUR_RADIUS]      = { "Blur radius",      UIFX_KIND_PX,     1,  20,  1    },
    [UIFX_ROW_BLUR_PASSES]      = { "Blur passes",      UIFX_KIND_NUM,    1,   5,  1    },
    [UIFX_ROW_HALO]             = { "Glass halo",       UIFX_KIND_PX,     0,  40,  2    },
    [UIFX_ROW_TRANSPARENCY]     = { "Translucency",     UIFX_KIND_SWITCH, 0,   1,  1    },
    /* 0.50 is transparency_set_opacity's own floor — a window that can go fully
     * invisible is a window you cannot find again. */
    [UIFX_ROW_OPACITY]          = { "Window opacity",   UIFX_KIND_PCT, 0.5f,   1,  0.02f},
};

/* Exactly one of *ip / *fp comes back non-NULL. The config fields are a mix of
 * int and float and it is not worth converting them to match: they are parsed
 * from synuirc and consumed by scenefx in the types they already have. */
/* Takes the CONFIG rather than the server so uifx_state_load_config() can run
 * inside synui_config_load(), where there is no server yet. */
static void uifx_field(syn_config_t *cfg, int row, int **ip, float **fp)
{
    *ip = NULL;
    *fp = NULL;

    switch (row) {
    case UIFX_ROW_CORNER:           *ip = &cfg->corner_radius;     break;
    case UIFX_ROW_SHADOW:           *ip = &cfg->shadow;            break;
    case UIFX_ROW_SHADOW_SIZE:      *fp = &cfg->shadow_blur_sigma; break;
    case UIFX_ROW_SHADOW_SPREAD:    *fp = &cfg->shadow_spread;     break;
    case UIFX_ROW_SHADOW_DROP:      *ip = &cfg->shadow_offset_y;   break;
    case UIFX_ROW_SHADOW_OPACITY:   *fp = &cfg->shadow_color[3];   break;
    case UIFX_ROW_BLUR:             *ip = &cfg->blur;              break;
    case UIFX_ROW_BLUR_RADIUS:      *ip = &cfg->blur_radius;       break;
    case UIFX_ROW_BLUR_PASSES:      *ip = &cfg->blur_passes;       break;
    case UIFX_ROW_HALO:             *ip = &cfg->glass_halo;        break;
    case UIFX_ROW_TRANSPARENCY:     *ip = &cfg->transparency;      break;
    case UIFX_ROW_OPACITY:          *fp = &cfg->active_opacity;    break;
    default: break;
    }
}

static float uifx_get(syn_server_t *s, int row)
{
    int *ip; float *fp;
    uifx_field(&s->config, row, &ip, &fp);
    if (ip) return (float)*ip;
    if (fp) return *fp;
    return 0.0f;
}

static void uifx_set(syn_server_t *s, int row, float v)
{
    int *ip; float *fp;
    uifx_field(&s->config, row, &ip, &fp);
    if (ip) *ip = (int)lroundf(v);
    else if (fp) *fp = v;
}

const char *uifx_row_label(int row)
{
    if (row < 0 || row >= UIFX_ROW_COUNT) return "?";
    return uifx_rows[row].label;
}

/* ── What is and is not live ─────────────────────────────────
 *
 * Every one of these rows has a state in which it edits a real value that
 * changes nothing on screen, and each of those states has a different cause.
 * Naming the cause is the whole difference between "this slider is broken" and
 * "the switch two rows up is off".
 */
const char *uifx_row_inert(syn_server_t *s, int row)
{
    const syn_config_t *cfg = &s->config;

    switch (row) {
    case UIFX_ROW_CORNER:
        /* chrome_corner_radius() forces 0 for the retro styles: a Windows 95
         * window with 12px corners is instantly wrong, so the chrome overrides
         * the setting rather than overwriting it. */
        return chrome_corner_radius(cfg) == 0 && cfg->corner_radius > 0
             ? "retro chrome is square" : NULL;
    case UIFX_ROW_SHADOW:
        /* 95 sat flat on the desktop; chrome_shadow() drops it. */
        return cfg->chrome == SYN_CHROME_BEVEL && cfg->shadow
             ? "95 chrome has no shadow" : NULL;
    case UIFX_ROW_SHADOW_SIZE:
    case UIFX_ROW_SHADOW_SPREAD:
    case UIFX_ROW_SHADOW_DROP:
    case UIFX_ROW_SHADOW_OPACITY:
        return chrome_shadow(cfg) ? NULL : "shadow is off";
    case UIFX_ROW_BLUR_RADIUS:
    case UIFX_ROW_BLUR_PASSES:
        return cfg->blur ? NULL : "blur is off";
    case UIFX_ROW_HALO:
        return cfg->blur ? NULL : "blur is off";
    case UIFX_ROW_OPACITY:
        return cfg->transparency ? NULL : "translucency is off";
    default:
        return NULL;
    }
}

const char *uifx_note(syn_server_t *s)
{
    const syn_config_t *cfg = &s->config;

    /* Only the whole-page facts belong here — a single off switch is already
     * said by the greyed rows under it. The chrome style is the one that is
     * invisible from this panel, because it is set from Super+T. */
    if (cfg->chrome == SYN_CHROME_BEVEL)
        return "95 chrome \xc2\xb7 corners square and shadow off, whatever these say";
    if (cfg->chrome != SYN_CHROME_FLAT)
        return "retro chrome \xc2\xb7 corners are square, whatever this says";
    return NULL;
}

/* ── Reading a row ───────────────────────────────────────── */

float uifx_row_value(syn_server_t *s, int row, char *buf, size_t n)
{
    if (row < 0 || row >= UIFX_ROW_COUNT) {
        snprintf(buf, n, "?");
        return -1.0f;
    }
    float v = uifx_get(s, row);

    if (uifx_rows[row].kind == UIFX_KIND_SWITCH) {
        snprintf(buf, n, "%s", v != 0.0f ? "on" : "off");
        return -1.0f;    /* a word, not a bar */
    }

    switch (uifx_rows[row].kind) {
    case UIFX_KIND_PX:  snprintf(buf, n, "%d px", (int)lroundf(v));       break;
    case UIFX_KIND_PCT: snprintf(buf, n, "%d%%", (int)(v * 100.0f + 0.5f)); break;
    default:            snprintf(buf, n, "%d", (int)lroundf(v));          break;
    }

    float lo = uifx_rows[row].min, hi = uifx_rows[row].max;
    float frac = (v - lo) / (hi - lo);
    return frac < 0.0f ? 0.0f : frac > 1.0f ? 1.0f : frac;
}

/* ── Pushing a row back out ──────────────────────────────── */

/*
 * Three separate paths, because the values land in three different places:
 *
 *   - blur_* are ONE global scene setting, pushed with wlr_scene_set_blur_data
 *     (synui_main.c does this once at init; here is the only other caller).
 *   - corner_radius, the per-buffer blur and the opacities are carried on each
 *     buffer node, which is anim.c's walk.
 *   - the shadow, the halo and the border ring are frame-level nodes rebuilt by
 *     view_update_decorations.
 *
 * Deliberately NOT deco_refresh_all(): that re-runs view_resize on every
 * window, which sends a configure to every client. Nothing on this page changes
 * a window's *metrics* — the shadow lives outside the frame and the corner
 * radius is a render property — so the clients have nothing to hear about, and
 * a configure storm per keypress on a held-down arrow key is its own bug.
 */
void uifx_apply(syn_server_t *s)
{
    const syn_config_t *cfg = &s->config;

    wlr_scene_set_blur_data(s->scene, cfg->blur_passes, cfg->blur_radius,
                            cfg->blur_noise, cfg->blur_brightness,
                            cfg->blur_contrast, cfg->blur_saturation);

    anim_apply_alpha_all(s);

    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link) {
            /* Unsized views pick the current values up at their first layout. */
            if (!v->mapped || v->w <= 0 || v->h <= 0) continue;
            view_update_decorations(v);
        }
    }

    /* An idle desktop generates no damage, so without this the panel would show
     * a number the screen disagrees with — and a shadow turned OFF would leave
     * its last frame sitting there. Same rule as filters_repaint(). */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (o->scene_output)
            wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
        wlr_output_schedule_frame(o->wlr_output);
    }
}

/*
 * The two translucency rows are theme.c's, not this page's, and going around it
 * would break three things at once: the focused level drives the unfocused one
 * through inactive_from_active(), foot is excluded from the compositor fade and
 * needs its own alpha pushed (glass_push), and both levels are persisted in
 * theme.state — which is loaded AFTER uifx.state at startup, so a value written
 * here would be silently reverted on the next login. Hand them over instead.
 */
static bool uifx_owned_by_theme(int row)
{
    return row == UIFX_ROW_TRANSPARENCY || row == UIFX_ROW_OPACITY;
}

void uifx_adjust(syn_server_t *s, int dir)
{
    int row = s->filters.uifx_selected;
    if (row < 0 || row >= UIFX_ROW_COUNT) return;

    float v = uifx_get(s, row);
    float next;

    if (uifx_rows[row].kind == UIFX_KIND_SWITCH) {
        next = (v != 0.0f) ? 0.0f : 1.0f;   /* both directions flip it */
    } else {
        next = v + dir * uifx_rows[row].step;
        if (next < uifx_rows[row].min) next = uifx_rows[row].min;
        if (next > uifx_rows[row].max) next = uifx_rows[row].max;
        if (next == v) return;              /* already at an end of the range */
    }

    if (uifx_owned_by_theme(row)) {
        /* These apply and save themselves, so no uifx_dirty and no uifx_apply:
         * the change is already on screen and already on disk. */
        if (row == UIFX_ROW_TRANSPARENCY) transparency_set_enabled(s, next != 0.0f);
        else                              transparency_set_opacity(s, next);

        char tval[32];
        uifx_row_value(s, row, tval, sizeof(tval));
        const char *twhy = uifx_row_inert(s, row);
        snprintf(s->filters.status, sizeof(s->filters.status), "%s: %s%s%s",
                 uifx_rows[row].label, tval, twhy ? " \xc2\xb7 " : "",
                 twhy ? twhy : "");
        return;
    }

    uifx_set(s, row, next);
    s->filters.uifx_dirty = 1;

    char val[32];
    uifx_row_value(s, row, val, sizeof(val));

    const char *why = uifx_row_inert(s, row);
    if (why)
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "%s: %s \xc2\xb7 %s", uifx_rows[row].label, val, why);
    else
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "%s: %s", uifx_rows[row].label, val);

    uifx_apply(s);
}

/*
 * The switch that decides whether a row does anything. Space uses it: on the CRT
 * page Space is the master on/off from any row, and the equivalent here is "turn
 * on the thing this row belongs to" — pressing it on a greyed Shadow size row
 * should light the shadow, not do nothing because the row is not itself a
 * switch. Rows that ARE switches govern themselves; Rounded corners has no
 * switch at all (0 px is how you turn it off) and answers -1.
 */
static int uifx_governor(int row)
{
    switch (row) {
    case UIFX_ROW_SHADOW:
    case UIFX_ROW_SHADOW_SIZE:
    case UIFX_ROW_SHADOW_SPREAD:
    case UIFX_ROW_SHADOW_DROP:
    case UIFX_ROW_SHADOW_OPACITY:   return UIFX_ROW_SHADOW;
    case UIFX_ROW_BLUR:
    case UIFX_ROW_BLUR_RADIUS:
    case UIFX_ROW_BLUR_PASSES:
    case UIFX_ROW_HALO:             return UIFX_ROW_BLUR;
    case UIFX_ROW_TRANSPARENCY:
    case UIFX_ROW_OPACITY:          return UIFX_ROW_TRANSPARENCY;
    default:                        return -1;
    }
}

void uifx_space(syn_server_t *s)
{
    int gov = uifx_governor(s->filters.uifx_selected);
    if (gov < 0) {
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "%s has no switch \xc2\xb7 Left to 0 px turns it off",
                 uifx_row_label(s->filters.uifx_selected));
        return;
    }

    int on = uifx_get(s, gov) == 0.0f;
    if (uifx_owned_by_theme(gov)) {
        transparency_set_enabled(s, on);    /* applies and saves itself */
    } else {
        uifx_set(s, gov, on ? 1.0f : 0.0f);
        s->filters.uifx_dirty = 1;
        uifx_apply(s);
    }
    snprintf(s->filters.status, sizeof(s->filters.status),
             "%s %s", uifx_row_label(gov), on ? "on" : "off");
}

/* ── Persisted state ─────────────────────────────────────── */
/*
 * Its own file rather than a section of filters.state: the two pages are saved
 * independently (`s` saves the page you are on), and a shared file would mean
 * saving a tuned shadow also wrote back whatever the CRT page happened to hold.
 * Loaded over the config defaults at startup, like every other .state here.
 */

static bool uifx_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "uifx.state");
}

/* The key each row is stored under. These are the synuirc key names wherever one
 * exists, so a value read out of this file can be pasted straight into a config
 * — the panel and the config file stay one vocabulary. NULL means the row is not
 * ours to persist: the translucency pair lives in theme.state, written by
 * transparency_set_*(), and storing a second copy here would only give startup
 * two answers and let the later loader win. */
static const char *uifx_key(int row)
{
    switch (row) {
    case UIFX_ROW_CORNER:           return "corner_radius";
    case UIFX_ROW_SHADOW:           return "shadow";
    case UIFX_ROW_SHADOW_SIZE:      return "shadow_blur_sigma";
    case UIFX_ROW_SHADOW_SPREAD:    return "shadow_spread";
    case UIFX_ROW_SHADOW_DROP:      return "shadow_offset_y";
    case UIFX_ROW_SHADOW_OPACITY:   return "shadow_opacity";
    case UIFX_ROW_BLUR:             return "blur";
    case UIFX_ROW_BLUR_RADIUS:      return "blur_radius";
    case UIFX_ROW_BLUR_PASSES:      return "blur_passes";
    case UIFX_ROW_HALO:             return "glass_halo";
    default:                        return NULL;   /* incl. the theme-owned pair */
    }
}

void uifx_state_save(syn_server_t *s)
{
    char path[256];
    if (!uifx_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: uifx: cannot write '%s': %s",
                path, strerror(errno));
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "save failed: %s", strerror(errno));
        return;
    }
    for (int row = 0; row < UIFX_ROW_COUNT; row++) {
        const char *key = uifx_key(row);
        if (!key) continue;
        int *ip; float *fp;
        uifx_field(&s->config, row, &ip, &fp);
        if (ip)      fprintf(f, "%s=%d\n", key, *ip);
        else if (fp) fprintf(f, "%s=%.3f\n", key, *fp);
    }
    fclose(f);

    s->filters.uifx_dirty = 0;
    snprintf(s->filters.status, sizeof(s->filters.status), "saved");
    wlr_log(WLR_INFO, "synui: uifx: saved to %s", path);
}

/*
 * Read from synui_config_load()'s tail, for the reason filters.c spells out at
 * length: synui_config_reload() replaces s->config wholesale, so anything this
 * file owns that is only loaded at startup is thrown away by every reload —
 * SIGHUP, Ctrl+Shift+R, and super+shift+w. The server half is uifx_apply(),
 * which the reload owes because these values live in the scene graph.
 */
void uifx_state_load_config(syn_config_t *cfg)
{
    char path[256];
    if (!uifx_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;    /* never saved — not an error */

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        for (int row = 0; row < UIFX_ROW_COUNT; row++) {
            const char *k = uifx_key(row);
            if (!k || strcmp(k, key) != 0) continue;
            /* Clamp to the row's own range on the way in: a hand-edited file is
             * the one path that can carry a value the panel could not reach. */
            float v = strtof(val, NULL);
            if (v < uifx_rows[row].min) v = uifx_rows[row].min;
            if (v > uifx_rows[row].max) v = uifx_rows[row].max;

            int *ip; float *fp;
            uifx_field(cfg, row, &ip, &fp);
            if (ip)      *ip = (int)lroundf(v);
            else if (fp) *fp = v;
            break;
        }
    }
    fclose(f);
}
