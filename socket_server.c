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
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
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

#include "synapd.h"
#include "socket_server.h"
#include "inference.h"
#include "context.h"
#include "log.h"

#define THREAD_POOL_SIZE   8
#define EPOLL_MAX_EVENTS   64
#define RECV_BUF_SIZE      (64 * 1024)   /* 64 KiB per client recv buf */

/* ── Work queue ───────────────────────────────────────────── */
typedef struct work_item {
    int              client_fd;
    pid_t            client_pid;
    uid_t            client_uid;
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
static void handle_query(work_item_t *w) {
    char *prompt = (char *)w->payload;
    if (!prompt || w->hdr.payload_len == 0) {
        send_error(w->client_fd, w->hdr.request_id, "empty prompt");
        return;
    }
    prompt[w->hdr.payload_len - 1] = '\0';

    /* Fetch rolling system context */
    char sys_ctx[1024] = {0};
    context_get_summary(w->state, sys_ctx, sizeof(sys_ctx));

    /* Run inference */
    char *out = malloc(SYN_MAX_PAYLOAD);
    if (!out) { send_error(w->client_fd, w->hdr.request_id, "oom"); return; }

    int n = inference_run(w->state, sys_ctx, prompt, out, SYN_MAX_PAYLOAD - 1, 512);
    if (n < 0) {
        send_error(w->client_fd, w->hdr.request_id, "inference failed");
        free(out);
        return;
    }

    /* Push Q+A into context store */
    context_push(w->state, CTX_QUERY,    w->client_pid, prompt);
    context_push(w->state, CTX_RESPONSE, 0,             out);

    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_QUERY, out, strlen(out) + 1);
    free(out);
}

static void handle_syscall_event(work_item_t *w) {
    char *evt = (char *)w->payload;
    if (!evt) return;
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
    if (!intent) return;
    intent[w->hdr.payload_len - 1] = '\0';

    int delta = 0;
    inference_sched_hint(w->state, intent, &delta);

    char resp[16];
    snprintf(resp, sizeof(resp), "%d", delta);
    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_SCHED_HINT, resp, strlen(resp) + 1);
}

static void handle_status(work_item_t *w) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "synapd/%s model=%s requests=%lu active=%lu",
        SYNAPD_VERSION,
        w->state->model_loaded ? "loaded" : "none",
        (unsigned long)atomic_load(&w->state->requests_total),
        (unsigned long)atomic_load(&w->state->requests_active)
    );
    send_response(w->client_fd, w->hdr.request_id,
                  SYN_MSG_STATUS, buf, strlen(buf) + 1);
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
        case SYN_MSG_QUERY:         handle_query(w);         break;
        case SYN_MSG_SYSCALL_EVENT: handle_syscall_event(w); break;
        case SYN_MSG_SCHED_HINT:    handle_sched_hint(w);    break;
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

                if (r != sizeof(hdr) || hdr.magic != SYN_MAGIC ||
                    hdr.version != SYNAPD_PROTOCOL_VER) {
                    close(cfd);
                    continue;
                }

                if (hdr.payload_len > SYN_MAX_PAYLOAD) {
                    syn_log(LOG_WARNING, "socket_server: oversized payload %u", hdr.payload_len);
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
                w->client_pid = hdr.client_pid;
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
    unlink(s->config.socket_path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
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

    chmod(s->config.socket_path, 0660);

    if (listen(fd, s->config.max_clients) < 0) {
        syn_log(LOG_ERR, "socket_server: listen(): %s", strerror(errno));
        close(fd);
        return -1;
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

    unlink(s->config.socket_path);
    syn_log(LOG_INFO, "socket_server: stopped");
}
