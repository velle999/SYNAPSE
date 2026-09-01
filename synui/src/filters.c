/*
 * filters.c — the Super+E panel: CRT post-process filters, and (page two, in
 * uifx.c) the window effects.
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
 * Tab switches to the window-effects page (uifx.c) and back. The two pages are
 * one panel rather than two binds because they answer the same question — "how
 * does this desktop look, and let me turn that while watching it" — and because
 * Super+E is already the key people learned for it. Everything below dispatches
 * on fl->page; the uifx page owns its own cursor, dirty flag and state file, so
 * the only thing shared is the frame and the status line.
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
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "i18n.h"
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
    case FILTER_ROW_LIFT:      return &s->config.effect_lift;
    case FILTER_ROW_HUE:       return &s->config.effect_hue;
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
    case FILTER_ROW_ENABLED:   return _("CRT filters");
    case FILTER_ROW_SCANLINE:  return _("Scanlines");
    case FILTER_ROW_CURVATURE: return _("Screen curvature");
    case FILTER_ROW_ABERRATION:return _("Chromatic aberration");
    case FILTER_ROW_GLITCH:    return _("Glitch strength");
    case FILTER_ROW_PHOSPHOR:  return _("Phosphor");
    case FILTER_ROW_MONO:      return _("Monochrome");
    case FILTER_ROW_BLOOM:     return _("Phosphor bloom");
    case FILTER_ROW_LIFT:      return _("Phosphor lift");
    case FILTER_ROW_HUE:       return _("Phosphor hue");
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
    /* Hue is the one row whose number is not a strength: it is a rotation of
     * the phosphor's own colour, centred on the preset, so it reads in degrees
     * off that. A percentage here would say 45% of nothing. The bar still
     * fills 0..1, which puts the preset at its middle. */
    if (row == FILTER_ROW_HUE)
        snprintf(buf, n, "%+4d\xc2\xb0",
                 (int)((v - 0.5f) * 2.0f * SYN_PHOSPHOR_HUE_RANGE +
                       (v < 0.5f ? -0.5f : 0.5f)));
    else
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
    fprintf(f, "lift=%.3f\n",       s->config.effect_lift);
    fprintf(f, "hue=%.3f\n",        s->config.effect_hue);
    fclose(f);

    s->filters.dirty = 0;
    snprintf(s->filters.status, sizeof(s->filters.status), "saved");
    wlr_log(WLR_INFO, "synui: filters: saved to %s", path);
}

/*
 * Applied over the config defaults, so a tuned look survives a restart.
 * Absent file is not an error — it just means "never saved".
 *
 * Takes the CONFIG, not the server, and is read from synui_config_load()'s tail
 * rather than once from synui_main(). That is the whole difference between a
 * reload keeping the look and a reload throwing it away: synui_config_reload()
 * does `s->config = fresh`, so every effect_* field comes back from the sources
 * synui_config_load() reads and from nowhere else. Loaded from startup only,
 * this file lost every reload — Ctrl+Shift+R in the shortcut palette put CRT
 * effects back ON (synuirc ships `effects = on`) in whatever phosphor
 * settings.state was carrying. Same trap, same fix as theme.state; see theme.c.
 *
 * Nothing here needs a server-side apply half: effects.c samples s->config
 * every frame, and a reload repaints regardless.
 */
void filters_state_load_config(syn_config_t *cfg)
{
    char path[256];
    if (!filters_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

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
        else if (strcmp(key, "lift") == 0)       cfg->effect_lift       = clamp01f(strtof(val, NULL));
        else if (strcmp(key, "hue") == 0)        cfg->effect_hue        = clamp01f(strtof(val, NULL));
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
    /* The page is deliberately NOT reset: it stays where you left it, so tuning
     * a shadow over several sessions does not cost a Tab each time. It does mean
     * the other page has to announce itself, or Super+E would look like it had
     * quietly become a different panel. */
    if (s->filters.page == FILTER_PAGE_UIFX)
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "window effects \xc2\xb7 Tab for CRT filters");
    synui_render_filters(s);
}

void filters_hide(syn_server_t *s)
{
    s->filters.visible = 0;
    synui_render_filters(s);
    ctlpanel_child_closed(s, "filters");
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
    if (s->filters.selected == FILTER_ROW_HUE)
        filters_row_value(s, FILTER_ROW_HUE, v, sizeof(v));   /* degrees */
    else
        snprintf(v, sizeof(v), "%d%%", (int)(next * 100.0f + 0.5f));
    snprintf(s->filters.status, sizeof(s->filters.status), "%s: %s",
             filters_row_label(s->filters.selected), v);

    /* Turning a slider up while the master switch is off would move a number
     * and change nothing on screen. Say so rather than let it look broken. */
    if (!s->config.effects)
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "%s: %s \xc2\xb7 filters are off (Space on the top row)",
                 filters_row_label(s->filters.selected), v);
    /* Bloom is the phosphor glow, lift its transfer curve and hue its colour:
     * with no monochrome amount there is no phosphor for any of them to act on,
     * so the slider moves a number and nothing else. */
    else if ((s->filters.selected == FILTER_ROW_BLOOM ||
              s->filters.selected == FILTER_ROW_LIFT  ||
              s->filters.selected == FILTER_ROW_HUE) &&
             s->config.effect_mono <= 0.0f)
        snprintf(s->filters.status, sizeof(s->filters.status),
                 "%s: %s \xc2\xb7 Monochrome is at 0%%", filters_row_label(s->filters.selected), v);

    filters_repaint(s);
}

/* The window-effects page, same keys against uifx.c's rows. Unmodified keys
 * only — the caller has already handled Tab and let modified combos through. */
static int filters_key_uifx(syn_server_t *s, xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        filters_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->filters.uifx_selected > 0) s->filters.uifx_selected--;
        break;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->filters.uifx_selected < UIFX_ROW_COUNT - 1) s->filters.uifx_selected++;
        break;
    case XKB_KEY_Left:
    case XKB_KEY_h:
        uifx_adjust(s, -1);
        break;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        uifx_adjust(s, +1);
        break;
    case XKB_KEY_space:
        uifx_space(s);
        break;
    case XKB_KEY_s:
        uifx_state_save(s);
        break;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
    synui_render_filters(s);
    return 1;
}

/* Tab, both ways. With two pages the direction does not matter yet, but the
 * cycle is written as one so a third page needs no new key. */
static void filters_page_cycle(syn_server_t *s, int dir)
{
    syn_filters_t *fl = &s->filters;
    fl->page = (fl->page + dir + FILTER_PAGE_COUNT) % FILTER_PAGE_COUNT;
    snprintf(fl->status, sizeof(fl->status), "%s",
             fl->page == FILTER_PAGE_UIFX ? "window effects \xc2\xb7 Tab for CRT filters"
                                          : "CRT filters \xc2\xb7 Tab for window effects");
    synui_render_filters(s);
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h. This panel's Enter is a second
 * spelling of Esc, so a left click is Right (step the value on) and a right
 * click is Left — clicking a slider row to close the panel would be nothing but
 * a way to lose your place.
 *
 * Both pages share these: the row cursor they move is chosen by fl->page, the
 * same way filters_key does it, so switching page cannot leave the pointer
 * driving the other page's selection. */

/* The selected-row cursor for whichever page is up. */
static int *filters_cursor(syn_server_t *s)
{
    return s->filters.page == FILTER_PAGE_UIFX ? &s->filters.uifx_selected
                                               : &s->filters.selected;
}

static void filters_page_adjust(syn_server_t *s, int dir)
{
    if (s->filters.page == FILTER_PAGE_UIFX) uifx_adjust(s, dir);
    else                                     filters_adjust(s, dir);
}

int filters_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->filters.visible) return 0;

    int row = hit_row_at(&s->filters.hit, lx, ly);
    int *cur = filters_cursor(s);
    if (row < 0 || row == *cur) return 1;
    *cur = row;
    synui_render_filters(s);
    return 1;
}

int filters_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    if (!s->filters.visible) return 0;

    if (!hit_in_panel(&s->filters.hit, lx, ly)) {
        filters_hide(s);
        return 1;
    }

    filters_motion(s, lx, ly);            /* act on the row pointed at */

    if (hit_row_at(&s->filters.hit, lx, ly) < 0) return 1;   /* chrome */
    if (button != BTN_LEFT && button != BTN_RIGHT) return 1;

    filters_page_adjust(s, button == BTN_LEFT ? +1 : -1);
    synui_render_filters(s);
    return 1;
}

int filters_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->filters.visible) return 0;
    if (delta == 0) return 1;

    int rows = s->filters.page == FILTER_PAGE_UIFX ? UIFX_ROW_COUNT
                                                   : FILTER_ROW_COUNT;
    int *cur = filters_cursor(s);
    int next = *cur + (delta > 0 ? 1 : -1);
    if (next < 0 || next >= rows) return 1;   /* stop at the ends, as Up/Down do */
    *cur = next;
    synui_render_filters(s);
    return 1;
}

int filters_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->filters.visible) return 0;

    /* Shift+Tab is the one modified key this panel claims — it is the other half
     * of Tab, and no global bind uses it. Everything else modified (Super+…)
     * still reaches the bind table. */
    if (sym == XKB_KEY_ISO_Left_Tab &&
        !(mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))) {
        filters_page_cycle(s, -1);
        return 1;
    }

    /* Modified combos (Super+…) still reach the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    if (sym == XKB_KEY_Tab) {
        filters_page_cycle(s, +1);
        return 1;
    }

    if (s->filters.page == FILTER_PAGE_UIFX)
        return filters_key_uifx(s, sym);

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
