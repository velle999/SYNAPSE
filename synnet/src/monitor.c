#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/inet_diag.h>
#include <linux/sock_diag.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <poll.h>
#include <time.h>
#include "../include/synnet.h"

/* ── synapd IPC (reuse syn protocol) ────────────────────── */
#define SYN_MAGIC        0x53594E41u
#define SYN_VERSION      1
#define SYN_MSG_QUERY    0x01
#define SYN_MSG_RESPONSE 0x80

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

static int synapd_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, "/run/synapd/synapd.sock", sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int synapd_query(int fd, const char *prompt, char *out, size_t outlen) {
    if (fd < 0) return -1;

    size_t plen = strlen(prompt);
    syn_hdr_t hdr = {
        .magic       = SYN_MAGIC,
        .version     = SYN_VERSION,
        .msg_type    = SYN_MSG_QUERY,
        .flags       = 0,
        .payload_len = (uint32_t)plen,
        .request_id  = (uint32_t)time(NULL),
        .client_pid  = (uint32_t)getpid(),
    };
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    hdr.timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    if (write(fd, &hdr, sizeof(hdr)) < 0) return -1;
    if (write(fd, prompt, plen) < 0) return -1;

    /* read response header */
    syn_hdr_t rhdr;
    struct timeval tv = {.tv_sec = 15};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (read(fd, &rhdr, sizeof(rhdr)) < (ssize_t)sizeof(rhdr)) return -1;
    if (rhdr.magic != SYN_MAGIC) return -1;

    size_t rlen = rhdr.payload_len < outlen - 1 ? rhdr.payload_len : outlen - 1;
    ssize_t n = read(fd, out, rlen);
    if (n < 0) return -1;
    out[n] = '\0';
    return 0;
}

/* ── Process connector (new connection events) ───────────── */
static int proc_connector_init(void) {
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_CONNECTOR);
    if (fd < 0) return -1;

    struct sockaddr_nl addr = {
        .nl_family = AF_NETLINK,
        .nl_pid    = getpid(),
        .nl_groups = CN_IDX_PROC,
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return -1;
    }

    /* subscribe to proc events */
    struct {
        struct nlmsghdr  nl;
        struct cn_msg    cn;
        enum proc_cn_mcast_op op;
    } msg = {
        .nl = {
            .nlmsg_len   = sizeof(msg),
            .nlmsg_type  = NLMSG_DONE,
            .nlmsg_flags = 0,
            .nlmsg_pid   = getpid(),
        },
        .cn = {
            .id    = {CN_IDX_PROC, CN_VAL_PROC},
            .len   = sizeof(enum proc_cn_mcast_op),
        },
        .op = PROC_CN_MCAST_LISTEN,
    };
    if (send(fd, &msg, sizeof(msg), 0) < 0) {
        close(fd); return -1;
    }
    return fd;
}

/* ── Suspicious connection heuristics ────────────────────── */
static int is_suspicious_port(uint16_t port) {
    /* known C2/malware ports */
    static const uint16_t sus_ports[] = {
        1337, 4444, 4445, 5555, 6666, 7777, 8888, 9999,
        31337, 12345, 54321, 0
    };
    for (int i = 0; sus_ports[i]; i++)
        if (port == sus_ports[i]) return 1;
    return 0;
}

static int is_private_ip(uint32_t ip) {
    uint32_t h = ntohl(ip);
    return (h >> 24 == 10) ||
           ((h >> 20) == (172 << 4 | 1)) ||
           ((h >> 16) == (192 << 8 | 168));
}

/* ── inet_diag: dump active TCP connections ──────────────── */
static void check_connections(synnet_state_t *s) {
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_INET_DIAG);
    if (fd < 0) return;

    struct {
        struct nlmsghdr  nlh;
        struct inet_diag_req req;
    } msg = {
        .nlh = {
            .nlmsg_len   = sizeof(msg),
            .nlmsg_type  = TCPDIAG_GETSOCK,
            .nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST,
        },
        .req = {
            .idiag_family = AF_INET,
            .idiag_states = 0xFFF,
        },
    };

    struct sockaddr_nl addr = {.nl_family = AF_NETLINK};
    if (sendto(fd, &msg, sizeof(msg), 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return;
    }

    char buf[8192];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
        struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
        for (; NLMSG_OK(nlh, (uint32_t)n); nlh = NLMSG_NEXT(nlh, n)) {
            if (nlh->nlmsg_type == NLMSG_DONE) goto done;
            if (nlh->nlmsg_type != TCPDIAG_GETSOCK) continue;

            struct inet_diag_msg *diag = NLMSG_DATA(nlh);
            uint32_t dst_ip = diag->id.idiag_dst[0];
            uint16_t dst_port = ntohs(diag->id.idiag_dport);

            if (dst_ip == 0 || is_private_ip(dst_ip)) continue;

            s->events_seen++;

            if (is_suspicious_port(dst_port)) {
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &dst_ip, ip_str, sizeof(ip_str));

                syslog(LOG_WARNING, "synnet: suspicious connection to %s:%u",
                       ip_str, dst_port);
                s->events_blocked++;

                /* query synapd for verdict */
                if (s->synapd_fd >= 0) {
                    char prompt[512], response[1024];
                    snprintf(prompt, sizeof(prompt),
                        "A process is connecting to %s port %u which is a known malware/C2 port. "
                        "Should this be blocked? Reply with just BLOCK or ALLOW and one sentence reason.",
                        ip_str, dst_port);

                    s->ai_queries++;
                    if (synapd_query(s->synapd_fd, prompt, response, sizeof(response)) == 0) {
                        syslog(LOG_WARNING, "synnet: AI verdict for %s:%u — %s",
                               ip_str, dst_port, response);

                        if (strncmp(response, "BLOCK", 5) == 0) {
                            synnet_apply_rule(ip_str, SYNNET_ACTION_BLOCK);
                            syslog(LOG_WARNING, "synnet: blocked %s (AI verdict)", ip_str);
                        }
                    }
                    /* reconnect for next query */
                    close(s->synapd_fd);
                    s->synapd_fd = synapd_connect();
                }
            }
        }
    }
done:
    close(fd);
}

/* ── Public API ──────────────────────────────────────────── */
int synnet_init(synnet_state_t *s) {
    memset(s, 0, sizeof(*s));

    s->netlink_fd = proc_connector_init();
    if (s->netlink_fd < 0)
        syslog(LOG_WARNING, "synnet: proc connector failed — using poll mode");
    else
        syslog(LOG_INFO, "synnet: proc connector initialized");

    s->synapd_fd = synapd_connect();
    if (s->synapd_fd < 0)
        syslog(LOG_WARNING, "synnet: synapd not available — AI verdicts disabled");
    else
        syslog(LOG_INFO, "synnet: connected to synapd");

    s->running = 1;
    syslog(LOG_INFO, "synnet: initialized");
    return 0;
}

void synnet_run(synnet_state_t *s) {
    syslog(LOG_INFO, "synnet: entering monitor loop");
    int tick = 0;

    while (s->running) {
        check_connections(s);
        sleep(10);
        tick++;

        /* reconnect synapd if disconnected */
        if (s->synapd_fd < 0)
            s->synapd_fd = synapd_connect();

        if (tick % 6 == 0)
            syslog(LOG_INFO, "synnet: stats — seen=%lu blocked=%lu ai_queries=%lu",
                   s->events_seen, s->events_blocked, s->ai_queries);
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

    /* ensure nftables table exists */
    system("nft add table inet synnet 2>/dev/null");
    system("nft add chain inet synnet input { type filter hook input priority 0\\; } 2>/dev/null");

    snprintf(cmd, sizeof(cmd),
        "nft add rule inet synnet input ip saddr %s %s comment \\\"synnet-ai\\\"",
        ip, act);
    int r = system(cmd);
    if (r == 0)
        syslog(LOG_INFO, "synnet: rule applied — %s %s",
               action == SYNNET_ACTION_ALLOW ? "allow" : "block", ip);
    return r == 0 ? 0 : 1;
}

int synnet_query_ai(synnet_state_t *s, synnet_event_t *ev, char *out, size_t outlen) {
    if (s->synapd_fd < 0) return -1;
    char prompt[512];
    char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ev->src_ip, src, sizeof(src));
    inet_ntop(AF_INET, &ev->dst_ip, dst, sizeof(dst));
    snprintf(prompt, sizeof(prompt),
        "Network connection: %s:%u -> %s:%u proto=%u comm=%s. "
        "Is this suspicious? Reply BLOCK or ALLOW with one sentence.",
        src, ev->src_port, dst, ev->dst_port, ev->proto, ev->comm);
    return synapd_query(s->synapd_fd, prompt, out, outlen);
}
