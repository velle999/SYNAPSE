/*
 * stubborn_client — an xdg-shell client that drops one configure.
 *
 * The window bug this exists for (velle, 2026-07-31): a maximized Firefox frame
 * at 2544x1396 with the page rendered into 552x304 in its top-left corner and
 * the desktop showing through the rest, staying that way until the window was
 * moved. synui answers a toplevel's initial commit with a 0x0 configure — pick
 * your own size, the layout resizes you on map — and view_resize() is the only
 * code that ever sizes a window afterwards. So a client that does not act on the
 * configure that follows the map keeps whatever size it picked for itself, under
 * chrome drawn at the size synui thinks it has, until the user happens to drag
 * it and the next view_resize() puts it right.
 *
 * This reproduces that without needing Firefox: it picks its own size from the
 * 0x0 initial configure, then IGNORES the size of the next `drop` configures
 * (acking them, so it stays protocol-legal and synui sees a settled client),
 * and obeys everything after. With one dropped configure that is exactly the
 * failure above; the compositor's only way out is to send the configure again.
 *
 * It prints one line per commit:
 *
 *     commit <w>x<h>
 *
 * so the harness can watch the size settle without screenshotting anything.
 *
 * With `premax` it also sends set_maximized *before* its first commit, the way
 * Firefox does when it restores a maximized session. That is the second bug in
 * the same area: synui used to record the request straight into view->maximized,
 * a flag that is supposed to mean "this window has been through
 * view_apply_maximized" — the call that also floats it and saves the box to come
 * back to. The window ended up maximized-but-tiled, and, since
 * layout_restore_geometry only maximizes `if (saved_max && !view->maximized)`,
 * never went through the real path at all. The tell is `floating` in
 * `synctl clients`: a maximized window that is not floating is that bug.
 *
 * With `snap` it rounds every configured size DOWN to a multiple of that many
 * pixels before painting, the way a terminal lands on a character-cell boundary
 * a few px inside the box. xdg-shell has no size-increment protocol, so this is
 * indistinguishable from a client ignoring the configure except by how far short
 * it falls — and the compositor must not chase it. Measured against foot, which
 * commits 2 px under on height and 14 px at the worst.
 *
 * Usage: stubborn_client [drop] [seconds] [premax] [snap]  (defaults: 1 2 0 0)
 */
#define _GNU_SOURCE           /* memfd_create */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

/* The size the client picks when told to choose — the same shape Firefox landed
 * on, so a failure here looks like the screenshot. */
#define OWN_W 552
#define OWN_H 304

static struct wl_compositor *compositor;
static struct wl_shm        *shm;
static struct xdg_wm_base   *wm_base;

static struct wl_surface   *surface;
static struct xdg_surface  *xsurf;
static struct xdg_toplevel *toplevel;

static int  width = OWN_W, height = OWN_H;
static int  drop_left = 1;          /* configures whose size we ignore */
static int  snap = 0;               /* round the configured size down to this */
static bool running = true;

/* ── shm buffer ──────────────────────────────────────────── */
static struct wl_buffer *make_buffer(int w, int h)
{
    int stride = w * 4;
    size_t size = (size_t)stride * h;

    int fd = memfd_create("stubborn", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0) { perror("memfd"); return NULL; }

    void *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) { perror("mmap"); close(fd); return NULL; }
    memset(px, 0xff, size);                     /* opaque white */
    munmap(px, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                                      WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buf;
}

/* Hand the buffer back as soon as the compositor is done with it. Without this
 * every paint leaks a proxy, which an ASan build of the suite reports against
 * this fixture. */
static void buf_release(void *d, struct wl_buffer *b)
{
    (void)d;
    wl_buffer_destroy(b);
}
static const struct wl_buffer_listener buf_listener = { .release = buf_release };

static void paint(void)
{
    struct wl_buffer *buf = make_buffer(width, height);
    if (!buf) return;
    wl_buffer_add_listener(buf, &buf_listener, NULL);
    /* Declare the window geometry, which is what the compositor compares
     * against: this is the surface size, no CSD margin. */
    xdg_surface_set_window_geometry(xsurf, 0, 0, width, height);
    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, width, height);
    wl_surface_commit(surface);
    printf("commit %dx%d\n", width, height);
    fflush(stdout);
}

/* ── xdg-shell ───────────────────────────────────────────── */
static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial)
{
    (void)d;
    xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h,
                         struct wl_array *states)
{
    (void)d; (void)t; (void)states;

    if (w <= 0 || h <= 0)
        return;                     /* "you choose" — keep what we have */

    if (drop_left > 0) {
        /* The bug: acked below, but never acted on. */
        drop_left--;
        fprintf(stderr, "stubborn: ignoring configure %dx%d (%d left to drop)\n",
                w, h, drop_left);
        return;
    }
    if (snap > 1) {
        /* Never up to zero: a terminal with one cell left still shows one cell. */
        w -= w % snap; if (w < snap) w = snap;
        h -= h % snap; if (h < snap) h = snap;
    }
    width = w;
    height = h;
}

static void tl_close(void *d, struct xdg_toplevel *t)
{
    (void)d; (void)t;
    running = false;
}

static const struct xdg_toplevel_listener tl_listener = {
    .configure = tl_configure,
    .close     = tl_close,
};

static void xs_configure(void *d, struct xdg_surface *x, uint32_t serial)
{
    (void)d;
    xdg_surface_ack_configure(x, serial);
    paint();
}
static const struct xdg_surface_listener xs_listener = { .configure = xs_configure };

/* ── registry ────────────────────────────────────────────── */
static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver)
{
    (void)d; (void)ver;
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (strcmp(iface, wl_shm_interface.name) == 0)
        shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (strcmp(iface, xdg_wm_base_interface.name) == 0)
        wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name)
{ (void)d; (void)r; (void)name; }
static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_remove,
};

int main(int argc, char **argv)
{
    if (argc > 1) drop_left = atoi(argv[1]);
    int secs   = argc > 2 ? atoi(argv[2]) : 2;
    int premax = argc > 3 ? atoi(argv[3]) : 0;
    if (argc > 4) snap = atoi(argv[4]);

    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "stubborn: no display\n"); return 1; }

    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);

    if (!compositor || !shm || !wm_base) {
        fprintf(stderr, "stubborn: missing globals\n");
        return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);

    surface  = wl_compositor_create_surface(compositor);
    xsurf    = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xsurf, &xs_listener, NULL);
    toplevel = xdg_surface_get_toplevel(xsurf);
    xdg_toplevel_add_listener(toplevel, &tl_listener, NULL);
    xdg_toplevel_set_app_id(toplevel, "stubborn");
    xdg_toplevel_set_title(toplevel, "stubborn");
    /* Before the initial commit, i.e. while xdg_surface->initialized is still
     * false — the case the compositor has to hold rather than act on. */
    if (premax) xdg_toplevel_set_maximized(toplevel);
    wl_surface_commit(surface);                 /* initial commit, no buffer */

    /* Run for `secs`, so the harness gets the *settled* size rather than
     * whatever was on screen at an arbitrary instant. Roundtrip rather than
     * dispatch: a compositor that never sends the configure again is the failing
     * case, and dispatch() would block in it forever instead of reporting it. */
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (running) {
        if (wl_display_roundtrip(dpy) < 0) break;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - t0.tv_sec >= secs) break;
        usleep(50 * 1000);
    }

    printf("final %dx%d\n", width, height);
    fflush(stdout);

    /* Tear down in reverse, so an ASan build of the suite has nothing to say
     * about the fixture and every leak it reports belongs to the compositor. */
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xsurf);
    wl_surface_destroy(surface);
    xdg_wm_base_destroy(wm_base);
    wl_shm_destroy(shm);
    wl_compositor_destroy(compositor);
    wl_registry_destroy(reg);
    wl_display_disconnect(dpy);
    return 0;
}
