/*
 * barscan.c — what is ACTUALLY under the bar.
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

/* Below this the node is not the backdrop, it is something in front of it, and
 * we keep descending. Not 1.0: clients round their own opacity, and a window at
 * "fully opaque" routinely arrives as 0.996. */
#define BARSCAN_OPAQUE_ALPHA 0.9

/* Every Nth pixel, both axes, when averaging a readback. The strip is 34
 * logical rows of a screen that is 2560 wide; a mean over every pixel and a
 * mean over every 4th differ in the third decimal, and the ink flips over a
 * band 0.05 wide. */
#define BARSCAN_STEP 4

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
 * The topmost leaf under the BAR at (lx,ly).
 *
 * See the header comment: the root's children are walked top-first and nothing
 * is considered until the walk has passed layer_tree[TOP], which is the tree the
 * bar itself lives in.
 */
static struct wlr_scene_node *leaf_under_bar(syn_server_t *s, int lx, int ly)
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

    bool below = false;
    struct wlr_scene_node *child;
    wl_list_for_each_reverse(child, &s->scene->tree.children, link) {
        if (!below) {
            /* Everything down to and including the bar's own layer is over the
             * top of the question being asked. */
            if (child == bar_tree) below = true;
            continue;
        }
        if (child == wp_tree || child == wp_rect) continue;
        struct wlr_scene_node *hit = leaf_at(child, lx, ly);
        if (hit) return hit;
    }
    return NULL;
}

/* ── Reading a leaf's luminance ──────────────────────────── */

/* Mean relative luminance of `n` over `want` (layout coords), or -1 if this
 * node cannot answer — it is see-through, or its pixels cannot be read.
 *
 * The two node types are genuinely different questions. A RECT is a colour
 * synui chose and already knows exactly; a BUFFER is a client's pixels and has
 * to come off the GPU. Decorations are rects, which is why a window dragged
 * under the bar by its titlebar costs no readback at all. */
static double leaf_lum(syn_server_t *s, struct wlr_scene_node *n,
                       const struct wlr_box *want)
{
    struct wlr_box box;
    if (!leaf_box(n, &box)) return -1.0;

    struct wlr_box hit;
    if (!wlr_box_intersection(&hit, &box, want)) return -1.0;
    if (hit.width <= 0 || hit.height <= 0) return -1.0;

    if (n->type == WLR_SCENE_NODE_RECT) {
        struct wlr_scene_rect *r = wl_container_of(n, r, node);
        if (r->color[3] < BARSCAN_OPAQUE_ALPHA) return -1.0;
        /* scene rect colours are straight sRGB 0..1, so they go through the
         * same linearisation the pixel path does — just without the 0..255
         * lookup table, which is what syn_srgb_lut is. */
        double lr = syn_srgb_lut((int)lround(r->color[0] * 255.0));
        double lg = syn_srgb_lut((int)lround(r->color[1] * 255.0));
        double lb = syn_srgb_lut((int)lround(r->color[2] * 255.0));
        return 0.2126 * lr + 0.7152 * lg + 0.0722 * lb;
    }

    struct wlr_scene_buffer *sb = wl_container_of(n, sb, node);
    if (!sb->buffer) return -1.0;
    if (sb->opacity < BARSCAN_OPAQUE_ALPHA) return -1.0;
    if (sb->dst_width <= 0 || sb->dst_height <= 0) return -1.0;

    /*
     * ⚠ A ROTATED OR FLIPPED BUFFER IS DECLINED RATHER THAN GUESSED AT.
     * src_box/dst mapping under a transform is not the identity this arithmetic
     * assumes, and a backdrop measured through the wrong axis is worse than no
     * measurement: -1 falls back to the wallpaper, a wrong number inks the bar
     * confidently backwards.
     */
    if (sb->transform != WL_OUTPUT_TRANSFORM_NORMAL) return -1.0;

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
    if (src.width <= 0 || src.height <= 0) return -1.0;

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
    if (!tex) return -1.0;

    uint32_t fmt = wlr_texture_preferred_read_format(tex);
    int bpp;
    /* Which byte is which. Only the 32-bit packed orders are handled; anything
     * else declines, for the same reason the transform above does. */
    int ri, gi, bi;
    switch (fmt) {
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ARGB8888: bpp = 4; ri = 2; gi = 1; bi = 0; break;
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_ABGR8888: bpp = 4; ri = 0; gi = 1; bi = 2; break;
    default:
        if (own) wlr_texture_destroy(tex);
        return -1.0;
    }

    size_t stride = (size_t)src.width * (size_t)bpp;
    unsigned char *data = malloc(stride * (size_t)src.height);
    if (!data) {
        if (own) wlr_texture_destroy(tex);
        return -1.0;
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
        free(data);
        return -1.0;
    }

    double sum = 0.0;
    long n_px = 0;
    for (int y = 0; y < src.height; y += BARSCAN_STEP) {
        const unsigned char *row = data + (size_t)y * stride;
        for (int x = 0; x < src.width; x += BARSCAN_STEP) {
            const unsigned char *px = row + (size_t)x * bpp;
            sum += 0.2126 * syn_srgb_lut(px[ri]) +
                   0.7152 * syn_srgb_lut(px[gi]) +
                   0.0722 * syn_srgb_lut(px[bi]);
            n_px++;
        }
    }
    free(data);
    return n_px ? sum / (double)n_px : -1.0;
}

/* ── The scan ────────────────────────────────────────────── */

/*
 * Fill one output's bar_strip_lum[].
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
static void scan_output(syn_server_t *s, syn_output_t *o)
{
    for (int i = 0; i < BARSCAN_COLS; i++) o->bar_strip_lum[i] = -1.0;

    if (!o->wlr_output || !o->wlr_output->enabled) return;

    struct wlr_box ob;
    wlr_output_layout_get_box(s->output_layout, o->wlr_output, &ob);
    if (ob.width <= 0 || ob.height <= 0) return;

    int strip = SYN_BAR_STRIP_LOGICAL;
    if (strip > ob.height) strip = ob.height;
    int top = (s->config.bar_edge == SYN_BAR_EDGE_BOTTOM)
            ? ob.y + ob.height - strip : ob.y;

    for (int c = 0; c < BARSCAN_COLS; c++) {
        int x0 = ob.x + (int)((int64_t)ob.width * c       / BARSCAN_COLS);
        int x1 = ob.x + (int)((int64_t)ob.width * (c + 1) / BARSCAN_COLS);
        if (x1 <= x0) continue;

        struct wlr_scene_node *n =
            leaf_under_bar(s, (x0 + x1) / 2, top + strip / 2);
        if (!n) continue;   /* nothing of ours here — the wallpaper answers */

        struct wlr_box want = { x0, top, x1 - x0, strip };
        double lum = leaf_lum(s, n, &want);
        if (lum >= 0.0) o->bar_strip_lum[c] = lum;
    }
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
