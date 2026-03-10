/*
 * scheduler.c — Kernel AI scheduler bridge
 *
 * Writes scheduling hints and daemon status to the sysfs
 * interface exposed by synapse_kmod (/sys/kernel/synapse/).
 *
 * If synapse_kmod is not loaded, this subsystem degrades
 * gracefully — synapd continues to work, just without
 * kernel-level scheduling integration.
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>

#include "synapd.h"
#include "scheduler.h"
#include "log.h"

/* ── Init ─────────────────────────────────────────────────── */
int scheduler_init(synapd_state_t *s) {
    synapd_scheduler_t *sch = &s->scheduler;
    sch->sysfs_fd    = -1;
    sch->kmod_present = 0;

    /* Check if synapse_kmod is loaded */
    struct stat st;
    if (stat(SYNAPD_SYSFS_PATH, &st) < 0) {
        syn_log(LOG_INFO, "scheduler: %s not found — synapse_kmod not loaded", SYNAPD_SYSFS_PATH);
        syn_log(LOG_INFO, "scheduler: running in userspace-only mode");
        return -1;  /* non-fatal */
    }

    /* Open hints file for writing */
    sch->sysfs_fd = open(SYNAPD_SYSFS_HINTS, O_WRONLY | O_CLOEXEC);
    if (sch->sysfs_fd < 0) {
        syn_log(LOG_WARNING, "scheduler: cannot open %s: %s",
                 SYNAPD_SYSFS_HINTS, strerror(errno));
        return -1;
    }

    sch->kmod_present = 1;
    syn_log(LOG_INFO, "scheduler: connected to synapse_kmod sysfs interface");

    scheduler_write_status(s, "READY");
    return 0;
}

/* ── Heartbeat (called every second from main loop) ──────── */
void scheduler_heartbeat(synapd_state_t *s) {
    synapd_scheduler_t *sch = &s->scheduler;
    if (!sch->kmod_present) return;

    time_t now = time(NULL);
    if (now - sch->last_heartbeat < 5) return;  /* every 5 seconds */
    sch->last_heartbeat = now;

    char buf[128];
    snprintf(buf, sizeof(buf),
        "ALIVE requests=%lu active=%lu model=%s\n",
        (unsigned long)atomic_load(&s->requests_total),
        (unsigned long)atomic_load(&s->requests_active),
        s->model_loaded ? "1" : "0"
    );
    scheduler_write_status(s, buf);
}

/* ── Write scheduling hint for a PID ─────────────────────── */
/*
 * Format written to /sys/kernel/synapse/ai_hints:
 *   "HINT pid=<pid> nice=<delta> class=<class>\n"
 *
 * synapse_kmod reads this and adjusts the process's scheduling
 * parameters via kernel-internal calls.
 */
int scheduler_write_hint(synapd_state_t *s, pid_t pid,
                          int nice_delta, const char *sched_class)
{
    synapd_scheduler_t *sch = &s->scheduler;
    if (!sch->kmod_present || sch->sysfs_fd < 0) return -1;

    char buf[128];
    int n = snprintf(buf, sizeof(buf),
        "HINT pid=%d nice=%d class=%s\n",
        (int)pid, nice_delta,
        sched_class ? sched_class : "normal"
    );

    ssize_t w = write(sch->sysfs_fd, buf, n);
    if (w < 0) {
        syn_log(LOG_WARNING, "scheduler: sysfs write failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* ── Write status string ──────────────────────────────────── */
void scheduler_write_status(synapd_state_t *s, const char *status) {
    int fd = open(SYNAPD_SYSFS_STATUS, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;
    write(fd, status, strlen(status));
    close(fd);
}

/* ── Destroy ──────────────────────────────────────────────── */
void scheduler_destroy(synapd_state_t *s) {
    synapd_scheduler_t *sch = &s->scheduler;
    if (sch->kmod_present)
        scheduler_write_status(s, "SHUTDOWN");
    if (sch->sysfs_fd >= 0) {
        close(sch->sysfs_fd);
        sch->sysfs_fd = -1;
    }
    sch->kmod_present = 0;
}
