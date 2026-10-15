/*
 * scene_reupload_rig.c — does a scene buffer that keeps its texture show the
 * pixels it was last given?
 *
 * scenefx0.5's 0003 patch stops wlr_scene_buffer_set_buffer() destroying the
 * texture of a compositor-drawn buffer when a same-size one replaces it: the
 * texture is kept, and the new pixels are uploaded into it (damaged region
 * only) the next time the node is rendered. synui redraws the cat, titlebars
 * and its panels that way, the cat 60 times a second.
 *
 * Keeping a texture is only correct if nothing stale survives, and a stale
 * pixel here would be a piece of the previous frame of a widget, left on
 * screen with no error anywhere. So every frame is read back and compared,
 * pixel for pixel, with the buffer that should be showing:
 *
 *   1  a first buffer (no texture yet — the ordinary path);
 *   2  a different buffer, same size, damage covering only what changed;
 *   3  the SAME buffer object redrawn in place and set again with damage;
 *   4  two replacements before one render — the upload must cover the union
 *      of both damages, from the last buffer;
 *   5  a buffer of another size (the texture must be dropped, not resized);
 *   6  unmapped (NULL) and mapped again;
 *   7  200 steps of whole-buffer redraws, the cat's pattern.
 *
 *   --stale   negative control: step 2 is set with EMPTY damage. With the
 *             patch the texture is kept and only damage is uploaded, so the
 *             old pixels must show — or the rig cannot see staleness at all.
 *             A scenefx without the patch recreates the texture from the whole
 *             buffer and nothing can be stale; that is reported as a SKIP, not
 *             a pass, because the control proves nothing there.
 *
 * Exit: 0 = every frame matched (with --stale: step 2 showed stale pixels),
 * 1 = otherwise, or a render error; 77 = no renderer, or --stale on a scenefx
 * that does not keep textures.
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
#include <scenefx/types/wlr_scene.h>

/* ── a CPU buffer, the way synui's cairo buffers are ─────────────────────── */

struct mem_buffer {
    struct wlr_buffer base;
    uint32_t *px;               /* ARGB8888, premultiplied, opaque here */
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

/* Opaque, and different at every pixel and for every seed, so a stale pixel
 * from any earlier frame cannot happen to match. */
static void paint(struct mem_buffer *m, unsigned seed, int x1, int y1, int x2, int y2)
{
    int w = m->base.width;
    for (int y = y1; y < y2; y++)
        for (int x = x1; x < x2; x++) {
            unsigned v = (unsigned)(x * 2654435761u) ^ (unsigned)(y * 40503u) ^
                         (seed * 0x9E3779B9u);
            m->px[(size_t)y * w + x] = 0xff000000u | (v & 0x00ffffffu);
        }
}

static void paint_all(struct mem_buffer *m, unsigned seed)
{
    paint(m, seed, 0, 0, m->base.width, m->base.height);
}

static struct mem_buffer *mb_copy(const struct mem_buffer *src)
{
    struct mem_buffer *m = mb_new(src->base.width, src->base.height);
    memcpy(m->px, src->px, (size_t)src->base.width * src->base.height * 4);
    return m;
}

/* ── one output, rendered whole every frame and read back ────────────────── */

struct rig {
    struct wlr_renderer *renderer;
    struct wlr_output *out;
    struct wlr_scene_output *so;
    int w, h;
    uint8_t *cap;               /* ABGR8888 bytes: R, G, B, A */
};

static bool output_enable(struct wlr_output *o, struct wlr_allocator *a,
                          struct wlr_renderer *r)
{
    if (!wlr_output_init_render(o, a, r)) return false;
    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_enabled(&st, true);
    bool ok = wlr_output_commit_state(o, &st);
    wlr_output_state_finish(&st);
    return ok;
}

static bool render(struct rig *rig)
{
    wlr_damage_ring_add_whole(&rig->so->damage_ring);
    struct wlr_output_state st;
    wlr_output_state_init(&st);
    if (!wlr_scene_output_build_state(rig->so, &st, NULL) || !st.buffer) {
        wlr_output_state_finish(&st);
        return false;
    }
    /* ⚠ A buffer the size of the output is a COMPOSITED frame. wlroots 0.20
     * can put a lone buffer node straight on the primary plane at a dst box,
     * and then nothing is rendered and nothing here is tested — the first
     * version of this rig read its own buffer back and compared it with
     * itself. The opaque backdrop in main() prevents it; this says so if it
     * ever stops. */
    if (st.buffer->width != rig->w || st.buffer->height != rig->h) {
        printf("FAIL: the frame was not composited (%dx%d buffer on a %dx%d "
               "output — direct scanout)\n", st.buffer->width,
               st.buffer->height, rig->w, rig->h);
        wlr_output_state_finish(&st);
        return false;
    }
    struct wlr_texture *t = wlr_texture_from_buffer(rig->renderer, st.buffer);
    bool ok = t && wlr_texture_read_pixels(t, &(struct wlr_texture_read_pixels_options){
        .data = rig->cap, .format = DRM_FORMAT_ABGR8888,
        .stride = (uint32_t)rig->w * 4,
    });
    if (t) wlr_texture_destroy(t);
    ok = ok && wlr_output_commit_state(rig->out, &st);
    wlr_output_state_finish(&st);
    return ok;
}

/* Pixels of the node (at x0,y0) that differ from `m`, exactly. */
static long mismatches(const struct rig *rig, const struct mem_buffer *m,
                       int x0, int y0)
{
    long n = 0;
    for (int y = 0; y < m->base.height; y++)
        for (int x = 0; x < m->base.width; x++) {
            uint32_t want = m->px[(size_t)y * m->base.width + x];
            const uint8_t *got = rig->cap + ((size_t)(y + y0) * rig->w + (x + x0)) * 4;
            if (got[0] != ((want >> 16) & 0xff) || got[1] != ((want >> 8) & 0xff) ||
                got[2] != (want & 0xff))
                n++;
        }
    return n;
}

static int failures;

static void expect(struct rig *rig, const char *step, const struct mem_buffer *m,
                   int x0, int y0)
{
    if (!render(rig)) {
        printf("FAIL %s: render or readback failed\n", step);
        failures++;
        return;
    }
    long n = mismatches(rig, m, x0, y0);
    if (n) {
        printf("FAIL %s: %ld of %d pixels are not the buffer that was set\n",
               step, n, m->base.width * m->base.height);
        failures++;
    } else {
        printf("ok   %s\n", step);
    }
}

static void set_with_damage(struct wlr_scene_buffer *node, struct mem_buffer *m,
                            int x1, int y1, int x2, int y2)
{
    pixman_region32_t d;
    pixman_region32_init_rect(&d, x1, y1, (unsigned)(x2 - x1), (unsigned)(y2 - y1));
    wlr_scene_buffer_set_buffer_with_damage(node, &m->base, &d);
    pixman_region32_fini(&d);
}

int main(int argc, char **argv)
{
    bool stale = false, verbose = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--stale")) stale = true;
        else if (!strcmp(argv[i], "-v")) verbose = true;
        else { fprintf(stderr, "usage: %s [--stale] [-v]\n", argv[0]); return 2; }
    }
    wlr_log_init(verbose ? WLR_DEBUG : WLR_ERROR, NULL);

    struct wl_display *dpy = wl_display_create();
    struct wlr_backend *be = wlr_headless_backend_create(wl_display_get_event_loop(dpy));
    struct rig rig = { 0 };
    rig.renderer = be ? fx_renderer_create(be) : NULL;
    if (!rig.renderer) { printf("SKIP: no renderer (fx_renderer_create)\n"); return 77; }
    struct wlr_allocator *alloc = wlr_allocator_autocreate(be, rig.renderer);
    if (!alloc) { printf("SKIP: no allocator\n"); return 77; }
    rig.out = wlr_headless_add_output(be, 320, 200);
    if (!wlr_backend_start(be) || !output_enable(rig.out, alloc, rig.renderer)) {
        printf("SKIP: output did not start\n");
        return 77;
    }
    rig.w = rig.out->width;
    rig.h = rig.out->height;
    rig.cap = calloc((size_t)rig.w * rig.h, 4);

    struct wlr_scene *scene = wlr_scene_create();
    rig.so = wlr_scene_output_create(scene, rig.out);
    wlr_scene_output_set_position(rig.so, 0, 0);

    const int W = 96, H = 64, X0 = 17, Y0 = 23;   /* the cat's canvas, roughly */

    /* An opaque backdrop under everything, so every frame is composited (see
     * render()). */
    wlr_scene_rect_create(&scene->tree, rig.w, rig.h,
                          (const float[4]){ 0.1f, 0.2f, 0.3f, 1.0f });

    /* 1 — first buffer: no texture yet. */
    struct mem_buffer *a = mb_new(W, H);
    paint_all(a, 1);
    struct wlr_scene_buffer *node = wlr_scene_buffer_create(&scene->tree, &a->base);
    wlr_scene_node_set_position(&node->node, X0, Y0);
    expect(&rig, "1 first buffer", a, X0, Y0);
    if (failures) return 1;

    /* 2 — a different buffer of the same size, damage over what changed. */
    struct mem_buffer *b = mb_copy(a);
    paint(b, 2, 10, 8, 50, 40);
    if (stale) {
        pixman_region32_t none;
        pixman_region32_init(&none);
        wlr_scene_buffer_set_buffer_with_damage(node, &b->base, &none);
        pixman_region32_fini(&none);
        if (!render(&rig)) { printf("FAIL --stale: render failed\n"); return 1; }
        long n = mismatches(&rig, b, X0, Y0);
        if (n == 0) {
            printf("SKIP: --stale: this scenefx recreated the texture from the "
                   "whole buffer (no keep-texture patch), so nothing can be "
                   "stale here\n");
            return 77;
        }
        printf("ok   --stale: %ld pixel(s) of the new buffer missing with no "
               "damage given — the texture was kept and only damage uploaded\n", n);
        return 0;
    }
    set_with_damage(node, b, 10, 8, 50, 40);
    expect(&rig, "2 same size, partial damage", b, X0, Y0);

    /* 3 — the same object redrawn in place. */
    paint(b, 3, 40, 20, 90, 60);
    set_with_damage(node, b, 40, 20, 90, 60);
    expect(&rig, "3 same buffer redrawn in place", b, X0, Y0);

    /* 4 — two replacements before one render: the union, from the last. */
    struct mem_buffer *c = mb_copy(b);
    paint(c, 4, 0, 0, 30, 30);
    set_with_damage(node, c, 0, 0, 30, 30);
    struct mem_buffer *d = mb_copy(c);
    paint(d, 5, 60, 30, 96, 64);
    set_with_damage(node, d, 60, 30, 96, 64);
    expect(&rig, "4 two sets, one render", d, X0, Y0);

    /* 5 — another size. */
    struct mem_buffer *e = mb_new(W + 8, H - 4);
    paint_all(e, 6);
    wlr_scene_buffer_set_buffer(node, &e->base);
    expect(&rig, "5 another size", e, X0, Y0);

    /* 6 — unmapped and mapped again. */
    wlr_scene_buffer_set_buffer(node, NULL);
    struct mem_buffer *f = mb_new(W + 8, H - 4);
    paint_all(f, 7);
    wlr_scene_buffer_set_buffer(node, &f->base);
    expect(&rig, "6 unmapped, mapped again", f, X0, Y0);

    /* 7 — the cat: a new whole buffer every step, set without damage. */
    long bad_steps = 0;
    struct mem_buffer *cat = NULL;
    for (int i = 0; i < 200; i++) {
        struct mem_buffer *n = mb_new(W + 8, H - 4);
        paint_all(n, 100u + (unsigned)i);
        wlr_scene_buffer_set_buffer(node, &n->base);
        if (cat) wlr_buffer_drop(&cat->base);
        cat = n;
        if (!render(&rig) || mismatches(&rig, cat, X0, Y0)) bad_steps++;
    }
    if (bad_steps) {
        printf("FAIL 7 200 whole redraws: %ld step(s) showed the wrong pixels\n",
               bad_steps);
        failures++;
    } else {
        printf("ok   7 200 whole redraws\n");
    }

    wlr_buffer_drop(&a->base);
    wlr_buffer_drop(&b->base);
    wlr_buffer_drop(&c->base);
    wlr_buffer_drop(&d->base);
    wlr_buffer_drop(&e->base);
    wlr_buffer_drop(&f->base);
    if (cat) wlr_buffer_drop(&cat->base);
    wlr_scene_node_destroy(&scene->tree.node);
    free(rig.cap);
    wlr_allocator_destroy(alloc);
    wlr_renderer_destroy(rig.renderer);
    wlr_backend_destroy(be);
    wl_display_destroy(dpy);

    if (failures) {
        printf("%d step(s) failed\n", failures);
        return 1;
    }
    printf("scene_reupload: every frame showed the buffer that was set\n");
    return 0;
}
