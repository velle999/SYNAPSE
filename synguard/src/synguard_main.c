/*
 * synguard_main.c — SynapseOS AI Security Monitor
 *
 * synguard sits between the kernel (synapse_kmod) and the AI (synapd)
 * to provide real-time, AI-assisted security policy enforcement.
 *
 * It is deliberately NOT an LSM (Linux Security Module) — LSMs are
 * loaded at boot and can't be updated at runtime. synguard is a
 * userspace daemon that applies policy through the kmod's action
 * interface and POSIX signals. This makes it hot-reloadable and
 * auditable without rebooting.
 *
 * The tradeoff: a determined root attacker can kill it. That's
 * acceptable for SynapseOS's threat model (workstation + server
 * AI assistant, not adversarial kernel hardening). SynapseOS
 * also ships a lightweight LSM stub for the true TOCTOU cases.
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
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <systemd/sd-daemon.h>   /* sd_notify: readiness, from the main PID */
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <getopt.h>
#include <pthread.h>
#include <time.h>

#include "synguard.h"
#include "sg_log.h"

/* ── Global state ─────────────────────────────────────────── */
static synguard_state_t g_state;

/* ── Signal handling ──────────────────────────────────────── */
static void signal_handler(int sig)
{
    switch (sig) {
    case SIGTERM:
    case SIGINT:
        sg_log(LOG_INFO, "synguard: signal %d — shutting down", sig);
        g_state.running = 0;
        break;
    case SIGHUP:
        sg_log(LOG_INFO, "synguard: SIGHUP — reloading rules");
        rules_free(&g_state);
        rules_load(&g_state, g_state.config.rules_dir);
        sg_log(LOG_INFO, "synguard: reloaded %d rules", g_state.rules_count);
        break;
    case SIGPIPE:
        break;
    }
}

static void setup_signals(void)
{
    struct sigaction sa = {0};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = signal_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

/* ── Startup banner ───────────────────────────────────────── */
static void print_banner(const synguard_state_t *s)
{
    const char *mode_str[] = {
        [MODE_ENFORCE]  = "ENFORCE",
        [MODE_AUDIT]    = "AUDIT",
        [MODE_LEARNING] = "LEARNING",
        [MODE_LOCKDOWN] = "LOCKDOWN",
    };
    sg_log(LOG_INFO, "synguard %s starting", SYNGUARD_VERSION);
    sg_log(LOG_INFO, "mode=%s rules=%d ai=%s (%s) kmod=%s",
           mode_str[s->config.mode],
           s->rules_count,
           s->config.ai_enabled ? "on" : "off",
           s->config.ai_enforce ? "enforcing" : "advisory",
           s->kmod_present ? "present" : "absent (degraded)");

    /* What the loaded policy can actually do, not just how much of it there
     * is. A rule count says nothing about capability: the shipped set is
     * entirely alert/escalate/allow/log, so "mode=ENFORCE rules=55" reads as
     * an armed system while nothing can reach a kill. Print the census, and
     * say plainly when acting is unreachable. */
    int n[VERDICT_QUARANTINE + 1];
    rules_census(s, n, sizeof(n) / sizeof(n[0]));

    sg_log(LOG_INFO,
           "policy: %d alert · %d escalate · %d allow · %d log · "
           "%d deny · %d quarantine",
           n[VERDICT_ALERT], n[VERDICT_ESCALATE], n[VERDICT_ALLOW],
           n[VERDICT_LOG], n[VERDICT_DENY], n[VERDICT_QUARANTINE]);

    if (rules_enforcement_reachable(s)) {
        sg_log(LOG_INFO, "policy: enforcement is REACHABLE — "
                         "matching events can be killed or frozen");
    } else if (s->config.mode == MODE_ENFORCE ||
               s->config.mode == MODE_LOCKDOWN) {
        /* The case a stock install is in. Not an error — it is a deliberate
         * default — but it must not be mistaken for an armed system. */
        sg_log(LOG_WARNING,
               "policy: mode=%s but NO rule carries deny/quarantine%s — "
               "nothing can be killed or frozen; synguard is detecting and "
               "alerting only. See /etc/synguard/rules.d/40-enforce.rules.example",
               mode_str[s->config.mode],
               (n[VERDICT_ESCALATE] && !s->config.ai_enforce)
                   ? " and the AI classifier is advisory (--ai-enforce unset)"
                   : "");
    } else {
        sg_log(LOG_INFO,
               "policy: mode=%s does not act — deny verdicts are logged as "
               "WOULD-DENY", mode_str[s->config.mode]);
    }
}

/* ── Runtime directory setup ─────────────────────────────── */
static int setup_dirs(void)
{
    const char *dirs[] = {
        "/run/synapd",
        "/var/log/synguard",
        "/var/lib/synguard",
        "/etc/synguard",
        "/etc/synguard/rules.d",
        NULL
    };
    for (int i = 0; dirs[i]; i++) {
        if (mkdir(dirs[i], 0750) < 0 && errno != EEXIST) {
            fprintf(stderr, "synguard: mkdir %s: %s\n", dirs[i], strerror(errno));
            return -1;
        }
    }
    return 0;
}

/* ── Usage ────────────────────────────────────────────────── */
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "SynapseOS AI Security Monitor\n"
        "\n"
        "Options:\n"
        "  -m, --mode MODE      Policy mode: enforce|audit|learning|lockdown\n"
        "                       (default: audit — safe to start)\n"
        "  -r, --rules DIR      Rules directory (default: /etc/synguard/rules.d/)\n"
        "  --no-ai              Disable AI classification (rules only)\n"
        "  --ai-enforce         Allow AI verdicts to DENY/QUARANTINE.\n"
        "                       Off by default: the classifier is advisory and\n"
        "                       its verdicts are clamped to alert. Only rule\n"
        "                       verdicts can kill without this flag.\n"
        "  --no-audit           Disable audit log\n"
        "  -d, --debug          Debug mode (foreground, verbose)\n"
        "  -f, --foreground     Run in foreground\n"
        "  -v, --version        Print version\n"
        "  -h, --help           This help\n"
        "\n"
        "Modes:\n"
        "  enforce   Block DENY verdicts with SIGKILL\n"
        "  audit     Log only, never block (safe default)\n"
        "  learning  Build baseline profile, flag anomalies\n"
        "  lockdown  Block everything not in explicit allowlist\n",
        prog
    );
}

/* ── Main event loop ──────────────────────────────────────── */
static int run_event_loop(synguard_state_t *s)
{
    sg_log(LOG_INFO, "synguard: entering event loop");

    /*
     * The event reader runs in a dedicated thread (kmod_reader_start).
     * This main loop handles:
     *   - Periodic baseline save
     *   - Stats logging
     *   - Rule hot-reload check (inotify in future)
     */
    time_t last_save  = time(NULL);
    time_t last_stats = time(NULL);

    while (s->running) {
        sleep(1);

        time_t now = time(NULL);

        /* Flush baseline to disk every 5 minutes */
        if (now - last_save > 300) {
            baseline_save(s);
            last_save = now;
        }

        /* Close out any repeat-alert windows that have expired. */
        alert_flush_summaries(s);

        /* Log stats every 60s */
        if (now - last_stats > 60) {
            sg_log(LOG_INFO,
                "synguard: stats — events=%lu rules=%lu ai=%lu denials=%lu "
                "alerts=%lu quarantines=%lu protected-skips=%lu "
                "stale-pid-skips=%lu dropped=%lu suppressed=%lu",
                s->stats.events_processed,
                s->stats.rules_matched,
                s->stats.ai_queries,
                s->stats.denials,
                s->stats.alerts,
                s->stats.quarantines,
                s->stats.protected_skips,
                s->stats.stale_pid_skips,
                s->stats.events_dropped,
                s->stats.alerts_suppressed);
            last_stats = now;
        }
    }
    return 0;
}

/* ── Entry point ──────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    int foreground = 0;
    const char *mode_str = "audit";

    static struct option long_opts[] = {
        {"mode",       required_argument, 0, 'm'},
        {"rules",      required_argument, 0, 'r'},
        {"no-ai",      no_argument,       0, 'A'},
        {"ai-enforce", no_argument,       0, 'E'},
        {"no-audit",   no_argument,       0, 'U'},
        {"debug",      no_argument,       0, 'd'},
        {"foreground", no_argument,       0, 'f'},
        {"version",    no_argument,       0, 'v'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:r:AEUdfvh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'm': mode_str = optarg; break;
        case 'r': g_state.config.rules_dir = optarg; break;
        case 'A': g_state.config.ai_enabled = 0; break;
        case 'E': g_state.config.ai_enforce = 1; break;
        case 'U': g_state.config.audit_enabled = 0; break;
        case 'd': g_state.debug = 1; foreground = 1; break;
        case 'f': foreground = 1; break;
        case 'v':
            printf("synguard %s (SynapseOS AI security monitor)\n", SYNGUARD_VERSION);
            return 0;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    /* Parse mode */
    if (strcmp(mode_str, "enforce")  == 0) g_state.config.mode = MODE_ENFORCE;
    else if (strcmp(mode_str, "audit")    == 0) g_state.config.mode = MODE_AUDIT;
    else if (strcmp(mode_str, "learning") == 0) g_state.config.mode = MODE_LEARNING;
    else if (strcmp(mode_str, "lockdown") == 0) g_state.config.mode = MODE_LOCKDOWN;
    else {
        fprintf(stderr, "synguard: unknown mode '%s'\n", mode_str);
        return 1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "synguard: must run as root\n");
        return 1;
    }

    openlog("synguard", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    sg_log_init(g_state.debug ? LOG_DEBUG : LOG_INFO);

    if (setup_dirs() < 0) return 1;
    setup_signals();

    if (synguard_init(&g_state) < 0) {
        sg_log(LOG_ERR, "synguard: init failed");
        return 1;
    }

    /* Connect to synapd */
    if (g_state.config.ai_enabled) {
        if (sg_synapd_connect(&g_state) < 0) {
            sg_log(LOG_WARNING, "synguard: synapd not available — AI classification disabled");
            g_state.config.ai_enabled = 0;
        }
    }

    /* Start event reader thread */
    if (kmod_reader_start(&g_state) < 0) {
        sg_log(LOG_WARNING, "synguard: kmod reader failed — running without kernel events");
    }

    /* Banner LAST, not first.
     *
     * It used to print here-minus-two-steps, before either of the facts it
     * reports had been established. kmod_present is assigned in exactly one
     * place — kmod_reader_start(), below — so the banner read it while it was
     * still the zero from synguard_init() and announced "kmod=absent
     * (degraded)" on every boot ever, including the boots where the module was
     * loaded and the reader went on to drain millions of events from it. The
     * one line an operator actually reads said monitoring was dead while it
     * was working.
     *
     * ai= had the same shape: printed before sg_synapd_connect() could fail
     * and turn ai_enabled back off, so it could claim ai=on for a daemon that
     * had just given up on synapd.
     *
     * A startup banner is a report, so it goes after the things it reports on.
     * Anything that needs to announce itself earlier should log for itself. */
    print_banner(&g_state);

    /*
     * Tell systemd we are up.
     *
     * This used to be system("systemd-notify READY=1"), which is wrong twice
     * over. It runs a shell out of a root daemon holding the whole capability
     * set, purely to send one datagram. And the datagram then comes from a
     * grandchild (sh -> systemd-notify), while the unit says
     * NotifyAccess=main — so the readiness only ever landed by the grace of
     * whatever sandboxing was NOT in effect. Tightening the unit
     * (CapabilityBoundingSet, seccomp-installing directives) broke the helper
     * and the service sat in "activating" until TimeoutStartSec killed it,
     * with the daemon itself running fine the whole time.
     *
     * sd_notify() sends it from this process, which is the main PID, so it
     * satisfies NotifyAccess=main by construction and has no helper to break.
     */
    sd_notify(0, "READY=1");

    int ret = run_event_loop(&g_state);

    sg_log(LOG_INFO, "synguard: shutting down");
    baseline_save(&g_state);
    audit_close(&g_state);
    sg_synapd_disconnect(&g_state);
    synguard_destroy(&g_state);
    closelog();

    return ret;
}
