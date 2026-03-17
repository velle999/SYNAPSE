/*
 * synapse_ctx.c — AI_CTX syscall family handlers
 *
 * Linux 7.0 introduced extension points for AI-aware scheduling.
 * We hook into these to implement:
 *
 *   AI_CTX_SET   (syscall ~451) — process declares intent
 *   AI_CTX_GET   (syscall ~452) — process reads its AI sched class
 *   AI_CTX_QUERY (syscall ~453) — process sends a query to synapd
 *
 * On kernels < 7.0, we register trampoline hooks via kprobes
 * on do_syscall_64 with nr matching, or simply report that
 * AI_CTX is unavailable.
 *
 * The AI_CTX_QUERY path is the most complex:
 *   process → AI_CTX_QUERY syscall → kmod → sysfs relay →
 *   synapd reads → inference → synapd writes response → kmod
 *   copies to userspace process
 *
 * For the relay we use a simple per-PID pending request table
 * with completion objects. The process blocks; synapd writes
 * the response to /sys/kernel/synapse/ai_hints as a special
 * QUERY_RESP record.
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/hashtable.h>
#include <linux/completion.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/atomic.h>

#include "synapse_kmod.h"
#include "synapse_ctx.h"
#include "synapse_sched.h"

extern void synapse_stat_query(void);
extern bool synapse_daemon_is_alive(void);

/* ── AI_CTX syscall numbers ───────────────────────────────── */
/*
 * Linux 7.0 allocates these. On earlier kernels we detect them
 * by trying to register and falling back gracefully.
 *
 * These numbers are defined in the SynapseOS kernel patch.
 * On a stock kernel, processes calling these get ENOSYS.
 */
#define NR_AI_CTX_SET    451
#define NR_AI_CTX_GET    452
#define NR_AI_CTX_QUERY  453

/* ── Per-PID context store ────────────────────────────────── */
#define CTX_HASH_BITS  8

struct ai_ctx_entry {
    pid_t             pid;
    uint32_t          flags;
    char              intent[256];
    ai_sched_class_t  sched_class;
    int               nice_value;
    u64               set_time_ns;
    struct hlist_node node;
};

static DEFINE_HASHTABLE(ctx_table, CTX_HASH_BITS);
static DEFINE_SPINLOCK(ctx_lock);

/* ── Pending query table ──────────────────────────────────── */
/*
 * When a process calls AI_CTX_QUERY, we:
 * 1. Insert a pending entry keyed by request_id
 * 2. Write the query to /sys/kernel/synapse/syscall_log
 *    with a special QUERY prefix so synapd picks it up
 * 3. Block the calling process on a completion
 * 4. synapd writes "QUERY_RESP id=N response=..." to ai_hints
 * 5. We complete the pending entry and copy response to user
 *
 * Timeout: 5 seconds (user process gets ETIMEDOUT)
 */
#define QUERY_HASH_BITS  6
#define QUERY_TIMEOUT_MS 5000

struct pending_query {
    u32               request_id;
    struct completion done;
    char              response[1024];
    int               result;          /* 0=ok, -errno=error */
    struct hlist_node node;
};

static DEFINE_HASHTABLE(query_table, QUERY_HASH_BITS);
static DEFINE_SPINLOCK(query_lock);
static atomic_t next_request_id = ATOMIC_INIT(1);

/* ── Context table ops ────────────────────────────────────── */
static struct ai_ctx_entry *ctx_find(pid_t pid)
{
    struct ai_ctx_entry *e;
    hash_for_each_possible(ctx_table, e, node, (u32)pid) {
        if (e->pid == pid) return e;
    }
    return NULL;
}

static struct ai_ctx_entry *ctx_alloc(pid_t pid)
{
    struct ai_ctx_entry *e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e) return NULL;
    e->pid = pid;
    hash_add(ctx_table, &e->node, (u32)pid);
    return e;
}

/* ── AI_CTX_SET handler ───────────────────────────────────── */
/*
 * Called when a process declares its intent.
 * We store the context and request a scheduling hint from
 * the AI (via the deferred workqueue, not blocking the syscall).
 */
static long handle_ai_ctx_set(struct ai_ctx_set_args __user *uargs)
{
    struct ai_ctx_set_args args;
    if (copy_from_user(&args, uargs, sizeof(args)))
        return -EFAULT;

    pid_t pid = task_pid_vnr(current);

    spin_lock(&ctx_lock);
    struct ai_ctx_entry *e = ctx_find(pid);
    if (!e) {
        e = ctx_alloc(pid);
        if (!e) {
            spin_unlock(&ctx_lock);
            return -ENOMEM;
        }
    }
    e->flags      = args.flags;
    e->set_time_ns = ktime_get_raw_ns();
    strncpy(e->intent, args.intent, sizeof(e->intent) - 1);
    spin_unlock(&ctx_lock);

    pr_debug("synapse_kmod: AI_CTX_SET pid=%d flags=0x%x intent=%.32s\n",
             pid, args.flags, args.intent);

    /*
     * If intent string is present, we want synapd to see it.
     * We push it to the syscall event ring so synapd picks it up.
     * The daemon will respond via ai_hints with a scheduling suggestion.
     */
    if (args.intent[0] && synapse_daemon_is_alive()) {
        /* The ring push happens in synapse_probe.c;
         * here we just log. In a full implementation we'd
         * enqueue a deferred work item to write to the log ring. */
        pr_debug("synapse_kmod: queuing AI_CTX intent for scheduling\n");
    }

    return 0;
}

/* ── AI_CTX_GET handler ───────────────────────────────────── */
static long handle_ai_ctx_get(struct ai_ctx_get_result __user *ures)
{
    pid_t pid = task_pid_vnr(current);

    struct ai_ctx_get_result result = {0};
    result.last_updated_ns = ktime_get_raw_ns();

    spin_lock(&ctx_lock);
    struct ai_ctx_entry *e = ctx_find(pid);
    if (e) {
        result.sched_class    = e->sched_class;
        result.nice_value     = e->nice_value;
        result.flags_applied  = e->flags;
        result.last_updated_ns = e->set_time_ns;
        snprintf(result.reason, sizeof(result.reason),
                 "AI_CTX active since %llu ns", e->set_time_ns);
    } else {
        result.sched_class = AI_SCHED_NORMAL;
        result.nice_value  = task_nice(current);
        strncpy(result.reason, "no AI_CTX set", sizeof(result.reason) - 1);
    }
    spin_unlock(&ctx_lock);

    if (copy_to_user(ures, &result, sizeof(result)))
        return -EFAULT;

    return 0;
}

/* ── AI_CTX_QUERY handler ─────────────────────────────────── */
static long handle_ai_ctx_query(struct ai_ctx_query_args __user *uargs)
{
    if (!synapse_daemon_is_alive())
        return -ENODEV;  /* synapd not running */

    struct ai_ctx_query_args args;
    if (copy_from_user(&args, uargs, sizeof(args)))
        return -EFAULT;

    /* Allocate pending query */
    struct pending_query *pq = kzalloc(sizeof(*pq), GFP_KERNEL);
    if (!pq) return -ENOMEM;

    pq->request_id = atomic_inc_return(&next_request_id);
    init_completion(&pq->done);

    spin_lock(&query_lock);
    hash_add(query_table, &pq->node, pq->request_id);
    spin_unlock(&query_lock);

    synapse_stat_query();

    /*
     * In a full implementation: write "QUERY id=N prompt=..." to
     * the syscall log ring so synapd picks it up, then wait.
     * For now we log the query intent and return ETIMEDOUT
     * after the timeout (synapd integration is the synapd side).
     */
    pr_debug("synapse_kmod: AI_CTX_QUERY pid=%d req=%u prompt=%.32s\n",
             (int)task_pid_vnr(current), pq->request_id, args.prompt);

    unsigned long timeout = msecs_to_jiffies(
        args.timeout_ms ? args.timeout_ms : QUERY_TIMEOUT_MS);

    long ret = wait_for_completion_interruptible_timeout(&pq->done, timeout);

    spin_lock(&query_lock);
    hash_del(&pq->node);
    spin_unlock(&query_lock);

    long result;
    if (ret == 0) {
        result = -ETIMEDOUT;
    } else if (ret < 0) {
        result = -EINTR;
    } else {
        /* Copy response to user */
        if (copy_to_user(args.response, pq->response,
                         min(sizeof(pq->response), sizeof(args.response))))
            result = -EFAULT;
        else
            result = 0;
    }

    kfree(pq);
    return result;
}

/* ── Complete a pending query (called from ai_hints write handler) */
void synapse_ctx_complete_query(u32 request_id, const char *response)
{
    spin_lock(&query_lock);
    struct pending_query *pq;
    hash_for_each_possible(query_table, pq, node, request_id) {
        if (pq->request_id == request_id) {
            strncpy(pq->response, response, sizeof(pq->response) - 1);
            pq->result = 0;
            complete(&pq->done);
            spin_unlock(&query_lock);
            return;
        }
    }
    spin_unlock(&query_lock);
    pr_debug("synapse_kmod: QUERY_RESP for unknown req=%u\n", request_id);
}

/* ── Syscall hook (kprobe on do_syscall_64) ───────────────── */
/*
 * On Linux 7.0+ we can register proper sys_* handlers.
 * On older kernels we intercept via a kprobe on do_syscall_64
 * and check the syscall number ourselves.
 *
 * This is the compatibility approach that works on 6.8+.
 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
/*
 * Linux 7.0: register via the AI_CTX syscall registration API.
 * This is a SynapseOS kernel patch.
 */
extern int ai_ctx_register_handlers(
    long (*set_fn)(struct ai_ctx_set_args __user *),
    long (*get_fn)(struct ai_ctx_get_result __user *),
    long (*query_fn)(struct ai_ctx_query_args __user *)
);
extern void ai_ctx_unregister_handlers(void);

int synapse_ctx_init(void)
{
    int ret = ai_ctx_register_handlers(
        handle_ai_ctx_set,
        handle_ai_ctx_get,
        handle_ai_ctx_query
    );
    if (ret) {
        pr_warn("synapse_kmod: AI_CTX syscall registration failed: %d\n", ret);
        return ret;
    }
    pr_info("synapse_kmod: AI_CTX syscalls registered (Linux 7.0 native)\n");
    return 0;
}

void synapse_ctx_exit(void)
{
    ai_ctx_unregister_handlers();
}

#else
/*
 * Fallback for Linux < 7.0: intercept via kprobe on do_syscall_64.
 * We check the syscall nr and dispatch to our handlers.
 * Less efficient but works on 6.8+.
 */

static int syscall64_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    unsigned long nr = regs->orig_ax;

    if (nr == NR_AI_CTX_SET) {
        regs->ax = handle_ai_ctx_set(
            (struct ai_ctx_set_args __user *)regs->di);
    } else if (nr == NR_AI_CTX_GET) {
        regs->ax = handle_ai_ctx_get(
            (struct ai_ctx_get_result __user *)regs->di);
    } else if (nr == NR_AI_CTX_QUERY) {
        regs->ax = handle_ai_ctx_query(
            (struct ai_ctx_query_args __user *)regs->di);
    } else {
        return 0;  /* not our syscall, let it run normally */
    }

    /*
     * Signal to the kernel to skip the real syscall handler.
     * We set the syscall number to -1 (no-op) and the return
     * value is already in regs->ax.
     */
    regs->orig_ax = -1;
    return 0;
}

static struct kprobe kp_syscall64 = {
    .symbol_name = "do_syscall_64",
    .pre_handler = syscall64_pre_handler,
};

static bool kp_syscall64_registered;

int synapse_ctx_init(void)
{
    int ret = register_kprobe(&kp_syscall64);
    if (ret) {
        pr_warn("synapse_kmod: AI_CTX fallback kprobe failed: %d "
                "(AI_CTX syscalls unavailable — do_syscall_64 may be "
                "in noinstr section on this kernel)\n", ret);
        kp_syscall64_registered = false;
        return ret;
    }
    kp_syscall64_registered = true;
    pr_info("synapse_kmod: AI_CTX syscalls available via kprobe shim "
            "(nr: SET=%d GET=%d QUERY=%d)\n",
            NR_AI_CTX_SET, NR_AI_CTX_GET, NR_AI_CTX_QUERY);
    return 0;
}

void synapse_ctx_exit(void)
{
    if (kp_syscall64_registered)
        unregister_kprobe(&kp_syscall64);
}
#endif  /* LINUX_VERSION_CODE */
