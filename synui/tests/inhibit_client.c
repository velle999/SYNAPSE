/*
 * inhibit_client — a window that asks for the keys synui would take as binds.
 *
 * keyboard-shortcuts-inhibit-v1 is how Moonlight in fullscreen, and gtk-vnc for
 * its keyboard grab, ask the compositor to hand over keys it would otherwise act
 * on — Super+O included, which on the ThinkPad moved the Moonlight window
 * instead of reaching the desktop at the other end. This client maps a plain
 * window and, once it has keyboard focus, asks for exactly that (`inhibit`) or
 * does not (`plain`, the control). It prints what it is told:
 *
 *   enter            keyboard focus arrived
 *   active|inactive  the compositor granted or withdrew the inhibition
 *   key <code> <0|1> a key reached this window (evdev code, released|pressed)
 *
 * ⚠ IT CONNECTS TO $WAYLAND_DISPLAY AND NEVER FALLS BACK. wl_display_connect
 * with no name set uses wayland-0 in $XDG_RUNTIME_DIR, which on a developer's
 * machine is the live desktop. Unset is an error, not a default.
 *
 * Usage: inhibit_client inhibit|plain [seconds]
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE           /* memfd_create */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"

#define W 320
#define H 200

static struct wl_compositor *compositor;
static struct wl_shm        *shm;
static struct xdg_wm_base   *wm_base;
static struct wl_seat       *seat;
static struct zwp_keyboard_shortcuts_inhibit_manager_v1 *inhibit_mgr;

static struct wl_surface   *surface;
static struct xdg_surface  *xsurf;
static struct xdg_toplevel *toplevel;
static struct wl_keyboard  *keyboard;
static struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor;

static bool want_inhibit;
static bool running = true;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

/* ── shm buffer ──────────────────────────────────────────── */
static void buf_release(void *d, struct wl_buffer *b) { (void)d; wl_buffer_destroy(b); }
static const struct wl_buffer_listener buf_listener = { .release = buf_release };

static void paint(void)
{
    int stride = W * 4;
    size_t size = (size_t)stride * H;
    int fd = memfd_create("inhibit", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) < 0) { perror("memfd"); return; }
    void *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) { close(fd); return; }
    memset(px, 0xff, size);
    munmap(px, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, W, H, stride,
                                                      WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(buf, &buf_listener, NULL);
    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, W, H);
    wl_surface_commit(surface);
}

/* ── inhibitor ───────────────────────────────────────────── */
static void inh_active(void *d, struct zwp_keyboard_shortcuts_inhibitor_v1 *i)
{ (void)d; (void)i; say("active"); }
static void inh_inactive(void *d, struct zwp_keyboard_shortcuts_inhibitor_v1 *i)
{ (void)d; (void)i; say("inactive"); }
static const struct zwp_keyboard_shortcuts_inhibitor_v1_listener inh_listener = {
    .active = inh_active, .inactive = inh_inactive,
};

/* ── keyboard ────────────────────────────────────────────── */
static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t sz)
{ (void)d; (void)k; (void)fmt; (void)sz; close(fd); }
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial,
                     struct wl_surface *s, struct wl_array *keys)
{
    (void)d; (void)k; (void)serial; (void)s; (void)keys;
    say("enter");
    /* Asked for once focus is here, as a real client does: the protocol lets a
     * compositor grant it only to the surface that has the keyboard. */
    if (want_inhibit && !inhibitor && inhibit_mgr) {
        inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
            inhibit_mgr, surface, seat);
        zwp_keyboard_shortcuts_inhibitor_v1_add_listener(inhibitor, &inh_listener, NULL);
    }
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{ (void)d; (void)k; (void)serial; (void)s; say("leave"); }
static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t,
                   uint32_t key, uint32_t state)
{ (void)d; (void)k; (void)serial; (void)t; say("key %u %u", key, state); }
static void kb_mods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep,
                    uint32_t lat, uint32_t lock, uint32_t grp)
{ (void)d; (void)k; (void)serial; (void)dep; (void)lat; (void)lock; (void)grp; }
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t del)
{ (void)d; (void)k; (void)r; (void)del; }
static const struct wl_keyboard_listener kb_listener = {
    .keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave,
    .key = kb_key, .modifiers = kb_mods, .repeat_info = kb_repeat,
};

/* ── seat ────────────────────────────────────────────────── */
/* The keyboard is asked for when the seat says it has one, as a real client
 * does: a headless compositor has no keyboard until the rig's virtual one
 * connects, and asking before that is a protocol error. */
static void seat_caps(void *d, struct wl_seat *st, uint32_t caps)
{
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
        keyboard = wl_seat_get_keyboard(st);
        wl_keyboard_add_listener(keyboard, &kb_listener, NULL);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard) {
        wl_keyboard_release(keyboard);
        keyboard = NULL;
    }
}
static void seat_name(void *d, struct wl_seat *st, const char *n)
{ (void)d; (void)st; (void)n; }
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps, .name = seat_name,
};

/* ── xdg-shell ───────────────────────────────────────────── */
static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial)
{ (void)d; xdg_wm_base_pong(b, serial); }
static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };
static void tl_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h,
                         struct wl_array *st)
{ (void)d; (void)t; (void)w; (void)h; (void)st; }
static void tl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; running = false; }
static const struct xdg_toplevel_listener tl_listener = {
    .configure = tl_configure, .close = tl_close,
};
static void xs_configure(void *d, struct xdg_surface *x, uint32_t serial)
{ (void)d; xdg_surface_ack_configure(x, serial); paint(); }
static const struct xdg_surface_listener xs_listener = { .configure = xs_configure };

/* ── registry ────────────────────────────────────────────── */
static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver)
{
    (void)d; (void)ver;
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
    else if (!strcmp(iface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name))
        inhibit_mgr = wl_registry_bind(r, name,
            &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n)
{ (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_remove,
};

int main(int argc, char **argv)
{
    if (argc < 2 || (strcmp(argv[1], "inhibit") && strcmp(argv[1], "plain"))) {
        fprintf(stderr, "usage: inhibit_client inhibit|plain [seconds]\n");
        return 2;
    }
    want_inhibit = !strcmp(argv[1], "inhibit");
    int secs = argc > 2 ? atoi(argv[2]) : 4;

    const char *name = getenv("WAYLAND_DISPLAY");
    if (!name || !*name) {
        fprintf(stderr, "inhibit_client: WAYLAND_DISPLAY is not set — refusing "
                        "to fall back to the live desktop\n");
        return 2;
    }
    struct wl_display *dpy = wl_display_connect(name);
    if (!dpy) { fprintf(stderr, "inhibit_client: cannot connect to %s\n", name); return 1; }

    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!compositor || !shm || !wm_base || !seat) {
        fprintf(stderr, "inhibit_client: missing globals\n");
        return 1;
    }
    if (want_inhibit && !inhibit_mgr) say("no-manager");
    xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);
    wl_seat_add_listener(seat, &seat_listener, NULL);

    surface  = wl_compositor_create_surface(compositor);
    xsurf    = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xsurf, &xs_listener, NULL);
    toplevel = xdg_surface_get_toplevel(xsurf);
    xdg_toplevel_add_listener(toplevel, &tl_listener, NULL);
    xdg_toplevel_set_app_id(toplevel, "inhibit_client");
    xdg_toplevel_set_title(toplevel, "inhibit_client");
    wl_surface_commit(surface);

    struct timespec t0, now;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (running) {
        if (wl_display_roundtrip(dpy) < 0) break;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - t0.tv_sec >= secs) break;
        usleep(20 * 1000);
    }

    if (inhibitor) zwp_keyboard_shortcuts_inhibitor_v1_destroy(inhibitor);
    if (inhibit_mgr) zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(inhibit_mgr);
    if (keyboard) wl_keyboard_release(keyboard);
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xsurf);
    wl_surface_destroy(surface);
    xdg_wm_base_destroy(wm_base);
    wl_seat_release(seat);
    wl_shm_destroy(shm);
    wl_compositor_destroy(compositor);
    wl_registry_destroy(reg);
    wl_display_disconnect(dpy);
    return 0;
}
