/*
 * filters.c — the Super+E panel for the CRT post-process filters.
 *
 * effects.c owns the GLES pass; this owns the knobs. Super+E used to be a blind
 * on/off toggle, which left the four strengths (scanline, curvature, chromatic
 * aberration, glitch) editable only by hand-editing synuirc and restarting —
 * for settings whose whole point is that you tune them by eye.
 *
 * The pass samples s->config.effect_* fresh every frame (see effects.c), so a
 * slider needs to do nothing but move the float: the next repaint already shows
 * it. What it does have to do is *force* that repaint — an idle desktop has no
 * damage, so without damaging the outputs the screen would sit there showing the
 * old strength until something else happened to redraw.
 *
 * Modelled on power.c, down to the keys (Up/Down select, Left/Right adjust,
 * Space toggle, s save, Esc close), because a second panel that worked a second
 * way would be its own small bug.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"

/* One notch of Left/Right. Twenty steps end to end: fine enough to tune a
 * scanline by eye, coarse enough to cross the range without going numb. */
#define FILTER_STEP 0.05f

static float clamp01f(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/* The strength a row edits. Rows with no float — the master switch and the
 * discrete phosphor selector — map to NULL, and callers must check. */
static float *row_field(syn_server_t *s, int row)
{
    switch (row) {
    case FILTER_ROW_SCANLINE:  return &s->config.effect_scanline;
    case FILTER_ROW_CURVATURE: return &s->config.effect_curvature;
    case FILTER_ROW_ABERRATION:return &s->config.effect_aberration;
    case FILTER_ROW_GLITCH:    return &s->config.effect_glitch;
    case FILTER_ROW_MONO:      return &s->config.effect_mono;
    case FILTER_ROW_BLOOM:     return &s->config.effect_bloom;
    default:                   return NULL;
    }
}

/* The phosphor tint's name, for the selector row and the save status. */
static const char *phosphor_name(int ph)
{
    switch (ph) {
    case SYN_PHOSPHOR_GREEN: return "green";
    case SYN_PHOSPHOR_AMBER: return "amber";
    case SYN_PHOSPHOR_WHITE: return "white";
    default:                 return "off";
    }
}

const char *filters_row_label(int row)
{
    switch (row) {
    case FILTER_ROW_ENABLED:   return "CRT filters";
    case FILTER_ROW_SCANLINE:  return "Scanlines";
    case FILTER_ROW_CURVATURE: return "Screen curvature";
    case FILTER_ROW_ABERRATION:return "Chromatic aberration";
    case FILTER_ROW_GLITCH:    return "Glitch strength";
    case FILTER_ROW_PHOSPHOR:  return "Phosphor";
    case FILTER_ROW_MONO:      return "Monochrome";
    case FILTER_ROW_BLOOM:     return "Phosphor bloom";
    default:                   return "?";
    }
}

/* The value a row shows, and (for the slider rows) its 0..1 fraction. Returns
 * -1.0f for the master switch, which renders as a word, not a bar. */
float filters_row_value(syn_server_t *s, int row, char *buf, size_t n)
{
    if (row == FILTER_ROW_ENABLED) {
        snprintf(buf, n, "%s", s->config.effects ? "on" : "off");
        return -1.0f;
    }
    if (row == FILTER_ROW_PHOSPHOR) {   /* a word, not a bar */
        snprintf(buf, n, "%s", phosphor_name(s->config.effect_phosphor));
        return -1.0f;
    }
    float v = *row_field(s, row);
    snprintf(buf, n, "%3d%%", (int)(v * 100.0f + 0.5f));
    return v;
}

/* Nothing here changes the scene graph, so nothing here would repaint on its
 * own. Force it, or the panel shows a number the screen does not agree with. */
static void filters_repaint(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (o->scene_output)
            wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
        wlr_output_schedule_frame(o->wlr_output);
    }
}

/* ── Persisted state ─────────────────────────────────────── */

static bool filters_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "filters.state");
}

void filters_state_save(syn_server_t *s)
{
    char path[256];
    if (!filters_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: filters: cannot write '%s': %s",
                path, strerror(errno));
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "save failed: %s", strerror(errno));
        return;
    }
    fprintf(f, "enabled=%d\n",    s->config.effects ? 1 : 0);
    fprintf(f, "scanline=%.3f\n",   s->config.effect_scanline);
    fprintf(f, "curvature=%.3f\n",  s->config.effect_curvature);
    fprintf(f, "aberration=%.3f\n", s->config.effect_aberration);
    fprintf(f, "glitch=%.3f\n",     s->config.effect_glitch);
    fprintf(f, "phosphor=%s\n",     phosphor_name(s->config.effect_phosphor));
    fprintf(f, "mono=%.3f\n",       s->config.effect_mono);
    fprintf(f, "bloom=%.3f\n",      s->config.effect_bloom);
    fclose(f);

    s->filters.dirty = 0;
    snprintf(s->filters.status, sizeof(s->filters.status), "saved");
    wlr_log(WLR_INFO, "synui: filters: saved to %s", path);
}

/* Applied over the config defaults at startup, so a tuned look survives a
 * restart. Absent file is not an error — it just means "never saved". */
void filters_state_load(syn_server_t *s)
{
    char path[256];
    if (!filters_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    syn_config_t *cfg = &s->config;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if      (strcmp(key, "enabled") == 0)    cfg->effects = atoi(val) ? 1 : 0;
        else if (strcmp(key, "scanline") == 0)   cfg->effect_scanline   = clamp01f(strtof(val, NULL));
        else if (strcmp(key, "curvature") == 0)  cfg->effect_curvature  = clamp01f(strtof(val, NULL));
        else if (strcmp(key, "aberration") == 0) cfg->effect_aberration = clamp01f(strtof(val, NULL));
        else if (strcmp(key, "glitch") == 0)     cfg->effect_glitch     = clamp01f(strtof(val, NULL));
        else if (strcmp(key, "mono") == 0)       cfg->effect_mono       = clamp01f(strtof(val, NULL));
        else if (strcmp(key, "bloom") == 0)      cfg->effect_bloom      = clamp01f(strtof(val, NULL));
        else if (strcmp(key, "phosphor") == 0) {
            if      (strcmp(val, "green") == 0) cfg->effect_phosphor = SYN_PHOSPHOR_GREEN;
            else if (strcmp(val, "amber") == 0) cfg->effect_phosphor = SYN_PHOSPHOR_AMBER;
            else if (strcmp(val, "white") == 0) cfg->effect_phosphor = SYN_PHOSPHOR_WHITE;
            else                                cfg->effect_phosphor = SYN_PHOSPHOR_OFF;
        }
    }
    fclose(f);
}

/* ── Panel ───────────────────────────────────────────────── */

void filters_show(syn_server_t *s)
{
    s->filters.visible   = 1;
    s->filters.selected  = FILTER_ROW_SCANLINE;   /* the knob people came for */
    s->filters.status[0] = '\0';
    synui_render_filters(s);
}

void filters_hide(syn_server_t *s)
{
    s->filters.visible = 0;
    synui_render_filters(s);
}

void filters_toggle(syn_server_t *s)
{
    if (s->filters.visible) filters_hide(s);
    else                    filters_show(s);
}

static void filters_adjust(syn_server_t *s, int dir)
{
    if (s->filters.selected == FILTER_ROW_PHOSPHOR) {
        /* Cycle Off → green → amber → white → Off, both directions. */
        int cur = s->config.effect_phosphor;
        cur = (cur + dir + SYN_PHOSPHOR_COUNT) % SYN_PHOSPHOR_COUNT;
        s->config.effect_phosphor = cur;
        s->filters.dirty = 1;
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "Phosphor: %s", phosphor_name(cur));
        /* An off master switch, or a zeroed monochrome amount, means the tint
         * moves a word and nothing on screen — say which. */
        if (!s->config.effects && cur != SYN_PHOSPHOR_OFF)
            snprintf(s->filters.status, sizeof(s->filters.status),
                     "Phosphor: %s \xc2\xb7 filters are off (Space on the top row)",
                     phosphor_name(cur));
        else if (s->config.effect_mono <= 0.0f && cur != SYN_PHOSPHOR_OFF)
            snprintf(s->filters.status, sizeof(s->filters.status),
                     "Phosphor: %s \xc2\xb7 Monochrome is at 0%%",
                     phosphor_name(cur));
        filters_repaint(s);
        return;
    }

    float *field = row_field(s, s->filters.selected);
    if (!field) {                       /* the master switch row */
        s->config.effects = !s->config.effects;
        s->filters.dirty = 1;
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "filters %s", s->config.effects ? "on" : "off");
        filters_repaint(s);
        return;
    }

    float next = clamp01f(*field + dir * FILTER_STEP);
    if (next == *field) return;         /* already at an end of the range */
    *field = next;
    s->filters.dirty = 1;

    char v[32];
    snprintf(v, sizeof(v), "%d%%", (int)(next * 100.0f + 0.5f));
    snprintf(s->filters.status, sizeof(s->filters.status), "%s: %s",
             filters_row_label(s->filters.selected), v);

    /* Turning a slider up while the master switch is off would move a number
     * and change nothing on screen. Say so rather than let it look broken. */
    if (!s->config.effects)
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "%s: %s \xc2\xb7 filters are off (Space on the top row)",
                 filters_row_label(s->filters.selected), v);
    /* Bloom is the phosphor glow: with no monochrome amount there is no
     * phosphor to bleed, so the slider moves a number and nothing else. */
    else if (s->filters.selected == FILTER_ROW_BLOOM && s->config.effect_mono <= 0.0f)
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "%s: %s \xc2\xb7 Monochrome is at 0%%", filters_row_label(s->filters.selected), v);

    filters_repaint(s);
}

int filters_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->filters.visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        filters_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->filters.selected > 0) s->filters.selected--;
        synui_render_filters(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->filters.selected < FILTER_ROW_COUNT - 1) s->filters.selected++;
        synui_render_filters(s);
        return 1;
    case XKB_KEY_Left:
    case XKB_KEY_h:
        filters_adjust(s, -1);
        synui_render_filters(s);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        filters_adjust(s, +1);
        synui_render_filters(s);
        return 1;
    case XKB_KEY_space:
        /* On any row, Space is the master on/off — the one control you want
         * without having to first navigate to the top row. */
        s->config.effects = !s->config.effects;
        s->filters.dirty = 1;
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "filters %s", s->config.effects ? "on" : "off");
        filters_repaint(s);
        synui_render_filters(s);
        return 1;
    case XKB_KEY_s:
        filters_state_save(s);
        synui_render_filters(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
