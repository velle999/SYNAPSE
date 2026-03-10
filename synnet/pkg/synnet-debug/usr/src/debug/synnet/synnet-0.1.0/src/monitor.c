#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/inet_diag.h>
#include <linux/sock_diag.h>
#include <sys/socket.h>
#include "../include/synnet.h"

int synnet_init(synnet_state_t *s) {
    memset(s, 0, sizeof(*s));

    /* Open netlink socket for network monitoring */
    s->netlink_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_INET_DIAG);
    if (s->netlink_fd < 0) {
        syslog(LOG_WARNING, "synnet: netlink socket failed: %s — monitor disabled",
               strerror(errno));
        s->netlink_fd = -1;
    } else {
        syslog(LOG_INFO, "synnet: netlink monitor initialized");
    }

    s->synapd_fd = -1;
    s->running   = 1;

    syslog(LOG_INFO, "synnet: initialized");
    return 0;
}

void synnet_run(synnet_state_t *s) {
    syslog(LOG_INFO, "synnet: entering monitor loop");
    extern volatile int g_running;

    while (s->running) {
        /* TODO: poll netlink for new connections and query synapd */
        sleep(5);
        s->events_seen++;

        if (s->events_seen % 12 == 0) {
            syslog(LOG_INFO, "synnet: stats — seen=%lu blocked=%lu ai_queries=%lu",
                   s->events_seen, s->events_blocked, s->ai_queries);
        }
    }
}

void synnet_shutdown(synnet_state_t *s) {
    s->running = 0;
    if (s->netlink_fd >= 0) close(s->netlink_fd);
    if (s->synapd_fd >= 0) close(s->synapd_fd);
    syslog(LOG_INFO, "synnet: shutdown complete");
}

int synnet_apply_rule(const char *ip, synnet_action_t action) {
    const char *act = action == SYNNET_ACTION_ALLOW ? "accept" : "drop";
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "nft add rule inet filter input ip saddr %s %s", ip, act);
    int r = system(cmd);
    if (r == 0)
        printf("synnet: %s %s\n", action == SYNNET_ACTION_ALLOW ? "allowed" : "blocked", ip);
    else
        fprintf(stderr, "synnet: nft rule failed\n");
    return r == 0 ? 0 : 1;
}

int synnet_query_ai(synnet_state_t *s, synnet_event_t *ev, char *out, size_t outlen) {
    (void)s; (void)ev; (void)out; (void)outlen;
    /* TODO: send event to synapd socket and get AI verdict */
    return -1;
}
