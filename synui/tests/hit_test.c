/*
 * hit_test.c — the panel hit-test geometry the pointer chain is built on.
 *
 * Worth a test of its own because it is the one piece of the "panels take the
 * mouse now" work that every panel shares, and because the failure it guards
 * against is a QUIET one: an off-by-one in the row band does not crash, it just
 * selects the row above the one you pointed at, and only on some panels, and
 * only near the edges. Nobody files that bug — they conclude the panel is
 * flaky and go back to the keyboard.
 *
 * The numbers here are the real geometry of two panels, taken from render.c:
 * the power panel (rows drawn on a text baseline, hit box starting 16px above
 * it) and the clipboard (a scrolling list, where the index under the cursor is
 * not the row under the cursor).
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* A hidden panel must hit-test as nothing at all — not as "the last place it
 * was". This is what stops a closed panel eating clicks. */
static void test_cleared(void)
{
    syn_hit_t g;
    hit_set_panel(&g, 100, 100, 400, 300);
    hit_set_rows(&g, 12, 50, 376, 30, 8);
    CHECK(hit_in_panel(&g, 150, 150), "a live panel should contain its own middle");

    hit_clear(&g);
    CHECK(!hit_in_panel(&g, 150, 150), "a cleared panel still claimed a point");
    CHECK(hit_row_at(&g, 150, 150) == -1, "a cleared panel still claimed a row");
    CHECK(hit_index_at(&g, 150, 150) == -1, "a cleared panel still claimed an index");
}

/* The panel rect is half-open: the top-left corner is inside, the bottom-right
 * is not. Get this wrong and a click on the last pixel row of a panel either
 * falls through to the window behind it or closes the panel. */
static void test_panel_edges(void)
{
    syn_hit_t g;
    hit_set_panel(&g, 100, 200, 400, 300);

    CHECK(hit_in_panel(&g, 100, 200),   "top-left corner should be inside");
    CHECK(hit_in_panel(&g, 499, 499),   "bottom-right-most pixel should be inside");
    CHECK(!hit_in_panel(&g, 500, 400),  "one past the right edge should be outside");
    CHECK(!hit_in_panel(&g, 300, 500),  "one past the bottom edge should be outside");
    CHECK(!hit_in_panel(&g, 99, 400),   "one before the left edge should be outside");
    CHECK(!hit_in_panel(&g, 300, 199),  "one above the top edge should be outside");

    /* No rows were recorded, which several panels never do. That must not be a
     * row of zero height at the origin. */
    CHECK(hit_row_at(&g, 150, 250) == -1, "a panel with no row grid claimed a row");
}

/* The power panel: pw 520, row_h 30, top 66, and a hit box that starts 16px
 * above the text baseline — see synui_render_power(). */
static void test_power_rows(void)
{
    const int px = 700, py = 300, pw = 520, row_h = 30, top = 66;
    syn_hit_t g;
    hit_set_panel(&g, px, py, pw, 400);
    hit_set_rows(&g, 12, top - 16, pw - 24, row_h, 5);

    const int row0_top = py + top - 16;

    CHECK(hit_row_at(&g, px + 100, row0_top) == 0,
          "the first pixel of row 0 should be row 0");
    CHECK(hit_row_at(&g, px + 100, row0_top + row_h - 1) == 0,
          "the last pixel of row 0 should still be row 0");
    CHECK(hit_row_at(&g, px + 100, row0_top + row_h) == 1,
          "the next pixel down should be row 1");
    CHECK(hit_row_at(&g, px + 100, row0_top + 4 * row_h) == 4,
          "the last of five rows should be reachable");

    /* Above the first row and below the last is the panel's chrome — the title,
     * the status line, the key hints. A click there must do nothing, not
     * activate whichever row is nearest. */
    CHECK(hit_row_at(&g, px + 100, row0_top - 1) == -1,
          "the header above row 0 claimed a row");
    CHECK(hit_row_at(&g, px + 100, row0_top + 5 * row_h) == -1,
          "the footer below the last row claimed a row");

    /* Horizontally the band stops where the highlight does. */
    CHECK(hit_row_at(&g, px + 11, row0_top) == -1,
          "left of the row band claimed a row");
    CHECK(hit_row_at(&g, px + pw - 12, row0_top) == -1,
          "right of the row band claimed a row");
    CHECK(hit_row_at(&g, px + pw - 13, row0_top) == 0,
          "the last pixel of the row band should be row 0");
}

/* A scrolling list: what is under the cursor is a ROW, and what the panel acts
 * on is an INDEX. The clipboard, the theme manager and the pickers all rely on
 * the difference, and getting it wrong pastes the wrong entry. */
static void test_scrolled_index(void)
{
    const int px = 0, py = 0, row_h = 24, top = 78;
    syn_hit_t g;
    hit_set_panel(&g, px, py, 560, 400);
    hit_set_rows(&g, 10, top - 15, 540, row_h, 10);

    const int row0_top = py + top - 15;

    /* Unscrolled, row and index are the same thing. */
    CHECK(hit_row_at(&g, 100, row0_top + 2 * row_h) == 2, "row 2 by position");
    CHECK(hit_index_at(&g, 100, row0_top + 2 * row_h) == 2,
          "unscrolled, index should equal row");

    /* Scrolled down 7, the third visible row is entry 9. */
    hit_set_first(&g, 7);
    CHECK(hit_row_at(&g, 100, row0_top + 2 * row_h) == 2,
          "scrolling must not move the row band");
    CHECK(hit_index_at(&g, 100, row0_top + 2 * row_h) == 9,
          "scrolled by 7, visible row 2 should be entry 9");

    /* A miss stays a miss: the offset must not turn -1 into an entry. */
    CHECK(hit_index_at(&g, 100, row0_top - 1) == -1,
          "a miss above the list came back as an entry");
    CHECK(hit_index_at(&g, 100, row0_top + 10 * row_h) == -1,
          "a miss below the list came back as an entry");

    /* hit_set_rows() resets the offset. A panel that scrolled, closed and
     * reopened at the top would otherwise keep the old one until something
     * happened to call hit_set_first() again. */
    hit_set_rows(&g, 10, top - 15, 540, row_h, 10);
    CHECK(hit_index_at(&g, 100, row0_top + 2 * row_h) == 2,
          "hit_set_rows should have reset the scroll offset");
}

/* The calendar is the one panel whose rows are a grid, and it recovers the cell
 * width by dividing the band by seven. That only works if the band is exactly
 * seven cells wide, which is the contract render.c writes it under. */
static void test_calendar_band(void)
{
    const int px = 40, py = 0, grid_x = 14, grid_y = 66, cell_w = 44, cell_h = 34;
    syn_hit_t g;
    hit_set_panel(&g, px, py, 336, 322);
    hit_set_rows(&g, grid_x, grid_y + 12, 7 * cell_w, cell_h, 6);

    CHECK(g.row_w / 7 == cell_w, "the band should divide back into cell widths");

    const int band_x = px + grid_x, band_y = py + grid_y + 12;

    /* Column 3 of week 2, the way clock.c computes it. */
    CHECK(hit_row_at(&g, band_x + 3 * cell_w + 2, band_y + 2 * cell_h + 2) == 2,
          "week 2 should be row 2");
    CHECK((int)((band_x + 3 * cell_w + 2 - g.row_x) / (g.row_w / 7)) == 3,
          "column 3 should come back as column 3");

    /* Past the seventh column is outside the band, not an eighth day. */
    CHECK(hit_row_at(&g, band_x + 7 * cell_w, band_y + 2) == -1,
          "past the last column claimed a row");
}

/* hit_set_grid(): several cells per row, indexed left-to-right then down.
 *
 * Worth its own case because hit_index_at() grew column arithmetic to serve it,
 * and that function is on the click path of EVERY panel in the tree — so the
 * list cases above are as much a part of this feature's coverage as the grid
 * ones here. The emoji picker is the caller. */
static void test_grid(void)
{
    const int px = 100, py = 50;
    const int gx = 20, gy = 60, cw = 48, ch = 44, cols = 10, rows = 6;

    syn_hit_t g;
    hit_set_panel(&g, px, py, 560, 400);
    hit_set_grid(&g, gx, gy, cw, ch, cols, rows);

    const int x0 = px + gx, y0 = py + gy;

    /* The first cell, and its neighbours in each direction. */
    CHECK(hit_index_at(&g, x0 + 2, y0 + 2) == 0, "cell 0,0 should be index 0");
    CHECK(hit_index_at(&g, x0 + cw + 2, y0 + 2) == 1, "cell 0,1 should be index 1");
    CHECK(hit_index_at(&g, x0 + 2, y0 + ch + 2) == cols,
          "the first cell of row 1 should be index cols");
    CHECK(hit_index_at(&g, x0 + 3 * cw + 2, y0 + 2 * ch + 2) == 2 * cols + 3,
          "row 2 column 3 should be index 2*cols+3");

    /* The last drawn cell, and one past it in each direction. */
    CHECK(hit_index_at(&g, x0 + (cols - 1) * cw + 2, y0 + (rows - 1) * ch + 2)
              == rows * cols - 1,
          "the bottom-right cell should be the last index");
    CHECK(hit_index_at(&g, x0 + cols * cw + 2, y0 + 2) == -1,
          "past the last COLUMN claimed a cell");
    CHECK(hit_index_at(&g, x0 + 2, y0 + rows * ch + 2) == -1,
          "past the last ROW claimed a cell");
    CHECK(hit_index_at(&g, x0 - 2, y0 + 2) == -1,
          "left of the grid claimed a cell");

    /* Columns report themselves. */
    CHECK(hit_col_at(&g, x0 + 2, y0 + 2) == 0, "column 0 should be column 0");
    CHECK(hit_col_at(&g, x0 + 4 * cw + 2, y0 + 2) == 4, "column 4 should be column 4");

    /* first counts in CELLS for a grid, so a page scrolled by whole rows
     * offsets by (top_row * cols) and the same arithmetic serves both shapes.
     * This is the part a list-shaped assumption gets wrong. */
    hit_set_first(&g, 3 * cols);
    CHECK(hit_index_at(&g, x0 + 2, y0 + 2) == 3 * cols,
          "a scrolled grid should offset by whole rows of cells");
    CHECK(hit_index_at(&g, x0 + 2 * cw + 2, y0 + ch + 2) == 4 * cols + 2,
          "scroll offset should compose with the row/column arithmetic");

    /* And hit_set_grid() resets the offset, as hit_set_rows() does. */
    hit_set_grid(&g, gx, gy, cw, ch, cols, rows);
    CHECK(hit_index_at(&g, x0 + 2, y0 + 2) == 0,
          "hit_set_grid should have reset the scroll offset");
}

/* A list must keep answering exactly as it did before columns existed. The
 * calendar case above already leans on this, but it reads the band by hand;
 * this asserts the plain path every other panel takes. */
static void test_list_unaffected_by_cols(void)
{
    syn_hit_t g;
    hit_set_panel(&g, 0, 0, 400, 400);
    hit_set_rows(&g, 10, 20, 380, 30, 5);

    CHECK(g.cols == 1, "hit_set_rows should leave exactly one column");
    CHECK(hit_index_at(&g, 200, 20 + 2 * 30 + 2) == 2,
          "a plain list should still index by row alone");
    CHECK(hit_col_at(&g, 200, 20 + 2) == 0, "a list should always be column 0");
    /* The row spans [row_x, row_x + row_w) = [10, 390), so 389 is the last
     * pixel of it and 390 is already past — the same half-open convention the
     * grid uses per column. */
    CHECK(hit_index_at(&g, 389, 20 + 2) == 0,
          "the right-hand end of a full-width row is still that row");
    CHECK(hit_index_at(&g, 390, 20 + 2) == -1,
          "one pixel past the row's width is not that row");
}

/* ── Loose rects ─────────────────────────────────────────────
 *
 * The welcome menu's corner checkbox and the emoji picker's category tabs. Two
 * things are worth pinning: that the index answered is the ORDER they were
 * added in (the emoji picker reads it straight back as a category number, so an
 * off-by-one there filters to the wrong block), and that hit_set_panel() blanks
 * them — a panel that stops drawing a tab must stop answering for it, and that
 * happens by nobody doing anything.
 */
static void test_spots(void)
{
    syn_hit_t g;
    hit_set_panel(&g, 100, 50, 400, 300);

    /* Three tabs of different widths in a row, as the emoji strip draws them:
     * panel-local x 18, 70 and 140, all on the same 22px band. */
    CHECK(hit_add_spot(&g, 18,  38, 46, 22) == 0, "the first spot should be 0");
    CHECK(hit_add_spot(&g, 70,  38, 62, 22) == 1, "the second spot should be 1");
    CHECK(hit_add_spot(&g, 140, 38, 40, 22) == 2, "the third spot should be 2");

    CHECK(hit_spot_at(&g, 100 + 20, 50 + 40) == 0, "inside the first tab");
    CHECK(hit_spot_at(&g, 100 + 100, 50 + 48) == 1, "inside the second tab");
    CHECK(hit_spot_at(&g, 100 + 175, 50 + 59) == 2, "inside the third tab");

    /* The gutter between two tabs belongs to neither. */
    CHECK(hit_spot_at(&g, 100 + 66, 50 + 45) == -1,
          "the gap between two tabs is not a tab");
    /* Above and below the band. */
    CHECK(hit_spot_at(&g, 100 + 20, 50 + 37) == -1, "one pixel above the strip");
    CHECK(hit_spot_at(&g, 100 + 20, 50 + 60) == -1, "one pixel below the strip");
    /* Half-open on the right, like every other rect in this file. */
    CHECK(hit_spot_at(&g, 100 + 63, 50 + 45) == 0, "the last pixel of a tab");
    CHECK(hit_spot_at(&g, 100 + 64, 50 + 45) == -1, "one past a tab is not it");

    /* A zero-width label records nothing rather than a rect that answers for a
     * strip of nothing — and it must not consume an index either, or every tab
     * after it would answer with the wrong category. */
    syn_hit_t z;
    hit_set_panel(&z, 0, 0, 200, 200);
    CHECK(hit_add_spot(&z, 10, 10, 0, 20) == -1, "an empty rect is not a spot");
    CHECK(hit_add_spot(&z, 10, 10, 30, 20) == 0,
          "a dropped rect must not take an index with it");

    /* Re-recording the panel blanks them. */
    hit_set_panel(&g, 100, 50, 400, 300);
    CHECK(g.spots == 0, "hit_set_panel should blank the spot list");
    CHECK(hit_spot_at(&g, 100 + 20, 50 + 40) == -1,
          "a re-rendered panel must not answer from last frame's tabs");

    /* And so does hit_clear() — the hidden path. */
    hit_add_spot(&g, 18, 38, 46, 22);
    hit_clear(&g);
    CHECK(hit_spot_at(&g, 100 + 20, 50 + 40) == -1,
          "a hidden panel's spots must not be clickable");

    /* The overflow is a dropped rect, not a smashed struct. */
    syn_hit_t f;
    hit_set_panel(&f, 0, 0, 500, 500);
    for (int i = 0; i < SYN_HIT_SPOTS; i++)
        CHECK(hit_add_spot(&f, 0, i * 4, 10, 4) == i, "spot %d", i);
    CHECK(hit_add_spot(&f, 0, 400, 10, 4) == -1,
          "one spot past the ceiling should be refused");
    CHECK(f.spots == SYN_HIT_SPOTS, "a refused spot must not grow the list");
}

/* The welcome menu (Super+Escape), with render.c's own numbers: rows on a 28px
 * pitch whose hit band starts 20px above the first text baseline, and the
 * "Don't show again" checkbox as a loose rect in the bottom-right corner.
 *
 * Pinned because the menu is the one panel whose rows are ACTIONS — a click
 * landing one row high does not select the wrong thing, it launches the wrong
 * thing. */
static void test_welcome_rows(void)
{
    const int px = 710, py = 178, pw = 500, ph = 752;
    const int top = 128, row_h = 28, rows = 19;
    syn_hit_t g;

    hit_set_panel(&g, px, py, pw, ph);
    hit_set_rows(&g, 30, top - 20, pw - 60, row_h, rows);

    /* Row 0's band is [top-20, top+8) in panel-local y: the text baseline sits
     * 20px down it, which is what makes the row you click the row you read. */
    CHECK(hit_row_at(&g, px + 250, py + top - 21) == -1, "above row 0");
    CHECK(hit_row_at(&g, px + 250, py + top - 20) == 0,  "the top of row 0");
    CHECK(hit_row_at(&g, px + 250, py + top) == 0,       "row 0's baseline");
    CHECK(hit_row_at(&g, px + 250, py + top + 7) == 0,   "the last pixel of row 0");
    CHECK(hit_row_at(&g, px + 250, py + top + 8) == 1,   "the top of row 1");

    /* The last row, and the footer below it, where the hints are drawn. */
    CHECK(hit_row_at(&g, px + 250, py + top - 20 + (rows - 1) * row_h + 4)
          == rows - 1, "the last row");
    CHECK(hit_row_at(&g, px + 250, py + top - 20 + rows * row_h) == -1,
          "the footer is not a row");
    /* Still inside the panel, though — which is what stops a click on the
     * footer hints from closing the menu. */
    CHECK(hit_in_panel(&g, px + 250, py + ph - 8),
          "the footer is still the panel");

    /* The checkbox: right-aligned to 30px off the right edge, on the version's
     * line. Its rect is a spot, so it answers even though it is nowhere near a
     * row band. */
    const int cb_w = 130, cb_h = 22;
    const int cb_x = pw - 30 - cb_w, cb_y = ph - 16 - cb_h + 5;
    hit_add_spot(&g, cb_x - 6, cb_y, cb_w + 12, cb_h);

    CHECK(hit_spot_at(&g, px + cb_x + 4, py + cb_y + 11) == 0,
          "the checkbox should take a click on its box");
    CHECK(hit_spot_at(&g, px + cb_x + cb_w - 4, py + cb_y + 11) == 0,
          "and on the far end of its label");
    CHECK(hit_row_at(&g, px + cb_x + 4, py + cb_y + 11) == -1,
          "the checkbox must not also read as a row");
    CHECK(hit_spot_at(&g, px + 60, py + cb_y + 11) == -1,
          "the version string is not the checkbox");

    /* The corner X, 20px inset 10px off the top-right. Pinned for the same
     * reason as the rows: it is the only way out of this menu that does not
     * require knowing about Escape, and a rect that has drifted off the drawing
     * is a button that silently does nothing. */
    const int x_sz = 20, x_inset = 10;
    const int x_x = pw - x_inset - x_sz, x_y = x_inset;
    hit_set_close(&g, x_x, x_y, x_sz, x_sz);

    CHECK(hit_in_close(&g, px + x_x + 10, py + x_y + 10),
          "the middle of the close button");
    CHECK(!hit_in_close(&g, px + x_x - 2, py + x_y + 10),
          "just left of the close button is the header");
    CHECK(!hit_in_close(&g, px + x_x + 10, py + top),
          "the first row is not the close button");
    CHECK(hit_row_at(&g, px + x_x + 10, py + x_y + 10) == -1,
          "the close button must not also read as a row");
    CHECK(hit_in_panel(&g, px + x_x + 10, py + x_y + 10),
          "and it is inside the panel, so a press on it is not a click-off");
}

int main(void)
{
    test_cleared();
    test_panel_edges();
    test_power_rows();
    test_scrolled_index();
    test_calendar_band();
    test_grid();
    test_list_unaffected_by_cols();
    test_spots();
    test_welcome_rows();

    if (failures) {
        fprintf(stderr, "hit_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("hit_test: ok\n");
    return 0;
}
