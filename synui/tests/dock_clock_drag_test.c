/*
 * dock_clock_drag_test.c — dragging the dock's CLOCK cell into another gap.
 *
 * The clock cell can be picked up and moved along the run (dock_clock_slot,
 * persisted as clock_slot in dock.state). Same rig and the same stubs as
 * tests/dock_reorder_test.c, driving the model exactly the way input.c drives
 * it: dock_clock_at() decides the press belongs to the clock,
 * dock_clock_drag_begin() arms the gesture, dock_drag_motion() carries it and
 * dock_drag_end() commits.
 *
 * The clock cell is LOCATED by sweeping dock_clock_at() along the bar rather
 * than by re-deriving its rect here. Re-deriving it would mean a second copy of
 * dock_metrics()' layout walk, and a test that agrees with a bug in the walk it
 * copied is worth nothing.
 *
 * Run as:
 *     ninja -C build && ./build/dock_clock_drag_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synui.h"

/* velle's DP-3, at its real place in the layout: the dock is centred on the
 * output box, and an output at a non-zero origin is exactly what a hit test
 * that forgets to subtract the tree's position gets wrong. */
#define OUT_X 1080
#define OUT_Y 1080
#define OUT_W 2560
#define OUT_H 1440

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}


/* One 1920x1080 output at the layout origin. dock.c asks for this to centre the
 * bar and to clip the slide, and for nothing else. */
void output_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box)
{
    (void)s; (void)o;
    box->x = OUT_X; box->y = OUT_Y; box->width = OUT_W; box->height = OUT_H;
}

/* Rendering is skipped wholesale: create_cairo_buf() returning NULL makes
 * dock_render_output() bail before it touches a scene node, which is the whole
 * half of the file this test is not about. The hit-boxes it would have written
 * are set by hand below instead — they are pure layout arithmetic and the test
 * needs them to be the ones the renderer produces, not the ones it happens to
 * have left over. */
struct wlr_buffer *create_cairo_buf(int w, int h, cairo_t **cr)
{
    (void)w; (void)h; *cr = NULL; return NULL;
}

void cairo_begin(cairo_t *cr) { (void)cr; }
void set_scene_buffer(struct wlr_scene_buffer **node,
                      struct wlr_scene_tree *parent, struct wlr_buffer *buf)
{ (void)node; (void)parent; (void)buf; }
void cairo_rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                        double r)
{ (void)cr; (void)x; (void)y; (void)w; (void)h; (void)r; }
void icon_draw_monogram(cairo_t *cr, const char *name, double x, double y,
                        double size)
{ (void)cr; (void)name; (void)x; (void)y; (void)size; }
void syn_buffer_backdrop_blur(struct wlr_scene_buffer *b, bool want, int radius)
{ (void)b; (void)want; (void)radius; }

static syn_icon_entry_t the_icon;
const syn_icon_entry_t *icon_lookup(const char *app_id)
{
    (void)app_id;
    return &the_icon;
}

/* The dock keeps its own copy of each icon pre-scaled to the cell and drops it
 * when this moves. Constant here: the stub icon never changes, and a generation
 * that never moves is the honest answer for a table that never retints. */
unsigned icon_generation(void) { return 1; }

/* Views and workspaces: this desktop has none. Every dock entry is either
 * pinned-and-not-running or running-with-no-window, which is all the reorder
 * rule can see anyway — it works off app_ids and the pin list. */
const char *view_app_id(syn_view_t *v) { (void)v; return NULL; }
struct wlr_surface *view_surface(syn_view_t *v) { (void)v; return NULL; }
void view_close(syn_view_t *v) { (void)v; }
void view_apply_minimized(syn_server_t *s, syn_view_t *v, int on)
{ (void)s; (void)v; (void)on; }
void focus_view(syn_server_t *s, syn_view_t *v, struct wlr_surface *surf)
{ (void)s; (void)v; (void)surf; }
int workspace_visible(syn_workspace_t *ws) { (void)ws; return 1; }
void workspace_switch(syn_server_t *s, int idx) { (void)s; (void)idx; }
/* Only reached by dock_output_fullscreen(), which walks the active desk looking
 * for a fullscreen window to tuck the dock under. Workspace 0's window list is
 * empty here, so it answers "no" and the dock stays on top. */
syn_workspace_t *server_active_workspace(syn_server_t *s)
{ return &s->workspaces[0]; }
void xwayland_unwedge(syn_server_t *s, const char *app_id, const char *title)
{ (void)s; (void)app_id; (void)title; }
void synui_render_dockmenu(syn_server_t *s) { (void)s; }
bool synui_binding_execute(syn_server_t *s, const char *action, const char *arg)
{ (void)s; (void)action; (void)arg; return true; }

/* The dock clock's text. Never reached with the clock off (its default), and
 * measured-and-drawn through the fallback stack in render.c when it is on —
 * which is a font system, not something to link a model test against. */
void syn_show_text(cairo_t *cr, const char *text) { (void)cr; (void)text; }
void syn_text_extents(cairo_t *cr, const char *text, cairo_text_extents_t *ext)
{ (void)cr; (void)text; memset(ext, 0, sizeof(*ext)); }
const char *syn_text_ui_font(void) { return "monospace"; }

/*
 * The occlusion test behind dock_point_clear(), which asks "is a window in front
 * of the dock here" — and only when the dock is NOT on top, which in this rig it
 * always is (dock_autohide defaults on). NULL is "nothing over the dock", the
 * answer that lets every hit test through; a stub that claimed otherwise would
 * silently disarm every assertion in this file rather than fail one.
 */
struct wlr_surface *surface_at(syn_server_t *s, double lx, double ly,
                               syn_view_t **view_out, double *sx, double *sy)
{
    (void)s; (void)lx; (void)ly; (void)sx; (void)sy;
    if (view_out) *view_out = NULL;
    return NULL;
}

/* The one thing worth OBSERVING rather than discarding: a click launches, and
 * this counts it. Nothing else in the model distinguishes "the release ran the
 * click" from "the release did nothing at all". */
static int spawns = 0;
static char last_spawn[256];
void synui_spawn(const char *cmd)
{
    spawns++;
    snprintf(last_spawn, sizeof(last_spawn), "%s", cmd ? cmd : "");
}

/* dock_state_save() writes ~/.config/synui/dock.state. Refused outright rather
 * than pointed at a scratch dir: this test asserts the in-memory pin list, and
 * a config path that resolves is a config path that can be the LIVE one if $HOME
 * is ever not what the runner assumed. */
static int saves = 0;
bool syn_config_path(char *buf, size_t n, const char *leaf)
{
    (void)buf; (void)n; (void)leaf;
    saves++;
    return false;      /* dock_state_save() gives up here, having counted */
}
void syn_config_ensure_dir(void) {}

/* ── The scene, stubbed ──────────────────────────────────── */
/* dock.c reads o->dock.tree->node.{x,y} to turn a layout coordinate into a
 * dock-canvas one, and calls a handful of setters on nodes it owns. A real
 * wlr_scene would mean a wl_display and a backend for two integers. */
static struct wlr_scene_tree fake_tree;

void wlr_scene_node_set_position(struct wlr_scene_node *node, int x, int y)
{ node->x = x; node->y = y; }
void wlr_scene_node_set_enabled(struct wlr_scene_node *node, bool enabled)
{ (void)node; (void)enabled; }
void wlr_scene_node_raise_to_top(struct wlr_scene_node *node) { (void)node; }
void wlr_scene_node_place_below(struct wlr_scene_node *node,
                                struct wlr_scene_node *sibling)
{ (void)node; (void)sibling; }
void wlr_scene_node_destroy(struct wlr_scene_node *node) { (void)node; }
void wlr_scene_buffer_set_source_box(struct wlr_scene_buffer *b,
                                     const struct wlr_fbox *box)
{ (void)b; (void)box; }
void wlr_scene_buffer_set_dest_size(struct wlr_scene_buffer *b, int w, int h)
{ (void)b; (void)w; (void)h; }
struct wlr_scene_tree *wlr_scene_tree_create(struct wlr_scene_tree *parent)
{ (void)parent; return &fake_tree; }
void wlr_output_schedule_frame(struct wlr_output *output) { (void)output; }


/* ── The rig ─────────────────────────────────────────────── */

static syn_server_t server;
static syn_output_t output;

/* The bar is always shown here (autohide off, as velle's dock.state has it), so
 * the tree sits where dock_apply_position would have put it. The renderer is
 * stubbed out, so nothing else writes this. */
static void place_tree(void)
{
    fake_tree.node.x = OUT_X;
    fake_tree.node.y = OUT_Y + OUT_H - server.config.dock_height;
}

/* Sweep the run axis for the clock cell. Returns its layout-space x, or -1. */
static double find_clock_x(double *span_out)
{
    double y = OUT_Y + OUT_H - server.config.dock_height / 2.0;
    double first = -1, last = -1;
    for (double x = OUT_X; x < OUT_X + OUT_W; x += 1.0) {
        if (dock_clock_at(&server, x, y)) {
            if (first < 0) first = x;
            last = x;
        }
    }
    if (span_out) *span_out = first < 0 ? 0 : (last - first + 1);
    return first < 0 ? -1 : (first + last) / 2.0;
}

/* Sweep for the run-axis centre of a given dock ENTRY, the same way the clock
 * cell is found: the renderer that would have written the hit-boxes is stubbed
 * out here, so asking the model is the only honest way to locate anything. */
static double find_entry_x(const syn_dock_entry_t *want)
{
    double y = OUT_Y + OUT_H - server.config.dock_height / 2.0;
    double first = -1, last = -1;
    for (double x = OUT_X; x < OUT_X + OUT_W; x += 1.0) {
        if (dock_entry_at(&server, x, y) == want) {
            if (first < 0) first = x;
            last = x;
        }
    }
    return first < 0 ? -1 : (first + last) / 2.0;
}

int main(void)
{
    printf("dock: drag the clock cell to a new slot\n");

    snprintf(the_icon.exec, sizeof(the_icon.exec), "%s", "true");
    the_icon.icon_surface = NULL;

    memset(&server, 0, sizeof(server));
    memset(&output, 0, sizeof(output));
    wl_list_init(&server.outputs);
    for (int i = 0; i < WORKSPACE_MAX; i++)
        wl_list_init(&server.workspaces[i].windows);

    /* velle's ~/.config/synui/dock.state, as it actually sits on disk: the
     * clock ON and parked past the last icon, the apps button ON, auto-hide and
     * on-top OFF, and the full sixteen pins. The pin COUNT matters — it is what
     * decides how many gaps the clock has to walk through. */
    server.config.dock_enabled     = 1;
    server.config.dock_autohide    = 0;
    server.config.dock_height      = 64;
    server.config.dock_edge        = SYN_DOCK_EDGE_BOTTOM;
    server.config.dock_clock       = 1;
    server.config.dock_clock_slot  = -1;
    server.config.dock_apps_button = 1;
    server.dock_drag.icon          = DOCK_DRAG_BAR;

    output.server = &server;
    output.dock.tree = &fake_tree;
    output.dock.shown = 1;
    output.dock.slide_progress = 1.0;
    wl_list_insert(&server.outputs, &output.link);

    static const char *const pins[] = {
        "steam", "firefox", "vivaldi-stable", "mpv", "chibi", "vesktop",
        "synfiles", "synpkg", "syn-arsenal", "synstudio", "syn-arcade",
        "syn-settings", "syn-disks", "syn-update", "syn-edit", "syntty",
    };
    int npins = (int)(sizeof(pins) / sizeof(pins[0]));
    server.config.dock_pin_count = npins;
    for (int i = 0; i < npins; i++)
        snprintf(server.config.dock_pin[i], 128, "%s", pins[i]);
    dock_rebuild(&server);
    place_tree();

    check(server.dock_entry_count == npins, "sixteen pinned entries");

    /* ── 1. The cell can be found at all ───────────────────────────────── */
    double span = 0;
    double cx = find_clock_x(&span);
    printf("      clock cell: centre x=%.1f span=%.0fpx\n", cx, span);
    check(cx >= 0, "dock_clock_at() finds the clock cell on the bar");
    if (cx < 0) { printf("%d failure(s)\n", ++failures); return 1; }

    /* It has to sit past the LAST icon, which is what clock_slot = -1 means. */
    double last_icon = find_entry_x(&server.dock_entries[npins - 1]);
    printf("      last icon centre x=%.1f\n", last_icon);
    check(last_icon >= 0 && cx > last_icon,
          "…parked past the last icon, as clock_slot = -1 asks");

    /* ── 2. A press on it arms a CLOCK drag ────────────────────────────── */
    double y = OUT_Y + OUT_H - server.config.dock_height / 2.0;
    dock_clock_drag_begin(&server, cx, y);
    check(server.dock_drag.active == 1, "the press arms a drag");
    check(server.dock_drag.icon == DOCK_DRAG_CLOCK, "…and it is the CLOCK's drag");
    check(server.dock_drag.slot == npins, "…armed at the slot it is parked in");

    /* The apps button is hit-tested BEFORE the clock in input.c, so a press
     * meant for the clock must not be claimed by it. */
    check(!dock_apps_at(&server, cx, y),
          "the apps button does not claim the clock's cell");
    {
        /* Not just at the centre: input.c asks dock_apps_at() FIRST, so ANY
         * overlap between the two cells means a press aimed at the clock opens
         * the application page instead — and the gesture that was supposed to
         * move the clock silently becomes a different feature entirely. */
        double cf = -1, cl = -1, af = -1, al = -1;
        for (double x = OUT_X; x < OUT_X + OUT_W; x += 1.0) {
            if (dock_clock_at(&server, x, y)) { if (cf < 0) cf = x; cl = x; }
            if (dock_apps_at(&server, x, y))  { if (af < 0) af = x; al = x; }
        }
        printf("      clock cell x=[%.0f,%.0f]  apps button x=[%.0f,%.0f]\n",
               cf, cl, af, al);
        check(af >= 0, "the apps button is hit-testable at all");
        check(cf >= 0 && af >= 0 && (al < cf || af > cl),
              "the two cells do not overlap anywhere");
    }

    /* ── 3. Carrying it up the row moves the slot ──────────────────────── */
    double target = find_entry_x(&server.dock_entries[3]);   /* into gap 3 */
    printf("      dragging from x=%.1f to x=%.1f (icon 3's centre)\n", cx, target);
    check(target >= 0, "icon 3 is somewhere on the bar to aim at");

    dock_drag_motion(&server, cx - 18, y);       /* past the 6px threshold */
    check(server.dock_drag.moved == 1, "the gesture crosses the drag threshold");
    dock_drag_motion(&server, target, y);
    printf("      slot during drag: %d\n", server.dock_drag.slot);
    check(server.dock_drag.slot > 0 && server.dock_drag.slot < npins,
          "…and the slot follows the cursor into the middle of the row");

    /* ── 4. The release commits it ─────────────────────────────────────── */
    int want = server.dock_drag.slot;
    dock_drag_end(&server, target, y);
    check(server.dock_drag.active == 0, "the release ends the drag");
    printf("      dock_clock_slot after release: %d\n",
           server.config.dock_clock_slot);
    check(server.config.dock_clock_slot == want,
          "the clock's new slot is committed to the config");

    /* ── 5. …and the cell actually MOVED on the bar ────────────────────── */
    place_tree();
    double span2 = 0;
    double cx2 = find_clock_x(&span2);
    printf("      clock cell now: centre x=%.1f span=%.0fpx\n", cx2, span2);
    check(cx2 >= 0, "the clock cell is still hit-testable after the move");
    check(cx2 < last_icon, "…and is drawn up the row where it was dropped");

    /* ── 6. A press that never travels commits nothing ─────────────────── */
    int before = server.config.dock_clock_slot;
    dock_clock_drag_begin(&server, cx2, y);
    dock_drag_end(&server, cx2, y);
    check(server.config.dock_clock_slot == before,
          "a press with no travel leaves the slot alone");

    printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
