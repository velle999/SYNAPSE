#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include "../include/synui.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "SynapseOS Wayland Compositor\n"
        "\n"
        "Options:\n"
        "  -d, --debug    Enable verbose wlroots logging\n"
        "  -v, --version  Print version and exit\n"
        "  -h, --help     Show this help and exit\n"
        "\n"
        "Environment overrides:\n"
        "  WLR_BACKENDS   Force backend (e.g. drm,libinput or x11)\n"
        "  WLR_RENDERER   Force renderer (e.g. pixman)\n"
        "\n"
        "Keybindings:\n"
        "  Super+Enter        Open terminal\n"
        "  Super+Space        Open AI command bar\n"
        "  Super+1..9         Switch workspace\n"
        "  Super+Shift+Q      Quit compositor\n",
        prog);
}

/* Returns 1 if running inside a known hypervisor. */
static int detect_vm(void) {
    static const char *const vendors[] = {
        "VirtualBox", "VMware", "QEMU", "innotek", "KVM", "Xen", NULL
    };
    FILE *f = fopen("/sys/class/dmi/id/sys_vendor", "r");
    if (!f) return 0;
    char buf[128] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    for (int i = 0; vendors[i]; i++) {
        if (strstr(buf, vendors[i]))
            return 1;
    }
    return 0;
}

/*
 * Verify that DISPLAY / WAYLAND_DISPLAY point to live sockets.
 * Clears stale vars so wlroots does not try the wrong backend.
 * If no display environment survives, forces WLR_BACKENDS=drm,libinput
 * so synui can start on a bare TTY.
 */
static void sanitize_display_env(void) {
    /* Let the user override everything via WLR_BACKENDS. */
    if (getenv("WLR_BACKENDS"))
        return;

    /* Check Wayland parent socket. */
    const char *wd = getenv("WAYLAND_DISPLAY");
    if (wd) {
        char path[108] = {0};
        const char *xdg = getenv("XDG_RUNTIME_DIR");
        if (!xdg) xdg = "/run/user/0";
        snprintf(path, sizeof(path), "%s/%s", xdg, wd);
        if (access(path, R_OK | W_OK) != 0) {
            fprintf(stderr,
                "synui: WAYLAND_DISPLAY=%s socket not accessible — clearing\n", wd);
            unsetenv("WAYLAND_DISPLAY");
        }
    }

    /* Check X11 parent socket. */
    const char *disp = getenv("DISPLAY");
    if (disp) {
        int n = 0;
        /* DISPLAY is typically ":N" or "host:N"; extract N after the colon. */
        const char *colon = strchr(disp, ':');
        if (colon)
            n = atoi(colon + 1);
        char path[64];
        snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", n);
        if (access(path, R_OK | W_OK) != 0) {
            fprintf(stderr,
                "synui: DISPLAY=%s X server socket not accessible — clearing\n", disp);
            unsetenv("DISPLAY");
        }
    }

    /* No live display server found → run standalone on TTY via DRM. */
    if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY")) {
        fprintf(stderr, "synui: no display server found — using drm,libinput backend\n");
        setenv("WLR_BACKENDS", "drm,libinput", 0);
    }
}

int main(int argc, char *argv[]) {
    int debug = 0;

    static struct option long_opts[] = {
        {"debug",   no_argument, 0, 'd'},
        {"version", no_argument, 0, 'v'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "dvh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'd': debug = 1; break;
        case 'v':
            printf("synui %s (SynapseOS Wayland Compositor)\n", SYNUI_VERSION);
            return 0;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    wlr_log_init(debug ? WLR_DEBUG : WLR_INFO, NULL);

    /* Force software rendering + DRM in VMs. */
    if (detect_vm()) {
        fprintf(stderr, "synui: VM/hypervisor detected — forcing pixman renderer\n");
        setenv("WLR_RENDERER", "pixman", 1);
        if (!getenv("WLR_BACKENDS"))
            setenv("WLR_BACKENDS", "drm,libinput", 1);
        setenv("WLR_NO_HARDWARE_CURSORS", "1", 1);
    }

    /* Clear stale DISPLAY/WAYLAND_DISPLAY if the server is not reachable. */
    sanitize_display_env();

    fprintf(stderr, "synui: starting (WLR_BACKENDS=%s WLR_RENDERER=%s)\n",
            getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "(auto)",
            getenv("WLR_RENDERER") ? getenv("WLR_RENDERER") : "(auto)");

    struct synui_server server = {0};

    if (synui_server_init(&server) < 0) {
        fprintf(stderr, "synui: failed to initialize compositor\n");
        return 1;
    }

    fprintf(stderr, "synui %s — SynapseOS compositor running on %s\n",
            SYNUI_VERSION,
            getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "unknown");

    synui_server_run(&server);
    synui_server_destroy(&server);
    return 0;
}
