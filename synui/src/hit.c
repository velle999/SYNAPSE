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
}

void hit_set_rows(syn_hit_t *g, int lx, int ly, int w, int h, int n)
{
    g->row_x = g->x + lx;
    g->row_y = g->y + ly;
    g->row_w = w;
    g->row_h = h;
    g->rows  = n < 0 ? 0 : n;
    g->first = 0;
}

void hit_set_first(syn_hit_t *g, int first)
{
    g->first = first < 0 ? 0 : first;
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

    if (lx < g->row_x || lx >= g->row_x + g->row_w) return -1;
    if (ly < g->row_y) return -1;

    int i = (int)((ly - g->row_y) / g->row_h);
    return (i >= 0 && i < g->rows) ? i : -1;
}

int hit_index_at(const syn_hit_t *g, double lx, double ly)
{
    int row = hit_row_at(g, lx, ly);
    return row < 0 ? -1 : row + g->first;
}
