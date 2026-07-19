/*
 * ai_interface.c — AI integration for synui
 *
 * Three AI-facing features:
 *
 *  1. AI THREAD
 *     A background pthread runs the synapd IPC client.
 *     The compositor writes requests to a pipe; the thread
 *     reads them, queries synapd, writes responses back.
 *     Zero blocking of the Wayland event loop.
 *
 *  2. COMMAND BAR (Super+Space)
 *     An in-compositor overlay accepting typed natural language.
 *     Input is sent to synapd. The response can be:
 *       - A command to execute:  "CMD: foot"
 *       - A window action:       "ACTION: focus firefox"
 *       - A workspace action:    "WORKSPACE: switch 3"
 *       - Plain text answer:     displayed in the bar
 *
 *  3. NEURAL OVERLAY (Super+A)
 *     Drawn directly by the compositor each frame.
 *     Shows: synapd status, synguard alerts, active AI_CTX,
 *     workspace intent, system load estimate.
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
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <stdatomic.h>

#include <wlr/render/wlr_renderer.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* ── synapd wire protocol ────────────────────────────────── */
#define SYN_MAGIC       0x53594E41u
#define SYN_PROTO_VER   1
#define SYN_MSG_QUERY   0x01
#define SYN_MSG_RESP    0x80
#define SYN_MSG_ERROR   0xFF

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

/* Write the whole buffer, tolerating partial writes and EINTR. Returns 0 on
 * success, -1 on error. Needed because the request/response structs can
 * exceed PIPE_BUF, so a single write() may transfer only part of them. */
static int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

/* Real on-device inference for even a short prompt has been observed to take
 * ~38s (CPU-only local model, growing context) — a short timeout here isn't
 * "safety margin", it fires on every normal query and permanently wedges the
 * overlay in "reconnecting…" (see ai_thread_fn). 90s gives headroom above
 * that observed worst case while still bounding a truly wedged synapd. */
#define AI_QUERY_TIMEOUT_SEC 90

/* Connect to synapd with send/receive timeouts. The timeouts matter on
 * every connection (not just the first): without them a wedged synapd
 * blocks the AI thread in recv() forever, and ai_thread_stop()'s join then
 * hangs the whole shutdown. Called from AI thread only. */
static int ai_connect_synapd(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SYNAPD_SOCKET, sizeof(addr.sun_path) - 1);
    struct timeval tv = { .tv_sec = AI_QUERY_TIMEOUT_SEC };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Called from AI thread only */
static int ai_thread_synapd_query(int synapd_fd, uint32_t *req_id_ctr,
                                    const char *prompt,
                                    char *out, size_t out_len)
{
    syn_hdr_t hdr = {
        .magic       = SYN_MAGIC,
        .version     = SYN_PROTO_VER,
        .msg_type    = SYN_MSG_QUERY,
        .payload_len = (uint32_t)(strlen(prompt) + 1),
        .request_id  = ++(*req_id_ctr),
        .client_pid  = (uint32_t)getpid(),
    };

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    hdr.timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    if (write(synapd_fd, &hdr, sizeof(hdr)) != sizeof(hdr)) return -1;
    if (write(synapd_fd, prompt, hdr.payload_len) != (ssize_t)hdr.payload_len)
        return -1;

    syn_hdr_t rhdr;
    if (recv(synapd_fd, &rhdr, sizeof(rhdr), MSG_WAITALL) != sizeof(rhdr))
        return -1;
    if (rhdr.magic != SYN_MAGIC || rhdr.msg_type == SYN_MSG_ERROR) return -1;
    if (rhdr.payload_len > (1u << 20))
        return -1;   /* implausible length — treat the stream as corrupt */

    uint32_t rlen = rhdr.payload_len < out_len ? rhdr.payload_len : out_len - 1;
    ssize_t r = recv(synapd_fd, out, rlen, MSG_WAITALL);
    if (r < 0) return -1;
    out[r] = '\0';

    /* Drain any payload beyond our buffer: leaving it in the socket would
     * desynchronise the stream — the next query would parse leftover
     * payload bytes as a header, corrupting every response after it. */
    uint32_t left = rhdr.payload_len - rlen;
    while (left > 0) {
        char junk[512];
        size_t chunk = left < sizeof(junk) ? left : sizeof(junk);
        ssize_t d = recv(synapd_fd, junk, chunk, MSG_WAITALL);
        if (d <= 0) return -1;
        left -= (uint32_t)d;
    }
    return 0;
}

/* ── AI thread ───────────────────────────────────────────── */
static void *ai_thread_fn(void *arg)
{
    syn_server_t *s = (syn_server_t *)arg;

    /* Declare our intent to the AI scheduler */
    struct { uint32_t flags; char intent[256]; uint32_t ph; uint32_t r[4]; } ctx = {
        .flags = (1 << 4),  /* AI_CTX_FLAG_INFERENCE */
    };
    strncpy(ctx.intent, "synui AI thread — routing layout and command requests to synapd",
            sizeof(ctx.intent) - 1);
    syscall(NR_AI_CTX_SET, &ctx);

    int synapd_fd = -1;
    uint32_t req_id = 0;
    char response[4096];

    /* Connect to synapd */
    synapd_fd = ai_connect_synapd();
    if (synapd_fd >= 0) {
        atomic_store(&s->ai_connected, 1);
        atomic_store(&s->ai_synapd_fd, synapd_fd);
        wlr_log(WLR_INFO, "ai_thread: connected to synapd");
        strncpy(s->overlay.synapd_status, "⚡ online",
                sizeof(s->overlay.synapd_status) - 1);
    } else {
        strncpy(s->overlay.synapd_status, "○ synapd offline",
                sizeof(s->overlay.synapd_status) - 1);
    }

    while (atomic_load(&s->ai_connected) || synapd_fd < 0) {
        /* Read request from pipe */
        syn_ai_request_t req;
        ssize_t n = read(s->ai_pipe_req[0], &req, sizeof(req));
        if (n == 0) break;   /* pipe closed */
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Shutting down: drop whatever is still queued rather than running it.
         * Every queued request is an LLM round trip, and the compositor is
         * joining this thread — the answers have nowhere to go. */
        if (atomic_load(&s->ai_stopping)) break;

        if (synapd_fd < 0) {
            /* Try reconnect (same timeouts as the initial connection) */
            synapd_fd = ai_connect_synapd();
            if (synapd_fd < 0) continue;
            atomic_store(&s->ai_connected, 1);
            atomic_store(&s->ai_synapd_fd, synapd_fd);
        }

        memset(response, 0, sizeof(response));
        int ok = ai_thread_synapd_query(synapd_fd, &req_id,
                                         req.prompt,
                                         response, sizeof(response));
        if (ok == 0) {
            /* Clear a stale "reconnecting…"/"offline" label left over from an
             * earlier failure — this is the only place a mid-run recovery
             * gets reflected, since a successful reconnect above doesn't by
             * itself mean the round trip will succeed. */
            strncpy(s->overlay.synapd_status, "⚡ online",
                    sizeof(s->overlay.synapd_status) - 1);
        }
        if (ok < 0) {
            atomic_store(&s->ai_synapd_fd, -1);
            close(synapd_fd);
            synapd_fd = -1;
            atomic_store(&s->ai_connected, 0);
            strncpy(s->overlay.synapd_status, "○ synapd reconnecting…",
                    sizeof(s->overlay.synapd_status) - 1);

            /* Still answer the compositor. A failed round trip used to
             * `continue` silently, which left cmdbar.waiting set and the bar
             * showing "⟳ thinking…" until the session ended — a dead synapd
             * and a slow one looked identical, and neither ever resolved.
             * Not sent while stopping: the compositor is joining this thread
             * and nothing is left to read the pipe. */
            if (!atomic_load(&s->ai_stopping)) {
                syn_ai_response_t err = {
                    .request_id = req.id,
                    .type       = req.type,
                    .ok         = 0,
                };
                strncpy(err.response, "synapd query failed — reconnecting",
                        sizeof(err.response) - 1);
                if (write_all(s->ai_pipe_resp[1], &err, sizeof(err)) < 0)
                    wlr_log(WLR_ERROR, "ai_thread: failed to write error to compositor");
            }
            continue;
        }

        /* Write response back to compositor. The struct exceeds PIPE_BUF, so
         * use write_all; the reader reassembles fragments across frames. */
        syn_ai_response_t resp = {
            .request_id = req.id,
            .type       = req.type,   /* so the compositor can route the reply */
            .ok         = 1,
        };
        strncpy(resp.response, response, sizeof(resp.response) - 1);
        if (write_all(s->ai_pipe_resp[1], &resp, sizeof(resp)) < 0)
            wlr_log(WLR_ERROR, "ai_thread: failed to write response to compositor");
    }

    atomic_store(&s->ai_synapd_fd, -1);
    if (synapd_fd >= 0) close(synapd_fd);
    return NULL;
}

/* On any failure leave every fd at -1 so ai_thread_send/_poll are no-ops —
 * a half-set-up pipe with no consumer would eventually fill and block the
 * event loop (and fd 0 would alias stdin).
 *
 * O_CLOEXEC matters: autostart and AI "CMD:" children fork+exec, and an
 * inherited write end would keep the request pipe from ever reading EOF —
 * ai_thread_stop would then join a thread that never exits. */
int ai_thread_start(syn_server_t *s)
{
    if (pipe2(s->ai_pipe_req, O_CLOEXEC) < 0) {
        s->ai_pipe_req[0]  = s->ai_pipe_req[1]  = -1;
        s->ai_pipe_resp[0] = s->ai_pipe_resp[1] = -1;
        return -1;
    }
    if (pipe2(s->ai_pipe_resp, O_CLOEXEC) < 0) {
        close(s->ai_pipe_req[0]); close(s->ai_pipe_req[1]);
        s->ai_pipe_req[0]  = s->ai_pipe_req[1]  = -1;
        s->ai_pipe_resp[0] = s->ai_pipe_resp[1] = -1;
        return -1;
    }

    /* Make response pipe non-blocking for polling */
    fcntl(s->ai_pipe_resp[0], F_SETFL, O_NONBLOCK);

    atomic_store(&s->ai_connected, 0);
    atomic_store(&s->ai_synapd_fd, -1);

    if (pthread_create(&s->ai_thread, NULL, ai_thread_fn, s) != 0) {
        close(s->ai_pipe_req[0]);  close(s->ai_pipe_req[1]);
        close(s->ai_pipe_resp[0]); close(s->ai_pipe_resp[1]);
        s->ai_pipe_req[0]  = s->ai_pipe_req[1]  = -1;
        s->ai_pipe_resp[0] = s->ai_pipe_resp[1] = -1;
        return -1;
    }
    s->ai_running = 1;
    return 0;
}

/* Shut the AI thread down. Closing the request pipe's write end makes its
 * blocking read() return 0 when it's idle waiting for work — but a query
 * already in flight is blocked in recv() on synapd_fd instead, which that
 * close doesn't touch, so shutdown() it too (same fix as secfeed_stop()'s
 * sec_fd) or a slow/wedged synapd stalls the whole compositor shutdown for
 * up to AI_QUERY_TIMEOUT_SEC. Join, then close the remaining pipe ends.
 * Safe to call unconditionally. */
void ai_thread_stop(syn_server_t *s)
{
    /* Before the shutdown(), so the thread cannot start another query in the
     * window between the socket dying and the join. */
    atomic_store(&s->ai_stopping, 1);

    int fd = atomic_load(&s->ai_synapd_fd);
    if (fd >= 0)
        shutdown(fd, SHUT_RDWR);
    if (s->ai_pipe_req[1] >= 0) {
        close(s->ai_pipe_req[1]);
        s->ai_pipe_req[1] = -1;
    }
    if (s->ai_running) {
        pthread_join(s->ai_thread, NULL);
        s->ai_running = 0;
    }
    if (s->ai_pipe_req[0]  >= 0) { close(s->ai_pipe_req[0]);  s->ai_pipe_req[0]  = -1; }
    if (s->ai_pipe_resp[0] >= 0) { close(s->ai_pipe_resp[0]); s->ai_pipe_resp[0] = -1; }
    if (s->ai_pipe_resp[1] >= 0) { close(s->ai_pipe_resp[1]); s->ai_pipe_resp[1] = -1; }
    atomic_store(&s->ai_connected, 0);
}

void ai_thread_send(syn_server_t *s, const syn_ai_request_t *req)
{
    if (s->ai_disabled || s->ai_pipe_req[1] < 0) return;
    if (write_all(s->ai_pipe_req[1], req, sizeof(*req)) < 0)
        wlr_log(WLR_ERROR, "ai_thread_send: failed to queue request");
}

/*
 * Non-blocking poll for a complete response. Because a syn_ai_response_t is
 * larger than PIPE_BUF its write isn't atomic, so we accumulate fragments
 * into s->ai_resp_rx across calls and only return 0 once a whole struct has
 * arrived. Never blocks the render loop (resp pipe read end is O_NONBLOCK).
 */
int ai_thread_poll(syn_server_t *s, syn_ai_response_t *resp)
{
    if (s->ai_disabled || s->ai_pipe_resp[0] < 0) return -1;

    const size_t need = sizeof(*resp);
    while (s->ai_resp_rx.have < need) {
        ssize_t n = read(s->ai_pipe_resp[0],
                         s->ai_resp_rx.buf + s->ai_resp_rx.have,
                         need - s->ai_resp_rx.have);
        if (n > 0) {
            s->ai_resp_rx.have += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return -1;   /* EAGAIN (no more bytes yet) or closed — try next frame */
    }

    memcpy(resp, s->ai_resp_rx.buf, need);
    s->ai_resp_rx.have = 0;
    return 0;
}

/* ── Command bar ─────────────────────────────────────────── */
void cmdbar_show(syn_server_t *s)
{
    s->cmdbar.visible   = 1;
    s->cmdbar.input_len = 0;
    s->cmdbar.input[0]  = '\0';
    s->cmdbar.response[0] = '\0';
    s->cmdbar.waiting   = 0;
    s->cmdbar.ctx[0]    = '\0';
    s->cmdbar.out_lines = 0;
    s->cmdbar.out_more  = 0;
    synui_render_cmdbar(s);
    wlr_log(WLR_DEBUG, "cmdbar: shown");
}

/* Super+Backspace: the command bar, scoped to the focused window, so you can
 * ask "what is this?" / "why is it using the network?" and have the model know
 * what "this" refers to. With nothing focused it degrades to a plain cmdbar.
 *
 * This used to spawn `foot -e synsh -c 'syn ask'`. `syn` has no `ask`
 * subcommand and never has: the terminal opened, printed "Unknown command",
 * and exited within a frame, so the key looked completely dead. */
void cmdbar_ask_window(syn_server_t *s)
{
    cmdbar_show(s);

    syn_view_t *v = s->focused_view;
    if (!v || !v->mapped) {
        strncpy(s->cmdbar.response, "no focused window — ask anything",
                sizeof(s->cmdbar.response) - 1);
        synui_render_cmdbar(s);
        return;
    }

    const char *app   = view_app_id(v);
    const char *title = view_title(v);
    snprintf(s->cmdbar.ctx, sizeof(s->cmdbar.ctx), "%s — %s",
             app   && *app   ? app   : "(unknown)",
             title && *title ? title : "(untitled)");

    /* Echo the referent back, so the bar visibly says what it is scoped to. */
    snprintf(s->cmdbar.response, sizeof(s->cmdbar.response),
             "about: %s", s->cmdbar.ctx);
    synui_render_cmdbar(s);
    wlr_log(WLR_DEBUG, "cmdbar: ask-window ctx='%s'", s->cmdbar.ctx);
}

void cmdbar_hide(syn_server_t *s)
{
    s->cmdbar.visible = 0;
    synui_render_cmdbar(s);
    wlr_log(WLR_DEBUG, "cmdbar: hidden");
}

void cmdbar_key(syn_server_t *s, uint32_t keysym)
{
    syn_cmdbar_t *bar = &s->cmdbar;

    if (keysym == XKB_KEY_Escape) {
        cmdbar_hide(s);
        return;
    }

    if (keysym == XKB_KEY_Return) {
        cmdbar_submit(s);
        synui_render_cmdbar(s);
        return;
    }

    if (keysym == XKB_KEY_BackSpace) {
        if (bar->input_len > 0)
            bar->input[--bar->input_len] = '\0';
        synui_render_cmdbar(s);
        return;
    }

    /* Printable character */
    if (keysym >= 0x20 && keysym < 0x7F &&
        bar->input_len < CMDBAR_MAX_INPUT - 1) {
        bar->input[bar->input_len++] = (char)keysym;
        bar->input[bar->input_len]   = '\0';
    }
    synui_render_cmdbar(s);
}

/* ── CMD: output capture ─────────────────────────────────── */

/*
 * A CMD: child used to be fork+exec'd onto /dev/null, so "df -h" ran, printed
 * to nothing, and the bar said "ran: df -h" — the command worked and the user
 * saw no answer. So we keep the child's stdout+stderr on a pipe and drain it
 * from the wl_event_loop, never from a blocking read: the compositor's event
 * loop is the same one that drives every client's frames.
 *
 * Why EOF and not waitpid(): SIGCHLD carries reap_children() (synui_main.c),
 * which waitpid(-1)s every child the moment it dies. A waitpid() here would be
 * racing that handler for the status and would usually lose. EOF on the pipe is
 * ours alone, and it says the thing we actually care about — no writer is left.
 *
 * Why a GUI launch does not fill the bar with junk: firefox holds its stdout
 * open for its whole life and chatters on stderr, so its EOF arrives minutes
 * later, long after CMDCAP_SHOW_SECS has passed and the answer stopped being
 * an answer to anything. Short commands EOF in milliseconds and win the bar;
 * long-lived ones silently drain and are dropped at exit.
 *
 * We must keep draining even once the buffer is full: a child whose pipe fills
 * blocks forever in write(), which for a GUI app means a hung window.
 */
#define CMDCAP_BUF_MAX    (16 * 1024)
#define CMDCAP_SHOW_SECS  10

typedef struct {
    syn_server_t           *s;
    struct wl_list          link;       /* s->cmdcaps */
    struct wl_event_source *src;
    int                     fd;
    unsigned                gen;        /* vs s->cmdcap_gen; see synui.h */
    time_t                  started;
    size_t                  len;
    bool                    capped;     /* buf full: still drain, stop storing */
    char                    buf[CMDCAP_BUF_MAX];
} syn_cmdcap_t;

/* Copy `n` bytes of arbitrary child output into `dst` as text cairo will
 * accept. Two hazards, both of which have cost this project a panel before:
 * cairo_show_text() puts its *entire context* into a permanent error state when
 * handed invalid UTF-8 (so one bad byte in `ls` output would blank every row
 * under it), and control bytes render as boxes or move the pen. Anything not a
 * well-formed, non-overlong, non-surrogate scalar is dropped, whole sequences
 * at a time — so `dst` can never end mid-character and never needs trimming. */
static void cmdcap_sanitize(char *dst, size_t cap, const char *src, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; ) {
        unsigned char c = (unsigned char)src[i];

        if (c == '\t') {                       /* tabs: pen-moving, not text */
            if (o + 1 < cap) dst[o++] = ' ';
            i++;
            continue;
        }
        if (c < 0x20 || c == 0x7F) { i++; continue; }   /* controls, incl. ESC */
        if (c < 0x80) { dst[o++] = (char)c; i++; continue; }

        int need;
        unsigned cp;
        if      ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
        else { i++; continue; }                /* stray continuation / 0xF8+ */

        /* i+need indexes the last continuation byte; past the end means the
         * sequence is cut short by the cap and there is nothing valid left. */
        if (i + (size_t)need >= n) break;

        bool ok = true;
        for (int k = 1; k <= need; k++) {
            unsigned char cc = (unsigned char)src[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { i++; continue; }
        /* Overlong, surrogate, or out of range — all rejected by cairo. */
        if ((need == 1 && cp < 0x80) ||
            (need == 2 && cp < 0x800) ||
            (need == 3 && cp < 0x10000) ||
            (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
            i++;
            continue;
        }
        if (o + (size_t)need + 1 >= cap) break;   /* whole sequence or none */
        for (int k = 0; k <= need; k++) dst[o++] = src[i + k];
        i += (size_t)need + 1;
    }
    dst[o] = '\0';
}

/* Split the captured bytes into the bar's rows. Trailing blank lines go: every
 * command ends with a newline and we are not showing an empty row for it. */
static void cmdcap_to_bar(syn_cmdcap_t *c)
{
    syn_cmdbar_t *bar = &c->s->cmdbar;
    bar->out_lines = 0;
    bar->out_more  = 0;

    size_t end = c->len;
    while (end > 0 && (c->buf[end-1] == '\n' || c->buf[end-1] == '\r')) end--;

    size_t start = 0;
    for (size_t i = 0; i <= end; i++) {
        if (i < end && c->buf[i] != '\n') continue;
        if (bar->out_lines < CMDBAR_OUT_LINES) {
            cmdcap_sanitize(bar->out[bar->out_lines], CMDBAR_OUT_COLS,
                            c->buf + start, i - start);
            bar->out_lines++;
        } else {
            bar->out_more++;
        }
        start = i + 1;
    }
    if (c->capped) bar->out_more++;   /* at least one, exact count unknown */
}

static void cmdcap_free(syn_cmdcap_t *c)
{
    wl_list_remove(&c->link);
    if (c->src) wl_event_source_remove(c->src);
    if (c->fd >= 0) close(c->fd);
    free(c);
}

void cmdcap_stop_all(syn_server_t *s)
{
    syn_cmdcap_t *c, *tmp;
    wl_list_for_each_safe(c, tmp, &s->cmdcaps, link)
        cmdcap_free(c);
}

/* EOF (or error): the child is done writing. Take the bar only if this is still
 * the newest command, the bar is still up, and the answer is still fresh. */
static void cmdcap_finish(syn_cmdcap_t *c)
{
    syn_server_t *s = c->s;
    bool fresh = (time(NULL) - c->started) <= CMDCAP_SHOW_SECS;

    wlr_log(WLR_DEBUG,
            "cmdbar: capture EOF: %zu bytes, gen %u/%u, visible %d, fresh %d",
            c->len, c->gen, s->cmdcap_gen, s->cmdbar.visible, (int)fresh);

    if (c->gen == s->cmdcap_gen && s->cmdbar.visible && fresh && c->len) {
        cmdcap_to_bar(c);
        synui_render_cmdbar(s);
    }
    cmdcap_free(c);
}

static int cmdcap_readable(int fd, uint32_t mask, void *data)
{
    (void)mask;
    syn_cmdcap_t *c = data;
    char scratch[4096];

    for (;;) {
        ssize_t n = read(fd, scratch, sizeof scratch);
        if (n > 0) {
            if (!c->capped) {
                size_t room = sizeof(c->buf) - c->len;
                size_t take = (size_t)n < room ? (size_t)n : room;
                memcpy(c->buf + c->len, scratch, take);
                c->len += take;
                if (c->len == sizeof(c->buf)) c->capped = true;
            }
            continue;                       /* drain: never stall the child */
        }
        if (n < 0 && errno == EINTR)  continue;   /* SIGCHLD landed mid-read */
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;                       /* still running; more to come */
        break;                              /* 0 = EOF, or a real error */
    }
    cmdcap_finish(c);
    return 0;
}

/*
 * fork+exec `path` with argv[] and its output on a pipe we drain. Returns false
 * only if the command could not be started at all; the caller still owns
 * bar->response.
 *
 * argv, not a command string, because one caller passes the user's own words
 * (see cmdbar_submit): building a shell line out of those means quoting them,
 * and a line with an apostrophe in it — "what's running" — either breaks or,
 * worse, doesn't.
 */
static bool cmdcap_spawn(syn_server_t *s, const char *path,
                         const char *const argv[])
{
    int pfd[2];
    /* O_CLOEXEC: this pipe must not leak into the *next* CMD: child, which
     * would hold the write end open and defer our EOF until that one exits. */
    if (pipe2(pfd, O_CLOEXEC) < 0) {
        wlr_log(WLR_ERROR, "cmdbar: pipe: %s", strerror(errno));
        return false;
    }

    syn_cmdcap_t *c = calloc(1, sizeof *c);
    if (!c) { close(pfd[0]); close(pfd[1]); return false; }

    pid_t pid = fork();
    if (pid < 0) {
        wlr_log(WLR_ERROR, "cmdbar: fork: %s", strerror(errno));
        close(pfd[0]); close(pfd[1]); free(c);
        return false;
    }
    if (pid == 0) {
        setsid();   /* detach like spawn() so it outlives the compositor */
        /* Undo O_CLOEXEC on the write end by dup2'ing it onto the std fds;
         * dup2's copy does not carry the flag, so it survives exec. */
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
        synui_child_reset_signals();
        execvp(path, (char *const *)argv);
        _exit(127);
    }

    close(pfd[1]);                       /* our EOF depends on only the child
                                          * holding the write end */
    fcntl(pfd[0], F_SETFL, O_NONBLOCK);  /* the handler must never block */

    c->s       = s;
    c->fd      = pfd[0];
    c->gen     = ++s->cmdcap_gen;
    c->started = time(NULL);
    wl_list_insert(&s->cmdcaps, &c->link);

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    c->src = wl_event_loop_add_fd(loop, pfd[0], WL_EVENT_READABLE,
                                  cmdcap_readable, c);
    if (!c->src) { cmdcap_free(c); return false; }
    return true;
}

/* A shell command line, captured the same way. This is the CMD: path: what the
 * model returns is a *shell fragment* — pipes, redirects and quoting are the
 * point of it — so it has to reach a shell to mean anything. */
static bool cmdcap_run(syn_server_t *s, const char *cmd)
{
    const char *argv[] = { "sh", "-c", cmd, NULL };
    return cmdcap_spawn(s, "/bin/sh", argv);
}

/*
 * EVERY path out of here must leave bar->response saying what happened.
 *
 * cmdbar_submit() pre-loads the buffer with "⟳ thinking…", and the frame
 * handler clears bar->waiting and re-renders whatever is in it. So a branch
 * that acts and returns without rewriting the text leaves the bar spinning on
 * "thinking…" forever — the answer arrived, the command ran, and the user is
 * still watching a spinner. That was true of all three structured branches,
 * and since the prompt *asks* the model for CMD:, it was the common case, not
 * an edge one.
 */
void execute_ai_action(syn_server_t *s, const char *response)
{
    /* Parse structured actions from AI response */

    /* CMD: <shell command> */
    const char *cmd = strstr(response, "CMD:");
    if (cmd) {
        cmd += 4;
        while (*cmd == ' ') cmd++;
        char cmdcopy[512];
        strncpy(cmdcopy, cmd, sizeof(cmdcopy) - 1);
        cmdcopy[sizeof(cmdcopy) - 1] = '\0';
        /* Truncate at newline */
        char *nl = strchr(cmdcopy, '\n');
        if (nl) *nl = '\0';
        wlr_log(WLR_INFO, "cmdbar: executing CMD: %s", cmdcopy);

        /* Naming what ran is the honest report until the output lands — and
         * stays the whole report for a GUI launch, which prints nothing. */
        snprintf(s->cmdbar.response, sizeof(s->cmdbar.response),
                 "▸ %s", cmdcopy);
        if (!cmdcap_run(s, cmdcopy))
            snprintf(s->cmdbar.response, sizeof(s->cmdbar.response),
                     "could not run: %s", cmdcopy);
        return;
    }

    /* ACTION: focus <app_id> */
    const char *action = strstr(response, "ACTION:");
    if (action) {
        char act[128] = {0};
        sscanf(action + 7, " %127[^\n]", act);
        if (strncmp(act, "focus ", 6) == 0) {
            const char *app_id = act + 6;
            /* Find and focus matching window */
            syn_workspace_t *ws = server_active_workspace(s);
            syn_view_t *v;
            bool found = false;
            wl_list_for_each(v, &ws->windows, link) {
                if (!v->mapped) continue;
                const char *aid = view_app_id(v);
                if (aid && strstr(aid, app_id)) {
                    focus_view(s, v, view_surface(v));
                    found = true;
                    break;
                }
            }
            snprintf(s->cmdbar.response, sizeof(s->cmdbar.response),
                     found ? "▸ focused %s" : "no window matching '%s'", app_id);
        } else {
            snprintf(s->cmdbar.response, sizeof(s->cmdbar.response),
                     "unknown action: %s", act);
        }
        return;
    }

    /* WORKSPACE: switch <N> */
    const char *ws_act = strstr(response, "WORKSPACE:");
    if (ws_act) {
        int n;
        if (sscanf(ws_act + 10, " switch %d", &n) == 1 &&
            n >= 1 && n <= WORKSPACE_MAX) {
            workspace_switch(s, n - 1);
            snprintf(s->cmdbar.response, sizeof(s->cmdbar.response),
                     "▸ workspace %d", n);
        } else {
            /* workspace_switch() ignores an out-of-range index (layout.c), so
             * the range check here is not about safety — it is so a nonsense N
             * is reported instead of silently doing nothing. */
            snprintf(s->cmdbar.response, sizeof(s->cmdbar.response),
                     "bad workspace in: %.64s", ws_act);
        }
        return;
    }

    /* Default: display response text in command bar */
    strncpy(s->cmdbar.response, response, sizeof(s->cmdbar.response) - 1);
}

/*
 * Would synsh answer this itself?
 *
 * The bar and synsh take the same kind of input, and synsh already knows the
 * answers to the common ones — that music here means cliamp, that packages mean
 * pacman, that "disk space" is `df -h`. The bar knew none of it, so every line
 * went to the model, and the model does not know what is installed: asked to
 * play music it suggests whatever players existed on the internet, which is how
 * Super+Space could not play music while the prompt could.
 *
 * The question goes to `synsh --intent-check` rather than to a table of our own
 * because two tables become two behaviours. This is the one place both agree.
 *
 * Synchronous, on the wl_event_loop, which is normally the thing you must never
 * do — but --intent-check exists to be called this way: it matches against
 * $PATH and exits before touching synapd, in ~1ms. The alternative is spending
 * a 35-123s model round trip to be told the time.
 */
static bool synsh_claims(const char *line)
{
    /*
     * Block SIGCHLD BEFORE the fork, not after it.
     *
     * SIGCHLD is a real async handler here (signal(SIGCHLD, reap_children) in
     * synui_main.c), and it waitpid(-1)s every child it can. This is the one
     * child in the file whose exit STATUS is the answer, so it must not be
     * reaped out from under us — and --intent-check exits in about a
     * millisecond, so "after the fork" is not early enough: the child can be
     * dead and reaped before the next line runs, leaving waitpid() with ECHILD
     * and this function guessing.
     *
     * The child clears the mask in synui_child_reset_signals() — a blocked mask
     * survives exec, and synsh must not inherit one.
     */
    sigset_t chld, prev;
    sigemptyset(&chld);
    sigaddset(&chld, SIGCHLD);
    sigprocmask(SIG_BLOCK, &chld, &prev);

    pid_t pid = fork();
    if (pid < 0) {                  /* can't ask: let the model have it */
        sigprocmask(SIG_SETMASK, &prev, NULL);
        return false;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        synui_child_reset_signals();
        execlp("synsh", "synsh", "--intent-check", line, (char *)NULL);
        _exit(127);
    }

    int st = 0;
    pid_t r;
    while ((r = waitpid(pid, &st, 0)) < 0 && errno == EINTR)
        ;

    sigprocmask(SIG_SETMASK, &prev, NULL);

    if (r != pid || !WIFEXITED(st)) return false;
    /* 0 = mine. 70 = not an intent. 127 = synsh is not installed — both mean
     * the model, which is exactly what happened before this existed. */
    return WEXITSTATUS(st) == 0;
}

/*
 * What is actually installed, for the prompt — synsh's answer, not ours.
 *
 * Without it the model recommends software from the internet at large: asked
 * about disk usage it has offered gnome-disks, zenity and kde-disk-manager on a
 * box that has none of them. synsh already resolves this list from $PATH to
 * keep its own prompt honest, so the bar asks synsh rather than growing a
 * second list that can disagree with the first.
 *
 * Resolved once and cached: $PATH does not change under a running compositor,
 * and this is the same synchronous-fork bargain as synsh_claims() — worth it
 * once, not per keystroke. "" if synsh is missing, which just restores the old
 * prompt rather than failing the query.
 */
static const char *synsh_toolinfo(void)
{
    static char buf[512];
    static bool done = false;
    if (done) return buf;
    done = true;

    /* popen, not the fork/waitpid dance above: the output is the whole answer
     * and the exit status is irrelevant, so reap_children() stealing the child
     * from pclose() costs us nothing. */
    FILE *f = popen("synsh --toolinfo 2>/dev/null", "r");
    if (!f) return buf;
    if (fgets(buf, sizeof(buf), f)) buf[strcspn(buf, "\n")] = '\0';
    pclose(f);
    if (buf[0]) wlr_log(WLR_INFO, "cmdbar: tools: %s", buf);
    return buf;
}

/* True if the first word of `line` is an executable — the same test synsh uses
 * to tell a command from a question (classify.c's cmd_in_path), so a program
 * name typed into the bar launches exactly as it would in the shell. A word
 * with a '/' is a path and is checked directly; otherwise $PATH is walked. */
static bool cmdbar_is_launch(const char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    /* '?' is the AI prefix idiom; never treat a question as a launch. */
    if (!*line || *line == '?') return false;

    char first[128];
    size_t i = 0;
    while (line[i] && line[i] != ' ' && line[i] != '\t' && i < sizeof(first) - 1)
        first[i] = line[i], i++;
    first[i] = '\0';

    if (strchr(first, '/'))                       /* ./x or /usr/bin/x */
        return access(first, X_OK) == 0;

    const char *path = getenv("PATH");
    if (!path) return false;
    char copy[4096];
    snprintf(copy, sizeof(copy), "%s", path);
    for (char *dir = strtok(copy, ":"); dir; dir = strtok(NULL, ":")) {
        char full[600];
        if (snprintf(full, sizeof(full), "%s/%s", dir, first) >= (int)sizeof(full))
            continue;
        if (access(full, X_OK) == 0) return true;
    }
    return false;
}

/* Fire-and-forget launch of a typed command in a foot terminal. --hold keeps
 * the window after the command exits, so a one-shot's output ("ls -la") is not
 * lost the instant it prints; an interactive program (htop, vim) holds it open
 * itself until quit. sh -c carries the line verbatim — flags, pipes and quoting
 * mean what a shell would make of them, without this code re-quoting the user's
 * own words. Returns false only if the process could not be started at all. */
static bool cmdbar_launch_term(const char *line)
{
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();                                 /* outlive the compositor */
        synui_child_reset_signals();
        execlp("foot", "foot", "--hold", "sh", "-c", line, (char *)NULL);
        _exit(127);
    }
    return true;
}

void cmdbar_submit(syn_server_t *s)
{
    syn_cmdbar_t *bar = &s->cmdbar;
    if (!bar->input_len) return;

    /* The last command's output is not an answer to this question. Drop it as
     * the question is asked, not when the next output lands: the reply may be
     * plain text or a GUI launch, neither of which writes a row. A capture
     * still draining from the previous command carries an older generation and
     * so cannot repopulate these either (see cmdcap_spawn). */
    bar->out_lines = 0;
    bar->out_more  = 0;

    /*
     * Intents first, and note this runs BEFORE bar->waiting is set: synsh
     * answers now, not eventually, so the bar must not be left saying
     * "thinking…" about a question already answered.
     *
     * --intent, because synsh's -c is the shell interface and means shell
     * there; this is the caller that wants the prompt's behaviour instead.
     * Output lands in the bar through the same capture as CMD:, so `df -h`
     * prints its rows and cliamp reports the terminal it opened.
     */
    if (synsh_claims(bar->input)) {
        wlr_log(WLR_INFO, "cmdbar: synsh intent: %s", bar->input);
        const char *argv[] = { "synsh", "--intent", "-c", bar->input, NULL };
        snprintf(bar->response, sizeof(bar->response), "▸ %s", bar->input);
        if (!cmdcap_spawn(s, "synsh", argv))
            snprintf(bar->response, sizeof(bar->response),
                     "could not run synsh: %s", bar->input);
        return;
    }

    /*
     * A bare program name is a launch, not a question: routing "htop" through
     * the model would pay the ~38s round trip only to be told to run the thing
     * already named. If the first word is a program on PATH, open the line in a
     * terminal — the same call synsh makes when it classifies a line as a
     * command, so the bar and the shell agree on what typing a program does.
     *
     * Below the intent check on purpose: "play <song>" is an intent first, not
     * an invocation of whatever `play` happens to be on PATH.
     */
    if (cmdbar_is_launch(bar->input)) {
        wlr_log(WLR_INFO, "cmdbar: launch: %s", bar->input);
        if (cmdbar_launch_term(bar->input)) {
            cmdbar_hide(s);              /* hand the keyboard to the terminal */
        } else {
            snprintf(bar->response, sizeof(bar->response),
                     "could not launch: %s", bar->input);
        }
        return;
    }

    bar->waiting = 1;
    strncpy(bar->response, "⟳ thinking…", sizeof(bar->response) - 1);

    /* Build prompt with compositor context. focused_window is only present when
     * the bar was opened with Super+Backspace (cmdbar_ask_window), which is what
     * gives "what is this?" a referent. */
    char focus_line[256] = "";
    if (bar->ctx[0])
        snprintf(focus_line, sizeof(focus_line),
                 "focused_window: %s\n", bar->ctx);

    /* The tool line is what stops CMD: naming programs this machine has never
     * had; the Arch line is the same correction synsh's prompt carries, because
     * a model asked to install something reaches for apt-get by default and the
     * suggestion looks perfectly reasonable until you run it. */
    const char *tools = synsh_toolinfo();

    char prompt[1024];
    snprintf(prompt, sizeof(prompt),
        "[COMPOSITOR_CMD]\n"
        "workspace: %s\n"
        "%s"
        "%s%s%s"
        "request: %s\n"
        "\n"
        "This is Arch Linux. Use only programs listed above as installed;\n"
        "if none fits, say so rather than naming one that is not there.\n"
        "\n"
        "Respond with one of:\n"
        "CMD: <shell command to run>\n"
        "ACTION: focus <app_id>\n"
        "WORKSPACE: switch <N>\n"
        "Or plain text to display as answer.",
        server_active_workspace(s)->name,
        focus_line,
        tools[0] ? "installed: " : "", tools, tools[0] ? "\n" : "",
        bar->input
    );

    syn_ai_request_t req = {
        .type = AI_MSG_QUERY_CMD,
        .id   = (uint64_t)time(NULL),
    };
    strncpy(req.prompt, prompt, sizeof(req.prompt) - 1);
    ai_thread_send(s, &req);

    /* Async: response handled in compositor main loop via ai_thread_poll */
}

/* ── Neural overlay ──────────────────────────────────────── */
void overlay_toggle(syn_server_t *s)
{
    s->overlay.visible = !s->overlay.visible;
    /* Drive the synapd monitor: poll fast while the panel is on screen, idle
     * when it's hidden. */
    synmon_set_active(s, s->overlay.visible);
    if (s->overlay.visible)
        overlay_update(s);
    synui_render_overlay(s);
}

void overlay_update(syn_server_t *s)
{
    syn_overlay_t *ov = &s->overlay;

    /* Workspace info */
    syn_workspace_t *ws = server_active_workspace(s);
    int win_count = 0;
    syn_view_t *v;
    wl_list_for_each(v, &ws->windows, link)
        if (v->mapped) win_count++;

    snprintf(ov->ai_context, sizeof(ov->ai_context),
        "workspace: %s [%d]  windows: %d  layout: %s%s",
        ws->name, ws->index + 1, win_count,
        ws->layout == LAYOUT_TILING   ? "tiling"   :
        ws->layout == LAYOUT_MONOCLE  ? "monocle"  :
        ws->layout == LAYOUT_AI       ? "AI"       : "floating",
        ws->intent[0] ? "  intent: " : ""
    );
    if (ws->intent[0])
        strncat(ov->ai_context, ws->intent,
                sizeof(ov->ai_context) - strlen(ov->ai_context) - 1);

    ov->last_update = time(NULL);
}

/*
 * overlay_render — draw the neural overlay using wlroots renderer
 *
 * The overlay is a semi-transparent dark panel in the top-right corner.
 * We draw colored rectangles for the background and use the renderer's
 * scissor to clip. Text rendering would require a font library (Pango
 * or freetype); here we write a framebuffer-ready placeholder and note
 * that in SynapseOS the full implementation uses cairo + pango via
 * a layer-shell surface (cleaner than inline rendering).
 */
void overlay_render(syn_server_t *s, struct wlr_renderer *renderer,
                    int width, int height)
{
    (void)renderer;
    (void)width;
    (void)height;
    synui_render_overlay(s);
}
