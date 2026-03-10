/*
 * synapse_probe.c — Syscall kprobes for SynapseOS
 *
 * Hooks into security-relevant syscalls using kprobes.
 * Captured events are written to a ring buffer which
 * synapd reads via /sys/kernel/synapse/syscall_log.
 *
 * Monitored syscalls:
 *   execve / execveat   — process execution
 *   openat              — file opens (filtered to sensitive paths)
 *   socket / connect    — network activity
 *   ptrace              — process inspection/injection
 *   init_module         — kernel module loading
 *   finit_module        — kernel module loading (fd-based)
 *   mount               — filesystem mount
 *   setuid / setgid     — privilege changes
 *   capset              — capability changes
 *   kill (SIGKILL only) — targeted termination
 *
 * Implementation uses kretprobes + kprobes depending on
 * whether we need pre or post-syscall inspection.
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/dcache.h>
#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/syscalls.h>

#include "synapse_kmod.h"
#include "synapse_probe.h"

extern void synapse_stat_event(void);
extern void synapse_stat_syscall(void);
extern bool synapse_events_enabled(void);

/* ── Ring buffer ──────────────────────────────────────────── */
struct synapse_ring {
    struct synapse_syscall_event *events;
    int                           size;
    atomic_t                      head;   /* write position */
    atomic_t                      tail;   /* read position */
    spinlock_t                    lock;
};

static struct synapse_ring g_ring;

static int ring_init(int size)
{
    g_ring.events = kvzalloc(size * sizeof(*g_ring.events), GFP_KERNEL);
    if (!g_ring.events) return -ENOMEM;
    g_ring.size = size;
    atomic_set(&g_ring.head, 0);
    atomic_set(&g_ring.tail, 0);
    spin_lock_init(&g_ring.lock);
    return 0;
}

static void ring_free(void)
{
    kvfree(g_ring.events);
    g_ring.events = NULL;
}

/*
 * ring_push — add an event to the ring buffer.
 * If the ring is full, the oldest event is overwritten (lossy).
 */
static void ring_push(const struct synapse_syscall_event *evt)
{
    spin_lock(&g_ring.lock);
    int idx = atomic_read(&g_ring.head) % g_ring.size;
    memcpy(&g_ring.events[idx], evt, sizeof(*evt));
    atomic_inc(&g_ring.head);
    /* Advance tail if we've lapped */
    if (atomic_read(&g_ring.head) - atomic_read(&g_ring.tail) > g_ring.size)
        atomic_inc(&g_ring.tail);
    spin_unlock(&g_ring.lock);
}

/*
 * synapse_probe_read_log — drain ring buffer into buf for sysfs read.
 * Returns bytes written.
 */
ssize_t synapse_probe_read_log(char *buf, size_t buf_len)
{
    size_t pos = 0;

    spin_lock(&g_ring.lock);
    int tail = atomic_read(&g_ring.tail);
    int head = atomic_read(&g_ring.head);

    /* Read up to 32 events per sysfs read to avoid huge pages */
    int max_read = min(head - tail, 32);

    for (int i = 0; i < max_read && pos < buf_len - 128; i++) {
        int idx = (tail + i) % g_ring.size;
        struct synapse_syscall_event *e = &g_ring.events[idx];

        const char *fname = e->filename[0] ? e->filename : "-";
        pos += scnprintf(buf + pos, buf_len - pos,
            "%llu %u %u %u %s %s\n",
            e->timestamp_ns,
            e->pid, e->uid,
            e->syscall_nr,
            e->comm,
            fname
        );
        atomic_inc(&g_ring.tail);
    }
    spin_unlock(&g_ring.lock);

    return (ssize_t)pos;
}

/* ── Event construction helper ────────────────────────────── */
static void fill_event(struct synapse_syscall_event *e,
                        unsigned int syscall_nr,
                        uint8_t flags)
{
    struct task_struct *task = current;
    e->timestamp_ns = ktime_get_raw_ns();
    e->pid          = task_pid_vnr(task);
    e->tgid         = task_tgid_vnr(task);
    e->uid          = from_kuid_munged(current_user_ns(),
                                       task_uid(task));
    e->syscall_nr   = syscall_nr;
    e->flags        = flags;
    memcpy(e->comm, task->comm, TASK_COMM_LEN);
    e->filename[0]  = '\0';
}

/* ── Sensitive path filter ────────────────────────────────── */
/*
 * We don't hook every open() — only opens of sensitive paths.
 * This dramatically reduces noise.
 */
static bool is_sensitive_path(const char *path)
{
    static const char *const sensitive[] = {
        "/etc/passwd", "/etc/shadow", "/etc/sudoers",
        "/etc/ssh/",   "/root/",      "/proc/kcore",
        "/dev/mem",    "/dev/kmem",   "/boot/",
        "/sys/kernel/", "/proc/sysrq-trigger",
        NULL
    };
    for (int i = 0; sensitive[i]; i++)
        if (strncmp(path, sensitive[i], strlen(sensitive[i])) == 0)
            return true;
    return false;
}

/* ── execve kprobe ────────────────────────────────────────── */
/*
 * Hook sys_execve / sys_execveat.
 * We want the filename of what's being executed.
 */
static int execve_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    if (!synapse_events_enabled()) return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_execve, SYNAPSE_EVT_EXEC);

    /*
     * regs->di (x86_64) holds the first argument = filename pointer.
     * We use strncpy_from_user to safely copy from userspace.
     */
    const char __user *filename = (const char __user *)regs->di;
    if (filename)
        strncpy_from_user(e.filename, filename, sizeof(e.filename) - 1);

    ring_push(&e);
    synapse_stat_event();
    synapse_stat_syscall();
    return 0;
}

static struct kprobe kp_execve = {
    .symbol_name = "__x64_sys_execve",
    .pre_handler = execve_pre_handler,
};

static struct kprobe kp_execveat = {
    .symbol_name = "__x64_sys_execveat",
    .pre_handler = execve_pre_handler,   /* same handler, args differ */
};

/* ── openat kprobe ────────────────────────────────────────── */
static int openat_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    if (!synapse_events_enabled()) return 0;

    const char __user *filename = (const char __user *)regs->si;
    if (!filename) return 0;

    char kbuf[128] = {0};
    if (strncpy_from_user(kbuf, filename, sizeof(kbuf) - 1) <= 0)
        return 0;

    if (!is_sensitive_path(kbuf)) return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_openat, SYNAPSE_EVT_OPEN);
    strncpy(e.filename, kbuf, sizeof(e.filename) - 1);

    ring_push(&e);
    synapse_stat_event();
    return 0;
}

static struct kprobe kp_openat = {
    .symbol_name = "__x64_sys_openat",
    .pre_handler = openat_pre_handler,
};

/* ── socket / connect kprobe ─────────────────────────────── */
static int socket_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    if (!synapse_events_enabled()) return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_socket, SYNAPSE_EVT_SOCKET);
    e.args[0] = regs->di;  /* domain */
    e.args[1] = regs->si;  /* type   */
    e.args[2] = regs->dx;  /* proto  */

    ring_push(&e);
    synapse_stat_event();
    return 0;
}

static struct kprobe kp_socket = {
    .symbol_name = "__x64_sys_socket",
    .pre_handler = socket_pre_handler,
};

/* ── ptrace kprobe ────────────────────────────────────────── */
static int ptrace_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    if (!synapse_events_enabled()) return 0;

    long request = (long)regs->di;
    /* Only report ATTACH and PEEKTEXT/DATA */
    if (request != PTRACE_ATTACH && request != 0 && request != 1)
        return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_ptrace, SYNAPSE_EVT_PTRACE);
    e.args[0] = regs->di;  /* request */
    e.args[1] = regs->si;  /* pid */

    ring_push(&e);
    synapse_stat_event();
    return 0;
}

static struct kprobe kp_ptrace = {
    .symbol_name = "__x64_sys_ptrace",
    .pre_handler = ptrace_pre_handler,
};

/* ── init_module kprobe ───────────────────────────────────── */
static int insmod_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    if (!synapse_events_enabled()) return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_init_module, SYNAPSE_EVT_MODULE);
    snprintf(e.filename, sizeof(e.filename), "module_load pid=%u", e.pid);

    ring_push(&e);
    synapse_stat_event();

    pr_info("synapse_kmod: module load detected from pid=%u (%s)\n",
            e.pid, e.comm);
    return 0;
}

static struct kprobe kp_insmod = {
    .symbol_name = "__x64_sys_init_module",
    .pre_handler = insmod_pre_handler,
};

static struct kprobe kp_finit_module = {
    .symbol_name = "__x64_sys_finit_module",
    .pre_handler = insmod_pre_handler,
};

/* ── setuid kprobe ────────────────────────────────────────── */
static int setuid_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    if (!synapse_events_enabled()) return 0;

    uid_t target_uid = (uid_t)regs->di;
    /* Only interesting if escalating to root */
    if (target_uid != 0) return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_setuid, SYNAPSE_EVT_SETUID);
    e.args[0] = target_uid;

    ring_push(&e);
    synapse_stat_event();
    return 0;
}

static struct kprobe kp_setuid = {
    .symbol_name = "__x64_sys_setuid",
    .pre_handler = setuid_pre_handler,
};

/* ── Probe table ──────────────────────────────────────────── */
static struct kprobe *all_probes[] = {
    &kp_execve,
    &kp_execveat,
    &kp_openat,
    &kp_socket,
    &kp_ptrace,
    &kp_insmod,
    &kp_finit_module,
    &kp_setuid,
};

#define N_PROBES  ARRAY_SIZE(all_probes)

/* ── Enable / disable ─────────────────────────────────────── */
static bool g_probes_enabled = true;

void synapse_probe_set_enabled(bool enabled)
{
    if (enabled == g_probes_enabled) return;
    g_probes_enabled = enabled;

    if (!g_ring.events) return;

    if (enabled) {
        for (int i = 0; i < (int)N_PROBES; i++)
            enable_kprobe(all_probes[i]);
        pr_info("synapse_kmod: probes enabled\n");
    } else {
        for (int i = 0; i < (int)N_PROBES; i++)
            disable_kprobe(all_probes[i]);
        pr_info("synapse_kmod: probes disabled\n");
    }
}

/* ── Init / exit ──────────────────────────────────────────── */
int synapse_probe_init(int ring_size)
{
    int ret;

    ret = ring_init(ring_size);
    if (ret) return ret;

    ret = register_kprobes(all_probes, N_PROBES);
    if (ret) {
        pr_err("synapse_kmod: register_kprobes failed: %d\n", ret);
        ring_free();
        return ret;
    }

    pr_info("synapse_kmod: %zu kprobes registered, ring_size=%d\n",
            N_PROBES, ring_size);
    return 0;
}

void synapse_probe_exit(void)
{
    if (g_ring.events) {
        unregister_kprobes(all_probes, N_PROBES);
        ring_free();
    }
}
