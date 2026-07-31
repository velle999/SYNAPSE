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

int main(void)
{
    test_cleared();
    test_panel_edges();
    test_power_rows();
    test_scrolled_index();
    test_calendar_band();

    if (failures) {
        fprintf(stderr, "hit_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("hit_test: ok\n");
    return 0;
}
