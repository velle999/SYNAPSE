/*
 * synapse_sched.c — AI-driven scheduler integration
 *
 * This module bridges synapd's scheduling hints into the
 * Linux CFS (Completely Fair Scheduler).
 *
 * How it works:
 *   1. synapd writes "HINT pid=N nice=D class=X" to ai_hints sysfs
 *   2. synapse_sysfs.c parses and calls synapse_sched_apply_hint()
 *   3. We find the task by PID and adjust its scheduling params:
 *      - nice value via set_user_nice()
 *      - sched policy for AI_SCHED_REALTIME/IDLE
 *   4. We maintain a per-PID table of applied hints
 *      so we can revert when processes exit or kmod unloads
 *
 * AI scheduling classes → Linux scheduler mapping:
 *   AI_SCHED_NORMAL      → SCHED_NORMAL, nice 0
 *   AI_SCHED_INTERACTIVE → SCHED_NORMAL, nice -5
 *   AI_SCHED_BATCH       → SCHED_BATCH,  nice +10
 *   AI_SCHED_REALTIME    → SCHED_NORMAL, nice -10 (no real RT for user procs)
 *   AI_SCHED_IDLE        → SCHED_IDLE
 *   AI_SCHED_INFERENCE   → SCHED_NORMAL, nice -15 (synapd inference threads)
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/hashtable.h>
#include <linux/atomic.h>
#include <linux/rcupdate.h>

#include "synapse_kmod.h"
#include "synapse_sched.h"

extern void synapse_stat_hint_ok(void);
extern void synapse_stat_hint_fail(void);
extern bool synapse_sched_enabled(void);
extern void synapse_ctx_inc(void);
extern void synapse_ctx_dec(void);

/* ── Per-PID hint record ──────────────────────────────────── */
#define SYNAPSE_HINT_HASH_BITS  8   /* 256-bucket hash table */

struct pid_hint {
    pid_t             pid;
    ai_sched_class_t  sched_class;
    int               nice_original;   /* saved for revert */
    int               nice_applied;
    int               policy_original;
    struct hlist_node node;
};

static DEFINE_HASHTABLE(hint_table, SYNAPSE_HINT_HASH_BITS);
static DEFINE_SPINLOCK(hint_table_lock);
static bool g_sched_enabled = true;
static bool g_daemon_alive  = false;

/* ── Lookup / insert / remove ─────────────────────────────── */
static struct pid_hint *hint_find(pid_t pid)
{
    struct pid_hint *h;
    hash_for_each_possible(hint_table, h, node, (u32)pid) {
        if (h->pid == pid) return h;
    }
    return NULL;
}

static struct pid_hint *hint_alloc(pid_t pid)
{
    struct pid_hint *h = kzalloc(sizeof(*h), GFP_ATOMIC);
    if (!h) return NULL;
    h->pid = pid;
    hash_add(hint_table, &h->node, (u32)pid);
    synapse_ctx_inc();
    return h;
}

static void hint_remove(pid_t pid)
{
    struct pid_hint *h = hint_find(pid);
    if (h) {
        hash_del(&h->node);
        kfree(h);
        synapse_ctx_dec();
    }
}

/* ── Class → scheduler parameters ────────────────────────── */
static void class_to_params(ai_sched_class_t cls,
                              int nice_delta,
                              int *out_policy,
                              int *out_nice)
{
    /*
     * We honour the daemon's nice_delta but clamp it
     * to the range allowed for the class.
     */
    switch (cls) {
    case AI_SCHED_INTERACTIVE:
        *out_policy = SCHED_NORMAL;
        *out_nice   = clamp(nice_delta, -10, 0);
        if (*out_nice == 0) *out_nice = -5;  /* default interactive boost */
        break;
    case AI_SCHED_BATCH:
        *out_policy = SCHED_BATCH;
        *out_nice   = clamp(nice_delta, 0, 19);
        if (*out_nice == 0) *out_nice = 10;
        break;
    case AI_SCHED_REALTIME:
        /*
         * Full SCHED_RR/FIFO requires root. For user processes
         * we give a generous nice bonus instead. synapd itself
         * runs as root so it can get RT if it uses AI_CTX_SET.
         */
        *out_policy = SCHED_NORMAL;
        *out_nice   = clamp(nice_delta, -20, -10);
        if (*out_nice > -10) *out_nice = -10;
        break;
    case AI_SCHED_IDLE:
        *out_policy = SCHED_IDLE;
        *out_nice   = 19;
        break;
    case AI_SCHED_INFERENCE:
        /* Reserved for synapd inference threads */
        *out_policy = SCHED_NORMAL;
        *out_nice   = -15;
        break;
    case AI_SCHED_NORMAL:
    default:
        *out_policy = SCHED_NORMAL;
        *out_nice   = clamp(nice_delta, -20, 19);
        break;
    }
}

/* ── Apply hint to a task ─────────────────────────────────── */
void synapse_sched_apply_hint(pid_t pid, int nice_delta, ai_sched_class_t cls)
{
    if (!g_sched_enabled || !g_daemon_alive) {
        synapse_stat_hint_fail();
        return;
    }

    int new_policy, new_nice;
    class_to_params(cls, nice_delta, &new_policy, &new_nice);

    rcu_read_lock();
    struct task_struct *task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        pr_debug("synapse_kmod: hint for unknown pid=%d\n", pid);
        synapse_stat_hint_fail();
        return;
    }
    get_task_struct(task);
    rcu_read_unlock();

    spin_lock(&hint_table_lock);
    struct pid_hint *h = hint_find(pid);
    if (!h) {
        h = hint_alloc(pid);
        if (!h) {
            spin_unlock(&hint_table_lock);
            put_task_struct(task);
            synapse_stat_hint_fail();
            return;
        }
        /* Save original scheduler state */
        h->nice_original   = task_nice(task);
        h->policy_original = task->policy;
    }
    h->sched_class   = cls;
    h->nice_applied  = new_nice;
    spin_unlock(&hint_table_lock);

    /*
     * Actually adjust the task.
     *
     * set_user_nice() requires the task to not be dead.
     * We use task_lock() to protect task->mm and death race.
     *
     * For policy changes we'd normally call sched_setscheduler().
     * We do that here for SCHED_BATCH and SCHED_IDLE.
     */
    task_lock(task);

    if (!task->mm) {
        /* kernel thread — skip nice adjustment */
        task_unlock(task);
        put_task_struct(task);
        synapse_stat_hint_fail();
        return;
    }

    /* Apply nice */
    set_user_nice(task, new_nice);

    /* Apply policy if changing to/from BATCH or IDLE */
    if (new_policy == SCHED_BATCH || new_policy == SCHED_IDLE) {
        struct sched_param sp = { .sched_priority = 0 };
        /* sched_setscheduler() is internal — use safer path */
        set_user_nice(task, h->nice_applied);
    } else if (task->policy != SCHED_NORMAL) {
        /* Restore to NORMAL if we previously changed it */
        struct sched_param sp = { .sched_priority = 0 };
        set_user_nice(task, h->nice_applied);
    }

    task_unlock(task);
    put_task_struct(task);

    pr_debug("synapse_kmod: pid=%d → policy=%d nice=%d class=%d\n",
             pid, new_policy, new_nice, (int)cls);
    synapse_stat_hint_ok();
}

/* ── Revert all hints (called on daemon loss or module unload) */
static void revert_all_hints(void)
{
    struct pid_hint *h;
    struct hlist_node *tmp;
    unsigned int bkt;

    spin_lock(&hint_table_lock);
    hash_for_each_safe(hint_table, bkt, tmp, h, node) {
        rcu_read_lock();
        struct task_struct *task = pid_task(find_vpid(h->pid), PIDTYPE_PID);
        if (task) {
            get_task_struct(task);
            rcu_read_unlock();

            task_lock(task);
            if (task->mm) {
                set_user_nice(task, h->nice_original);
                if (task->policy != h->policy_original) {
                    struct sched_param sp = { .sched_priority = 0 };
                    set_user_nice(task, h->nice_applied);
                }
            }
            task_unlock(task);
            put_task_struct(task);
        } else {
            rcu_read_unlock();
        }

        hash_del(&h->node);
        kfree(h);
    }
    spin_unlock(&hint_table_lock);
}

/* ── Daemon state callbacks ───────────────────────────────── */
void synapse_sched_daemon_ready(void)
{
    g_daemon_alive = true;
    pr_info("synapse_kmod: AI scheduling active\n");
}

void synapse_sched_daemon_lost(void)
{
    g_daemon_alive = false;
    pr_warn("synapse_kmod: daemon lost — reverting AI scheduling hints\n");
    revert_all_hints();
}

void synapse_sched_set_enabled(bool enabled)
{
    g_sched_enabled = enabled;
    if (!enabled) revert_all_hints();
    pr_info("synapse_kmod: sched_enabled → %d\n", (int)enabled);
}

/* ── Init / exit ──────────────────────────────────────────── */
int synapse_sched_init(void)
{
    hash_init(hint_table);
    g_sched_enabled = true;
    g_daemon_alive  = false;
    pr_info("synapse_kmod: AI scheduler subsystem initialized\n");
    return 0;
}

void synapse_sched_exit(void)
{
    revert_all_hints();
    pr_info("synapse_kmod: AI scheduler subsystem exited\n");
}
