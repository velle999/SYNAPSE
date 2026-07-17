/* x11_wedge_test — reproduce the Steam close-to-tray "wedge".
 *
 * The symptom (see the dock/Steam notes): after a number of close-to-tray /
 * restore cycles, the X window is IsViewable at full size but the compositor
 * has no view for it at all, wlroots' _NET_CLIENT_LIST is empty, and nothing
 * but an X unmap/map recovers it.
 *
 * Suspected cause is a race in wlroots' xwm, NOT in synui:
 * xwm_handle_surface_serial_message() drops a WL_SURFACE_SERIAL that arrives
 * while xsurface->serial is still non-zero, and serial is only cleared by
 * xwayland_surface_dissociate() — which runs off the wl_surface destroy, on a
 * *different fd* from the X11 client message. If the X fd is read first, the
 * pairing message is dropped, the xsurface is never put on unpaired_surfaces,
 * and the surface that follows is never associated. The window can then never
 * map, for the life of the X window.
 *
 * x11_remap_test deliberately round-trips and sleeps between unmap and map so
 * the compositor sees a clean, separate pair. That serialisation is exactly
 * what hides this bug. This test does the opposite: unmap and map are issued
 * back to back with a single flush and no round-trip in between, so the
 * destroy(S1)+create(S2) on the Wayland fd and the WL_SURFACE_SERIAL on the X
 * fd are in flight at the same time.
 *
 * A cycle is a PASS if the compositor has a view for the window again within
 * the settle deadline, and a WEDGE if it never comes back.
 *
 * Usage: x11_wedge_test [cycles]   (default 40)
 * Requires SYNUI_SOCKET to point at the compositor under test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define APP_ID       "x11-wedge-test"
#define SETTLE_MS    3000
#define POLL_MS      100

/* Ask the compositor whether it has a mapped view for us. synctl only reports
 * mapped views (ipc.c: cmd_clients skips !v->mapped), which is precisely the
 * property under test: a wedged window is absent, not present-and-broken. */
static int view_present(void)
{
    FILE *p = popen("synctl clients 2>/dev/null", "r");
    if (!p)
        return -1;

    char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf) - 1, p);
    buf[n] = '\0';
    pclose(p);
    return strstr(buf, "\"" APP_ID "\"") != NULL;
}

/* Poll until the view is back, or the deadline passes. */
static int wait_for_view(void)
{
    for (int waited = 0; waited < SETTLE_MS; waited += POLL_MS) {
        if (view_present() == 1)
            return 1;
        usleep(POLL_MS * 1000);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int cycles = (argc > 1) ? atoi(argv[1]) : 40;
    if (cycles < 1)
        cycles = 1;

    /* Refuse to run against the live desktop. synctl falls back to
     * XDG_RUNTIME_DIR/synui-$WAYLAND_DISPLAY.sock when SYNUI_SOCKET is unset,
     * which is the session compositor — this test unmaps windows in a loop and
     * has no business doing that to a real session. */
    const char *sock = getenv("SYNUI_SOCKET");
    if (!sock || !*sock) {
        fprintf(stderr, "x11_wedge_test: refusing to run: SYNUI_SOCKET is unset,\n"
                        "  synctl would fall back to the live session socket.\n");
        return 2;
    }
    fprintf(stderr, "x11_wedge_test: using SYNUI_SOCKET=%s\n", sock);

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "x11_wedge_test: cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(dpy);
    Window w = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, 400, 300,
                                   0, BlackPixel(dpy, screen),
                                   WhitePixel(dpy, screen));

    XClassHint *ch = XAllocClassHint();
    ch->res_name  = (char *)APP_ID;
    ch->res_class = (char *)APP_ID;
    XSetClassHint(dpy, w, ch);
    XFree(ch);
    XStoreName(dpy, w, "x11 wedge test");
    XSelectInput(dpy, w, StructureNotifyMask | ExposureMask);

    /* Baseline: one clean, fully round-tripped map. If this does not show up
     * the rig is broken and every later "wedge" would be a false positive. */
    XMapWindow(dpy, w);
    XFlush(dpy);
    for (;;) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == MapNotify)
            break;
    }
    if (!wait_for_view()) {
        fprintf(stderr, "x11_wedge_test: BROKEN RIG: baseline map never produced "
                        "a view — not a wedge, the compositor never saw us.\n");
        XCloseDisplay(dpy);
        return 3;
    }
    printf("baseline: view present, starting %d tight cycles\n", cycles);
    fflush(stdout);

    int gaps_seen = 0;

    for (int i = 1; i <= cycles; i++) {
        /* The whole point: no XSync, no event drain, no sleep BETWEEN the two
         * requests. Both reach the X server in one batch, so Xwayland does
         * unrealize(S1) + realize(S2) and sends the new WL_SURFACE_SERIAL while
         * the destroy of S1 is still in flight on the other fd. */
        XUnmapWindow(dpy, w);
        XMapWindow(dpy, w);
        XFlush(dpy);

        /* Round-trip AFTER the pair (not between): proves the X server has
         * processed both. This does not serialise the fds, so the race window
         * we are hunting is untouched. */
        XSync(dpy, False);

        /* Informational only. Catching the view absent here is a bonus, not a
         * health check: the compositor handles the unmap and the map in the
         * same event-loop pass, so the view is destroyed and rebuilt between
         * two IPC responses and a popen'd synctl usually cannot observe the
         * gap at all. Expect 0 even on a perfectly working rig. The real proof
         * a cycle happened is wlroots' "New xwayland surface" line per cycle,
         * which the driver counts. */
        if (view_present() == 0)
            gaps_seen++;

        /* MANDATORY settle before judging. Without this the check races the
         * compositor and returns "present" off the STALE pre-unmap view, so
         * every cycle passes instantly and the test measures nothing. */
        usleep(400000);

        if (!wait_for_view()) {
            printf("\n*** WEDGED at cycle %d/%d ***\n", i, cycles);
            printf("    window is mapped in X but the compositor has no view.\n");
            fflush(stdout);
            /* Leave the window wedged and the connection open: the driver
             * inspects X and the compositor while this state is live. */
            pause();
        }

        printf("cycle %d/%d ok\n", i, cycles);
        fflush(stdout);
    }

    printf("\nno wedge in %d cycles (incidentally caught the unmap gap on %d)\n",
           cycles, gaps_seen);
    XDestroyWindow(dpy, w);
    XCloseDisplay(dpy);
    return 0;
}
