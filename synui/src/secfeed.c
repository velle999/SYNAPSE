/*
 * secfeed.c — synguard security-verdict consumer
 *
 * Subscribes to synguard's verdict broadcast (/run/synguard.sock) on a
 * background thread and forwards each fixed-size record over a pipe. The
 * compositor's frame loop drains the pipe (secfeed_dispatch) and, for any
 * verdict whose pid matches a window's client process, sets that window's
 * security border — DENY/QUARANTINE → red "denied", ALERT → red "alert".
 *
 * The thread never touches wlroots/Wayland state (which is single-threaded);
 * it only reads the socket and writes the pipe. All border updates happen on
 * the main thread in secfeed_dispatch(). A record (160 bytes) is smaller than
 * PIPE_BUF, so each pipe write is atomic and reads can't tear.
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

#include "synui.h"

/* Must match synguard's sg_secfeed_msg_t / protocol constants. */
#define SECFEED_SOCKET   "/run/synguard.sock"
#define SECFEED_MAGIC    0x53474656u   /* "SGFV" */
#define SECFEED_VERSION  1

/* synguard sg_verdict_t values we care about. */
#define SG_VERDICT_ALERT       2
#define SG_VERDICT_DENY        4
#define SG_VERDICT_QUARANTINE  5

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t pid;
    uint32_t uid;
    int32_t  verdict;
    int32_t  threat;
    char     comm[16];
    char     reason[112];
} secfeed_msg_t;
#pragma pack(pop)

/* ── Background subscriber thread ─────────────────────────── */
static int secfeed_connect(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SECFEED_SOCKET, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void *secfeed_thread_fn(void *arg)
{
    syn_server_t *s = arg;
    int fd = -1;

    for (;;) {
        if (fd < 0) {
            fd = secfeed_connect();
            if (fd < 0) {
                /* synguard may not be up yet — retry periodically. */
                struct timespec ts = { 2, 0 };
                nanosleep(&ts, NULL);
                continue;
            }
            wlr_log(WLR_INFO, "synui: subscribed to synguard verdict feed");
        }

        secfeed_msg_t msg;
        ssize_t n = recv(fd, &msg, sizeof(msg), MSG_WAITALL);
        if (n != (ssize_t)sizeof(msg)) {
            close(fd);
            fd = -1;
            continue;   /* feed dropped — reconnect */
        }
        if (msg.magic != SECFEED_MAGIC || msg.version != SECFEED_VERSION)
            continue;   /* skip malformed/foreign records */

        /* Hand off to the main thread (atomic: record < PIPE_BUF). */
        if (write(s->sec_pipe[1], &msg, sizeof(msg)) != (ssize_t)sizeof(msg))
            wlr_log(WLR_ERROR, "synui: secfeed pipe write failed");
    }
    return NULL;
}

void secfeed_start(syn_server_t *s)
{
    if (s->sec_disabled) {
        s->sec_pipe[0] = s->sec_pipe[1] = -1;
        return;
    }
    if (pipe(s->sec_pipe) < 0) {
        wlr_log(WLR_ERROR, "synui: secfeed pipe() failed");
        s->sec_pipe[0] = s->sec_pipe[1] = -1;
        return;
    }
    fcntl(s->sec_pipe[0], F_SETFL, O_NONBLOCK);   /* frame loop never blocks */
    if (pthread_create(&s->sec_thread, NULL, secfeed_thread_fn, s) != 0) {
        wlr_log(WLR_ERROR, "synui: secfeed thread failed");
        close(s->sec_pipe[0]); close(s->sec_pipe[1]);
        s->sec_pipe[0] = s->sec_pipe[1] = -1;
    }
}

/* ── Main-thread application ──────────────────────────────── */

/* Find the mapped window whose client process is `pid`, if any. */
static syn_view_t *view_for_pid(syn_server_t *s, pid_t pid)
{
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link) {
            if (!v->mapped) continue;
            /* view_pid() returns the real X11 app pid for xwayland views
             * (all of which otherwise share the Xwayland client). */
            if (view_pid(v) == pid) return v;
        }
    }
    return NULL;
}

static win_security_t verdict_to_security(int32_t verdict)
{
    switch (verdict) {
    case SG_VERDICT_DENY:
    case SG_VERDICT_QUARANTINE: return WIN_SECURE_DENIED;
    case SG_VERDICT_ALERT:      return WIN_SECURE_ALERT;
    default:                    return WIN_SECURE_NORMAL;
    }
}

void secfeed_dispatch(syn_server_t *s)
{
    if (s->sec_disabled || s->sec_pipe[0] < 0) return;

    secfeed_msg_t msg;
    /* Drain whatever has arrived this frame (bounded to avoid starving it). */
    for (int i = 0; i < 32; i++) {
        ssize_t n = read(s->sec_pipe[0], &msg, sizeof(msg));
        if (n != (ssize_t)sizeof(msg)) break;   /* EAGAIN / done */

        win_security_t state = verdict_to_security(msg.verdict);
        if (state == WIN_SECURE_NORMAL) continue;   /* not window-relevant */

        syn_view_t *v = view_for_pid(s, (pid_t)msg.pid);
        if (v) {
            view_set_security(v, state);
            wlr_log(WLR_INFO, "synui: security %s on pid=%u (%s)",
                    state == WIN_SECURE_DENIED ? "DENIED" : "ALERT",
                    msg.pid, msg.comm);
        }
    }
}
