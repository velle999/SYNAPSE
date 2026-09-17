/* x11_dialog_test — a dialog whose owner never maps, the way a Windows
 * installer opens under Wine.
 *
 * An Inno Setup installer's first window is "Select Setup Language". Its owner
 * is the Delphi TApplication window, which exists but is never shown, so on X11
 * the dialog maps with WM_TRANSIENT_FOR naming a window nobody can see. synui
 * floats any transient or modal X11 window, and that is the path this client
 * exercises: a managed 400x300 window, transient for an unmapped 1x1 leader.
 *
 * Prints "mapped" once the dialog is mapped and stays alive until SIGTERM.
 *
 * Usage: x11_dialog_test
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define W 400
#define H 300

static volatile sig_atomic_t quit = 0;

static void on_term(int sig) { (void)sig; quit = 1; }

int main(void)
{
    signal(SIGTERM, on_term);

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }
    int scr = DefaultScreen(dpy);
    Window root = RootWindow(dpy, scr);

    /* The owner. Created, never mapped. */
    Window leader = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);

    Window dlg = XCreateSimpleWindow(dpy, root, 0, 0, W, H, 0, 0, 0xff0000);
    XStoreName(dpy, dlg, "Select Setup Language");
    XClassHint class = { .res_name = "x11dialog", .res_class = "x11dialog" };
    XSetClassHint(dpy, dlg, &class);
    XSetTransientForHint(dpy, dlg, leader);

    Atom type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(dpy, dlg, type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&dialog, 1);

    XMapWindow(dpy, dlg);
    XSync(dpy, False);
    printf("mapped\n");
    fflush(stdout);

    while (!quit) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
        }
        usleep(50 * 1000);
    }

    XDestroyWindow(dpy, dlg);
    XDestroyWindow(dpy, leader);
    XCloseDisplay(dpy);
    return 0;
}
