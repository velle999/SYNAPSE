#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include "../include/synui.h"

int main(int argc, char *argv[]) {
    wlr_log_init(WLR_DEBUG, NULL);

    struct synui_server server = {0};

    if (synui_server_init(&server) < 0) {
        fprintf(stderr, "synui: failed to initialize compositor\n");
        return 1;
    }

    printf("synui %s — SynapseOS Wayland Compositor\n", SYNUI_VERSION);
    printf("WAYLAND_DISPLAY=%s\n",
           getenv("WAYLAND_DISPLAY") ?: "unknown");

    synui_server_run(&server);
    synui_server_destroy(&server);
    return 0;
}
