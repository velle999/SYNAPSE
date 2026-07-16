/* x11_remap_test — map/unmap/re-map one X11 toplevel, the way an app that
 * closes to the tray does.
 *
 * This is the cycle Steam performs every time it closes to the tray and is
 * restored, and it is the one no other test covered: every smoke test maps a
 * window once and then exits, so the compositor's re-map path never ran.
 *
 * The window is a normal managed toplevel (no override-redirect), and each
 * cycle waits for the X server to round-trip so the compositor genuinely sees
 * a separate unmap and map rather than a coalesced no-op.
 *
 * Usage: x11_remap_test [cycles]   (default 5)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

int main(int argc, char **argv)
{
    int cycles = (argc > 1) ? atoi(argv[1]) : 5;
    if (cycles < 1) cycles = 1;

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "x11_remap_test: cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(dpy);
    Window w = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, 400, 300,
                                   0, BlackPixel(dpy, screen),
                                   WhitePixel(dpy, screen));

    /* A WM_CLASS makes the view identifiable in the log, like Steam's. */
    XClassHint *ch = XAllocClassHint();
    ch->res_name  = (char *)"x11-remap-test";
    ch->res_class = (char *)"x11-remap-test";
    XSetClassHint(dpy, w, ch);
    XFree(ch);
    XStoreName(dpy, w, "x11 remap test");

    XSelectInput(dpy, w, StructureNotifyMask | ExposureMask);

    for (int i = 0; i < cycles; i++) {
        XMapWindow(dpy, w);
        XFlush(dpy);
        /* Drain to the MapNotify so the compositor has really mapped us before
         * we tear it down again — otherwise the pair can collapse into
         * nothing and the cycle is not exercised at all. */
        for (;;) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == MapNotify) break;
        }
        /* Give the compositor a frame to build the view's scene tree. */
        usleep(150000);

        XUnmapWindow(dpy, w);
        XFlush(dpy);
        for (;;) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == UnmapNotify) break;
        }
        usleep(150000);

        printf("x11_remap_test: cycle %d/%d done\n", i + 1, cycles);
        fflush(stdout);
    }

    XDestroyWindow(dpy, w);
    XCloseDisplay(dpy);
    return 0;
}
