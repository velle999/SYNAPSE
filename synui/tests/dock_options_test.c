/*
 * dock_options_test.c — the dock's four new switches, at the layout level.
 *
 * `dock_magnify`, `dock_clock`, `dock_on_top` and the right-click menu that
 * carries all three plus auto-hide. What makes them worth a test of their own is
 * that three of them change GEOMETRY, and the dock's geometry is now two rects
 * rather than one:
 *
 *   the CANVAS  the buffer and the scene tree — what slides, what gets cropped
 *   the BODY    the painted slab, inset into the canvas by the magnification
 *               headroom on the side facing away from the screen edge
 *
 * Confusing them is silent. A body measured on the canvas floats the dock a
 * headroom clear of the screen edge and nothing errors; a canvas measured on the
 * body clips the swollen icons off at the slab and nothing errors either. So the
 * assertions here are on the numbers the compositor actually hands the scene:
 * the buffer it asks for, and where it puts the tree.
 *
 * What is asserted, in order of how easy each is to break:
 *
 *   1. The BODY stays welded to the screen edge whether or not there is
 *      headroom above it. This is the one that is invisible in code review and
 *      obvious on screen.
 *
 *   2. Magnification grows the RUN, not just the icons. Scaling icons in place
 *      overlaps them; the row has to slide apart, and the bar with it.
 *
 *   3. dock_entry_at() finds a swollen icon where it is DRAWN. The cells are per
 *      output now — only the screen the pointer is on magnifies — so an entry's
 *      hit box cannot live on the entry any more, and a hit test that still read
 *      it would be right on one monitor and wrong on the next.
 *
 *   4. `dock_on_top` decides raise-vs-tuck, and auto-hide overrides it. A
 *      summoned dock that arrived behind the window it was summoned over would
 *      reveal nothing.
 *
 *   5. The clock takes a cell of its own, and only when it is on.
 *
 *   6. The right-click menu offers the dock's switches whether or not the click
 *      landed on an icon — the reach problem it exists to solve — and hides the
 *      on-top row in the state where it would be ignored.
 *
 * Run as:
 *     ninja -C build && ./build/dock_options_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>

#include "synui.h"

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static void check_int(int got, int want, const char *what)
{
    int ok = got == want;
    printf("  %s  %s", ok ? "ok  " : "FAIL", what);
    if (!ok) printf("  (got %d, want %d)", got, want);
    printf("\n");
    if (!ok) failures++;
}

/* dock.c's, and private to it — spelt out so a change to either has to be made
 * in both places deliberately. */
#define ICON     48
#define PAD      8
#define HEADROOM 32
#define CLOCK_H  92
#define OUT_W    1920
#define OUT_H    1080

/* ── The compositor, stubbed ─────────────────────────────── */

void output_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box)
{
    (void)s; (void)o;
    box->x = 0; box->y = 0; box->width = OUT_W; box->height = OUT_H;
}

/*
 * A REAL cairo context, unlike dock_reorder_test's, and that is the whole reason
 * this rig is a second file rather than more cases in that one.
 *
 * That test stubs create_cairo_buf() to NULL, which makes dock_render_output()
 * bail before it touches a scene node — fine there, fatal here: the numbers
 * under test are the buffer size it asks for and the position it then gives the
 * tree, and both are on the far side of that early return. Rendering into a
 * throwaway image surface is cheaper than teaching every cairo call in dock.c to
 * be stubbable, and it also means the drawing code is actually EXECUTED, so a
 * clock cell that indexes off the end of its box fails here rather than on
 * somebody's screen.
 */
static int buf_w, buf_h, buf_count;
static cairo_surface_t *render_surface;
static struct wlr_buffer fake_buffer;

struct wlr_buffer *create_cairo_buf(int w, int h, cairo_t **cr)
{
    buf_w = w; buf_h = h; buf_count++;
    if (render_surface) cairo_surface_destroy(render_surface);
    render_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    *cr = cairo_create(render_surface);
    return &fake_buffer;
}

void cairo_begin(cairo_t *cr) { (void)cr; }

static struct wlr_scene_buffer fake_scene_buffer;
void set_scene_buffer(struct wlr_scene_buffer **node,
                      struct wlr_scene_tree *parent, struct wlr_buffer *buf)
{ (void)parent; (void)buf; *node = &fake_scene_buffer; }

void cairo_rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                        double r)
{ (void)r; cairo_rectangle(cr, x, y, w, h); }
/* A filled square in the cell, not a no-op. No icon theme is loaded here, so
 * every entry falls through to the monogram — and a stub that drew nothing would
 * leave dock_render_output() exercising its layout and none of its painting,
 * which is half the code the cell sizes feed. */
void icon_draw_monogram(cairo_t *cr, const char *name, double x, double y,
                        double size)
{
    (void)name;
    cairo_set_source_rgba(cr, 0.6, 0.7, 0.9, 1.0);
    cairo_rectangle(cr, x, y, size, size);
    cairo_fill(cr);
}
void syn_buffer_backdrop_blur(struct wlr_scene_buffer *b, bool want, int radius)
{ (void)b; (void)want; (void)radius; }

void syn_show_text(cairo_t *cr, const char *text) { (void)cr; (void)text; }
void syn_text_extents(cairo_t *cr, const char *text, cairo_text_extents_t *ext)
{ (void)cr; (void)text; memset(ext, 0, sizeof(*ext)); }
const char *syn_text_ui_font(void) { return "monospace"; }

static syn_icon_entry_t the_icon;
const syn_icon_entry_t *icon_lookup(const char *app_id)
{ (void)app_id; return &the_icon; }
unsigned icon_generation(void) { return 1; }

const char *view_app_id(syn_view_t *v) { (void)v; return NULL; }
struct wlr_surface *view_surface(syn_view_t *v) { (void)v; return NULL; }
void view_close(syn_view_t *v) { (void)v; }
void view_apply_minimized(syn_server_t *s, syn_view_t *v, int on)
{ (void)s; (void)v; (void)on; }
void focus_view(syn_server_t *s, syn_view_t *v, struct wlr_surface *surf)
{ (void)s; (void)v; (void)surf; }
int workspace_visible(syn_workspace_t *ws) { (void)ws; return 1; }
void workspace_switch(syn_server_t *s, int idx) { (void)s; (void)idx; }
static syn_server_t server;
syn_workspace_t *server_active_workspace(syn_server_t *s)
{ return &s->workspaces[0]; }
void xwayland_unwedge(syn_server_t *s, const char *app_id, const char *title)
{ (void)s; (void)app_id; (void)title; }
void synui_render_dockmenu(syn_server_t *s) { (void)s; }
void synui_spawn(const char *cmd) { (void)cmd; }
bool synui_binding_execute(syn_server_t *s, const char *action, const char *arg)
{ (void)s; (void)action; (void)arg; return true; }

/* No window is ever in front of the dock in this rig. `dock_on_top` is asserted
 * on the RAISE calls below, which is where the decision actually lives —
 * mocking a window on top of it here would test this stub instead. */
struct wlr_surface *surface_at(syn_server_t *s, double lx, double ly,
                               syn_view_t **view_out, double *sx, double *sy)
{
    (void)s; (void)lx; (void)ly; (void)sx; (void)sy;
    if (view_out) *view_out = NULL;
    return NULL;
}

/* dock_state_save() refused rather than pointed at a scratch dir, for the reason
 * dock_reorder_test gives: a config path that resolves is one that can be the
 * LIVE one if $HOME is not what the runner assumed. */
bool syn_config_path(char *buf, size_t n, const char *leaf)
{ (void)buf; (void)n; (void)leaf; return false; }
void syn_config_ensure_dir(void) {}

/* ── The scene, stubbed ──────────────────────────────────── */

static struct wlr_scene_tree fake_tree;
static int raises, tucks;

void wlr_scene_node_set_position(struct wlr_scene_node *node, int x, int y)
{ node->x = x; node->y = y; }
void wlr_scene_node_set_enabled(struct wlr_scene_node *node, bool enabled)
{ (void)node; (void)enabled; }
void wlr_scene_node_raise_to_top(struct wlr_scene_node *node)
{ (void)node; raises++; }
void wlr_scene_node_place_below(struct wlr_scene_node *node,
                                struct wlr_scene_node *sibling)
{ (void)node; (void)sibling; tucks++; }
void wlr_scene_node_destroy(struct wlr_scene_node *node) { (void)node; }
void wlr_scene_buffer_set_source_box(struct wlr_scene_buffer *b,
                                     const struct wlr_fbox *box)
{ (void)b; (void)box; }
void wlr_scene_buffer_set_dest_size(struct wlr_scene_buffer *b, int w, int h)
{ (void)b; (void)w; (void)h; }
struct wlr_scene_tree *wlr_scene_tree_create(struct wlr_scene_tree *parent)
{ (void)parent; return &fake_tree; }
void wlr_output_schedule_frame(struct wlr_output *output) { (void)output; }

/* dockmenu_open() clamps its rect inside the output under the cursor, and reaches
 * that output through the layout. Answered rather than stubbed to NULL so the
 * clamp is actually EXERCISED — a menu taller than the screen edge it pops off
 * is exactly the shape the new settings rows made possible. */
static struct wlr_output fake_wlr_output;
struct wlr_output *wlr_output_layout_output_at(struct wlr_output_layout *layout,
                                               double lx, double ly)
{ (void)layout; (void)lx; (void)ly; return &fake_wlr_output; }

/* ── The rig ─────────────────────────────────────────────── */

static syn_output_t output;

static void relayout(void)
{
    raises = tucks = 0;
    dock_relayout(&server);
}

static void set_pins(const char *const *pins, int n)
{
    server.config.dock_pin_count = n;
    for (int i = 0; i < n; i++)
        snprintf(server.config.dock_pin[i], 128, "%s", pins[i]);
    dock_rebuild(&server);
    relayout();
}

/* The flat run for `n` icons, plus the clock's cell when it is on. */
static int flat_run(int n, bool clock)
{
    return (n > 0 ? n * ICON + (n + 1) * PAD : PAD * 2) +
           (clock ? CLOCK_H + PAD : 0);
}

static bool menu_has(syn_dockact_t a)
{
    for (int i = 0; i < server.dockmenu.action_count; i++)
        if (server.dockmenu.actions[i] == a) return true;
    return false;
}

int main(void)
{
    printf("dock: magnify, clock, on-top and the menu that carries them\n");

    snprintf(the_icon.exec, sizeof(the_icon.exec), "%s", "true");
    the_icon.icon_surface = NULL;

    memset(&server, 0, sizeof(server));
    memset(&output, 0, sizeof(output));
    wl_list_init(&server.outputs);
    for (int i = 0; i < WORKSPACE_MAX; i++)
        wl_list_init(&server.workspaces[i].windows);

    /* A REAL theme, not the zeroed one memset leaves. dock_paint_body() reads
     * panel_bg/panel_accent/panel_ink and dock_opacity, and at 0 alpha on 0
     * colours it draws nothing at all — so an all-zero config would run the
     * layout under test and skip every line that consumes it. */
    server.config.panel_bg[0] = 0.06f; server.config.panel_bg[1] = 0.06f;
    server.config.panel_bg[2] = 0.12f; server.config.panel_bg[3] = 1.0f;
    server.config.panel_accent[0] = 0.00f; server.config.panel_accent[1] = 0.85f;
    server.config.panel_accent[2] = 0.75f; server.config.panel_accent[3] = 1.0f;
    server.config.panel_ink[0] = 0.95f; server.config.panel_ink[1] = 0.95f;
    server.config.panel_ink[2] = 1.00f; server.config.panel_ink[3] = 1.0f;
    server.config.dock_opacity  = 0.72f;
    server.config.dock_radius   = 26;

    server.config.dock_enabled  = 1;
    server.config.dock_autohide = 0;   /* pinned, so the on-top rule is live */
    server.config.dock_magnify  = 1;
    server.config.dock_clock    = 0;
    server.config.dock_on_top   = 0;
    server.config.dock_height   = 64;
    server.config.dock_edge     = SYN_DOCK_EDGE_BOTTOM;
    server.dock_drag.icon = -1;

    fake_wlr_output.data = &output;
    output.wlr_output = &fake_wlr_output;
    output.server = &server;
    output.dock.tree = &fake_tree;
    output.dock.shown = 1;
    output.dock.slide_progress = 1.0;
    output.dock.clock_drawn = -1;
    wl_list_insert(&server.outputs, &output.link);

    const char *const four[] = { "alpha", "bravo", "charlie", "delta" };
    set_pins(four, 4);

    /* ── 1. Headroom is above the body, not under it ───────────────────── */
    check_int(buf_h, 64 + HEADROOM, "the canvas is the slab plus headroom");
    check_int(fake_tree.node.y, OUT_H - (64 + HEADROOM),
              "…and the canvas starts that much higher");
    check_int(fake_tree.node.y + buf_h, OUT_H,
              "…so the BODY still ends on the screen edge");
    check_int(buf_w, flat_run(4, false), "the flat run holds four icons");

    server.config.dock_magnify = 0;
    relayout();
    check_int(buf_h, 64, "magnify off leaves no headroom at all");
    check_int(fake_tree.node.y, OUT_H - 64, "…and the canvas IS the slab");
    server.config.dock_magnify = 1;
    relayout();

    /* ── 2. Magnification grows the run ────────────────────────────────── */
    /* The pointer parked on the middle of slot 1, in the FLAT canvas-local
     * coordinates dock_metrics() samples the falloff from. */
    output.dock.mag_run = PAD + 1 * (ICON + PAD) + ICON / 2.0;
    output.dock.mag_amount = 1.0;
    relayout();
    int magnified = buf_w;
    check(magnified > flat_run(4, false),
          "a magnified row is wider than the flat one");
    check_int(buf_h, 64 + HEADROOM, "…and no taller: the growth is on the run");

    /* ── 3. The hit test finds a swollen icon where it is drawn ────────── */
    /* Slot 1 is directly under the pointer, so it is at full scale — and its
     * drawn centre has moved, because slot 0 in front of it grew too. Asking at
     * the FLAT centre is the check that matters: that is where the old,
     * entry-resident hit box still was. */
    double bar_x = fake_tree.node.x;
    double bar_y = fake_tree.node.y;
    double probe_y = bar_y + buf_h - 24;   /* inside the slab, near its top */

    /* Walk the run and find where slot 1's cell actually is. */
    int first = -1, last = -1;
    for (int x = 0; x < buf_w; x++) {
        syn_dock_entry_t *e = dock_entry_at(&server, bar_x + x, probe_y);
        if (e == &server.dock_entries[1]) {
            if (first < 0) first = x;
            last = x;
        }
    }
    check(first >= 0, "the icon under the pointer is hit-testable");
    check(last - first + 1 > ICON,
          "…and its cell is WIDER than a flat one");

    syn_dock_entry_t *hit = dock_entry_at(&server, bar_x + first + 2, probe_y);
    check(hit == &server.dock_entries[1], "…and it is the right entry");

    output.dock.mag_amount = 0.0;
    relayout();
    check_int(buf_w, flat_run(4, false), "letting go returns the flat run");

    /* ── 4. on-top vs tucked ───────────────────────────────────────────── */
    server.config.dock_on_top = 0;
    relayout();
    check(tucks == 1 && raises == 0, "a pinned dock is tucked under windows");

    server.config.dock_on_top = 1;
    relayout();
    check(raises == 1 && tucks == 0, "…and raised when asked to be");

    server.config.dock_on_top = 0;
    server.config.dock_autohide = 1;
    relayout();
    check(raises == 1 && tucks == 0,
          "an auto-hiding dock is on top regardless");
    server.config.dock_autohide = 0;
    relayout();

    /* ── 5. The clock's cell ───────────────────────────────────────────── */
    server.config.dock_clock = 1;
    relayout();
    check_int(buf_w, flat_run(4, true), "the clock takes a cell past the icons");
    server.config.dock_clock = 0;
    relayout();
    check_int(buf_w, flat_run(4, false), "…and gives it back when switched off");

    /* ── 6. The right-click menu ───────────────────────────────────────── */
    /* On the bar BODY: no app rows, every switch. */
    dockmenu_open(&server, NULL, 400, OUT_H - 20);
    check(!menu_has(SYN_DOCKACT_PIN) && !menu_has(SYN_DOCKACT_UNPIN),
          "a menu opened on the bar body offers no app rows");
    check(menu_has(SYN_DOCKACT_AUTOHIDE) && menu_has(SYN_DOCKACT_MAGNIFY) &&
          menu_has(SYN_DOCKACT_CLOCK) && menu_has(SYN_DOCKACT_SETTINGS),
          "…and does offer the dock's own switches");
    check(menu_has(SYN_DOCKACT_ONTOP),
          "…including on-top, with the dock pinned");
    dockmenu_close(&server);

    /* On an ICON: the app rows come back, and the switches stay. */
    dockmenu_open(&server, &server.dock_entries[0], 400, OUT_H - 20);
    check(menu_has(SYN_DOCKACT_UNPIN),
          "a menu opened on a pinned icon offers to unpin it");
    check(menu_has(SYN_DOCKACT_AUTOHIDE) && menu_has(SYN_DOCKACT_SEP),
          "…with the dock's switches under a rule");
    dockmenu_close(&server);

    /* Auto-hiding, the on-top row would be a switch that is ignored. */
    server.config.dock_autohide = 1;
    dockmenu_open(&server, NULL, 400, OUT_H - 20);
    check(!menu_has(SYN_DOCKACT_ONTOP),
          "an auto-hiding dock does not offer the on-top row");
    dockmenu_close(&server);
    server.config.dock_autohide = 0;

    printf("dock_options_test: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
