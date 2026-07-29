/*
 * synguard.h — SynapseOS AI Security Monitor
 *
 * synguard is the security daemon that watches the syscall event
 * stream from synapse_kmod, runs AI classification on suspicious
 * activity, and enforces a rule-based + AI-assisted policy.
 *
 * Architecture:
 *
 *   synapse_kmod
 *      │ /sys/kernel/synapse/syscall_log  (event stream)
 *      │ /sys/kernel/synapse/ai_hints     (kill/block signals out)
 *      ▼
 *   synguard
 *      ├── event_reader  — drains syscall_log ring buffer
 *      ├── rule_engine   — fast static rules (no AI, microsecond latency)
 *      ├── ai_classifier — calls synapd for threat scoring
 *      ├── action_engine — kill, alert, quarantine, log
 *      ├── audit_log     — append-only event + decision log
 *      └── ipc_server    — Unix socket for 'syn guard' CLI queries
 *
 * Decision pipeline for each event:
 *   1. Fast rules  → ALLOW / DENY / ESCALATE
 *   2. If ESCALATE → AI classification (NORMAL / SUSPICIOUS / BLOCK)
 *   3. Action based on policy + AI verdict
 *
 * Policy modes:
 *   ENFORCE  — block on DENY/BLOCK verdicts
 *   AUDIT    — log everything, never block
 *   LEARNING — log + ask AI to build a baseline profile
 *   LOCKDOWN — block everything not in allowlist
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>

/* ── Version ─────────────────────────────────────────────── */
#define SYNGUARD_VERSION      "0.1.0-synapse"

/* ── Paths ────────────────────────────────────────────────── */
#define SYNGUARD_SOCKET_PATH  "/run/synapd/synguard.sock"
#define SYNGUARD_PID_FILE     "/run/synapd/synguard.pid"

/* Security-verdict broadcast feed ("syn guard watch"). Placed directly in
 * /run (world-traversable) with a 0666 socket so the unprivileged compositor
 * can subscribe and colour windows by verdict. Read-only stream of records. */
#ifndef SYNGUARD_SECFEED_SOCKET   /* overridable for packaging / tests */
#define SYNGUARD_SECFEED_SOCKET "/run/synguard.sock"
#endif
#define SG_SECFEED_MAGIC        0x53474656u   /* "SGFV" */
#define SG_SECFEED_VERSION      1
#define SYNGUARD_AUDIT_LOG    "/var/log/synguard/audit.log"
#define SYNGUARD_RULES_DIR    "/etc/synguard/rules.d/"
#define SYNGUARD_STATE_DIR    "/var/lib/synguard/"
#define SYNGUARD_BASELINE     "/var/lib/synguard/baseline.db"

/* ── Sysfs paths (from synapse_kmod) ─────────────────────── */
#define KMOD_SYSCALL_LOG      "/sys/kernel/synapse/syscall_log"

/*
 * The event feed proper. Every open() gets its own cursor, so a second reader
 * (an admin with `cat`, or an attacker draining the ring to blind us) cannot
 * consume our events -- which reading syscall_log used to do, because that
 * advanced the ring's single shared tail.
 *
 * We reopen it each poll and carry the cursor across with lseek() rather than
 * holding the fd. An held fd pins the module (fops.owner) and would make
 * `rmmod synapse_kmod` fail with EBUSY for as long as synguard runs, breaking
 * the DKMS upgrade and reload workflow.
 *
 * Absent = an older kmod, where syscall_log is still the destructive drain.
 */
#define KMOD_EVENT_DEV        "/dev/synapse-events"

/*
 * How many read() batches one poll cycle may drain before yielding. At 32K a
 * read (~470 events) this is far more than a full ring, so in practice the
 * loop always ends because the ring is empty; the cap exists only so a
 * pathological producer cannot hold the thread forever and stall the canary.
 */
#define SG_MAX_BATCHES_PER_CYCLE  256
#define KMOD_AI_HINTS         "/sys/kernel/synapse/ai_hints"
#define KMOD_STATUS           "/sys/kernel/synapse/status"

/* ── synapd socket ────────────────────────────────────────── */
#define SYNAPD_SOCKET_PATH    "/run/synapd/synapd.sock"

/* ── Event types (mirrors kmod SYNAPSE_EVT_*) ────────────── */
typedef enum {
    EVT_EXEC    = 0x01,
    EVT_OPEN    = 0x02,
    EVT_SOCKET  = 0x04,
    EVT_PTRACE  = 0x08,
    EVT_MODULE  = 0x10,
    EVT_MOUNT   = 0x20,
    EVT_SETUID  = 0x40,
    EVT_UNKNOWN = 0x00,
} evt_type_t;

/* ── Access mode (open events only) ──────────────────────── */
/*
 * Narrows an `open` rule to reads or writes. For open events the kmod ships
 * the O_* flags as arg0, but nothing consulted them: every rule named
 * "*-write" also fired on reads, and the persistence surfaces are *read*
 * constantly by the system itself (systemd walks /etc/systemd/system, every
 * login shell sources /etc/profile.d). That buried the real detections under
 * thousands of benign lines per boot.
 */
typedef enum {
    ACCESS_ANY   = 0,   /* default — flags are not consulted */
    ACCESS_READ  = 1,
    ACCESS_WRITE = 2,
} sg_access_t;

/* ── Raw event from kmod ──────────────────────────────────── */
typedef struct {
    uint64_t  timestamp_ns;
    uint32_t  pid;
    uint32_t  uid;
    uint32_t  syscall_nr;
    uint8_t   evt_type;       /* EVT_* */
    uint8_t   has_arg0;       /* wire carried arg0 (newer kmod log format) */
    uint8_t   has_ret;        /* wire carried the syscall's return value */
    int32_t   ret;            /* syscall return; < 0 is -errno */
    uint64_t  arg0;           /* first syscall arg (setuid: target uid) */
    char      comm[16];
    char      filename[128];
} sg_event_t;

/*
 * has_ret == 0 means the kmod reported this event at syscall ENTRY and the
 * outcome is unknown — every probe but openat still does, and an older kmod
 * does for openat too. It does NOT mean the syscall succeeded. Code that acts
 * on an event must treat "unknown" as its own case, because the alternative is
 * what shipped: an attempt was read as an access, and the deny path killed two
 * processes for opens the kernel had already refused with EACCES and ENOENT.
 */
#define SG_EVENT_FAILED(e)   ((e)->has_ret && (e)->ret < 0)

/* ── Rule verdict ─────────────────────────────────────────── */
typedef enum {
    VERDICT_ALLOW     = 0,   /* permit, no further action */
    VERDICT_LOG       = 1,   /* permit, log the event */
    VERDICT_ALERT     = 2,   /* permit, generate alert */
    VERDICT_ESCALATE  = 3,   /* defer to AI classifier */
    VERDICT_DENY      = 4,   /* deny (SIGKILL in ENFORCE mode) */
    VERDICT_QUARANTINE= 5,   /* isolate process (future: namespace jail) */
} sg_verdict_t;

/* ── AI threat score ──────────────────────────────────────── */
typedef enum {
    THREAT_NONE       = 0,
    THREAT_LOW        = 1,
    THREAT_MEDIUM     = 2,
    THREAT_HIGH       = 3,
    THREAT_CRITICAL   = 4,
} sg_threat_t;

/* ── AI classification result ─────────────────────────────── */
typedef struct {
    sg_threat_t   threat_level;
    sg_verdict_t  verdict;
    char          reason[256];
    float         confidence;   /* 0.0 - 1.0 */
} sg_ai_result_t;

/* ── Rule ─────────────────────────────────────────────────── */
#define RULE_MAX_PATTERN  256
#define RULE_MAX_NAME     64

/* Kernel-side rule capacity, mirrored here so synguard_state_t does not have
 * to include sg_bpf.h (which is built only when BPF-LSM is available, and
 * includes this header itself). core.c static-asserts the two agree. */
#define SG_MAX_KERNEL_RULES  64

typedef struct sg_rule {
    char          name[RULE_MAX_NAME];
    uint8_t       evt_mask;           /* EVT_* bitmask */
    uint32_t      uid_match;          /* UID_ANY = 0xFFFFFFFF */
    char          comm_pattern[RULE_MAX_PATTERN];   /* fnmatch */
    char          path_pattern[RULE_MAX_PATTERN];   /* fnmatch */
    sg_access_t   access_mode;        /* ACCESS_ANY unless the rule says otherwise */
    sg_verdict_t  verdict;
    int           priority;           /* lower = higher priority */
    int           enabled;
    struct sg_rule *next;
} sg_rule_t;

#define UID_ANY  0xFFFFFFFFu

/* ── Policy mode ──────────────────────────────────────────── */
typedef enum {
    MODE_ENFORCE  = 0,
    MODE_AUDIT    = 1,
    MODE_LEARNING = 2,
    MODE_LOCKDOWN = 3,
} sg_mode_t;

/* ── Config ───────────────────────────────────────────────── */
typedef struct {
    sg_mode_t   mode;
    int         ai_enabled;        /* use synapd for classification */
    int         bpf_enforce;       /* arm the BPF-LSM gate, so a lowered deny rule
                                      is refused in-kernel rather than only killed
                                      after the fact. Off by default: the kernel
                                      path PREVENTS, and turning that on for every
                                      install has to be deliberate. */
    int         ai_enforce;        /* let an AI verdict escalate to DENY/QUARANTINE.
                                      Off by default: the classifier is advisory, and a
                                      hallucinated "deny" must never SIGKILL the login
                                      chain. Rule-verdict denies are unaffected. */
    int         ai_timeout_ms;     /* max wait for AI verdict */
    float       ai_threshold;      /* escalate to AI if score > this */
    int         log_level;
    int         audit_enabled;
    const char *audit_log_path;
    const char *rules_dir;
    int         poll_interval_ms;  /* how often to drain kmod ring */
} sg_config_t;

/* ── Process baseline entry ───────────────────────────────── */
typedef struct {
    char     comm[16];
    uint32_t typical_evt_mask;     /* events this process normally makes */
    uint32_t seen_count;
    time_t   first_seen;
    time_t   last_seen;
} sg_baseline_entry_t;

/* ── Alert ────────────────────────────────────────────────── */
typedef struct {
    time_t       timestamp;
    sg_event_t   event;
    sg_verdict_t verdict;
    sg_threat_t  threat;
    char         reason[512];
    char         action_taken[128];
} sg_alert_t;

/* ── Stats ────────────────────────────────────────────────── */
/*
 * _Atomic because these are written by the reader thread and by the AI
 * classifier worker, and read by main every 60s to print the stats line.
 * Plain uint64_t made that a data race — three of them, which ThreadSanitizer
 * flagged on the very first run once a worker thread existed to expose the
 * pattern properly. A torn counter in a log line is harmless in practice, but
 * it is still UB, and "the numbers are probably fine" is a poor foundation for
 * the one output an operator uses to decide whether the detector is healthy.
 *
 * Lock-free on x86_64, and `++` on an _Atomic lvalue is an atomic RMW, so all
 * 21 increment sites stay exactly as they were. start_time is set once before
 * any thread exists and stays plain.
 */
typedef struct {
    _Atomic uint64_t  events_processed;
    _Atomic uint64_t  rules_matched;
    _Atomic uint64_t  ai_queries;
    _Atomic uint64_t  ai_timeouts;
    _Atomic uint64_t  ai_skipped;      /* classifications skipped, queue full */
    _Atomic uint64_t  denials;
    _Atomic uint64_t  alerts;
    _Atomic uint64_t  quarantines;
    _Atomic uint64_t  protected_skips; /* actions refused on a protected pid */
    _Atomic uint64_t  stale_pid_skips; /* refused: pid no longer the culprit */
    _Atomic uint64_t  failed_syscall_skips; /* refused: the syscall itself failed */
    _Atomic uint64_t  kernel_enforced_skips; /* refused: the BPF gate owns this rule */
    _Atomic uint64_t  events_dropped;  /* ring lapped before we read them */
    _Atomic uint64_t  reader_lag_ms;   /* age of the newest event processed */
    _Atomic uint64_t  reader_lag_max_ms; /* worst lag, so a stall still shows */
    _Atomic uint64_t  alerts_suppressed; /* repeats collapsed into a summary */
    time_t            start_time;
} sg_stats_t;

/* ── Global daemon state ──────────────────────────────────── */
typedef struct synguard_state {
    volatile int     running;
    int              debug;

    sg_config_t      config;
    sg_stats_t       stats;

    /* Rule engine */
    sg_rule_t       *rules_head;
    int              rules_count;
    pthread_rwlock_t rules_lock;

    /* kmod interface */
    int              kmod_fd;       /* fd to KMOD_SYSCALL_LOG */
    /* Set by the reader thread each cycle, read by main for the banner and by
     * canary_tick — atomic for the same reason the counters are. */
    _Atomic int      kmod_present;

    /* synapd IPC */
    int              synapd_fd;
    int              synapd_connected;
    uint32_t         request_counter;

    /* Audit log */
    int              audit_fd;      /* append-only log fd */
    pthread_mutex_t  audit_lock;

    /* IPC server (for 'syn guard' CLI) */
    int              server_fd;
    pthread_t        server_thread;

    /* Event reader thread */
    pthread_t        reader_thread;

    /* Baseline DB */
    sg_baseline_entry_t *baseline;
    int              baseline_count;
    pthread_mutex_t  baseline_lock;

    /*
     * Deny rules the BPF-LSM gate is enforcing, by name — recorded when the
     * lowered policy loads AND arms, so it is empty on every path where the
     * kernel is not actually refusing anything.
     *
     * By NAME rather than by sg_rule_t*, because a verdict can be dispatched
     * from the AI worker thread after the rule list has been freed and
     * reloaded underneath it; the existing code passes a name across that hop
     * for the same reason. Written once during init, read by the reader and
     * worker threads, never mutated after — no lock needed, and adding one
     * would be a lie about its lifetime.
     */
    char             bpf_enforced_rules[SG_MAX_KERNEL_RULES][RULE_MAX_NAME];
    int              bpf_enforced_count;

} synguard_state_t;

/* ── Function declarations ────────────────────────────────── */

/* Core */
int  synguard_init(synguard_state_t *s);
void synguard_destroy(synguard_state_t *s);
int  synguard_run(synguard_state_t *s);

/* Wire-format string escaping (core.c).
 *
 * The kmod's syscall_log and the baseline file are both whitespace-delimited
 * text, but a comm may contain a space ("Socket Thread") and so may a path.
 * The kmod escapes those bytes as \xHH so each field stays one token; these
 * mirror that, and the baseline file uses the same encoding. Anything without
 * whitespace or a backslash round-trips unchanged.
 */
#define SG_ESC_MAX_COMM      80    /* comm[16] fully escaped, + NUL   */
#define SG_ESC_MAX_FILENAME  640   /* kmod filename[128] escaped, + NUL */
void sg_str_escape(char *dst, size_t dlen, const char *src);
void sg_str_unescape(char *dst, size_t dlen, const char *src);

/* Event processing */
void synguard_process_event(synguard_state_t *s, const sg_event_t *e);

/* Stops the AI classifier worker and joins it. Call before joining the reader:
 * the worker only ever produces results for the reader to dispatch, so the
 * reader must still be alive to drain whatever is already classified. */
void synguard_ai_worker_stop(void);
int  kmod_parse_event(const char *line, sg_event_t *out);

/* Worm/C2 egress detector (event_processor.c). Exposed for tests: returns the
 * threat level of the connect() in `e` (THREAT_NONE when nothing tripped) and
 * fills `reason`. */
sg_threat_t netwatch_connect(const sg_event_t *e, char *reason, size_t rlen);

/* Rule engine */
int          rules_load(synguard_state_t *s, const char *dir);

/* Count enabled rules by verdict into counts[0..n-1], indexed by sg_verdict_t.
 * rules_enforcement_reachable() returns nonzero only when some verdict path
 * can actually reach action_deny()/action_quarantine() — mode permits acting
 * AND a loaded rule (or an AI verdict allowed to stand) can ask for it. */
void         rules_census(const synguard_state_t *s, int *counts, size_t n);
int          rules_enforcement_reachable(const synguard_state_t *s);
/*
 * Warn about acting `event open` rules whose path the kmod never reports, so
 * a rule that loads and counts as enforceable but can never match says so at
 * startup instead of passing for a quiet system. Returns how many it found.
 */
int          rules_report_unreachable_paths(const synguard_state_t *s);
/* Same check against an explicit prefix-list file, so it is testable without
 * the kmod loaded. rules_report_unreachable_paths() is this with the real
 * /sys/kernel/synapse/sensitive_paths. */
int          rules_report_unreachable_paths_from(const synguard_state_t *s,
                                                 const char *sysfs_path);
sg_verdict_t rules_evaluate(synguard_state_t *s, const sg_event_t *e,
                             const sg_rule_t **matched_rule);
void         rules_free(synguard_state_t *s);

/* AI classifier */
int synguard_ai_classify(synguard_state_t *s,
                          const sg_event_t *e,
                          const char *context,
                          sg_ai_result_t *out);

/* Action engine */
void action_deny(synguard_state_t *s, const sg_event_t *e, const char *reason);
void action_alert(synguard_state_t *s, const sg_alert_t *alert);

/*
 * Whether acting on a DENY would prevent anything. Returns NULL to enforce, or
 * a short reason to log and stand down (event_processor.c).
 *
 * A verdict is decided from a record of something that already happened, and
 * there are two cases where the record describes an access that never
 * occurred: the syscall failed, or the BPF-LSM gate had already refused it.
 * Killing then destroys a process tree to prevent nothing. Both have happened
 * on a live desktop — once for an ENOENT ld.so preload probe, once for an
 * EACCES read of a root-owned canary, which took out the session.
 *
 * "Unknown outcome" is NOT one of those cases and deliberately still enforces:
 * an older kmod reports no return value at all, and disarming enforcement for
 * every event it produces would be the worse failure.
 */
const char *sg_deny_suppression_reason(synguard_state_t *s,
                                       const sg_event_t *e,
                                       const char *rule_name);

/* Test seam: replace the "is the kernel gate live?" probe. The BPF layer is a
 * compile-time option, so without this the gate half of the predicate above is
 * unreachable in any build that omits it. NULL restores the real probe. */
void sg_deny_set_gate_probe_for_test(int (*fn)(void));

/*
 * Emit "×N in Ms" lines for repeat-alert windows that have closed. Call from
 * the main loop; it only does work when a window has actually expired.
 */
void alert_flush_summaries(synguard_state_t *s);
void action_quarantine(synguard_state_t *s, const sg_event_t *e);

/* Security-verdict broadcast feed (secfeed.c). A fixed-size record is sent to
 * every subscriber whenever a window-relevant verdict is taken, so the
 * compositor can colour the offending window's border. */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;      /* SG_SECFEED_MAGIC */
    uint32_t version;    /* SG_SECFEED_VERSION */
    uint32_t pid;
    uint32_t uid;
    int32_t  verdict;    /* sg_verdict_t */
    int32_t  threat;     /* sg_threat_t */
    char     comm[16];
    /* Alert reasons are truncated to fit. This is a FIXED wire layout shared
     * with synui and chibi's secfeed.py — do not widen it without bumping
     * SG_SECFEED_VERSION and rebuilding both. Keep the most useful part of a
     * reason (the counts, the offending destination) in the first 111 chars:
     * the netwatch alert used to lose its "(last=<dest>)" tail here. */
    char     reason[112];
} sg_secfeed_msg_t;      /* 152 bytes, fixed layout (< PIPE_BUF) */
#pragma pack(pop)

int  secfeed_init(void);                       /* start listener + accept thread */
void secfeed_publish(const sg_alert_t *alert); /* broadcast one verdict */
void secfeed_close(void);

/* Process isolation (isolation.c)
 *
 * sg_is_protected() is the hard guard: it returns nonzero (and fills `why`,
 * if non-NULL) when a pid must never be killed or frozen — PID 0/1, synguard
 * itself, synguard's own process group, kernel threads, and the core
 * SynapseOS daemons. It is always enforced and cannot be disabled.
 *
 * sg_kill_tree() / sg_freeze_tree() apply DENY / QUARANTINE to a process and
 * its entire descendant subtree; both return -1 (and take no action) if the
 * target is protected, 0 otherwise. Must run in process context as root.
 *
 * Both take `expect_comm` — the comm recorded in the event that produced the
 * verdict — and refuse to act if the pid no longer matches it. See
 * sg_pid_identity_ok(): by the time a verdict is acted on, the event is up to
 * poll_interval_ms old, and the pid may have been recycled onto an unrelated
 * process. Pass NULL only for a pid observed in this same instant. */
int  sg_is_protected(pid_t pid, char *why, size_t wlen);
int  sg_kill_tree(synguard_state_t *s, pid_t target, const char *expect_comm,
                  const char *reason);
int  sg_freeze_tree(synguard_state_t *s, pid_t target, const char *expect_comm);

/* Identity re-check, exposed for testing.
 *
 * sg_proc_starttime() reads field 22 of /proc/<pid>/stat (start time in clock
 * ticks since boot), the kernel's own tiebreaker for a recycled pid: two
 * processes that share a pid cannot share a start time. Returns 0 on success.
 *
 * sg_pid_identity_ok() returns 1 when `pid` is still the process whose comm is
 * `expect_comm`, 0 when it is gone or is now something else. A NULL or empty
 * expect_comm skips the comm test and only confirms the pid is alive. */
int  sg_proc_starttime(pid_t pid, unsigned long long *out);
int  sg_pid_identity_ok(pid_t pid, const char *expect_comm);

/* Audit log */
int  audit_init(synguard_state_t *s);
void audit_write(synguard_state_t *s, const sg_alert_t *alert);
void audit_close(synguard_state_t *s);

/* Baseline */
int  baseline_load(synguard_state_t *s);
void baseline_update(synguard_state_t *s, const sg_event_t *e);
int  baseline_is_anomalous(synguard_state_t *s, const sg_event_t *e);
void baseline_save(synguard_state_t *s);

/* kmod interface */
int  kmod_reader_start(synguard_state_t *s);
int  kmod_parse_event(const char *line, sg_event_t *out);

/* synapd IPC */
int  sg_synapd_connect(synguard_state_t *s);
void sg_synapd_disconnect(synguard_state_t *s);
int  sg_synapd_query(synguard_state_t *s, const char *prompt,
                     char *out, size_t out_len);
