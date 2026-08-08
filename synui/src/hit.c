/*
 * hit.c — pointer hit-testing for the panels the compositor draws itself.
 *
 * synui's settings panels were keyboard-only for a long time, and each one that
 * later grew a pointer grew it privately: the Bluetooth panel keeps its own
 * x/y/w/h, the dock menu keeps another set, the desktop menu a third. That is
 * fine for one panel and a maintenance problem at fifteen, because the geometry
 * is written by render.c and read somewhere else entirely — every copy is a
 * chance for the drawn rows and the clickable rows to drift apart by a few
 * pixels and stay wrong until somebody notices they are selecting the row above
 * the one they pointed at.
 *
 * So there is one shape (syn_hit_t), one writer (the panel's synui_render_*),
 * and the two functions below. The panels answer "what is under the cursor" and
 * nothing else here; what a click *means* stays in the panel that owns it,
 * because a click on a row of the task manager and a click on a row of the
 * wallpaper picker are not the same event.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include "synui.h"

void hit_clear(syn_hit_t *g)
{
    /* Zeroing w and h is what makes hit_in_panel false; the rest is tidiness so
     * a stale row grid can never be read back either. */
    *g = (syn_hit_t){ 0 };
}

void hit_set_panel(syn_hit_t *g, int x, int y, int w, int h)
{
    g->x = x; g->y = y; g->w = w; g->h = h;
    /* A panel whose rows are never recorded (the calendar grid, a panel that is
     * one block of text) still hit-tests as a panel — which is all that
     * click-off-to-close needs. Clear the grid so it reports no rows rather
     * than whatever the last panel to use this struct had. */
    g->row_x = g->row_y = g->row_w = g->row_h = g->rows = 0;
    /* Same for the close button. Cleared here rather than by the panels means
     * one that stops drawing it — the setting was changed, or it never had one
     * — stops answering for it on the next render without saying so. */
    g->close_x = g->close_y = g->close_w = g->close_h = 0;
}

void hit_set_rows(syn_hit_t *g, int lx, int ly, int w, int h, int n)
{
    g->row_x = g->x + lx;
    g->row_y = g->y + ly;
    g->row_w = w;
    g->row_h = h;
    g->rows  = n < 0 ? 0 : n;
    g->cols  = 1;     /* a list is a grid one cell wide */
    g->first = 0;
}

/* A grid of cells rather than a stack of full-width rows — the emoji picker,
 * where a list of 1779 single characters would be absurd.
 *
 * Deliberately the same struct and the same hit_index_at(): a grid IS a list
 * whose rows hold several cells, and the alternative was a second geometry
 * shape with its own scroll offset, which is precisely the drift this file was
 * written to stop. `cell_w` is the pitch as well as the width, as row_h always
 * was, so a gutter belongs inside the cell rather than between cells.
 */
void hit_set_grid(syn_hit_t *g, int lx, int ly,
                  int cell_w, int cell_h, int cols, int rows)
{
    g->row_x = g->x + lx;
    g->row_y = g->y + ly;
    g->row_w = cell_w;
    g->row_h = cell_h;
    g->cols  = cols < 1 ? 1 : cols;
    g->rows  = rows < 0 ? 0 : rows;
    g->first = 0;
}

void hit_set_first(syn_hit_t *g, int first)
{
    g->first = first < 0 ? 0 : first;
}

/* ── The corner close button ─────────────────────────────────
 *
 * Panel-local, like hit_set_rows(): render.c works in the panel's own
 * coordinates and this is the one place that adds the origin back on.
 *
 * Only meaningful after hit_set_panel(), which zeroes it — a panel that stops
 * drawing the button (because the setting changed) therefore stops answering
 * for it on the very next render, with no explicit clear anywhere.
 */
void hit_set_close(syn_hit_t *g, int lx, int ly, int w, int h)
{
    g->close_x = g->x + lx;
    g->close_y = g->y + ly;
    g->close_w = w < 0 ? 0 : w;
    g->close_h = h < 0 ? 0 : h;
}

int hit_in_close(const syn_hit_t *g, double lx, double ly)
{
    if (g->close_w <= 0 || g->close_h <= 0) return 0;
    return lx >= g->close_x && lx < g->close_x + g->close_w &&
           ly >= g->close_y && ly < g->close_y + g->close_h;
}

int hit_in_panel(const syn_hit_t *g, double lx, double ly)
{
    if (g->w <= 0 || g->h <= 0) return 0;
    return lx >= g->x && lx < g->x + g->w &&
           ly >= g->y && ly < g->y + g->h;
}

int hit_row_at(const syn_hit_t *g, double lx, double ly)
{
    if (g->rows <= 0 || g->row_h <= 0) return -1;
    if (!hit_in_panel(g, lx, ly)) return -1;

    /* cols is 0 in a struct written before hit_set_rows/_grid ran, and on the
     * panels that record a rect and no grid at all. Treat that as a list, which
     * is what every caller of this function meant before grids existed. */
    int cols = g->cols > 0 ? g->cols : 1;

    if (lx < g->row_x || lx >= g->row_x + g->row_w * cols) return -1;
    if (ly < g->row_y) return -1;

    int i = (int)((ly - g->row_y) / g->row_h);
    return (i >= 0 && i < g->rows) ? i : -1;
}

/* Which column the cursor is in, or -1 off the grid. A list always answers 0,
 * so callers that predate grids need not ask. */
int hit_col_at(const syn_hit_t *g, double lx, double ly)
{
    if (hit_row_at(g, lx, ly) < 0) return -1;
    if (g->row_w <= 0) return -1;

    int cols = g->cols > 0 ? g->cols : 1;
    int c = (int)((lx - g->row_x) / g->row_w);
    return (c >= 0 && c < cols) ? c : -1;
}

int hit_index_at(const syn_hit_t *g, double lx, double ly)
{
    int row = hit_row_at(g, lx, ly);
    if (row < 0) return -1;

    int col = hit_col_at(g, lx, ly);
    if (col < 0) return -1;

    int cols = g->cols > 0 ? g->cols : 1;
    /* first counts in CELLS, not rows, so a grid scrolled by whole rows passes
     * (top_row * cols) and this arithmetic stays the same for both shapes. */
    return g->first + row * cols + col;
}
