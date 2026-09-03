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
 *        vpointer_click X Y right [N]      — the same, with the RIGHT button,
 *                                            for opening a context menu.
 *        vpointer_click X Y move           — move only, no button at all, to
 *                                            put the pointer on something and
 *                                            let a hover state settle.
 *        vpointer_click X Y drag TOX TOY   — press at (X,Y), travel to
 *                                            (TOX,TOY) in steps, release. The
 *                                            steps matter: an armed grab is
 *                                            promoted to a drag by MOTION, so a
 *                                            single jump to the far end is a
 *                                            different code path from a drag.
 *        vpointer_click X Y rel DX DY N [GAP_MS]
 *                                          — put the cursor at (X,Y) with one
 *                                            absolute motion, then send N
 *                                            RELATIVE motions of (DX,DY).
 *                                            This is the only mode that takes
 *                                            the relative path, which is where
 *                                            pointer_smoothing lives: absolute
 *                                            motion is deliberately not
 *                                            smoothed (a tablet must sit under
 *                                            the stylus), so every other mode
 *                                            here would test the wrong branch.
 *                                            GAP_MS spaces the reports, which
 *                                            matters because the filter works
 *                                            from ELAPSED TIME — sending them
 *                                            back to back is a 1000 Hz mouse
 *                                            and spacing them 8 ms apart is a
 *                                            125 Hz one.
 *        vpointer_click X Y scroll N [horiz]
 *                                          — move to (X,Y) and turn a MOUSE
 *                                            WHEEL N notches: positive is down
 *                                            (or, with `horiz`, right).
 *
 * ⚠ THE WHEEL IS SENT AS DISCRETE NOTCHES, and that is the difference between
 * testing a mouse and testing a touchpad. wl_pointer carries two shapes of the
 * same gesture: a continuous `axis` value, which is what a finger produces, and
 * an `axis_discrete` count of detents, which only a wheel produces. Qt turns
 * the first into a pixelDelta and the second into an angleDelta of 120 per
 * notch — and QML that reads angleDelta (as a shelf browsing one tile per
 * notch must) sees NOTHING AT ALL from a continuous stream. A poke that sent
 * only `axis` would therefore prove the opposite of what it looks like it
 * proves: a working wheel handler would fail it, and a broken one would too.
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
    bool drag  = argc > 4 && !strcmp(argv[3], "drag");
    bool right = argc > 3 && !strcmp(argv[3], "right");
    bool moveonly = argc > 3 && !strcmp(argv[3], "move");
    bool scroll = argc > 4 && !strcmp(argv[3], "scroll");
    bool rel   = argc > 6 && !strcmp(argv[3], "rel");
    /* Deltas are SIGNED — they go on the wire as wl_fixed_t, not as the
     * unsigned coordinates NEG_CHECK guards, so moving left is legal here. */
    double rel_dx = rel ? atof(argv[4]) : 0.0;
    double rel_dy = rel ? atof(argv[5]) : 0.0;
    int    rel_n  = rel ? atoi(argv[6]) : 0;
    int    rel_gap_ms = (rel && argc > 7) ? atoi(argv[7]) : 8;
    int    rel_settle_ms = (rel && argc > 8) ? atoi(argv[8]) : 120;
    int  notches = scroll ? atoi(argv[4]) : 0;
    bool horiz = scroll && argc > 5 && !strcmp(argv[5], "horiz");
    uint32_t btn = right ? BTN_RIGHT : BTN_LEFT;
    int tox = drag ? atoi(argv[4]) : 0;
    int toy = drag ? (argc > 5 ? atoi(argv[5]) : py) : 0;
    int clicks = 2;
    if (moveonly)          clicks = 0;
    else if (rel)          clicks = 0;
    else if (scroll)       clicks = 0;
    else if (right)        clicks = argc > 4 ? atoi(argv[4]) : 1;
    else if (drag)         clicks = 0;
    else if (argc > 3)     clicks = atoi(argv[3]);

    /* ⛔ AN UNRECOGNISED WORD IS ZERO CLICKS, SILENTLY, AND THAT COST A ROUND.
     * There is no `left` keyword — the left button is the default and the third
     * argument is a COUNT — so `vpointer_click 120 73 left` reads as
     * atoi("left") == 0 and moves the pointer without ever pressing it. The
     * tool exits 0, the rig sees nothing happen, and the failure it reports is
     * about the feature under test rather than about its own invocation.
     * Refuse it here instead: this is a test tool, and a test tool that does
     * nothing quietly is worse than one that will not run. */
    if (argc > 3 && !drag && !right && !moveonly && !scroll && !rel &&
        clicks == 0 && strcmp(argv[3], "0") != 0) {
        fprintf(stderr, "vpointer_click: '%s' is not a count or a known word.\n"
                        "There is no `left` — the left button is the default:\n"
                        "  vpointer_click X Y [N]        left, N times\n"
                        "  vpointer_click X Y right [N]  right\n"
                        "  vpointer_click X Y move       no button at all\n",
                argv[3]);
        return 2;
    }
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

    /*
     * Relative motion, one report at a time, exactly as a mouse produces it.
     *
     * Each is its own frame and its own roundtrip: coalescing them into one
     * batch would hand the compositor a single large delta, which is the one
     * shape a smoothing filter has nothing to do — the whole question is what
     * it does to a STREAM of small ones.
     */
    if (rel) {
        for (int i = 0; i < rel_n; i++) {
            zwlr_virtual_pointer_v1_motion(ptr, now_ms(),
                                           wl_fixed_from_double(rel_dx),
                                           wl_fixed_from_double(rel_dy));
            zwlr_virtual_pointer_v1_frame(ptr);
            wl_display_roundtrip(dpy);
            if (rel_gap_ms > 0)
                nanosleep(&(struct timespec){
                    0, (long)rel_gap_ms * 1000 * 1000 }, NULL);
        }
        /* Let the settle timer run. A smoothed pointer still has a fraction of
         * the last report in hand when the reports stop, and it is applied one
         * frame later — reading the position before that is reading a movement
         * that has not finished, which would fail against a correct filter. */
        if (rel_settle_ms > 0)
            nanosleep(&(struct timespec){
                0, (long)rel_settle_ms * 1000 * 1000 }, NULL);
        wl_display_roundtrip(dpy);
    }

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

    /*
     * ⚠ MOVE FIRST, THEN TURN, AND THE MOVE IS ITS OWN ROUNDTRIP. The wheel
     * goes to whatever surface has pointer focus, and focus follows MOTION —
     * so a scroll sent in the same batch as the motion that was supposed to
     * put the cursor over the thing being tested can be delivered to the
     * surface the cursor was over BEFORE it. The absolute motion above has
     * already been flushed and answered by the time this runs.
     */
    if (scroll) {
        const uint32_t axis = horiz ? WL_POINTER_AXIS_HORIZONTAL_SCROLL
                                    : WL_POINTER_AXIS_VERTICAL_SCROLL;
        /* One detent, in the units wl_pointer carries. libinput reports 15 for
         * a normal mouse wheel and that is what every client's arithmetic is
         * calibrated against; the DISCRETE count beside it is the one that
         * matters (see the header), but a value of zero here would still be a
         * scroll of nothing to anything reading the continuous axis. */
        const double step = 15.0;
        int dir = notches < 0 ? -1 : 1;
        int n = notches < 0 ? -notches : notches;

        for (int i = 0; i < n; i++) {
            zwlr_virtual_pointer_v1_axis_source(ptr,
                                                WL_POINTER_AXIS_SOURCE_WHEEL);
            zwlr_virtual_pointer_v1_axis_discrete(ptr, now_ms(), axis,
                                                  step * dir, dir);
            zwlr_virtual_pointer_v1_frame(ptr);
            wl_display_roundtrip(dpy);
            /* Notches arrive from a hand, not a loop. Sent back to back they
             * are coalesced into one event by the time Qt sees them, which
             * makes "eight notches" indistinguishable from one and hides
             * exactly the accumulation bug this exists to catch. */
            nanosleep(&(struct timespec){ 0, 40 * 1000 * 1000 }, NULL);
        }
    }

    for (int i = 0; i < clicks; i++) {
        zwlr_virtual_pointer_v1_button(ptr, now_ms(), btn,
                                       WL_POINTER_BUTTON_STATE_PRESSED);
        zwlr_virtual_pointer_v1_frame(ptr);
        wl_display_flush(dpy);
        zwlr_virtual_pointer_v1_button(ptr, now_ms(), btn,
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
