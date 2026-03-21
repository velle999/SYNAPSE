/*
 * scheduler.c — Kernel AI scheduler bridge
 *
 * Writes scheduling hints and daemon status to the sysfs
 * interface exposed by synapse_kmod (/sys/kernel/synapse/).
 *
 * If synapse_kmod is not loaded, this subsystem will attempt
 * to load it via modprobe and periodically retry connecting.
 * synapd continues to work in userspace-only mode if the
 * kernel module remains unavailable.
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
#include <sys/wait.h>

#include "synapd.h"
#include "scheduler.h"
#include "log.h"

/* How often to retry connecting if kmod was not available (seconds) */
#define KMOD_RETRY_INTERVAL 30

/* ── Try to modprobe the kernel module ───────────────────── */
static int try_load_kmod(void) {
    syn_log(LOG_INFO, "scheduler: attempting modprobe synapse_kmod");
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* Child — try modprobe silently */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execl("/usr/bin/modprobe", "modprobe", "synapse_kmod", NULL);
        execl("/sbin/modprobe", "modprobe", "synapse_kmod", NULL);
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* ── Try to connect to the kmod sysfs interface ──────────── */
static int try_connect_kmod(synapd_state_t *s) {
    synapd_scheduler_t *sch = &s->scheduler;

    struct stat st;
    if (stat(SYNAPD_SYSFS_PATH, &st) < 0)
        return -1;

    /* Open hints file for writing */
    int fd = open(SYNAPD_SYSFS_HINTS, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        syn_log(LOG_WARNING, "scheduler: cannot open %s: %s",
                 SYNAPD_SYSFS_HINTS, strerror(errno));
        return -1;
    }

    sch->sysfs_fd     = fd;
    sch->kmod_present = 1;
    syn_log(LOG_INFO, "scheduler: connected to synapse_kmod sysfs interface");

    scheduler_write_status(s, "READY");
    return 0;
}

/* ── Init ─────────────────────────────────────────────────── */
int scheduler_init(synapd_state_t *s) {
    synapd_scheduler_t *sch = &s->scheduler;
    sch->sysfs_fd       = -1;
    sch->kmod_present   = 0;
    sch->last_heartbeat = 0;
    sch->last_retry     = 0;

    /* Try connecting to already-loaded kmod */
    if (try_connect_kmod(s) == 0)
        return 0;

    /* Not loaded yet — attempt modprobe */
    if (try_load_kmod() == 0) {
        /* Give the module a moment to create sysfs entries */
        usleep(500000);
        if (try_connect_kmod(s) == 0)
            return 0;
    }

    syn_log(LOG_INFO, "scheduler: synapse_kmod not available — "
                       "running in userspace-only mode (will retry every %ds)",
                       KMOD_RETRY_INTERVAL);
    return -1;  /* non-fatal */
}

/* ── Heartbeat (called every second from main loop) ──────── */
void scheduler_heartbeat(synapd_state_t *s) {
    synapd_scheduler_t *sch = &s->scheduler;
    time_t now = time(NULL);

    /* If kmod not connected, periodically retry */
    if (!sch->kmod_present) {
        if (now - sch->last_retry < KMOD_RETRY_INTERVAL)
            return;
        sch->last_retry = now;

        if (try_connect_kmod(s) == 0) {
            syn_log(LOG_INFO, "scheduler: synapse_kmod became available — connected");
            return;
        }
        /* Still not available — stay quiet, try again later */
        return;
    }

    /* Normal heartbeat every 5 seconds */
    if (now - sch->last_heartbeat < 5) return;
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
        /* sysfs write failed — kmod may have been unloaded */
        syn_log(LOG_WARNING, "scheduler: sysfs write failed: %s — "
                              "marking kmod disconnected", strerror(errno));
        close(sch->sysfs_fd);
        sch->sysfs_fd     = -1;
        sch->kmod_present = 0;
        return -1;
    }
    return 0;
}

/* ── Write status string ──────────────────────────────────── */
void scheduler_write_status(synapd_state_t *s, const char *status) {
    int fd = open(SYNAPD_SYSFS_STATUS, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        /* sysfs gone — kmod was unloaded */
        if (s->scheduler.kmod_present) {
            syn_log(LOG_WARNING, "scheduler: lost connection to synapse_kmod");
            if (s->scheduler.sysfs_fd >= 0)
                close(s->scheduler.sysfs_fd);
            s->scheduler.sysfs_fd     = -1;
            s->scheduler.kmod_present = 0;
        }
        return;
    }
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
