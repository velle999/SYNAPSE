/*
 * dispcfg.c — built-in display settings panel
 *
 * A compositor-drawn menu (Super+D, or "Display Settings" on the welcome
 * menu) for configuring monitors without external tools:
 *
 *   Up/Down (j/k)          select a monitor
 *   Left/Right (h/l)       rotate it (normal → 90° → 180° → 270°)
 *   Shift+arrows           move it one cell in the arrangement grid
 *                          (swaps with whatever monitor is already there)
 *   Enter / Esc / q        close
 *
 * Monitors live on a logical 2D grid (syn_output_t::grid_x/grid_y), not a
 * single row or column — this is what lets an L-shaped desk (e.g. a
 * portrait monitor beside the main one, with a third stacked above the
 * portrait one) actually work: dispcfg_rechain() packs rows and columns
 * from real edges instead of assuming everything lines up in one line, so
 * the cursor crosses between outputs where the desk visually says it
 * should.
 *
 * Every change applies immediately: the transform is committed to the
 * output, then every output is re-flowed from the grid and the usual
 * post-apply reflow runs (output_layout_changed — the same path the
 * wlr-randr protocol handler uses).
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include <drm_fourcc.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "synui.h"

static syn_output_t *selected_output(syn_server_t *s)
{
    syn_dispcfg_t *d = &s->dispcfg;
    if (d->selected < 0 || d->selected >= d->count) return NULL;
    return d->order[d->selected];
}

/* Seed order[] from the current outputs, sorted by grid cell (grid_y then
 * grid_x) so the panel lists monitors in reading order — top row first,
 * left to right within each row. */
static void dispcfg_seed(syn_server_t *s)
{
    syn_dispcfg_t *d = &s->dispcfg;
    d->count = 0;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (d->count >= DISPCFG_MAX_OUTPUTS) break;
        d->order[d->count++] = o;
    }

    /* Insertion sort by (grid_y, grid_x). */
    for (int i = 1; i < d->count; i++) {
        syn_output_t *cur = d->order[i];
        int j = i - 1;
        while (j >= 0 &&
               (d->order[j]->grid_y > cur->grid_y ||
                (d->order[j]->grid_y == cur->grid_y &&
                 d->order[j]->grid_x > cur->grid_x))) {
            d->order[j + 1] = d->order[j];
            j--;
        }
        d->order[j + 1] = cur;
    }

    if (d->selected >= d->count)
        d->selected = d->count ? d->count - 1 : 0;
    if (d->selected < 0)
        d->selected = 0;
}

/* Collect the distinct values of one grid axis, ascending. Returns the count. */
static int dispcfg_tracks(syn_dispcfg_t *d, bool axis_x, int *out)
{
    int n = 0;
    for (int i = 0; i < d->count; i++) {
        int v = axis_x ? d->order[i]->grid_x : d->order[i]->grid_y;
        int j;
        for (j = 0; j < n; j++)
            if (out[j] == v) break;
        if (j == n) out[n++] = v;
    }
    for (int i = 1; i < n; i++) {
        int key = out[i], j = i - 1;
        while (j >= 0 && out[j] > key) { out[j + 1] = out[j]; j--; }
        out[j + 1] = key;
    }
    return n;
}

/* Re-flow every output's pixel position from its logical grid cell (using
 * transform-aware effective sizes), then run the shared post-apply reflow.
 *
 * This is a CSS-grid-style pack with auto tracks: each distinct grid_y is a
 * row as tall as the tallest output in it, each distinct grid_x is a column
 * as wide as the widest output in it, and an output lands at the top-left of
 * its cell. Tracks are sized across the *whole* grid, not per row, so a
 * column means the same x on every row — that is what makes an L-shaped desk
 * work: main monitor at (2,0), a portrait one at (1,0) to its left, and a
 * third at (2,-1) stacked above the main one lands above the MAIN monitor,
 * not above the narrow portrait one. Packing each row independently from
 * x=0 (what this used to do) put that third monitor at x=0, so the cursor
 * crossed into it off the portrait monitor's top edge instead of off the
 * main monitor's, and windows landed a screen to the left of where the desk
 * said they should.
 *
 * Sizing per column rather than stretching to the widest row also keeps the
 * portrait monitor's narrow width its own — nothing is padded out to match
 * a wider neighbour on another row. */
static void dispcfg_rechain(syn_server_t *s)
{
    syn_dispcfg_t *d = &s->dispcfg;
    if (d->count == 0) return;

    int rows[DISPCFG_MAX_OUTPUTS], cols[DISPCFG_MAX_OUTPUTS];
    int nrows = dispcfg_tracks(d, false, rows);
    int ncols = dispcfg_tracks(d, true,  cols);

    /* Track origins: each row starts below the previous row's tallest
     * output, each column right of the previous column's widest. */
    int row_y[DISPCFG_MAX_OUTPUTS], col_x[DISPCFG_MAX_OUTPUTS];
    int y = 0;
    for (int r = 0; r < nrows; r++) {
        int row_h = 0;
        for (int i = 0; i < d->count; i++) {
            if (d->order[i]->grid_y != rows[r]) continue;
            int w, h;
            wlr_output_effective_resolution(d->order[i]->wlr_output, &w, &h);
            if (h > row_h) row_h = h;
        }
        row_y[r] = y;
        y += row_h;
    }
    int x = 0;
    for (int c = 0; c < ncols; c++) {
        int col_w = 0;
        for (int i = 0; i < d->count; i++) {
            if (d->order[i]->grid_x != cols[c]) continue;
            int w, h;
            wlr_output_effective_resolution(d->order[i]->wlr_output, &w, &h);
            if (w > col_w) col_w = w;
        }
        col_x[c] = x;
        x += col_w;
    }

    /* Place each output at its cell origin. dispcfg_move() swaps rather than
     * stacks, so one output per cell is the normal case — but a saved layout
     * could still name the same cell twice, and two outputs at identical
     * coordinates would leave one of them unreachable by the cursor. Shift
     * any extras right within the cell so they stay distinct. */
    int used[DISPCFG_MAX_OUTPUTS][DISPCFG_MAX_OUTPUTS] = {{0}};
    for (int i = 0; i < d->count; i++) {
        int r = 0, c = 0;
        for (int j = 0; j < nrows; j++)
            if (rows[j] == d->order[i]->grid_y) { r = j; break; }
        for (int j = 0; j < ncols; j++)
            if (cols[j] == d->order[i]->grid_x) { c = j; break; }

        int w, h;
        wlr_output_effective_resolution(d->order[i]->wlr_output, &w, &h);
        wlr_output_layout_add(s->output_layout, d->order[i]->wlr_output,
                              col_x[c] + used[r][c], row_y[r]);
        used[r][c] += w;
    }

    output_layout_changed(s);   /* re-renders the panel too */
}

/* ── Deep colour (the HDR row) ───────────────────────────── */
/*
 * Ask the backend for a 10-bit framebuffer on this output.
 *
 * There is no "does this connector do 10-bit?" query in wlroots, and the DRM
 * property alone would not tell us whether the whole modeset (this mode, this
 * bandwidth, this cable) survives at 30bpp. So the capability test IS the
 * apply: build the state, wlr_output_test_state() it, and only commit if the
 * backend says yes. A DisplayPort link that has the headroom at 1440p60 and
 * not at 4K120 therefore reports honestly at each mode.
 *
 * Returns 1 if the output is now in the requested format.
 */
int dispcfg_set_deep_color(syn_server_t *s, syn_output_t *o, int enable)
{
    if (!o || !o->wlr_output) return 0;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_render_format(&state, enable ? DRM_FORMAT_XRGB2101010
                                                      : DRM_FORMAT_XRGB8888);

    int ok = wlr_output_test_state(o->wlr_output, &state) &&
             wlr_output_commit_state(o->wlr_output, &state);
    wlr_output_state_finish(&state);

    if (!ok) {
        wlr_log(WLR_INFO, "synui: %s rejected %s scanout",
                o->wlr_output->name, enable ? "10-bit" : "8-bit");
        /* Asking for 10-bit and being refused leaves the output where it was;
         * record the refusal so the panel can say so rather than showing a
         * setting that silently did nothing. */
        if (enable) { o->deep_color = 0; o->deep_color_ok = 0; }
        return 0;
    }

    o->deep_color    = enable ? 1 : 0;
    o->deep_color_ok = 1;
    return 1;
}

/*
 * Does the monitor advertise HDR over EDID? wlroots surfaces no colorimetry
 * fields, so this is the honest proxy we have: a display that accepts a
 * 10-bit framebuffer is the one that could carry HDR if the renderer ever
 * gains it. Reported separately from deep_color so the panel does not imply
 * the compositor is doing HDR tone mapping — it is not.
 */
void dispcfg_probe_hdr(syn_server_t *s, syn_output_t *o)
{
    if (!o || !o->wlr_output) return;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_render_format(&state, DRM_FORMAT_XRGB2101010);
    o->hdr_capable = wlr_output_test_state(o->wlr_output, &state) ? 1 : 0;
    wlr_output_state_finish(&state);
}

static void dispcfg_toggle_deep_color(syn_server_t *s)
{
    syn_output_t *sel = selected_output(s);
    if (!sel) return;
    dispcfg_set_deep_color(s, sel, !sel->deep_color);
    output_persist_save(s);
    synui_render_dispcfg(s);
}

/* Move the selected monitor one cell in the grid. If another monitor
 * already occupies the destination cell, swap grid cells with it instead
 * of stacking two outputs on top of each other. */
static void dispcfg_move(syn_server_t *s, int dx, int dy)
{
    syn_dispcfg_t *d = &s->dispcfg;
    syn_output_t *sel = selected_output(s);
    if (!sel) return;

    int nx = sel->grid_x + dx, ny = sel->grid_y + dy;

    syn_output_t *occupant = NULL;
    for (int i = 0; i < d->count; i++) {
        if (d->order[i] != sel &&
            d->order[i]->grid_x == nx && d->order[i]->grid_y == ny) {
            occupant = d->order[i];
            break;
        }
    }

    if (occupant) {
        occupant->grid_x = sel->grid_x;
        occupant->grid_y = sel->grid_y;
        snprintf(d->status, sizeof(d->status), "%s \xe2\x86\x94 %s",
                 sel->wlr_output->name, occupant->wlr_output->name);
    } else {
        snprintf(d->status, sizeof(d->status), "%s moved",
                 sel->wlr_output->name);
    }
    sel->grid_x = nx;
    sel->grid_y = ny;

    dispcfg_seed(s);   /* reading order (and d->selected) may have changed */
    for (int i = 0; i < d->count; i++)
        if (d->order[i] == sel) { d->selected = i; break; }

    dispcfg_rechain(s);
}

static void dispcfg_rotate(syn_server_t *s, int dir)
{
    syn_dispcfg_t *d = &s->dispcfg;
    syn_output_t *o = selected_output(s);
    if (!o) return;

    /* Cycle the pure rotations; a flipped transform folds back onto its
     * rotation first (transform & 3 is the rotation component). */
    int rot = (((int)o->wlr_output->transform & 3) + dir) & 3;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_transform(&state, (enum wl_output_transform)rot);
    if (!wlr_output_test_state(o->wlr_output, &state) ||
        !wlr_output_commit_state(o->wlr_output, &state)) {
        wlr_output_state_finish(&state);
        snprintf(d->status, sizeof(d->status),
                 "%s: rotation rejected by backend", o->wlr_output->name);
        synui_render_dispcfg(s);
        return;
    }
    wlr_output_state_finish(&state);

    static const char *rot_names[] = { "normal", "90°", "180°", "270°" };
    snprintf(d->status, sizeof(d->status), "%s → %s",
             o->wlr_output->name, rot_names[rot]);

    /* The effective size changed — re-chain so neighbours don't overlap. */
    dispcfg_rechain(s);
}

/* Mark the selected monitor as the X11 primary — where SDL games and other
 * X11 apps that ask for "the default display" will open. Exactly one output
 * holds the flag, so promoting one demotes the rest. */
static void dispcfg_make_primary(syn_server_t *s)
{
    syn_dispcfg_t *d = &s->dispcfg;
    syn_output_t *sel = selected_output(s);
    if (!sel) return;

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        o->primary = (o == sel);

    output_persist_save(s);
    xwayland_apply_primary(s);

    snprintf(d->status, sizeof(d->status), "%s → primary (X11 default display)",
             sel->wlr_output->name);
    synui_render_dispcfg(s);
}

void dispcfg_show(syn_server_t *s)
{
    s->dispcfg.visible = 1;
    s->dispcfg.status[0] = '\0';
    dispcfg_seed(s);
    synui_render_dispcfg(s);
}

void dispcfg_hide(syn_server_t *s)
{
    s->dispcfg.visible = 0;
    synui_render_dispcfg(s);
}

void dispcfg_toggle(syn_server_t *s)
{
    if (s->dispcfg.visible) dispcfg_hide(s);
    else                    dispcfg_show(s);
}

void dispcfg_outputs_changed(syn_server_t *s)
{
    if (!s->dispcfg.visible) return;
    dispcfg_seed(s);
    synui_render_dispcfg(s);
}

int dispcfg_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_dispcfg_t *d = &s->dispcfg;
    if (!d->visible) return 0;

    /* Shift+direction moves the selected monitor one cell in the
     * arrangement grid, ahead of the generic modified-combo bailout below
     * (which would otherwise hand it to the global bind table). Shift
     * turns h/j/k/l into their uppercase keysyms, so match those too. */
    if (mods == WLR_MODIFIER_SHIFT) {
        switch (sym) {
        case XKB_KEY_Left:  case XKB_KEY_H: dispcfg_move(s, -1,  0); return 1;
        case XKB_KEY_Right: case XKB_KEY_L: dispcfg_move(s, +1,  0); return 1;
        case XKB_KEY_Up:    case XKB_KEY_K: dispcfg_move(s,  0, -1); return 1;
        case XKB_KEY_Down:  case XKB_KEY_J: dispcfg_move(s,  0, +1); return 1;
        default: break;
        }
    }

    /* Other modified combos (Super+…) still reach the bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        dispcfg_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (d->selected > 0) d->selected--;
        synui_render_dispcfg(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (d->selected < d->count - 1) d->selected++;
        synui_render_dispcfg(s);
        return 1;
    case XKB_KEY_Left:
    case XKB_KEY_h:
        dispcfg_rotate(s, -1);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        dispcfg_rotate(s, +1);
        return 1;
    case XKB_KEY_p:
        dispcfg_make_primary(s);
        return 1;
    case XKB_KEY_d:
        dispcfg_toggle_deep_color(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
