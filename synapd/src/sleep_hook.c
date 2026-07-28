/*
 * sleep_hook.c — systemd system-sleep hook: drop synapd's model before suspend.
 *
 * Installed as /usr/lib/systemd/system-sleep/synapd, which systemd runs as
 *     <hook> pre  suspend
 *     <hook> post suspend
 * and, crucially, WAITS for. That wait is the entire mechanism: it is the only
 * point at which something can hold the machine back until the GPU is clear.
 *
 * Why this exists
 * ---------------
 * With NVreg_PreserveVideoMemoryAllocations=1 the NVIDIA driver copies every
 * resident byte of VRAM out to TemporaryFilePath from inside its
 * PM_SUSPEND_PREPARE notifier — before the kernel freezes anything, and with
 * the monitors already dark. The box is fully awake and running for the whole
 * copy, which reads to a person as "it never went to sleep".
 *
 * Measured on this hardware, stopping the AI daemon and suspending again:
 *
 *     synapd running : 7300 MiB VRAM -> 4533.7 MiB written, 83s awake
 *     synapd stopped : 2655 MiB VRAM ->  416.2 MiB written, 36s awake
 *
 * dVRAM 4645 MiB against dWritten 4117 MiB — very nearly 1:1. The dump is
 * sized by resident VRAM and nothing else, which is also why the stall has
 * ranged from 42 seconds to eight hours with no configuration change at all.
 * synapd's model is the single largest allocation on the GPU, so releasing it
 * for the duration of a suspend is the one lever that does not involve
 * repointing a resume-critical path at a nofail mount.
 *
 * Failure policy: NEVER block the suspend. A machine that will not sleep
 * because an optional AI daemon is unreachable is a worse bug than a slow
 * sleep. Every error path here exits 0 and lets the suspend proceed at the old
 * cost.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>

#include "synapd.h"

/*
 * Bounded, but generous. The unload waits for any in-flight generation to
 * finish — the model must not be freed under a running query — and a long
 * answer can take tens of seconds. If it does expire we simply carry on and
 * pay the dump, which is exactly what happened before this existed.
 */
#define REPLY_TIMEOUT_SEC  45

static int connect_synapd(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SYNAPD_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    struct timeval tv = { .tv_sec = REPLY_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

/* Send one header-only message and wait for its reply. Returns 0 on a reply. */
static int ask(int fd, uint8_t type, char *out, size_t out_len)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    syn_msg_header_t hdr = {
        .magic        = SYN_MAGIC,
        .version      = SYNAPD_PROTOCOL_VER,
        .msg_type     = type,
        .flags        = 0,
        .payload_len  = 0,
        .request_id   = 1,
        .client_pid   = (uint32_t)getpid(),
        .timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec,
    };

    if (write(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) return -1;

    syn_msg_header_t rep;
    ssize_t n = read(fd, &rep, sizeof(rep));
    if (n != (ssize_t)sizeof(rep)) return -1;
    if (rep.magic != SYN_MAGIC)    return -1;

    /* Drain the payload even when the caller does not want it, so the socket
     * is not closed mid-message on the daemon's side. */
    if (rep.payload_len > 0 && rep.payload_len < SYN_MAX_PAYLOAD) {
        char *buf = malloc(rep.payload_len + 1);
        if (buf) {
            ssize_t got = read(fd, buf, rep.payload_len);
            if (got > 0) {
                buf[got] = '\0';
                if (out && out_len) snprintf(out, out_len, "%s", buf);
            }
            free(buf);
        }
    }
    return (rep.msg_type == SYN_MSG_ERROR) ? -1 : 0;
}

int main(int argc, char *argv[])
{
    /* systemd passes: <pre|post> <suspend|hibernate|hybrid-sleep|suspend-then-hibernate> */
    if (argc < 3) return 0;

    const char *when = argv[1];
    openlog("synapd-sleep", LOG_PID, LOG_DAEMON);

    int fd = connect_synapd();
    if (fd < 0) {
        /* Not running, or not reachable. Nothing to release — and nothing here
         * is worth delaying a suspend over. */
        syslog(LOG_INFO, "synapd not reachable (%s) — suspending unchanged",
               strerror(errno));
        closelog();
        return 0;
    }

    char reply[256] = {0};
    int rc;

    if (strcmp(when, "pre") == 0) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        rc = ask(fd, SYN_MSG_SLEEP, reply, sizeof(reply));
        clock_gettime(CLOCK_MONOTONIC, &t1);

        double ms = (t1.tv_sec - t0.tv_sec) * 1000.0
                  + (t1.tv_nsec - t0.tv_nsec) / 1000000.0;

        if (rc == 0)
            syslog(LOG_INFO, "released model before suspend in %.0f ms (%s)",
                   ms, reply[0] ? reply : "ok");
        else
            /* Loud, because the symptom is only ever "sleep felt slow again"
             * and there is otherwise nothing to connect that to. */
            syslog(LOG_WARNING,
                   "model NOT released after %.0f ms — the VRAM dump will run "
                   "at full size and the machine will stay awake longer", ms);
    } else if (strcmp(when, "post") == 0) {
        rc = ask(fd, SYN_MSG_WAKE, reply, sizeof(reply));
        syslog(rc == 0 ? LOG_INFO : LOG_WARNING,
               "resume: %s", rc == 0 ? (reply[0] ? reply : "reload started")
                                     : "could not start model reload");
    }

    close(fd);
    closelog();
    return 0;   /* never fail a suspend */
}
