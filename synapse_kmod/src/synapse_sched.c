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
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 * https://github.com/velle999/SYNAPSE
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <uapi/linux/sched/types.h>   /* struct sched_attr */
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/hashtable.h>
#include <linux/atomic.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/list.h>

#include "synapse_kmod.h"
#include "synapse_sched.h"

extern void synapse_stat_hint_ok(void);
extern void synapse_stat_hint_fail(void);
extern bool synapse_sched_enabled(void);
extern void synapse_ctx_inc(void);
extern void synapse_ctx_dec(void);
extern struct workqueue_struct *synapse_get_wq(void);

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

/*
 * Reverting hints calls sched_setscheduler_nocheck(), which may sleep, so it
 * cannot run in the watchdog timer (softirq) context that detects daemon
 * loss. We defer the revert to this work item, which runs in process context.
 */
static struct work_struct revert_work;

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
         * Full SCHED_RR/FIFO requires root, so a REALTIME class buys a
         * generous nice bonus and nothing more. Nothing here ever grants a
         * real RT policy: the only caller is an AI hint arriving over sysfs,
         * and a model that mislabels a busy loop as realtime must not be able
         * to starve the box. (This used to add "synapd can get RT via
         * AI_CTX_SET" — that syscall family is gone.)
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

/*
 * Set a task's scheduling policy. We only ever use non-RT policies
 * (SCHED_NORMAL/BATCH/IDLE), so sched_priority is always 0 and the nice
 * value carries the priority. sched_setattr_nocheck() is the module-exported
 * entry point (sched_setscheduler_nocheck() is not exported); it may sleep,
 * so callers must be in process context. Returns 0 on success.
 */
static int synapse_set_policy(struct task_struct *task, int policy, int nice)
{
    struct sched_attr attr = {
        .size         = sizeof(attr),
        .sched_policy = policy,
        .sched_nice   = nice,
    };
    return sched_setattr_nocheck(task, &attr);
}

/*
 * synapse_sched_pid_protected — processes the AI scheduler must never touch,
 * even on an explicit /sys/kernel/synapse/ai_hints write.
 *
 * Mirrors synguard's userspace sg_is_protected(): PID 0/1, the global init,
 * kernel threads, and the core SynapseOS/session daemons. Without this a
 * writer to ai_hints could "HINT pid=1 class=idle" and drop init (or the
 * compositor / the security monitor itself) to SCHED_IDLE — a trivial local
 * DoS. The check is enforced in the kernel, below the sysfs permission gate,
 * so it holds regardless of who managed to open the file.
 */
static bool synapse_sched_pid_protected(pid_t pid, struct task_struct *task)
{
    static const char *const protected_comm[] = {
        "systemd", "systemd-logind", "synguard", "synapd",
        "synui", "synnet", "seatd", "greetd", NULL
    };
    int i;

    if (pid <= 1)                 return true;   /* swapper, init/systemd */
    if (is_global_init(task))     return true;
    if (task->flags & PF_KTHREAD) return true;   /* kernel thread */

    for (i = 0; protected_comm[i]; i++)
        if (strncmp(task->comm, protected_comm[i], TASK_COMM_LEN) == 0)
            return true;
    return false;
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

    if (synapse_sched_pid_protected(pid, task)) {
        pr_warn_ratelimited(
            "synapse_kmod: refused AI scheduling hint for protected pid=%d (%s)\n",
            pid, task->comm);
        put_task_struct(task);
        synapse_stat_hint_fail();
        return;
    }

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
     * Adjust the task. We're in process context (sysfs store), so it is
     * safe to call sched_setattr_nocheck(), which may sleep.
     *
     * Kernel threads (no mm) are not user-reschedulable. Read task->mm
     * under task_lock to avoid racing with exit, then drop the lock before
     * touching the scheduler — set_user_nice() and sched_setattr_nocheck()
     * take their own rq/pi locks and must not be called under task_lock.
     */
    task_lock(task);
    bool is_kthread = (task->mm == NULL);
    task_unlock(task);

    if (is_kthread) {
        put_task_struct(task);
        synapse_stat_hint_fail();
        return;
    }

    /* Apply nice. */
    set_user_nice(task, new_nice);

    /*
     * Apply scheduling policy. All classes we map to are non-RT
     * (SCHED_NORMAL/BATCH/IDLE), so sched_priority is always 0.
     * Only call when the policy actually changes.
     */
    if (task->policy != new_policy) {
        int rc = synapse_set_policy(task, new_policy, new_nice);
        if (rc)
            pr_debug("synapse_kmod: sched_setattr(pid=%d policy=%d) "
                     "failed: %d\n", pid, new_policy, rc);
    }

    put_task_struct(task);

    pr_debug("synapse_kmod: pid=%d → policy=%d nice=%d class=%d\n",
             pid, new_policy, new_nice, (int)cls);
    synapse_stat_hint_ok();
}

/* ── Revert all hints (process context only) ──────────────────
 *
 * Restores each tracked task's original nice and policy. Because
 * sched_setattr_nocheck() may sleep, we first detach every node from
 * the hash table under the spinlock into a local list, then do the actual
 * scheduler calls with no lock held. Concurrent reverters are safe: the
 * first to take the lock claims all nodes, the rest find the table empty.
 *
 * Must NOT be called from atomic context — use schedule_revert() for that.
 */
static void revert_all_hints(void)
{
    struct pid_hint *h;
    struct hlist_node *tmp;
    unsigned int bkt;
    HLIST_HEAD(drain);

    spin_lock(&hint_table_lock);
    hash_for_each_safe(hint_table, bkt, tmp, h, node) {
        hash_del(&h->node);
        hlist_add_head(&h->node, &drain);
    }
    spin_unlock(&hint_table_lock);

    hlist_for_each_entry_safe(h, tmp, &drain, node) {
        rcu_read_lock();
        struct task_struct *task = pid_task(find_vpid(h->pid), PIDTYPE_PID);
        if (task)
            get_task_struct(task);
        rcu_read_unlock();

        if (task) {
            set_user_nice(task, h->nice_original);
            if (task->policy != h->policy_original)
                synapse_set_policy(task, h->policy_original, h->nice_original);
            put_task_struct(task);
        }

        hlist_del(&h->node);
        kfree(h);
        synapse_ctx_dec();   /* balances the inc in hint_alloc() */
    }
}

static void revert_work_fn(struct work_struct *w)
{
    (void)w;
    revert_all_hints();
}

/*
 * Queue a revert from any context (callable under a spinlock / in softirq).
 * The work item runs revert_all_hints() in process context where sleeping
 * is allowed.
 */
static void schedule_revert(void)
{
    struct workqueue_struct *wq = synapse_get_wq();
    if (wq)
        queue_work(wq, &revert_work);
    else
        pr_warn("synapse_kmod: no workqueue — hints not reverted\n");
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
    /* Called from the watchdog timer (softirq) — must defer the revert. */
    schedule_revert();
}

void synapse_sched_set_enabled(bool enabled)
{
    g_sched_enabled = enabled;
    if (!enabled) schedule_revert();
    pr_info("synapse_kmod: sched_enabled → %d\n", (int)enabled);
}

/* ── Init / exit ──────────────────────────────────────────── */
int synapse_sched_init(void)
{
    hash_init(hint_table);
    INIT_WORK(&revert_work, revert_work_fn);
    g_sched_enabled = true;
    g_daemon_alive  = false;
    pr_info("synapse_kmod: AI scheduler subsystem initialized\n");
    return 0;
}

void synapse_sched_exit(void)
{
    g_sched_enabled = false;
    g_daemon_alive  = false;
    /*
     * Module teardown stops the watchdog and flushes/destroys the workqueue
     * before calling us, so no revert_work should be in flight. cancel_work_sync
     * is a defensive barrier (also covers the init error path), then a final
     * synchronous drain restores any still-live tasks. Always process context.
     */
    cancel_work_sync(&revert_work);
    revert_all_hints();
    pr_info("synapse_kmod: AI scheduler subsystem exited\n");
}
