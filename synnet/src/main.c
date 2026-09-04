#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <syslog.h>
#include <locale.h>
#include "../include/synnet.h"
#include "../include/i18n.h"
#include "config.h"

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
        "  --trust-if <iface>\n"
        "  --untrust-if <iface>\n"
        "                  Accept DHCP and DNS on a container/VM bridge this\n"
        "                  box is the gateway for (waydroid0, virbr0, …). The\n"
        "                  guest's first DHCP packet comes from 0.0.0.0 and is\n"
        "                  otherwise dropped, so its network never comes up.\n"
        "                  Applied immediately and remembered. (root)\n"
        "  --open <proto>/<port> [<cidr>]\n"
        "  --close <proto>/<port> [<cidr>]\n"
        "                  Let a source the base chain would DROP reach a port\n"
        "                  — e.g. `--open tcp/5900 100.64.0.0/10` for a VPN.\n"
        "                  The source defaults to `any`, which means the whole\n"
        "                  internet if this box is reachable from it. This is\n"
        "                  NOT what --allow does: that only un-blocks an\n"
        "                  address. Applied immediately and remembered. (root)\n"
        "  --allow <ip>    Un-block an IP that --block blocked. Does NOT open\n"
        "                  the firewall to it; see --open\n"
        "  --block <ip>    Block IP\n"
        "  -h, --help      This help\n",
        SYNNET_VERSION);
}

/*
 * ⛔ LC_NUMERIC STAYS AT C, AND FOR THIS PROGRAM THAT IS THE nft SCRIPT.
 * setlocale(LC_ALL, "") changes what printf's %f writes and what atof() reads,
 * and everything synnet composes with snprintf goes somewhere that is not a
 * person: an nft ruleset, a key=value state file syn-settings parses, and a
 * prompt whose answer is matched against BLOCK and ALLOW. A decimal comma in
 * any of the three is a rule the kernel refuses or a number a sibling misreads.
 */
static void i18n_start(void) {
    setlocale(LC_ALL, "");
    setlocale(LC_NUMERIC, "C");

    const char *dir = getenv("SYNNET_LOCALEDIR");
    bindtextdomain(SYNNET_GETTEXT_DOMAIN, dir && *dir ? dir : SYNNET_LOCALEDIR);
    bind_textdomain_codeset(SYNNET_GETTEXT_DOMAIN, "UTF-8");
    textdomain(SYNNET_GETTEXT_DOMAIN);
}

void synnet_i18n_init(void) { i18n_start(); }

int main(int argc, char *argv[]) {
    int foreground = 0, dry_run = 0, debug = 0;

    synnet_i18n_init();

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
                fputs(_("synnet: --firewall needs root "
                        "(sudo synnet --firewall)\n"), stderr);
                return 1;
            }
            if (want && synnet_firewall_set_enabled(!strcmp(want, "on")) != 0) {
                fprintf(stderr, _("synnet: could not write %s\n"),
                        synnet_fw_pref_path());
                return 1;
            }

            if (want && !strcmp(want, "off")) {
                synnet_nft_drop_firewall();
                fputs(_("synnet: input firewall OFF — nothing inbound is "
                        "filtered. `synnet --firewall on` puts it back.\n"), stdout);
                return 0;
            }

            /* Applying while the preference says off would be a lie that lasts
             * a minute: the next tick reads the file and takes the chain away
             * again. Say what is actually in the way. */
            if (!want && !synnet_firewall_enabled()) {
                fprintf(stderr, _("synnet: the firewall is switched off in %s — "
                                  "`synnet --firewall on` to turn it back on\n"),
                        synnet_fw_pref_path());
                return 1;
            }

            if (synnet_nft_ensure_firewall() != 0) {
                fputs(_("synnet: could not apply the input firewall — "
                        "this box is NOT ingress-filtered\n"), stderr);
                return 1;
            }
            fputs(_("synnet: input firewall applied "
                    "(default-drop input; loopback, established, ICMP, "
                    "private-range sources and DHCP accepted)\n"), stdout);
            return 0;
        } else if ((!strcmp(argv[i], "--trust-if") ||
                    !strcmp(argv[i], "--untrust-if")) && i + 1 < argc) {
            /* Trust a container/VM bridge for the gateway services this box
             * serves on it. See SYNNET_FW_IFACES for why this is DHCP+DNS and
             * not `allow in on <iface>`.
             *
             * Root because it writes /etc and loads a chain — and said here
             * rather than letting fopen's EACCES arrive as a confusing "could
             * not write" for something the user is simply not allowed to do. */
            int on = !strcmp(argv[i], "--trust-if");
            const char *ifn = argv[++i];

            if (geteuid() != 0) {
                /* ⚠ The two %s are FLAG SPELLINGS — what you type — so they
                 * stay English while the sentence around them moves. */
                fprintf(stderr, _("synnet: %s needs root "
                                  "(sudo synnet %s %s)\n"),
                        on ? "--trust-if" : "--untrust-if",
                        on ? "--trust-if" : "--untrust-if", ifn);
                return 1;
            }

            int r = synnet_trusted_iface_set(ifn, on);
            if (r == -2) {
                fprintf(stderr, _("synnet: '%s' is not a legal interface name "
                                  "(1-15 chars: letters, digits, '_', '.', '-')\n"),
                        ifn);
                return 1;
            }
            if (r != 0) {
                fprintf(stderr, _("synnet: could not update %s\n"),
                        synnet_fw_ifaces_path());
                return 1;
            }

            /* ⚠ THE FILE IS NOT THE FIREWALL. The daemon's re-assert tick only
             * rebuilds a chain that has GONE; one that is merely out of date
             * looks healthy to it and would keep dropping the container's DHCP
             * until the next reboot. Reload the chain now. */
            if (!synnet_firewall_enabled()) {
                /* ⛔ TWO WHOLE SENTENCES. "trusted"/"untrusted" dropped into a
                 * %s reach every reader in English however the line around
                 * them is translated, and neither can agree with the interface
                 * name beside it. */
                if (on)
                    printf(_("synnet: trusted %s in %s — the firewall is switched "
                             "off, so nothing changed in the kernel.\n"),
                           ifn, synnet_fw_ifaces_path());
                else
                    printf(_("synnet: untrusted %s in %s — the firewall is switched "
                             "off, so nothing changed in the kernel.\n"),
                           ifn, synnet_fw_ifaces_path());
                return 0;
            }
            if (synnet_nft_ensure_firewall() != 0) {
                fprintf(stderr, _("synnet: %s recorded, but the firewall could "
                                  "not be reloaded — this box is NOT ingress-"
                                  "filtered right now\n"), ifn);
                return 1;
            }
            /* ⛔ FOUR WORDS IN TWO SLOTS IS ONE SENTENCE PER BRANCH. */
            if (on)
                printf(_("synnet: trusting %s — DHCP and DNS from that link "
                         "are accepted\n"), ifn);
            else
                printf(_("synnet: no longer trusting %s — DHCP and DNS from "
                         "that link are no longer accepted\n"), ifn);
            {
                /* Matched by name, so an interface that does not exist yet is
                 * fine and is the normal case: container bridges appear when
                 * the container starts. Say so, or it reads like a typo. */
                char sys[512];
                snprintf(sys, sizeof(sys), "/sys/class/net/%s", ifn);
                if (on && access(sys, F_OK) != 0)
                    printf(_("synnet: %s does not exist yet — the rule matches "
                             "by name and takes effect when it appears.\n"), ifn);
            }
            return 0;

        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(); return 0;
        } else if ((!strcmp(argv[i], "--open") || !strcmp(argv[i], "--close"))
                   && i + 1 < argc) {
            /* ⛔ THE ONLY VERB THAT WIDENS INGRESS. `--allow` is an unblock and
             * `--trust-if` is DHCP+DNS; neither can let a non-private source
             * reach a port, which is why a VPN on 100.64.0.0/10 established
             * outbound and then had every packet inside the tunnel dropped. */
            int on = !strcmp(argv[i], "--open");
            const char *pp = argv[++i];
            /* The source is optional, so take the next argument only if there
             * IS one and it is not another flag — otherwise `--open tcp/22
             * --status` would eat the flag as a CIDR and refuse it. */
            const char *src = NULL;
            if (i + 1 < argc && argv[i+1][0] != '-') src = argv[++i];

            char spec[SYNNET_PORTRULE_MAX * 2];
            snprintf(spec, sizeof(spec), "%s %s", pp, src ? src : "any");

            if (geteuid() != 0) {
                /* ⚠ The two %s are FLAG SPELLINGS — what you type — so they
                 * stay English while the sentence around them moves. */
                fprintf(stderr, _("synnet: %s needs root (sudo synnet %s %s)\n"),
                        on ? "--open" : "--close",
                        on ? "--open" : "--close", spec);
                return 1;
            }

            int r = synnet_open_port_set(spec, on);
            if (r == -2) {
                fprintf(stderr,
                        _("synnet: '%s' is not a legal rule. Write it as "
                          "<tcp|udp>/<port> and a source, e.g. "
                          "'tcp/5900 100.64.0.0/10' or 'tcp/5900 any'\n"), spec);
                return 1;
            }
            if (r != 0) {
                fprintf(stderr, _("synnet: could not update %s\n"),
                        synnet_fw_ports_path());
                return 1;
            }

            /* ⚠ THE FILE IS NOT THE FIREWALL — the same trap --trust-if
             * documents. The daemon's re-assert tick only rebuilds a chain
             * that has GONE; one that is merely out of date looks healthy. */
            if (!synnet_firewall_enabled()) {
                /* ⛔ TWO WHOLE SENTENCES per branch. */
                if (on)
                    printf(_("synnet: recorded '%s' in %s — the firewall is "
                             "switched off, so nothing changed in the kernel.\n"),
                           spec, synnet_fw_ports_path());
                else
                    printf(_("synnet: removed '%s' from %s — the firewall is "
                             "switched off, so nothing changed in the kernel.\n"),
                           spec, synnet_fw_ports_path());
                return 0;
            }
            if (synnet_nft_ensure_firewall() != 0) {
                fprintf(stderr, _("synnet: '%s' recorded, but the firewall could "
                                  "not be reloaded — this box is NOT ingress-"
                                  "filtered right now\n"), spec);
                return 1;
            }
            if (on) {
                printf(_("synnet: %s is open\n"), spec);
                /* ⛔ SAID OUT LOUD, EVERY TIME. `any` is the difference between
                 * a port reachable from one VPN and a port reachable from the
                 * internet, and it is one word — the person who typed it has to
                 * be told which of the two they just did. */
                if (!src || !strcmp(src, "any"))
                    fputs(_("synnet: ⚠ that is open to ANY source, including "
                            "the internet if this machine is reachable from "
                            "it. Name a CIDR to narrow it.\n"), stdout);
            } else {
                printf(_("synnet: %s is closed\n"), spec);
            }
            return 0;
        } else if (!strcmp(argv[i], "--allow") && i+1 < argc) {
            return synnet_apply_rule(argv[++i], SYNNET_ACTION_ALLOW);
        } else if (!strcmp(argv[i], "--block") && i+1 < argc) {
            return synnet_apply_rule(argv[++i], SYNNET_ACTION_BLOCK);
        } else {
            fprintf(stderr, _("synnet: unknown option '%s'\n"), argv[i]);
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
