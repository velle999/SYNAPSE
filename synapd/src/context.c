/*
 * context.c — Rolling system context store
 *
 * Maintains a circular buffer of recent system events
 * (syscalls, queries, responses, system messages).
 * Provides a summary string fed as the system prompt
 * prefix to every inference call.
 *
 * Periodically flushed to /var/lib/synapd/context/ for
 * persistence across daemon restarts.
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "synapd.h"
#include "context.h"
#include "log.h"

/* ── Init ─────────────────────────────────────────────────── */
int context_init(synapd_state_t *s) {
    synapd_context_t *c = &s->context;
    memset(c, 0, sizeof(*c));
    pthread_mutex_init(&c->lock, NULL);
    syn_log(LOG_INFO, "context: initialized (capacity=%d events)", CONTEXT_MAX_EVENTS);

    /* Try to load persisted context */
    char path[256];
    snprintf(path, sizeof(path), "%s/context.log", SYNAPD_CONTEXT_DIR);
    FILE *f = fopen(path, "r");
    if (f) {
        char line[600];
        int loaded = 0;
        while (fgets(line, sizeof(line), f) && loaded < 64) {
            int type; pid_t pid; long ts;
            char data[512] = {0};
            if (sscanf(line, "%d|%d|%ld|%511[^\n]", &type, &pid, &ts, data) == 4) {
                context_push(s, (ctx_event_type_t)type, pid, data);
                loaded++;
            }
        }
        fclose(f);
        if (loaded > 0)
            syn_log(LOG_INFO, "context: restored %d events from disk", loaded);
    }
    return 0;
}

/* ── Push event ───────────────────────────────────────────── */
void context_push(synapd_state_t *s, ctx_event_type_t type,
                  pid_t pid, const char *data)
{
    synapd_context_t *c = &s->context;
    pthread_mutex_lock(&c->lock);

    uint32_t idx = c->head % CONTEXT_MAX_EVENTS;
    ctx_event_t *e = &c->events[idx];

    e->type      = type;
    e->timestamp = time(NULL);
    e->pid       = pid;
    strncpy(e->data, data, sizeof(e->data) - 1);
    e->data[sizeof(e->data) - 1] = '\0';

    c->head++;
    if (c->count < CONTEXT_MAX_EVENTS) c->count++;

    /* Rough token estimate: 1 token ≈ 4 chars */
    c->used_tokens = (c->used_tokens + strlen(data) / 4 + 1);
    if (c->used_tokens > 8192) c->used_tokens = 8192;

    pthread_mutex_unlock(&c->lock);
}

/* ── Get summary (for system prompt injection) ────────────── */
void context_get_summary(synapd_state_t *s, char *out, size_t out_len) {
    synapd_context_t *c = &s->context;
    pthread_mutex_lock(&c->lock);

    uint32_t count = c->count < 32 ? c->count : 32;
    uint32_t start = (c->head - count) % CONTEXT_MAX_EVENTS;

    size_t pos = 0;
    pos += snprintf(out + pos, out_len - pos,
        "SynapseOS context (last %u events):\n", count);

    for (uint32_t i = 0; i < count && pos < out_len - 64; i++) {
        uint32_t idx = (start + i) % CONTEXT_MAX_EVENTS;
        ctx_event_t *e = &c->events[idx];

        const char *type_str;
        switch (e->type) {
        case CTX_SYSCALL:  type_str = "sys";  break;
        case CTX_QUERY:    type_str = "qry";  break;
        case CTX_RESPONSE: type_str = "rsp";  break;
        case CTX_SYSTEM:   type_str = "osys"; break;
        default:           type_str = "unk";  break;
        }

        char truncated[128] = {0};
        strncpy(truncated, e->data, 120);
        if (strlen(e->data) > 120) strcat(truncated, "...");

        pos += snprintf(out + pos, out_len - pos,
            "[%s pid=%d] %s\n", type_str, e->pid, truncated);
    }

    pthread_mutex_unlock(&c->lock);
}

/* ── Token estimate ───────────────────────────────────────── */
uint32_t context_used_tokens(synapd_state_t *s) {
    return s->context.used_tokens;
}

/* ── Flush to disk ────────────────────────────────────────── */
void context_flush(synapd_state_t *s) {
    synapd_context_t *c = &s->context;
    pthread_mutex_lock(&c->lock);

    char path[256];
    snprintf(path, sizeof(path), "%s/context.log", SYNAPD_CONTEXT_DIR);

    FILE *f = fopen(path, "w");
    if (!f) {
        syn_log(LOG_WARNING, "context: flush failed: %s", strerror(errno));
        pthread_mutex_unlock(&c->lock);
        return;
    }

    uint32_t count = c->count < 128 ? c->count : 128;
    uint32_t start = (c->head - count) % CONTEXT_MAX_EVENTS;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start + i) % CONTEXT_MAX_EVENTS;
        ctx_event_t *e = &c->events[idx];
        fprintf(f, "%d|%d|%ld|%s\n",
                (int)e->type, (int)e->pid, (long)e->timestamp, e->data);
    }

    fclose(f);
    pthread_mutex_unlock(&c->lock);
    syn_log(LOG_DEBUG, "context: flushed %u events to disk", count);
}

/* ── Destroy ──────────────────────────────────────────────── */
void context_destroy(synapd_state_t *s) {
    pthread_mutex_destroy(&s->context.lock);
}
