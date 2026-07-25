/*
 * deskicon_drag_test.c — the desktop-icon grid, drag-to-move, and persistence.
 *
 * The compositor has no way to synthesize a pointer drag headless (wlroots'
 * headless backend has no input devices, and synui implements virtual-keyboard
 * but not virtual-pointer — see the decorations rig), so the drag is exercised
 * where it actually lives: deskmenu.c's model. This links that one file against
 * stubs for the compositor it normally talks to, points $HOME at a scratch
 * desktop and its config dir, and drives deskicon_drag_begin/motion/end directly — the same calls
 * input.c makes from pointer_button/process_pointer_motion.
 *
 * Run as:
 *     ninja -C build && ./build/deskicon_drag_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

/* ── The compositor, stubbed ─────────────────────────────── */

/* One 1920x1080 output at the layout origin, with a 40px bar along the top so
 * the usable box is not the output box — a layout bug that ignores the usable
 * area would otherwise pass. */
static struct wlr_box test_area = { .x = 0, .y = 40, .width = 1920,
                                    .height = 1040 };
static syn_output_t *the_output = (syn_output_t *)&test_area;   /* opaque here */

syn_output_t *server_primary_output(syn_server_t *s) { (void)s; return the_output; }
syn_output_t *server_focused_output(syn_server_t *s) { (void)s; return the_output; }

void output_usable_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box)
{
    (void)s; (void)o;
    *box = test_area;
}

void output_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box)
{
    (void)s; (void)o;
    box->x = 0; box->y = 0; box->width = 1920; box->height = 1080;
}

/* Renders are counted, not drawn: a drag that never repaints looks identical to
 * a working one in the model alone. */
static int render_calls;
void synui_render_deskicons(syn_server_t *s) { (void)s; render_calls++; }
void synui_render_deskmenu(syn_server_t *s)  { (void)s; }

/* Nothing on the scratch desktop is a .desktop file, so the real lookup is
 * never the thing under test here. */
const syn_icon_entry_t *icon_lookup_desktop_path(const char *p) { (void)p; return NULL; }

static char spawned[512];
void synui_spawn(const char *cmd) { snprintf(spawned, sizeof(spawned), "%s", cmd); }

void wppick_toggle(syn_server_t *s)          { (void)s; }
void theme_toggle(syn_server_t *s)           { (void)s; }
void dispcfg_toggle(syn_server_t *s)         { (void)s; }
void taskmgr_toggle(syn_server_t *s)         { (void)s; }
void synui_start_menu_open(syn_server_t *s)  { (void)s; }

/* deskicons.state lands in the scratch tree's config dir rather than the real
 * ~/.config/synui: the point of the test is not to rewrite the user's desktop. */
static char cfg_dir[512];

bool syn_config_path(char *buf, size_t n, const char *name)
{
    if (!cfg_dir[0]) return false;
    snprintf(buf, n, "%s/%s", cfg_dir, name);
    return true;
}

void syn_config_ensure_dir(void) { mkdir(cfg_dir, 0755); }

/* ── Helpers ─────────────────────────────────────────────── */

#define CELL_X(col) (test_area.x + SYN_DESKICON_PAD + (col) * SYN_DESKICON_W)
#define CELL_Y(row) (test_area.y + SYN_DESKICON_PAD + (row) * SYN_DESKICON_H)

static int rows_of(void)
{
    int r = (test_area.height - 2 * SYN_DESKICON_PAD) / SYN_DESKICON_H;
    return r < 1 ? 1 : r;
}

static int find_icon(syn_server_t *s, const char *label)
{
    for (int i = 0; i < s->deskicon_count; i++)
        if (strcmp(s->deskicons[i].label, label) == 0) return i;
    return -1;
}

/* Assert icon `label` sits exactly on cell (col,row). */
static void assert_cell(syn_server_t *s, const char *label, int col, int row)
{
    int i = find_icon(s, label);
    assert(i >= 0);
    if (s->deskicons[i].x != CELL_X(col) || s->deskicons[i].y != CELL_Y(row)) {
        fprintf(stderr, "FAIL: %s at (%d,%d), expected cell (%d,%d) = (%d,%d)\n",
                label, s->deskicons[i].x, s->deskicons[i].y, col, row,
                CELL_X(col), CELL_Y(row));
        abort();
    }
}

/* Drag icon `label` by (dx,dy) from the middle of its cell, in a few steps so
 * the slop is crossed the way a real pointer crosses it. */
static void drag(syn_server_t *s, const char *label, double dx, double dy)
{
    int i = find_icon(s, label);
    assert(i >= 0);

    double px = s->deskicons[i].x + SYN_DESKICON_W / 2.0;
    double py = s->deskicons[i].y + SYN_DESKICON_H / 2.0;

    deskicon_drag_begin(s, i, px, py);
    for (int step = 1; step <= 4; step++)
        deskicon_drag_motion(s, px + dx * step / 4.0, py + dy * step / 4.0);
    deskicon_drag_end(s, px + dx, py + dy);
}

/* Every icon on its own cell — the property the whole two-pass layout exists
 * to keep. */
static void assert_no_overlap(syn_server_t *s)
{
    for (int i = 0; i < s->deskicon_count; i++)
        for (int k = i + 1; k < s->deskicon_count; k++)
            if (s->deskicons[i].x == s->deskicons[k].x &&
                s->deskicons[i].y == s->deskicons[k].y) {
                fprintf(stderr, "FAIL: %s and %s both at (%d,%d)\n",
                        s->deskicons[i].label, s->deskicons[k].label,
                        s->deskicons[i].x, s->deskicons[i].y);
                abort();
            }
}

static void write_file(const char *path, const char *body)
{
    FILE *f = fopen(path, "w");
    assert(f);
    if (body) fputs(body, f);
    fclose(f);
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    static char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(void)
{
    /* Scratch $HOME with a five-file desktop. Names are deliberately out of
     * readdir order; the layout has to be name-ordered. */
    char home[] = "/tmp/synui-deskicon-XXXXXX";
    assert(mkdtemp(home));

    char desk[600], path[700];
    snprintf(desk, sizeof(desk), "%s/Desktop", home);
    assert(mkdir(desk, 0755) == 0);
    snprintf(cfg_dir, sizeof(cfg_dir), "%s/.config/synui", home);
    {
        char parent[600];
        snprintf(parent, sizeof(parent), "%s/.config", home);
        assert(mkdir(parent, 0755) == 0);
    }

    static const char *names[] = { "delta.txt", "bravo.txt", "echo.txt",
                                  "alpha.txt", "charlie.txt" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", desk, names[i]);
        write_file(path, "x\n");
    }

    setenv("HOME", home, 1);

    syn_server_t *s = calloc(1, sizeof(*s));
    assert(s);
    s->deskicon_selected       = -1;
    s->deskicon_last_click_idx = -1;
    s->deskicon_drag.idx       = -1;
    s->config.desktop_icons    = true;

    /* ── 1. The auto-grid ───────────────────────────────── */
    deskicons_reload(s);
    assert(s->deskicon_count == 5);
    assert(rows_of() == 10);   /* (1040 - 32) / 92, floored */

    /* Column-major, name order, inside the usable box (y starts at 40+16). */
    assert_cell(s, "alpha.txt",   0, 0);
    assert_cell(s, "bravo.txt",   0, 1);
    assert_cell(s, "charlie.txt", 0, 2);
    assert_cell(s, "delta.txt",   0, 3);
    assert_cell(s, "echo.txt",    0, 4);
    assert_no_overlap(s);
    printf("ok 1 — auto-grid: 5 icons, column-major, in the usable box\n");

    /* ── 2. A press with no travel is still a click ─────── */
    int before_x = s->deskicons[find_icon(s, "alpha.txt")].x;
    drag(s, "alpha.txt", 3, 2);   /* 3.6px total: under the 6px slop */
    assert(s->deskicons[find_icon(s, "alpha.txt")].x == before_x);
    assert(s->deskicons[find_icon(s, "alpha.txt")].placed == 0);
    {
        char st[700];
        snprintf(st, sizeof(st), "%s/deskicons.state", cfg_dir);
        assert(slurp(st) == NULL);   /* a click must not write state */
    }
    printf("ok 2 — a sub-slop press moves nothing and writes no state\n");

    /* ── 3. A real drag lands on a cell and reflows the rest ── */
    render_calls = 0;
    drag(s, "charlie.txt", 2 * SYN_DESKICON_W + 9, 3 * SYN_DESKICON_H - 11);
    assert(render_calls > 0);              /* it repainted while dragging */
    assert_cell(s, "charlie.txt", 2, 5);   /* row 2 + 3, snapped off the +9/-11 */
    assert(s->deskicons[find_icon(s, "charlie.txt")].placed == 1);

    /* The cell it vacated closes up; the other four keep their name order. */
    assert_cell(s, "alpha.txt", 0, 0);
    assert_cell(s, "bravo.txt", 0, 1);
    assert_cell(s, "delta.txt", 0, 2);
    assert_cell(s, "echo.txt",  0, 3);
    assert_no_overlap(s);
    printf("ok 3 — a drag snaps to the nearest cell and the flow closes up\n");

    /* ── 4. It was written to deskicons.state ───────────── */
    char statefile[700];
    snprintf(statefile, sizeof(statefile), "%s/deskicons.state", cfg_dir);
    char *body = slurp(statefile);
    assert(body);
    {
        char want[128];
        snprintf(want, sizeof(want), "pos=%d,%d,charlie.txt\n",
                 CELL_X(2), CELL_Y(5));
        if (strcmp(body, want) != 0) {
            fprintf(stderr, "FAIL: deskicons.state is\n%s\nexpected\n%s", body, want);
            abort();
        }
    }
    printf("ok 4 — only the dragged icon is persisted, keyed on its name\n");

    /* ── 5. It comes back there ─────────────────────────── */
    deskicons_reload(s);
    assert(s->deskicon_count == 5);
    assert_cell(s, "charlie.txt", 2, 5);
    assert(s->deskicons[find_icon(s, "charlie.txt")].placed == 1);
    assert_cell(s, "alpha.txt", 0, 0);
    assert_cell(s, "bravo.txt", 0, 1);
    assert_cell(s, "delta.txt", 0, 2);
    assert_cell(s, "echo.txt",  0, 3);
    assert_no_overlap(s);
    printf("ok 5 — a reload puts the dragged icon back on its own cell\n");

    /* ── 6. Dropping onto an occupied cell swaps ────────── */
    drag(s, "echo.txt",
         CELL_X(0) - s->deskicons[find_icon(s, "echo.txt")].x,
         CELL_Y(0) - s->deskicons[find_icon(s, "echo.txt")].y);
    assert_cell(s, "echo.txt",  0, 0);   /* the drop wins its target */
    assert_cell(s, "alpha.txt", 0, 3);   /* and alpha takes echo's old cell */
    assert(s->deskicons[find_icon(s, "alpha.txt")].placed == 1);
    assert_no_overlap(s);
    printf("ok 6 — a drop onto an occupied cell swaps the two\n");

    /* ── 7. A stale, off-screen cell is clamped back on ── */
    write_file(statefile,
               "pos=99999,99999,delta.txt\n"
               "pos=-4000,-4000,bravo.txt\n"
               "# a comment, and a line the parser must not choke on\n"
               "pos=garbage\n");
    deskicons_reload(s);
    assert_no_overlap(s);
    for (int i = 0; i < s->deskicon_count; i++) {
        syn_deskicon_t *ic = &s->deskicons[i];
        if (ic->x < test_area.x || ic->y < test_area.y ||
            ic->x + SYN_DESKICON_W  > test_area.x + test_area.width ||
            ic->y + SYN_DESKICON_H  > test_area.y + test_area.height) {
            fprintf(stderr, "FAIL: %s escaped the usable box at (%d,%d)\n",
                    ic->label, ic->x, ic->y);
            abort();
        }
    }
    /* Far bottom-right clamps to the last cell; far top-left to the first. */
    assert_cell(s, "bravo.txt",  0,  0);   /* -4000,-4000 → the first cell */
    assert_cell(s, "delta.txt", 18,  9);   /* 99999,99999 → the last one */
    printf("ok 7 — stale coords clamp onto the current grid; junk lines ignored\n");

    /* ── 8. A drag cannot escape the usable box mid-flight ── */
    {
        int i = find_icon(s, "alpha.txt");
        deskicon_drag_begin(s, i, s->deskicons[i].x + 10, s->deskicons[i].y + 10);
        deskicon_drag_motion(s, -9000, -9000);
        assert(s->deskicons[i].x == test_area.x && s->deskicons[i].y == test_area.y);
        deskicon_drag_motion(s, 9000, 9000);
        assert(s->deskicons[i].x == test_area.x + test_area.width  - SYN_DESKICON_W);
        assert(s->deskicons[i].y == test_area.y + test_area.height - SYN_DESKICON_H);
        deskicon_drag_end(s, 9000, 9000);
        assert_no_overlap(s);
    }
    printf("ok 8 — a drag stays inside the usable box it is drawn into\n");

    /* ── 9. A rescan cancels an in-flight drag ──────────── */
    {
        int i = find_icon(s, "bravo.txt");
        deskicon_drag_begin(s, i, s->deskicons[i].x + 10, s->deskicons[i].y + 10);
        deskicons_reload(s);
        assert(s->deskicon_drag.active == 0);
        assert(s->deskicon_drag.idx    == -1);
        /* And a stray release afterwards is a no-op, not a move. */
        int x = s->deskicons[find_icon(s, "bravo.txt")].x;
        deskicon_drag_end(s, 500, 500);
        assert(s->deskicons[find_icon(s, "bravo.txt")].x == x);
    }
    printf("ok 9 — a rescan cancels the drag rather than moving the wrong icon\n");

    /* ── 10. Turning icons off, then on, keeps the layout ── */
    s->config.desktop_icons = false;
    deskicons_reload(s);
    assert(s->deskicon_count == 0);
    s->config.desktop_icons = true;
    deskicons_reload(s);
    assert(s->deskicon_count == 5);
    assert_cell(s, "alpha.txt", 18,  9);   /* where test 8 dropped it */
    assert_cell(s, "delta.txt",  0,  1);   /* the cell test 8's swap gave it */
    assert_cell(s, "bravo.txt",  0,  0);
    assert_no_overlap(s);
    printf("ok 10 — the placements survive the desktop_icons toggle\n");

    /* Leave the scratch tree behind only on success. */
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", desk, names[i]);
        unlink(path);
    }
    unlink(statefile);
    rmdir(cfg_dir);
    snprintf(path, sizeof(path), "%s/.config", home);
    rmdir(path);
    rmdir(desk);
    rmdir(home);
    free(s);

    printf("\nall deskicon drag/persistence checks passed\n");
    return 0;
}
