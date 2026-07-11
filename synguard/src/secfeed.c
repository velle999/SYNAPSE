/*
 * secfeed.c — Security-verdict broadcast feed
 *
 * Implements the long-stubbed "syn guard watch" channel: a read-only stream
 * of verdict records that subscribers (notably synui, to colour window
 * borders) can consume. synguard runs as root and the compositor as the user,
 * so the socket lives directly in /run, owned root:synapse and mode 0660 —
 * see the rationale at the chmod in secfeed_init().
 *
 * Design: a listening AF_UNIX stream socket with a dedicated accept thread
 * that appends accepted client fds to a small array. secfeed_publish() — called
 * from the event-processing thread — writes one fixed-size record to every
 * client, dropping any whose write fails (disconnected). All access to the
 * client array is under a mutex. Writes use MSG_NOSIGNAL | MSG_DONTWAIT so a
 * slow or dead subscriber can never block or kill synguard.
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
#include <pthread.h>
#include <poll.h>
#include <time.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "synguard.h"
#include "sg_log.h"

#define SECFEED_MAX_CLIENTS 16

static int             listen_fd = -1;
static int             clients[SECFEED_MAX_CLIENTS];
static int             client_count = 0;
static pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t       accept_thread;
static volatile int    running = 0;

/*
 * Drop subscribers whose peer has gone away. secfeed_publish() also prunes
 * dead clients, but only when it has an alert to deliver — and alerts are
 * rare (often one or none for a whole uptime). Without a reap here, every
 * subscriber that exits (e.g. each synui restart) leaks its slot forever and
 * the feed eventually wedges at SECFEED_MAX_CLIENTS, refusing everyone.
 *
 * Caller must hold clients_lock.
 */
static void clients_reap_locked(void)
{
    if (client_count == 0) return;

    struct pollfd pfd[SECFEED_MAX_CLIENTS];
    for (int i = 0; i < client_count; i++) {
        pfd[i].fd      = clients[i];
        pfd[i].events  = POLLRDHUP;   /* HUP/ERR/NVAL are reported regardless */
        pfd[i].revents = 0;
    }
    if (poll(pfd, client_count, 0) <= 0)
        return;

    /* Walk backwards: compaction moves the tail element into slot i, and the
     * tail has already been examined, so pfd[] stays aligned for i-1..0. */
    const short gone = POLLRDHUP | POLLHUP | POLLERR | POLLNVAL;
    for (int i = client_count - 1; i >= 0; i--) {
        if (!(pfd[i].revents & gone)) continue;
        close(clients[i]);
        clients[i] = clients[--client_count];
        sg_log(LOG_INFO, "secfeed: reaped a departed subscriber (%d left)",
               client_count);
    }
}

/*
 * A refused subscriber reconnects in a loop, so an unthrottled warning here
 * turns into millions of identical journal lines (1.7M in one boot, once).
 * Caller must hold clients_lock, which is what makes the statics safe.
 */
static void log_refused_locked(void)
{
    static time_t        last_log   = 0;
    static unsigned long suppressed = 0;

    time_t now = time(NULL);
    if (last_log != 0 && now - last_log < 60) {
        suppressed++;
        return;
    }
    if (suppressed)
        sg_log(LOG_WARNING, "secfeed: subscriber refused (at capacity, %d clients); "
               "%lu further refusals suppressed", client_count, suppressed);
    else
        sg_log(LOG_WARNING, "secfeed: subscriber refused (at capacity, %d clients)",
               client_count);

    last_log   = now;
    suppressed = 0;
}

static void clients_add(int fd)
{
    pthread_mutex_lock(&clients_lock);

    if (client_count >= SECFEED_MAX_CLIENTS)
        clients_reap_locked();   /* reclaim slots from subscribers that left */

    if (client_count < SECFEED_MAX_CLIENTS) {
        clients[client_count++] = fd;
        sg_log(LOG_INFO, "secfeed: subscriber connected (%d total)", client_count);
    } else {
        close(fd);   /* genuinely at capacity — refuse */
        log_refused_locked();
    }
    pthread_mutex_unlock(&clients_lock);
}

static void *accept_fn(void *arg)
{
    (void)arg;
    while (running) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break;   /* listener closed on shutdown */
        }
        clients_add(fd);
    }
    return NULL;
}

int secfeed_init(void)
{
    listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        sg_log(LOG_WARNING, "secfeed: socket(): %s", strerror(errno));
        return -1;
    }

    unlink(SYNGUARD_SECFEED_SOCKET);   /* clear a stale socket from a crash */

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SYNGUARD_SECFEED_SOCKET, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        sg_log(LOG_WARNING, "secfeed: bind(%s): %s",
               SYNGUARD_SECFEED_SOCKET, strerror(errno));
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }

    /* The compositor runs unprivileged and has to subscribe, so this socket
     * cannot be root-only — but it must not be 0666 either, which is what it
     * used to be. The feed is a live stream of every verdict synguard reaches:
     * world-readable, it hands any local process (a compromised browser tab, a
     * Steam game) a running commentary on exactly what the host's security
     * daemon detects and when — and lets any of them occupy all
     * SECFEED_MAX_CLIENTS slots and starve the real subscriber.
     *
     * Gate it on the `synapse` group instead, the same way synapd gates its
     * control socket (socket_server.c). Fail closed: if the group is missing,
     * the socket stays root-only and the compositor loses border colouring —
     * which is a visible, logged degradation rather than a silent open door. */
    struct group *gr = getgrnam("synapse");
    if (gr) {
        if (chown(SYNGUARD_SECFEED_SOCKET, 0, gr->gr_gid) < 0)
            sg_log(LOG_WARNING, "secfeed: chown(%s, :synapse): %s",
                   SYNGUARD_SECFEED_SOCKET, strerror(errno));
    } else {
        sg_log(LOG_WARNING,
               "secfeed: no 'synapse' group — only root can subscribe");
    }
    chmod(SYNGUARD_SECFEED_SOCKET, 0660);

    if (listen(listen_fd, SECFEED_MAX_CLIENTS) < 0) {
        sg_log(LOG_WARNING, "secfeed: listen(): %s", strerror(errno));
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }

    running = 1;
    if (pthread_create(&accept_thread, NULL, accept_fn, NULL) != 0) {
        sg_log(LOG_WARNING, "secfeed: pthread_create failed");
        running = 0;
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }

    sg_log(LOG_INFO, "secfeed: verdict feed listening on %s",
           SYNGUARD_SECFEED_SOCKET);
    return 0;
}

void secfeed_publish(const sg_alert_t *alert)
{
    if (listen_fd < 0 || !alert) return;

    sg_secfeed_msg_t msg = {
        .magic   = SG_SECFEED_MAGIC,
        .version = SG_SECFEED_VERSION,
        .pid     = alert->event.pid,
        .uid     = alert->event.uid,
        .verdict = (int32_t)alert->verdict,
        .threat  = (int32_t)alert->threat,
    };
    snprintf(msg.comm,   sizeof(msg.comm),   "%s", alert->event.comm);
    snprintf(msg.reason, sizeof(msg.reason), "%s", alert->reason);

    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < client_count; ) {
        ssize_t w = send(clients[i], &msg, sizeof(msg),
                         MSG_NOSIGNAL | MSG_DONTWAIT);
        if (w == (ssize_t)sizeof(msg)) {
            i++;   /* delivered */
        } else {
            /* Dead or backed-up subscriber — drop it (compact the array). */
            close(clients[i]);
            clients[i] = clients[--client_count];
            sg_log(LOG_DEBUG, "secfeed: dropped a subscriber (%d left)",
                   client_count);
        }
    }
    pthread_mutex_unlock(&clients_lock);
}

void secfeed_close(void)
{
    running = 0;
    if (listen_fd >= 0) {
        shutdown(listen_fd, SHUT_RDWR);
        close(listen_fd);
        listen_fd = -1;
        pthread_join(accept_thread, NULL);
    }
    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < client_count; i++)
        close(clients[i]);
    client_count = 0;
    pthread_mutex_unlock(&clients_lock);
    unlink(SYNGUARD_SECFEED_SOCKET);
}
