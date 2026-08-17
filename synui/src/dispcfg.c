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

#include "edid.h"
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

/* ── The screen arrangement ──────────────────────────────────
 *
 * Extend / Duplicate / built-in off, and everything that has to happen when
 * one of them is chosen. See syn_display_mode_t in synui.h for what the three
 * mean and why they are one setting rather than three switches.
 */
const char *const syn_display_mode_names[SYN_DISPLAY_MODE_COUNT] = {
    "extend", "mirror", "external",
};

/*
 * Name → syn_display_mode_t, or -1.
 *
 * A function rather than every caller walking the array, for the same reason
 * lid_action_from_name() is one: config.c has to parse the key and input.c has
 * to take it as a dispatch argument, and neither of them links dispcfg.c in the
 * unit tests. A function is something a test can stub in one line; an extern
 * array is not, and making config.c reference the array directly broke four
 * test binaries at link time.
 */
int display_mode_from_name(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SYN_DISPLAY_MODE_COUNT; i++)
        if (strcasecmp(name, syn_display_mode_names[i]) == 0) return i;
    return -1;
}

/* Somewhere for a window to go. Any attached output that is not `except`. */
static syn_output_t *dispcfg_other_output(syn_server_t *s, syn_output_t *except)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (o != except && !o->detached) return o;
    return NULL;
}

/*
 * Take an output out of the layout, and move everything that was on it.
 *
 * The move is not a nicety. A view keeps a ->output back-pointer, and layout.c
 * sizes and places it against that output's box; left pointing at a detached
 * one, a window would be laid out against a screen that is not in the layout
 * and would be unreachable. This is the same sweep output_destroy() does when a
 * monitor is unplugged (synui_main.c) — a detached output is an unplugged one
 * as far as window placement is concerned, and the two must not disagree.
 *
 * Refuses to detach the LAST attached output. A desktop with no screen in the
 * layout has nowhere to put anything, no usable area to compute and no way for
 * the user to undo it; every caller here already checks, and this is the
 * backstop that means none of them can be wrong in the one direction that
 * cannot be recovered from without a TTY.
 */
static void dispcfg_detach(syn_server_t *s, syn_output_t *o)
{
    if (!o || o->detached) return;

    syn_output_t *home = dispcfg_other_output(s, o);
    if (!home) {
        wlr_log(WLR_INFO, "synui: dispcfg: refusing to detach %s — it is the "
                          "only screen left", o->wlr_output->name);
        return;
    }

    int moved = 0;
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[i].windows, link) {
            if (v->output != o) continue;
            v->output = home;
            moved++;
        }
    }
    if (s->ai_layout_output == o) s->ai_layout_output = NULL;
    /* NULL means "ask the cursor", so an open panel re-homes by itself. */
    if (s->ui_output == o) s->ui_output = NULL;

    o->detached = 1;
    wlr_output_layout_remove(s->output_layout, o->wlr_output);

    /* Actually switch it off. Detaching alone would leave a lit screen showing
     * a stale frame for ever — the scene has no output to render to it. */
    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_enabled(&st, false);
    wlr_output_commit_state(o->wlr_output, &st);
    wlr_output_state_finish(&st);

    wlr_log(WLR_INFO, "synui: dispcfg: %s detached (%d window(s) moved to %s)",
            o->wlr_output->name, moved, home->wlr_output->name);
}

/* The other half: back on, and back in the layout. The caller re-flows — this
 * only has to get the output enabled, since dispcfg_rechain() below is what
 * decides where it lands. */
static void dispcfg_attach(syn_server_t *s, syn_output_t *o)
{
    if (!o || !o->detached) return;
    (void)s;

    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_enabled(&st, true);
    if (!wlr_output_commit_state(o->wlr_output, &st)) {
        wlr_output_state_finish(&st);
        wlr_log(WLR_ERROR, "synui: dispcfg: could not re-enable %s",
                o->wlr_output->name);
        return;
    }
    wlr_output_state_finish(&st);

    o->detached = 0;
    if (o->scene_output)
        wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
    wlr_output_schedule_frame(o->wlr_output);
    wlr_log(WLR_INFO, "synui: dispcfg: %s attached", o->wlr_output->name);
}

/* Is there an external (non-built-in) screen to fall back on? EXTERNAL mode is
 * refused without one — it would switch off the only screen the machine has. */
static bool dispcfg_has_external(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        if (!output_is_internal(o->wlr_output)) return true;
    return false;
}

/*
 * The largest resolution EVERY attached output can do, for Duplicate.
 *
 * Modes are matched on width×height and refresh is left to the output's own
 * best at that size: insisting on a common refresh as well finds nothing far
 * too often (a 60Hz TV beside a 144Hz panel share plenty of resolutions and no
 * timings), and a mirrored pair running at different refresh rates is fine —
 * they are separate CRTCs scanning out the same content.
 *
 * Returns false when the outputs share no resolution at all, in which case the
 * caller overlaps them at their own modes. That is a worse mirror, and it is
 * still the honest answer: the alternative is refusing to mirror.
 */
static bool dispcfg_common_mode(syn_server_t *s, int *out_w, int *out_h)
{
    syn_output_t *first = NULL, *o;
    wl_list_for_each(o, &s->outputs, link) { first = o; break; }
    if (!first) return false;

    int best_w = 0, best_h = 0;
    struct wlr_output_mode *m;
    wl_list_for_each(m, &first->wlr_output->modes, link) {
        /* Every OTHER output has to offer this size too. */
        bool all = true;
        wl_list_for_each(o, &s->outputs, link) {
            if (o == first) continue;
            bool found = false;
            struct wlr_output_mode *m2;
            wl_list_for_each(m2, &o->wlr_output->modes, link)
                if (m2->width == m->width && m2->height == m->height) {
                    found = true;
                    break;
                }
            if (!found) { all = false; break; }
        }
        if (!all) continue;

        if ((int64_t)m->width * m->height > (int64_t)best_w * best_h) {
            best_w = m->width;
            best_h = m->height;
        }
    }

    if (best_w == 0) return false;
    *out_w = best_w;
    *out_h = best_h;
    return true;
}

/* Put this output into w×h at its best refresh for that size. */
static void dispcfg_set_mode(syn_output_t *o, int w, int h)
{
    struct wlr_output_mode *m, *best = NULL;
    wl_list_for_each(m, &o->wlr_output->modes, link) {
        if (m->width != w || m->height != h) continue;
        if (!best || m->refresh > best->refresh) best = m;
    }
    if (!best) return;
    if (o->wlr_output->current_mode == best) return;

    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_mode(&st, best);
    if (!wlr_output_test_state(o->wlr_output, &st) ||
        !wlr_output_commit_state(o->wlr_output, &st))
        wlr_log(WLR_INFO, "synui: dispcfg: %s refused %dx%d",
                o->wlr_output->name, w, h);
    wlr_output_state_finish(&st);
}

/* Remember the mode an output is in before Duplicate overwrites it, once. The
 * guard is what makes this idempotent: rechain runs on every hotplug, and a
 * second save while mirrored would record the MIRRORED mode as the one to
 * restore, which is how "turn duplicate off" would leave every screen at the
 * lowest common resolution for ever. */
static void dispcfg_save_mode(syn_output_t *o)
{
    if (o->saved_mode_w > 0) return;
    struct wlr_output_mode *m = o->wlr_output->current_mode;
    if (!m) return;
    o->saved_mode_w       = m->width;
    o->saved_mode_h       = m->height;
    o->saved_mode_refresh = m->refresh;
}

static void dispcfg_restore_mode(syn_output_t *o)
{
    if (o->saved_mode_w <= 0) return;
    int w = o->saved_mode_w, h = o->saved_mode_h;
    o->saved_mode_w = o->saved_mode_h = o->saved_mode_refresh = 0;
    dispcfg_set_mode(o, w, h);
}

/*
 * Decide which outputs are in the layout, before rechain places them.
 *
 * Runs on every reflow rather than only on a mode change, because the answer
 * depends on what is plugged in: EXTERNAL with the TV unplugged has to give the
 * built-in panel back, or closing the lid at a desk and then walking away with
 * the laptop leaves a machine with no working screen and no way to say so.
 * That self-correction is the reason this is recomputed from the mode rather
 * than applied once when the mode is set.
 */
static void dispcfg_apply_mode(syn_server_t *s)
{
    const int mode = s->config.display_mode;
    syn_output_t *o;

    /* Leaving Duplicate: give every screen its own mode back first, so the
     * grid below packs them at the sizes they will actually be in. */
    if (mode != SYN_DISPLAY_MIRROR)
        wl_list_for_each(o, &s->outputs, link)
            dispcfg_restore_mode(o);

    if (mode == SYN_DISPLAY_EXTERNAL && dispcfg_has_external(s)) {
        wl_list_for_each(o, &s->outputs, link)
            if (!output_is_internal(o->wlr_output))
                dispcfg_attach(s, o);
        /* Externals first, so there is somewhere for the built-in panel's
         * windows to go before it is taken out. */
        wl_list_for_each(o, &s->outputs, link)
            if (output_is_internal(o->wlr_output))
                dispcfg_detach(s, o);
        return;
    }

    /* Every other case wants every screen back. This is also the EXTERNAL
     * fallback: with no external screen, the mode stands but does nothing, and
     * the built-in panel keeps working. */
    wl_list_for_each(o, &s->outputs, link)
        dispcfg_attach(s, o);

    if (mode != SYN_DISPLAY_MIRROR) return;

    int w = 0, h = 0;
    if (!dispcfg_common_mode(s, &w, &h)) {
        wlr_log(WLR_INFO, "synui: dispcfg: no resolution every screen shares — "
                          "duplicating at their own modes (the smaller ones "
                          "will show a crop)");
        return;
    }
    wl_list_for_each(o, &s->outputs, link) {
        dispcfg_save_mode(o);
        dispcfg_set_mode(o, w, h);
    }
}

/* Re-flow every output's pixel position from its logical grid cell (using
 * transform-aware effective sizes), then run the shared post-apply reflow.
 *
 * This is a CSS-grid-style pack with auto tracks: each distinct grid_y is a
 * row as tall as the tallest output in it, each distinct grid_x is a column
 * as wide as the widest output in it, and an output lands at the top of its
 * cell, centred across it. Tracks are sized across the *whole* grid, not per
 * row, so a
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
 * a wider neighbour on another row.
 *
 * A monitor narrower than its column is CENTRED in it rather than pinned to
 * the left edge, because that is how the smaller monitor stacked above a
 * bigger one actually sits on the desk: 1920 above 2560 overhangs by the same
 * 320px either side, so the cursor leaves the top edge into it from the
 * middle of the screen instead of from the left. Rows stay top-aligned —
 * monitors side by side sit on a desk, not floating at a shared centreline. */
static void dispcfg_rechain(syn_server_t *s)
{
    syn_dispcfg_t *d = &s->dispcfg;
    if (d->count == 0) return;

    dispcfg_apply_mode(s);

    /* Duplicate: every attached screen at the same origin. There is no grid to
     * pack — that is what makes them show the same thing. */
    if (s->config.display_mode == SYN_DISPLAY_MIRROR) {
        for (int i = 0; i < d->count; i++) {
            if (d->order[i]->detached) continue;
            wlr_output_layout_add(s->output_layout,
                                  d->order[i]->wlr_output, 0, 0);
        }
        output_layout_changed(s);
        return;
    }

    int rows[DISPCFG_MAX_OUTPUTS], cols[DISPCFG_MAX_OUTPUTS];
    int nrows = dispcfg_tracks(d, false, rows);
    int ncols = dispcfg_tracks(d, true,  cols);

    /* Cell extents, and each output's (row, column) index. dispcfg_move()
     * swaps rather than stacks, so one output per cell is the normal case —
     * but a hand-edited saved layout could name the same cell twice, and two
     * outputs at identical coordinates would leave one of them unreachable by
     * the cursor. Cell-mates lie side by side, so a cell is as wide as their
     * widths summed; sizing the track from that keeps the run inside its own
     * column instead of overflowing into the next one. */
    int cell_w[DISPCFG_MAX_OUTPUTS][DISPCFG_MAX_OUTPUTS] = {{0}};
    int cell_h[DISPCFG_MAX_OUTPUTS][DISPCFG_MAX_OUTPUTS] = {{0}};
    int ri[DISPCFG_MAX_OUTPUTS], ci[DISPCFG_MAX_OUTPUTS];
    int ow[DISPCFG_MAX_OUTPUTS];

    for (int i = 0; i < d->count; i++) {
        ri[i] = ci[i] = 0;
        ow[i] = 0;
        /* A detached output takes up no room in the grid. Sizing its cell
         * anyway would leave a monitor-shaped hole between the screens that
         * ARE on — the cursor would cross a dead gap on its way from one to
         * the next, and windows dropped in it would land nowhere. */
        if (d->order[i]->detached) continue;

        for (int j = 0; j < nrows; j++)
            if (rows[j] == d->order[i]->grid_y) { ri[i] = j; break; }
        for (int j = 0; j < ncols; j++)
            if (cols[j] == d->order[i]->grid_x) { ci[i] = j; break; }

        int w, h;
        wlr_output_effective_resolution(d->order[i]->wlr_output, &w, &h);
        ow[i] = w;
        cell_w[ri[i]][ci[i]] += w;
        if (h > cell_h[ri[i]][ci[i]]) cell_h[ri[i]][ci[i]] = h;
    }

    /* Track sizes and origins: a row is as tall as its tallest cell, a column
     * as wide as its widest, stacked in ascending grid order. */
    int row_y[DISPCFG_MAX_OUTPUTS], col_x[DISPCFG_MAX_OUTPUTS];
    int col_w[DISPCFG_MAX_OUTPUTS];
    int y = 0;
    for (int r = 0; r < nrows; r++) {
        int row_h = 0;
        for (int c = 0; c < ncols; c++)
            if (cell_h[r][c] > row_h) row_h = cell_h[r][c];
        row_y[r] = y;
        y += row_h;
    }
    int x = 0;
    for (int c = 0; c < ncols; c++) {
        col_w[c] = 0;
        for (int r = 0; r < nrows; r++)
            if (cell_w[r][c] > col_w[c]) col_w[c] = cell_w[r][c];
        col_x[c] = x;
        x += col_w[c];
    }

    /* Place each output, centring its cell's run in the column. */
    int used[DISPCFG_MAX_OUTPUTS][DISPCFG_MAX_OUTPUTS] = {{0}};
    for (int i = 0; i < d->count; i++) {
        /* dispcfg_detach() already removed it from the layout; adding it back
         * here is exactly the bug the flag exists to prevent. */
        if (d->order[i]->detached) continue;

        int r = ri[i], c = ci[i];
        int centre = (col_w[c] - cell_w[r][c]) / 2;
        wlr_output_layout_add(s->output_layout, d->order[i]->wlr_output,
                              col_x[c] + centre + used[r][c], row_y[r]);
        used[r][c] += ow[i];
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
 * Can this output carry a 10-bit framebuffer at all? Same test as the apply
 * above, without committing, so the panel can grey out the row on a monitor
 * or link that would refuse it.
 *
 * This says nothing about HDR. Every 10-bit-capable GPU plane passes it —
 * on this desk all three monitors do, including two plain SDR panels — so it
 * must not be used to answer "is this an HDR display?". That is
 * dispcfg_probe_edid()'s job.
 */
void dispcfg_probe_deep_color(syn_server_t *s, syn_output_t *o)
{
    if (!o || !o->wlr_output) return;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_render_format(&state, DRM_FORMAT_XRGB2101010);
    o->deep_color_capable = wlr_output_test_state(o->wlr_output, &state) ? 1 : 0;
    wlr_output_state_finish(&state);
}

/*
 * What does the monitor itself claim? edid.c reads the connector's EDID and
 * pulls the CTA-861 HDR static metadata (which EOTFs the panel implements —
 * PQ means HDR10) and colorimetry (BT.2020) out of it.
 *
 * Kept strictly separate from deep_color: a display that takes a 10-bit
 * framebuffer is not thereby an HDR display, and conflating the two is why
 * the panel used to show the same answer for an HDR10 monitor and the SDR one
 * next to it. Reporting this does not mean synui outputs HDR — it composites
 * SDR sRGB and does not touch HDR_OUTPUT_METADATA — but the panel should not
 * be the last thing on the desk that doesn't know what is plugged in.
 */
void dispcfg_probe_edid(syn_server_t *s, syn_output_t *o)
{
    if (!o || !o->wlr_output) return;

    syn_edid_hdr_t hdr;
    edid_hdr_probe_connector(o->wlr_output->name, &hdr);

    o->hdr_pq       = hdr.pq;
    o->hdr_hlg      = hdr.hlg;
    o->wide_gamut   = hdr.bt2020;
    o->hdr_max_nits = hdr.max_nits;

    if (o->hdr_pq || o->hdr_hlg)
        wlr_log(WLR_INFO, "synui: %s advertises HDR (%s%s%s, %.0f cd/m2 peak) "
                          "- compositing SDR, output stays 8-bit sRGB",
                o->wlr_output->name,
                o->hdr_pq ? "PQ" : "", (o->hdr_pq && o->hdr_hlg) ? "+" : "",
                o->hdr_hlg ? "HLG" : "", (double)o->hdr_max_nits);
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

/*
 * Set the screen arrangement, and say what happened.
 *
 * The status line is not decoration here. EXTERNAL with nothing plugged in
 * does nothing on purpose (dispcfg_apply_mode falls back rather than blanking
 * the only screen), and a row that moved while the desk did not is exactly the
 * shape of "this setting is broken" — so the one case that declines says so.
 */
void dispcfg_set_mode_cfg(syn_server_t *s, int mode)
{
    if (mode < 0 || mode >= SYN_DISPLAY_MODE_COUNT) return;

    s->config.display_mode = mode;
    settings_state_set("display_mode", syn_display_mode_names[mode]);

    dispcfg_seed(s);
    dispcfg_rechain(s);
    /* The grid moved, so the saved positions did too. rechain ends in
     * output_layout_changed(), which saves — this is only for the mode itself,
     * which is not part of outputs.conf. */

    syn_dispcfg_t *d = &s->dispcfg;
    if (mode == SYN_DISPLAY_EXTERNAL && !dispcfg_has_external(s))
        snprintf(d->status, sizeof(d->status),
                 "external only: no external screen connected \xe2\x80\x94 "
                 "the built-in panel stays on");
    else
        snprintf(d->status, sizeof(d->status), "screens: %s",
                 syn_display_mode_names[mode]);

    wlr_log(WLR_INFO, "synui: dispcfg: display mode = %s",
            syn_display_mode_names[mode]);
    synui_render_dispcfg(s);
}

/* Step to the next arrangement — what the `display_mode` bind action and the
 * panel's `m` key both do. One key that cycles rather than three actions,
 * because this is one setting with three positions. */
void dispcfg_cycle_mode(syn_server_t *s)
{
    dispcfg_set_mode_cfg(s,
        (s->config.display_mode + 1) % SYN_DISPLAY_MODE_COUNT);
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
    ctlpanel_child_closed(s, "displays");
}

void dispcfg_toggle(syn_server_t *s)
{
    if (s->dispcfg.visible) dispcfg_hide(s);
    else                    dispcfg_show(s);
}

void dispcfg_outputs_changed(syn_server_t *s)
{
    /* A hotplug can change the ANSWER to the current mode, whether or not the
     * panel is open: unplugging the TV in "external only" has to give the
     * built-in panel back, and plugging one in has to take it away again.
     * Re-running the mode is what makes that automatic — see the comment on
     * dispcfg_apply_mode(). This is outside the visibility check below, which
     * is only about repainting.
     *
     * EXTEND is the default and re-running it is what already happened on
     * every hotplug, so this costs nothing on a desktop. */
    if (s->config.display_mode != SYN_DISPLAY_EXTEND) {
        dispcfg_seed(s);
        dispcfg_rechain(s);
    }

    if (!s->dispcfg.visible) return;
    dispcfg_seed(s);
    synui_render_dispcfg(s);
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h — with the same exception the
 * Bluetooth panel makes, for the same reason. The actions here are several
 * (rotate, make primary, toggle deep colour) and one click cannot mean all of
 * them, so it means the one thing it unambiguously can: this is the monitor I
 * am talking about. The keys the footer names still do the rest.
 *
 * The mini-map is clickable as well as the list, because a picture of your
 * monitors is what a pointer will go for first, and having it be the one part
 * of the panel that does not respond would read as broken.
 */

/* order[] index under (lx,ly): a mini-map cell, or a list row. -1 for neither. */
static int dispcfg_at(const syn_dispcfg_t *d, double lx, double ly)
{
    for (int i = 0; i < d->count && i < DISPCFG_MAX_OUTPUTS; i++) {
        const struct wlr_box *c = &d->cell[i];
        if (c->width <= 0) continue;
        if (lx >= c->x && lx < c->x + c->width &&
            ly >= c->y && ly < c->y + c->height)
            return i;
    }
    int row = hit_row_at(&d->hit, lx, ly);
    return (row >= 0 && row < d->count) ? row : -1;
}

int dispcfg_motion(syn_server_t *s, double lx, double ly)
{
    syn_dispcfg_t *d = &s->dispcfg;
    if (!d->visible) return 0;

    int i = dispcfg_at(d, lx, ly);
    if (i < 0 || i == d->selected) return 1;
    d->selected = i;
    synui_render_dispcfg(s);
    return 1;
}

int dispcfg_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)button; (void)time_msec;
    syn_dispcfg_t *d = &s->dispcfg;
    if (!d->visible) return 0;

    if (!hit_in_panel(&d->hit, lx, ly)) {
        dispcfg_hide(s);
        return 1;
    }
    return dispcfg_motion(s, lx, ly);   /* select, and nothing else */
}

int dispcfg_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    syn_dispcfg_t *d = &s->dispcfg;
    if (!d->visible) return 0;
    if (delta == 0) return 1;

    int next = d->selected + (delta > 0 ? 1 : -1);
    if (next < 0 || next >= d->count) return 1;
    d->selected = next;
    synui_render_dispcfg(s);
    return 1;
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
    case XKB_KEY_m:
        dispcfg_cycle_mode(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
