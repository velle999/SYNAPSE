/* idle_inhibit — tiny Wayland client that holds a real idle inhibitor on demand.
 * Installed as the executable "synui-idle-inhibit".
 *
 * synui implements zwp_idle_inhibit_manager_v1: creating an inhibitor makes the
 * compositor set wlr_idle_notifier_v1 inhibited, which stops the ext-idle-notify
 * ticks swayidle watches — so swayidle never reaches its lock/DPMS timeouts, and
 * re-arms cleanly the instant the inhibitor is released.
 *
 * This process reads a control stream on stdin: a byte '1' creates the inhibitor
 * (media active), '0' destroys it (media idle). EOF or a Wayland disconnect
 * exits (systemd restarts us for the next synui session).
 *
 * Detection of "media playing" lives in the feeding script (synui-media-inhibit);
 * this program only knows how to hold and release the Wayland inhibitor.
 */
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include "idle-inhibit-unstable-v1-client-protocol.h"

static struct wl_compositor                *compositor;
static struct zwp_idle_inhibit_manager_v1  *inhibit_mgr;
static struct wl_surface                   *surface;
static struct zwp_idle_inhibitor_v1        *inhibitor;

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t version)
{
    (void)data; (void)version;
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 1);
    else if (strcmp(iface, zwp_idle_inhibit_manager_v1_interface.name) == 0)
        inhibit_mgr = wl_registry_bind(reg, name,
                                       &zwp_idle_inhibit_manager_v1_interface, 1);
}

static void registry_global_remove(void *data, struct wl_registry *reg,
                                   uint32_t name)
{
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

static void set_inhibit(struct wl_display *dpy, int on)
{
    if (on && !inhibitor) {
        inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(inhibit_mgr,
                                                                 surface);
        fprintf(stderr, "synui-idle-inhibit: audio active — inhibiting idle\n");
    } else if (!on && inhibitor) {
        zwp_idle_inhibitor_v1_destroy(inhibitor);
        inhibitor = NULL;
        fprintf(stderr, "synui-idle-inhibit: audio idle — releasing inhibit\n");
    }
    wl_display_flush(dpy);
}

int main(void)
{
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) {
        fprintf(stderr, "synui-idle-inhibit: cannot connect to Wayland\n");
        return 1;
    }

    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(dpy);

    if (!compositor || !inhibit_mgr) {
        fprintf(stderr,
                "synui-idle-inhibit: compositor lacks idle-inhibit support\n");
        wl_display_disconnect(dpy);
        return 1;
    }

    /* The inhibitor needs a surface to reference; synui counts the inhibitor on
     * creation and never requires it to be mapped, so an unmapped surface is
     * sufficient. */
    surface = wl_compositor_create_surface(compositor);
    wl_surface_commit(surface);
    wl_display_flush(dpy);

    struct pollfd fds[2] = {
        { .fd = wl_display_get_fd(dpy), .events = POLLIN },
        { .fd = STDIN_FILENO,           .events = POLLIN },
    };

    for (;;) {
        while (wl_display_prepare_read(dpy) != 0)
            wl_display_dispatch_pending(dpy);
        wl_display_flush(dpy);

        if (poll(fds, 2, -1) < 0) {
            wl_display_cancel_read(dpy);
            break;
        }

        if (fds[0].revents & POLLIN)
            wl_display_read_events(dpy);
        else
            wl_display_cancel_read(dpy);
        wl_display_dispatch_pending(dpy);

        if (fds[0].revents & (POLLERR | POLLHUP))
            break;

        /* POLLHUP/POLLERR (feeder closed the pipe) may arrive without POLLIN. */
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            char buf[64];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0)
                break;  /* EOF: feeder exited */
            for (ssize_t i = 0; i < n; i++) {
                if (buf[i] == '1')      set_inhibit(dpy, 1);
                else if (buf[i] == '0') set_inhibit(dpy, 0);
            }
        }
    }

    if (inhibitor)
        zwp_idle_inhibitor_v1_destroy(inhibitor);
    wl_display_disconnect(dpy);
    return 0;
}
