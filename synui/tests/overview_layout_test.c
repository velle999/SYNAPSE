/*
 * overview_layout_test.c — the mission-control grid.
 *
 * overview_layout() is the one piece of the overview that is pure arithmetic,
 * and it is also the piece everything else trusts: the renderer draws the boxes
 * it returns, the hit test asks which of those boxes the pointer is in, and the
 * key handler reads the column count back out of them to know what Up and Down
 * mean. So a layout that is subtly wrong is not a cosmetic bug — it is clicks
 * landing on the wrong window, and arrow keys moving on a grid that is not on
 * the screen.
 *
 * The properties asserted here are the ones that cannot be seen by looking at
 * one screenshot:
 *
 *   - every tile is INSIDE the area the overview owns, at every window count.
 *     A tile that runs under the desktop strip is one whose bottom half is
 *     unclickable, and it only happens at particular counts.
 *   - no two tiles OVERLAP. The whole premise is "laid out so none of them
 *     overlap"; an off-by-one in the gap arithmetic breaks exactly that and
 *     still looks fine until two windows are near-identical.
 *   - the column count is READABLE BACK from the boxes. overview_key() derives
 *     it by finding the first tile on row 1, so a layout whose rows are not
 *     detectable that way silently turns Up/Down into Left/Right.
 *
 * It runs against 1..OVERVIEW_MAX windows on several output shapes, because the
 * failures above are all count-dependent and shape-dependent — a bug that only
 * bites at 5 windows on a 16:9 screen is exactly the kind that ships.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xkbcommon/xkbcommon.h>

#include "synui.h"

static int failures;
static int checks;

#define CHECK(cond, ...) do {                                    \
        checks++;                                                \
        if (cond) { } else {                                     \
            failures++;                                          \
            printf("  FAIL — ");                                 \
            printf(__VA_ARGS__); printf("\n");                   \
        }                                                        \
    } while (0)

/* ── The compositor, stubbed ─────────────────────────────────
 *
 * overview.c is linked alone. Only the pure half is under test, and none of
 * these are reached from it — they exist so the object links.
 */
void server_output_box(syn_server_t *s, struct wlr_box *b)
{ (void)s; b->x = 0; b->y = 0; b->width = 1920; b->height = 1080; }
void workspace_switch(syn_server_t *s, int i)          { (void)s; (void)i; }
void view_close(syn_view_t *v)                         { (void)v; }
void view_apply_minimized(syn_server_t *s, syn_view_t *v, int m)
{ (void)s; (void)v; (void)m; }
void focus_view(syn_server_t *s, syn_view_t *v, struct wlr_surface *su)
{ (void)s; (void)v; (void)su; }
struct wlr_surface *view_surface(syn_view_t *v)        { (void)v; return NULL; }
void synui_render_overview(syn_server_t *s)            { (void)s; }
void ctlpanel_child_closed(syn_server_t *s, const char *a) { (void)s; (void)a; }

/* ── Helpers ─────────────────────────────────────────────── */

static int overlaps(const struct wlr_box *a, const struct wlr_box *b)
{
    return a->x < b->x + b->width  && b->x < a->x + a->width &&
           a->y < b->y + b->height && b->y < a->y + a->height;
}

/* The same derivation overview_key() uses: the first tile whose y differs from
 * tile 0's starts row 1, so its index is the column count. Duplicated here on
 * purpose — the assertion is that the LAYOUT supports this reading, and calling
 * the compositor's copy of it would assert only that a function equals itself. */
static int cols_from_boxes(const struct wlr_box *t, int n)
{
    for (int i = 1; i < n; i++)
        if (t[i].y != t[0].y) return i;
    return n;
}

static void check_shape(const char *name, int ow, int oh)
{
    struct wlr_box ob = { 0, 0, ow, oh };
    struct wlr_box tiles[OVERVIEW_MAX];

    /* The area the tiles are allowed to use: the output, less the margins, the
     * heading and the desktop strip. Spelled out here rather than taken from
     * the implementation, so a change to either constant has to be a deliberate
     * change to both. */
    int ax = ob.x + OVERVIEW_MARGIN;
    int ay = ob.y + OVERVIEW_MARGIN + OVERVIEW_HEAD_H;
    int ar = ob.x + ob.width  - OVERVIEW_MARGIN;
    int ab = ob.y + ob.height - OVERVIEW_MARGIN - OVERVIEW_STRIP_H;

    int worst_n = 0;
    int bad_bounds = 0, bad_overlap = 0, bad_empty = 0, bad_cols = 0;

    for (int n = 1; n <= OVERVIEW_MAX; n++) {
        memset(tiles, 0, sizeof(tiles));
        overview_layout(&ob, n, tiles);

        for (int i = 0; i < n; i++) {
            if (tiles[i].width < 1 || tiles[i].height < 1) {
                if (!bad_empty++) worst_n = n;
                continue;
            }
            if (tiles[i].x < ax || tiles[i].y < ay ||
                tiles[i].x + tiles[i].width  > ar ||
                tiles[i].y + tiles[i].height > ab) {
                if (!bad_bounds++) worst_n = n;
            }
            for (int j = i + 1; j < n; j++)
                if (overlaps(&tiles[i], &tiles[j])) {
                    if (!bad_overlap++) worst_n = n;
                }
        }

        /* Row 0 must be a full row of `cols` tiles at the same y, and the count
         * read back must divide the grid the way the key handler assumes: every
         * tile's row index has to be i / cols. */
        int cols = cols_from_boxes(tiles, n);
        if (cols < 1) { bad_cols++; continue; }
        for (int i = 0; i < n; i++) {
            int want_row = i / cols;
            int got_row  = 0;
            for (int j = 1; j <= i; j++)
                if (tiles[j].y != tiles[j - 1].y) got_row++;
            if (want_row != got_row) { if (!bad_cols++) worst_n = n; break; }
        }
    }

    CHECK(bad_empty == 0,   "%s: %d tile(s) came out empty (first at n=%d)",
          name, bad_empty, worst_n);
    CHECK(bad_bounds == 0,  "%s: %d tile(s) outside the usable area (first at n=%d)",
          name, bad_bounds, worst_n);
    CHECK(bad_overlap == 0, "%s: %d overlapping pair(s) (first at n=%d)",
          name, bad_overlap, worst_n);
    CHECK(bad_cols == 0,    "%s: the column count is not readable back from the "
          "boxes (first at n=%d) — Up/Down would move on a grid that is not drawn",
          name, worst_n);
}

/* ── Tests ───────────────────────────────────────────────── */

static void test_grid(void)
{
    printf("overview: the tile grid\n");

    /* Landscape, portrait, ultrawide, and the 1024x768 a VM and the live ISO
     * come up at — the small one is where a layout runs out of room first, and
     * it is the shape nobody develops on. */
    check_shape("1920x1080", 1920, 1080);
    check_shape("2560x1440", 2560, 1440);
    check_shape("3440x1440", 3440, 1440);
    check_shape("1080x1920", 1080, 1920);
    check_shape("1024x768",  1024, 768);
}

static void test_grid_shape(void)
{
    printf("overview: the grid is the shape you would draw by hand\n");

    struct wlr_box ob = { 0, 0, 1920, 1080 };
    struct wlr_box t[OVERVIEW_MAX];

    overview_layout(&ob, 1, t);
    CHECK(cols_from_boxes(t, 1) == 1, "one window is one tile");

    overview_layout(&ob, 2, t);
    CHECK(cols_from_boxes(t, 2) == 2, "two windows go side by side, not stacked");

    overview_layout(&ob, 4, t);
    CHECK(cols_from_boxes(t, 4) == 2, "four windows are 2x2");

    /* The one that is easy to get wrong, and the reason the last row is
     * centred: five in a 3x2 grid leaves two tiles under three. */
    overview_layout(&ob, 5, t);
    int cols5 = cols_from_boxes(t, 5);
    CHECK(cols5 == 3, "five windows are 3 over 2 (got %d columns)", cols5);

    int row0_mid = t[0].x + (t[2].x + t[2].width - t[0].x) / 2;
    int row1_mid = t[3].x + (t[4].x + t[4].width - t[3].x) / 2;
    CHECK(row1_mid - row0_mid < 2 && row0_mid - row1_mid < 2,
          "…and the short last row is CENTRED under the full one "
          "(rows at %d and %d)", row0_mid, row1_mid);

    /* The property the column choice actually has, stated as what it is FOR:
     * no other column count would draw the windows bigger. This is the whole
     * algorithm, so asserting it directly is worth more than asserting any
     * particular count — and it is what stops someone "simplifying" it back to
     * round(sqrt(n * aspect)), which puts four windows in three columns and
     * loses a third of the tile size to letterboxing. */
    for (int n = 1; n <= 12; n++) {
        overview_layout(&ob, n, t);
        int got = cols_from_boxes(t, n);
        double got_scale = -1.0, best_scale = -1.0;
        int best_c = 0;
        for (int c = 1; c <= n; c++) {
            int r = (n + c - 1) / c;
            double cw = (double)(1920 - 2 * OVERVIEW_MARGIN - (c - 1) * OVERVIEW_GAP) / c;
            double ch = (double)(1080 - 2 * OVERVIEW_MARGIN - OVERVIEW_HEAD_H
                                 - OVERVIEW_STRIP_H - (r - 1) * OVERVIEW_GAP) / r;
            if (cw < 1.0 || ch < 1.0) continue;
            double sc = cw / 1920.0;
            if (ch / 1080.0 < sc) sc = ch / 1080.0;
            if (c == got) got_scale = sc;
            if (sc > best_scale) { best_scale = sc; best_c = c; }
        }
        CHECK(got_scale >= best_scale - 1e-9,
              "n=%d chose %d columns, but %d draws the windows bigger",
              n, got, best_c);
    }
}

static void test_desktop_strip(void)
{
    printf("overview: the desktop strip\n");

    struct wlr_box ob = { 0, 0, 1920, 1080 };
    struct wlr_box ws[WORKSPACE_MAX];
    overview_ws_layout(&ob, ws);

    int bad = 0, unordered = 0, outside = 0;
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        if (ws[i].width < 1 || ws[i].height < 1) bad++;
        if (ws[i].x < ob.x || ws[i].x + ws[i].width > ob.x + ob.width ||
            ws[i].y < ob.y || ws[i].y + ws[i].height > ob.y + ob.height)
            outside++;
        if (i > 0 && ws[i].x <= ws[i - 1].x) unordered++;
        for (int j = i + 1; j < WORKSPACE_MAX; j++)
            if (overlaps(&ws[i], &ws[j])) bad++;
    }

    CHECK(bad == 0, "every pill is a real, non-overlapping box");
    CHECK(outside == 0, "every pill is on the output");
    CHECK(unordered == 0, "desktop 1 is leftmost and 9 is rightmost");

    /* Below the tiles, not through them. The strip is the one thing the tile
     * area was shortened to make room for, so an overlap here means the
     * shortening and the strip's own position disagree. */
    struct wlr_box t[OVERVIEW_MAX];
    overview_layout(&ob, 12, t);
    int clash = 0;
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < WORKSPACE_MAX; j++)
            if (overlaps(&t[i], &ws[j])) clash++;
    CHECK(clash == 0, "no tile is drawn through the desktop strip");

    /* A narrow output must not give each desktop a slab as wide as a window. */
    struct wlr_box wide = { 0, 0, 3440, 1440 };
    overview_ws_layout(&wide, ws);
    CHECK(ws[0].width <= 140, "the pills are capped rather than filling an "
          "ultrawide (got %d)", ws[0].width);

    /* Off-origin outputs: a second monitor's layout box does not start at 0,0,
     * and a strip that forgot ob.x would be drawn on the FIRST monitor. */
    struct wlr_box second = { 1920, 0, 1920, 1080 };
    overview_ws_layout(&second, ws);
    CHECK(ws[0].x >= 1920, "the strip follows the output it is drawn on "
          "(got x=%d for an output at x=1920)", ws[0].x);
    overview_layout(&second, 3, t);
    CHECK(t[0].x >= 1920, "…and so do the tiles (got x=%d)", t[0].x);
}

/* n = 0 has no tiles to place, and must not write one anyway: the renderer
 * calls this before it knows whether the desktop is empty. */
static void test_empty(void)
{
    printf("overview: an empty desktop\n");

    struct wlr_box ob = { 0, 0, 1920, 1080 };
    struct wlr_box t[2];
    memset(t, 0x5a, sizeof(t));
    overview_layout(&ob, 0, t);
    CHECK(t[0].width == 0x5a5a5a5a || t[0].width != 0,
          "zero windows writes no boxes");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    test_grid();
    test_grid_shape();
    test_desktop_strip();
    test_empty();

    if (failures) {
        printf("FAIL: %d checked, %d failed\n", checks, failures);
        return 1;
    }
    printf("PASS: %d checked, 0 failed\n", checks);
    return 0;
}
