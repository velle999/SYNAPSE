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

static void sig_handler(int sig) {
    (void)sig;
    /* The monitor loop runs on g_state.running — clear that, not a separate
     * flag, or the daemon would never stop. */
    g_state.running = 0;
}

static void usage(void) {
    fprintf(stderr,
        "synnet %s — SynapseOS AI Network Policy Daemon\n"
        "Usage: synnet [OPTIONS]\n"
        "  --foreground    Run in foreground\n"
        "  --debug         Verbose logging\n"
        "  --dry-run       Monitor only, no blocking\n"
        "  --status        Show the firewall, the live ruleset and the blocklist\n"
        "  --firewall [on|off]\n"
        "                  Apply the base input firewall now, or switch it on\n"
        "                  or off for good. Off is remembered, or the daemon\n"
        "                  would put it back within the minute. (root)\n"
        "  --allow <ip>    Allow IP\n"
        "  --block <ip>    Block IP\n"
        "  -h, --help      This help\n",
        SYNNET_VERSION);
}

int main(int argc, char *argv[]) {
    int foreground = 0, dry_run = 0, debug = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--foreground")) foreground = 1;
        else if (!strcmp(argv[i], "--dry-run"))   dry_run = 1;
        else if (!strcmp(argv[i], "--debug"))      debug = 1;
        else if (!strcmp(argv[i], "--status")) {
            return synnet_status();
        } else if (!strcmp(argv[i], "--firewall")) {
            /* `--firewall`, `--firewall on`, `--firewall off`. The bare form is
             * "apply it now" and stays what it was; the two words are the
             * PREFERENCE, which is what the settings pane writes.
             *
             * ⚠ OFF HAS TO BE PERSISTED, not just applied. The daemon rebuilds
             * a missing chain once a minute, so tearing it down without
             * recording the preference is a switch that flips itself back
             * within sixty seconds — the re-assert undoing the user instead of
             * the flush it exists for. */
            const char *want = (i + 1 < argc &&
                                (!strcmp(argv[i+1], "on") ||
                                 !strcmp(argv[i+1], "off"))) ? argv[++i] : NULL;
            /* Apply it now, without waiting for the once-a-minute check or a
             * daemon restart. Not merely convenience: the firewall used to be
             * reachable ONLY as a side effect of starting the daemon, so the
             * answer to "put it back" was `systemctl restart synnet`, which
             * also drops the process event stream and the synapd connection.
             *
             * Needs root — it loads an nftables chain. Said here rather than
             * letting nft's own message arrive on stderr, where it reads like a
             * bug in synnet. */
            if (geteuid() != 0) {
                fprintf(stderr, "synnet: --firewall needs root "
                                "(sudo synnet --firewall)\n");
                return 1;
            }
            if (want && synnet_firewall_set_enabled(!strcmp(want, "on")) != 0) {
                fprintf(stderr, "synnet: could not write %s\n",
                        synnet_fw_pref_path());
                return 1;
            }

            if (want && !strcmp(want, "off")) {
                synnet_nft_drop_firewall();
                printf("synnet: input firewall OFF — nothing inbound is "
                       "filtered. `synnet --firewall on` puts it back.\n");
                return 0;
            }

            /* Applying while the preference says off would be a lie that lasts
             * a minute: the next tick reads the file and takes the chain away
             * again. Say what is actually in the way. */
            if (!want && !synnet_firewall_enabled()) {
                fprintf(stderr, "synnet: the firewall is switched off in %s — "
                                "`synnet --firewall on` to turn it back on\n",
                        synnet_fw_pref_path());
                return 1;
            }

            if (synnet_nft_ensure_firewall() != 0) {
                fprintf(stderr, "synnet: could not apply the input firewall — "
                                "this box is NOT ingress-filtered\n");
                return 1;
            }
            printf("synnet: input firewall applied "
                   "(default-drop input; loopback, established, ICMP, "
                   "private-range sources and DHCP accepted)\n");
            return 0;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(); return 0;
        } else if (!strcmp(argv[i], "--allow") && i+1 < argc) {
            return synnet_apply_rule(argv[++i], SYNNET_ACTION_ALLOW);
        } else if (!strcmp(argv[i], "--block") && i+1 < argc) {
            return synnet_apply_rule(argv[++i], SYNNET_ACTION_BLOCK);
        } else {
            fprintf(stderr, "synnet: unknown option '%s'\n", argv[i]);
            usage();
            return 1;
        }
    }

    openlog("synnet", LOG_PID | (foreground ? LOG_PERROR : 0) | LOG_CONS,
            LOG_DAEMON);
    setlogmask(LOG_UPTO(debug ? LOG_DEBUG : LOG_INFO));
    syslog(LOG_INFO, "synnet %s starting%s", SYNNET_VERSION,
           dry_run ? " (dry-run)" : "");

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    g_state.dry_run = dry_run;

    if (synnet_init(&g_state) < 0) {
        syslog(LOG_ERR, "synnet_init failed");
        return EXIT_FAILURE;
    }

    synnet_run(&g_state);
    synnet_shutdown(&g_state);
    closelog();
    return 0;
}
