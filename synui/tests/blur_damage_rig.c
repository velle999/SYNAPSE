/*
 * blur_damage_rig.c — does scenefx's own blur damage handling leave a stale
 * pixel behind?
 *
 * synui used to damage the WHOLE output on every frame while blur was on. That
 * was the pkgrel-140 answer (scenefx 0.4) to a wallpaper flash on a click in
 * Dolphin, a glimpse of the window behind a game, and Steam turning opaque on
 * mouse-move: blur's edge-artifact step copied pixels forward out of a rotating
 * swapchain buffer. It cost a full-output blur on every frame the cat or the
 * visualiser asked for — 57-80% of a core on an idle desktop (2026-10-15).
 *
 * scenefx 0.5 does its own blur-aware damage (apply_blur_region in
 * wlr_scene.c): it grows the damage by the blur's sample size wherever it meets
 * a blur node and saves/restores a padding ring around it. This rig asks
 * whether that is enough, with nothing but pixels as the answer.
 *
 * TWO headless outputs look at the same scene from the same position:
 *
 *   A  rendered the way scenefx renders by default: partial damage, its own
 *      blur expansion and padding copy, committed only when the scene says a
 *      frame is needed (as synui's scene_commit_colour does);
 *   B  repainted whole on every frame — the reference picture.
 *
 * After each frame both committed buffers are read back and compared pixel for
 * pixel. The scene is synui's glass, built the way synui builds it: a masked
 * blur node under a translucent window (anim.c), a halo ring with a clipped
 * hole (deco.c), a blurred panel rect (syn_rect_backdrop_blur), and moving
 * things under and around all three — the cat walking, a window being dragged
 * behind the glass, a client repainting a corner of itself, a fullscreen game
 * opening over everything and closing again.
 *
 *   --stale   negative control: swap the wallpaper with NO damage at frame 40.
 *             A must then disagree with B, or the rig cannot see staleness.
 *
 * Exit: 0 = every frame identical (with --stale: some frame differed),
 * 1 = otherwise, or a render error; 77 = no renderer.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#define WLR_USE_UNSTABLE

#include <drm_fourcc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>

/* ── a CPU-side buffer the renderer can upload ─────────────────────────── */
struct mem_buffer {
    struct wlr_buffer base;
    uint32_t *px;          /* premultiplied ARGB8888 */
};

static void mb_destroy(struct wlr_buffer *b)
{
    struct mem_buffer *m = wl_container_of(b, m, base);
    wlr_buffer_finish(b);
    free(m->px);
    free(m);
}

static bool mb_begin(struct wlr_buffer *b, uint32_t flags, void **data,
                     uint32_t *format, size_t *stride)
{
    (void)flags;
    struct mem_buffer *m = wl_container_of(b, m, base);
    *data = m->px;
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)b->width * 4;
    return true;
}

static void mb_end(struct wlr_buffer *b) { (void)b; }

static const struct wlr_buffer_impl mb_impl = {
    .destroy = mb_destroy,
    .begin_data_ptr_access = mb_begin,
    .end_data_ptr_access = mb_end,
};

static struct mem_buffer *mb_new(int w, int h)
{
    struct mem_buffer *m = calloc(1, sizeof(*m));
    m->px = calloc((size_t)w * h, 4);
    wlr_buffer_init(&m->base, &mb_impl, w, h);
    return m;
}

static uint32_t argb(unsigned a, unsigned r, unsigned g, unsigned b)
{
    /* premultiplied */
    r = r * a / 255; g = g * a / 255; b = b * a / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Busy, high-frequency content: a blur over a flat colour hides everything. */
static void paint_pattern(struct mem_buffer *m, unsigned seed)
{
    int w = m->base.width, h = m->base.height;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned v = (unsigned)(x * 7 + y * 13) ^ (seed * 2654435761u);
            unsigned r = ((x / 9 + y / 7 + seed) & 1) ? 230 : 40;
            unsigned g = (v >> 3) & 0xff;
            unsigned b = (unsigned)(x * 255 / (w ? w : 1));
            m->px[(size_t)y * w + x] = argb(255, r, g, b);
        }
}

/* A translucent window body with opaque "text" rows, like a glass client. */
static void paint_glass_window(struct mem_buffer *m, unsigned seed)
{
    int w = m->base.width, h = m->base.height;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            bool text = (y % 18) < 9 && x > 20 && x < w - 20 &&
                        ((x / 6 + y / 18 + seed) % 5) != 0;
            m->px[(size_t)y * w + x] = text ? argb(255, 20, 20, 30)
                                            : argb(140, 235, 240, 250);
        }
}

/* ── the rig ───────────────────────────────────────────────────────────── */
struct rig {
    struct wlr_renderer *renderer;
    struct wlr_scene *scene;
    struct wlr_output *out[2];
    struct wlr_scene_output *so[2];
    uint32_t *cap[2];
    struct wlr_buffer *seen[8];   /* A's distinct swapchain buffers */
    int nseen;
    /* What a screen still holds after a commit: the front buffer, and on a
     * triple-buffered one a queued buffer too. Headless frees a buffer the
     * moment it is committed, so without this the swapchain hands the SAME
     * buffer back every frame and no stale region can ever exist. */
    struct wlr_buffer *held[2][4];
    int hold;
    int w, h;                     /* buffer size */
};

static bool output_enable(struct wlr_output *o, struct wlr_allocator *alloc,
                          struct wlr_renderer *r, int transform, float scale)
{
    if (!wlr_output_init_render(o, alloc, r)) return false;
    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_enabled(&st, true);
    wlr_output_state_set_transform(&st, transform);
    wlr_output_state_set_scale(&st, scale);
    bool ok = wlr_output_commit_state(o, &st);
    wlr_output_state_finish(&st);
    return ok;
}

/* Render one output and read back what it committed. Returns false only on a
 * real failure; "no frame needed" leaves the previous capture in place, which
 * is exactly what stays on that screen. */
static bool frame_one(struct rig *rig, int i, bool whole)
{
    struct wlr_scene_output *so = rig->so[i];
    if (whole)
        wlr_damage_ring_add_whole(&so->damage_ring);
    else if (!wlr_scene_output_needs_frame(so))
        return true;

    struct wlr_output_state st;
    wlr_output_state_init(&st);
    if (!wlr_scene_output_build_state(so, &st, NULL)) {
        wlr_output_state_finish(&st);
        return false;
    }
    if (st.buffer && i == 0) {
        int k = 0;
        while (k < rig->nseen && rig->seen[k] != st.buffer) k++;
        if (k == rig->nseen && k < 8) rig->seen[rig->nseen++] = st.buffer;
    }
    if (st.buffer) {
        struct wlr_texture *t = wlr_texture_from_buffer(rig->renderer, st.buffer);
        if (!t) { wlr_output_state_finish(&st); return false; }
        bool ok = wlr_texture_read_pixels(t, &(struct wlr_texture_read_pixels_options){
            .data = rig->cap[i], .format = DRM_FORMAT_ABGR8888,
            .stride = (uint32_t)rig->w * 4,
        });
        wlr_texture_destroy(t);
        if (!ok) { wlr_output_state_finish(&st); return false; }
    }
    bool ok = wlr_output_commit_state(rig->out[i], &st);
    if (ok && st.buffer && rig->hold > 0) {
        struct wlr_buffer **h = rig->held[i];
        if (h[rig->hold - 1]) wlr_buffer_unlock(h[rig->hold - 1]);
        memmove(h + 1, h, sizeof(*h) * (size_t)(rig->hold - 1));
        h[0] = wlr_buffer_lock(st.buffer);
    }
    wlr_output_state_finish(&st);
    return ok;
}

struct diff { long n; int maxd, x1, y1, x2, y2; };

static struct diff compare(const struct rig *rig, int tol)
{
    struct diff d = { 0, 0, rig->w, rig->h, -1, -1 };
    const uint8_t *a = (const uint8_t *)rig->cap[0];
    const uint8_t *b = (const uint8_t *)rig->cap[1];
    for (int y = 0; y < rig->h; y++)
        for (int x = 0; x < rig->w; x++) {
            size_t o = ((size_t)y * rig->w + x) * 4;
            int m = 0;
            for (int c = 0; c < 3; c++) {      /* alpha is X on the output */
                int dd = abs((int)a[o + c] - (int)b[o + c]);
                if (dd > m) m = dd;
            }
            if (m > d.maxd) d.maxd = m;
            if (m > tol) {
                d.n++;
                if (x < d.x1) d.x1 = x;
                if (y < d.y1) d.y1 = y;
                if (x > d.x2) d.x2 = x;
                if (y > d.y2) d.y2 = y;
            }
        }
    return d;
}

int main(int argc, char **argv)
{
    int W = 1280, H = 720, frames = 240, transform = WL_OUTPUT_TRANSFORM_NORMAL;
    int tol = 0, passes = 2, radius = 5, halo = 14, hold = 2;
    float scale = 1.0f, noise = 0.02f;
    bool stale = false, verbose = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--stale")) stale = true;
        else if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) { W = atoi(argv[++i]); H = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--transform") && i + 1 < argc) transform = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--tol") && i + 1 < argc) tol = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--passes") && i + 1 < argc) passes = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--radius") && i + 1 < argc) radius = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--halo") && i + 1 < argc) halo = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--noise") && i + 1 < argc) noise = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold = atoi(argv[++i]);
        else { fprintf(stderr, "usage: %s [--stale] [--size W H] [--frames N] [--transform T] [--scale S] [--tol N] [--passes N] [--radius N] [--halo N] [--noise F] [--hold 0-3] [-v]\n", argv[0]); return 2; }
    }
    wlr_log_init(verbose ? WLR_DEBUG : WLR_ERROR, NULL);

    struct wl_display *dpy = wl_display_create();
    struct wlr_backend *be = wlr_headless_backend_create(wl_display_get_event_loop(dpy));
    struct rig rig = { 0 };
    if (hold < 0 || hold > 3) { fprintf(stderr, "--hold is 0-3\n"); return 2; }
    rig.hold = hold;
    rig.renderer = be ? fx_renderer_create(be) : NULL;
    if (!rig.renderer) { printf("SKIP: no renderer (fx_renderer_create)\n"); return 77; }
    struct wlr_allocator *alloc = wlr_allocator_autocreate(be, rig.renderer);
    if (!alloc) { printf("SKIP: no allocator\n"); return 77; }

    for (int i = 0; i < 2; i++)
        rig.out[i] = wlr_headless_add_output(be, W, H);
    if (!wlr_backend_start(be)) { printf("SKIP: backend did not start\n"); return 77; }
    for (int i = 0; i < 2; i++)
        if (!output_enable(rig.out[i], alloc, rig.renderer, transform, scale)) {
            printf("SKIP: output commit failed\n"); return 77;
        }
    rig.w = rig.out[0]->width;          /* buffer size, untransformed */
    rig.h = rig.out[0]->height;
    for (int i = 0; i < 2; i++)
        rig.cap[i] = calloc((size_t)rig.w * rig.h, 4);

    int lw, lh;                          /* the scene's coordinates */
    wlr_output_effective_resolution(rig.out[0], &lw, &lh);

    rig.scene = wlr_scene_create();
    wlr_scene_set_blur_data(rig.scene, passes, radius, noise, 0.90f, 1.00f, 1.15f);
    for (int i = 0; i < 2; i++) {
        rig.so[i] = wlr_scene_output_create(rig.scene, rig.out[i]);
        wlr_scene_output_set_position(rig.so[i], 0, 0);
    }
    struct wlr_scene_tree *root = &rig.scene->tree;

    /* Wallpaper. */
    struct mem_buffer *wall = mb_new(lw, lh);
    paint_pattern(wall, 1);
    struct wlr_scene_buffer *wall_n = wlr_scene_buffer_create(root, &wall->base);
    wlr_buffer_drop(&wall->base);

    /* An ordinary window that gets dragged behind the glass. */
    struct mem_buffer *behind = mb_new(lw / 3, lh / 3);
    paint_pattern(behind, 7);
    struct wlr_scene_buffer *behind_n = wlr_scene_buffer_create(root, &behind->base);
    wlr_buffer_drop(&behind->base);
    wlr_scene_node_set_position(&behind_n->node, lw / 12, lh / 8);

    /* The glass window: frame tree, halo ring, masked blur, translucent body. */
    int gw = lw * 2 / 5, gh = lh / 2, gx = lw / 3, gy = lh / 4, rad = 12;
    struct wlr_scene_tree *frame = wlr_scene_tree_create(root);
    wlr_scene_node_set_position(&frame->node, gx, gy);
    if (halo > 0) {
        struct wlr_scene_blur *ring = wlr_scene_blur_create(frame, gw + 2 * halo, gh + 2 * halo);
        wlr_scene_blur_set_corner_radii(ring, corner_radii_all(rad + halo));
        wlr_scene_blur_set_alpha(ring, 1.0f);
        wlr_scene_node_set_position(&ring->node, -halo, -halo);
        wlr_scene_blur_set_clipped_region(ring, (struct clipped_region){
            .area = { .x = halo, .y = halo, .width = gw, .height = gh },
            .corners = corner_radii_all(rad) });
    }
    struct mem_buffer *gb = mb_new(gw, gh);
    paint_glass_window(gb, 0);
    /* What the client's body looks like now — the scene does not hand the
     * buffer back, so the rig keeps its own copy for partial repaints. */
    uint32_t *gbody = malloc((size_t)gw * gh * 4);
    memcpy(gbody, gb->px, (size_t)gw * gh * 4);
    struct wlr_scene_buffer *glass = wlr_scene_buffer_create(frame, &gb->base);
    wlr_buffer_drop(&gb->base);
    wlr_scene_buffer_set_corner_radius(glass, rad);
    struct wlr_scene_blur *gblur = wlr_scene_blur_create(frame, gw, gh);
    wlr_scene_blur_set_corner_radii(gblur, corner_radii_all(rad));
    wlr_scene_blur_set_transparency_mask_source(gblur, glass);
    wlr_scene_node_place_below(&gblur->node, &glass->node);

    /* A synui panel: a translucent rect with its own blur node behind it. */
    int pw = lw / 4, ph = lh / 12, px = lw * 2 / 3, py = lh * 4 / 5;
    struct wlr_scene_blur *pblur = wlr_scene_blur_create(root, pw, ph);
    wlr_scene_blur_set_corner_radius(pblur, 10);
    wlr_scene_node_set_position(&pblur->node, px, py);
    struct wlr_scene_rect *panel = wlr_scene_rect_create(root, pw, ph,
        (float[4]){ 0.10f * 0.45f, 0.12f * 0.45f, 0.16f * 0.45f, 0.45f });
    wlr_scene_rect_set_corner_radius(panel, 10);
    wlr_scene_node_set_position(&panel->node, px, py);

    /* The cat: a small opaque sprite on top, redrawn every frame. */
    int cw = 48, ch = 32;
    struct wlr_scene_buffer *cat = wlr_scene_buffer_create(root, NULL);

    /* A fullscreen game, opened over everything for a while. */
    struct mem_buffer *gm = mb_new(lw, lh);
    paint_pattern(gm, 99);
    struct wlr_scene_buffer *game = wlr_scene_buffer_create(root, &gm->base);
    wlr_buffer_drop(&gm->base);
    wlr_scene_node_set_enabled(&game->node, false);

    /* Two frames to settle every swapchain buffer to the same picture. */
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 2; i++)
            if (!frame_one(&rig, i, true)) { printf("FAIL: render error while settling\n"); return 1; }

    long bad_frames = 0;
    int worst = 0;
    for (int f = 0; f < frames; f++) {
        /* The cat walks a loop that passes under the glass, through the halo
         * and over the panel, 3 px a frame, and its sprite changes every frame
         * (the tail never stops). */
        double t = f / (double)frames;
        int cx = (int)(lw * (0.05 + 0.85 * t));
        int cy = (int)(lh * (0.20 + 0.65 * (t < 0.5 ? 2 * t : 2 - 2 * t)));
        struct mem_buffer *cb = mb_new(cw, ch);
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++)
                cb->px[y * cw + x] = ((x + y + f) % 6 < 3) ? argb(255, 230, 140, 40)
                                                          : argb(255, 60, 30, 10);
        wlr_scene_buffer_set_buffer(cat, &cb->base);
        wlr_buffer_drop(&cb->base);
        wlr_scene_node_set_position(&cat->node, cx, cy);

        /* A client repaints a corner of itself (a click in Dolphin, Steam on
         * mouse-move): a new buffer with damage on just that box. */
        if (f % 3 == 0) {
            struct mem_buffer *nb = mb_new(gw, gh);
            paint_glass_window(nb, (unsigned)f);
            pixman_region32_t dmg;
            pixman_region32_init_rect(&dmg, 30 + (f * 5) % (gw - 120), 40, 80, 24);
            /* Outside the damage the new buffer must equal the old one, as a
             * real client's does: copy the old body back there. */
            for (int y = 0; y < gh; y++)
                for (int x = 0; x < gw; x++) {
                    size_t o = (size_t)y * gw + x;
                    if (pixman_region32_contains_point(&dmg, x, y, NULL))
                        gbody[o] = nb->px[o];
                    else
                        nb->px[o] = gbody[o];
                }
            wlr_scene_buffer_set_buffer_with_damage(glass, &nb->base, &dmg);
            pixman_region32_fini(&dmg);
            wlr_buffer_drop(&nb->base);
        }

        /* A window dragged behind the glass. */
        if (f >= 60 && f < 120)
            wlr_scene_node_set_position(&behind_n->node, lw / 12 + (f - 60) * 4, lh / 8 + (f - 60));

        /* A game opens over everything, then closes. */
        if (f == 130) wlr_scene_node_set_enabled(&game->node, true);
        if (f == 150) wlr_scene_node_set_enabled(&game->node, false);

        /* The glass window itself moves for a stretch (a drag). */
        if (f >= 170 && f < 200)
            wlr_scene_node_set_position(&frame->node, gx - (f - 170) * 3, gy + (f - 170) * 2);

        /* Negative control: a wallpaper change nobody reported. */
        if (stale && f == 40) {
            struct mem_buffer *nw = mb_new(lw, lh);
            paint_pattern(nw, 5);
            pixman_region32_t none;
            pixman_region32_init(&none);
            wlr_scene_buffer_set_buffer_with_damage(wall_n, &nw->base, &none);
            pixman_region32_fini(&none);
            wlr_buffer_drop(&nw->base);
        }

        if (!frame_one(&rig, 0, false) || !frame_one(&rig, 1, true)) {
            printf("FAIL: render error at frame %d\n", f);
            return 1;
        }
        struct diff d = compare(&rig, tol);
        if (d.maxd > worst) worst = d.maxd;
        if (d.n > 0) {
            bad_frames++;
            if (bad_frames <= 12 || verbose)
                printf("  frame %3d: %ld px differ (max %d) in %d,%d-%d,%d\n",
                       f, d.n, d.maxd, d.x1, d.y1, d.x2, d.y2);
        }
    }

    printf("%s: %d frames at %dx%d (transform %d, scale %.2f, %d pass(es), radius %d, halo %d) — "
           "%ld frame(s) differ from a whole repaint, worst channel delta %d\n",
           stale ? "stale-control" : "blur-damage", frames, rig.w, rig.h, transform,
           scale, passes, radius, halo, bad_frames, worst);
    /* One buffer would mean every frame repaints on top of the last one, and a
     * stale region could never exist to be found. A screen rotates 2-3. */
    printf("  partial-damage output rotated through %d swapchain buffer(s)\n", rig.nseen);
    if (rig.nseen < 2) {
        printf("FAIL: the rig never rendered into an older buffer — it proves nothing\n");
        return 1;
    }

    for (int i = 0; i < 2; i++)
        for (int k = 0; k < 4; k++)
            if (rig.held[i][k]) wlr_buffer_unlock(rig.held[i][k]);
    wlr_scene_node_destroy(&rig.scene->tree.node);
    for (int i = 0; i < 2; i++) free(rig.cap[i]);
    free(gbody);
    wlr_allocator_destroy(alloc);
    wlr_renderer_destroy(rig.renderer);
    wlr_backend_destroy(be);
    wl_display_destroy(dpy);
    (void)wall_n;
    /* --stale is the negative control, and it inverts HERE rather than through
     * meson's should_fail: that would also count a crash as the expected
     * failure, and a rig that segfaults sees nothing. */
    if (stale) {
        printf("%s\n", bad_frames ? "ok: the rig sees an undamaged change"
                                   : "FAIL: an undamaged wallpaper change went unseen");
        return bad_frames ? 0 : 1;
    }
    return bad_frames ? 1 : 0;
}
