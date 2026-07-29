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
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 * https://github.com/velle999/SYNAPSE
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
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/fcntl.h>

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
 * syn_escape — render src as a single whitespace-free token.
 *
 * The log below is whitespace-delimited, but a comm may legally contain spaces
 * (Firefox's "Socket Thread", prctl(PR_SET_NAME, "evil worm")) and so may a
 * path. Emitted raw, those shift every field after them: userspace read the
 * second word of the comm as the filename, which silently fed synguard a
 * constant string in place of the connect() destination. Escape anything that
 * could break the framing as \xHH; synguard's kmod_parse_event() reverses it.
 *
 * Bytes with no whitespace or backslash pass through unchanged, so the common
 * case is byte-identical to the old format and older readers keep working.
 */
static void syn_escape(char *dst, size_t dlen, const char *src, size_t slen)
{
    size_t o = 0;

    for (size_t i = 0; i < slen && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];

        if (c <= 0x20 || c == 0x7f || c == '\\') {
            if (o + 4 >= dlen)
                break;
            o += scnprintf(dst + o, dlen - o, "\\x%02x", c);
        } else {
            if (o + 1 >= dlen)
                break;
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}

/*
 * synapse_probe_read_log — drain ring buffer into buf for sysfs read.
 * Returns bytes written.
 */
ssize_t synapse_probe_read_log(char *buf, size_t buf_len)
{
    /* Static, not stack: a fully-escaped filename is 4x. Every use is inside
     * g_ring.lock, which serialises readers. */
    static char comm_esc[SYN_ESC_MAX_COMM];
    static char name_esc[SYN_ESC_MAX_FILENAME];
    static char line[SYN_LOG_LINE_MAX];
    size_t pos = 0;

    spin_lock(&g_ring.lock);
    int tail = atomic_read(&g_ring.tail);
    int head = atomic_read(&g_ring.head);

    /* Read up to 32 events per sysfs read to avoid huge pages */
    int max_read = min(head - tail, 32);

    for (int i = 0; i < max_read; i++) {
        int idx = (tail + i) % g_ring.size;
        struct synapse_syscall_event *e = &g_ring.events[idx];

        syn_escape(comm_esc, sizeof(comm_esc), e->comm, sizeof(e->comm));
        syn_escape(name_esc, sizeof(name_esc), e->filename, sizeof(e->filename));

        const char *fname = name_esc[0] ? name_esc : "-";
        /* Trailing "flags arg0" lets userspace type events without guessing
         * from syscall_nr and see arg0 (setuid: target uid). Readers that
         * stop at the filename keep working — the fields only append. */
        int len = scnprintf(line, sizeof(line),
            "%llu %u %u %u %s %s %02x %llu\n",
            e->timestamp_ns,
            e->pid, e->uid,
            e->syscall_nr,
            comm_esc,
            fname,
            e->flags,
            (unsigned long long)e->args[0]
        );

        /* Never emit a half line: leave the event queued for the next read
         * rather than handing userspace a truncated record to misparse. */
        if (pos + (size_t)len >= buf_len)
            break;

        memcpy(buf + pos, line, (size_t)len);
        pos += (size_t)len;
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

/*
 * syscall_uregs — recover the real syscall-argument registers.
 *
 * On x86_64 with CONFIG_ARCH_HAS_SYSCALL_WRAPPER (the only config where the
 * __x64_sys_* symbols we kprobe exist), each wrapper is called as
 * long __x64_sys_foo(const struct pt_regs *regs), so at kprobe entry the
 * SysV first argument — regs->di — is a *pointer* to the user task's
 * pt_regs. The actual syscall arguments (di, si, dx, r10, r8, r9) live
 * there, not in the wrapper's own register frame.
 *
 * Reading the wrapper frame directly (the previous behaviour) yielded the
 * pt_regs pointer itself as "arg0" and an unreadable userspace address for
 * every filename, so all path/arg-based detection silently never matched.
 */
static inline struct pt_regs *syscall_uregs(struct pt_regs *regs)
{
    return (struct pt_regs *)regs->di;
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
        /* Raw input devices: a userland keylogger reads keystrokes from
         * these. Only the compositor (synui) legitimately opens them, so
         * any other opener is worth a synguard verdict. */
        "/dev/input/",
        /* System persistence & injection surfaces. synguard already had rules
         * for several of these (ld.so.preload, cron, systemd units) that never
         * fired because the kmod wasn't reporting the paths. */
        "/etc/ld.so.preload", "/etc/cron", "/var/spool/cron/",
        "/etc/systemd/system/", "/etc/profile.d/", "/etc/xdg/autostart/",
        "/etc/rc.local",
        NULL
    };
    for (int i = 0; sensitive[i]; i++)
        if (strncmp(path, sensitive[i], strlen(sensitive[i])) == 0)
            return true;
    return false;
}

/*
 * is_persistence_path — per-user auto-run/persistence files whose home prefix
 * varies (/home/<user>/…), so they need a substring rather than prefix match.
 * A trojan drops itself here to survive logout/reboot. These files are *read*
 * constantly (e.g. .bashrc on every shell), so the caller only reports a
 * write/create — a read is not interesting.
 */
static bool is_persistence_path(const char *path)
{
    static const char *const markers[] = {
        "/.config/autostart/", "/.config/systemd/",
        "/.bashrc", "/.bash_profile", "/.profile",
        "/.zshrc", "/.zshenv", "/.xprofile",
        NULL
    };
    for (int i = 0; markers[i]; i++)
        if (strstr(path, markers[i]))
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
     * execve(filename, argv, envp): the filename pointer is the user
     * pt_regs' first argument. Recover it via syscall_uregs().
     */
    struct pt_regs *u = syscall_uregs(regs);
    const char __user *filename = (const char __user *)u->di;
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

    /* openat(dfd, filename, flags, mode): filename is 2nd arg, flags is 3rd. */
    struct pt_regs *u = syscall_uregs(regs);
    const char __user *filename = (const char __user *)u->si;
    if (!filename) return 0;
    int oflags = (int)u->dx;

    char kbuf[128] = {0};
    if (strncpy_from_user(kbuf, filename, sizeof(kbuf) - 1) <= 0)
        return 0;

    bool sens    = is_sensitive_path(kbuf);
    bool persist = sens ? false : is_persistence_path(kbuf);
    if (!sens && !persist) return 0;

    /* Persistence files are read on every shell/session start; only a
     * write/create/truncate is worth reporting. Sensitive system paths
     * (shadow, ssh keys, kernel) are reported on any access. */
    if (persist &&
        !(oflags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)))
        return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_openat, SYNAPSE_EVT_OPEN);
    e.args[0] = (u64)(unsigned int)oflags;   /* open flags → wire arg0 */
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

    struct pt_regs *u = syscall_uregs(regs);

    /* Skip AF_UNIX/AF_LOCAL (1) and AF_NETLINK (16): local IPC that every
     * desktop process spams (Plex, NetworkManager, wpa_supplicant …). Left
     * unfiltered it floods the lossy ring and buries real network activity.
     * We only care about IP-family sockets here. */
    long dom = (long)u->di;
    if (dom == AF_UNIX || dom == AF_NETLINK)
        return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_socket, SYNAPSE_EVT_SOCKET);
    e.args[0] = u->di;  /* domain */
    e.args[1] = u->si;  /* type   */
    e.args[2] = u->dx;  /* proto  */

    ring_push(&e);
    synapse_stat_event();
    return 0;
}

static struct kprobe kp_socket = {
    .symbol_name = "__x64_sys_socket",
    .pre_handler = socket_pre_handler,
};

/* ── connect kprobe ───────────────────────────────────────── */
/*
 * connect(fd, sockaddr, addrlen). We capture the *destination* for IPv4/IPv6
 * connections into e->filename as "A.B.C.D:port" / "[v6]:port" — this is the
 * signal synguard uses to spot worm scanning (fan-out to many hosts) and
 * trojan C2 beaconing. AF_UNIX/netlink/etc are skipped (local, high-volume,
 * uninteresting). Distinguished from socket() downstream by syscall_nr (42).
 */
static int connect_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    if (!synapse_events_enabled()) return 0;

    struct pt_regs *u = syscall_uregs(regs);
    void __user *uaddr = (void __user *)u->si;
    int addrlen = (int)u->dx;
    /*
     * Zeroed, and it must stay that way. addrlen is attacker-controlled and
     * only has to clear sizeof(sa_family_t) (2) to get past the check below,
     * so a short connect() copies just a couple of bytes into this buffer —
     * while the AF_INET branch then formats sin_addr (offset 4) and sin_port
     * (offset 2), i.e. bytes copy_from_user() never wrote. Uninitialised, that
     * is kernel stack rendered as an "IP:port" into the event ring, from where
     * it reaches syscall_log, synguard, synapd's context, and anything the AI
     * summarises. Any local process could do it in a loop.
     */
    struct sockaddr_storage ss = {0};

    if (!uaddr || addrlen < (int)sizeof(sa_family_t))
        return 0;
    if (addrlen > (int)sizeof(ss))
        addrlen = (int)sizeof(ss);
    if (copy_from_user(&ss, uaddr, addrlen))
        return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_connect, SYNAPSE_EVT_SOCKET);
    e.args[0] = ss.ss_family;

    /*
     * Zeroing stops the leak; this stops the lie. A 2-byte AF_INET connect()
     * would otherwise be logged as a confident "0.0.0.0:0" that the process
     * never asked for. Require the address the family actually defines before
     * claiming to have read one — the kernel is going to reject these with
     * EINVAL anyway.
     */
    if (ss.ss_family == AF_INET && addrlen < (int)sizeof(struct sockaddr_in))
        return 0;
    if (ss.ss_family == AF_INET6 && addrlen < (int)sizeof(struct sockaddr_in6))
        return 0;

    if (ss.ss_family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)&ss;
        scnprintf(e.filename, sizeof(e.filename), "%pI4:%u",
                  &in->sin_addr, ntohs(in->sin_port));
    } else if (ss.ss_family == AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&ss;
        scnprintf(e.filename, sizeof(e.filename), "[%pI6c]:%u",
                  &in6->sin6_addr, ntohs(in6->sin6_port));
    } else {
        return 0;   /* not an IP connection — ignore */
    }

    ring_push(&e);
    synapse_stat_event();
    return 0;
}

static struct kprobe kp_connect = {
    .symbol_name = "__x64_sys_connect",
    .pre_handler = connect_pre_handler,
};

/* ── ptrace kprobe ────────────────────────────────────────── */
static int ptrace_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    if (!synapse_events_enabled()) return 0;

    struct pt_regs *u = syscall_uregs(regs);
    long request = (long)u->di;
    /* Only report ATTACH and PEEKTEXT/DATA */
    if (request != PTRACE_ATTACH && request != 0 && request != 1)
        return 0;

    struct synapse_syscall_event e = {0};
    fill_event(&e, __NR_ptrace, SYNAPSE_EVT_PTRACE);
    e.args[0] = u->di;  /* request */
    e.args[1] = u->si;  /* pid */

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

    struct pt_regs *u = syscall_uregs(regs);
    uid_t target_uid = (uid_t)u->di;
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
    &kp_connect,
    &kp_ptrace,
    &kp_insmod,
    &kp_finit_module,
    &kp_setuid,
};

#define N_PROBES  ARRAY_SIZE(all_probes)

/* Track which probes were successfully registered — some symbols may not
 * be kprobeable on a given kernel, and enable/disable/unregister on a
 * never-registered kprobe is undefined. */
static bool probe_registered[ARRAY_SIZE(all_probes)];

/* ── Enable / disable ─────────────────────────────────────── */
static bool g_probes_enabled = true;

void synapse_probe_set_enabled(bool enabled)
{
    if (enabled == g_probes_enabled) return;
    g_probes_enabled = enabled;

    if (!g_ring.events) return;

    if (enabled) {
        for (int i = 0; i < (int)N_PROBES; i++)
            if (probe_registered[i]) enable_kprobe(all_probes[i]);
        pr_info("synapse_kmod: probes enabled\n");
    } else {
        for (int i = 0; i < (int)N_PROBES; i++)
            if (probe_registered[i]) disable_kprobe(all_probes[i]);
        pr_info("synapse_kmod: probes disabled\n");
    }
}

/* ── Self-integrity: are our probes still armed? ──────────── */
/*
 * Returns the number of probes we registered and intend to be active that
 * have been disarmed or removed out from under us (KPROBE_FLAG_DISABLED via
 * an external disable_kprobe(), or KPROBE_FLAG_GONE). 0 == healthy.
 *
 * This detects a targeted attempt to blind the monitor by disarming its
 * probes. It reads flags only, so it is safe from the watchdog timer
 * (softirq) context. Note: the global `echo 0 > .../kprobes/enabled` switch
 * disarms without setting per-probe flags and is not visible here — that
 * remains a known gap (no exported kprobes_all_disarmed).
 */
int synapse_probe_integrity_check(void)
{
    int i, tampered = 0;

    if (!g_probes_enabled) return 0;   /* we disabled them on purpose */

    for (i = 0; i < (int)N_PROBES; i++) {
        if (!probe_registered[i]) continue;
        if (all_probes[i]->flags &
            (KPROBE_FLAG_DISABLED | KPROBE_FLAG_GONE))
            tampered++;
    }
    return tampered;
}

/* ── Init / exit ──────────────────────────────────────────── */
int synapse_probe_init(int ring_size)
{
    int ret, i, ok = 0;

    ret = ring_init(ring_size);
    if (ret) return ret;

    /* Register probes individually — some symbols may not be
     * kprobeable on this kernel, and that's fine. */
    for (i = 0; i < (int)N_PROBES; i++) {
        ret = register_kprobe(all_probes[i]);
        if (ret) {
            pr_warn("synapse_kmod: kprobe %s failed: %d (skipping)\n",
                    all_probes[i]->symbol_name, ret);
            probe_registered[i] = false;
        } else {
            probe_registered[i] = true;
            ok++;
        }
    }

    if (ok == 0) {
        pr_err("synapse_kmod: no kprobes registered — probes disabled\n");
        ring_free();
        return -ENODEV;
    }

    pr_info("synapse_kmod: %d/%zu kprobes registered, ring_size=%d\n",
            ok, N_PROBES, ring_size);
    return 0;
}

void synapse_probe_exit(void)
{
    int i;
    if (g_ring.events) {
        for (i = 0; i < (int)N_PROBES; i++) {
            if (probe_registered[i])
                unregister_kprobe(all_probes[i]);
        }
        ring_free();
    }
}
