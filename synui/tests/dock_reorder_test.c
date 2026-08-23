/*
 * dock_reorder_test.c — dragging a dock icon to a new place in the row.
 *
 * A left press on a dock icon no longer launches the app. It arms a drag, and
 * the RELEASE decides which gesture it was: a press that never travelled is the
 * click it always was, and one that did rearranges the row. That split is
 * cheap to get subtly wrong in a way nothing complains about — a rearrange that
 * also raises the app it moved, or a click that quietly re-pins something —
 * so the model is driven here directly, exactly as input.c drives it.
 *
 * There is no way to synthesize a pointer drag on the headless backend
 * (wlroots' headless backend has no input devices, and synui implements
 * virtual-keyboard but not virtual-pointer), so this links dock.c against stubs
 * for the compositor half, the same shape tests/deskicon_drag_test.c uses for
 * the desktop icons.
 *
 * What is asserted, in order of how easy each is to break:
 *
 *   1. A press with no travel is a CLICK. The whole reason the press stopped
 *      launching things is that a rearrange would otherwise launch as well;
 *      the counterpart is that an ordinary click must still work.
 *
 *   2. Reordering writes the PIN LIST, because that is the only order that
 *      survives a logout. dock_rebuild() lays pinned entries out first in
 *      config order, so a display position inside the pinned run IS a pin
 *      index — and one past it is not a position at all.
 *
 *   3. An UNPINNED (running-only) icon dropped among the pins gets pinned
 *      there — the macOS gesture — and dropped among the other running apps
 *      stays unpinned, because there is no order there to write down.
 *
 *   4. A drag whose slot did not change writes nothing. Otherwise the 6px
 *      threshold becomes the only thing standing between a nudge and a pin.
 *
 *   5. The dragged entry is identified by app_id at release, not only by
 *      index. dock_rebuild() replaces the whole entry array on any map or
 *      unmap, so an app finishing its launch mid-gesture shifts everything
 *      after it — and acting on the stale index would rearrange something the
 *      user never touched.
 *
 * Run as:
 *     ninja -C build && ./build/dock_reorder_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synui.h"

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

/* ── The compositor, stubbed ─────────────────────────────── */

/* One 1920x1080 output at the layout origin. dock.c asks for this to centre the
 * bar and to clip the slide, and for nothing else. */
void output_box_of(syn_server_t *s, syn_output_t *o, struct wlr_box *box)
{
    (void)s; (void)o;
    box->x = 0; box->y = 0; box->width = 1920; box->height = 1080;
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

#define ICON 48       /* DOCK_ICON_SIZE — dock.c's, and private to it */
#define PAD  8        /* DOCK_ICON_PAD */

static syn_server_t  server;
static syn_output_t  output;

/* The bar's own left edge in layout coordinates, for the current entry count.
 * dock.c centres the run on the output; this is the same arithmetic, spelt out
 * so a change to the layout has to be made in both places deliberately. */
static double bar_left(void)
{
    int n = server.dock_entry_count;
    int run = n > 0 ? n * ICON + (n + 1) * PAD : PAD * 2;
    /* INTEGER division, deliberately, and the cast is there to say so. dock.c's
     * dock_geometry() truncates here too, and the tree's position is an int
     * either way — rounding differently would put this rig's idea of the bar
     * half a pixel from the compositor's and quietly move every slot boundary
     * that the drop-target arithmetic is measured against. */
    return (double)((1920 - run) / 2);
}

/* The layout-coordinate centre of the icon currently in display slot `i`. */
static void slot_centre(int i, double *lx, double *ly)
{
    *lx = bar_left() + PAD + i * (ICON + PAD) + ICON / 2.0;
    /* Vertically anywhere inside the bar; the run axis is what decides a slot
     * on a bottom dock. */
    *ly = 1080 - server.config.dock_height / 2.0;
}

/*
 * Write the hit-boxes the RENDERER would have written, and put the scene tree
 * where dock_apply_position would have put it.
 *
 * Both are normally set by dock_render_output(), which is stubbed out here —
 * and dock_rebuild() zeroes the boxes on its way past, so this has to run after
 * anything that rebuilds. That includes committing a drag, which is why the
 * gesture helper below calls it too: without that, the first successful
 * rearrange leaves every icon's hit-box at 0,0 and the next drag cannot find
 * anything to press.
 */
static void sync_hitboxes(void)
{
    for (int i = 0; i < server.dock_entry_count; i++) {
        server.dock_entries[i].x = PAD + i * (ICON + PAD);
        server.dock_entries[i].y = (server.config.dock_height - ICON) / 2 - 4;
        server.dock_entries[i].w = ICON;
        server.dock_entries[i].h = ICON;
    }
    fake_tree.node.x = (int)bar_left();
    fake_tree.node.y = 1080 - server.config.dock_height;
}

static void relayout(void)
{
    dock_rebuild(&server);
    sync_hitboxes();
}

static void set_pins(const char *const *pins, int n)
{
    server.config.dock_pin_count = n;
    for (int i = 0; i < n; i++)
        snprintf(server.config.dock_pin[i], 128, "%s", pins[i]);
    relayout();
}

/*
 * Drag the icon in display slot `from` onto display slot `to`. `travel` says
 * whether the gesture crosses the drag threshold at all — false is a click.
 *
 * The first motion is a fixed nudge rather than the midpoint of the two slots,
 * and that is not arbitrary: a drag from a slot back to ITSELF has no midpoint
 * to travel to, so a midpoint-based rig would never arm `moved` and the case it
 * was written to test — a real drag whose slot did not change — would silently
 * become a click. The nudge is past DOCK_DRAG_THRESHOLD (6px) and well inside
 * one cell, so it arms the gesture without changing which slot is under it.
 */
static void drag(int from, int to, bool travel)
{
    double fx, fy, tx, ty;
    slot_centre(from, &fx, &fy);
    slot_centre(to, &tx, &ty);

    syn_dock_entry_t *e = dock_entry_at(&server, fx, fy);
    if (!e) { printf("  FAIL  no icon at slot %d\n", from); failures++; return; }
    dock_icon_drag_begin(&server, e, fx, fy);

    if (travel) {
        /* Two motions: input.c sends a stream, and the model has to survive
         * being given one rather than a single jump to the destination. */
        dock_drag_motion(&server, fx + 18, fy);
        dock_drag_motion(&server, tx, ty);
    }
    dock_drag_end(&server, travel ? tx : fx, travel ? ty : fy);

    /* A committed reorder rebuilds the entries and zeroes their hit-boxes; the
     * renderer that would put them back is stubbed out. */
    sync_hitboxes();
}

static const char *pin_order(void)
{
    static char buf[512];
    buf[0] = '\0';
    for (int i = 0; i < server.config.dock_pin_count; i++) {
        if (i) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, server.config.dock_pin[i], sizeof(buf) - strlen(buf) - 1);
    }
    return buf;
}

static void expect_pins(const char *want, const char *what)
{
    const char *got = pin_order();
    if (strcmp(got, want) == 0) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s — wanted [%s], got [%s]\n", what, want, got);
        failures++;
    }
}

int main(void)
{
    printf("dock: drag an icon to rearrange it\n");

    snprintf(the_icon.exec, sizeof(the_icon.exec), "%s", "true");
    the_icon.icon_surface = NULL;

    memset(&server, 0, sizeof(server));
    memset(&output, 0, sizeof(output));
    wl_list_init(&server.outputs);
    for (int i = 0; i < WORKSPACE_MAX; i++)
        wl_list_init(&server.workspaces[i].windows);

    server.config.dock_enabled = 1;
    server.config.dock_autohide = 0;
    server.config.dock_height = 64;
    server.config.dock_edge = SYN_DOCK_EDGE_BOTTOM;
    server.dock_drag.icon = -1;

    output.server = &server;
    output.dock.tree = &fake_tree;
    output.dock.shown = 1;
    output.dock.slide_progress = 1.0;
    wl_list_insert(&server.outputs, &output.link);

    const char *const four[] = { "alpha", "bravo", "charlie", "delta" };
    set_pins(four, 4);
    expect_pins("alpha bravo charlie delta", "the pins start in config order");

    /* ── 1. A press that does not travel is a click ────────────────────── */
    spawns = 0;
    drag(0, 0, false);
    check(spawns == 1, "a press with no travel launches the app");
    expect_pins("alpha bravo charlie delta", "…and rearranges nothing");

    /* ── 2. A real drag reorders the PIN LIST ──────────────────────────── */
    spawns = 0; saves = 0;
    drag(0, 2, true);
    expect_pins("bravo charlie alpha delta", "dragging the first icon to slot 2");
    check(spawns == 0, "…without also launching what it moved");
    check(saves > 0, "…and persisting the new order");

    drag(2, 0, true);
    expect_pins("alpha bravo charlie delta", "…and dragging it back undoes it");

    /* Backwards, which walks the shift loop the other way — the one line of
     * this that is easy to write as a copy of the forward case. */
    drag(3, 1, true);
    expect_pins("alpha delta bravo charlie", "dragging the last icon to slot 1");
    set_pins(four, 4);

    /* ── 3. …and it CLAMPS to the pin list ─────────────────────────────── */
    /* A pinned icon cannot be dropped outside the pinned run: there is nowhere
     * outside it that an order can be written down. The nearest place it can
     * actually be is the end of the pins. */
    drag(0, 3, true);
    expect_pins("bravo charlie delta alpha", "a pinned icon dropped at the end");
    set_pins(four, 4);

    /* ── 4. A drag that changes nothing writes nothing ─────────────────── */
    saves = 0; spawns = 0;
    drag(1, 1, true);
    expect_pins("alpha bravo charlie delta", "a drag back to the same slot");
    check(saves == 0, "…writes no state");
    check(spawns == 0, "…and is not a click either");

    /* ── 5. An unpinned icon ───────────────────────────────────────────── */
    /*
     * A running-only entry sits AFTER the pins, appended by dock_rebuild. It is
     * synthesised here rather than mapped, because a real view would mean a
     * wlr_surface and a workspace list and this rule cannot see either — it
     * works off the app_id and the pin list.
     *
     * Re-synthesised after every relayout for the same reason: dock_rebuild()
     * derives the entries from the pin list and the mapped views, and there are
     * no mapped views here, so anything not pinned is dropped on each rebuild.
     */
    set_pins(four, 4);
    syn_dock_entry_t *extra = &server.dock_entries[server.dock_entry_count];
    snprintf(extra->app_id, sizeof(extra->app_id), "%s", "echo");
    extra->pinned = 0;
    extra->running = 1;
    server.dock_entry_count++;
    extra->x = PAD + 4 * (ICON + PAD);
    extra->y = (server.config.dock_height - ICON) / 2 - 4;
    extra->w = ICON; extra->h = ICON;
    /* The bar just got one icon wider, so its left edge moved. */
    fake_tree.node.x = (int)bar_left();

    drag(4, 1, true);
    expect_pins("alpha echo bravo charlie delta",
                "an unpinned icon dropped among the pins gets pinned there");

    /* ── 6. The identity check ─────────────────────────────────────────── */
    /*
     * Press an icon, then let the entry array be replaced under the gesture —
     * which is what dock_rebuild() does on any map or unmap. The index the
     * press captured now names a different app. Committing on it would
     * rearrange something the user never touched, and there is no way to tell
     * afterwards that it happened, so the gesture is abandoned instead.
     */
    set_pins(four, 4);
    double px, py, qx, qy;
    slot_centre(0, &px, &py);
    slot_centre(2, &qx, &qy);
    syn_dock_entry_t *pressed = dock_entry_at(&server, px, py);
    check(pressed != NULL, "an icon under the press point");
    dock_icon_drag_begin(&server, pressed, px, py);
    dock_drag_motion(&server, (px + qx) / 2, py);

    /* "charlie" launches and the array is rebuilt with it first. Index 0 is now
     * a different app from the one the press landed on. */
    const char *const shifted[] = { "charlie", "alpha", "bravo", "delta" };
    server.config.dock_pin_count = 4;
    for (int i = 0; i < 4; i++)
        snprintf(server.config.dock_pin[i], 128, "%s", shifted[i]);
    dock_rebuild(&server);

    saves = 0; spawns = 0;
    dock_drag_motion(&server, qx, qy);
    dock_drag_end(&server, qx, qy);
    expect_pins("charlie alpha bravo delta",
                "an entry array replaced mid-drag abandons the reorder");
    check(saves == 0, "…and writes nothing");
    check(spawns == 0, "…and does not fall back to launching either");

    if (failures) {
        printf("dock_reorder_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("dock_reorder_test: OK\n");
    return 0;
}
