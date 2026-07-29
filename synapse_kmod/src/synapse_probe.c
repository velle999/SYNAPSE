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
 *
 * head and tail are free-running counters that are never reset, so they WILL
 * wrap. Every use of them here is therefore unsigned: u32 wraparound is
 * defined, and `head - tail` stays the true distance across the wrap, which is
 * the same trick kfifo uses.
 *
 * They were plain signed ints, and that was a live out-of-bounds write waiting
 * on the clock. atomic_t is a signed 32-bit counter; this box logs ~440k
 * events/hour, so head reaches INT_MAX after roughly 200 days of uptime and
 * goes negative. C's % keeps the sign of the dividend, so `head % size` then
 * yields a NEGATIVE index and the memcpy below writes a ~200-byte event before
 * the start of the ring — kernel heap corruption on an uptime threshold, with
 * nothing in the logs leading up to it.
 *
 * It is the INDEX that breaks, not the distance: `head - tail` keeps returning
 * the correct gap across the wrap even when signed, because the kernel builds
 * with -fno-strict-overflow. Simulated at the boundary, the old code's push
 * index goes 4095 -> 0 -> -4095 while the gap stays a steady 100. So the read
 * path is not spared either — once tail wraps, `(tail + i) % size` is equally
 * negative and reads out of bounds.
 */
static void ring_push(const struct synapse_syscall_event *evt)
{
    spin_lock(&g_ring.lock);
    u32 size = (u32)g_ring.size;
    u32 head = (u32)atomic_read(&g_ring.head);
    memcpy(&g_ring.events[head % size], evt, sizeof(*evt));
    atomic_inc(&g_ring.head);
    /* Advance tail if we've lapped. Unsigned, so this stays correct when the
     * counters straddle the wrap. */
    if ((u32)atomic_read(&g_ring.head) - (u32)atomic_read(&g_ring.tail) > size)
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
 * format_event — render one event as its wire line. Caller holds g_ring.lock.
 *
 * The scratch buffers are static rather than stack because a fully-escaped
 * filename is 4x its raw size; the lock serialises every caller.
 */
static int format_event(char *line, size_t line_len,
                        const struct synapse_syscall_event *e)
{
    static char comm_esc[SYN_ESC_MAX_COMM];
    static char name_esc[SYN_ESC_MAX_FILENAME];

    syn_escape(comm_esc, sizeof(comm_esc), e->comm, sizeof(e->comm));
    syn_escape(name_esc, sizeof(name_esc), e->filename, sizeof(e->filename));

    /* Trailing "flags arg0" lets userspace type events without guessing
     * from syscall_nr and see arg0 (setuid: target uid, openat: O_* flags).
     * Readers that stop at the filename keep working — fields only append.
     *
     * "ret" is last and is "-" for every probe that still reports at syscall
     * ENTRY, where the outcome is not yet known. It is NOT rendered as a
     * number-that-means-unknown: any sentinel integer is a return value some
     * syscall can legitimately produce, and a consumer that mistook one for
     * the other would draw exactly the wrong conclusion about whether the
     * access succeeded. "-" cannot be misread, and matches how an absent
     * filename has always been written. */
    char ret_buf[16];
    if (e->has_ret)
        scnprintf(ret_buf, sizeof(ret_buf), "%d", e->ret);
    else
        strscpy(ret_buf, "-", sizeof(ret_buf));

    return scnprintf(line, line_len,
        "%llu %u %u %u %s %s %02x %llu %s\n",
        e->timestamp_ns,
        e->pid, e->uid,
        e->syscall_nr,
        comm_esc,
        name_esc[0] ? name_esc : "-",
        e->flags,
        (unsigned long long)e->args[0],
        ret_buf
    );
}

u32 synapse_probe_ring_tail(void)
{
    return (u32)atomic_read(&g_ring.tail);
}

/*
 * synapse_probe_read_from — copy events after *cursor into buf.
 *
 * THE READ IS NOT DESTRUCTIVE. Only the caller's own cursor advances; the
 * ring's tail is moved by ring_push() alone, when a new event overwrites an
 * old one. That is the whole point:
 *
 * This used to `atomic_inc(&g_ring.tail)` per event emitted, making the ring a
 * single-consumer queue exposed as a file with nothing enforcing the single
 * consumer. Any second reader silently stole events from synguard. It was a
 * live footgun — `cat /sys/kernel/synapse/syscall_log` while testing a
 * detection rule consumed the very event under test, and did, twice — and a
 * post-root evasion primitive: drain the ring in a loop and events never reach
 * the detector, with no rmmod and no kprobe disarm for the integrity watchdog
 * to notice.
 *
 * With per-reader cursors, concurrent readers cannot affect each other. The
 * cost is that a reader can now fall behind and lose events instead of holding
 * them; that is reported rather than hidden, via *dropped.
 *
 * All arithmetic is u32: head and tail free-run and wrap after ~200 days at
 * this event rate, and a signed `%` would then yield a NEGATIVE index — an
 * out-of-bounds access handed to userspace. `head - cursor` is correct across
 * the wrap precisely because it is unsigned.
 */
ssize_t synapse_probe_read_from(char *buf, size_t buf_len,
                                u32 *cursor, u64 *dropped)
{
    static char line[SYN_LOG_LINE_MAX];
    size_t pos = 0;

    spin_lock(&g_ring.lock);

    u32 tail = (u32)atomic_read(&g_ring.tail);
    u32 head = (u32)atomic_read(&g_ring.head);
    u32 cur  = *cursor;

    /*
     * Has the writer lapped this reader? `cur - tail` underflows to a huge
     * value exactly when cur is behind tail, which is the test we want. The
     * events between are gone; skip forward and say how many were missed
     * rather than reading whatever now occupies those slots.
     */
    if ((u32)(cur - tail) > (u32)g_ring.size) {
        if (dropped)
            *dropped += (u64)(u32)(tail - cur);
        cur = tail;
    }

    while (cur != head) {
        struct synapse_syscall_event *e =
            &g_ring.events[cur % (u32)g_ring.size];

        int len = format_event(line, sizeof(line), e);

        /* Never emit a half line: leave the event for the next read rather
         * than handing userspace a truncated record to misparse. */
        if (pos + (size_t)len >= buf_len)
            break;

        memcpy(buf + pos, line, (size_t)len);
        pos += (size_t)len;
        cur++;
    }

    *cursor = cur;
    spin_unlock(&g_ring.lock);

    return (ssize_t)pos;
}

/*
 * synapse_probe_read_log — the sysfs syscall_log view.
 *
 * A bounded, NON-DESTRUCTIVE peek at the most recent events, for a human with
 * a shell. It consumes nothing, so `cat`ting this file can no longer blind
 * synguard. Real consumers use /dev/synapse-events, which gives each open its
 * own cursor.
 */
ssize_t synapse_probe_read_log(char *buf, size_t buf_len)
{
    static char line[SYN_LOG_LINE_MAX];
    size_t pos = 0;

    spin_lock(&g_ring.lock);

    u32 tail = (u32)atomic_read(&g_ring.tail);
    u32 head = (u32)atomic_read(&g_ring.head);

    /* Show the tail end of the ring: the newest events are what a human
     * looking at this file wants, and only ~PAGE_SIZE of them fit anyway. */
    u32 avail = head - tail;
    u32 show  = min(avail, 32u);
    u32 cur   = head - show;

    while (cur != head) {
        struct synapse_syscall_event *e =
            &g_ring.events[cur % (u32)g_ring.size];

        int len = format_event(line, sizeof(line), e);
        if (pos + (size_t)len >= buf_len)
            break;

        memcpy(buf + pos, line, (size_t)len);
        pos += (size_t)len;
        cur++;
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
    /*
     * Identity in the INITIAL namespaces, never the task's own view of itself.
     *
     * These were task_pid_vnr()/task_tgid_vnr() and
     * from_kuid_munged(current_user_ns(), ...), i.e. the pid and uid as seen
     * from inside whatever namespace the traced task happens to sit in. For a
     * security monitor that is exactly backwards: it lets the subject choose
     * the identity it is reported under.
     *
     * The uid was the dangerous half, and it was reachable by any local user.
     * kernel.unprivileged_userns_clone is 1 on a stock Arch kernel, so
     * `unshare -U -r` maps the caller to uid 0 inside its own user namespace —
     * and every event it generated was logged as uid=0. Measured, not
     * theorised: as uid 1000, `unshare -U -r /tmp/probe` produced ring records
     * reading `... 0 59 unshare /tmp/probe ...`.
     *
     * That is a detection bypass, because synguard's shipped policy has
     *   rule allow-root-exec { event exec  uid 0  verdict log  priority 5 }
     * and first match wins on the lowest priority number. So an unprivileged
     * user in a user namespace matched allow-root-exec ahead of every
     * exec-path rule (escalate-exec-from-tmp is 35, the alert-*-exec rules are
     * 26-46) and had all of their execs quietly downgraded to "log".
     *
     * The pid half is the same mistake with a different consequence: synguard
     * resolves these numbers against /proc in the ROOT namespace, so a
     * container-local pid pointed it at an unrelated process — a misdirected
     * scheduling hint today, and a misdirected kill if a deny rule is ever
     * added.
     *
     * init_user_ns/global pid it is. This is the identity that matches what
     * every consumer of syscall_log can actually look up.
     */
    e->pid          = task_pid_nr(task);
    e->tgid         = task_tgid_nr(task);
    e->uid          = from_kuid_munged(&init_user_ns, task_uid(task));
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
 *
 * THIS LIST IS THE REACHABILITY BOUNDARY FOR synguard's `event open` RULES.
 * A rule on a path outside it parses, loads, and counts toward the "N
 * enforceable rules" banner, but the kmod never reports the open, so the rule
 * can never match. Nothing said so, which is how deny-bpf-canary — the
 * positive control that exists purely to prove enforcement can fire — became
 * the one rule guaranteed not to fire (its /var/lib/synguard/ path was not
 * listed). A control that cannot trip is worse than no control: it reads as a
 * quiet system.
 *
 * So the list is published at /sys/kernel/synapse/sensitive_paths and
 * synguard checks every deny rule against it at load time. Add paths HERE and
 * only here; userspace reads this array rather than keeping a copy that drifts.
 */
const char *const synapse_sensitive_paths[] = {
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
    /* synguard's own state directory. baseline.db is what "known good"
     * means, so tampering with it is worth seeing; and this is what makes
     * the bpf-canary positive control reachable at all. */
    "/var/lib/synguard/",
    NULL
};

static bool is_sensitive_path(const char *path)
{
    for (int i = 0; synapse_sensitive_paths[i]; i++)
        if (strncmp(path, synapse_sensitive_paths[i],
                    strlen(synapse_sensitive_paths[i])) == 0)
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
    /* The result is deliberately discarded, but say so: strncpy_from_user() is
     * __must_check, and an ignored -EFAULT here is harmless only because `e` is
     * zero-initialised, so a failed copy leaves an empty filename rather than
     * stack contents. Stating that keeps the build clean, so a real
     * unused-result warning elsewhere is not lost in the noise. */
    if (filename && strncpy_from_user(e.filename, filename,
                                      sizeof(e.filename) - 1) < 0)
        e.filename[0] = '\0';

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

/* ── openat kretprobe ─────────────────────────────────────── */
/*
 * openat is the one probe that reports at syscall EXIT rather than entry, so
 * the event can carry whether the open actually happened.
 *
 * Entry-time reporting made a failed open indistinguishable from a successful
 * one. Every consumer therefore had to treat an attempt as an access, and
 * synguard's deny path did exactly that: it SIGKILLed a process for opening a
 * root-owned 0600 file it could never have read (EACCES at inode_permission),
 * and another for probing an ld.so preload file that did not exist (ENOENT at
 * lookup). Neither process ever saw a byte. The kernel had already refused
 * both, and the enforcement added nothing but a dead process tree.
 *
 * The filter still runs at entry, where the filename argument is a valid user
 * pointer and the O_* flags are readable — see the note on syscall_uregs(),
 * whose pt_regs indirection is only meaningful there. What the entry handler
 * decides to report is stashed in the kretprobe instance and pushed by the
 * return handler, which adds regs_return_value().
 *
 * The cost is one rethook instance per openat SYSTEM-WIDE, taken before the
 * entry handler runs and recycled immediately for the ~99.9% of opens the path
 * filter rejects. That is a freelist pop and push. The exhaustion case is the
 * one worth watching: no instance means no return probe and the event is lost
 * ENTIRELY, where the old entry-only probe would have recorded it. That must
 * never be silent, so kr_openat.nmissed is published in the stats attribute —
 * this ring has been through one episode of losing events with every counter
 * reading zero, and once was enough.
 */
struct openat_ctx {
    char kbuf[128];
    int  oflags;
};

static int openat_entry_handler(struct kretprobe_instance *ri,
                                struct pt_regs *regs)
{
    struct openat_ctx *ctx = (struct openat_ctx *)ri->data;

    if (!synapse_events_enabled()) return 1;

    /* openat(dfd, filename, flags, mode): filename is 2nd arg, flags is 3rd. */
    struct pt_regs *u = syscall_uregs(regs);
    const char __user *filename = (const char __user *)u->si;
    if (!filename) return 1;
    int oflags = (int)u->dx;

    char kbuf[128] = {0};
    if (strncpy_from_user(kbuf, filename, sizeof(kbuf) - 1) <= 0)
        return 1;

    bool sens    = is_sensitive_path(kbuf);
    bool persist = sens ? false : is_persistence_path(kbuf);
    if (!sens && !persist) return 1;

    /* Persistence files are read on every shell/session start; only a
     * write/create/truncate is worth reporting. Sensitive system paths
     * (shadow, ssh keys, kernel) are reported on any access. */
    if (persist &&
        !(oflags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)))
        return 1;

    /* Returning 0 arms the return handler, which is what holds the instance
     * for the duration of the syscall. Everything above returns 1 so the
     * instance is recycled now. */
    memcpy(ctx->kbuf, kbuf, sizeof(ctx->kbuf));
    ctx->oflags = oflags;
    return 0;
}

static int openat_ret_handler(struct kretprobe_instance *ri,
                              struct pt_regs *regs)
{
    struct openat_ctx *ctx = (struct openat_ctx *)ri->data;
    struct synapse_syscall_event e = {0};

    /* Still the same task at syscall exit, so comm/pid/uid are the caller's.
     * The timestamp is now the completion time rather than the attempt time —
     * a sub-microsecond shift on a syscall, and the more accurate one to
     * report for an access that did occur. */
    fill_event(&e, __NR_openat, SYNAPSE_EVT_OPEN);
    e.args[0] = (u64)(unsigned int)ctx->oflags;   /* open flags → wire arg0 */
    strncpy(e.filename, ctx->kbuf, sizeof(e.filename) - 1);

    e.ret     = (int32_t)regs_return_value(regs);
    e.has_ret = 1;

    ring_push(&e);
    synapse_stat_event();
    return 0;
}

/*
 * maxactive bounds concurrent in-flight instances. Only opens that pass the
 * path filter hold one across the syscall; the rest are recycled inside the
 * entry handler, so the live count is bounded by CPUs plus however many
 * sensitive opens are blocked in lookup at once. 64 is generous for that, and
 * nmissed says so if it ever is not.
 */
static struct kretprobe kr_openat = {
    .kp.symbol_name = "__x64_sys_openat",
    .entry_handler  = openat_entry_handler,
    .handler        = openat_ret_handler,
    .data_size      = sizeof(struct openat_ctx),
    .maxactive      = 64,
};

/* Published in the stats attribute: how many openat events were lost because
 * no return instance was free. Losing events is the failure this ring has
 * actually suffered, so it gets a counter rather than a comment. */
unsigned long synapse_probe_openat_missed(void)
{
    return kr_openat.nmissed;
}

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
/* openat is NOT here: it is a kretprobe and registers separately. Its inner
 * kp is still handed to the integrity watchdog below, so disarming it is as
 * visible as disarming any of these. */
static struct kprobe *all_probes[] = {
    &kp_execve,
    &kp_execveat,
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

/* Same, for the openat kretprobe, which is not in all_probes[]. */
static bool openat_registered;

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
        if (openat_registered) enable_kretprobe(&kr_openat);
        pr_info("synapse_kmod: probes enabled\n");
    } else {
        for (int i = 0; i < (int)N_PROBES; i++)
            if (probe_registered[i]) disable_kprobe(all_probes[i]);
        if (openat_registered) disable_kretprobe(&kr_openat);
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

    /* The kretprobe's inner kp carries the same flags, so openat is covered
     * by the watchdog exactly as it was when it was a plain kprobe. Leaving it
     * out would have made the file-open probe the one probe an attacker could
     * disarm unobserved. */
    if (openat_registered &&
        (kr_openat.kp.flags & (KPROBE_FLAG_DISABLED | KPROBE_FLAG_GONE)))
        tampered++;

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

    /* openat separately: it is a kretprobe so it can report the syscall's
     * return value. A failure here is not fatal for the same reason the others
     * are not, but it is louder — openat is the probe the file-access rules are
     * built on, and losing it silently would leave a policy that looks armed
     * with nothing feeding it. */
    ret = register_kretprobe(&kr_openat);
    if (ret) {
        pr_warn("synapse_kmod: kretprobe %s failed: %d — file-open events "
                "are NOT being reported this boot\n",
                kr_openat.kp.symbol_name, ret);
        openat_registered = false;
    } else {
        openat_registered = true;
        ok++;
    }

    if (ok == 0) {
        pr_err("synapse_kmod: no kprobes registered — probes disabled\n");
        ring_free();
        return -ENODEV;
    }

    pr_info("synapse_kmod: %d/%zu probes registered (openat=kretprobe), "
            "ring_size=%d\n", ok, N_PROBES + 1, ring_size);
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
        /* unregister_kretprobe() waits out in-flight return instances, so no
         * handler can still be running against the ring we free below. */
        if (openat_registered) {
            unregister_kretprobe(&kr_openat);
            openat_registered = false;
        }
        ring_free();
    }
}
