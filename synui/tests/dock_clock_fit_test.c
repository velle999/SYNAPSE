/*
 * dock_clock_fit_test — does the dock's clock actually FIT its cell?
 *
 * ⚠ THE OTHER DOCK RIG CANNOT SEE THIS BUG, AND THAT IS WHY THIS FILE EXISTS.
 * dock_options_test stubs syn_text_extents() to zero on purpose — it asserts
 * the layout's FLOOR, and says so — so every string it measures is 0px wide and
 * a clock cell can never overflow in it. The dock's clock ran off both edges of
 * a vertical column for as long as vertical docks have existed, with a green
 * suite the whole time.
 *
 * So this rig draws with cairo's real toy text API and reads the PIXELS back.
 *
 * ⚠ IT IS NOT A PIXEL DIFF. There is no reference image and nothing here cares
 * what the time says: the assertion is a PROPERTY — "no bright pixel lands in
 * the margin at the edge of the bar" — which is exactly what "it does not fit"
 * means and is the one thing a screenshot comparison could not have told us
 * without breaking every time the clock ticked.
 *
 * THE BUG. A horizontal bar's clock cell is measured and grows along the run
 * until the string fits. A vertical column's run is its HEIGHT; the width is
 * `dock_height`, and nothing the clock does can change it. The old code gave up
 * and used a flat 40px cell with a 15px time centred in it — and "12:34:56 PM"
 * at 15px is half as wide again as the 64px column it was drawn in.
 *
 * Two fixes, both pinned here: the digital clock brings its FONT SIZE down until
 * the string fits the column, and the new analog face sidesteps the question by
 * being square.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

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
#include <time.h>

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

void syn_show_text(cairo_t *cr, const char *text) { cairo_show_text(cr, text); }
void syn_text_extents(cairo_t *cr, const char *text, cairo_text_extents_t *ext)
{ cairo_text_extents(cr, text, ext); }
/* text.c's own default, restated rather than linked: pulling text.c in would
 * bring fontconfig and the whole fallback cache along for one string. */
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

/*
 * ⚠ THE CLOCK IS FROZEN, and it has to be.
 *
 * The ink assertions below count lit pixels inside the clock's cell, and what
 * is drawn there is the actual time — so the count moves with the digits. At a
 * 32px column the difference between a thin minute and a fat one is about ten
 * pixels against a threshold of forty, which made this test pass or fail by
 * the clock on the wall. It cost a wholly innocent change an hour in 447.
 *
 * dock.c calls time(NULL) and nothing else, and it is statically linked into
 * this binary, so a definition here is the one it resolves to. TZ is pinned as
 * well: localtime_r() is what turns the epoch into digits.
 *
 * 11:11 on the 11th is deliberate — the thinnest time there is. An assertion
 * that holds for it holds for every other.
 */
/* 2026-01-11 11:11:00 UTC. */
#define FAKE_NOW ((time_t)1768129860)
time_t time(time_t *t)
{
    time_t fake = FAKE_NOW;
    if (t) *t = fake;
    return fake;
}
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


/* ── Reading the picture back ────────────────────────────── */

/*
 * Is there INK at this pixel — text, rather than the bar it is drawn on?
 *
 * The rig's palette is chosen to make this a threshold rather than a judgement:
 * panel_bg is near-black, panel_ink is near-white, and the clock's two strings
 * are drawn at 0.95 and 0.62 alpha. The only other ink in the cell is the pair
 * of hairlines beside it, at 0.22 — well under the line — and they are inset 10px
 * from the body edges anyway, which is outside every margin this file probes.
 */
static bool ink_at(int x, int y)
{
    if (!render_surface) return false;
    if (x < 0 || y < 0 || x >= buf_w || y >= buf_h) return false;

    cairo_surface_flush(render_surface);
    const unsigned char *data = cairo_image_surface_get_data(render_surface);
    int stride = cairo_image_surface_get_stride(render_surface);
    const uint32_t *px = (const uint32_t *)(data + (size_t)y * stride);
    uint32_t p = px[x];

    /* ARGB32 is PREMULTIPLIED, so a bright pixel at low alpha is not bright.
     * Reading the channels raw is the right test here precisely because of
     * that: what reaches the screen is what is in the buffer. */
    int r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
    return (r + g + b) / 3 > 150;
}

/* The clock cell's span along the run, found the way a click finds it. */
static void clock_span(double bar_x, double bar_y, bool vertical,
                       int *first, int *last)
{
    *first = *last = -1;
    int len = vertical ? buf_h : buf_w;
    for (int i = 0; i < len; i++) {
        double px = vertical ? bar_x + 4       : bar_x + i;
        double py = vertical ? bar_y + i       : bar_y + 4;
        /* Probe down the middle of the cross axis, where the cell always is. */
        if (vertical) px = bar_x + server.config.dock_height / 2.0;
        else          py = bar_y + server.config.dock_height / 2.0;
        if (dock_clock_at(&server, px, py)) {
            if (*first < 0) *first = i;
            *last = i;
        }
    }
}

/*
 * How many bright pixels land in the MARGIN — the outermost `margin` pixels of
 * the bar's fixed axis, across the clock cell's own rows.
 *
 * On a column the fixed axis is the width, which is the axis that cannot grow
 * and therefore the only one a string can overflow. On a bar it is the height,
 * checked for completeness: a time too tall for the slab is the same failure
 * turned ninety degrees.
 */
static int margin_ink(bool vertical, int cell_first, int cell_last, int margin)
{
    int hits = 0;
    int fixed = vertical ? buf_w : buf_h;
    for (int i = cell_first; i <= cell_last; i++)
        for (int m = 0; m < margin; m++) {
            int lo = m, hi = fixed - 1 - m;
            if (vertical) {
                if (ink_at(lo, i)) hits++;
                if (ink_at(hi, i)) hits++;
            } else {
                if (ink_at(i, lo)) hits++;
                if (ink_at(i, hi)) hits++;
            }
        }
    return hits;
}

/* Every bright pixel in the cell, so "it fits" cannot pass by drawing nothing. */
static int cell_ink(bool vertical, int cell_first, int cell_last)
{
    int hits = 0;
    int fixed = vertical ? buf_w : buf_h;
    for (int i = cell_first; i <= cell_last; i++)
        for (int j = 0; j < fixed; j++)
            hits += ink_at(vertical ? j : i, vertical ? i : j) ? 1 : 0;
    return hits;
}

static void rig_init(void)
{
    snprintf(the_icon.exec, sizeof(the_icon.exec), "%s", "true");
    memset(&server, 0, sizeof(server));
    memset(&output, 0, sizeof(output));
    wl_list_init(&server.outputs);
    for (int i = 0; i < WORKSPACE_MAX; i++)
        wl_list_init(&server.workspaces[i].windows);

    /* Near-black on near-white: see ink_at(). */
    server.config.panel_bg[0] = 0.03f; server.config.panel_bg[1] = 0.03f;
    server.config.panel_bg[2] = 0.05f; server.config.panel_bg[3] = 1.0f;
    server.config.panel_accent[0] = 0.00f; server.config.panel_accent[1] = 0.85f;
    server.config.panel_accent[2] = 0.75f; server.config.panel_accent[3] = 1.0f;
    server.config.panel_ink[0] = 1.00f; server.config.panel_ink[1] = 1.00f;
    server.config.panel_ink[2] = 1.00f; server.config.panel_ink[3] = 1.0f;
    server.config.dock_opacity = 1.0f;   /* opaque: nothing to see through */
    server.config.dock_radius  = 26;
    server.config.dock_enabled = 1;
    server.config.dock_magnify = 0;      /* a flat row; the swell is not the subject */
    server.config.dock_magnify_scale = 1.6f;
    server.config.dock_clock      = 1;
    server.config.dock_clock_slot = -1;
    server.config.dock_height     = 64;
    server.config.dock_edge       = SYN_DOCK_EDGE_BOTTOM;
    server.dock_drag.icon = DOCK_DRAG_BAR;

    /* ⚠ THE WIDEST STRING THE CLOCK CAN PRODUCE, deliberately. 12-hour with
     * seconds and an am/pm is "12:34:56 PM" — and a rig that tested "13:04"
     * would pass on a bug the moment somebody turned seconds on. */
    server.clock.fmt24   = 0;
    server.clock.seconds = 1;

    fake_wlr_output.data = &output;
    output.wlr_output = &fake_wlr_output;
    output.server = &server;
    output.dock.tree = &fake_tree;
    output.dock.shown = 1;
    output.dock.slide_progress = 1.0;
    wl_list_insert(&server.outputs, &output.link);

    static const char *const pins[] = { "one", "two", "three" };
    set_pins(pins, 3);
    dock_rebuild(&server);
}

/* Render one arrangement and report where the clock cell landed. */
static void arrange(syn_dock_edge_t edge, int analog, int thick,
                    bool *vertical, int *first, int *last)
{
    server.config.dock_edge         = edge;
    server.config.dock_clock_analog = analog;
    server.config.dock_height       = thick;
    dock_relayout(&server);

    *vertical = (edge == SYN_DOCK_EDGE_LEFT || edge == SYN_DOCK_EDGE_RIGHT);
    clock_span(fake_tree.node.x, fake_tree.node.y, *vertical, first, last);
}

int main(void)
{
    /* With time() frozen above, TZ is the other half of "what digits get
     * drawn": localtime_r() reads it, and a developer in Sydney would otherwise
     * render a different hour from CI. */
    setenv("TZ", "UTC", 1);
    tzset();

    printf("dock clock: does it fit the cell it is given?\n");
    rig_init();

    bool vert; int first, last;

    /* ── 1. The column, digital — the reported bug ─────────────────────── */
    /* Three thicknesses: the stock dock, the thinnest the size row offers and
     * a large one. A fit that only works at 64 is a constant in disguise. */
    static const int THICKS[] = { 32, 64, 128 };
    for (size_t i = 0; i < sizeof(THICKS) / sizeof(THICKS[0]); i++) {
        int t = THICKS[i];
        arrange(SYN_DOCK_EDGE_LEFT, 0, t, &vert, &first, &last);

        char what[96];
        snprintf(what, sizeof(what),
                 "a %dpx column draws its clock inside the bar", t);
        bool found = first >= 0;
        check(found, what);
        if (!found) continue;

        snprintf(what, sizeof(what), "…and there is a clock there at all (%d)", t);
        check(cell_ink(vert, first, last) > 40, what);

        /* 2px, not 1: an antialiased glyph edge one pixel inside the bar is
         * still inside it, and a test that failed on that would be a test
         * nobody could keep passing. */
        snprintf(what, sizeof(what),
                 "…with nothing spilling into the %dpx bar's edge", t);
        check_int(margin_ink(vert, first, last, 2), 0, what);
    }

    /* ── 2. The column, analog ─────────────────────────────────────────── */
    arrange(SYN_DOCK_EDGE_LEFT, 1, 64, &vert, &first, &last);
    check(first >= 0, "a column's analog clock is hit-testable");
    check_int(last - first + 1, 64,
              "…in a SQUARE cell — the whole reason a face fits a column");
    check(cell_ink(vert, first, last) > 40, "…and a face is drawn in it");
    check_int(margin_ink(vert, first, last, 2), 0,
              "…inside the bar, like the digits");

    /* The face is not measured, so the clock's settings cannot change its
     * size — which is what makes it the arrangement that cannot overflow. */
    int a_first = first, a_last = last;
    server.clock.seconds = 0;
    server.clock.fmt24   = 1;
    arrange(SYN_DOCK_EDGE_LEFT, 1, 64, &vert, &first, &last);
    check(first == a_first && last == a_last,
          "…and the cell is the same size in 24-hour with no seconds");
    server.clock.seconds = 1;
    server.clock.fmt24   = 0;

    /* ── 3. The bar is untouched ───────────────────────────────────────── */
    arrange(SYN_DOCK_EDGE_BOTTOM, 0, 64, &vert, &first, &last);
    check(first >= 0, "a horizontal bar still has its clock");
    check(last - first + 1 > 92,
          "…in a cell MEASURED past its 92px floor for a long time");
    check(cell_ink(vert, first, last) > 40, "…with the time drawn in it");
    check_int(margin_ink(vert, first, last, 2), 0,
              "…and nothing spilling out of the slab");

    printf("dock_clock_fit_test: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
