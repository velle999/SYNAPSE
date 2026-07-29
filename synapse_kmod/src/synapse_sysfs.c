/*
 * synapse_sysfs.c — Sysfs interface for synapse_kmod
 *
 * Exposes /sys/kernel/synapse/ with these files:
 *
 *   status       (rw) — daemon writes heartbeat strings
 *   ai_hints     (w)  — daemon writes "HINT pid=N nice=N class=X\n"
 *   syscall_log  (r)  — userspace reads captured syscall events
 *   stats        (r)  — module counters
 *   config       (rw) — runtime config (events_enabled, sched_enabled)
 *   version      (r)  — module version string
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 * https://github.com/velle999/SYNAPSE
 */

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/ktime.h>
#include <linux/version.h>

#include "synapse_kmod.h"
#include "synapse_sysfs.h"
#include "synapse_sched.h"
#include "synapse_probe.h"

extern void synapse_daemon_heartbeat(void);
extern void synapse_daemon_shutdown(void);
extern void synapse_get_stats(struct synapse_stats *);
extern bool synapse_events_enabled(void);
extern bool synapse_sched_enabled(void);
extern int  synapse_kmod_set_pinned(bool pin);
extern bool synapse_kmod_is_pinned(void);
extern u64  synapse_integrity_alert_count(void);

/* ── /sys/kernel/synapse/status ──────────────────────────── */
/*
 * synapd writes: "ALIVE requests=N active=N model=1\n"
 *                "READY\n"
 *                "SHUTDOWN\n"
 *
 * Reading returns the last status string written.
 */
static char  status_buf[SYNAPSE_STATUS_MAX_LEN];
static DEFINE_SPINLOCK(status_lock);

static ssize_t status_show(struct kobject *kobj,
                             struct kobj_attribute *attr,
                             char *buf)
{
    ssize_t len;
    spin_lock(&status_lock);
    len = scnprintf(buf, PAGE_SIZE, "%s\n", status_buf);
    spin_unlock(&status_lock);
    return len;
}

static ssize_t status_store(struct kobject *kobj,
                              struct kobj_attribute *attr,
                              const char *buf, size_t count)
{
    size_t len = min(count, (size_t)(SYNAPSE_STATUS_MAX_LEN - 1));

    spin_lock(&status_lock);
    memcpy(status_buf, buf, len);
    status_buf[len] = '\0';
    /* Strip trailing newline for clean storage */
    if (len > 0 && status_buf[len-1] == '\n')
        status_buf[len-1] = '\0';
    spin_unlock(&status_lock);

    /* Parse and dispatch */
    if (strncmp(buf, "ALIVE", 5) == 0) {
        synapse_daemon_heartbeat();
    } else if (strncmp(buf, "READY", 5) == 0) {
        synapse_daemon_heartbeat();
        pr_info("synapse_kmod: synapd ready\n");
    } else if (strncmp(buf, "SHUTDOWN", 8) == 0) {
        synapse_daemon_shutdown();
    }

    return (ssize_t)count;
}

/* 0644, not 0664: status is world-readable (status tools) but only root
 * (the file owner) may write the heartbeat — no group-write surface. */
static struct kobj_attribute status_attr =
    __ATTR(status, 0644, status_show, status_store);

/* ── /sys/kernel/synapse/ai_hints ────────────────────────── */
/*
 * Write-only from userspace. synapd writes:
 *   "HINT pid=<pid> nice=<delta> class=<name>\n"
 *
 * Multiple hints can be written in one write() call, one per line.
 *
 * class names:
 *   normal, interactive, batch, realtime, idle, inference
 */
static ai_sched_class_t parse_sched_class(const char *name)
{
    if (strcmp(name, "interactive") == 0) return AI_SCHED_INTERACTIVE;
    if (strcmp(name, "batch")       == 0) return AI_SCHED_BATCH;
    if (strcmp(name, "realtime")    == 0) return AI_SCHED_REALTIME;
    if (strcmp(name, "idle")        == 0) return AI_SCHED_IDLE;
    if (strcmp(name, "inference")   == 0) return AI_SCHED_INFERENCE;
    return AI_SCHED_NORMAL;
}

static void process_hint_line(const char *line)
{
    int pid = 0, nice = 0;
    char class_str[32] = "normal";

    /*
     * Parse: "HINT pid=N nice=N class=X"
     * sscanf is safe here — we're in a sysfs write handler,
     * single-threaded via the sysfs lock, with bounded input.
     */
    if (sscanf(line, "HINT pid=%d nice=%d class=%31s",
               &pid, &nice, class_str) < 2) {
        pr_debug("synapse_kmod: malformed hint: %.80s\n", line);
        return;
    }

    if (pid <= 0) return;
    if (nice < -20) nice = -20;
    if (nice >  19) nice =  19;

    ai_sched_class_t cls = parse_sched_class(class_str);

    pr_debug("synapse_kmod: hint pid=%d nice=%d class=%d\n",
             pid, nice, (int)cls);

    synapse_sched_apply_hint((pid_t)pid, nice, cls);
}

static ssize_t ai_hints_store(struct kobject *kobj,
                               struct kobj_attribute *attr,
                               const char *buf, size_t count)
{
    /* Process each line */
    char *copy = kstrndup(buf, count, GFP_KERNEL);
    if (!copy) return -ENOMEM;

    char *line, *rest = copy;
    while ((line = strsep(&rest, "\n")) != NULL) {
        if (*line && strncmp(line, "HINT", 4) == 0)
            process_hint_line(line);
    }

    kfree(copy);
    return (ssize_t)count;
}

/* 0200, not 0220: write-only, owner (root) only — drop the group-write bit. */
static struct kobj_attribute ai_hints_attr =
    __ATTR(ai_hints, 0200, NULL, ai_hints_store);

/* ── /sys/kernel/synapse/syscall_log ─────────────────────── */
/*
 * Read-only, and NON-DESTRUCTIVE: a bounded peek at the newest events for a
 * human with a shell. Reading it consumes nothing.
 *
 * It used to advance the ring's shared tail, which made `cat`ting this file
 * steal events from synguard — an operational footgun and a post-root way to
 * blind the detector. Consumers now use /dev/synapse-events, where every open
 * has its own cursor. See synapse_evchr.c.
 *
 * Format: "TS_NS PID UID SYSCALL_NR COMM FILENAME|- FLAGS ARG0\n", where
 * FLAGS is the SYNAPSE_EVT_* event type and ARG0 is the first syscall
 * argument (openat: O_* flags; setuid: target uid). Fields only ever append.
 *
 * The consumer is synguard, not synapd — synapd never reads this.
 */
static ssize_t syscall_log_show(struct kobject *kobj,
                                  struct kobj_attribute *attr,
                                  char *buf)
{
    return synapse_probe_read_log(buf, PAGE_SIZE);
}

static struct kobj_attribute syscall_log_attr =
    __ATTR(syscall_log, 0440, syscall_log_show, NULL);

/* ── /sys/kernel/synapse/stats ───────────────────────────── */
static ssize_t stats_show(struct kobject *kobj,
                            struct kobj_attribute *attr,
                            char *buf)
{
    struct synapse_stats st;
    synapse_get_stats(&st);

    return scnprintf(buf, PAGE_SIZE,
        "events_captured=%llu\n"
        "hints_applied=%llu\n"
        "hints_rejected=%llu\n"
        "syscalls_hooked=%llu\n"
        "ai_queries_routed=%llu\n"
        "daemon_heartbeats=%llu\n"
        "daemon_timeouts=%llu\n"
        "active_contexts=%u\n"
        "integrity_alerts=%llu\n"
        "lockdown=%d\n"
        "kmod_version=0x%08x\n",
        st.events_captured,
        st.hints_applied,
        st.hints_rejected,
        st.syscalls_hooked,
        st.ai_queries_routed,
        st.daemon_heartbeats,
        st.daemon_timeouts,
        st.active_contexts,
        synapse_integrity_alert_count(),
        synapse_kmod_is_pinned() ? 1 : 0,
        st.kmod_version
    );
}

static struct kobj_attribute stats_attr =
    __ATTR(stats, 0444, stats_show, NULL);

/* ── /sys/kernel/synapse/config ──────────────────────────── */
static ssize_t config_show(struct kobject *kobj,
                             struct kobj_attribute *attr,
                             char *buf)
{
    return scnprintf(buf, PAGE_SIZE,
        "events_enabled=%d\n"
        "sched_enabled=%d\n",
        synapse_events_enabled() ? 1 : 0,
        synapse_sched_enabled()  ? 1 : 0
    );
}

static ssize_t config_store(struct kobject *kobj,
                              struct kobj_attribute *attr,
                              const char *buf, size_t count)
{
    /* Simple key=value parser */
    int val;
    if (sscanf(buf, "events_enabled=%d", &val) == 1) {
        /* Disabling event capture blinds the whole security monitor.
         * That must never happen quietly — log it at warning level. */
        if (val == 0)
            pr_warn("synapse_kmod: SECURITY: syscall event capture DISABLED "
                    "via sysfs — monitoring is now blind\n");
        else
            pr_info("synapse_kmod: events_enabled → %d\n", val);
        synapse_probe_set_enabled(val != 0);
    } else if (sscanf(buf, "sched_enabled=%d", &val) == 1) {
        pr_info("synapse_kmod: sched_enabled → %d\n", val);
        synapse_sched_set_enabled(val != 0);
    }
    return (ssize_t)count;
}

/* 0644, not 0664: config is world-readable but only root may flip the knobs. */
static struct kobj_attribute config_attr =
    __ATTR(config, 0644, config_show, config_store);

/* ── /sys/kernel/synapse/lockdown ────────────────────────────
 * Anti-hijack self-pin toggle. "1" pins the module so rmmod is refused;
 * "0" unpins. Reading returns "lockdown=<0|1>". Owner(root)-write only. */
static ssize_t lockdown_show(struct kobject *kobj,
                             struct kobj_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "lockdown=%d\n",
                     synapse_kmod_is_pinned() ? 1 : 0);
}

static ssize_t lockdown_store(struct kobject *kobj,
                              struct kobj_attribute *attr,
                              const char *buf, size_t count)
{
    int val, rc;
    if (sscanf(buf, "%d", &val) != 1)
        return -EINVAL;
    rc = synapse_kmod_set_pinned(val != 0);
    return rc ? rc : (ssize_t)count;
}

static struct kobj_attribute lockdown_attr =
    __ATTR(lockdown, 0640, lockdown_show, lockdown_store);

/* ── /sys/kernel/synapse/version ─────────────────────────── */
static ssize_t version_show(struct kobject *kobj,
                              struct kobj_attribute *attr,
                              char *buf)
{
    return scnprintf(buf, PAGE_SIZE,
        "synapse_kmod %s (SynapseOS)\nkernel %s\n",
        SYNAPSE_KMOD_VERSION, utsname()->release);
}

static struct kobj_attribute version_attr =
    __ATTR(version, 0444, version_show, NULL);

/* ── /sys/kernel/synapse/sensitive_paths ─────────────────── */
/*
 * One path prefix per line: the complete set of opens the kmod will report.
 * synguard reads this at rule-load time so it can say out loud when an
 * `event open` rule names a path the kernel will never tell it about — a rule
 * that loads, counts as enforceable, and silently can never match. World
 * readable: it is a static compile-time list, and knowing which paths are
 * watched is not the secret worth keeping (the rules are the secret, and they
 * are 0640).
 */
static ssize_t sensitive_paths_show(struct kobject *kobj,
                                    struct kobj_attribute *attr,
                                    char *buf)
{
    ssize_t len = 0;
    int i;

    for (i = 0; synapse_sensitive_paths[i]; i++) {
        /* Never let the last entry run off the end of the page. */
        if (len >= PAGE_SIZE - 1)
            break;
        len += scnprintf(buf + len, PAGE_SIZE - len, "%s\n",
                         synapse_sensitive_paths[i]);
    }
    return len;
}

static struct kobj_attribute sensitive_paths_attr =
    __ATTR(sensitive_paths, 0444, sensitive_paths_show, NULL);

/* ── Attribute group ──────────────────────────────────────── */
static struct attribute *synapse_attrs[] = {
    &status_attr.attr,
    &ai_hints_attr.attr,
    &syscall_log_attr.attr,
    &stats_attr.attr,
    &config_attr.attr,
    &lockdown_attr.attr,
    &version_attr.attr,
    &sensitive_paths_attr.attr,
    NULL,
};

static const struct attribute_group synapse_attr_group = {
    .attrs = synapse_attrs,
};

/* ── Public init / exit ───────────────────────────────────── */
int synapse_sysfs_init(struct kobject **kobj_out)
{
    struct kobject *kobj;
    int ret;

    /*
     * Create /sys/kernel/synapse/
     * kernel_kobj is the /sys/kernel kobject.
     */
    kobj = kobject_create_and_add("synapse", kernel_kobj);
    if (!kobj) {
        pr_err("synapse_kmod: kobject_create_and_add failed\n");
        return -ENOMEM;
    }

    ret = sysfs_create_group(kobj, &synapse_attr_group);
    if (ret) {
        pr_err("synapse_kmod: sysfs_create_group failed: %d\n", ret);
        kobject_put(kobj);
        return ret;
    }

    *kobj_out = kobj;

    /* Write initial status */
    strncpy(status_buf, "MODULE_LOADED", sizeof(status_buf) - 1);

    return 0;
}

void synapse_sysfs_exit(struct kobject *kobj)
{
    if (kobj) {
        sysfs_remove_group(kobj, &synapse_attr_group);
        kobject_put(kobj);
    }
}
