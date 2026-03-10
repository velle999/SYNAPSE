/*
 * ai.c — synapd IPC client
 *
 * Connects synsh to the synapd Unix socket.
 * Sends natural language queries, receives structured responses.
 *
 * Response parsing:
 *   synapd returns a JSON-like structured response that synsh
 *   parses to extract:
 *     - command(s) to execute
 *     - human explanation
 *     - warnings (destructive operations)
 *
 * Response format (synapd emits this via prompt engineering):
 *   SYNSH_CMD: <shell command>
 *   SYNSH_EXPLAIN: <one-line explanation>
 *   SYNSH_WARN: <warning if destructive>
 *   SYNSH_ANSWER: <direct answer, no command>
 *
 * SynapseOS Project — GPLv2
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <time.h>

#include "synsh.h"

/* ── Connect ──────────────────────────────────────────────── */
int synsh_ai_connect(synsh_state_t *s) {
    if (s->synapd_fd >= 0) return 0;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SYN_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* Set reasonable timeout */
    struct timeval tv = { .tv_sec = 30, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    s->synapd_fd = fd;
    s->synapd_online = true;
    s->req_id_counter = 1;
    return 0;
}

/* ── Disconnect ───────────────────────────────────────────── */
void synsh_ai_disconnect(synsh_state_t *s) {
    if (s->synapd_fd >= 0) {
        close(s->synapd_fd);
        s->synapd_fd = -1;
    }
    s->synapd_online = false;
}

/* ── Ping ─────────────────────────────────────────────────── */
bool synsh_ai_ping(synsh_state_t *s) {
    if (s->synapd_fd < 0) {
        if (synsh_ai_connect(s) < 0) return false;
    }

    syn_msg_header_t hdr = {
        .magic       = SYN_MAGIC,
        .version     = SYN_PROTO_VER,
        .msg_type    = SYN_MSG_STATUS,
        .payload_len = 0,
        .request_id  = s->req_id_counter++,
        .client_pid  = (uint32_t)getpid(),
    };
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    hdr.timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    if (write(s->synapd_fd, &hdr, sizeof(hdr)) != sizeof(hdr))
        return false;

    syn_msg_header_t resp;
    if (read(s->synapd_fd, &resp, sizeof(resp)) != sizeof(resp))
        return false;

    return resp.magic == SYN_MAGIC && resp.msg_type == (SYN_MSG_RESPONSE | SYN_MSG_STATUS);
}

/* ── Send/recv helpers ────────────────────────────────────── */
static int send_query(synsh_state_t *s, const char *prompt,
                      uint32_t req_id, char **out_buf, uint32_t *out_len)
{
    /* Build full prompt with synsh-specific instruction prefix */
    char *full_prompt = NULL;
    int plen = asprintf(&full_prompt,
        "[SYNSH_QUERY]\n"
        "The user typed: %s\n\n"
        "You are the AI core of SynapseOS. Respond in this exact format:\n"
        "If this requires running a command:\n"
        "  SYNSH_CMD: <exact shell command>\n"
        "  SYNSH_EXPLAIN: <one sentence what it does>\n"
        "  SYNSH_WARN: <only if destructive/irreversible, else omit>\n"
        "If multiple commands are needed:\n"
        "  SYNSH_CMD: <cmd1>\n"
        "  SYNSH_CMD: <cmd2>\n"
        "  SYNSH_EXPLAIN: <explanation>\n"
        "If this is a question with no command needed:\n"
        "  SYNSH_ANSWER: <direct answer>\n"
        "Never add extra text outside these tags.",
        prompt
    );
    if (plen < 0 || !full_prompt) return -1;

    syn_msg_header_t hdr = {
        .magic       = SYN_MAGIC,
        .version     = SYN_PROTO_VER,
        .msg_type    = SYN_MSG_QUERY,
        .payload_len = (uint32_t)strlen(full_prompt) + 1,
        .request_id  = req_id,
        .client_pid  = (uint32_t)getpid(),
    };
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    hdr.timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    /* Send header + payload */
    if (write(s->synapd_fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        free(full_prompt);
        return -1;
    }
    if (write(s->synapd_fd, full_prompt, hdr.payload_len) != (ssize_t)hdr.payload_len) {
        free(full_prompt);
        return -1;
    }
    free(full_prompt);

    /* Read response header */
    syn_msg_header_t resp_hdr;
    if (read(s->synapd_fd, &resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr))
        return -1;

    if (resp_hdr.magic != SYN_MAGIC || resp_hdr.payload_len == 0)
        return -1;

    /* Read response payload */
    *out_buf = malloc(resp_hdr.payload_len + 1);
    if (!*out_buf) return -1;

    ssize_t r = read(s->synapd_fd, *out_buf, resp_hdr.payload_len);
    if (r <= 0) { free(*out_buf); *out_buf = NULL; return -1; }
    (*out_buf)[r] = '\0';
    *out_len = (uint32_t)r;

    return 0;
}

/* ── Parse synapd response into structured form ───────────── */
static void parse_response(const char *raw, syn_ai_response_t *out) {
    memset(out, 0, sizeof(*out));

    char *copy = strdup(raw);
    char *line = strtok(copy, "\n");

    char *commands[32] = {0};
    int   n_cmds = 0;

    while (line) {
        /* Skip leading whitespace */
        while (*line == ' ' || *line == '\t') line++;

        if (strncmp(line, "SYNSH_CMD:", 10) == 0) {
            char *cmd = line + 10;
            while (*cmd == ' ') cmd++;
            if (n_cmds < 31) commands[n_cmds++] = strdup(cmd);

        } else if (strncmp(line, "SYNSH_EXPLAIN:", 14) == 0) {
            char *exp = line + 14;
            while (*exp == ' ') exp++;
            if (!out->explanation) out->explanation = strdup(exp);

        } else if (strncmp(line, "SYNSH_WARN:", 11) == 0) {
            char *warn = line + 11;
            while (*warn == ' ') warn++;
            if (!out->warning) out->warning = strdup(warn);

        } else if (strncmp(line, "SYNSH_ANSWER:", 13) == 0) {
            char *ans = line + 13;
            while (*ans == ' ') ans++;
            if (!out->explanation) out->explanation = strdup(ans);
            /* Pure answer — no command */
        }

        line = strtok(NULL, "\n");
    }
    free(copy);

    /* Build command fields */
    if (n_cmds == 1) {
        out->command     = commands[0];
        out->is_multiline = false;
    } else if (n_cmds > 1) {
        out->is_multiline = true;
        out->commands    = malloc(n_cmds * sizeof(char *));
        if (out->commands) {
            memcpy(out->commands, commands, n_cmds * sizeof(char *));
            out->n_commands = n_cmds;
        }
    }
}

/* ── Main query function ──────────────────────────────────── */
int synsh_ai_query(synsh_state_t *s, const char *input, syn_ai_response_t *out) {
    if (s->synapd_fd < 0) {
        if (synsh_ai_connect(s) < 0) return -1;
    }

    char    *raw_resp = NULL;
    uint32_t raw_len  = 0;
    uint32_t req_id   = s->req_id_counter++;

    int r = send_query(s, input, req_id, &raw_resp, &raw_len);
    if (r < 0) {
        /* Try reconnect once */
        synsh_ai_disconnect(s);
        if (synsh_ai_connect(s) < 0) return -1;
        r = send_query(s, input, req_id, &raw_resp, &raw_len);
        if (r < 0) return -1;
    }

    parse_response(raw_resp, out);
    free(raw_resp);
    return 0;
}

/* ── AI tab completion ────────────────────────────────────── */
int synsh_ai_complete(synsh_state_t *s, const char *partial,
                      char **completions, int max)
{
    if (s->synapd_fd < 0 || !partial || max <= 0) return 0;

    /* Build a completion-specific prompt */
    char prompt_buf[512];
    snprintf(prompt_buf, sizeof(prompt_buf),
        "[SYNSH_COMPLETE]\n"
        "Suggest up to 5 shell command completions for: %s\n"
        "Reply with one completion per line, nothing else. "
        "Only emit completions, no explanations.",
        partial
    );

    syn_msg_header_t hdr = {
        .magic       = SYN_MAGIC,
        .version     = SYN_PROTO_VER,
        .msg_type    = SYN_MSG_QUERY,
        .payload_len = (uint32_t)strlen(prompt_buf) + 1,
        .request_id  = s->req_id_counter++,
        .client_pid  = (uint32_t)getpid(),
    };
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    hdr.timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    if (write(s->synapd_fd, &hdr, sizeof(hdr)) != sizeof(hdr)) return 0;
    if (write(s->synapd_fd, prompt_buf, hdr.payload_len) != (ssize_t)hdr.payload_len) return 0;

    syn_msg_header_t resp_hdr;
    if (read(s->synapd_fd, &resp_hdr, sizeof(resp_hdr)) != sizeof(resp_hdr)) return 0;

    if (!resp_hdr.payload_len) return 0;

    char *raw = malloc(resp_hdr.payload_len + 1);
    if (!raw) return 0;

    ssize_t r = read(s->synapd_fd, raw, resp_hdr.payload_len);
    if (r <= 0) { free(raw); return 0; }
    raw[r] = '\0';

    /* Parse one completion per line */
    int count = 0;
    char *line = strtok(raw, "\n");
    while (line && count < max) {
        while (*line == ' ') line++;
        if (*line && strncmp(line, partial, strlen(partial)) == 0)
            completions[count++] = strdup(line);
        line = strtok(NULL, "\n");
    }
    free(raw);
    return count;
}

/* ── Free response ────────────────────────────────────────── */
void synsh_ai_response_free(syn_ai_response_t *r) {
    if (!r) return;
    free(r->command);
    free(r->explanation);
    free(r->warning);
    if (r->commands) {
        for (int i = 0; i < r->n_commands; i++) free(r->commands[i]);
        free(r->commands);
    }
    memset(r, 0, sizeof(*r));
}
