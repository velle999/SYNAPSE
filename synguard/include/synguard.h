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
    uint64_t  arg0;           /* first syscall arg (setuid: target uid) */
    char      comm[16];
    char      filename[128];
} sg_event_t;

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
typedef struct {
    uint64_t  events_processed;
    uint64_t  rules_matched;
    uint64_t  ai_queries;
    uint64_t  ai_timeouts;
    uint64_t  denials;
    uint64_t  alerts;
    uint64_t  quarantines;
    uint64_t  protected_skips;   /* actions refused on a protected pid */
    uint64_t  stale_pid_skips;   /* actions refused: pid no longer the culprit */
    time_t    start_time;
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
    int              kmod_present;

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
