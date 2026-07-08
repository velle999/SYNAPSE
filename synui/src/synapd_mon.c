/*
 * synapd_mon.c — live synapd activity monitor for the neural overlay
 *
 * A background thread polls synapd's control interface (SYN_MSG_STATUS and
 * SYN_MSG_CONTEXT_GET) while the neural overlay (Super+A) is open, and hands
 * a fixed-size snapshot to the compositor over a pipe. The pipe's read end
 * lives in the Wayland event loop, so a fresh snapshot wakes the main thread
 * (synmon_readable), which copies it into s->overlay and redraws — no polling
 * on the frame loop, and the overlay stays live even when the desktop is
 * otherwise idle (no window damage).
 *
 * The thread never touches wlroots/Wayland state (single-threaded); it only
 * talks to the synapd socket and writes the pipe. A snapshot is < PIPE_BUF,
 * so each write is atomic and reads can't tear. Shutdown mirrors secfeed:
 * raise the stop flag and shutdown() the poll socket so a blocked recv()
 * returns at once (the inter-poll sleep runs in short slices anyway).
 *
 * These are cheap control messages — synapd answers them without invoking the
 * model — so the poll socket uses a short timeout, unlike the AI thread's 90s
 * inference timeout.
 *
 * SynapseOS Project — GPLv2
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
#include <sys/socket.h>
#include <sys/un.h>
#include <stdatomic.h>
#include <stdint.h>

#include <wayland-server-core.h>

#include "synui.h"

/* Must match synapd's wire protocol (synapd/include/synapd.h). */
#define SYN_MAGIC            0x53594E41u
#define SYN_PROTO_VER        1
#define SYN_MSG_CONTEXT_GET  0x05
#define SYN_MSG_STATUS       0x06

/* Control requests never run inference, so a short timeout is right here. */
#define SYNMON_TIMEOUT_SEC   2
/* Poll cadence and the shutdown-responsive sleep slice (100ms units). */
#define SYNMON_POLL_SLICES   10   /* ~1s between polls while overlay open */
#define SYNMON_IDLE_SLICES   3    /* ~300ms tick while overlay hidden */
#define SYNMON_RETRY_SLICES  10   /* ~1s backoff after a failed poll */

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

/* One poll's worth of synapd state, handed main-thread-ward over the pipe. */
typedef struct {
    int           online;
    char          model[16];
    unsigned long requests;
    unsigned long active;
    unsigned      ctx_used;
    unsigned      ctx_window;
    int           activity_n;
    char          activity[OVERLAY_ACTIVITY_MAX][100];
} synmon_snapshot_t;

/* ── Background poller ────────────────────────────────────── */
static int synmon_connect(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SYNAPD_SOCKET, sizeof(addr.sun_path) - 1);
    struct timeval tv = { .tv_sec = SYNMON_TIMEOUT_SEC };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Send a payload-less control request and read its text reply into out.
 * Returns 0 on success, -1 on any protocol/socket error. */
static int synmon_request(int fd, uint8_t type, char *out, size_t out_len)
{
    syn_hdr_t hdr = {
        .magic      = SYN_MAGIC,
        .version    = SYN_PROTO_VER,
        .msg_type   = type,
        .client_pid = (uint32_t)getpid(),
    };
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    hdr.timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    if (write(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) return -1;

    syn_hdr_t rhdr;
    if (recv(fd, &rhdr, sizeof(rhdr), MSG_WAITALL) != (ssize_t)sizeof(rhdr))
        return -1;
    if (rhdr.magic != SYN_MAGIC) return -1;
    if (rhdr.payload_len > (1u << 20)) return -1;

    uint32_t rlen = rhdr.payload_len < out_len ? rhdr.payload_len
                                               : (uint32_t)out_len - 1;
    ssize_t r = 0;
    if (rlen) {
        r = recv(fd, out, rlen, MSG_WAITALL);
        if (r < 0) return -1;
    }
    out[r] = '\0';

    /* Drain any payload past our buffer so the next request stays aligned. */
    uint32_t left = rhdr.payload_len - rlen;
    while (left > 0) {
        char junk[512];
        size_t chunk = left < sizeof(junk) ? left : sizeof(junk);
        ssize_t d = recv(fd, junk, chunk, MSG_WAITALL);
        if (d <= 0) return -1;
        left -= (uint32_t)d;
    }
    return 0;
}

/* Pull the fields we care about out of the STATUS line. Tolerates missing
 * keys, so it still works against an older synapd without ctx_* fields. */
static void parse_status(const char *s, synmon_snapshot_t *snap)
{
    const char *p;
    if ((p = strstr(s, "model=")))       sscanf(p + 6, "%15s", snap->model);
    if ((p = strstr(s, "requests=")))    snap->requests   = strtoul(p + 9, NULL, 10);
    if ((p = strstr(s, "active=")))      snap->active     = strtoul(p + 7, NULL, 10);
    if ((p = strstr(s, "ctx_used=")))    snap->ctx_used   = (unsigned)strtoul(p + 9, NULL, 10);
    if ((p = strstr(s, "ctx_window=")))  snap->ctx_window = (unsigned)strtoul(p + 11, NULL, 10);
}

/* CONTEXT_GET returns a header line then one line per recent event
 * ("[qry pid=123] ...").  Skip the header and keep the last few lines. */
static void parse_context(const char *s, synmon_snapshot_t *snap)
{
    snap->activity_n = 0;

    const char *p = s;
    const char *nl = strchr(p, '\n');
    if (nl) p = nl + 1;   /* drop the "SynapseOS context (...)" header */

    /* Collect lines into a small ring so we naturally keep the newest ones. */
    char ring[OVERLAY_ACTIVITY_MAX][100];
    int head = 0, count = 0;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len > 0) {
            if (len > sizeof(ring[0]) - 1) len = sizeof(ring[0]) - 1;
            memcpy(ring[head], p, len);
            ring[head][len] = '\0';
            head = (head + 1) % OVERLAY_ACTIVITY_MAX;
            if (count < OVERLAY_ACTIVITY_MAX) count++;
        }
        if (!e) break;
        p = e + 1;
    }

    int start = (head - count + OVERLAY_ACTIVITY_MAX) % OVERLAY_ACTIVITY_MAX;
    for (int i = 0; i < count; i++)
        strcpy(snap->activity[snap->activity_n++],
               ring[(start + i) % OVERLAY_ACTIVITY_MAX]);
}

/* Sleep up to slices×100ms, waking early if a shutdown was requested. */
static void synmon_sleep(syn_server_t *s, int slices)
{
    for (int i = 0; i < slices && !atomic_load(&s->synmon_stop); i++) {
        struct timespec ts = { 0, 100 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
}

static void *synmon_thread_fn(void *arg)
{
    syn_server_t *s = arg;
    int fd = -1;

    while (!atomic_load(&s->synmon_stop)) {
        if (fd < 0) {
            fd = synmon_connect();
            if (fd < 0) { synmon_sleep(s, SYNMON_RETRY_SLICES); continue; }
            atomic_store(&s->synmon_fd, fd);
        }

        /* Only poll while the overlay is on screen — otherwise idle cheaply
         * without pestering synapd (and the local model it fronts). */
        if (!atomic_load(&s->synmon_want)) {
            synmon_sleep(s, SYNMON_IDLE_SLICES);
            continue;
        }

        synmon_snapshot_t snap;
        memset(&snap, 0, sizeof(snap));
        char sbuf[512], cbuf[8192];

        if (synmon_request(fd, SYN_MSG_STATUS, sbuf, sizeof(sbuf)) == 0) {
            snap.online = 1;
            parse_status(sbuf, &snap);
            /* synapd counts every message (including this STATUS poll) in
             * requests_active, so it always reports at least our own query.
             * Discount it so the overlay shows only *other* in-flight work. */
            if (snap.active > 0) snap.active--;
            if (synmon_request(fd, SYN_MSG_CONTEXT_GET, cbuf, sizeof(cbuf)) == 0)
                parse_context(cbuf, &snap);
            if (write(s->synmon_pipe[1], &snap, sizeof(snap)) < 0 &&
                errno != EAGAIN)
                wlr_log(WLR_ERROR, "synui: synmon pipe write failed");
            synmon_sleep(s, SYNMON_POLL_SLICES);
        } else {
            /* Poll failed — report offline and reconnect. */
            snap.online = 0;
            if (write(s->synmon_pipe[1], &snap, sizeof(snap)) < 0 &&
                errno != EAGAIN)
                wlr_log(WLR_ERROR, "synui: synmon pipe write failed");
            atomic_store(&s->synmon_fd, -1);
            close(fd);
            fd = -1;
            synmon_sleep(s, SYNMON_RETRY_SLICES);
        }
    }

    atomic_store(&s->synmon_fd, -1);
    if (fd >= 0) close(fd);
    return NULL;
}

/* ── Main-thread application ──────────────────────────────── */
static int synmon_readable(int fd, uint32_t mask, void *data)
{
    (void)mask;
    syn_server_t *s = data;

    /* Coalesce: keep only the most recent snapshot queued this wake-up. */
    synmon_snapshot_t snap;
    int got = 0;
    while (read(fd, &snap, sizeof(snap)) == (ssize_t)sizeof(snap))
        got = 1;
    if (!got) return 0;

    syn_overlay_t *ov = &s->overlay;
    ov->mon_online  = snap.online;
    snprintf(ov->model, sizeof(ov->model), "%s",
             snap.model[0] ? snap.model : "?");
    ov->requests    = snap.requests;
    ov->active      = snap.active;
    ov->ctx_used    = snap.ctx_used;
    ov->ctx_window  = snap.ctx_window;
    ov->activity_n  = snap.activity_n;
    for (int i = 0; i < snap.activity_n; i++)
        snprintf(ov->activity[i], sizeof(ov->activity[i]), "%s",
                 snap.activity[i]);

    /* Redraw if the panel is up; scene damage schedules the frame. */
    if (ov->visible)
        synui_render_overlay(s);
    return 0;
}

/* ── Lifecycle ────────────────────────────────────────────── */
void synmon_start(syn_server_t *s)
{
    atomic_store(&s->synmon_stop, 0);
    atomic_store(&s->synmon_fd, -1);
    atomic_store(&s->synmon_want, 0);
    s->synmon_running = 0;
    s->synmon_src = NULL;

    /* O_CLOEXEC so forked+exec'd children (autostart, AI "CMD:") don't hold
     * the pipe open past our own close. */
    if (pipe2(s->synmon_pipe, O_CLOEXEC) < 0) {
        wlr_log(WLR_ERROR, "synui: synmon pipe() failed");
        s->synmon_pipe[0] = s->synmon_pipe[1] = -1;
        return;
    }
    fcntl(s->synmon_pipe[0], F_SETFL, O_NONBLOCK);   /* handler never blocks */

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->synmon_src = wl_event_loop_add_fd(loop, s->synmon_pipe[0],
                                         WL_EVENT_READABLE, synmon_readable, s);

    if (pthread_create(&s->synmon_thread, NULL, synmon_thread_fn, s) != 0) {
        wlr_log(WLR_ERROR, "synui: synmon thread failed");
        if (s->synmon_src) {
            wl_event_source_remove(s->synmon_src);
            s->synmon_src = NULL;
        }
        close(s->synmon_pipe[0]); close(s->synmon_pipe[1]);
        s->synmon_pipe[0] = s->synmon_pipe[1] = -1;
        return;
    }
    s->synmon_running = 1;
}

void synmon_stop(syn_server_t *s)
{
    if (s->synmon_running) {
        atomic_store(&s->synmon_stop, 1);
        int fd = atomic_load(&s->synmon_fd);
        if (fd >= 0)
            shutdown(fd, SHUT_RDWR);   /* unblock a poll mid-recv() */
        pthread_join(s->synmon_thread, NULL);
        s->synmon_running = 0;
    }
    if (s->synmon_src) {
        wl_event_source_remove(s->synmon_src);
        s->synmon_src = NULL;
    }
    if (s->synmon_pipe[0] >= 0) { close(s->synmon_pipe[0]); s->synmon_pipe[0] = -1; }
    if (s->synmon_pipe[1] >= 0) { close(s->synmon_pipe[1]); s->synmon_pipe[1] = -1; }
}

/* Toggle fast polling. Cheap and thread-safe; a no-op if the monitor never
 * started (pipe setup failed). */
void synmon_set_active(syn_server_t *s, int on)
{
    atomic_store(&s->synmon_want, on ? 1 : 0);
}
