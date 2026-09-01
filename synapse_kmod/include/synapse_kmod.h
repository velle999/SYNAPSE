/*
 * synapse_kmod.h — SynapseOS Kernel Module Interface
 *
 * Shared definitions between:
 *   - synapse_kmod.ko  (kernel module)
 *   - synapd           (AI daemon, userspace)
 *   - syn tools        (userspace utilities)
 *
 * This file is safe to include from both kernel and userspace.
 *
 * Kernel interface provided by synapse_kmod:
 *
 *   /sys/kernel/synapse/
 *     status        rw  — daemon writes heartbeat; kmod reads health
 *     ai_hints      w   — daemon writes scheduling hints
 *     syscall_log   r   — kmod writes syscall events; daemon reads
 *     stats         r   — kmod exposes counters
 *     config        rw  — runtime configuration knobs
 *
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 * https://github.com/velle999/SYNAPSE
 */
#pragma once

/*
 * Guard: kernel headers define __KERNEL__ when compiling modules.
 * Userspace code includes this file without __KERNEL__.
 */
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

/* ── Version ─────────────────────────────────────────────── */
#define SYNAPSE_KMOD_VERSION     "0.1.0"
#define SYNAPSE_KMOD_MAGIC       0x53594E4B   /* "SYNK" */

/* ── Sysfs paths (userspace) ──────────────────────────────── */
#define SYNAPSE_SYSFS_ROOT       "/sys/kernel/synapse"
#define SYNAPSE_SYSFS_STATUS     SYNAPSE_SYSFS_ROOT "/status"
#define SYNAPSE_SYSFS_AI_HINTS   SYNAPSE_SYSFS_ROOT "/ai_hints"
#define SYNAPSE_SYSFS_SYSCALL_LOG SYNAPSE_SYSFS_ROOT "/syscall_log"
#define SYNAPSE_SYSFS_STATS      SYNAPSE_SYSFS_ROOT "/stats"
#define SYNAPSE_SYSFS_CONFIG     SYNAPSE_SYSFS_ROOT "/config"

/* ── AI scheduling classes ────────────────────────────────── */
/*
 * AI_SCHED_* extends the standard Linux scheduling policy space.
 * The kernel module maps these to real CFS/RT parameters.
 */
typedef enum {
    AI_SCHED_NORMAL      = 0,   /* default CFS, no hint */
    AI_SCHED_INTERACTIVE = 1,   /* interactive: high wakeup priority */
    AI_SCHED_BATCH       = 2,   /* background batch: low priority */
    AI_SCHED_REALTIME    = 3,   /* near-RT: inference-critical path */
    AI_SCHED_IDLE        = 4,   /* below idle: cleanup tasks */
    AI_SCHED_INFERENCE   = 5,   /* synapd inference threads: priority boost */
    AI_SCHED_MAX         = 6,
} ai_sched_class_t;

/* ── Syscall event record ─────────────────────────────────── */
/*
 * Written to /sys/kernel/synapse/syscall_log by kprobes.
 * Read by synapd for security analysis and context tracking.
 *
 * sysfs line format (one event per line):
 *   <timestamp_ns> <pid> <uid> <syscall_nr> <comm> <filename|-> <flags:hex> <arg0> <ret|->
 * The trailing "<flags> <arg0>" pair was appended in 0.1.1 and <ret> after it;
 * readers must treat both as optional, since a reader may be newer than the
 * loaded module. For SYNAPSE_EVT_SETUID, arg0 is the target uid.
 *
 * <ret> is the SYSCALL'S RETURN VALUE, or "-" when this event was reported at
 * syscall entry and the outcome is therefore unknown. It exists because a
 * consumer cannot otherwise tell an open that happened from one the kernel
 * refused: the probes fire on ENTRY, so an ENOENT lookup failure and an EACCES
 * permission failure both produce an event indistinguishable from success.
 * synguard acted on those — it SIGKILLed two processes for opens that had
 * already been denied by ordinary DAC — which is the whole reason this field
 * is here. A negative <ret> is an errno: nothing was read, nothing was
 * written, and no enforcement action is warranted on the strength of it.
 *
 * <comm> and <filename> are ESCAPED: any byte <= 0x20, 0x7f, or '\' is written
 * as \xHH, so each is exactly one whitespace-delimited token. Without this a
 * comm containing a space ("Socket Thread") shifts every later field and the
 * reader picks up the comm's second word as the filename. Readers must
 * unescape; bytes needing no escape are emitted verbatim, so ordinary comms
 * and paths look exactly as they always did.
 */
struct synapse_syscall_event {
    uint64_t  timestamp_ns;
    uint32_t  pid;
    uint32_t  tgid;
    uint32_t  uid;
    uint32_t  syscall_nr;
    uint64_t  args[4];            /* first 4 syscall args */
    char      comm[16];           /* task->comm */
    char      filename[128];      /* for open/exec: filename */
    uint8_t   flags;              /* SYNAPSE_EVT_* */
    uint8_t   has_ret;            /* 1: `ret` is the syscall's return value */
    int32_t   ret;                /* syscall return; < 0 is -errno */
    uint8_t   pad[2];
};

/* Worst-case escaped sizes (every byte becomes \xHH) + NUL, and the longest
 * log line they can produce once the numeric fields are added. The numeric
 * slack covers timestamp(20) pid(10) uid(10) nr(10) flags(2) arg0(20) ret(11)
 * plus separators — 128 rather than a tight fit, because a wire field has been
 * appended twice now and scnprintf() truncates silently. */
#define SYN_ESC_MAX_COMM      (16  * 4 + 1)
#define SYN_ESC_MAX_FILENAME  (128 * 4 + 1)
#define SYN_LOG_LINE_MAX      (SYN_ESC_MAX_COMM + SYN_ESC_MAX_FILENAME + 128)

#define SYNAPSE_EVT_EXEC    0x01  /* execve/execveat */
#define SYNAPSE_EVT_OPEN    0x02  /* open/openat sensitive file */
#define SYNAPSE_EVT_SOCKET  0x04  /* socket/connect/bind */
#define SYNAPSE_EVT_PTRACE  0x08  /* ptrace/process injection */
#define SYNAPSE_EVT_MODULE  0x10  /* init_module/finit_module */
#define SYNAPSE_EVT_MOUNT   0x20  /* mount/umount */
#define SYNAPSE_EVT_SETUID  0x40  /* setuid/setgid/capset */

/* ── Hint wire format ─────────────────────────────────────── */
/*
 * Text protocol written by synapd to /sys/kernel/synapse/ai_hints:
 *   "HINT pid=<pid> nice=<delta> class=<class_name>\n"
 *
 * Example:
 *   "HINT pid=1234 nice=-5 class=interactive\n"
 *   "HINT pid=5678 nice=15 class=batch\n"
 *
 * The kmod parses this and applies adjustments via kernel APIs.
 */
#define SYNAPSE_HINT_MAX_LEN  256

/* ── Status wire format ───────────────────────────────────── */
/*
 * Text written by synapd to /sys/kernel/synapse/status:
 *   "ALIVE requests=<n> active=<n> model=<0|1>\n"
 *   "READY\n"
 *   "SHUTDOWN\n"
 *
 * If the kmod sees no ALIVE heartbeat for >30s, it logs a warning
 * and falls back to stock Linux scheduling for all processes.
 */
#define SYNAPSE_STATUS_MAX_LEN  256

/* ── Module stats ─────────────────────────────────────────── */
struct synapse_stats {
    uint64_t  events_captured;
    uint64_t  hints_applied;
    uint64_t  hints_rejected;
    uint64_t  syscalls_hooked;
    uint64_t  ai_queries_routed;
    uint64_t  daemon_heartbeats;
    uint64_t  daemon_timeouts;
    uint32_t  active_contexts;   /* PIDs carrying a scheduling hint */
    uint32_t  kmod_version;
};


/* ── what synapse_main.c owns, for the rest of the module ───────────────────
 *
 * ⚠ THIS IS THE ONLY DECLARATION OF ANY OF THESE, AND IT USED TO BE THREE.
 * Every one of them is defined in synapse_main.c and called from another
 * translation unit, and each caller carried its OWN `extern` line at the top
 * of its file — synapse_sysfs.c had eight, synapse_sched.c six,
 * synapse_probe.c three. Nothing checked any of them against the definition:
 * a signature that changed on one side would link cleanly and go wrong at run
 * time, in a kernel module, where "go wrong" is the whole machine.
 *
 * The compiler was saying so the entire time — seventeen `no previous
 * prototype` warnings on every build, one per function, which is exactly what
 * that warning is for. Declaring them here silences it by fixing it.
 *
 * ⚠ KERNEL SIDE ONLY. This header is included by synapd and the syn tools as
 * well; `u64` and `struct workqueue_struct` do not exist out there.
 */
#ifdef __KERNEL__

struct workqueue_struct;

/* Module state, and the pin that keeps it loaded. */
int   synapse_kmod_set_pinned(bool pin);
bool  synapse_kmod_is_pinned(void);
u64   synapse_integrity_alert_count(void);

/* The daemon's heartbeat, as seen from in here. */
void  synapse_daemon_heartbeat(void);
void  synapse_daemon_shutdown(void);
bool  synapse_daemon_is_alive(void);

/* The two runtime switches every hot path checks first. */
bool  synapse_events_enabled(void);
bool  synapse_sched_enabled(void);

/* Counters. Each is one atomic increment; they are functions so the state
 * stays private to synapse_main.c. */
void  synapse_stat_event(void);
void  synapse_stat_hint_ok(void);
void  synapse_stat_hint_fail(void);
void  synapse_stat_syscall(void);
void  synapse_stat_query(void);
void  synapse_ctx_inc(void);
void  synapse_ctx_dec(void);

struct workqueue_struct *synapse_get_wq(void);
void  synapse_get_stats(struct synapse_stats *out);

#endif /* __KERNEL__ */
