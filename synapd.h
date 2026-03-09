/*
 * synapd.h — Core types and constants for the SynapseOS AI Daemon
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */
#pragma once

#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <syslog.h>

/* ── Version ─────────────────────────────────────────────── */
#define SYNAPD_VERSION        "0.1.0-synapse"
#define SYNAPD_PROTOCOL_VER   1

/* ── Paths ────────────────────────────────────────────────── */
#define SYNAPD_SOCKET_PATH    "/run/synapd/synapd.sock"
#define SYNAPD_PID_FILE       "/run/synapd/synapd.pid"
#define SYNAPD_DEFAULT_MODEL  "/var/lib/synapd/models/synapse-7b-q4_k_m.gguf"
#define SYNAPD_CONTEXT_DIR    "/var/lib/synapd/context"
#define SYNAPD_SYSFS_PATH     "/sys/kernel/synapse"        /* synapse_kmod sysfs */
#define SYNAPD_SYSFS_STATUS   SYNAPD_SYSFS_PATH "/status"
#define SYNAPD_SYSFS_HINTS    SYNAPD_SYSFS_PATH "/ai_hints"

/* ── IPC Protocol ─────────────────────────────────────────── */
/* Message types sent over the Unix socket */
typedef enum {
    SYN_MSG_QUERY          = 0x01, /* natural language query */
    SYN_MSG_SYSCALL_EVENT  = 0x02, /* from synapse_kmod: syscall context */
    SYN_MSG_SCHED_HINT     = 0x03, /* from synapse_kmod: scheduling hint request */
    SYN_MSG_CONTEXT_PUSH   = 0x04, /* push arbitrary context string */
    SYN_MSG_CONTEXT_GET    = 0x05, /* get current context summary */
    SYN_MSG_STATUS         = 0x06, /* status/ping */
    SYN_MSG_RELOAD         = 0x07, /* reload model / config */
    SYN_MSG_SHUTDOWN       = 0x08, /* graceful shutdown (root only) */
    SYN_MSG_RESPONSE       = 0x80, /* response flag OR'd with request type */
    SYN_MSG_ERROR          = 0xFF,
} syn_msg_type_t;

/* Wire format: fixed header + variable payload */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /* SYN_MAGIC */
    uint8_t  version;       /* SYNAPD_PROTOCOL_VER */
    uint8_t  msg_type;      /* syn_msg_type_t */
    uint16_t flags;
    uint32_t payload_len;   /* bytes following this header */
    uint32_t request_id;    /* echoed in response */
    uint32_t client_pid;    /* sender PID for privilege checks */
    uint64_t timestamp_ns;  /* CLOCK_MONOTONIC_RAW */
} syn_msg_header_t;
#pragma pack(pop)

#define SYN_MAGIC        0x53594E41u  /* "SYNA" */
#define SYN_MAX_PAYLOAD  (1024 * 1024)   /* 1 MiB max message */

/* ── Client session ───────────────────────────────────────── */
typedef struct syn_client {
    int       fd;
    pid_t     pid;
    uid_t     uid;
    uint32_t  client_id;
    time_t    connected_at;
    uint64_t  requests;
    struct syn_client *next;
} syn_client_t;

/* ── Config ───────────────────────────────────────────────── */
typedef struct {
    const char *socket_path;
    const char *model_path;
    uint32_t    context_window;  /* tokens */
    int         n_threads;
    int         n_gpu_layers;    /* -1 = auto */
    int         log_level;
    int         max_clients;
} synapd_config_t;

/* ── Inference state (opaque to most subsystems) ─────────── */
typedef struct synapd_inference synapd_inference_t;

/* ── Context store ────────────────────────────────────────── */
#define CONTEXT_MAX_EVENTS  2048

typedef enum {
    CTX_SYSCALL  = 1,
    CTX_QUERY    = 2,
    CTX_RESPONSE = 3,
    CTX_SYSTEM   = 4,
} ctx_event_type_t;

typedef struct {
    ctx_event_type_t type;
    time_t           timestamp;
    pid_t            pid;
    char             data[512];
} ctx_event_t;

typedef struct {
    ctx_event_t     events[CONTEXT_MAX_EVENTS];
    uint32_t        head;
    uint32_t        count;
    uint32_t        used_tokens;
    pthread_mutex_t lock;
} synapd_context_t;

/* ── Scheduler bridge state ───────────────────────────────── */
typedef struct {
    int     sysfs_fd;           /* fd to /sys/kernel/synapse/ai_hints */
    int     kmod_present;       /* 1 if synapse_kmod is loaded */
    time_t  last_heartbeat;
} synapd_scheduler_t;

/* ── Global daemon state ──────────────────────────────────── */
typedef struct synapd_state {
    /* Lifecycle */
    volatile int        running;
    int                 debug;

    /* Config */
    synapd_config_t     config;

    /* Subsystems */
    synapd_inference_t *inference;
    synapd_context_t    context;
    synapd_scheduler_t  scheduler;

    /* Socket server */
    int                 socket_fd;
    syn_client_t       *clients;
    pthread_mutex_t     clients_lock;
    pthread_t           server_thread;

    /* Stats */
    _Atomic uint64_t    requests_total;
    _Atomic uint64_t    requests_active;
    int                 model_loaded;

} synapd_state_t;

/* ── Forward declarations ─────────────────────────────────── */
void synapd_reload_config(synapd_state_t *s);
void sd_notify_ready(void);  /* thin wrapper around sd_notify(0, "READY=1") */
