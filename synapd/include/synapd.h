#include <sys/types.h>
/*
 * synapd.h — Core types and constants for the SynapseOS AI Daemon
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
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
/* The ONLY directory a model may be loaded from. SYN_MSG_RELOAD takes a model
 * name from a socket client, so this is the boundary that makes it safe: the
 * request carries a bare filename and never a path. */
/* Overridable only at COMPILE time, so the confinement can be exercised
 * against a scratch directory. Never from the environment: anyone who can set
 * the build flags already owns the binary, whereas a runtime override would be
 * a way around the boundary itself. */
#ifndef SYNAPD_MODEL_DIR
#define SYNAPD_MODEL_DIR      "/var/lib/synapd/models"
#endif
#define SYNAPD_DEFAULT_MODEL  SYNAPD_MODEL_DIR "/synapse-7b-q4_k_m.gguf"
/* The model last chosen at RUNTIME, as a bare filename — see
 * synapd_selected_{save,load}(). Kept beside the models rather than under /etc
 * because the daemon writes it as its own unprivileged user; a drop-in would
 * need root, which is exactly the privilege synapd was given up.
 *
 * Compile-time overridable on the same terms as SYNAPD_MODEL_DIR above, and for
 * the same reason: selected_test redirects both at a scratch directory, and a
 * runtime override would be a way around the confinement rather than a way to
 * exercise it. */
#ifndef SYNAPD_SELECTED_FILE
#define SYNAPD_SELECTED_FILE  "/var/lib/synapd/model.selected"
#endif
/* Retrieval embeddings. A SEPARATE model on purpose: a chat model's hidden
 * states are not interchangeable with a purpose-built embedder's, and chibi's
 * stored vectors were produced by this exact model via ollama. Serving them
 * from anything else silently invalidates every vector already on disk. */
#define SYNAPD_DEFAULT_EMBED_MODEL \
        "/var/lib/synapd/models/nomic-embed-text-v1.5.f16.gguf"
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
    /* Drop the model off the GPU before the machine suspends, and put it back
     * afterwards. NVreg_PreserveVideoMemoryAllocations makes the driver copy
     * every resident byte of VRAM to /var/tmp inside its PM_SUSPEND_PREPARE
     * notifier, and the machine is awake with its monitors dark for the whole
     * copy — measured at 1:1 with VRAM in use, and synapd's model is the bulk
     * of it. SLEEP is synchronous: it answers only once the VRAM is actually
     * released, so the hook that sends it can hold off the suspend until then.
     * WAKE answers immediately and reloads on a thread. */
    SYN_MSG_SLEEP          = 0x09, /* release the model; replies when VRAM is freed */
    SYN_MSG_WAKE           = 0x0A, /* start a background reload; replies at once */
    SYN_MSG_EMBED          = 0x0B, /* embed text; replies with float32[n_embd] */
    SYN_MSG_RESPONSE       = 0x80, /* response flag OR'd with request type */
    SYN_MSG_ERROR          = 0xFF,
} syn_msg_type_t;

/* Query flags — carried in syn_msg_header_t.flags, meaningful on SYN_MSG_QUERY.
 * Fully backward-compatible: flags==0 is the legacy behaviour every existing
 * client (synsh, synguard, kmod, chibi) already sends. Agentic clients that
 * drive their own full prompt (e.g. vibe) set SYN_QF_RAW and a token budget. */
#define SYN_QF_RAW          0x8000u  /* skip the built-in Synapse persona + rolling
                                        OS system-context injection, and don't push
                                        the turn into the context store — the client
                                        owns the whole prompt */
#define SYN_QF_TOKENS_MASK  0x7FFFu  /* max output tokens; 0 = daemon default (512) */

/* Wire format: fixed header + variable payload */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /* SYN_MAGIC */
    uint8_t  version;       /* SYNAPD_PROTOCOL_VER */
    uint8_t  msg_type;      /* syn_msg_type_t */
    uint16_t flags;
    uint32_t payload_len;   /* bytes following this header */
    uint32_t request_id;    /* echoed in response */
    /* The sender's own idea of its PID.
     *
     * ⚠ ADVISORY ONLY — NEVER USE THIS FOR A PRIVILEGE CHECK, which is what
     * this comment used to invite. It is filled in by the sender and a
     * malicious one can put anything here. socket_server.c takes the real
     * identity from SO_PEERCRED and logs a disagreement; the field is kept on
     * the wire so existing clients are unchanged, and so the disagreement is
     * observable at all. */
    uint32_t client_pid;    /* advisory; the kernel's answer is what is used */
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
    float       temperature;     /* 0 = greedy/deterministic */
    float       top_p;           /* nucleus cutoff, applied when sampling */
    int         top_k;           /* 0 = disabled */
    /* Set when the value above came from an explicit command-line flag rather
     * than the built-in default. A per-model profile fills in what the operator
     * did NOT ask for; it must never override what they did. Without these,
     * "-T 0.8" and "no flag at all" are indistinguishable. */
    int         temp_set;
    int         top_p_set;
    int         top_k_set;
    const char *embed_model_path; /* NULL/missing = embeddings unavailable */
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
    time_t  last_retry;         /* last kmod reconnection attempt */
} synapd_scheduler_t;

/* ── Global daemon state ──────────────────────────────────── */
typedef struct synapd_state {
    /* Lifecycle */
    volatile int        running;
    int                 debug;

    /* Config */
    synapd_config_t     config;

    /* Backing store for a model chosen at runtime by SYN_MSG_RELOAD.
     * config.model_path otherwise points at argv or a string literal, neither
     * of which can be rewritten, and a heap pointer would need an owner. */
    char                model_path_store[512];

    /* What was loaded before the switch now in flight — the model to put back
     * if the new one will not load.
     *
     * Captured by handle_reload() rather than read off config.model_path in
     * switch_thread(), because by the time the thread runs, model_path_store
     * already holds the NEW path, and after the first successful switch
     * config.model_path POINTS AT model_path_store. The thread would then
     * "restore" the very model that had just failed to load, and the log line
     * would name it as the rescue. Two different models are the only case where
     * that distinction shows, which is why it survived until the choice started
     * being persisted. */
    char                model_path_prev[512];

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
    /* Set while inference_init runs — the socket is already serving then,
     * so handlers can tell "still loading" apart from "no model". */
    _Atomic int         model_loading;

    /* Guards the EXISTENCE of s->inference, not its contents (inf->lock does
     * that). Readers are the handlers that run the model; the writer is a
     * load or an unload.
     *
     * Needed because inference_run() copies s->inference to a local, checks it
     * for NULL, and only then takes inf->lock — so a concurrent
     * inference_destroy() frees the object in the window between the check and
     * the lock. That was harmless while unloads only happened at shutdown; the
     * moment a suspend can unload underneath a live query it is a
     * use-after-free. Held for the whole of a query, so an unload waits for
     * in-flight work rather than pulling the model out from under it. */
    pthread_rwlock_t    model_rw;
    /* Unloaded for suspend, as opposed to never loaded or still loading —
     * so a query during sleep can say so instead of "no model". */
    _Atomic int         model_sleeping;

    /*
     * The last switch that failed, and llama.cpp's reason for it.
     *
     * A failed switch restores the previous model, so every observable field
     * goes back to what it was and the daemon looks exactly as it did before
     * the request — which is why the picker sat on "loading …" forever. These
     * two are the only trace, and they ride on STATUS so the caller learns the
     * outcome from the same poll it was already watching.
     *
     * Written only by switch_thread() under the model write lock, read by
     * handle_status() without one: they are fixed-size buffers that go from
     * one complete string to another, and a status poll that catches the
     * previous value is a poll one round trip early.
     */
    char                switch_file[128];   /* the file that would not load */
    char                switch_err[192];    /* "unknown pre-tokenizer type…" */

} synapd_state_t;

/* ── Forward declarations ─────────────────────────────────── */
void synapd_reload_config(synapd_state_t *s);
void sd_notify_ready(void);  /* thin wrapper around sd_notify(0, "READY=1") */
