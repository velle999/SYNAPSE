/*
 * ai_classifier.c — AI threat classification via synapd
 *
 * Sends a security analysis prompt to synapd and parses
 * the structured response into a threat score and verdict.
 *
 * Prompt format (sent to synapd):
 * ─────────────────────────────────
 *   [SECURITY_ANALYSIS]
 *   <event context>
 *   Classify this event. Reply in EXACTLY this format:
 *   THREAT: none|low|medium|high|critical
 *   VERDICT: allow|log|alert|deny
 *   CONFIDENCE: 0.0-1.0
 *   REASON: <one sentence>
 *
 * Response parsing extracts each field.
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

#include "synguard.h"
#include "sg_log.h"

/* ── synapd wire protocol (minimal inline copy) ───────────── */
#define SYN_MAGIC        0x53594E41u
#define SYN_PROTO_VER    1
#define SYN_MSG_QUERY    0x01
#define SYN_MSG_RESPONSE 0x80
#define SYN_MSG_ERROR    0xFF

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

/* ── Connect to synapd ────────────────────────────────────── */
int sg_synapd_connect(synguard_state_t *s)
{
    s->synapd_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s->synapd_fd < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SYNAPD_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(s->synapd_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s->synapd_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(s->synapd_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->synapd_fd);
        s->synapd_fd = -1;
        return -1;
    }

    s->synapd_connected = 1;
    sg_log(LOG_INFO, "ai_classifier: connected to synapd");
    return 0;
}

void sg_synapd_disconnect(synguard_state_t *s)
{
    if (s->synapd_fd >= 0) {
        close(s->synapd_fd);
        s->synapd_fd = -1;
    }
    s->synapd_connected = 0;
}

/* ── Raw query/response ───────────────────────────────────── */
int sg_synapd_query(synguard_state_t *s, const char *prompt,
                    char *out, size_t out_len)
{
    if (!s->synapd_connected || s->synapd_fd < 0) return -1;

    syn_hdr_t hdr = {
        .magic       = SYN_MAGIC,
        .version     = SYN_PROTO_VER,
        .msg_type    = SYN_MSG_QUERY,
        .payload_len = (uint32_t)(strlen(prompt) + 1),
        .request_id  = ++s->request_counter,
        .client_pid  = (uint32_t)getpid(),
    };

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    hdr.timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    if (write(s->synapd_fd, &hdr, sizeof(hdr)) != sizeof(hdr)) goto reconnect;
    if (write(s->synapd_fd, prompt, hdr.payload_len) != (ssize_t)hdr.payload_len)
        goto reconnect;

    /* Read response header */
    syn_hdr_t rhdr;
    if (recv(s->synapd_fd, &rhdr, sizeof(rhdr), MSG_WAITALL) != sizeof(rhdr))
        goto reconnect;

    if (rhdr.magic != SYN_MAGIC || rhdr.msg_type == SYN_MSG_ERROR) {
        if (rhdr.payload_len > 0 && rhdr.payload_len < out_len)
            recv(s->synapd_fd, out, rhdr.payload_len, MSG_WAITALL);
        return -1;
    }

    if (rhdr.payload_len == 0) { out[0] = '\0'; return 0; }

    uint32_t rlen = rhdr.payload_len < out_len ? rhdr.payload_len : out_len - 1;
    ssize_t r = recv(s->synapd_fd, out, rlen, MSG_WAITALL);
    if (r < 0) goto reconnect;
    out[r] = '\0';

    /* Drain overflow */
    if (rlen < rhdr.payload_len) {
        char drain[256];
        uint32_t rem = rhdr.payload_len - rlen;
        while (rem > 0) {
            uint32_t chunk = rem < sizeof(drain) ? rem : sizeof(drain);
            recv(s->synapd_fd, drain, chunk, MSG_WAITALL);
            rem -= chunk;
        }
    }
    return 0;

reconnect:
    sg_log(LOG_WARNING, "ai_classifier: synapd connection lost, reconnecting");
    sg_synapd_disconnect(s);
    sg_synapd_connect(s);
    return -1;
}

/* ── Parse AI response ────────────────────────────────────── */
static sg_threat_t parse_threat(const char *s)
{
    if (strcasecmp(s, "critical") == 0) return THREAT_CRITICAL;
    if (strcasecmp(s, "high")     == 0) return THREAT_HIGH;
    if (strcasecmp(s, "medium")   == 0) return THREAT_MEDIUM;
    if (strcasecmp(s, "low")      == 0) return THREAT_LOW;
    return THREAT_NONE;
}

static int parse_ai_response(const char *resp, sg_ai_result_t *out)
{
    char threat_str[32]  = {0};
    char verdict_str[32] = {0};
    char conf_str[16]    = {0};
    char reason[256]     = {0};

    /* Parse line by line */
    char copy[2048];
    strncpy(copy, resp, sizeof(copy) - 1);
    char *line = strtok(copy, "\n");
    while (line) {
        if (sscanf(line, "THREAT: %31s",  threat_str) == 1)  {}
        else if (sscanf(line, "VERDICT: %31s", verdict_str) == 1)  {}
        else if (sscanf(line, "CONFIDENCE: %15s", conf_str) == 1) {}
        else if (strncmp(line, "REASON: ", 8) == 0)
            strncpy(reason, line + 8, sizeof(reason) - 1);
        line = strtok(NULL, "\n");
    }

    if (!threat_str[0] && !verdict_str[0]) return -1;

    out->threat_level = parse_threat(threat_str);

    /* Parse verdict string */
    if (strcasecmp(verdict_str, "allow") == 0) out->verdict = VERDICT_ALLOW;
    else if (strcasecmp(verdict_str, "log")    == 0) out->verdict = VERDICT_LOG;
    else if (strcasecmp(verdict_str, "alert")  == 0) out->verdict = VERDICT_ALERT;
    else if (strcasecmp(verdict_str, "deny")   == 0) out->verdict = VERDICT_DENY;
    else out->verdict = VERDICT_LOG;

    out->confidence = conf_str[0] ? (float)atof(conf_str) : 0.5f;
    strncpy(out->reason, reason, sizeof(out->reason) - 1);

    return 0;
}

/* ── Public: classify an event ────────────────────────────── */
int synguard_ai_classify(synguard_state_t *s,
                          const sg_event_t *e,
                          const char *context,
                          sg_ai_result_t *out)
{
    if (!s->config.ai_enabled || !s->synapd_connected) return -1;

    s->stats.ai_queries++;

    char prompt[1024];
    snprintf(prompt, sizeof(prompt),
        "[SECURITY_ANALYSIS]\n"
        "%s\n"
        "\n"
        "Classify this security event. Reply in EXACTLY this format (4 lines):\n"
        "THREAT: none|low|medium|high|critical\n"
        "VERDICT: allow|log|alert|deny\n"
        "CONFIDENCE: 0.0-1.0\n"
        "REASON: <one sentence>\n"
        "\n"
        "Consider: Is this normal system behavior? Is it a known attack pattern?\n"
        "Is the process doing something outside its expected role?",
        context
    );

    char response[2048] = {0};

    /* Apply AI timeout */
    struct timeval tv = {
        .tv_sec  = s->config.ai_timeout_ms / 1000,
        .tv_usec = (s->config.ai_timeout_ms % 1000) * 1000,
    };
    if (s->synapd_fd >= 0) {
        setsockopt(s->synapd_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    int r = sg_synapd_query(s, prompt, response, sizeof(response));
    if (r < 0) {
        s->stats.ai_timeouts++;
        out->verdict     = VERDICT_ALERT;  /* safe fallback */
        out->threat_level = THREAT_LOW;
        strncpy(out->reason, "AI timeout — defaulting to alert",
                sizeof(out->reason) - 1);
        return 0;  /* not a hard failure */
    }

    if (parse_ai_response(response, out) < 0) {
        sg_log(LOG_DEBUG, "ai_classifier: unparseable response: %.100s", response);
        out->verdict      = VERDICT_ALERT;
        out->threat_level = THREAT_LOW;
        strncpy(out->reason, "AI response parse error", sizeof(out->reason) - 1);
    }

    return 0;
}
