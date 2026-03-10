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
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
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
#define SYNGUARD_AUDIT_LOG    "/var/log/synguard/audit.log"
#define SYNGUARD_RULES_DIR    "/etc/synguard/rules.d/"
#define SYNGUARD_STATE_DIR    "/var/lib/synguard/"
#define SYNGUARD_BASELINE     "/var/lib/synguard/baseline.db"

/* ── Sysfs paths (from synapse_kmod) ─────────────────────── */
#define KMOD_SYSCALL_LOG      "/sys/kernel/synapse/syscall_log"
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

/* ── Raw event from kmod ──────────────────────────────────── */
typedef struct {
    uint64_t  timestamp_ns;
    uint32_t  pid;
    uint32_t  uid;
    uint32_t  syscall_nr;
    uint8_t   evt_type;       /* EVT_* */
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

/* Event processing */
void synguard_process_event(synguard_state_t *s, const sg_event_t *e);

/* Rule engine */
int          rules_load(synguard_state_t *s, const char *dir);
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
