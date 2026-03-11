#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <syslog.h>
#include "../include/synnet.h"

static synnet_state_t g_state;
static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void usage(void) {
    fprintf(stderr,
        "synnet %s — SynapseOS AI Network Policy Daemon\n"
        "Usage: synnet [OPTIONS]\n"
        "  --foreground    Run in foreground\n"
        "  --debug         Verbose logging\n"
        "  --dry-run       Monitor only, no blocking\n"
        "  --status        Show current rules and stats\n"
        "  --allow <ip>    Allow IP\n"
        "  --block <ip>    Block IP\n"
        "  -h, --help      This help\n",
        SYNNET_VERSION);
}

int main(int argc, char *argv[]) {
    int foreground = 0, dry_run = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--foreground")) foreground = 1;
        else if (!strcmp(argv[i], "--dry-run"))   dry_run = 1;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(); return 0;
        } else if (!strcmp(argv[i], "--allow") && i+1 < argc) {
            return synnet_apply_rule(argv[++i], SYNNET_ACTION_ALLOW);
        } else if (!strcmp(argv[i], "--block") && i+1 < argc) {
            return synnet_apply_rule(argv[++i], SYNNET_ACTION_BLOCK);
        }
    }

    openlog("synnet", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "synnet %s starting", SYNNET_VERSION);

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    if (synnet_init(&g_state) < 0) {
        syslog(LOG_ERR, "synnet_init failed");
        return EXIT_FAILURE;
    }

    (void)dry_run;
    synnet_run(&g_state);
    synnet_shutdown(&g_state);
    closelog();
    return 0;
}
