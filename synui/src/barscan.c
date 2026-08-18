/*
 * barscan.c — what is ACTUALLY under a see-through surface.
 *
 * Two measurements, one walk, and the file is named for the first of them
 * because the bar was the first surface that needed it:
 *
 *   - bar_strip_lum[], SYN_LUM_COLS columns across the strip the bar occupies
 *   - scene_lum[], the same question over the WHOLE output, cell for cell with
 *     wallpaper.c's wp_lum_grid — which is what every surface that is not the
 *     bar asks, because every one of them opens where it is put
 *
 * The second exists because the bar was never the only surface with a backdrop
 * it could not see. The start menu, the bar's own menus, the mixer, the OSD and
 * the thirty panels synui draws all measured the WALLPAPER under themselves and
 * inked accordingly — correct on an empty desktop and wrong on every other one,
 * because a menu opened over a browser is over the browser, not over the
 * photograph the browser is covering. Same bug, same fix, one grid wider.
 *
 * ⚠ THE SCAN IS A SETTING — `scene_ink`, on by default, read at the top of
 * every scan through scene_ink_on(). With it off both arrays stay -1, which is
 * already "nothing covers this cell" and puts every consumer back on the
 * wallpaper alone; nothing else has to be told. That is why the clear is
 * unconditional and the gate is the line after it.
 *
 * The bill it buys out of is bounded and worth stating, because "reads pixels
 * back off the GPU 2.5 times a second" invites worse arithmetic than the truth:
 * with nothing covering the wallpaper the walk finds nothing and reads NOTHING,
 * and with every cell covered by one maximized window a row coalesces to a
 * single band read — nine an output, ~80 KB each. It is the runs and the bands
 * that make that true; see scan_grid() and BARSCAN_BAND.
 *
 * wallpaper.c answers "what is under the bar" with the wallpaper, because for
 * most of this compositor's life that was the whole answer: the bar reserves an
 * exclusive zone, so a tiled or maximized window is laid out BELOW it and the
 * only thing behind the glass is the picture. Two cases break that, and both
 * are ones a user arranges on purpose:
 *
 *   - AUTO-HIDE. `exclusiveZone: bar.autohide ? 0 : Theme.barSpan` (Bar.qml) —
 *     an auto-hiding bar reserves nothing, so every maximized window comes up
 *     underneath it and the wallpaper behind the bar is not on screen at all.
 *   - A FLOATING WINDOW dragged over the strip. Floating views are placed where
 *     the user puts them, not inside usable_area.
 *
 * In both, wallpaper.c measures a picture nobody can see and the bar inks
 * itself for it — dark text on a dark window, which is the bug this file is
 * for.
 *
 * ⚠ THIS DOES NOT REPLACE THE WALLPAPER MEASUREMENT, it overlays it. A column
 * that no window covers stays -1 here and the consumer falls back to that
 * output's wp_lum_grid top row, so a desktop with nothing under its bar
 * publishes exactly the numbers it published before this file existed. That
 * fallback is the whole safety story: the common case is not merely unchanged,
 * it is not even computed differently.
 *
 * ── Why a SEE-THROUGH window is composited and not declined ──────────────────
 *
 * ⚠ THIS FILE ONCE ANSWERED "I CANNOT SEE THAT" FOR EVERY WINDOW ON THE TWO
 * THEMES THE MEASUREMENT EXISTS FOR, WHICH IS AS GOOD AS BEING SWITCHED OFF.
 *
 * A node under the cut is not always solid. Two ways, and both are ordinary:
 * synui fades windows itself (active_opacity/inactive_opacity, a scene node
 * modifier), and a client can draw its own surface see-through (foot_alpha, the
 * desktop widgets), which arrives as a buffer whose PREMULTIPLIED pixels read
 * darker than anything on screen. Either way the pixels alone are not what the
 * user is looking at.
 *
 * The old answer was to decline both — -1, "the wallpaper answers here" — on
 * the grounds that most of what shows through such a surface is the wallpaper
 * anyway. That is defensible for a 30%-opaque terminal and indefensible for the
 * case it actually hit: macOS 26 fades to 0.94/0.88 and Prism to 0.90/0.84, so
 * on the two glass presets EVERY window was under the 0.9 gate — Prism did not
 * measure so much as a focused one — and `scene.<output>` stayed a full row of
 * -1 with a screen full of windows. Live backdrop read `on` and did nothing.
 *
 * So the fade is composited instead of being treated as a failure. It is not a
 * guess: the node opacity is synui's own number, the buffer's mean alpha is in
 * the pixels we already read, and what is behind is the wallpaper cell this
 * same grid is published beside. The mix goes through the encoding the GPU
 * mixes in (syn_lum_over/syn_lum_to_srgb, see contrast.h).
 *
 * ⚠ AND IT IS NEVER WORSE THAN THE -1 IT REPLACES, which is what makes it safe
 * where it is approximate. Compositing against the WALLPAPER is exact for a
 * tiled window and for a lone floating one; where a see-through window sits
 * over ANOTHER window it is wrong by whatever that window differs from the
 * picture — and that is precisely the answer -1 already produced, plus the top
 * window's own tint at its own alpha. An unmeasurable wallpaper still declines,
 * because then there is nothing to composite against and inventing a number is
 * how a bar gets inked confidently backwards.
 *
 * ── Why a scan and not a readback of the frame ───────────────────────────────
 *
 * The obvious implementation is to read the output's own front buffer back over
 * the strip. It cannot work: the bar is IN that buffer. Reading it back feeds
 * the bar's own ink into the measurement that chooses the bar's ink, which is a
 * loop that settles wherever it likes. So we ask the scene graph what is under
 * the bar instead, and read pixels from that node alone.
 *
 * ── Why the cut-off is layer_tree[TOP] and not a list of trees ───────────────
 *
 * "Under the bar" is a z-order question, and the scene graph already holds the
 * answer. The bar is a layer surface in layer_tree[TOP], so everything at or
 * above that node in the root's child list is the bar or is over it — the OSDs,
 * the lock screen, the drag icon, synui's own panels. Walking the root's
 * children top-first and starting to look only after passing layer_tree[TOP]
 * gets all of that right for free, and keeps getting it right when the z-order
 * changes, which a hard-coded list of trees would not.
 */

#include "synui.h"
#include "contrast.h"

#include <drm_fourcc.h>
#include <math.h>      /* fabs, for the debug trace's change test */
#include <stdlib.h>
#include <string.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

/* One per wp_lum_grid COLUMN, so a consumer that falls back to the wallpaper
 * does it cell-for-cell rather than resampling one grid into the other. */
#define BARSCAN_COLS SYN_LUM_COLS

/* How often the strip is re-scanned, in milliseconds.
 *
 * This is a poll and not a damage hook on purpose. The thing we are watching is
 * "what colour is the region under the bar", which changes with every frame a
 * covering client draws — a video, a terminal scrolling, a page being scrolled
 * past — and none of those are events worth re-inking a bar for at frame rate.
 * 400ms is under the threshold where a user reading the bar notices it was
 * briefly wrong, and far over the rate at which a GPU readback costs anything.
 */
#define BARSCAN_INTERVAL_MS 400

/* Opaque enough to answer WITHOUT knowing what is behind it.
 *
 * This is no longer the line between "measured" and "declined" — a see-through
 * node is composited against the wallpaper (see the header). It is what is left
 * of that question for the one case where compositing is impossible: an
 * unmeasurable wallpaper, where a node this solid is still its own answer and
 * anything fainter has nothing to be drawn over. Not 1.0: clients round their
 * own opacity, and a window at "fully opaque" routinely arrives as 0.996. */
#define BARSCAN_OPAQUE_ALPHA 0.9

/* …and solid enough that nothing shows through at all, so the composite is the
 * surface itself and the backdrop need not even be looked at. */
#define BARSCAN_SOLID_ALPHA 0.999

/* Every Nth pixel, both axes, when averaging a readback. The strip is 34
 * logical rows of a screen that is 2560 wide; a mean over every pixel and a
 * mean over every 4th differ in the third decimal, and the ink flips over a
 * band 0.05 wide. */
#define BARSCAN_STEP 4

/*
 * How many logical rows of each GRID cell are actually read back.
 *
 * ⚠ THE GRID CANNOT AFFORD THE STRIP'S "READ ALL OF IT". The strip is 34 rows
 * of one screen; the grid is every row of every screen, and reading each cell
 * whole is the entire framebuffer off the GPU 2.5 times a second — 14 MB per
 * output per tick on a 1440p monitor, which is a stall on the main loop in
 * service of a number that decides between two colours of text.
 *
 * So a band across the middle of each cell instead: full cell WIDTH, this many
 * rows. On a 1440p screen that is 160x8 rather than 160x160, 5 KB a cell and
 * 720 KB an output, and the sample is still 1280 pixels spread the whole way
 * across the cell — a mean well inside the 0.05-wide band the ink flips in for
 * anything short of a cell that is horizontally striped on exactly this pitch.
 * Full width and not a centred square for exactly that reason: the width is
 * where the variation a menu cares about lives.
 */
#define BARSCAN_BAND 8

/*
 * What a surface actually reads as once what is behind it shows through.
 *
 * `src` is the luminance read off the node and `scale` the node opacity the
 * compositor multiplies it by — separate arguments because a client's buffer is
 * PREMULTIPLIED and a scene node's opacity is not: the pixels already carry the
 * client's own alpha folded into them, and the node's fade is applied to that
 * result. `alpha` is the coverage the two come to together, which is what
 * decides how much of `back` survives.
 *
 * Mixed in the encoding the GPU mixes in, for the reason syn_lum_over() spells
 * out — a linear mix of luminances is several hundredths out in the midtones,
 * which is the entire width of the band the ink flips in. This is that function
 * with the source pre-scaled rather than weighted, which is the difference
 * between premultiplied pixels and a straight colour.
 */
/* ── Tracing why a column reads what it reads ────────────────────────────────
 *
 * Off unless SYNUI_BARSCAN_DEBUG is set in the compositor's environment, and
 * silent in steady state: a line comes out only when a column's answer CHANGES,
 * plus a heartbeat every BARSCAN_DBG_BEAT scans so a value that is stuck can be
 * told from one nobody is computing. 400 ms x 16 columns is far too much to log
 * unconditionally, and a trace nobody can read is a trace nobody uses.
 *
 * It exists because the failure this chases does not look like a failure: every
 * bail in the read path below is a bare `return`, scan_output() has already
 * cleared the row to -1, and scan_strip() only ever FILLS — so a column that
 * bails reads as "nothing here" and a column that composites wrongly reads as a
 * confident number. Both are silent, and the second one inks the bar backwards.
 * (Measured 2026-08-18: 0.24 published for thirty seconds while the pixels
 * under the bar were 0.058 — dark ink chosen at 4.71:1 over light's 3.62:1.)
 */
#define BARSCAN_DBG_BEAT 25

static bool barscan_dbg(void)
{
    static int on = -1;
    if (on < 0) on = getenv("SYNUI_BARSCAN_DEBUG") != NULL;
    return on == 1;
}

/* Which strip column leaf_lum_run is being asked about, so its own bails can
 * name one. -1 while the grid scan runs, which does not trace. */
static int g_dbg_col = -1;

#define BARSCAN_BAIL(reason, ...) do { \
    if (barscan_dbg() && g_dbg_col >= 0) \
        wlr_log(WLR_INFO, "synui: barscan: col %d declined: " reason, \
                g_dbg_col, ##__VA_ARGS__); \
} while (0)

static double lum_premult_over(double src, double scale, double alpha,
                               double back)
{
    if (alpha >= BARSCAN_SOLID_ALPHA) return src;
    /* Nothing to composite against: solid enough still answers for itself, and
     * anything fainter is a surface we cannot say the colour of. */
    if (!(back >= 0.0))
        return alpha >= BARSCAN_OPAQUE_ALPHA ? src : -1.0;
    if (alpha <= 0.0) return back;

    double e = syn_lum_to_srgb(src) * scale
             + (1.0 - alpha) * syn_lum_to_srgb(back);
    if (e < 0.0) e = 0.0;
    if (e > 1.0) e = 1.0;
    /* Both sides are treated as greys of their luminance, which is exact for
     * the mix: the blend is per-channel and linear in each. */
    return syn_rel_luminance(e, e, e);
}

/* ── Scene-graph geometry ────────────────────────────────── */

/* The layout-space box of a LEAF node, or false for a node with no area this
 * file can reason about (a tree, a shadow, a blur — none of which is a backdrop
 * anything can be read against). */
static bool leaf_box(struct wlr_scene_node *n, struct wlr_box *out)
{
    int lx, ly;
    if (!wlr_scene_node_coords(n, &lx, &ly)) return false;

    switch (n->type) {
    case WLR_SCENE_NODE_RECT: {
        struct wlr_scene_rect *r = wl_container_of(n, r, node);
        *out = (struct wlr_box){ lx, ly, r->width, r->height };
        return true;
    }
    case WLR_SCENE_NODE_BUFFER: {
        struct wlr_scene_buffer *b = wl_container_of(n, b, node);
        if (!b->buffer) return false;
        *out = (struct wlr_box){ lx, ly, b->dst_width, b->dst_height };
        return true;
    }
    default:
        return false;
    }
}

/*
 * The first leaf at (lx,ly) walking this subtree top-first, or NULL.
 *
 * Deliberately NOT wlr_scene_node_at(): that one starts at the scene root and
 * would find the bar every time, and disabling the bar's node to get it out of
 * the way would damage the strip and repaint it twice a second forever. This
 * takes the subtree to look in as an argument, which is what lets the caller
 * cut the search off at layer_tree[TOP].
 *
 * Children are stored bottom-first, so the reverse walk is the top-first one —
 * the same iteration order wlr_scene_node_at() uses, for the same reason.
 */
static struct wlr_scene_node *leaf_at(struct wlr_scene_node *n, int lx, int ly)
{
    if (!n->enabled) return NULL;

    if (n->type == WLR_SCENE_NODE_TREE) {
        struct wlr_scene_tree *tree = wl_container_of(n, tree, node);
        struct wlr_scene_node *child;
        wl_list_for_each_reverse(child, &tree->children, link) {
            struct wlr_scene_node *hit = leaf_at(child, lx, ly);
            if (hit) return hit;
        }
        return NULL;
    }

    struct wlr_box box;
    if (!leaf_box(n, &box)) return NULL;
    return wlr_box_contains_point(&box, lx, ly) ? n : NULL;
}

/*
 * The topmost leaf UNDER THE SHELL at (lx,ly).
 *
 * See the header comment: the root's children are walked top-first and nothing
 * is considered until the walk has passed layer_tree[TOP], which is the tree the
 * bar itself lives in.
 *
 * ⚠ THAT CUT IS WHAT KEEPS THE GRID OUT OF ITS OWN ANSWER, and it is the reason
 * the grid needed no cut of its own. quickshell's popups — the start menu, the
 * bar's menus, the mixer, the OSD — are layer surfaces on TOP, and synui's own
 * panels are trees created after layer_tree[OVERLAY] and therefore above it
 * (synui_main.c says so where it makes them). So every surface that asks this
 * question is on the far side of the cut from the answer, and a menu can never
 * measure itself, its neighbour, or the bar.
 *
 * ⚠ EXCEPT THE ONE CLASS OF SURFACE BELOW THE CUT THAT IS ALSO OURS: the
 * desktop widgets, on BOTTOM. They were kept out of the answer by accident —
 * they are see-through, and the see-through case used to decline — so the
 * moment a fade became something this file COMPOSITES rather than refuses, a
 * post-it asking which ink reads under it would have been reading its own card.
 * The layer is pruned for that reason, and it costs nothing else: what is
 * behind a widget is the wallpaper, which is what -1 hands the consumer
 * anyway, and every window is above them.
 *
 * BACKGROUND is deliberately NOT pruned with it. That layer is where a live
 * wallpaper (linux-wallpaperengine) draws, and it is genuinely the backdrop —
 * the one wallpaper.c cannot measure, because it never painted it.
 */
static struct wlr_scene_node *leaf_under_shell(syn_server_t *s, int lx, int ly)
{
    struct wlr_scene_node *bar_tree =
        &s->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_TOP]->node;

    /*
     * ⚠ THE WALLPAPER IS PRUNED, AND THAT IS WHAT MAKES -1 MEAN ANYTHING.
     *
     * The wallpaper is a scene node like any other, so a walk that did not stop
     * short of it would ALWAYS find something and no column would ever read -1.
     * That is not merely redundant with wallpaper.c, it is worse than it: the
     * cairo-side measurement knows the two cases that paint no picture — `none`
     * leaves the solid bg_rect, the rain draws its own GPU buffer and reports
     * itself — and a scene walk would read the first as a rect it happened to
     * find and the second as whatever frame it caught.
     *
     * Pruning them costs nothing and buys the common case outright: with the
     * bar over bare wallpaper every column stops at "nothing here", and the scan
     * does not read a single pixel back off the GPU.
     */
    struct wlr_scene_node *wp_tree =
        s->wallpaper_tree ? &s->wallpaper_tree->node : NULL;
    struct wlr_scene_node *wp_rect = s->bg_rect ? &s->bg_rect->node : NULL;

    /* Ours, and below the cut: see the note above on why the widgets are not
     * part of the answer they ask. */
    struct wlr_scene_node *widget_tree =
        &s->layer_tree[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]->node;

    bool below = false;
    struct wlr_scene_node *child;
    wl_list_for_each_reverse(child, &s->scene->tree.children, link) {
        if (!below) {
            /* Everything down to and including the bar's own layer is over the
             * top of the question being asked. */
            if (child == bar_tree) below = true;
            continue;
        }
        if (child == wp_tree || child == wp_rect || child == widget_tree)
            continue;
        struct wlr_scene_node *hit = leaf_at(child, lx, ly);
        if (hit) return hit;
    }
    return NULL;
}

/* ── Reading a leaf's luminance ──────────────────────────── */

/*
 * Mean relative luminance of `n` over `want` (layout coords), split into `cells`
 * equal columns and written into `out[0..cells)`. Every cell is -1 if this node
 * cannot answer — its pixels cannot be read, or it is see-through over a
 * wallpaper that could not be measured either.
 *
 * `back[0..cells)` is the wallpaper's own answer for those same cells, which is
 * what a see-through node is composited against. -1 there is "the wallpaper
 * could not be measured", exactly as it is in wp_lum_grid.
 *
 * ⚠ `cells` IS AN OPTIMISATION AND NOTHING ELSE: one call with cells=4 and four
 * calls over the quarters produce the same four numbers. It exists because the
 * grid asks about a whole row of cells at once and the same window usually
 * covers a run of them — a maximized browser is nine readbacks an output that
 * way and a hundred and forty-four the other, and the readback is a stall on
 * the main loop rather than a cost per byte. The strip calls it with cells=1,
 * which is the shape this function had before the grid existed.
 *
 * The two node types are genuinely different questions. A RECT is a colour
 * synui chose and already knows exactly; a BUFFER is a client's pixels and has
 * to come off the GPU. Decorations are rects, which is why a window dragged
 * under the bar by its titlebar costs no readback at all.
 */
static void leaf_lum_run(syn_server_t *s, struct wlr_scene_node *n,
                         const struct wlr_box *want, int cells,
                         const double *back, double *out)
{
    for (int i = 0; i < cells; i++) out[i] = -1.0;

    struct wlr_box box;
    if (!leaf_box(n, &box)) { BARSCAN_BAIL("node has no box"); return; }

    struct wlr_box hit;
    if (!wlr_box_intersection(&hit, &box, want)) { BARSCAN_BAIL("node does not meet the strip"); return; }
    if (hit.width <= 0 || hit.height <= 0) { BARSCAN_BAIL("empty intersection"); return; }

    if (n->type == WLR_SCENE_NODE_RECT) {
        struct wlr_scene_rect *r = wl_container_of(n, r, node);
        /* scene rect colours are straight sRGB 0..1, so they go through the
         * same linearisation the pixel path does — just without the 0..255
         * lookup table, which is what syn_srgb_lut is. Straight and not
         * premultiplied, which is why this is syn_lum_over() and the buffer
         * path below is not: deco.c folds a fading window's opacity into
         * color[3] and leaves the colour alone (a rect has no set_opacity). */
        double lr = syn_srgb_lut((int)lround(r->color[0] * 255.0));
        double lg = syn_srgb_lut((int)lround(r->color[1] * 255.0));
        double lb = syn_srgb_lut((int)lround(r->color[2] * 255.0));
        double lum = 0.2126 * lr + 0.7152 * lg + 0.0722 * lb;
        double a   = r->color[3];
        /* One colour over the whole rect, so every cell of the run gets it —
         * and the cells the rect does not reach get it too, because a run is
         * only ever built out of cells this same node was found at. What each
         * cell does NOT share is the wallpaper behind it. */
        for (int i = 0; i < cells; i++) {
            if (a >= BARSCAN_SOLID_ALPHA)   { out[i] = lum; continue; }
            if (!(back[i] >= 0.0)) {
                out[i] = a >= BARSCAN_OPAQUE_ALPHA ? lum : -1.0;
                continue;
            }
            out[i] = syn_lum_over(lum, a, back[i]);
        }
        return;
    }

    struct wlr_scene_buffer *sb = wl_container_of(n, sb, node);
    if (!sb->buffer) { BARSCAN_BAIL("scene buffer has no buffer"); return; }
    if (sb->dst_width <= 0 || sb->dst_height <= 0) { BARSCAN_BAIL("dst %dx%d", sb->dst_width, sb->dst_height); return; }

    /*
     * ⚠ A ROTATED OR FLIPPED BUFFER IS DECLINED RATHER THAN GUESSED AT.
     * src_box/dst mapping under a transform is not the identity this arithmetic
     * assumes, and a backdrop measured through the wrong axis is worse than no
     * measurement: -1 falls back to the wallpaper, a wrong number inks the bar
     * confidently backwards.
     */
    if (sb->transform != WL_OUTPUT_TRANSFORM_NORMAL) { BARSCAN_BAIL("transform %d", (int)sb->transform); return; }

    /* Layout rect → buffer pixels. dst_width/height is what the node occupies
     * on screen; buffer->width/height is what it is stored at, and a scaled
     * client (or an HiDPI one) makes those differ. */
    double sx = (double)sb->buffer->width  / (double)sb->dst_width;
    double sy = (double)sb->buffer->height / (double)sb->dst_height;

    struct wlr_box src = {
        .x      = (int)floor((hit.x - box.x) * sx),
        .y      = (int)floor((hit.y - box.y) * sy),
        .width  = (int)ceil(hit.width  * sx),
        .height = (int)ceil(hit.height * sy),
    };
    if (src.x < 0) src.x = 0;
    if (src.y < 0) src.y = 0;
    if (src.x + src.width  > sb->buffer->width)
        src.width  = sb->buffer->width  - src.x;
    if (src.y + src.height > sb->buffer->height)
        src.height = sb->buffer->height - src.y;
    if (src.width <= 0 || src.height <= 0) { BARSCAN_BAIL("src box empty after clamp"); return; }

    /*
     * ⚠ A CLIENT'S BUFFER IS NOT ONE wlr_texture_from_buffer() CAN IMPORT, and
     * asking it to is a silent -1 on every window on screen.
     *
     * What the scene holds for a surface node is a wlr_client_buffer — a
     * wrapper wlroots made when the client attached, which ALREADY carries the
     * texture the renderer draws from. Handed to wlr_texture_from_buffer() it
     * returns NULL, because the wrapper is not itself importable; the accessor
     * for it is wlr_client_buffer_get(). Measured: 320 declined reads and a bar
     * strip of -1 across the board, which reads exactly like "no window here".
     *
     * That texture is BORROWED and must not be destroyed — hence `own`. The
     * import path stays for the buffers that are not a client's: synui's own
     * cairo scene buffers, where wlr_texture_from_buffer is the right call and
     * the texture is ours to free.
     */
    struct wlr_texture *tex = NULL;
    bool own = false;
    struct wlr_client_buffer *cb = wlr_client_buffer_get(sb->buffer);
    if (cb) tex = cb->texture;
    if (!tex) {
        tex = wlr_texture_from_buffer(s->renderer, sb->buffer);
        own = tex != NULL;
    }
    if (!tex) { BARSCAN_BAIL("no texture for this buffer"); return; }

    uint32_t fmt = wlr_texture_preferred_read_format(tex);
    int bpp;
    /* Which byte is which, and where the ALPHA is — `ai` is -1 for the two
     * formats that have none, which is the compact way of saying "this buffer
     * is opaque by construction". Only the 32-bit packed orders are handled;
     * anything else declines, for the same reason the transform above does. */
    int ri, gi, bi, ai;
    switch (fmt) {
    case DRM_FORMAT_XRGB8888: bpp = 4; ri = 2; gi = 1; bi = 0; ai = -1; break;
    case DRM_FORMAT_ARGB8888: bpp = 4; ri = 2; gi = 1; bi = 0; ai =  3; break;
    case DRM_FORMAT_XBGR8888: bpp = 4; ri = 0; gi = 1; bi = 2; ai = -1; break;
    case DRM_FORMAT_ABGR8888: bpp = 4; ri = 0; gi = 1; bi = 2; ai =  3; break;
    default:
        BARSCAN_BAIL("unhandled read format 0x%08x", fmt);
        if (own) wlr_texture_destroy(tex);
        return;
    }

    size_t stride = (size_t)src.width * (size_t)bpp;
    unsigned char *data = malloc(stride * (size_t)src.height);
    if (!data) {
        if (own) wlr_texture_destroy(tex);
        return;
    }

    struct wlr_texture_read_pixels_options opts = {
        .data    = data,
        .format  = fmt,
        .stride  = (uint32_t)stride,
        .dst_x   = 0,
        .dst_y   = 0,
        .src_box = src,
    };
    bool ok = wlr_texture_read_pixels(tex, &opts);
    if (own) wlr_texture_destroy(tex);
    if (!ok) {
        BARSCAN_BAIL("read_pixels refused %dx%d at %d,%d",
                     src.width, src.height, src.x, src.y);
        free(data);
        return;
    }

    /*
     * One pass, `cells` accumulators. A pixel's cell is its column scaled into
     * the run, which is the same arithmetic the caller used to build the run in
     * the first place — so a cell here is the cell the caller will store.
     *
     * ⚠ THE ALPHA IS ACCUMULATED PER CELL, and it used to be one mean over the
     * whole read. That was right while a see-through surface was DECLINED:
     * half-transparency was a yes/no about the surface, and splitting it per
     * cell would have kept the opaque half of a window and dropped the rest, so
     * a menu straddling the two would have folded a real luminance together
     * with a -1 and taken the veto without knowing why. There is no veto now —
     * every cell gets a composite — and the coverage a cell is composited at is
     * a fact about that cell. It is what makes a window with a rounded corner,
     * or a terminal with an opaque panel in it, read as what it looks like.
     */
    double sum[SYN_LUM_COLS]  = { 0 };
    long   n_px[SYN_LUM_COLS] = { 0 };
    double a_sum[SYN_LUM_COLS] = { 0 };
    if (cells > SYN_LUM_COLS) cells = SYN_LUM_COLS;   /* never, but bounded */

    for (int y = 0; y < src.height; y += BARSCAN_STEP) {
        const unsigned char *row = data + (size_t)y * stride;
        for (int x = 0; x < src.width; x += BARSCAN_STEP) {
            const unsigned char *px = row + (size_t)x * bpp;
            int c = (int)((long)x * cells / src.width);
            if (c < 0) c = 0;
            if (c >= cells) c = cells - 1;
            sum[c] += 0.2126 * syn_srgb_lut(px[ri]) +
                      0.7152 * syn_srgb_lut(px[gi]) +
                      0.0722 * syn_srgb_lut(px[bi]);
            n_px[c]++;
            /* No alpha channel is opaque by construction — see `ai`. */
            a_sum[c] += ai >= 0 ? px[ai] / 255.0 : 1.0;
        }
    }
    free(data);

    if (barscan_dbg() && g_dbg_col >= 0)
        wlr_log(WLR_INFO,
                "synui: barscan: col %d read buf %dx%d dst %dx%d scale %.3fx%.3f "
                "src %d,%d %dx%d node %d,%d %dx%d opacity %.2f",
                g_dbg_col, sb->buffer->width, sb->buffer->height,
                sb->dst_width, sb->dst_height, sx, sy,
                src.x, src.y, src.width, src.height,
                box.x, box.y, box.width, box.height, sb->opacity);

    for (int i = 0; i < cells; i++) {
        if (!n_px[i]) continue;
        double lum = sum[i] / (double)n_px[i];
        /* The client's own coverage and synui's fade of it are two different
         * numbers and multiply: the fade is applied to a buffer that already
         * carries the client's alpha. */
        double a   = a_sum[i] / (double)n_px[i] * sb->opacity;
        out[i] = lum_premult_over(lum, sb->opacity, a, back[i]);
    }
}

/* The single-cell form, which is what the bar strip asks. */
static double leaf_lum(syn_server_t *s, struct wlr_scene_node *n,
                       const struct wlr_box *want, double back)
{
    double one = -1.0;
    leaf_lum_run(s, n, want, 1, &back, &one);
    return one;
}

/* ── The scan ────────────────────────────────────────────── */

/*
 * The WALLPAPER's own answer for one cell — what a see-through node in that
 * cell is composited against.
 *
 * This is the same number the consumer falls back to when a cell reads -1, and
 * that is the point: a window at alpha 0 composites to exactly the value the
 * cell would have published without it, so the two paths meet rather than
 * disagreeing at the edges. -1 means the wallpaper could not be measured (a
 * video wallpaper, or one that has not been painted yet).
 */
static double wp_cell(const syn_output_t *o, int r, int c)
{
    if (r < 0 || r >= SYN_LUM_ROWS || c < 0 || c >= SYN_LUM_COLS) return -1.0;
    /* ⚠ NOT o->wp_lum_grid DIRECTLY — under a live wallpaper the painted buffer
     * that grid describes is covered edge to edge by the engine's own surface,
     * and a see-through window composited against it is composited against a
     * picture nobody can see. wallpaper_lum_grid() is where that substitution
     * is made, once. */
    return wallpaper_lum_grid(o)[r * SYN_LUM_COLS + c];
}

/*
 * Fill one output's bar_strip_lum[] — the BAR's row.
 *
 * The strip is the same SYN_BAR_STRIP_LOGICAL rows wallpaper.c measures, on the
 * same edge — this has to describe the SAME region the wallpaper answer
 * describes, or the fallback would be splicing two different questions together
 * column by column.
 *
 * One probe point per column, at the strip's vertical middle. Not a fold over
 * the column's whole area: what we are looking for is the window under the bar,
 * windows are rectangles, and a rectangle that covers the middle of a 34px strip
 * covers essentially all of it. The pixels ARE averaged over the column's full
 * width once the covering node is known — it is the search that samples one
 * point, not the measurement.
 */
static void scan_strip(syn_server_t *s, syn_output_t *o,
                       const struct wlr_box *ob)
{
    /* What the trace last reported, per column, so it can stay quiet while
     * nothing moves. Debug-only, one output's worth: with the trace off it is
     * never read, and with it on the interesting machine has one screen whose
     * bar is wrong. */
    static double dbg_prev[BARSCAN_COLS] = { [0 ... BARSCAN_COLS - 1] = -2.0 };
    static int    dbg_beat;

    int strip = SYN_BAR_STRIP_LOGICAL;
    if (strip > ob->height) strip = ob->height;
    bool bottom = s->config.bar_edge == SYN_BAR_EDGE_BOTTOM;
    int top = bottom ? ob->y + ob->height - strip : ob->y;

    /* The wallpaper row the strip lies in — its own edge, so a see-through
     * window under a bottom bar is composited over the bottom of the picture
     * and not the top of it. Coarser than wp_top_lum, which measures the strip
     * itself, and per COLUMN, which wp_top_lum is not: the same cell the
     * consumer falls back to for this column. */
    int wp_row = bottom ? SYN_LUM_ROWS - 1 : 0;

    for (int c = 0; c < BARSCAN_COLS; c++) {
        int x0 = ob->x + (int)((int64_t)ob->width * c       / BARSCAN_COLS);
        int x1 = ob->x + (int)((int64_t)ob->width * (c + 1) / BARSCAN_COLS);
        if (x1 <= x0) continue;

        struct wlr_scene_node *n =
            leaf_under_shell(s, (x0 + x1) / 2, top + strip / 2);
        if (!n) {
            /* nothing of ours here — the wallpaper answers */
            if (barscan_dbg() && dbg_prev[c] >= -0.5) {
                wlr_log(WLR_INFO, "synui: barscan: col %d -> nothing over the "
                                  "wallpaper (was %.3f)", c, dbg_prev[c]);
                dbg_prev[c] = -1.0;
            }
            continue;
        }

        struct wlr_box want = { x0, top, x1 - x0, strip };
        double back = wp_cell(o, wp_row, c);
        /* The strip's own, folded — live-aware for the same reason wp_cell is. */
        if (!(back >= 0.0)) back = wallpaper_strip_lum(o);

        g_dbg_col = barscan_dbg() ? c : -1;
        double lum = leaf_lum(s, n, &want, back);
        g_dbg_col = -1;

        if (lum >= 0.0) o->bar_strip_lum[c] = lum;

        /* On a CHANGE, and on a heartbeat. A column stuck on a wrong number is
         * the failure being chased, and a trace that only fires on change would
         * go silent for exactly the state worth seeing. */
        if (barscan_dbg()) {
            bool moved = !(fabs(lum - dbg_prev[c]) < 0.005);
            if (moved || dbg_beat == 0)
                wlr_log(WLR_INFO,
                        "synui: barscan: col %d %s %.3f (backdrop %.3f, node "
                        "type %d)%s", c, moved ? "->" : "still", lum, back,
                        (int)n->type, moved ? "" : " [heartbeat]");
            if (moved) dbg_prev[c] = lum;
        }
    }

    if (barscan_dbg() && ++dbg_beat >= BARSCAN_DBG_BEAT) dbg_beat = 0;
}

/*
 * …and one output's scene_lum[] — the same question over the WHOLE screen, for
 * every surface whose position is not a constant.
 *
 * ⚠ THE ROW IS SCANNED AS RUNS OF ONE NODE, NOT AS SIXTEEN INDEPENDENT CELLS,
 * and that is the difference between this being affordable and not. The probe
 * is a tree walk and is cheap; the readback is a stall on the main loop, and a
 * maximized window covers all sixteen columns of a row with the same node. So
 * the sixteen probes stand, the run of equal nodes is coalesced, and one read
 * fills the whole run — nine readbacks an output for a full-screen window
 * rather than a hundred and forty-four.
 *
 * A run is only ever ONE node, so the fold is exact rather than an
 * approximation: leaf_lum_run splits the pixels it read at the same column
 * boundaries this loop used to find them.
 */
static void scan_grid(syn_server_t *s, syn_output_t *o,
                      const struct wlr_box *ob)
{
    struct wlr_scene_node *hit[SYN_LUM_COLS];
    int edge[SYN_LUM_COLS + 1];

    for (int c = 0; c <= SYN_LUM_COLS; c++)
        edge[c] = ob->x + (int)((int64_t)ob->width * c / SYN_LUM_COLS);

    for (int r = 0; r < SYN_LUM_ROWS; r++) {
        int y0 = ob->y + (int)((int64_t)ob->height * r       / SYN_LUM_ROWS);
        int y1 = ob->y + (int)((int64_t)ob->height * (r + 1) / SYN_LUM_ROWS);
        if (y1 <= y0) continue;

        /* The band actually read: BARSCAN_BAND rows about the cell's middle,
         * clamped to a cell too short to hold one. See BARSCAN_BAND. */
        int mid  = (y0 + y1) / 2;
        int band = y1 - y0 < BARSCAN_BAND ? y1 - y0 : BARSCAN_BAND;
        int by   = mid - band / 2;
        if (by < y0) by = y0;
        if (by + band > y1) by = y1 - band;

        for (int c = 0; c < SYN_LUM_COLS; c++)
            hit[c] = edge[c + 1] > edge[c]
                   ? leaf_under_shell(s, (edge[c] + edge[c + 1]) / 2, mid)
                   : NULL;

        for (int c = 0; c < SYN_LUM_COLS; ) {
            if (!hit[c]) { c++; continue; }   /* the wallpaper answers here */

            int end = c + 1;
            while (end < SYN_LUM_COLS && hit[end] == hit[c]) end++;

            struct wlr_box want = { edge[c], by, edge[end] - edge[c], band };
            double lum[SYN_LUM_COLS];
            double back[SYN_LUM_COLS];
            for (int i = c; i < end; i++) back[i - c] = wp_cell(o, r, i);
            leaf_lum_run(s, hit[c], &want, end - c, back, lum);
            for (int i = c; i < end; i++)
                if (lum[i - c] >= 0.0) o->scene_lum[r * SYN_LUM_COLS + i] = lum[i - c];

            c = end;
        }
    }
}

static void scan_output(syn_server_t *s, syn_output_t *o)
{
    /* Cleared first and unconditionally, which is what makes the switch and a
     * closing window the same code path: everything below only ever FILLS. */
    for (int i = 0; i < BARSCAN_COLS; i++)  o->bar_strip_lum[i] = -1.0;
    for (int i = 0; i < SYN_LUM_CELLS; i++) o->scene_lum[i]     = -1.0;

    if (!scene_ink_on(&s->config)) return;
    if (!o->wlr_output || !o->wlr_output->enabled) return;

    struct wlr_box ob;
    wlr_output_layout_get_box(s->output_layout, o->wlr_output, &ob);
    if (ob.width <= 0 || ob.height <= 0) return;

    scan_strip(s, o, &ob);
    scan_grid(s, o, &ob);
}

void barscan_scan(syn_server_t *s)
{
    if (!s || !s->scene) return;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        scan_output(s, o);
    /* backdrop_export() writes only on a change, so calling it every tick costs
     * a string compare on a quiet desktop and nothing else. */
    wallpaper_backdrop_republish(s);
}

static int barscan_tick(void *data)
{
    syn_server_t *s = data;
    barscan_scan(s);
    if (s->barscan_timer)
        wl_event_source_timer_update(s->barscan_timer, BARSCAN_INTERVAL_MS);
    return 0;
}

void barscan_init(syn_server_t *s)
{
    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->barscan_timer = wl_event_loop_add_timer(loop, barscan_tick, s);
    if (s->barscan_timer)
        wl_event_source_timer_update(s->barscan_timer, BARSCAN_INTERVAL_MS);
    else
        wlr_log(WLR_ERROR, "synui: barscan: no timer; the bar will ink itself "
                           "from the wallpaper alone");
}

void barscan_finish(syn_server_t *s)
{
    if (!s->barscan_timer) return;
    wl_event_source_remove(s->barscan_timer);
    s->barscan_timer = NULL;
}
