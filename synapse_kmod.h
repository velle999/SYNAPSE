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
 *   New syscalls (Linux 7.0 AI_CTX family):
 *     AI_CTX_SET    — process declares its intent to scheduler
 *     AI_CTX_GET    — process reads AI-assigned scheduling class
 *     AI_CTX_QUERY  — process asks AI directly (routed through kmod)
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
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

/* ── Process intent flags ─────────────────────────────────── */
/*
 * Passed to AI_CTX_SET syscall. Describes what a process
 * is about to do, so the AI scheduler can make informed decisions.
 */
#define AI_CTX_FLAG_COMPUTE     (1 << 0)  /* heavy CPU computation */
#define AI_CTX_FLAG_IO_HEAVY    (1 << 1)  /* lots of disk/network I/O */
#define AI_CTX_FLAG_LATENCY     (1 << 2)  /* latency-sensitive */
#define AI_CTX_FLAG_BACKGROUND  (1 << 3)  /* background task */
#define AI_CTX_FLAG_INFERENCE   (1 << 4)  /* AI inference workload */
#define AI_CTX_FLAG_INTERACTIVE (1 << 5)  /* direct user interaction */
#define AI_CTX_FLAG_EPHEMERAL   (1 << 6)  /* short-lived process */
#define AI_CTX_FLAG_TRUSTED     (1 << 7)  /* trusted system process */

/* ── AI_CTX syscall argument structures ──────────────────── */

/* AI_CTX_SET: declare process intent */
struct ai_ctx_set_args {
    uint32_t  flags;           /* AI_CTX_FLAG_* bitmask */
    char      intent[256];     /* natural language description (optional) */
    uint32_t  priority_hint;   /* suggested priority 0-99 */
    uint32_t  reserved[4];
};

/* AI_CTX_GET: read current AI scheduling class */
struct ai_ctx_get_result {
    ai_sched_class_t  sched_class;
    int               nice_value;      /* current nice adjustment */
    uint32_t          flags_applied;   /* which flags were used */
    char              reason[128];     /* explanation from AI */
    uint64_t          last_updated_ns;
};

/* AI_CTX_QUERY: ask AI a question, get response */
struct ai_ctx_query_args {
    char      prompt[512];
    char      response[1024];
    uint32_t  max_tokens;
    uint32_t  timeout_ms;
};

/* ── Syscall event record ─────────────────────────────────── */
/*
 * Written to /sys/kernel/synapse/syscall_log by kprobes.
 * Read by synapd for security analysis and context tracking.
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
    uint8_t   pad[3];
};

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
    uint32_t  active_contexts;   /* PIDs with active AI_CTX */
    uint32_t  kmod_version;
};
