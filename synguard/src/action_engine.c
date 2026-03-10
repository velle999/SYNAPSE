/*
 * action_engine.c — Security action enforcement
 *
 * Implements the three enforcement actions:
 *
 *   DENY       → SIGKILL the process immediately
 *   ALERT      → Log to audit, emit structured alert to any
 *                connected 'syn guard watch' clients
 *   QUARANTINE → SIGSTOP + write to cgroup to isolate resources
 *                (process is frozen, not killed; admin can inspect)
 *
 * In AUDIT and LEARNING modes, DENY becomes a logged warning.
 * Only ENFORCE and LOCKDOWN modes actually kill processes.
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
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

/* ── DENY: SIGKILL ────────────────────────────────────────── */
void action_deny(synguard_state_t *s, const sg_event_t *e, const char *reason)
{
    sg_log(LOG_WARNING,
           "⚡ DENY: killing pid=%u (%s) — %s",
           e->pid, e->comm, reason);

    if (kill((pid_t)e->pid, SIGKILL) < 0) {
        if (errno == ESRCH)
            sg_log(LOG_INFO, "deny: pid=%u already gone", e->pid);
        else
            sg_log(LOG_WARNING, "deny: kill(%u): %s", e->pid, strerror(errno));
    } else {
        sg_log(LOG_INFO, "deny: SIGKILL sent to pid=%u (%s)", e->pid, e->comm);
    }

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

    /*
     * In a full implementation: broadcast to connected 'syn guard watch'
     * clients via the IPC socket server. For now write a structured
     * JSON line to stderr so sinks can parse it.
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

    /* SIGSTOP freezes the process */
    if (kill((pid_t)e->pid, SIGSTOP) < 0) {
        if (errno == ESRCH)
            sg_log(LOG_INFO, "quarantine: pid=%u already gone", e->pid);
        else
            sg_log(LOG_WARNING, "quarantine: SIGSTOP(%u): %s",
                   e->pid, strerror(errno));
        return;
    }

    sg_log(LOG_INFO, "quarantine: pid=%u frozen. Resume with: kill -CONT %u",
           e->pid, e->pid);

    /* Try cgroupv2 isolation */
    char cg_path[256];
    snprintf(cg_path, sizeof(cg_path),
             "/sys/fs/cgroup/synguard/quarantine_%u", e->pid);

    if (mkdir("/sys/fs/cgroup/synguard", 0755) < 0 && errno != EEXIST)
        goto cg_skip;
    if (mkdir(cg_path, 0755) < 0 && errno != EEXIST)
        goto cg_skip;

    /* Move process to quarantine cgroup */
    char cg_procs[300];
    snprintf(cg_procs, sizeof(cg_procs), "%s/cgroup.procs", cg_path);
    int fd = open(cg_procs, O_WRONLY);
    if (fd >= 0) {
        char pidstr[16];
        snprintf(pidstr, sizeof(pidstr), "%u\n", e->pid);
        write(fd, pidstr, strlen(pidstr));
        close(fd);
        sg_log(LOG_INFO, "quarantine: pid=%u moved to cgroup %s", e->pid, cg_path);
    }

cg_skip:
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
            "status:    FROZEN (SIGSTOP)\n"
            "resume:    kill -CONT %u\n"
            "kill:      kill -9 %u\n",
            tbuf, e->pid, e->comm, e->uid,
            e->evt_type, e->filename[0] ? e->filename : "-",
            e->pid, e->pid
        );
        fclose(f);
        sg_log(LOG_INFO, "quarantine: report at %s", note_path);
    }
}
