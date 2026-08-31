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
 *   5. The clock takes a cell of its own, and only when it is on — and that
 *      cell can be DRAGGED anywhere in the row, which is the thing a test has to
 *      hold: everything past the clock's slot is one cell of a different width
 *      further along than icon arithmetic alone would put it.
 *
 *   6. The right-click menu offers the dock's switches whether or not the click
 *      landed on an icon — the reach problem it exists to solve — and hides the
 *      on-top row in the state where it would be ignored.
 *
 *   7. `dock_height` resizes the ICONS, not only the slab. It moved the slab
 *      alone for its whole life, which left a 200px dock as a wall of glass with
 *      48px pictures adrift in it, and the row reading as broken past ~80px.
 *
 *   8. `dock_magnify_scale` drives the swell AND the headroom the swell needs.
 *      A bigger scale against a fixed 32px of room is not a smaller effect: it
 *      is an icon clipped off at the far edge of the canvas, silently.
 *
 *   9. The show-all-apps button takes a cell at the end of the run and is
 *      hit-testable there. It is drawn ON the body, so dock_bar_at() answers
 *      true for the very same point — which is exactly why input.c has to ask
 *      dock_apps_at() first, and why that ordering is asserted here.
 *
 * Run as:
 *     ninja -C build && ./build/dock_options_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <math.h>
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
 * in both places deliberately.
 *
 * They are FUNCTIONS of the dock's size now rather than constants, so they are
 * derived here the same way. The bare defines are what the stock 64px dock comes
 * out at, which is what most of the cases below run at. */
#define STOCK    64
#define ICON     48
#define PAD      8
#define HEADROOM 32
#define CLOCK_H  92
#define MAG      1.60f
#define OUT_W    1920
#define OUT_H    1080

static int icon_for(int thick)
{
    int icon = thick - 16;
    return icon < 16 ? 16 : (icon > 192 ? 192 : icon);
}

static int pad_for(int thick)
{
    int pad = icon_for(thick) / 6;
    return pad < 4 ? 4 : pad;
}

/* Rounded up to a multiple of 8, the way the 32 it replaces was. */
static int head_for(int thick, double scale)
{
    double swell = icon_for(thick) * (scale - 1.0);
    if (swell < 0.0) swell = 0.0;
    return (int)(ceil(swell / 8.0) * 8.0);
}

/* The clock's cell is MEASURED from the strings it draws now, with a floor —
 * and syn_text_extents() is stubbed to zero here, so the floor is always what
 * wins in this rig. That is deliberate: the measurement belongs to the font
 * stack, the floor belongs to the layout, and only the second is ours to
 * assert. */
static int clock_for(int thick)
{
    return (int)lround(CLOCK_H * (thick / 64.0));
}

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
/* dock_ink_for_cell() asks what is behind each MARK so the clock, apps grid,
 * power mark and running dots can be inked for the surface they land on. UNMEASURED
 * here, which is the answer that hands the theme's own ink straight back — so
 * every geometry assertion in this file is made against exactly the colours it
 * was written for, and none of them depends on a wallpaper this harness has
 * not got. syn_mark_ink()'s own behaviour is dock_ink_test's. */
void wallpaper_backdrop_for_box(syn_server_t *s, const struct wlr_box *box,
                                double target, syn_backdrop_t *out)
{
    (void)s; (void)box; (void)target;
    out->lum = -1.0; out->lum_min = -1.0; out->lum_max = -1.0;
    out->ink = SYN_INK_NONE; out->best = SYN_INK_NONE;
}

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
/* Per-monitor desktops. These say "everything is on screen, on the one
 * desktop", which is the state every assertion in this file is written
 * against — the dock's own behaviour is what is under test, not which
 * monitor is showing what. */
bool view_workspace_shown(syn_view_t *v)                 { (void)v; return true; }
syn_workspace_t *output_active_workspace(syn_server_t *s, syn_output_t *o)
{ (void)o; return server_active_workspace(s); }
void workspace_switch_on(syn_server_t *s, syn_output_t *o, int i)
{ (void)s; (void)o; (void)i; }
void workspace_switch(syn_server_t *s, int idx) { (void)s; (void)idx; }
static syn_server_t server;
syn_workspace_t *server_active_workspace(syn_server_t *s)
{ return &s->workspaces[0]; }
void xwayland_unwedge(syn_server_t *s, const char *app_id, const char *title)
{ (void)s; (void)app_id; (void)title; }
void synui_render_dockmenu(syn_server_t *s) { (void)s; }
/* The application overlay. Counted rather than ignored: the apps button acts on
 * RELEASE now, so "did the click fire" is a thing a dock test can ask. */
int appgrid_toggle_calls = 0;
void appgrid_toggle(syn_server_t *s) { (void)s; appgrid_toggle_calls++; }
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
/* ⚠ WHICH node, not just how many. The dock and its own menu are siblings in
 * the scene, so "the menu is above the dock" is a statement about the ORDER of
 * two raises and is invisible to a counter. See the auto-hide case in main(). */
static struct wlr_scene_node *last_raised;
void wlr_scene_node_raise_to_top(struct wlr_scene_node *node)
{ raises++; last_raised = node; }
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

/* The flat run for `n` icons at thickness `thick`, plus the clock's cell and the
 * apps button when those are on. Each extra cell brings its own trailing pad,
 * exactly as the layout walk does. */
static int run_for_full(int n, int thick, bool clock, bool apps, bool power)
{
    int icon = icon_for(thick), pad = pad_for(thick);
    if (n == 0 && !clock && !apps && !power) return pad * 2;
    int run = pad + n * (icon + pad);
    if (clock) run += clock_for(thick) + pad;
    if (apps)  run += icon + pad;
    if (power) run += icon + pad;
    return run;
}

static int run_for(int n, int thick, bool clock, bool apps)
{
    return run_for_full(n, thick, clock, apps, false);
}

/* The stock dock, which is what most of the cases below run at. */
static int flat_run(int n, bool clock) { return run_for(n, STOCK, clock, false); }

/* The canvas-local run-axis span a cell hit test answers true over, or -1/-1
 * when it is nowhere on the bar. The probe is the real entry point input.c
 * calls, so what is asserted is what a click would find. */
static void span_of(bool (*probe)(syn_server_t *, double, double),
                    double bar_x, double probe_y, int width,
                    int *first, int *last)
{
    *first = *last = -1;
    for (int x = 0; x < width; x++)
        if (probe(&server, bar_x + x, probe_y)) {
            if (*first < 0) *first = x;
            *last = x;
        }
}

static bool menu_has(syn_dockact_t a)
{
    for (int i = 0; i < server.dockmenu.action_count; i++)
        if (server.dockmenu.actions[i] == a) return true;
    return false;
}

/* Where a row sits, or -1. The menu's ORDER is a design decision that a
 * presence check cannot see, and reordering it is a one-line edit — so the
 * order is asserted rather than described in a comment somebody has to find. */
static int menu_at(syn_dockact_t a)
{
    for (int i = 0; i < server.dockmenu.action_count; i++)
        if (server.dockmenu.actions[i] == a) return i;
    return -1;
}

/* ⚠ THE LAST one, and there are two: the icon menu carries a rule above the
 * dock's own placement rows AND a rule above the app block. menu_at() answers
 * with the first, which is not the one the seam assertions are about. */
static int menu_last_at(syn_dockact_t a)
{
    int at = -1;
    for (int i = 0; i < server.dockmenu.action_count; i++)
        if (server.dockmenu.actions[i] == a) at = i;
    return at;
}
static int menu_count(syn_dockact_t a)
{
    int n = 0;
    for (int i = 0; i < server.dockmenu.action_count; i++)
        if (server.dockmenu.actions[i] == a) n++;
    return n;
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
    /* ⚠ NOT the zero memset leaves. A scale of 0 makes every swell negative,
     * the headroom collapses to nothing, and the whole magnification half of
     * this file passes vacuously — which is how this rig's own copy of the
     * defaults bit the first time. */
    server.config.dock_magnify_scale = MAG;
    server.config.dock_clock    = 0;
    server.config.dock_clock_slot = -1;   /* past the last icon */
    /* Off in the rig, on by default on a real desktop: the cases that care
     * switch it on, and the run arithmetic everywhere else stays about icons. */
    server.config.dock_apps_button = 0;
    server.config.dock_on_top   = 0;
    server.config.dock_height   = STOCK;
    server.config.dock_edge     = SYN_DOCK_EDGE_BOTTOM;
    server.dock_drag.icon = DOCK_DRAG_BAR;

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

    /* ── 5. The clock's cell, and dragging it along the run ────────────── */
    server.config.dock_clock = 1;
    relayout();
    check_int(buf_w, flat_run(4, true), "the clock takes a cell past the icons");

    /* Where it actually is, asked the way a click asks. With slot -1 it is past
     * the last icon, so its span starts after every entry's. */
    bar_x = fake_tree.node.x;
    probe_y = fake_tree.node.y + buf_h - 24;
    int clk_first, clk_last;
    span_of(dock_clock_at, bar_x, probe_y, buf_w, &clk_first, &clk_last);
    check(clk_first >= 0, "…and the clock cell is hit-testable");
    check_int(clk_last - clk_first + 1, CLOCK_H, "…at the width it asked for");

    int last_icon_end = -1;
    for (int x = 0; x < buf_w; x++)
        if (dock_entry_at(&server, bar_x + x, probe_y)) last_icon_end = x;
    check(clk_first > last_icon_end, "…past the last icon, which is slot -1");

    /* Now drag it to the front. The whole gesture, through the same three entry
     * points input.c uses — a slot written by anything else is a slot the real
     * press cannot produce. */
    double grab_x = bar_x + clk_first + 4;
    dock_clock_drag_begin(&server, grab_x, probe_y);
    check(server.dock_drag.active && server.dock_drag.icon == DOCK_DRAG_CLOCK,
          "a press on the clock arms the clock drag, not a bar drag");
    dock_drag_motion(&server, bar_x + 2, probe_y);
    dock_drag_end(&server, bar_x + 2, probe_y);
    check_int(server.config.dock_clock_slot, 0, "…and dropping it at the front");

    relayout();
    span_of(dock_clock_at, bar_x, probe_y, buf_w, &clk_first, &clk_last);
    int first_icon_start = -1;
    for (int x = 0; x < buf_w; x++)
        if (dock_entry_at(&server, bar_x + x, probe_y)) { first_icon_start = x; break; }
    check(clk_first >= 0 && clk_last < first_icon_start,
          "…puts the cell before every icon");
    check_int(buf_w, flat_run(4, true), "…and costs the run exactly nothing");

    /* Dropped past the last icon it goes back to -1 rather than being written as
     * `n`: they look the same today and stop being the same the moment an app
     * opens. */
    dock_clock_drag_begin(&server, bar_x + clk_first + 4, probe_y);
    dock_drag_motion(&server, bar_x + buf_w - 2, probe_y);
    dock_drag_end(&server, bar_x + buf_w - 2, probe_y);
    check_int(server.config.dock_clock_slot, -1,
              "…and dragging it back to the end stores -1, not 4");

    server.config.dock_clock = 0;
    relayout();
    check_int(buf_w, flat_run(4, false), "…and gives it back when switched off");

    /* ── 6. The right-click menu ───────────────────────────────────────── */
    /* On the bar BODY: no app rows, every switch. */
    dockmenu_open(&server, NULL, 400, OUT_H - 20);
    check(!menu_has(SYN_DOCKACT_PIN) && !menu_has(SYN_DOCKACT_UNPIN),
          "a menu opened on the bar body offers no app rows");
    check(menu_has(SYN_DOCKACT_AUTOHIDE) && menu_has(SYN_DOCKACT_MAGNIFY) &&
          menu_has(SYN_DOCKACT_CLOCK) && menu_has(SYN_DOCKACT_APPS) &&
          menu_has(SYN_DOCKACT_POWER) &&
          menu_has(SYN_DOCKACT_SETTINGS),
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

    /*
     * ── The order, which is the whole of what an icon menu IS ──────────────
     *
     * The app's rows used to be FIRST. Two things were wrong with that: the
     * block is one to four rows depending on the icon (pinned? has an Exec?
     * running?), so every setting below it moved by up to four positions from
     * one icon to the next — "Auto-hide Dock" was never twice in the same
     * place; and the menu opens ABOVE the cursor on a bottom dock, so the rows
     * nearest the pointer are the LAST ones, and the pointer is sitting on the
     * icon those rows are about.
     *
     * Asserted rather than described, because "move these four lines" is a
     * one-minute edit and the reason they are where they are is not in the
     * diff.
     */
    check(menu_at(SYN_DOCKACT_AUTOHIDE) == 0,
          "the dock's settings start at the top, on every icon");
    check(menu_at(SYN_DOCKACT_UNPIN) > menu_at(SYN_DOCKACT_SETTINGS),
          "the app's own rows come after them");
    check(menu_last_at(SYN_DOCKACT_SEP) == menu_at(SYN_DOCKACT_UNPIN) - 1,
          "…with a rule immediately above the app block, marking the seam");
    dockmenu_close(&server);

    /* The two rows that only exist while the app IS running, which is what puts
     * Quit All Windows on the end of the menu. The entry is not running in the
     * rest of this file, so it is turned on for these four checks and turned
     * back off — a running flag left set would change what every later menu
     * offers. */
    server.dock_entries[0].running = 1;
    dockmenu_open(&server, &server.dock_entries[0], 400, OUT_H - 20);
    check(menu_at(SYN_DOCKACT_QUIT) == server.dockmenu.action_count - 1,
          "Quit All Windows is the LAST row in the menu");
    check(menu_at(SYN_DOCKACT_CLOSEWIN) == menu_at(SYN_DOCKACT_QUIT) - 1,
          "…with close-one directly above it, the commoner intent first");
    check(menu_at(SYN_DOCKACT_QUIT) > menu_at(SYN_DOCKACT_SETTINGS),
          "…and both of them below the dock's own settings");
    dockmenu_close(&server);
    server.dock_entries[0].running = 0;

    /* On the BAR BODY there is no app block — so there is ONE rule (the one
     * above the dock's placement rows) rather than two, and nothing after
     * Dock Settings for a second one to sit above. A menu ending in a hanging
     * separator would be a line drawn under nothing. */
    dockmenu_open(&server, NULL, 400, OUT_H - 20);
    check(menu_at(SYN_DOCKACT_AUTOHIDE) == 0,
          "the bar body's menu starts on the same row an icon's does");
    check(menu_count(SYN_DOCKACT_SEP) == 1,
          "…and carries one rule, not the app block's as well");
    check(menu_at(SYN_DOCKACT_SETTINGS) == server.dockmenu.action_count - 1,
          "…ending on Dock Settings");
    dockmenu_close(&server);

    /* Auto-hiding, the on-top row would be a switch that is ignored. */
    server.config.dock_autohide = 1;
    dockmenu_open(&server, NULL, 400, OUT_H - 20);
    check(!menu_has(SYN_DOCKACT_ONTOP),
          "an auto-hiding dock does not offer the on-top row");
    dockmenu_close(&server);

    /*
     * ── …and the menu stays ON TOP of an auto-hiding dock ──────────────────
     *
     * Reported as the right-click menu vanishing behind the dock for a moment
     * and coming back once the dock shrank — on an auto-hiding dock only.
     *
     * The menu raises itself when it draws, which was enough while nothing
     * raised the dock afterwards. An auto-hiding dock is always on top, and the
     * slide is an ANIMATION: dock_apply_position() runs once per frame for its
     * length, and every one of those frames put the dock back over the menu it
     * had just opened. It reappeared when the slide ended and the ticking
     * stopped — exactly "until the dock shrinks", and why a pinned dock never
     * showed it.
     *
     * ⚠ THE MENU NEEDS A TREE OF ITS OWN HERE. wlr_scene_tree_create() is
     * stubbed to hand every caller the same fake_tree, so with the real plumbing
     * the dock's node and the menu's node would be one pointer and the order
     * between them would be unobservable. Assigning a distinct one is what makes
     * "which was raised LAST" a real question.
     */
    static struct wlr_scene_tree menu_tree;
    server.dockmenu_ui.tree = &menu_tree;
    dockmenu_open(&server, &server.dock_entries[0], 400, OUT_H - 20);
    last_raised = NULL;
    relayout();
    check(last_raised == &menu_tree.node,
          "an auto-hiding dock leaves its own menu on top, not under it");

    /* And the dock is still raised at all — the fix is the ORDER of the two,
     * never "stop raising the dock", which would put a sliding dock behind the
     * windows it is sliding over. */
    check(raises >= 2, "…having raised itself first, as it must");

    dockmenu_close(&server);
    last_raised = NULL;
    relayout();
    check(last_raised != &menu_tree.node,
          "…and stops re-raising it once the menu is closed");
    server.dockmenu_ui.tree = NULL;
    server.config.dock_autohide = 0;

    /* ── 7. Dock size resizes the ICONS ────────────────────────────────── */
    server.config.dock_height = 100;
    relayout();
    check_int(buf_h, 100 + head_for(100, MAG),
              "a bigger dock is a taller canvas");
    check_int(buf_w, run_for(4, 100, false, false),
              "…and the icons and gaps in it grew too");

    /* The claim in one number: the drawn cell is the new icon size, not 48. */
    bar_x = fake_tree.node.x;
    probe_y = fake_tree.node.y + buf_h - 24;
    int icon_first = -1, icon_last = -1;
    for (int x = 0; x < buf_w; x++)
        if (dock_entry_at(&server, bar_x + x, probe_y) == &server.dock_entries[0]) {
            if (icon_first < 0) icon_first = x;
            icon_last = x;
        }
    check_int(icon_last - icon_first + 1, icon_for(100),
              "…which is what a click actually lands on");

    server.config.dock_height = 32;
    relayout();
    check_int(buf_w, run_for(4, 32, false, false),
              "and the smallest dock shrinks them the same way");

    server.config.dock_height = STOCK;
    relayout();

    /* ── 8. The magnification amount drives the headroom ───────────────── */
    server.config.dock_magnify_scale = 2.50f;
    relayout();
    check_int(buf_h, STOCK + head_for(STOCK, 2.50f),
              "a bigger swell asks for more headroom");
    check(head_for(STOCK, 2.50f) > HEADROOM,
          "…which is more than the constant it replaced");

    output.dock.mag_run = PAD + 1 * (ICON + PAD) + ICON / 2.0;
    output.dock.mag_amount = 1.0;
    relayout();
    int wide = buf_w;
    server.config.dock_magnify_scale = MAG;
    relayout();
    check(wide > buf_w, "…and swells the row further than 1.60 does");
    output.dock.mag_amount = 0.0;
    relayout();

    /* ── 9. The show-all-apps button ───────────────────────────────────── */
    server.config.dock_apps_button = 1;
    relayout();
    check_int(buf_w, run_for(4, STOCK, false, true),
              "the apps button takes a cell of its own");

    bar_x = fake_tree.node.x;
    probe_y = fake_tree.node.y + buf_h - 24;
    int apps_first, apps_last;
    span_of(dock_apps_at, bar_x, probe_y, buf_w, &apps_first, &apps_last);
    check_int(apps_last - apps_first + 1, ICON, "…the size of an icon");
    check(!dock_entry_at(&server, bar_x + apps_first + 2, probe_y),
          "…and it is not an entry — no app_id, no click on an app");

    /* THE ordering fact: the same point is on the bar body, so a press that
     * reached dock_bar_at() first would drag the dock instead of opening the
     * menu. input.c asks dock_apps_at() before it, and this is why. */
    check(dock_bar_at(&server, bar_x + apps_first + 2, probe_y, NULL),
          "…while the bar answers for that point too, hence the ask order");

    server.config.dock_apps_button = 0;
    relayout();
    check_int(buf_w, flat_run(4, false), "…and gives the cell back when off");

    /* ── 9b. The analog row is a STYLE, and only appears with a clock ──── */
    /* dock_clock is off at this point — see the end of section 5. */
    dockmenu_open(&server, NULL, 400, OUT_H - 20);
    check(!menu_has(SYN_DOCKACT_CLOCK_ANALOG),
          "with no dock clock, the menu offers no face for it");
    dockmenu_close(&server);

    server.config.dock_clock = 1;
    relayout();
    dockmenu_open(&server, NULL, 400, OUT_H - 20);
    check(menu_has(SYN_DOCKACT_CLOCK_ANALOG), "…and offers one when there is");
    dockmenu_close(&server);

    /* The switch, through the menu's own click path — and the CELL changes
     * shape, which is the whole point: a dial is square and a time is wide. */
    int digital_run = 0, analog_run = 0;
    {
        bar_x = fake_tree.node.x;
        probe_y = fake_tree.node.y + buf_h - 24;
        int f, l;
        span_of(dock_clock_at, bar_x, probe_y, buf_w, &f, &l);
        digital_run = l - f + 1;

        server.config.dock_clock_analog = 1;
        relayout();
        bar_x = fake_tree.node.x;
        probe_y = fake_tree.node.y + buf_h - 24;
        span_of(dock_clock_at, bar_x, probe_y, buf_w, &f, &l);
        analog_run = l - f + 1;
    }
    check_int(analog_run, STOCK, "an analog cell is the slab's own thickness");
    check(analog_run != digital_run,
          "…which is not the width the digits asked for");
    server.config.dock_clock_analog = 0;
    server.config.dock_clock = 0;
    relayout();

    /* ── 10. The power button ──────────────────────────────────────────── */
    server.config.dock_power_button = 1;
    relayout();
    check_int(buf_w, run_for_full(4, STOCK, false, false, true),
              "the power button takes a cell of its own");

    bar_x = fake_tree.node.x;
    probe_y = fake_tree.node.y + buf_h - 24;
    int pwr_first, pwr_last;
    span_of(dock_power_at, bar_x, probe_y, buf_w, &pwr_first, &pwr_last);
    check_int(pwr_last - pwr_first + 1, ICON, "…the size of an icon");
    check(!dock_entry_at(&server, bar_x + pwr_first + 2, probe_y),
          "…and it is not an entry either");
    /* Same ordering fact as the apps button: the cell is drawn ON the body. */
    check(dock_bar_at(&server, bar_x + pwr_first + 2, probe_y, NULL),
          "…while the bar answers for that point too, hence the ask order");

    /* ⚠ THE CELLS MUST NOT ANSWER FOR EACH OTHER. dock_cell_hit() took a bool
     * until this button existed, and `true` meaning "clock" is the same 1 that
     * now means "apps" — so a stale caller hit-tested the wrong cell and the
     * compiler said nothing. Every probe, over the whole bar, on a dock that
     * has all three. */
    server.config.dock_apps_button = 1;
    server.config.dock_clock       = 1;
    relayout();
    bar_x = fake_tree.node.x;
    probe_y = fake_tree.node.y + buf_h - 24;
    check_int(buf_w, run_for_full(4, STOCK, true, true, true),
              "all three cells sit in the run at once");
    {
        int overlap = 0, pwr_seen = 0, apps_seen = 0, clk_seen = 0;
        for (int x = 0; x < buf_w; x++) {
            double px = bar_x + x;
            int hits = (dock_power_at(&server, px, probe_y) ? 1 : 0)
                     + (dock_apps_at (&server, px, probe_y) ? 1 : 0)
                     + (dock_clock_at(&server, px, probe_y) ? 1 : 0);
            if (hits > 1) overlap++;
            if (dock_power_at(&server, px, probe_y)) pwr_seen  = 1;
            if (dock_apps_at (&server, px, probe_y)) apps_seen = 1;
            if (dock_clock_at(&server, px, probe_y)) clk_seen  = 1;
        }
        check(pwr_seen && apps_seen && clk_seen,
              "…and each one is reachable");
        check_int(overlap, 0, "…and no point answers for two of them");
    }

    /* The power button is LAST in the run, past the apps button — the rule that
     * keeps Shut Down furthest from the icons a hand is already aiming at. */
    span_of(dock_power_at, bar_x, probe_y, buf_w, &pwr_first, &pwr_last);
    span_of(dock_apps_at,  bar_x, probe_y, buf_w, &apps_first, &apps_last);
    check(pwr_first > apps_last, "…and the power cell comes after the apps one");

    /* A LEFT press opens the power menu, and the power menu is not the settings
     * menu: same popup, different rows. */
    dockmenu_open_power(&server, bar_x + pwr_first + 2, probe_y);
    check(server.dockmenu.visible, "a press on it opens a menu");
    check(menu_has(SYN_DOCKACT_LOCK) && menu_has(SYN_DOCKACT_LOGOUT) &&
          menu_has(SYN_DOCKACT_SUSPEND) && menu_has(SYN_DOCKACT_REBOOT) &&
          menu_has(SYN_DOCKACT_POWEROFF),
          "…carrying all five power rows");
    check(!menu_has(SYN_DOCKACT_AUTOHIDE) && !menu_has(SYN_DOCKACT_PIN) &&
          !menu_has(SYN_DOCKACT_SETTINGS),
          "…and none of the dock's own rows");
    /* Shut Down last, nearest the cursor's own edge of the popup and therefore
     * furthest from the button that was just pressed — see dockmenu_open_power. */
    check(server.dockmenu.actions[server.dockmenu.action_count - 1]
              == SYN_DOCKACT_POWEROFF,
          "…with Shut Down the furthest row from the press");
    dockmenu_close(&server);

    server.config.dock_power_button = 0;
    server.config.dock_apps_button  = 0;
    server.config.dock_clock        = 0;
    relayout();
    check_int(buf_w, flat_run(4, false), "…and gives its cell back when off");
    check(!dock_power_at(&server, bar_x + pwr_first + 2, probe_y),
          "…and stops answering for the point it used to hold");

    /*
     * ── The pin list, and what it does when it is full ──────────────────────
     *
     * ⛔ velle, 2026-08-30: "player won't let me pin it to the dock". dock.state
     * held exactly DOCK_PIN_MAX entries, so dock_pin_toggle() refused, logged to
     * wlr_log and returned — leaving a menu row that said "Pin to Dock" and did
     * nothing when clicked. Nothing on screen said why, so it read as a fault in
     * the application being pinned.
     *
     * Two things are checked: that the cap is big enough not to be reached by an
     * ordinary desktop, and that when it IS reached the row SAYS so.
     */
    server.config.dock_pin_count = 0;
    check(DOCK_PIN_MAX >= 24,
          "the pin list holds more than a desktop's worth of apps");
    /* ⚠ Pinned PLUS running share one table. A cap equal to it would mean a
     * full pin list leaves no cell for a window that is actually open. */
    check(DOCK_MAX_ENTRIES > DOCK_PIN_MAX,
          "…and there is room for running windows beyond every pin");

    for (int i = 0; i < DOCK_PIN_MAX; i++)
        snprintf(server.config.dock_pin[i], 128, "app%d", i);
    server.config.dock_pin_count = DOCK_PIN_MAX;

    dock_pin_toggle(&server, "one-too-many");
    check_int(server.config.dock_pin_count, DOCK_PIN_MAX,
              "a pin past the cap is refused rather than overrunning the table");
    check(!dock_pin_room(&server),
          "⛔ …and the menu can tell, so the row says the dock is full "
          "instead of doing nothing");

    /* And with room, the label is the plain one and the pin lands. */
    server.config.dock_pin_count = DOCK_PIN_MAX - 1;
    check(dock_pin_room(&server),
          "…while a dock with room reports room");
    dock_pin_toggle(&server, "syn-play");
    check_int(server.config.dock_pin_count, DOCK_PIN_MAX,
              "…and the pin is taken");

    printf("dock_options_test: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
