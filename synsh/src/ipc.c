/*
 * ipc.c — synapd IPC client for synsh
 *
 * Connects to /run/synapd/synapd.sock and exchanges
 * messages using the SYN wire protocol.
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
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#include "synsh.h"
#include "ipc.h"

/*
 * Inline protocol constants to avoid depending on synapd headers.
 * Keep in sync with synapd.h.
 */
#define SYN_MAGIC         0x53594E41u
#define SYN_PROTO_VER     1

#define SYN_MSG_QUERY     0x01
#define SYN_MSG_STATUS    0x06
#define SYN_MSG_RESPONSE  0x80
#define SYN_MSG_ERROR     0xFF

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  msg_type;
    uint16_t flags;
    uint32_t payload_len;
    uint32_t request_id;
    uint32_t client_pid;
    uint64_t timestamp_ns;
} syn_hdr_t;
#pragma pack(pop)

/* ── Connect ──────────────────────────────────────────────── */
int synapd_connect(synsh_state_t *s) {
    s->synapd_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s->synapd_fd < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SYNAPD_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    /* Set a timeout so we don't block if synapd is slow */
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(s->synapd_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s->synapd_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(s->synapd_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->synapd_fd);
        s->synapd_fd = -1;
        return -1;
    }

    s->synapd_connected = 1;
    return 0;
}

/* ── Disconnect ───────────────────────────────────────────── */
void synapd_disconnect(synsh_state_t *s) {
    if (s->synapd_fd >= 0) {
        close(s->synapd_fd);
        s->synapd_fd = -1;
    }
    s->synapd_connected = 0;
}

/* ── Send a message ───────────────────────────────────────── */
static int synapd_send(synsh_state_t *s, uint8_t msg_type,
                        const void *payload, uint32_t plen)
{
    syn_hdr_t hdr = {
        .magic       = SYN_MAGIC,
        .version     = SYN_PROTO_VER,
        .msg_type    = msg_type,
        .payload_len = plen,
        .request_id  = ++s->request_counter,
        .client_pid  = (uint32_t)getpid(),
    };

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    hdr.timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    if (write(s->synapd_fd, &hdr, sizeof(hdr)) != sizeof(hdr)) return -1;
    if (plen && payload)
        if (write(s->synapd_fd, payload, plen) != (ssize_t)plen) return -1;

    return 0;
}

/* ── Receive a response ───────────────────────────────────── */
static int synapd_recv(synsh_state_t *s, char *out_buf, size_t out_len) {
    syn_hdr_t hdr;
    ssize_t r = recv(s->synapd_fd, &hdr, sizeof(hdr), MSG_WAITALL);
    if (r != sizeof(hdr)) return -1;

    if (hdr.magic != SYN_MAGIC) return -1;

    uint8_t base_type = hdr.msg_type & ~(uint8_t)SYN_MSG_RESPONSE;
    (void)base_type;

    if (hdr.msg_type == SYN_MSG_ERROR) {
        /* Read error string */
        char errbuf[256] = {0};
        if (hdr.payload_len > 0 && hdr.payload_len < sizeof(errbuf)) {
            recv(s->synapd_fd, errbuf, hdr.payload_len, MSG_WAITALL);
        }
        return -1;
    }

    if (hdr.payload_len == 0) {
        if (out_buf && out_len > 0) out_buf[0] = '\0';
        return 0;
    }

    uint32_t read_len = hdr.payload_len;
    if (read_len >= out_len) read_len = out_len - 1;

    r = recv(s->synapd_fd, out_buf, read_len, MSG_WAITALL);
    if (r < 0) return -1;

    out_buf[r] = '\0';

    /* Drain leftover payload bytes if we truncated */
    uint32_t remaining = hdr.payload_len - read_len;
    if (remaining > 0) {
        char drain[256];
        while (remaining > 0) {
            uint32_t chunk = remaining < sizeof(drain) ? remaining : sizeof(drain);
            recv(s->synapd_fd, drain, chunk, MSG_WAITALL);
            remaining -= chunk;
        }
    }

    return (int)r;
}

/* ── Public: query synapd ─────────────────────────────────── */
int synapd_query(synsh_state_t *s,
                  const char *prompt,
                  char *out_buf, size_t out_len)
{
    if (!s->synapd_connected) return -1;

    /* Reconnect if needed */
    if (s->synapd_fd < 0) {
        if (synapd_connect(s) < 0) return -1;
    }

    size_t plen = strlen(prompt) + 1;
    if (synapd_send(s, SYN_MSG_QUERY, prompt, (uint32_t)plen) < 0) {
        synapd_disconnect(s);
        return -1;
    }

    return synapd_recv(s, out_buf, out_len);
}

/* ── Public: status check ─────────────────────────────────── */
int synapd_status(synsh_state_t *s, char *out_buf, size_t out_len) {
    if (!s->synapd_connected) {
        snprintf(out_buf, out_len, "synapd: not connected");
        return -1;
    }

    if (synapd_send(s, SYN_MSG_STATUS, NULL, 0) < 0) {
        snprintf(out_buf, out_len, "synapd: send failed");
        return -1;
    }

    return synapd_recv(s, out_buf, out_len);
}

void synsh_ai_disconnect(synsh_state_t *s) { synapd_disconnect(s); }

