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
#include <time.h>
#include <unistd.h>
#include <utime.h>

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

/* deskmenu_open() asks the layout which output the cursor is over, to clamp the
 * menu inside it. Standing up a real wlr_output_layout here would mean a
 * wl_display and a backend for one lookup, so the wlroots symbol is overridden
 * instead — a strong definition in the test object wins over the shared
 * library's. NULL means "no output there", which is the path that skips the
 * clamp and leaves the menu where it was asked for. */
struct wlr_output *wlr_output_layout_output_at(struct wlr_output_layout *l,
                                               double lx, double ly)
{
    (void)l; (void)lx; (void)ly;
    return NULL;
}

/* Renders are counted, not drawn: a drag that never repaints looks identical to
 * a working one in the model alone. The two paths are counted apart because
 * which one a motion takes is the whole difference between a drag that keeps up
 * with the cursor and one that does not — a repaint rebuilds a screen-sized
 * cairo surface, a move is a scene-node reposition. */
static int render_calls, move_calls;
void synui_render_deskicons(syn_server_t *s) { (void)s; render_calls++; }
void synui_move_deskicon_drag(syn_server_t *s) { (void)s; move_calls++; }
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

/* A file of exactly `bytes`, so an arrange-by-size assertion is about the sort
 * and not about how large "x\n" happens to be. */
static void write_sized(const char *path, size_t bytes)
{
    FILE *f = fopen(path, "w");
    assert(f);
    for (size_t i = 0; i < bytes; i++) fputc('x', f);
    fclose(f);
}

/* mtime `secs` in the past. Arrange-by-date has to be driven off stamps the
 * test chose: five files written in a loop can easily share a second. */
static void set_age(const char *path, time_t secs)
{
    time_t now = time(NULL);
    struct utimbuf t = { .actime = now - secs, .modtime = now - secs };
    assert(utime(path, &t) == 0);
}

/* The labels of the first `n` icons in flow order (column-major down column 0,
 * which is where the first ten land on this grid). */
static void assert_order(syn_server_t *s, const char *const *want, int n)
{
    for (int row = 0; row < n; row++) {
        int i = find_icon(s, want[row]);
        assert(i >= 0);
        if (s->deskicons[i].x != CELL_X(0) || s->deskicons[i].y != CELL_Y(row)) {
            fprintf(stderr, "FAIL: expected %s in row %d; it is at (%d,%d)\n",
                    want[row], row, s->deskicons[i].x, s->deskicons[i].y);
            for (int k = 0; k < s->deskicon_count; k++)
                fprintf(stderr, "      %-14s (%d,%d)\n", s->deskicons[k].label,
                        s->deskicons[k].x, s->deskicons[k].y);
            abort();
        }
    }
}

/* Index of the menu row whose label is `label`, or -1. */
static int menu_row(syn_server_t *s, const char *label)
{
    for (int i = 0; i < s->deskmenu.action_count; i++)
        if (strcmp(deskact_label(s->deskmenu.actions[i]), label) == 0) return i;
    return -1;
}

/* Click the menu row named `label`, the way a pointer would: through
 * deskmenu_click, at a point inside that row. */
static void click_row(syn_server_t *s, const char *label)
{
    int i = menu_row(s, label);
    assert(i >= 0);
    deskmenu_click(s, s->deskmenu.x + 20,
                   s->deskmenu.y + deskmenu_row_top(s, i) +
                       deskmenu_row_height(s, i) / 2.0);
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
    render_calls = move_calls = 0;
    drag(s, "charlie.txt", 2 * SYN_DESKICON_W + 9, 3 * SYN_DESKICON_H - 11);
    /* Four motion steps, and the icon travelled on every one. Exactly two of
     * them may repaint the desktop: the step that crosses the slop and lifts
     * the icon into its own layer, and the drop. The rest must take the cheap
     * path, or a drag is back to allocating a full-screen surface per event. */
    assert(render_calls == 2);
    assert(move_calls == 3);
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
        snprintf(want, sizeof(want),
                 "icons=on\narrange=name\npos=%d,%d,charlie.txt\n",
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

    /* ── Arrange by … ───────────────────────────────────── */

    /* Four more entries whose size, extension and mtime all disagree with
     * their name order, so no two arrange modes can pass by accident. The five
     * .txt files stay 2 bytes each: equal keys are what proves the name
     * tie-break. */
    char folder[700];
    snprintf(folder, sizeof(folder), "%s/folder", desk);
    assert(mkdir(folder, 0755) == 0);

    static const char *extras[] = { "zulu.bin", "mike.dat", "kilo.log" };
    static const size_t extra_size[] = { 3000, 500, 10 };
    for (unsigned i = 0; i < sizeof(extras) / sizeof(extras[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", desk, extras[i]);
        write_sized(path, extra_size[i]);
    }

    /* Ages, oldest to newest: zulu.bin, the .txt files, kilo.log, mike.dat. */
    snprintf(path, sizeof(path), "%s/zulu.bin", desk); set_age(path, 9000);
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", desk, names[i]);
        set_age(path, 3600);
    }
    snprintf(path, sizeof(path), "%s/kilo.log", desk); set_age(path, 600);
    snprintf(path, sizeof(path), "%s/mike.dat", desk); set_age(path, 5);
    set_age(folder, 60);

    /* ── 11. By size: folders first, then largest first ── */
    deskicons_reload(s);
    assert(s->deskicon_count == 9);
    deskicons_arrange(s, SYN_ARRANGE_SIZE);
    assert(s->config.desktop_icon_arrange == SYN_ARRANGE_SIZE);
    {
        static const char *const want[] = { "folder", "zulu.bin", "mike.dat",
                                            "kilo.log", "alpha.txt", "bravo.txt",
                                            "charlie.txt", "delta.txt",
                                            "echo.txt" };
        assert_order(s, want, 9);
    }
    assert_no_overlap(s);
    printf("ok 11 — arrange by size: folder first, then largest, name breaks ties\n");

    /* ── 12. Arranging un-pins every dragged icon ───────── */
    for (int i = 0; i < s->deskicon_count; i++)
        assert(s->deskicons[i].placed == 0);
    {
        /* And the state file kept the mode while dropping the placements —
         * otherwise the next reload would drag them straight back out of the
         * order just chosen. */
        char *st = slurp(statefile);
        assert(st);
        if (strcmp(st, "icons=on\narrange=size\n") != 0) {
            fprintf(stderr, "FAIL: deskicons.state is\n%s\nexpected arrange=size\n", st);
            abort();
        }
    }
    printf("ok 12 — an arrange clears the pinned cells and records the mode\n");

    /* ── 13. By date: newest first ──────────────────────── */
    deskicons_arrange(s, SYN_ARRANGE_DATE);
    {
        static const char *const want[] = { "mike.dat", "folder", "kilo.log" };
        assert_order(s, want, 3);
    }
    /* The five equally-aged .txt files land between kilo.log and zulu.bin, in
     * name order, and the oldest file is last. */
    assert_cell(s, "alpha.txt", 0, 3);
    assert_cell(s, "echo.txt",  0, 7);
    assert_cell(s, "zulu.bin",  0, 8);
    assert_no_overlap(s);
    printf("ok 13 — arrange by date: newest first, equal stamps in name order\n");

    /* ── 14. By type: folders, then extension ───────────── */
    deskicons_arrange(s, SYN_ARRANGE_TYPE);
    {
        static const char *const want[] = { "folder", "zulu.bin", "mike.dat",
                                            "kilo.log", "alpha.txt" };
        assert_order(s, want, 5);
    }
    assert_no_overlap(s);
    printf("ok 14 — arrange by type: folder first, then .bin/.dat/.log/.txt\n");

    /* ── 15. The mode outlives the session ──────────────── */
    s->config.desktop_icon_arrange = SYN_ARRANGE_NAME;   /* as if freshly parsed */
    deskicons_reload(s);
    assert(s->config.desktop_icon_arrange == SYN_ARRANGE_TYPE);
    assert_cell(s, "folder", 0, 0);
    printf("ok 15 — deskicons.state restores the arrange mode over the config\n");

    /* ── 16. The menu offers it, and shows which one is on ── */
    deskmenu_open(s, 100, 100);
    assert(menu_row(s, "Arrange by Name") >= 0);
    assert(menu_row(s, "Arrange by Size") >= 0);
    assert(s->deskmenu.action_count <= SYN_DESKMENU_MAX);
    assert(deskmenu_row_checked(s, menu_row(s, "Arrange by Type")));
    assert(!deskmenu_row_checked(s, menu_row(s, "Arrange by Name")));

    /* And clicking one is the whole path: row → mode → re-flow. */
    click_row(s, "Arrange by Name");
    assert(s->deskmenu.visible == 0);
    assert(s->config.desktop_icon_arrange == SYN_ARRANGE_NAME);
    {
        static const char *const want[] = { "alpha.txt", "bravo.txt",
                                            "charlie.txt", "delta.txt",
                                            "echo.txt", "folder", "kilo.log",
                                            "mike.dat", "zulu.bin" };
        assert_order(s, want, 9);
    }
    printf("ok 16 — the menu row is checked, and clicking it re-flows the desktop\n");

    /* ── 17. With icons off the arrange rows are not offered ── */
    s->config.desktop_icons = false;
    deskicons_reload(s);
    deskmenu_open(s, 100, 100);
    assert(menu_row(s, "Arrange by Name") < 0);
    assert(menu_row(s, "Task Manager") >= 0);
    deskmenu_close(s);
    s->config.desktop_icons = true;
    printf("ok 17 — no desktop, no arrange rows\n");

    /* ── 18. The tick itself outlives the session ────────── */
    deskicons_reload(s);
    assert(s->deskicon_count == 9);
    /* One pinned icon, so the round trip has something to lose. */
    drag(s, "zulu.bin", 3 * SYN_DESKICON_W, 0);
    assert(s->deskicons[find_icon(s, "zulu.bin")].placed == 1);

    deskmenu_open(s, 100, 100);
    click_row(s, "Show Desktop Icons");
    assert(s->config.desktop_icons == false);
    assert(s->deskicon_count == 0);
    {
        /* Off is written, and the pin is still there — the save has to land
         * before the rescan that empties the model, or the placement goes out
         * with the toggle. */
        char *st = slurp(statefile);
        assert(st);
        assert(strncmp(st, "icons=off\n", 10) == 0);
        assert(strstr(st, ",zulu.bin\n") != NULL);
    }
    /* A freshly parsed config — synuirc's default is off, and the state file is
     * what a login has to read to agree with the menu. */
    {
        static syn_config_t cfg;
        cfg.desktop_icons = true;             /* as if synuirc said `on` */
        deskicons_state_load(&cfg);
        assert(cfg.desktop_icons == false);   /* the menu's off wins */
    }
    printf("ok 18 — turning icons off is persisted, and keeps the pinned cells\n");

    /* ── 19. And back on, without losing the cell ────────── */
    deskmenu_open(s, 100, 100);
    click_row(s, "Show Desktop Icons");
    assert(s->config.desktop_icons == true);
    assert(s->deskicon_count == 9);
    assert(s->deskicons[find_icon(s, "zulu.bin")].placed == 1);
    {
        char *st = slurp(statefile);
        assert(st);
        assert(strncmp(st, "icons=on\n", 9) == 0);
        assert(strstr(st, ",zulu.bin\n") != NULL);

        static syn_config_t cfg;
        cfg.desktop_icons = false;            /* synuirc's default */
        deskicons_state_load(&cfg);
        assert(cfg.desktop_icons == true);
    }
    printf("ok 19 — turning them back on is persisted too, cell intact\n");

    /* ── 20. A smaller usable box does not eat the placement ── */
    /*
     * Everything that re-grids the desktop re-snaps the pinned icons against the
     * box in force at the time, and that box is not a constant: the bar reserves
     * its strip only once quickshell has started, a monitor can be unplugged, and
     * a mode change resizes the one that is left. A pin clamped onto a smaller
     * grid has to be exactly that — clamped for as long as the grid is small —
     * and not a new placement, or one session on a 800x480 output would be the
     * last one the user's arrangement survived.
     */
    {
        int i = find_icon(s, "zulu.bin");
        drag(s, "zulu.bin", CELL_X(5) - s->deskicons[i].x,
                            CELL_Y(8) - s->deskicons[i].y);
        assert_cell(s, "zulu.bin", 5, 8);
        assert_no_overlap(s);
    }

    struct wlr_box big = test_area;

    /* A grid of 8x4 cells: the pinned column still exists, the row does not. */
    test_area = (struct wlr_box){ .x = 0, .y = 40, .width = 800, .height = 480 };
    deskicons_layout(s);
    assert_cell(s, "zulu.bin", 5, 3);   /* clamped onto the last row it has */
    assert_no_overlap(s);

    test_area = big;
    deskicons_layout(s);
    assert_cell(s, "zulu.bin", 5, 8);   /* and back where the user left it */
    assert_no_overlap(s);
    printf("ok 20 — a pin clamped onto a smaller grid returns when it grows\n");

    /* ── 21. And the clamp is never what gets written ────── */
    /*
     * The save is driven by whatever the user does next, which may well be a
     * drag while the small monitor is the only one there. Writing the clamped
     * cell would make the loss permanent the moment it reached the file.
     */
    test_area = (struct wlr_box){ .x = 0, .y = 40, .width = 800, .height = 480 };
    deskicons_layout(s);
    drag(s, "alpha.txt", 0, 2 * SYN_DESKICON_H);
    {
        char *st = slurp(statefile);
        assert(st);
        /* 5 columns and 8 rows into the *big* grid — the coords the drop made. */
        char want[64];
        snprintf(want, sizeof(want), "pos=%d,%d,zulu.bin\n",
                 big.x + SYN_DESKICON_PAD + 5 * SYN_DESKICON_W,
                 big.y + SYN_DESKICON_PAD + 8 * SYN_DESKICON_H);
        if (!strstr(st, want)) {
            fprintf(stderr, "FAIL: state has no '%s':\n%s", want, st);
            abort();
        }
    }
    test_area = big;
    deskicons_reload(s);
    assert_cell(s, "zulu.bin", 5, 8);   /* the file still puts it back */
    printf("ok 21 — the state file keeps the pin, not the clamped cell\n");

    /* Leave the scratch tree behind only on success. */
    for (unsigned i = 0; i < sizeof(extras) / sizeof(extras[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", desk, extras[i]);
        unlink(path);
    }
    rmdir(folder);
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
