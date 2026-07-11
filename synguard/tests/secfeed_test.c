/*
 * secfeed_test.c — synguard verdict-feed slot management
 *
 * Drives the real secfeed API over a real AF_UNIX socket (no kernel module, no
 * synapd): the socket path is overridden to a temp file via
 * -DSYNGUARD_SECFEED_SOCKET, and secfeed.c is linked in directly.
 *
 * The case that matters is the slot leak that actually shipped once: subscribers
 * were only reaped when an alert happened to be published, so every subscriber
 * that disconnected (each synui restart) leaked its slot until the feed wedged
 * at SECFEED_MAX_CLIENTS and refused everyone.
 *
 * Whether a subscriber is really *accepted* cannot be read off connect() — at
 * capacity the server accept()s then close()s, so the client's connect still
 * succeeds against the listen backlog. The only honest signal is delivery: a
 * subscriber that holds a real slot receives a published verdict; a refused one
 * sees EOF. So every "is it connected" question here is answered by publishing a
 * verdict and counting who receives it, and every recv() is timeout-bounded so a
 * regressed build fails instead of hanging the suite.
 *
 * SynapseOS Project — GPLv2
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "synguard.h"

#ifndef SECFEED_MAX_CLIENTS
#define SECFEED_MAX_CLIENTS 16   /* mirror secfeed.c; the test asserts against it */
#endif

static int failures;

static void ok(const char *name, int cond)
{
    printf("  %s - %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* Connect one subscriber, with a 200ms recv timeout so no read can ever hang
 * the test — a refused subscriber must fail an assertion, not wedge the suite. */
static int subscribe(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SYNGUARD_SECFEED_SOCKET, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    struct timeval tv = { .tv_sec = 0, .tv_usec = 200 * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

/* The accept thread runs asynchronously, so a just-connect()ed client is not
 * necessarily accepted yet. Give it a moment; short and bounded. */
static void settle(void)
{
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 40 * 1000 * 1000 };  /* 40ms */
    nanosleep(&ts, NULL);
}

static sg_alert_t make_alert(void)
{
    sg_alert_t a = {0};
    a.event.pid = 4321;
    a.event.uid = 1000;
    a.verdict   = VERDICT_DENY;
    a.threat    = THREAT_HIGH;
    snprintf(a.event.comm, sizeof(a.event.comm), "evil");
    snprintf(a.reason, sizeof(a.reason), "exfil attempt");
    return a;
}

/* Publish one verdict, then count how many of fds[] receive a full record.
 * This is the ground truth for "how many subscribers actually hold a slot" —
 * connect() success cannot tell us that. */
static int count_receivers(int *fds, int n)
{
    sg_alert_t alert = make_alert();
    secfeed_publish(&alert);

    int got = 0;
    for (int i = 0; i < n; i++) {
        if (fds[i] < 0) continue;
        sg_secfeed_msg_t msg;
        ssize_t r = recv(fds[i], &msg, sizeof(msg), 0);
        if (r == (ssize_t)sizeof(msg)) got++;
    }
    return got;
}

int main(void)
{
    printf("secfeed slot management:\n");

    if (secfeed_init() != 0) {
        printf("  FAIL - secfeed_init()\n");
        return 1;
    }

    /* 1. Fill every slot, and prove it by delivery: all 16 must receive. */
    int fds[SECFEED_MAX_CLIENTS];
    for (int i = 0; i < SECFEED_MAX_CLIENTS; i++)
        fds[i] = subscribe();
    settle();
    ok("all SECFEED_MAX_CLIENTS subscribers receive a verdict",
       count_receivers(fds, SECFEED_MAX_CLIENTS) == SECFEED_MAX_CLIENTS);

    /* 2. Disconnect every one of them. This is the leak trigger: pre-fix, these
     *    slots were never reclaimed because reaping only happened on publish. */
    for (int i = 0; i < SECFEED_MAX_CLIENTS; i++)
        if (fds[i] >= 0) close(fds[i]);
    settle();

    /* 3. A fresh full wave must be accepted AND receive. Pre-fix the feed was
     *    wedged at capacity: the new subscribers were refused, so none of them
     *    would receive the verdict. (The leaked-in dead slots hold no live fd,
     *    so this count is exactly the newly-accepted subscribers.) */
    for (int i = 0; i < SECFEED_MAX_CLIENTS; i++)
        fds[i] = subscribe();
    settle();
    int wave2 = count_receivers(fds, SECFEED_MAX_CLIENTS);
    if (wave2 == SECFEED_MAX_CLIENTS) {
        ok("slots reclaimed after subscribers left (the leak regression)", 1);
    } else {
        printf("  FAIL - only %d/%d reconnected subscribers received the verdict"
               " — slots leaked\n", wave2, SECFEED_MAX_CLIENTS);
        failures++;
    }

    /* 4. The record arrives intact, field for field. */
    sg_alert_t alert = make_alert();
    secfeed_publish(&alert);
    sg_secfeed_msg_t msg;
    ssize_t r = recv(fds[0], &msg, sizeof(msg), 0);
    ok("subscriber receives a full verdict record", r == (ssize_t)sizeof(msg));
    if (r == (ssize_t)sizeof(msg)) {
        ok("  magic + version correct",
           msg.magic == SG_SECFEED_MAGIC && msg.version == SG_SECFEED_VERSION);
        ok("  pid / verdict / threat round-trip",
           msg.pid == 4321 && msg.verdict == VERDICT_DENY &&
           msg.threat == THREAT_HIGH);
        ok("  comm + reason strings intact",
           strcmp(msg.comm, "evil") == 0 &&
           strcmp(msg.reason, "exfil attempt") == 0);
    }

    /* 5. With every slot held by a live subscriber, one more must be refused —
     *    not silently overwrite a slot. A refused client receives no record. */
    int extra = subscribe();
    settle();
    if (extra >= 0) {
        struct timeval tv = { .tv_sec = 0, .tv_usec = 200 * 1000 };
        setsockopt(extra, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        secfeed_publish(&alert);              /* only the 16 real slots get it */
        char buf[sizeof(sg_secfeed_msg_t)];
        ssize_t er = recv(extra, buf, sizeof(buf), 0);
        ok("subscriber past capacity is refused (no record delivered)", er <= 0);

        /* Drain the record the 16 real subscribers just got, so their buffers
         * don't carry it into any later assertion. */
        for (int i = 0; i < SECFEED_MAX_CLIENTS; i++)
            if (fds[i] >= 0) { char d[sizeof(sg_secfeed_msg_t)]; recv(fds[i], d, sizeof(d), 0); }
        close(extra);
    } else {
        ok("subscriber past capacity is refused (connect rejected)", 1);
    }

    for (int i = 0; i < SECFEED_MAX_CLIENTS; i++)
        if (fds[i] >= 0) close(fds[i]);
    secfeed_close();

    if (failures) {
        printf("FAIL: %d case(s)\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
