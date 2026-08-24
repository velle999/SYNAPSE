/*
 * dock_cell_slots_test.c — the three non-icon cells share ONE layout model.
 *
 * The clock, the all-apps button and the power button are all cells parked in a
 * gap of the icon row. They were not: the clock had a slot and the other two
 * were welded to the end of the run, which is why only the clock could be
 * moved. velle asked for the other two to move as well, and for a toggle beside
 * the drag because the drag targets are small — "it can be a bit hard to grab
 * the right place at this point on the dock".
 *
 * WHAT THIS EXISTS FOR, in order of how badly it would fail silently:
 *
 *   1. ⚠ THE STOCK DOCK MUST NOT MOVE. Every one of the three defaults to
 *      DOCK_SLOT_END, and a tie in one gap lays out in dock_cell_t order —
 *      which has to reproduce the old hard-coded tail exactly: icons, clock,
 *      apps, power. A generalisation that reorders the stock dock is a
 *      regression on every installed desktop, and nothing in a build sees it.
 *
 *   2. ⚠ THE APPS AND POWER BUTTONS ACT ON RELEASE NOW. They acted on press,
 *      which a draggable button cannot afford: the overlay would open on the
 *      way into every attempt to move one. So the release owes the click when
 *      the press never travelled — and owes NOTHING when it did.
 *
 *   3. ⚠ CENTRE IS A SENTINEL, not a resolved number. Storing n/2 would make a
 *      centred cell walk off-centre the moment an app opened, which is the very
 *      bug DOCK_SLOT_END exists to avoid, one position over.
 *
 *   4. ⚠ THE TOGGLE AND THE DRAG ARE ONE SETTING. dock_slot_cycle() and the
 *      drag write the same field, and dock_slot_label() has to be able to
 *      describe a gap the drag chose that no toggle would ever pick.
 *
 * Same rig and the same stubs as tests/dock_clock_drag_test.c, driving the
 * model the way input.c drives it. Cells are LOCATED by sweeping their hit
 * tests along the bar rather than by re-deriving their rects here: re-deriving
 * would mean a second copy of dock_metrics()' walk, and a test that agrees with
 * a bug in the walk it copied is worth nothing.
 *
 * Run as:
 *     ninja -C build && ./build/dock_cell_slots_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
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

/*
 * A REAL cairo context on a scratch image surface, and a buffer handle that is
 * never dereferenced (set_scene_buffer() below is the only thing handed it).
 *
 * ⚠ This used to return NULL, and that is exactly how the clock drag shipped
 * broken with this file green. `if (!buf) return;` is the third statement of
 * dock_render_output(), so a NULL here skips the whole renderer — INCLUDING the
 * dock_apply_position() call at the end of it, which is where the bar's own
 * position is written and where the bug was. The model was sound the entire
 * time; the half that was stubbed out flung the dock to 0,0 the moment the
 * gesture crossed its threshold, and the drag then measured the cursor against
 * a node position that had nothing to do with the bar on screen.
 *
 * The drawing itself is still cheap — every text, icon and rounded-rect call is
 * a stub below — but the CONTROL FLOW is now the real one.
 */
static struct wlr_buffer *const fake_buf = (struct wlr_buffer *)(uintptr_t)0x1;

struct wlr_buffer *create_cairo_buf(int w, int h, cairo_t **cr)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    *cr = cairo_create(surf);
    cairo_surface_destroy(surf);   /* the context holds its own reference */
    return fake_buf;
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
/* dock_ink_resolve() asks what is behind the dock so its clock, apps grid and
 * power mark can be inked for the surface they actually land on. UNMEASURED
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
/* The application overlay. Counted rather than ignored: the apps button acts on
 * RELEASE now, so "did the click fire" is a thing a dock test can ask. */
int appgrid_toggle_calls = 0;
void appgrid_toggle(syn_server_t *s) { (void)s; appgrid_toggle_calls++; }
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

#define NPINS 8

static void place_tree(void) { dock_relayout(&server); }

static double probe_y(void)
{
    return OUT_Y + OUT_H - server.config.dock_height / 2.0;
}

/* Sweep the run axis for a cell, by its own hit test. -1 when it is not there.
 * `lo`/`hi` come back as the run-axis extent, which is what the no-overlap
 * assertion needs and what a centre alone cannot give. */
typedef bool (*cell_hit_fn)(syn_server_t *, double, double);

static double find_cell(cell_hit_fn hit, double *lo, double *hi)
{
    double y = probe_y(), first = -1, last = -1;
    for (double x = OUT_X; x < OUT_X + OUT_W; x += 1.0)
        if (hit(&server, x, y)) { if (first < 0) first = x; last = x; }
    if (lo) *lo = first;
    if (hi) *hi = last;
    return first < 0 ? -1 : (first + last) / 2.0;
}

static double find_entry_x(const syn_dock_entry_t *want)
{
    double y = probe_y(), first = -1, last = -1;
    for (double x = OUT_X; x < OUT_X + OUT_W; x += 1.0)
        if (dock_entry_at(&server, x, y) == want) {
            if (first < 0) first = x;
            last = x;
        }
    return first < 0 ? -1 : (first + last) / 2.0;
}

static void set_pins(int n)
{
    static const char *const pins[] = {
        "steam", "firefox", "mpv", "chibi",
        "synfiles", "synpkg", "synstudio", "syntty",
        "syn-edit",
    };
    server.config.dock_pin_count = n;
    for (int i = 0; i < n; i++)
        snprintf(server.config.dock_pin[i], 128, "%s", pins[i]);
    dock_rebuild(&server);
    place_tree();
}

int main(void)
{
    printf("dock: the clock, apps and power cells share one slot model\n");

    snprintf(the_icon.exec, sizeof(the_icon.exec), "%s", "true");
    the_icon.icon_surface = NULL;

    memset(&server, 0, sizeof(server));
    memset(&output, 0, sizeof(output));
    wl_list_init(&server.outputs);
    for (int i = 0; i < WORKSPACE_MAX; i++)
        wl_list_init(&server.workspaces[i].windows);

    server.config.dock_enabled       = 1;
    server.config.dock_height        = 56;
    server.config.dock_edge          = SYN_DOCK_EDGE_BOTTOM;
    server.config.dock_clock         = 1;
    server.config.dock_apps_button   = 1;
    server.config.dock_power_button  = 1;
    server.config.dock_clock_slot    = DOCK_SLOT_END;
    server.config.dock_apps_slot     = DOCK_SLOT_END;
    server.config.dock_power_slot    = DOCK_SLOT_END;
    server.config.dock_magnify       = 1;
    server.config.dock_magnify_scale = 1.50;
    server.dock_drag.icon            = DOCK_DRAG_BAR;

    output.server = &server;
    output.dock.tree = &fake_tree;
    output.dock.shown = 1;
    output.dock.slide_progress = 1.0;
    wl_list_insert(&server.outputs, &output.link);

    set_pins(NPINS);
    check(server.dock_entry_count == NPINS, "eight pinned entries");

    double y = probe_y();

    /* ── 1. The stock tail is still icons, clock, apps, power ─────────── */
    double clk_lo, clk_hi, app_lo, app_hi, pwr_lo, pwr_hi;
    double clk = find_cell(dock_clock_at, &clk_lo, &clk_hi);
    double app = find_cell(dock_apps_at,  &app_lo, &app_hi);
    double pwr = find_cell(dock_power_at, &pwr_lo, &pwr_hi);
    printf("      clock=[%.0f,%.0f] apps=[%.0f,%.0f] power=[%.0f,%.0f]\n",
           clk_lo, clk_hi, app_lo, app_hi, pwr_lo, pwr_hi);
    check(clk >= 0 && app >= 0 && pwr >= 0, "all three cells are on the bar");

    double last_icon = find_entry_x(&server.dock_entries[NPINS - 1]);
    check(last_icon >= 0 && clk > last_icon,
          "…the clock is past the last icon, as DOCK_SLOT_END asks");
    check(clk < app && app < pwr,
          "…and the tail is clock, then apps, then power — the order the "
          "hard-coded version drew and the order dock_cell_t now decides");

    /* No two of them may overlap ANYWHERE: input.c asks apps, then power, then
     * clock, so a single pixel of overlap sends a press to the wrong cell and
     * the gesture silently becomes a different feature. */
    check(app_lo > clk_hi && pwr_lo > app_hi,
          "…with no overlap between any two cells");

    /* ── 2. The apps button can be dragged into the row ────────────────── */
    int bar_x = fake_tree.node.x, bar_y = fake_tree.node.y;
    int spawns_before = appgrid_toggle_calls;

    dock_apps_drag_begin(&server, app, y);
    check(server.dock_drag.active == 1, "a press on the apps button arms a drag");
    check(server.dock_drag.icon == DOCK_DRAG_APPS, "…and it is the APPS drag");
    check(appgrid_toggle_calls == spawns_before,
          "…and the PRESS does not open the application overlay");

    double target = find_entry_x(&server.dock_entries[2]);
    dock_drag_motion(&server, app - 18, y);
    check(server.dock_drag.moved == 1, "the gesture crosses the drag threshold");
    /* The same trap dock_clock_drag_test.c guards: dock_apply_position() has a
     * "dragging THE BAR" branch, and a sentinel test that read `icon < 0` would
     * catch this drag too and fling the whole dock to 0,0. */
    check(fake_tree.node.x == bar_x && fake_tree.node.y == bar_y,
          "…and the BAR does not move: a cell drag is not a bar drag");

    dock_drag_motion(&server, target, y);
    int want = server.dock_drag.slot;
    printf("      apps slot during drag: %d\n", want);
    check(want > 0 && want < NPINS,
          "…the slot follows the cursor into the middle of the row");

    dock_drag_end(&server, target, y);
    check(server.dock_drag.active == 0, "the release ends the drag");
    check(server.config.dock_apps_slot == want,
          "…and commits the new slot to dock_apps_slot");
    check(appgrid_toggle_calls == spawns_before,
          "…without opening the overlay, because the press TRAVELLED");

    place_tree();
    app = find_cell(dock_apps_at, &app_lo, &app_hi);
    double icon2 = find_entry_x(&server.dock_entries[2]);
    double icon1 = find_entry_x(&server.dock_entries[1]);
    check(app >= 0 && app > icon1 && app < icon2 + 1e9,
          "…and the button is drawn where it was dropped");

    /* ── 3. A press that does NOT travel is still the click ────────────── */
    dock_apps_drag_begin(&server, app, y);
    dock_drag_end(&server, app, y);
    check(appgrid_toggle_calls == spawns_before + 1,
          "a press with no travel opens the application overlay");

    /* ── 4. The power button, the same gesture, its own field ──────────── */
    pwr = find_cell(dock_power_at, &pwr_lo, &pwr_hi);
    dock_power_drag_begin(&server, pwr, y);
    check(server.dock_drag.icon == DOCK_DRAG_POWER, "the power cell has its own drag");
    double target5 = find_entry_x(&server.dock_entries[5]);
    dock_drag_motion(&server, pwr - 18, y);
    dock_drag_motion(&server, target5, y);
    int pwant = server.dock_drag.slot;
    dock_drag_end(&server, target5, y);
    check(server.config.dock_power_slot == pwant,
          "…and the release writes dock_power_slot, not the apps one");
    check(server.config.dock_apps_slot == want,
          "…leaving the apps button where it was");
    check(!server.dockmenu.visible,
          "…and a power drag does NOT open the power menu");

    /* ── 5. Centre re-centres; it is a sentinel, not a number ──────────── */
    server.config.dock_clock_slot = DOCK_SLOT_CENTER;
    place_tree();
    /* "In the middle" is asserted as a gap and not as a distance: the cell is
     * ninety-odd pixels wide and its centre is nowhere near any icon's, so a
     * tolerance would either be meaningless or would have to be re-derived from
     * the very layout under test. Sitting between icon n/2-1 and icon n/2 IS
     * gap n/2, exactly. */
    double clk8   = find_cell(dock_clock_at, NULL, NULL);
    double left8  = find_entry_x(&server.dock_entries[NPINS / 2 - 1]);
    double right8 = find_entry_x(&server.dock_entries[NPINS / 2]);
    check(clk8 >= 0, "a centred clock is on the bar");
    check(left8 < clk8 && clk8 < right8,
          "…in gap 4 of eight icons — the middle");

    set_pins(NPINS + 1);          /* an app opens */
    double clk9   = find_cell(dock_clock_at, NULL, NULL);
    double left9  = find_entry_x(&server.dock_entries[(NPINS + 1) / 2 - 1]);
    double right9 = find_entry_x(&server.dock_entries[(NPINS + 1) / 2]);
    check(clk9 >= 0 && left9 < clk9 && clk9 < right9,
          "…and it MOVES UP to gap 4 of nine when a ninth icon opens — a "
          "stored n/2 would have stayed in the old gap");
    check(server.config.dock_clock_slot == DOCK_SLOT_CENTER,
          "…with the sentinel intact, not resolved into the config");
    set_pins(NPINS);

    /* ── 6. The toggle and the drag are the same setting ───────────────── */
    server.config.dock_clock_slot = DOCK_SLOT_END;
    check(strcmp(dock_slot_label(&server, DOCK_CELL_CLOCK), "right") == 0,
          "a bottom dock calls DOCK_SLOT_END \"right\"");
    server.config.dock_edge = SYN_DOCK_EDGE_LEFT;
    check(strcmp(dock_slot_label(&server, DOCK_CELL_CLOCK), "bottom") == 0,
          "…and a LEFT dock calls the same value \"bottom\"");
    server.config.dock_edge = SYN_DOCK_EDGE_BOTTOM;

    dock_slot_cycle(&server, DOCK_CELL_CLOCK, +1);
    check(server.config.dock_clock_slot == DOCK_SLOT_START,
          "cycling forward from the end wraps to the start");
    dock_slot_cycle(&server, DOCK_CELL_CLOCK, +1);
    check(server.config.dock_clock_slot == DOCK_SLOT_CENTER,
          "…then to the centre");
    dock_slot_cycle(&server, DOCK_CELL_CLOCK, -1);
    check(server.config.dock_clock_slot == DOCK_SLOT_START,
          "…and backwards again, so the panel's Left is not the panel's Right");

    /* A gap a DRAG chose is none of the three, and the label says so rather
     * than rounding to the nearest word — which is the whole of "the toggle
     * always reports where the cell actually is". */
    server.config.dock_clock_slot = 3;
    check(strcmp(dock_slot_label(&server, DOCK_CELL_CLOCK), "after 3 icons") == 0,
          "a dragged gap is named for what it is, not rounded to a preset");
    dock_slot_cycle(&server, DOCK_CELL_CLOCK, +1);
    check(server.config.dock_clock_slot == DOCK_SLOT_START,
          "…and cycling from it enters the ring at a definite place");

    /* ── 7. Everything still fits together at the far end ──────────────── */
    server.config.dock_clock_slot = DOCK_SLOT_START;
    server.config.dock_apps_slot  = DOCK_SLOT_START;
    server.config.dock_power_slot = DOCK_SLOT_START;
    place_tree();
    clk = find_cell(dock_clock_at, &clk_lo, &clk_hi);
    app = find_cell(dock_apps_at,  &app_lo, &app_hi);
    pwr = find_cell(dock_power_at, &pwr_lo, &pwr_hi);
    check(clk >= 0 && app >= 0 && pwr >= 0,
          "all three at the START are all still on the bar");
    check(clk < app && app < pwr,
          "…in dock_cell_t order, the same tie-break as at the end");
    check(app_lo > clk_hi && pwr_lo > app_hi,
          "…and still not overlapping");
    double first_icon = find_entry_x(&server.dock_entries[0]);
    check(first_icon > pwr_hi,
          "…with every icon after them");

    printf("\n");
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}
