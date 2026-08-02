/* x11_or_move_test — an override-redirect window that moves after it maps.
 *
 * This is exactly what a Steam menu does: it is placed in ROOT coordinates by
 * the client, mapped, and then moved again a moment later when the client
 * reconciles the guess it made against where its toplevel really is. The
 * compositor is not asked; it is told, through the X server.
 *
 * Maps a solid red 200x150 unmanaged window at 100,100, then on SIGUSR1 moves
 * it to 700,400 and stays alive so the screen can be captured either side.
 *
 * Usage: x11_or_move_test   (SIGUSR1 to move, SIGTERM to quit)
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define X1 100
#define Y1 100
#define X2 700
#define Y2 400
#define W  200
#define H  150

static volatile sig_atomic_t go = 0;
static volatile sig_atomic_t quit = 0;

static void on_usr1(int sig) { (void)sig; go = 1; }
static void on_term(int sig) { (void)sig; quit = 1; }

int main(void)
{
    signal(SIGUSR1, on_usr1);
    signal(SIGTERM, on_term);

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "x11_or_move_test: cannot open display\n"); return 1; }

    int scr = DefaultScreen(dpy);
    Window root = RootWindow(dpy, scr);

    /* Red on a TrueColor visual, which is all Xwayland offers. The colour is
     * the whole assertion: the test finds this block in a screenshot. */
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.background_pixel  = 0x00ff0000;
    attr.border_pixel      = 0x00ff0000;
    Window w = XCreateWindow(dpy, root, X1, Y1, W, H, 0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWOverrideRedirect | CWBackPixel | CWBorderPixel,
                             &attr);

    XSelectInput(dpy, w, ExposureMask | StructureNotifyMask);
    XMapWindow(dpy, w);
    XFlush(dpy);
    printf("mapped %d,%d %dx%d\n", X1, Y1, W, H);
    fflush(stdout);

    int moved = 0;
    while (!quit) {
        while (XPending(dpy)) {
            XEvent e;
            XNextEvent(dpy, &e);
            if (e.type == Expose) {
                XClearWindow(dpy, w);
                XFlush(dpy);
            }
        }
        if (go && !moved) {
            moved = 1;
            XMoveWindow(dpy, w, X2, Y2);
            printf("moved %d,%d\n", X2, Y2);
            fflush(stdout);
        }
        /* Repaint every tick, always. A move generates no Expose of its own —
         * the contents travel with the window — so a one-shot XClearWindow
         * after the move left the compositor holding a buffer the client had
         * only partly redrawn, and the screenshot caught a 10px slice of red
         * instead of the whole block. The window under test is a rectangle of
         * one colour; repainting it is free. */
        XClearWindow(dpy, w);
        XFlush(dpy);
        usleep(50000);
    }

    XCloseDisplay(dpy);
    return 0;
}
