/*
 * action_engine.c — Security action enforcement
 *
 * Implements the three enforcement actions:
 *
 *   DENY       → kill the process and its descendant subtree
 *                (isolation.c::sg_kill_tree)
 *   ALERT      → Log to audit, emit structured alert to any
 *                connected 'syn guard watch' clients
 *   QUARANTINE → freeze the process subtree via SIGSTOP + cgroup v2
 *                (isolation.c::sg_freeze_tree); not killed, admin can inspect
 *
 * Both DENY and QUARANTINE run through the sg_is_protected() guard, so they
 * never touch PID 0/1, kernel threads, synguard itself, or core SynapseOS
 * daemons.
 *
 * In AUDIT and LEARNING modes, DENY becomes a logged warning.
 * Only ENFORCE and LOCKDOWN modes actually kill processes.
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
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "synguard.h"
#include "sg_log.h"

/* ── DENY: kill the offending process and its descendants ─── */
/*
 * The actual termination (protected-pid guard, stale-pid guard, subtree
 * teardown, kill verification) lives in isolation.c::sg_kill_tree(). It
 * refuses and returns -1 if the target is protected, or if e->pid is no
 * longer the process e->comm names — by the time a verdict is acted on the
 * event is up to poll_interval_ms old and the pid may have been recycled. On
 * a refusal we do nothing further (no kmod hint), so neither a critical pid
 * nor a bystander that inherited the pid is ever touched.
 */
void action_deny(synguard_state_t *s, const sg_event_t *e, const char *reason)
{
    if (sg_kill_tree(s, (pid_t)e->pid, e->comm, reason) < 0)
        return;

    /* Write a hint to kmod so it can track the kill */
    char hint[128];
    snprintf(hint, sizeof(hint), "HINT pid=%u nice=19 class=idle\n", e->pid);
    int fd = open(KMOD_AI_HINTS, O_WRONLY);
    if (fd >= 0) {
        write(fd, hint, strlen(hint));
        close(fd);
    }
}

/* ── Repeat aggregation ───────────────────────────────────── */
/*
 * Alert on the first of a kind, then count the repeats and summarise them.
 *
 * One boot produced ~1,050 alerts that were all the system proving its own
 * identity: 319 systemd-userwork and 279 unix_chkpwd opening /etc/shadow, 448
 * sudo opening /etc/sudoers, 227 sudo setuid-ing to root. Every one is a true
 * positive, so silencing them with an `allow` rule would be lying, and
 * narrowing the rules by comm would trade real detection for quiet: a process
 * that can read /etc/shadow is already privileged and can call
 * prctl(PR_SET_NAME) to borrow any name on the exempt list.
 *
 * Aggregating costs nothing in detection. Every event is still evaluated,
 * still audited, and the FIRST of any (comm, reason) pair still alerts
 * immediately — so anything genuinely new surfaces at once. Only repetition
 * is collapsed.
 *
 * CRITICAL is never aggregated. "Monitoring may be blinded" repeated forty
 * times is forty separate facts about the system, not one fact restated.
 */
#define ALERT_AGG_SLOTS    64
#define ALERT_AGG_WINDOW   60      /* seconds */

struct alert_agg {
    char      comm[16];
    char      reason[128];
    uint32_t  count;               /* occurrences INCLUDING the one alerted */
    time_t    first;
    int       used;
};

static struct alert_agg agg_tab[ALERT_AGG_SLOTS];
static pthread_mutex_t  agg_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Returns 1 if this alert should be emitted now, 0 if it is a repeat being
 * counted. On a full table it returns 1 — degrade to noisy, never to silent.
 */
static int alert_should_emit(const sg_alert_t *alert)
{
    if (alert->threat >= THREAT_CRITICAL)
        return 1;

    time_t now = time(NULL);
    int free_slot = -1;
    int emit = 1;

    pthread_mutex_lock(&agg_lock);

    for (int i = 0; i < ALERT_AGG_SLOTS; i++) {
        if (!agg_tab[i].used) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (strcmp(agg_tab[i].comm, alert->event.comm) == 0 &&
            strncmp(agg_tab[i].reason, alert->reason,
                    sizeof(agg_tab[i].reason) - 1) == 0) {
            /* Window expired: let this one through and start a new window.
             * The flush below will already have reported the old count. */
            if (now - agg_tab[i].first >= ALERT_AGG_WINDOW) {
                agg_tab[i].first = now;
                agg_tab[i].count = 1;
                emit = 1;
            } else {
                agg_tab[i].count++;
                emit = 0;
            }
            pthread_mutex_unlock(&agg_lock);
            return emit;
        }
    }

    if (free_slot >= 0) {
        agg_tab[free_slot].used  = 1;
        agg_tab[free_slot].count = 1;
        agg_tab[free_slot].first = now;
        strncpy(agg_tab[free_slot].comm, alert->event.comm,
                sizeof(agg_tab[free_slot].comm) - 1);
        strncpy(agg_tab[free_slot].reason, alert->reason,
                sizeof(agg_tab[free_slot].reason) - 1);
    }

    pthread_mutex_unlock(&agg_lock);
    return 1;
}

/*
 * Emit "N more" lines for windows that have closed, and free their slots.
 * Called from the main loop; safe to call as often as you like.
 */
void alert_flush_summaries(synguard_state_t *s)
{
    time_t now = time(NULL);

    pthread_mutex_lock(&agg_lock);
    for (int i = 0; i < ALERT_AGG_SLOTS; i++) {
        if (!agg_tab[i].used)                       continue;
        if (now - agg_tab[i].first < ALERT_AGG_WINDOW) continue;

        if (agg_tab[i].count > 1) {
            sg_log(LOG_WARNING,
                   "⚠  ALERT ×%u in %llds — (%s) reason=%s "
                   "[%u repeats suppressed]",
                   agg_tab[i].count,
                   (long long)(now - agg_tab[i].first),
                   agg_tab[i].comm, agg_tab[i].reason,
                   agg_tab[i].count - 1);
            s->stats.alerts_suppressed += agg_tab[i].count - 1;
        }
        agg_tab[i].used = 0;
    }
    pthread_mutex_unlock(&agg_lock);
}

/* ── ALERT: structured alert to journal and clients ──────── */
void action_alert(synguard_state_t *s, const sg_alert_t *alert)
{
    const sg_event_t *e = &alert->event;

    /* Repeats are counted and summarised by alert_flush_summaries(). The
     * event has already been counted in stats and written to the audit log by
     * the caller — only the journal line and the client broadcast are
     * suppressed. */
    if (!alert_should_emit(alert))
        return;

    static const char *threat_names[] = {
        [THREAT_NONE]     = "NONE",
        [THREAT_LOW]      = "LOW",
        [THREAT_MEDIUM]   = "MEDIUM",
        [THREAT_HIGH]     = "HIGH",
        [THREAT_CRITICAL] = "CRITICAL",
    };

    const char *tname = alert->threat < 5 ? threat_names[alert->threat] : "?";

    /* Log to syslog */
    if (alert->threat >= THREAT_HIGH)
        sg_log(LOG_CRIT,
            "🚨 ALERT [%s] pid=%u (%s) evt=%02x file=%s reason=%s",
            tname, e->pid, e->comm, e->evt_type,
            e->filename[0] ? e->filename : "-",
            alert->reason);
    else
        sg_log(LOG_WARNING,
            "⚠  ALERT [%s] pid=%u (%s) evt=%02x file=%s reason=%s",
            tname, e->pid, e->comm, e->evt_type,
            e->filename[0] ? e->filename : "-",
            alert->reason);

    /* Broadcast to subscribers (e.g. synui colours the offending window). */
    secfeed_publish(alert);

    /*
     * Also write a structured JSON line to stderr in debug so sinks can
     * parse it.
     */
    if (s->debug) {
        fprintf(stderr,
            "{\"type\":\"alert\",\"threat\":\"%s\","
            "\"pid\":%u,\"comm\":\"%s\","
            "\"evt\":%u,\"file\":\"%s\","
            "\"reason\":\"%s\",\"action\":\"%s\"}\n",
            tname, e->pid, e->comm,
            e->evt_type,
            e->filename[0] ? e->filename : "",
            alert->reason,
            alert->action_taken[0] ? alert->action_taken : "alert"
        );
    }
}

/* ── QUARANTINE: SIGSTOP + cgroup isolation ───────────────── */
/*
 * Quarantine freezes the process in place without killing it.
 * A forensic admin can then:
 *   - Inspect /proc/<pid>/
 *   - Attach gdb or strace
 *   - Resume or kill it
 *
 * We write the process to a synguard cgroup (if cgroupv2 is mounted)
 * to limit any further resource consumption while frozen.
 */
void action_quarantine(synguard_state_t *s, const sg_event_t *e)
{
    sg_log(LOG_WARNING,
           "🔒 QUARANTINE: freezing pid=%u (%s)",
           e->pid, e->comm);

    /*
     * Freeze the process and its whole subtree (protected-pid guard,
     * SIGSTOP, cgroup v2 freeze) in isolation.c::sg_freeze_tree(). If the
     * target is protected it returns -1 and we leave no forensic note.
     */
    if (sg_freeze_tree(s, (pid_t)e->pid, e->comm) < 0)
        return;

    sg_log(LOG_INFO, "quarantine: pid=%u frozen. Resume with: kill -CONT %u "
           "(and clear cgroup.freeze if set)", e->pid, e->pid);

    /* Write a "quarantine note" to /var/lib/synguard/ for admin reference */
    char note_path[256];
    snprintf(note_path, sizeof(note_path),
             "/var/lib/synguard/quarantine_%u.txt", e->pid);
    FILE *f = fopen(note_path, "w");
    if (f) {
        char tbuf[64];
        time_t now = time(NULL);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        fprintf(f,
            "synguard quarantine report\n"
            "timestamp: %s\n"
            "pid:       %u\n"
            "comm:      %s\n"
            "uid:       %u\n"
            "event:     0x%02x\n"
            "file:      %s\n"
            "status:    FROZEN (subtree SIGSTOP + cgroup.freeze)\n"
            "cgroup:    /sys/fs/cgroup/synguard.quarantine.%u\n"
            "resume:    echo 0 > /sys/fs/cgroup/synguard.quarantine.%u/cgroup.freeze && kill -CONT %u\n"
            "kill:      kill -9 %u\n",
            tbuf, e->pid, e->comm, e->uid,
            e->evt_type, e->filename[0] ? e->filename : "-",
            /* cgroup:, resume: (twice), kill: — four pids, not three. */
            e->pid, e->pid, e->pid, e->pid
        );
        fclose(f);
        sg_log(LOG_INFO, "quarantine: report at %s", note_path);
    }
}
