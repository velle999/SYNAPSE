/*
 * vpointer_click.c — drive a real pointer double-click into a nested synui.
 *
 * The geometry half of edge-expand is reachable through `synctl dispatch`, but
 * the GESTURE is not: "two presses on the same window and the same edge inside
 * 400 ms" lives in cursor_button() and only a pointer can say it. So this is a
 * pointer — zwlr_virtual_pointer_v1, which synui exports as a privileged global
 * for syn-arcade's controller-as-mouse, and which wlroots wraps in a real
 * struct wlr_pointer. As far as the compositor is concerned these events come
 * from a mouse.
 *
 * ⚠ IT CONNECTS TO $WAYLAND_DISPLAY AND MUST NEVER FALL BACK. wl_display_connect
 * with a NULL name uses "wayland-0" under $XDG_RUNTIME_DIR, which on a
 * developer's machine is the LIVE desktop — a test that fell back would
 * double-click on velle's own windows. The name is required here, and an
 * unset one is an error rather than a default.
 *
 * Usage: vpointer_click X Y [N]            — move to (X,Y) in output coordinates
 *                                            and click the left button N times
 *                                            (default 2) inside the
 *                                            double-click window.
 *        vpointer_click X Y drag TOX TOY   — press at (X,Y), travel to
 *                                            (TOX,TOY) in steps, release. The
 *                                            steps matter: an armed grab is
 *                                            promoted to a drag by MOTION, so a
 *                                            single jump to the far end is a
 *                                            different code path from a drag.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct zwlr_virtual_pointer_manager_v1 *mgr;
static struct wl_seat *seat;
static struct wl_output *output;
/* The DENOMINATOR for motion_absolute, which takes a FRACTION and not a pixel:
 * wlroots computes x / x_extent and multiplies it by the layout size. Passing
 * an extent of 1 asks to move to <pixel> times the width of the screen, which
 * lands nowhere and silently does nothing. Read from the output's own mode so
 * the caller can go on naming pixels. */
static int32_t out_w, out_h;

static void out_geometry(void *d, struct wl_output *o, int32_t x, int32_t y,
                         int32_t pw, int32_t ph, int32_t sub, const char *make,
                         const char *model, int32_t tr)
{ (void)d;(void)o;(void)x;(void)y;(void)pw;(void)ph;(void)sub;(void)make;
  (void)model;(void)tr; }
static void out_mode(void *d, struct wl_output *o, uint32_t flags,
                     int32_t w, int32_t h, int32_t refresh)
{
    (void)d; (void)o; (void)refresh;
    if (flags & WL_OUTPUT_MODE_CURRENT) { out_w = w; out_h = h; }
}
static void out_done(void *d, struct wl_output *o) { (void)d; (void)o; }
static void out_scale(void *d, struct wl_output *o, int32_t f)
{ (void)d; (void)o; (void)f; }
static const struct wl_output_listener out_listener = {
    .geometry = out_geometry, .mode = out_mode,
    .done = out_done, .scale = out_scale,
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver)
{
    (void)d; (void)ver;
    if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
        mgr = wl_registry_bind(r, name,
                               &zwlr_virtual_pointer_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name))
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && !output) {
        output = wl_registry_bind(r, name, &wl_output_interface, 2);
        wl_output_add_listener(output, &out_listener, NULL);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name)
{ (void)d; (void)r; (void)name; }
static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_remove,
};

/* The compositor stamps events with this; wlroots wants it monotonic and in
 * milliseconds, and the double-click test is a subtraction of two of them. */
static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: vpointer_click X Y [clicks]\n");
        return 2;
    }
    int px = atoi(argv[1]), py = atoi(argv[2]);
    /* ⚠ EVERY COORDINATE IS SENT AS uint32_t. A negative one does not clamp,
     * it wraps to ~4.29e9 and the compositor slams the cursor to the far edge
     * — which reads as a drag in the OPPOSITE direction and cost an hour once.
     * Refuse it here rather than let a caller's arithmetic go quietly wrong. */
    #define NEG_CHECK(v, what) do { \
        if ((v) < 0) { \
            fprintf(stderr, "vpointer_click: %s is %d; coordinates are " \
                            "unsigned on the wire and a negative one wraps\n", \
                    what, (v)); \
            return 2; \
        } \
    } while (0)
    NEG_CHECK(px, "X"); NEG_CHECK(py, "Y");
    bool drag = argc > 4 && !strcmp(argv[3], "drag");
    int tox = drag ? atoi(argv[4]) : 0;
    int toy = drag ? (argc > 5 ? atoi(argv[5]) : py) : 0;
    int clicks = (!drag && argc > 3) ? atoi(argv[3]) : 2;
    if (drag) { NEG_CHECK(tox, "drag target X"); NEG_CHECK(toy, "drag target Y"); }

    const char *sock = getenv("WAYLAND_DISPLAY");
    if (!sock || !*sock) {
        fprintf(stderr, "vpointer_click: WAYLAND_DISPLAY is unset — refusing to\n"
                        "fall back to wayland-0, which is the live desktop.\n");
        return 2;
    }

    struct wl_display *dpy = wl_display_connect(sock);
    if (!dpy) { fprintf(stderr, "cannot connect to %s\n", sock); return 1; }

    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    wl_display_roundtrip(dpy);   /* …and again, for the output's mode event */

    if (out_w <= 0 || out_h <= 0) {
        fprintf(stderr, "vpointer_click: no output mode — cannot scale motion\n");
        return 1;
    }
    if (!mgr) {
        fprintf(stderr, "vpointer_click: no zwlr_virtual_pointer_manager_v1\n");
        return 1;
    }

    struct zwlr_virtual_pointer_v1 *ptr =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, seat);
    if (!ptr) { fprintf(stderr, "vpointer_click: no pointer\n"); return 1; }

    /* Absolute motion, so the test names a pixel rather than accumulating
     * deltas from wherever the cursor happened to be. x/x_extent is a FRACTION
     * of the layout — see out_w above for the trap. */
    zwlr_virtual_pointer_v1_motion_absolute(ptr, now_ms(),
                                            (uint32_t)px, (uint32_t)py,
                                            (uint32_t)out_w, (uint32_t)out_h);
    zwlr_virtual_pointer_v1_frame(ptr);
    wl_display_roundtrip(dpy);

    if (drag) {
        zwlr_virtual_pointer_v1_button(ptr, now_ms(), BTN_LEFT,
                                       WL_POINTER_BUTTON_STATE_PRESSED);
        zwlr_virtual_pointer_v1_frame(ptr);
        wl_display_roundtrip(dpy);

        /* In steps, and each one its own frame: GRAB_DRAG_SLOP is measured
         * from where the button went down, so the first few motions are
         * deliberately below it — that is the state being exercised. */
        const int steps = 12;
        for (int i = 1; i <= steps; i++) {
            int x = px + (tox - px) * i / steps;
            int y = py + (toy - py) * i / steps;
            zwlr_virtual_pointer_v1_motion_absolute(ptr, now_ms(),
                                                    (uint32_t)x, (uint32_t)y,
                                                    (uint32_t)out_w,
                                                    (uint32_t)out_h);
            zwlr_virtual_pointer_v1_frame(ptr);
            wl_display_roundtrip(dpy);
            nanosleep(&(struct timespec){ 0, 10 * 1000 * 1000 }, NULL);
        }

        zwlr_virtual_pointer_v1_button(ptr, now_ms(), BTN_LEFT,
                                       WL_POINTER_BUTTON_STATE_RELEASED);
        zwlr_virtual_pointer_v1_frame(ptr);
        wl_display_roundtrip(dpy);
        clicks = 0;
    }

    for (int i = 0; i < clicks; i++) {
        zwlr_virtual_pointer_v1_button(ptr, now_ms(), BTN_LEFT,
                                       WL_POINTER_BUTTON_STATE_PRESSED);
        zwlr_virtual_pointer_v1_frame(ptr);
        wl_display_flush(dpy);
        zwlr_virtual_pointer_v1_button(ptr, now_ms(), BTN_LEFT,
                                       WL_POINTER_BUTTON_STATE_RELEASED);
        zwlr_virtual_pointer_v1_frame(ptr);
        wl_display_roundtrip(dpy);
        /* Well inside the 400 ms the compositor allows, and long enough that
         * the two presses are not coalesced into one event batch. */
        if (i + 1 < clicks)
            nanosleep(&(struct timespec){ 0, 60 * 1000 * 1000 }, NULL);
    }

    zwlr_virtual_pointer_v1_destroy(ptr);
    wl_display_roundtrip(dpy);
    wl_display_disconnect(dpy);
    return 0;
}
