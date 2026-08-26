/*
 * synctl — talk to synui's control socket (the hyprctl of SynapseOS)
 *
 *   synctl workspaces
 *   synctl clients
 *   synctl activewindow
 *   synctl dispatch ws 3
 *   synctl dispatch spawn foot
 *
 * The compositor exports SYNUI_SOCKET into its children, so inside a synui
 * session this needs no configuration. Outside one, point SYNUI_SOCKET at the
 * socket yourself.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
            "usage: synctl <command> [args…]\n"
            "  clients | windows      every mapped window, as JSON\n"
            "  workspaces             all 9 virtual desktops\n"
            "  outputs | monitors     connected monitors\n"
            "  activeworkspace        the desktop currently on screen\n"
            "  activewindow           the focused window\n"
            "  binds | keys           the bind table, each chord as a keyboard\n"
            "                         says it (\"Super+Shift+C\")\n"
            "  version                compositor version\n"
            "  dispatch <action> [arg]  run any keybind action, e.g.\n"
            "                           synctl dispatch ws 3\n"
            "                           synctl dispatch spawn foot\n");
        return 2;
    }

    char path[256];
    const char *sock = getenv("SYNUI_SOCKET");
    if (sock) {
        snprintf(path, sizeof(path), "%s", sock);
    } else {
        const char *runtime = getenv("XDG_RUNTIME_DIR");
        const char *disp    = getenv("WAYLAND_DISPLAY");
        if (!runtime) {
            fprintf(stderr, "synctl: no SYNUI_SOCKET and no XDG_RUNTIME_DIR\n");
            return 1;
        }
        snprintf(path, sizeof(path), "%s/synui-%s.sock",
                 runtime, disp ? disp : "0");
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { perror("synctl: socket"); return 1; }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "synctl: cannot reach synui at %s: %s\n",
                path, strerror(errno));
        close(fd);
        return 1;
    }

    /* Join the arguments back into one line — the protocol is line-oriented. */
    char cmd[512] = {0};
    size_t n = 0;
    for (int i = 1; i < argc && n < sizeof(cmd) - 2; i++) {
        int w = snprintf(cmd + n, sizeof(cmd) - 1 - n, "%s%s",
                         i > 1 ? " " : "", argv[i]);
        if (w < 0) break;
        n += (size_t)w;
    }
    cmd[n++] = '\n';

    if (write(fd, cmd, n) < 0) { perror("synctl: write"); close(fd); return 1; }

    char buf[4096];
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        if (fwrite(buf, 1, (size_t)r, stdout) != (size_t)r) break;
    }
    close(fd);
    return 0;
}
