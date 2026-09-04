/*
 * socket_server.c — Unix domain socket IPC server
 *
 * Listens on SYNAPD_SOCKET_PATH. Accepts connections from:
 *   - synapse_kmod  (kernel module, via bridge)
 *   - synsh          (user shell)
 *   - synguard       (security monitor)
 *   - any other synapse-aware process
 *
 * Uses a thread pool (default 8 workers) so inference requests
 * don't block the accept loop. Inference itself is serialized
 * inside inference.c via mutex — this is intentional until we
 * support multi-context parallel inference.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <grp.h>

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include "synapd.h"
#include "socket_server.h"
#include "inference.h"
#include "offload.h"
#include "context.h"
#include "selected.h"
#include "log.h"

#define THREAD_POOL_SIZE   8
#define EPOLL_MAX_EVENTS   64

/* Set when systemd handed us the listening socket. The socket is then systemd's
 * to manage: unlinking it on stop would leave synapd.socket pointing at nothing
 * and the next activation with no path to listen on. */
static int g_socket_activated = 0;
#define RECV_BUF_SIZE      (64 * 1024)   /* 64 KiB per client recv buf */

/* synapd_header_valid() — the input gate — lives in src/wire.c, which is pure
 * and dependency-free so the test can link it without dragging in llama. */

/* ── Work queue ───────────────────────────────────────────── */
typedef struct work_item {
    int              client_fd;
    pid_t            client_pid;   /* from SO_PEERCRED, never from the header */
    uid_t            client_uid;   /* likewise — the kernel's, not claimed */
    syn_msg_header_t hdr;
    uint8_t         *payload;     /* heap-allocated, worker frees */
    synapd_state_t  *state;
    struct work_item *next;
} work_item_t;

typedef struct {
    work_item_t     *head;
    work_item_t     *tail;
    int              count;
    pthread_mutex_t  lock;
    pthread_cond_t   cond;
    int              shutdown;
} work_queue_t;

static work_queue_t  g_queue;
static pthread_t     g_workers[THREAD_POOL_SIZE];
static int           g_epoll_fd = -1;

/* ── Queue ops ────────────────────────────────────────────── */
static void queue_push(work_item_t *item) {
    pthread_mutex_lock(&g_queue.lock);
    item->next = NULL;
    if (g_queue.tail) g_queue.tail->next = item;
    else              g_queue.head = item;
    g_queue.tail = item;
    g_queue.count++;
    pthread_cond_signal(&g_queue.cond);
    pthread_mutex_unlock(&g_queue.lock);
}

static work_item_t *queue_pop(void) {
    pthread_mutex_lock(&g_queue.lock);
    while (!g_queue.head && !g_queue.shutdown)
        pthread_cond_wait(&g_queue.cond, &g_queue.lock);
    if (g_queue.shutdown && !g_queue.head) {
        pthread_mutex_unlock(&g_queue.lock);
        return NULL;
    }
    work_item_t *item = g_queue.head;
    g_queue.head = item->next;
    if (!g_queue.head) g_queue.tail = NULL;
    g_queue.count--;
    pthread_mutex_unlock(&g_queue.lock);
    return item;
}

/* ── Response helpers ─────────────────────────────────────── */
static int send_response(int fd, uint32_t req_id,
                          syn_msg_type_t type,
                          const void *payload, uint32_t plen)
{
    syn_msg_header_t hdr = {
        .magic       = SYN_MAGIC,
        .version     = SYNAPD_PROTOCOL_VER,
        .msg_type    = (uint8_t)(SYN_MSG_RESPONSE | type),
        .payload_len = plen,
        .request_id  = req_id,
    };
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    hdr.timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    if (write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) return -1;
    if (plen && payload)
        if (write(fd, payload, plen) != (ssize_t)plen) return -1;
    return 0;
}

static int send_error(int fd, uint32_t req_id, const char *msg) {
    return send_response(fd, req_id, SYN_MSG_ERROR, msg, strlen(msg) + 1);
}

/* ── Request handlers ─────────────────────────────────────── */
/* Embeddings for retrieval (chibi's thoth store).
 *
 * Note what is NOT here: no model_sleeping / model_loading guard, and the
 * dispatcher does not put this under model_rw. Those all protect the CHAT
 * model, which SYN_MSG_SLEEP releases for a suspend and the offload policy
 * re-fits when something else wants the card.
 *
 * ⚠ THIS CLAIM IS ONLY TRUE AS OF synapd 51. It was written when the embedder
 * lived inside synapd_inference, where inference_destroy() freed it along with
 * everything else — so every suspend and every model switch DID take RAG down,
 * for the sake of 274 MB that was never the pressure, while three comments in
 * two files said it could not happen. The embedder is its own object with its
 * own lifetime now (see the note at the top of inference.c), so RAG really
 * does keep answering through a gaming session instead of going dark exactly
 * when the desktop is busiest.
 *
 * Reply payload is raw little-endian float32[dim] -- the caller gets the
 * dimension from the payload length. */
static void handle_embed(work_item_t *w) {
    char *text = (char *)w->payload;
    if (!text || w->hdr.payload_len == 0) {
        send_error(w->client_fd, w->hdr.request_id, "empty text");
        return;
    }
    text[w->hdr.payload_len - 1] = '\0';

    /* Any real embedder is far below this; it exists so a bad dim cannot
     * overrun the buffer. */
    enum { EMBED_MAX_DIM = 4096 };
    float *vec = malloc(sizeof(float) * EMBED_MAX_DIM);
    if (!vec) { send_error(w->client_fd, w->hdr.request_id, "oom"); return; }

    int dim = inference_embed(w->state, text, vec, EMBED_MAX_DIM);
    if (dim < 0) {
        send_error(w->client_fd, w->hdr.request_id,
                   "embeddings unavailable — no embedding model loaded");
        free(vec);
        return;
    }

    send_response(w->client_fd, w->hdr.request_id, SYN_MSG_EMBED,
                  (const char *)vec, (size_t)dim * sizeof(float));
    free(vec);
}

static void handle_query(work_item_t *w) {
    char *prompt = (char *)w->payload;
    if (!prompt || w->hdr.payload_len == 0) {
        send_error(w->client_fd, w->hdr.request_id, "empty prompt");
        return;
    }
    prompt[w->hdr.payload_len - 1] = '\0';

    if (atomic_load(&w->state->model_sleeping)) {
        /* ⛔ IT DOES NOT SAY "reloading". Nothing reloads a sleeping model on
         * its own — only SYN_MSG_WAKE does — and there are two senders of
         * SLEEP now with very different timing: the suspend hook, whose resume
         * hook wakes it seconds later, and synui's game mode, which holds it
         * down for the length of a game. Promising a reload that is not
         * happening reads as a stuck daemon for the whole session. */
        send_error(w->client_fd, w->hdr.request_id,
                   "AI model is released — it comes back when whatever needed "
                   "the GPU is finished");
        return;
    }
    if (!w->state->model_loaded && atomic_load(&w->state->model_loading)) {
        send_error(w->client_fd, w->hdr.request_id,
                   "AI model is still loading — try again in a moment");
        return;
    }

    /* Query flags (legacy clients send 0 → persona + OS context + 512 tokens). */
    int raw        = (w->hdr.flags & SYN_QF_RAW) != 0;
    int max_tokens = w->hdr.flags & SYN_QF_TOKENS_MASK;  /* 0 => inference default */

    /* The rolling system context is the OS-assistant role's memory. An agentic
     * client (SYN_QF_RAW) drives its own prompt, so don't prepend OS state to it. */
    char sys_ctx[1024] = {0};
    if (!raw)
        context_get_summary(w->state, sys_ctx, sizeof(sys_ctx));

    /* Run inference */
    char *out = malloc(SYN_MAX_PAYLOAD);
    if (!out) { send_error(w->client_fd, w->hdr.request_id, "oom"); return; }

    int n = inference_run(w->state, raw ? NULL : sys_ctx, prompt,
                          out, SYN_MAX_PAYLOAD - 1, max_tokens, raw);
    if (n < 0) {
        send_error(w->client_fd, w->hdr.request_id, "inference failed");
        free(out);
        return;
    }

    /* Push Q+A into the context store — but not an agentic client's coding
     * turns, which would pollute the OS assistant's rolling memory. */
    if (!raw) {
        context_push(w->state, CTX_QUERY,    w->client_pid, prompt);
        context_push(w->state, CTX_RESPONSE, 0,             out);
    }

    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_QUERY, out, strlen(out) + 1);
    free(out);
}

static void handle_syscall_event(work_item_t *w) {
    char *evt = (char *)w->payload;
    /* payload_len is checked as well as the pointer. The write below indexes
     * [payload_len - 1], so a zero length is an out-of-bounds write one byte
     * BEFORE the buffer. Today that cannot happen — the receive loop only
     * malloc()s when payload_len > 0, so a zero length always arrives as a
     * NULL payload and the first test catches it. That invariant is implicit
     * and lives ~250 lines away in the epoll loop; a later change to allocate
     * unconditionally (malloc(len + 1) is the obvious refactor) would turn
     * this into a one-byte heap underflow reachable from any client, and the
     * TCP bridge makes "any client" mean another host. State it locally
     * instead of depending on distant code, the way handle_query() does. */
    if (!evt || w->hdr.payload_len == 0) return;
    evt[w->hdr.payload_len - 1] = '\0';

    context_push(w->state, CTX_SYSCALL, w->client_pid, evt);

    char classification[128] = {0};
    inference_classify_syscall(w->state, evt, classification, sizeof(classification));

    syn_log(LOG_DEBUG, "syscall_event pid=%d class=%s", w->client_pid, classification);

    if (strncmp(classification, "BLOCK", 5) == 0 ||
        strncmp(classification, "SUSPICIOUS", 10) == 0) {
        syn_log(LOG_WARNING, "synapd: anomaly pid=%d → %s", w->client_pid, classification);
    }

    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_SYSCALL_EVENT,
                  classification, strlen(classification) + 1);
}

static void handle_sched_hint(work_item_t *w) {
    char *intent = (char *)w->payload;
    /* Same zero-length guard as handle_syscall_event() — see the reasoning
     * there. Every handler that indexes [payload_len - 1] needs this. */
    if (!intent || w->hdr.payload_len == 0) return;
    intent[w->hdr.payload_len - 1] = '\0';

    int delta = 0;
    inference_sched_hint(w->state, intent, &delta);

    char resp[16];
    snprintf(resp, sizeof(resp), "%d", delta);
    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_SCHED_HINT, resp, strlen(resp) + 1);
}

static void handle_status(work_item_t *w) {
    /* Was 256 and full, then 640. The detail block adds a quoted model name
     * and the resolved prompt format, either of which can be long, and the
     * failure block on the end adds a filename plus llama's own sentence. */
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "synapd/%s model=%s requests=%lu active=%lu ctx_used=%u ctx_window=%u",
        SYNAPD_VERSION,
        w->state->model_loaded ? "loaded"
            : atomic_load(&w->state->model_loading) ? "loading"
            : atomic_load(&w->state->model_sleeping) ? "released-for-suspend"
            : "none",
        (unsigned long)atomic_load(&w->state->requests_total),
        (unsigned long)atomic_load(&w->state->requests_active),
        context_used_tokens(w->state),
        w->state->config.context_window
    );

    /* Appended, never inserted: synui parses this line key-by-key with strstr
     * and synsh prints it verbatim, so new keys on the end are backward
     * compatible while reordering the old ones would not be. */
    if (n > 0 && (size_t)n < sizeof(buf))
        inference_describe(w->state, buf + n, sizeof(buf) - (size_t)n);

    /* Last, after the describe block, for the same append-only reason. Omitted
     * entirely when nothing has failed, so a client that never looks for it is
     * unaffected and one that does can tell "no failure" from "empty reason".
     *
     * The quotes matter: llama's messages contain spaces and its own quotes
     * around the offending value ("unknown pre-tokenizer type: 'minicpm5'"),
     * and synui's parser reads a quoted value to the closing double quote.
     * Single quotes inside are therefore fine; a double quote is not, so any
     * that appear are flattened rather than allowed to end the field early. */
    n = (int)strlen(buf);
    if (w->state->switch_err[0] && (size_t)n < sizeof(buf)) {
        char safe[sizeof(w->state->switch_err)];
        snprintf(safe, sizeof(safe), "%s", w->state->switch_err);
        for (char *p = safe; *p; p++) if (*p == '"') *p = '\'';

        snprintf(buf + n, sizeof(buf) - (size_t)n,
                 " switch_file=\"%s\" switch_err=\"%s\"",
                 w->state->switch_file, safe);
    }

    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_STATUS, buf, strlen(buf) + 1);
}

/* ── Suspend / resume ─────────────────────────────────────── */
/*
 * Release the model so the NVIDIA driver has nothing to copy.
 *
 * MUST NOT return until the VRAM is actually gone: the caller is a systemd
 * system-sleep hook, and systemd only holds the suspend while that hook runs.
 * Answer early and the driver starts its dump with the model still resident,
 * which is the whole cost this exists to avoid.
 *
 * The write lock is what makes it safe. Queries hold the read lock for their
 * full duration, so this blocks until every in-flight one has finished rather
 * than freeing the model under it. A generation mid-suspend is rare and worth
 * waiting for; the alternative is a crash.
 */
static void handle_sleep(work_item_t *w) {
    synapd_state_t *s = w->state;

    /* Set BEFORE taking the lock so queries arriving while we wait are turned
     * away with the right reason instead of queueing behind the unload. */
    atomic_store(&s->model_sleeping, 1);

    pthread_rwlock_wrlock(&s->model_rw);
    int had_model = s->model_loaded;
    if (had_model) {
        /* Not "for suspend": synui's game mode sends this too. */
        syn_log(LOG_INFO, "synapd: releasing the model");
        inference_destroy(s);
    }
    pthread_rwlock_unlock(&s->model_rw);

    const char *msg = had_model ? "model released" : "no model was loaded";
    syn_log(LOG_INFO, "synapd: sleep — %s", msg);
    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_SLEEP, msg, strlen(msg) + 1);
}

/* Reload on a detached thread. Loading a multi-GB model takes tens of seconds
 * and nothing is waiting on it — a person coming back to a resumed desktop is
 * not typing an AI prompt in the first breath. Queries in that window get the
 * "reloading" error, exactly as they already do at boot. */
static void *reload_thread(void *arg) {
    synapd_state_t *s = arg;

    pthread_rwlock_wrlock(&s->model_rw);
    atomic_store(&s->model_loading, 1);
    if (inference_init(s) < 0)
        syn_log(LOG_WARNING, "synapd: model reload after resume FAILED");
    else
        syn_log(LOG_INFO, "synapd: model reloaded after resume");
    atomic_store(&s->model_loading, 0);
    /* Cleared last: until the model is back, a query should say "reloading",
     * not "no model". */
    atomic_store(&s->model_sleeping, 0);
    pthread_rwlock_unlock(&s->model_rw);
    return NULL;
}

/*
 * "Something else needs this GPU."
 *
 * ⚠ A HINT, NOT A COMMAND — offload.h says why the sender may not name a layer
 * count. It raises the floor the policy defends and the watcher decides what
 * that means; this returns immediately, because a caller that wants the card is
 * the worst possible thing to block for the tens of seconds a reload takes.
 *
 * ⚠ Game mode is NOT the sender any more — see SYN_MSG_DEMAND in synapd.h.
 */
static void handle_demand(work_item_t *w) {
    char *arg = (char *)w->payload;
    if (!arg || w->hdr.payload_len == 0) {
        send_error(w->client_fd, w->hdr.request_id, "demand takes 'high' or 'normal'");
        return;
    }
    arg[w->hdr.payload_len - 1] = '\0';

    int high;
    if      (strcmp(arg, "high")   == 0) high = 1;
    else if (strcmp(arg, "normal") == 0) high = 0;
    else {
        send_error(w->client_fd, w->hdr.request_id,
                   "demand takes 'high' or 'normal'");
        return;
    }

    const char *msg = offload_set_demand(w->state, high);
    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_DEMAND, msg, strlen(msg) + 1);
}

static void handle_wake(work_item_t *w) {
    synapd_state_t *s = w->state;

    if (s->model_loaded && !atomic_load(&s->model_sleeping)) {
        const char *msg = "model already loaded";
        send_response(w->client_fd, w->hdr.request_id,
                      SYN_MSG_WAKE, msg, strlen(msg) + 1);
        return;
    }

    pthread_t tid;
    if (pthread_create(&tid, NULL, reload_thread, s) != 0) {
        /* Leaving model_sleeping set would wedge every future query behind a
         * reload that is never coming. */
        atomic_store(&s->model_sleeping, 0);
        send_error(w->client_fd, w->hdr.request_id, "cannot spawn reload thread");
        return;
    }
    pthread_detach(tid);

    const char *msg = "reloading in background";
    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_WAKE, msg, strlen(msg) + 1);
}


/*
 * Swap the model on a detached thread.
 *
 * Backgrounded for the same reason resume is: loading several GB takes tens of
 * seconds, far longer than a client is willing to hold a socket open. The
 * caller gets an immediate acknowledgement and watches SYN_MSG_STATUS, where
 * model= reports "loading" and then the new model_name appears.
 *
 * If the new model fails to load, the OLD path is put back and reloaded. A
 * mistyped or corrupt file should cost you a pause, not your AI: without this
 * a single bad pick leaves the daemon running with no model at all until
 * somebody restarts it by hand.
 */
static void *switch_thread(void *arg) {
    synapd_state_t *s = arg;

    char previous[sizeof(s->model_path_store)];
    snprintf(previous, sizeof(previous), "%s", s->model_path_prev);

    pthread_rwlock_wrlock(&s->model_rw);
    atomic_store(&s->model_loading, 1);

    if (s->model_loaded)
        inference_destroy(s);

    s->config.model_path = s->model_path_store;
    if (inference_init(s) < 0) {
        /* Captured BEFORE the restore, which is itself a load and resets the
         * reason. Without this ordering the reported error is whatever the
         * successful restore had to say, i.e. nothing. */
        char why[sizeof(s->switch_err)];
        inference_error_get(why, sizeof(why));

        const char *slash = strrchr(s->model_path_store, '/');
        snprintf(s->switch_file, sizeof(s->switch_file), "%s",
                 slash ? slash + 1 : s->model_path_store);
        snprintf(s->switch_err, sizeof(s->switch_err), "%s",
                 why[0] ? why : "the model could not be loaded");

        syn_log(LOG_WARNING, "synapd: switching to %s FAILED — restoring %s (%s)",
                s->model_path_store, previous, s->switch_err);
        snprintf(s->model_path_store, sizeof(s->model_path_store), "%s", previous);
        if (inference_init(s) < 0)
            syn_log(LOG_ERR, "synapd: could not reload %s either — no model loaded",
                    previous);
        else
            syn_log(LOG_INFO, "synapd: previous model restored");
    } else {
        /* A switch that worked clears the last one that did not. The picker
         * shows this field verbatim, and a stale reason attached to a model
         * now happily running is worse than no reason at all. */
        s->switch_err[0]  = '\0';
        s->switch_file[0] = '\0';

        syn_log(LOG_INFO, "synapd: model switched to %s", s->model_path_store);
        /* Only on the success path, and only the bare name: the persisted
         * choice has to survive SYNAPD_MODEL_DIR moving, and a stored absolute
         * path would quietly reintroduce the traversal question that
         * synapd_model_resolve() exists to answer. */
        const char *slash = strrchr(s->model_path_store, '/');
        synapd_selected_save(slash ? slash + 1 : s->model_path_store);
    }

    atomic_store(&s->model_loading, 0);
    pthread_rwlock_unlock(&s->model_rw);
    return NULL;
}

/*
 * SYN_MSG_RELOAD — load a different model, or reload the current one.
 *
 * The opcode has been in the protocol since the beginning but was never
 * dispatched: it fell through to "unknown msg type" while synapd_reload_config()
 * logged "(stub)". Payload is a bare model filename, or empty to reload
 * whatever is already configured.
 */
static void handle_reload(work_item_t *w) {
    synapd_state_t *s = w->state;
    char *name = (char *)w->payload;

    /* Same zero-length guard as every other handler that indexes the payload. */
    if (name && w->hdr.payload_len > 0)
        name[w->hdr.payload_len - 1] = '\0';
    else
        name = NULL;

    /* A switch while one is already running would overwrite model_path_store
     * under the thread that is reading it. */
    if (atomic_load(&s->model_loading)) {
        send_error(w->client_fd, w->hdr.request_id, "a model is already loading");
        return;
    }

    char resolved[sizeof(s->model_path_store)];
    if (name && *name) {
        const char *why = "invalid model";
        if (synapd_model_resolve(name, resolved, sizeof(resolved), &why) != 0) {
            syn_log(LOG_WARNING, "synapd: reload refused (%s): %s", why, name);
            send_error(w->client_fd, w->hdr.request_id, why);
            return;
        }
    } else {
        /* Empty payload — reload what is already configured. */
        snprintf(resolved, sizeof(resolved), "%s", s->config.model_path);
    }

    /* Before the overwrite, while config.model_path still names what is
     * actually loaded. See model_path_prev's note in synapd.h. */
    snprintf(s->model_path_prev, sizeof(s->model_path_prev), "%s",
             s->config.model_path ? s->config.model_path : "");
    snprintf(s->model_path_store, sizeof(s->model_path_store), "%s", resolved);

    pthread_t tid;
    if (pthread_create(&tid, NULL, switch_thread, s) != 0) {
        send_error(w->client_fd, w->hdr.request_id, "cannot spawn switch thread");
        return;
    }
    pthread_detach(tid);

    char msg[sizeof(resolved) + 16];
    snprintf(msg, sizeof(msg), "loading %s", resolved);
    syn_log(LOG_INFO, "synapd: reload requested — %s", resolved);
    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_RELOAD, msg, strlen(msg) + 1);
}

static void handle_context_get(work_item_t *w) {
    char *buf = malloc(8192);
    if (!buf) { send_error(w->client_fd, w->hdr.request_id, "oom"); return; }
    context_get_summary(w->state, buf, 8192);
    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_CONTEXT_GET, buf, strlen(buf) + 1);
    free(buf);
}

/* ── Worker thread ────────────────────────────────────────── */
static void *worker_thread(void *arg) {
    (void)arg;
    while (1) {
        work_item_t *w = queue_pop();
        if (!w) break;  /* shutdown */

        atomic_fetch_add(&w->state->requests_active, 1);

        switch (w->hdr.msg_type) {
        /* Everything that touches the model runs under the READ lock, held for
         * the whole handler. inference_run() checks s->inference and only then
         * takes inf->lock, so without this an unload could free it in between.
         * Read locks do not exclude each other, so concurrent queries are
         * unaffected — the only thing that waits is an unload. */
        case SYN_MSG_QUERY:
        case SYN_MSG_SYSCALL_EVENT:
        case SYN_MSG_SCHED_HINT:
            pthread_rwlock_rdlock(&w->state->model_rw);
            if      (w->hdr.msg_type == SYN_MSG_QUERY)         handle_query(w);
            else if (w->hdr.msg_type == SYN_MSG_SYSCALL_EVENT) handle_syscall_event(w);
            else                                               handle_sched_hint(w);
            pthread_rwlock_unlock(&w->state->model_rw);
            break;

        /* Take the WRITE lock themselves, so they must not be nested here. */
        case SYN_MSG_SLEEP:         handle_sleep(w);         break;
        case SYN_MSG_DEMAND:        handle_demand(w);        break;
        case SYN_MSG_WAKE:          handle_wake(w);          break;
        case SYN_MSG_RELOAD:        handle_reload(w);        break;

        /* Not under model_rw: the embedder is independent of the chat model
         * and survives SLEEP, so an unload must not block or break it. */
        case SYN_MSG_EMBED:         handle_embed(w);         break;

        case SYN_MSG_STATUS:        handle_status(w);        break;
        case SYN_MSG_CONTEXT_GET:   handle_context_get(w);   break;
        default:
            send_error(w->client_fd, w->hdr.request_id, "unknown msg type");
            break;
        }

        atomic_fetch_sub(&w->state->requests_active, 1);
        free(w->payload);
        free(w);
    }
    return NULL;
}

/* ── Accept + read loop (server thread) ──────────────────── */
static void *server_thread_fn(void *arg) {
    synapd_state_t *s = (synapd_state_t *)arg;

    struct epoll_event events[EPOLL_MAX_EVENTS];

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = s->socket_fd };
    epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, s->socket_fd, &ev);

    while (s->running) {
        int n = epoll_wait(g_epoll_fd, events, EPOLL_MAX_EVENTS, 500);
        if (n < 0) {
            if (errno == EINTR) continue;
            syn_log(LOG_ERR, "socket_server: epoll_wait: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == s->socket_fd) {
                int cfd = accept4(s->socket_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (cfd < 0) continue;

                struct epoll_event cev = { .events = EPOLLIN | EPOLLET, .data.fd = cfd };
                epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, cfd, &cev);
                syn_log(LOG_DEBUG, "socket_server: new client fd=%d", cfd);
            } else {
                int cfd = events[i].data.fd;

                syn_msg_header_t hdr;
                ssize_t r = recv(cfd, &hdr, sizeof(hdr), MSG_WAITALL);
                if (r <= 0) {
                    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, cfd, NULL);
                    close(cfd);
                    continue;
                }

                if (!synapd_header_valid(&hdr, (size_t)r)) {
                    if (r == sizeof(hdr) && hdr.payload_len > SYN_MAX_PAYLOAD)
                        syn_log(LOG_WARNING, "socket_server: oversized payload %u",
                                hdr.payload_len);
                    close(cfd);
                    continue;
                }

                uint8_t *payload = NULL;
                if (hdr.payload_len > 0) {
                    payload = malloc(hdr.payload_len + 1);
                    if (!payload) { close(cfd); continue; }
                    ssize_t pr = recv(cfd, payload, hdr.payload_len, MSG_WAITALL);
                    if (pr != (ssize_t)hdr.payload_len) {
                        free(payload);
                        close(cfd);
                        continue;
                    }
                    payload[hdr.payload_len] = '\0';
                }

                work_item_t *w = calloc(1, sizeof(*w));
                if (!w) { free(payload); close(cfd); continue; }
                w->client_fd  = cfd;

                /* ── WHO IS ASKING, ACCORDING TO THE KERNEL ──────────────
                 *
                 * ⚠ NOT hdr.client_pid. That field is filled in by the
                 * sender, and this used to be assigned straight from it —
                 * identity supplied by the thing being observed, in the
                 * daemon whose entire job is security telemetry.
                 *
                 * The socket is 0660 root:synapse, so this is not open to
                 * the world; but any member of that group could claim to be
                 * any PID, and the claim went two places that matter:
                 * context_push() files the event under that PID in the
                 * rolling context the model is later shown, and the anomaly
                 * log names it. So attacker-controlled text could be filed
                 * against somebody else's process and read back as that
                 * process's history.
                 *
                 * SO_PEERCRED is the kernel's answer, taken at connect()
                 * time and not forgeable by the peer. It is the credentials
                 * of whoever opened this socket, which is the question.
                 *
                 * ⚠ It can still be STALE — the process may have exited and
                 * its PID been reused since it connected. That is a much
                 * smaller problem than a forgeable field, and it is the same
                 * PID-reuse race synguard already re-verifies against before
                 * it acts on anything. Nothing here kills a process; the
                 * value is used for attribution, and attribution wants the
                 * kernel's answer.
                 */
                struct ucred cred;
                socklen_t credlen = sizeof(cred);
                if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED,
                               &cred, &credlen) == 0 && credlen == sizeof(cred)) {
                    w->client_pid = (pid_t)cred.pid;
                    w->client_uid = (uid_t)cred.uid;

                    /* A client whose header disagrees with the kernel is
                     * either buggy or lying, and this daemon is the right
                     * place for that to be noticed rather than smoothed
                     * over. 0 means "did not fill it in", which is not a
                     * disagreement. */
                    if (hdr.client_pid != 0 &&
                        (pid_t)hdr.client_pid != w->client_pid)
                        syn_log(LOG_WARNING,
                                "socket_server: client claimed pid=%u, kernel "
                                "says pid=%d uid=%d — using the kernel's",
                                hdr.client_pid, w->client_pid, w->client_uid);
                } else {
                    /* No credentials means no attribution. Refusing the
                     * request would be worse than attributing it to nobody:
                     * this path is how the shell and the compositor ask
                     * questions, and a daemon that stops answering because a
                     * getsockopt failed is a broken desktop. 0 is "unknown",
                     * and it is honest. */
                    syn_log(LOG_WARNING, "socket_server: SO_PEERCRED failed on "
                            "fd=%d (%s) — request attributed to no pid",
                            cfd, strerror(errno));
                    w->client_pid = 0;
                    w->client_uid = (uid_t)-1;
                }
                w->hdr        = hdr;
                w->payload    = payload;
                w->state      = s;
                queue_push(w);
            }
        }
    }
    return NULL;
}

/* ── Public API ───────────────────────────────────────────── */
int socket_server_start(synapd_state_t *s) {
    int fd = -1;

#ifdef HAVE_LIBSYSTEMD
    /* Socket activation. synapd.socket hands us an already-bound, already-
     * listening socket that systemd created, owns and chmodded (SocketGroup=
     * synapse, SocketMode=0660). Binding it and chown()ing it to that group
     * were the ONLY reasons this code needed CAP_CHOWN, so letting systemd do
     * it is what lets synapd run as an unprivileged user. Do not unlink() or
     * bind() on this path — the socket is systemd's, not ours. */
    int nfds = sd_listen_fds(0);
    if (nfds > 1) {
        syn_log(LOG_ERR, "socket_server: got %d activation fds, expected 1", nfds);
        return -1;
    }
    if (nfds == 1) {
        fd = SD_LISTEN_FDS_START;
        if (sd_is_socket_unix(fd, SOCK_STREAM, 1, s->config.socket_path, 0) <= 0) {
            syn_log(LOG_ERR, "socket_server: activation fd is not a listening "
                              "unix socket at %s", s->config.socket_path);
            return -1;
        }
        g_socket_activated = 1;
        syn_log(LOG_INFO, "socket_server: socket-activated, no privilege needed");
    }
#endif

    if (fd < 0) {
        /* Not socket-activated (--socket on the command line, or a build with
         * no libsystemd). Bind it ourselves, which does need privilege to hand
         * the inode to the synapse group. */
        unlink(s->config.socket_path);

        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            syn_log(LOG_ERR, "socket_server: socket(): %s", strerror(errno));
            return -1;
        }

        struct sockaddr_un addr = {0};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, s->config.socket_path, sizeof(addr.sun_path) - 1);

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            syn_log(LOG_ERR, "socket_server: bind(%s): %s",
                     s->config.socket_path, strerror(errno));
            close(fd);
            return -1;
        }

        /* Unix-socket connect() needs write permission on the inode. Clients
         * (synsh in the user's terminal) are not root, so root:root 0660 locked
         * every one of them out with EACCES. Hand the socket to the synapse
         * group — the installer and live ISO both put the login user in it. */
        struct group *gr = getgrnam("synapse");
        if (gr) {
            if (chown(s->config.socket_path, -1, gr->gr_gid) < 0)
                syn_log(LOG_WARNING, "socket_server: chown(%s, :synapse): %s",
                         s->config.socket_path, strerror(errno));
        } else {
            syn_log(LOG_WARNING,
                     "socket_server: no 'synapse' group — only root can connect");
        }
        chmod(s->config.socket_path, 0660);

        if (listen(fd, s->config.max_clients) < 0) {
            syn_log(LOG_ERR, "socket_server: listen(): %s", strerror(errno));
            close(fd);
            return -1;
        }
    }

    s->socket_fd = fd;

    g_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (g_epoll_fd < 0) {
        syn_log(LOG_ERR, "socket_server: epoll_create1: %s", strerror(errno));
        return -1;
    }

    memset(&g_queue, 0, sizeof(g_queue));
    pthread_mutex_init(&g_queue.lock, NULL);
    pthread_cond_init(&g_queue.cond, NULL);

    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_create(&g_workers[i], NULL, worker_thread, NULL);

    pthread_create(&s->server_thread, NULL, server_thread_fn, s);

    syn_log(LOG_INFO, "socket_server: listening on %s (pool=%d)",
             s->config.socket_path, THREAD_POOL_SIZE);
    return 0;
}

void socket_server_stop(synapd_state_t *s) {
    pthread_mutex_lock(&g_queue.lock);
    g_queue.shutdown = 1;
    pthread_cond_broadcast(&g_queue.cond);
    pthread_mutex_unlock(&g_queue.lock);

    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_join(g_workers[i], NULL);

    pthread_join(s->server_thread, NULL);

    if (g_epoll_fd >= 0) { close(g_epoll_fd); g_epoll_fd = -1; }
    if (s->socket_fd >= 0) { close(s->socket_fd); s->socket_fd = -1; }

    /* Only clean up a socket we created ourselves — see g_socket_activated. */
    if (!g_socket_activated)
        unlink(s->config.socket_path);
    syn_log(LOG_INFO, "socket_server: stopped");
}
