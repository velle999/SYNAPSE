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
 * SynapseOS Project — GPLv2
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
#include <sys/stat.h>
#include <sys/types.h>

#include "synguard.h"
#include "sg_log.h"

/* ── DENY: kill the offending process and its descendants ─── */
/*
 * The actual termination (protected-pid guard, subtree teardown, kill
 * verification) lives in isolation.c::sg_kill_tree(). If the target is
 * protected, sg_kill_tree() refuses and returns -1; we do nothing further
 * (no kmod hint) so a spoofed/critical pid is never touched.
 */
void action_deny(synguard_state_t *s, const sg_event_t *e, const char *reason)
{
    if (sg_kill_tree(s, (pid_t)e->pid, reason) < 0)
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

/* ── ALERT: structured alert to journal and clients ──────── */
void action_alert(synguard_state_t *s, const sg_alert_t *alert)
{
    const sg_event_t *e = &alert->event;

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
    if (sg_freeze_tree(s, (pid_t)e->pid) < 0)
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
            e->pid, e->pid, e->pid
        );
        fclose(f);
        sg_log(LOG_INFO, "quarantine: report at %s", note_path);
    }
}
