/*
 * wp_live_gl_client — a background layer-shell client that paints with the GPU.
 *
 * TWO MODES, because a live wallpaper's buffer can arrive two ways and only one
 * of them is readable:
 *
 *   (default)   EGL/GLES through wayland-egl — a DMA-BUF with the driver's own
 *               tiled modifier, which NVIDIA imports as a plain GL_TEXTURE_2D.
 *   --linear    a DMA-BUF the client allocates itself with the LINEAR modifier
 *               and fills on the CPU. On NVIDIA (verified with
 *               eglQueryDmaBufModifiersEXT on an RTX 3060) LINEAR is the ONE
 *               modifier flagged external_only, so the compositor imports it as
 *               GL_TEXTURE_EXTERNAL_OES — the case scenefx cannot bind to an
 *               FBO and therefore cannot read back.
 *
 * WHY THIS EXISTS AND swaybg DOES NOT COVER IT.
 *
 * swaybg hands the compositor a wl_shm buffer, which wlroots imports as a plain
 * GL_TEXTURE_2D — a texture the renderer can bind to an FBO and read back. A
 * real live wallpaper does not: linux-wallpaperengine renders with EGL and
 * commits a DMA-BUF, and on NVIDIA that buffer's modifier makes the import
 * `external_only`, i.e. GL_TEXTURE_EXTERNAL_OES. scenefx's fx_texture_bind()
 * refuses those, so wlr_texture_preferred_read_format() answers
 * DRM_FORMAT_INVALID and wlr_texture_read_pixels() fails — silently, both of
 * them. The palette measurement then has nothing and falls back to the
 * invisible static picture underneath, which is exactly the bug this client
 * reproduces and the swaybg test cannot.
 *
 * It clears to one solid colour (argv[1], "#rrggbb", default green) with GLES
 * and keeps drawing on frame callbacks, which is all the compositor needs to
 * see to call it a wallpaper.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "linux-dmabuf-v1-client-protocol.h"

static struct wl_compositor *compositor;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *layer_surface;
static struct wl_egl_window *egl_window;
static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;
static EGLConfig egl_cfg;
static struct zwp_linux_dmabuf_v1 *dmabuf;
static struct wl_buffer *dma_buffer;
static float col[3] = { 0.0f, 0.63f, 0.0f };
static unsigned char rgb[3] = { 0x00, 0xA0, 0x00 };
static int cfg_w, cfg_h, running = 1, linear_mode = 0;

static void draw(void);

static void frame_done(void *data, struct wl_callback *cb, uint32_t t)
{
    (void)data; (void)t;
    wl_callback_destroy(cb);
    draw();
}
static const struct wl_callback_listener frame_listener = { .done = frame_done };

/*
 * Allocate one LINEAR DMA-BUF, fill it with the colour on the CPU, and wrap it
 * in a wl_buffer. Linear is the point: it is the modifier NVIDIA reports as
 * external_only, and an external-only import is the texture the palette
 * measurement could not read.
 */
static struct wl_buffer *linear_dmabuf(int w, int h)
{
    int fd = -1;
    for (int i = 128; i < 136 && fd < 0; i++) {
        char path[64];
        snprintf(path, sizeof path, "/dev/dri/renderD%d", i);
        fd = open(path, O_RDWR | O_CLOEXEC);
    }
    if (fd < 0) { fprintf(stderr, "wp_live_gl_client: no render node\n"); return NULL; }

    struct gbm_device *dev = gbm_create_device(fd);
    if (!dev) { fprintf(stderr, "wp_live_gl_client: no gbm device\n"); return NULL; }

    /* GBM_BO_USE_LINEAR alone: NVIDIA refuses LINEAR|RENDERING outright, and
     * this buffer is never rendered into — it is filled with the CPU. */
    struct gbm_bo *bo = gbm_bo_create(dev, (uint32_t)w, (uint32_t)h,
                                      GBM_FORMAT_ARGB8888, GBM_BO_USE_LINEAR);
    if (!bo) { fprintf(stderr, "wp_live_gl_client: no linear bo\n"); return NULL; }

    uint32_t stride = 0;
    void *map_data = NULL;
    unsigned char *p = gbm_bo_map(bo, 0, 0, (uint32_t)w, (uint32_t)h,
                                  GBM_BO_TRANSFER_WRITE, &stride, &map_data);
    if (!p) { fprintf(stderr, "wp_live_gl_client: gbm_bo_map failed\n"); return NULL; }
    for (int y = 0; y < h; y++) {
        unsigned char *row = p + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            row[x * 4 + 0] = rgb[2];   /* native-endian ARGB: B, G, R, A */
            row[x * 4 + 1] = rgb[1];
            row[x * 4 + 2] = rgb[0];
            row[x * 4 + 3] = 0xff;
        }
    }
    gbm_bo_unmap(bo, map_data);

    int buf_fd = gbm_bo_get_fd(bo);
    uint64_t mod = gbm_bo_get_modifier(bo);
    struct zwp_linux_buffer_params_v1 *params =
        zwp_linux_dmabuf_v1_create_params(dmabuf);
    zwp_linux_buffer_params_v1_add(params, buf_fd, 0, gbm_bo_get_offset(bo, 0),
                                   gbm_bo_get_stride(bo),
                                   (uint32_t)(mod >> 32), (uint32_t)(mod & 0xffffffff));
    struct wl_buffer *buf = zwp_linux_buffer_params_v1_create_immed(
        params, w, h, DRM_FORMAT_ARGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    close(buf_fd);
    fprintf(stderr, "wp_live_gl_client: linear dmabuf %dx%d modifier 0x%016llx\n",
            w, h, (unsigned long long)mod);
    return buf;
}

static void draw_dmabuf(void)
{
    if (!dma_buffer) {
        dma_buffer = linear_dmabuf(cfg_w, cfg_h);
        if (!dma_buffer) { running = 0; return; }
    }
    wl_surface_attach(surface, dma_buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, cfg_w, cfg_h);

    struct wl_callback *cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &frame_listener, NULL);
    wl_surface_commit(surface);
}

static void draw(void)
{
    if (linear_mode) { draw_dmabuf(); return; }
    if (egl_surf == EGL_NO_SURFACE) return;
    eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx);
    glViewport(0, 0, cfg_w, cfg_h);
    glClearColor(col[0], col[1], col[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    struct wl_callback *cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &frame_listener, NULL);
    eglSwapBuffers(egl_dpy, egl_surf);
}

static void ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                         uint32_t serial, uint32_t w, uint32_t h)
{
    (void)data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    cfg_w = (int)w ? (int)w : 640;
    cfg_h = (int)h ? (int)h : 480;

    if (linear_mode) { draw(); return; }

    if (!egl_window) {
        egl_window = wl_egl_window_create(surface, cfg_w, cfg_h);
        egl_surf = eglCreateWindowSurface(egl_dpy, egl_cfg,
                                          (EGLNativeWindowType)egl_window, NULL);
        if (egl_surf == EGL_NO_SURFACE) {
            fprintf(stderr, "wp_live_gl_client: no EGL window surface\n");
            running = 0;
            return;
        }
    } else {
        wl_egl_window_resize(egl_window, cfg_w, cfg_h, 0, 0);
    }
    draw();
}

static void ls_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)data; (void)ls;
    running = 0;
}

static const struct zwlr_layer_surface_v1_listener ls_listener = {
    .configure = ls_configure,
    .closed    = ls_closed,
};

static void reg_global(void *data, struct wl_registry *reg, uint32_t name,
                       const char *iface, uint32_t ver)
{
    (void)data; (void)ver;
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
        layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 1);
    else if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(reg, name, &zwp_linux_dmabuf_v1_interface,
                                  ver < 3 ? ver : 3);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n)
{ (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_remove,
};

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--linear")) { linear_mode = 1; continue; }
        unsigned r, g, b;
        const char *h = argv[i] + (argv[i][0] == '#');
        if (sscanf(h, "%2x%2x%2x", &r, &g, &b) == 3) {
            col[0] = r / 255.0f; col[1] = g / 255.0f; col[2] = b / 255.0f;
            rgb[0] = (unsigned char)r; rgb[1] = (unsigned char)g;
            rgb[2] = (unsigned char)b;
        }
    }

    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "wp_live_gl_client: no display\n"); return 1; }

    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!compositor || !layer_shell) {
        fprintf(stderr, "wp_live_gl_client: no compositor/layer-shell\n");
        return 1;
    }
    if (linear_mode && !dmabuf) {
        fprintf(stderr, "wp_live_gl_client: no linux-dmabuf\n");
        return 1;
    }

    if (linear_mode) goto surface_setup;

    egl_dpy = eglGetDisplay((EGLNativeDisplayType)dpy);
    if (egl_dpy == EGL_NO_DISPLAY || !eglInitialize(egl_dpy, NULL, NULL)) {
        fprintf(stderr, "wp_live_gl_client: no EGL display\n");
        return 1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLint n = 0;
    if (!eglChooseConfig(egl_dpy, cfg_attr, &egl_cfg, 1, &n) || n < 1) {
        fprintf(stderr, "wp_live_gl_client: no EGL config\n");
        return 1;
    }
    static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_ctx = eglCreateContext(egl_dpy, egl_cfg, EGL_NO_CONTEXT, ctx_attr);
    if (egl_ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "wp_live_gl_client: no EGL context\n");
        return 1;
    }

surface_setup:
    surface = wl_compositor_create_surface(compositor);
    layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper");
    zwlr_layer_surface_v1_add_listener(layer_surface, &ls_listener, NULL);
    zwlr_layer_surface_v1_set_anchor(layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, -1);
    /* No input region at all: a wallpaper that takes the pointer stops the
     * desktop responding, and this stand-in should behave like one. */
    struct wl_region *empty = wl_compositor_create_region(compositor);
    wl_surface_set_input_region(surface, empty);
    wl_region_destroy(empty);
    wl_surface_commit(surface);

    while (running && wl_display_dispatch(dpy) != -1) { }
    return 0;
}
