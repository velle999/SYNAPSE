/*
 * synapse_main.c — SynapseOS Kernel Module
 *
 * Entry point for synapse_kmod. Registers:
 *   - Kobject/sysfs at /sys/kernel/synapse/
 *   - kprobes on security-relevant syscalls
 *   - AI_CTX syscall handlers (Linux 7.0 extension points)
 *   - AI scheduling class within CFS
 *
 * Load:  modprobe synapse_kmod
 * Unload: modprobe -r synapse_kmod
 *
 * Dependencies:
 *   - Linux kernel 6.8+ (kprobe multi, sysfs attrs)
 *   - Linux 7.0 strongly preferred (AI_CTX syscall hooks)
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/version.h>

#include "synapse_kmod.h"
#include "synapse_sysfs.h"
#include "synapse_probe.h"
#include "synapse_sched.h"
#include "synapse_ctx.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("SynapseOS Project <dev@synapseos.dev>");
MODULE_DESCRIPTION("SynapseOS kernel-native AI integration module");
MODULE_VERSION(SYNAPSE_KMOD_VERSION);

/* ── Module parameters ────────────────────────────────────── */

/* Enable/disable syscall event capturing */
static bool synapse_events = true;
module_param(synapse_events, bool, 0644);
MODULE_PARM_DESC(synapse_events, "Capture syscall events for AI analysis (default: true)");

/* Enable/disable AI scheduler hints */
static bool synapse_sched = true;
module_param(synapse_sched, bool, 0644);
MODULE_PARM_DESC(synapse_sched, "Apply AI scheduling hints (default: true)");

/* Daemon heartbeat timeout in seconds before fallback */
static int synapse_daemon_timeout = 30;
module_param(synapse_daemon_timeout, int, 0644);
MODULE_PARM_DESC(synapse_daemon_timeout, "Seconds before daemon considered dead (default: 30)");

/* Ring buffer size for syscall event log */
static int synapse_ring_size = 4096;
module_param(synapse_ring_size, int, 0444);
MODULE_PARM_DESC(synapse_ring_size, "Syscall event ring buffer size (default: 4096)");

/* ── Global module state ──────────────────────────────────── */
struct synapse_module_state {
    /* Sysfs kobject — /sys/kernel/synapse/ */
    struct kobject      *kobj;

    /* Daemon health tracking */
    unsigned long        last_heartbeat_jiffies;
    bool                 daemon_alive;
    spinlock_t           daemon_lock;

    /* Stats — all atomic for lockless read from sysfs */
    atomic64_t           events_captured;
    atomic64_t           hints_applied;
    atomic64_t           hints_rejected;
    atomic64_t           syscalls_hooked;
    atomic64_t           ai_queries_routed;
    atomic64_t           daemon_heartbeats;
    atomic64_t           daemon_timeouts;
    atomic_t             active_contexts;

    /* Watchdog timer — checks daemon heartbeat */
    struct timer_list    watchdog;

    /* Workqueue for deferred synapd communication */
    struct workqueue_struct *wq;

    /* Module parameters (live copies) */
    bool                 events_enabled;
    bool                 sched_enabled;
    int                  daemon_timeout_secs;
};

/* Single global instance */
struct synapse_module_state synapse_state;

/* ── Daemon watchdog ──────────────────────────────────────── */
static void synapse_watchdog_fn(struct timer_list *t)
{
    struct synapse_module_state *s =
        container_of(t, struct synapse_module_state, watchdog);

    unsigned long now = jiffies;
    unsigned long deadline = s->last_heartbeat_jiffies +
                             secs_to_jiffies(s->daemon_timeout_secs);

    spin_lock(&s->daemon_lock);
    if (s->daemon_alive && time_after(now, deadline)) {
        s->daemon_alive = false;
        atomic64_inc(&s->daemon_timeouts);
        pr_warn("synapse_kmod: synapd heartbeat timeout — "
                "falling back to stock scheduling\n");
        /* Notify scheduler subsystem that AI hints are stale */
        synapse_sched_daemon_lost();
    }
    spin_unlock(&s->daemon_lock);

    /* Reschedule watchdog */
    mod_timer(&s->watchdog, now + secs_to_jiffies(5));
}

/* Called by sysfs status attribute write handler when
 * synapd writes "ALIVE ..." to /sys/kernel/synapse/status */
void synapse_daemon_heartbeat(void)
{
    spin_lock(&synapse_state.daemon_lock);
    synapse_state.last_heartbeat_jiffies = jiffies;
    if (!synapse_state.daemon_alive) {
        synapse_state.daemon_alive = true;
        pr_info("synapse_kmod: synapd reconnected\n");
        synapse_sched_daemon_ready();
    }
    spin_unlock(&synapse_state.daemon_lock);
    atomic64_inc(&synapse_state.daemon_heartbeats);
}

/* Called by sysfs when synapd writes "SHUTDOWN" */
void synapse_daemon_shutdown(void)
{
    spin_lock(&synapse_state.daemon_lock);
    synapse_state.daemon_alive = false;
    spin_unlock(&synapse_state.daemon_lock);
    pr_info("synapse_kmod: synapd announced shutdown\n");
    synapse_sched_daemon_lost();
}

/* ── Public state accessors (used by other .c files) ─────── */
bool synapse_daemon_is_alive(void)
{
    bool alive;
    spin_lock(&synapse_state.daemon_lock);
    alive = synapse_state.daemon_alive;
    spin_unlock(&synapse_state.daemon_lock);
    return alive;
}

bool synapse_events_enabled(void)  { return synapse_state.events_enabled; }
bool synapse_sched_enabled(void)   { return synapse_state.sched_enabled;  }

void synapse_stat_event(void)      { atomic64_inc(&synapse_state.events_captured); }
void synapse_stat_hint_ok(void)    { atomic64_inc(&synapse_state.hints_applied);   }
void synapse_stat_hint_fail(void)  { atomic64_inc(&synapse_state.hints_rejected);  }
void synapse_stat_syscall(void)    { atomic64_inc(&synapse_state.syscalls_hooked); }
void synapse_stat_query(void)      { atomic64_inc(&synapse_state.ai_queries_routed); }

void synapse_ctx_inc(void)  { atomic_inc(&synapse_state.active_contexts); }
void synapse_ctx_dec(void)  { atomic_dec(&synapse_state.active_contexts); }

/* Fill a stats struct for sysfs output */
void synapse_get_stats(struct synapse_stats *out)
{
    out->events_captured    = atomic64_read(&synapse_state.events_captured);
    out->hints_applied      = atomic64_read(&synapse_state.hints_applied);
    out->hints_rejected     = atomic64_read(&synapse_state.hints_rejected);
    out->syscalls_hooked    = atomic64_read(&synapse_state.syscalls_hooked);
    out->ai_queries_routed  = atomic64_read(&synapse_state.ai_queries_routed);
    out->daemon_heartbeats  = atomic64_read(&synapse_state.daemon_heartbeats);
    out->daemon_timeouts    = atomic64_read(&synapse_state.daemon_timeouts);
    out->active_contexts    = atomic_read(&synapse_state.active_contexts);
    out->kmod_version       = SYNAPSE_KMOD_MAGIC;
}

/* ── Module init ──────────────────────────────────────────── */
static int __init synapse_kmod_init(void)
{
    int ret;

    pr_info("synapse_kmod: loading v%s\n", SYNAPSE_KMOD_VERSION);
    pr_info("synapse_kmod: kernel %s\n", utsname()->release);

    /* Initialize global state */
    memset(&synapse_state, 0, sizeof(synapse_state));
    spin_lock_init(&synapse_state.daemon_lock);
    atomic64_set(&synapse_state.events_captured,   0);
    atomic64_set(&synapse_state.hints_applied,     0);
    atomic64_set(&synapse_state.hints_rejected,    0);
    atomic64_set(&synapse_state.syscalls_hooked,   0);
    atomic64_set(&synapse_state.ai_queries_routed, 0);
    atomic64_set(&synapse_state.daemon_heartbeats, 0);
    atomic64_set(&synapse_state.daemon_timeouts,   0);
    atomic_set(&synapse_state.active_contexts,     0);

    synapse_state.events_enabled      = synapse_events;
    synapse_state.sched_enabled       = synapse_sched;
    synapse_state.daemon_timeout_secs = synapse_daemon_timeout;
    synapse_state.daemon_alive        = false;
    synapse_state.last_heartbeat_jiffies = jiffies;

    /* 1. Create /sys/kernel/synapse/ kobject */
    ret = synapse_sysfs_init(&synapse_state.kobj);
    if (ret) {
        pr_err("synapse_kmod: sysfs init failed: %d\n", ret);
        return ret;
    }
    pr_info("synapse_kmod: sysfs registered at /sys/kernel/synapse/\n");

    /* 2. Initialize AI scheduling subsystem */
    ret = synapse_sched_init();
    if (ret) {
        pr_err("synapse_kmod: sched init failed: %d\n", ret);
        goto err_sched;
    }

    /* 3. Install kprobes on syscalls */
    if (synapse_events) {
        ret = synapse_probe_init(synapse_ring_size);
        if (ret) {
            pr_err("synapse_kmod: probe init failed: %d\n", ret);
            goto err_probe;
        }
        pr_info("synapse_kmod: kprobes installed\n");
    }

    /* 4. Register AI_CTX syscall handlers */
    ret = synapse_ctx_init();
    if (ret) {
        pr_warn("synapse_kmod: AI_CTX syscall registration failed: %d "
                "(kernel may not support AI_CTX)\n", ret);
        /* Non-fatal: AI_CTX is Linux 7.0+ only */
    }

    /* 5. Create workqueue for deferred work */
    synapse_state.wq = alloc_workqueue("synapse_wq",
                                        WQ_UNBOUND | WQ_MEM_RECLAIM, 4);
    if (!synapse_state.wq) {
        ret = -ENOMEM;
        goto err_wq;
    }

    /* 6. Start daemon watchdog timer */
    timer_setup(&synapse_state.watchdog, synapse_watchdog_fn, 0);
    mod_timer(&synapse_state.watchdog, jiffies + secs_to_jiffies(5));

    pr_info("synapse_kmod: loaded — waiting for synapd heartbeat\n");
    pr_info("synapse_kmod: events=%s sched=%s daemon_timeout=%ds\n",
            synapse_events ? "on" : "off",
            synapse_sched  ? "on" : "off",
            synapse_daemon_timeout);

    return 0;

err_wq:
    synapse_ctx_exit();
    synapse_probe_exit();
err_probe:
    synapse_sched_exit();
err_sched:
    synapse_sysfs_exit(synapse_state.kobj);
    return ret;
}

/* ── Module exit ──────────────────────────────────────────── */
static void __exit synapse_kmod_exit(void)
{
    pr_info("synapse_kmod: unloading\n");

    /* Stop watchdog */
    del_timer_sync(&synapse_state.watchdog);

    /* Drain and destroy workqueue */
    if (synapse_state.wq) {
        flush_workqueue(synapse_state.wq);
        destroy_workqueue(synapse_state.wq);
        synapse_state.wq = NULL;
    }

    /* Teardown in reverse init order */
    synapse_ctx_exit();
    synapse_probe_exit();
    synapse_sched_exit();
    synapse_sysfs_exit(synapse_state.kobj);

    pr_info("synapse_kmod: unloaded. stats: events=%llu hints=%llu syscalls=%llu\n",
            atomic64_read(&synapse_state.events_captured),
            atomic64_read(&synapse_state.hints_applied),
            atomic64_read(&synapse_state.syscalls_hooked));
}

module_init(synapse_kmod_init);
module_exit(synapse_kmod_exit);
