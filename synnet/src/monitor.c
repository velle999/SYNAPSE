#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
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

/* ── nftables enforcement (named-set model) ──────────────────
 *
 * One drop rule "ip daddr @blocked drop" lives in an output-hook chain;
 * blocking an IP just adds it to the set. This is idempotent (re-adding an
 * element or the table/chain is a no-op) and blocks the *outbound* path,
 * which is the direction of the connections we flag.
 */

/* Only ever hand inet_pton-validated dotted-quad strings to nft, so a value
 * that reached us from the network (or argv) can't inject shell. */
static int is_valid_ipv4(const char *ip) {
    struct in_addr a;
    return ip && inet_pton(AF_INET, ip, &a) == 1;
}

static int run_nft(const char *fmt, ...) {
    char cmd[2048];   /* the input-firewall script (ensure_firewall) is ~1KB */
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    /* Refuse a truncated command rather than run half an nft rule — a clipped
     * ruleset could silently narrow (or widen) the firewall. */
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        syslog(LOG_ERR, "synnet: nft command too long (%d bytes) — not run", n);
        return -1;
    }
    return system(cmd);
}

int synnet_nft_ensure(void) {
    /* Additive and idempotent — never flushes, so the set's contents (and
     * thus active blocks) survive a daemon restart. */
    run_nft("nft add table inet " SYNNET_NFT_TABLE " 2>/dev/null");
    run_nft("nft add set inet " SYNNET_NFT_TABLE " " SYNNET_NFT_SET
            " { type ipv4_addr\\; } 2>/dev/null");
    run_nft("nft add chain inet " SYNNET_NFT_TABLE " " SYNNET_NFT_CHAIN
            " { type filter hook output priority 0\\; } 2>/dev/null");
    /* Add the drop rule only if it isn't already present, so repeated
     * ensure() calls don't stack duplicate rules. */
    return run_nft(
        "sh -c 'nft list chain inet " SYNNET_NFT_TABLE " " SYNNET_NFT_CHAIN
        " 2>/dev/null | grep -q \"@" SYNNET_NFT_SET " drop\" || "
        "nft add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_CHAIN
        " ip daddr @" SYNNET_NFT_SET " drop comment \\\"synnet-ai\\\"'");
}

/* How many times the firewall has been rebuilt because it went missing. Not a
 * statistic for its own sake: a number that keeps climbing means something on
 * this box is flushing nftables underneath us, and that is worth being able to
 * see from `--status` rather than by reading a week of journal. */
static unsigned long g_fw_reasserts;

/* Publish what we asserted, so an unprivileged `--status` can answer "am I
 * firewalled" without CAP_NET_ADMIN. See SYNNET_FW_STATE.
 *
 * Written whole and renamed into place: `--status` may read it at any moment,
 * and half a state file is a status command reporting a firewall that is
 * neither on nor off.
 */
const char *synnet_fw_state_path(void) {
    const char *e = getenv("SYNNET_FW_STATE_FILE");
    return (e && *e) ? e : SYNNET_FW_STATE;
}

static void firewall_publish(int active) {
    /* Create the directory if it is not there.
     *
     * systemd's RuntimeDirectory= makes /run/synnet when the SERVICE starts, so
     * the daemon always has it. `sudo synnet --firewall` on a box where the
     * service has never run does not — and without this the firewall would be
     * applied while --status went on saying "not asserted", which is the same
     * class of lie this whole change is about. EEXIST is the normal answer. */
    {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", synnet_fw_state_path());
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) {
            *slash = '\0';
            if (mkdir(dir, 0755) != 0 && errno != EEXIST)
                return;                    /* nowhere to publish; not fatal */
        }
    }

    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", synnet_fw_state_path())
            >= (int)sizeof(tmp))
        return;
    FILE *f = fopen(tmp, "w");
    if (!f) return;                       /* /run/synnet missing: not fatal */
    fprintf(f,
            "state=%s\n"
            "policy=drop\n"
            "trust=lan\n"
            "since=%lld\n"
            "reasserts=%lu\n",
            active ? "active" : "failed",
            (long long)time(NULL), g_fw_reasserts);
    fclose(f);
    if (rename(tmp, synnet_fw_state_path()) != 0)
        unlink(tmp);
}

/* ── Base input firewall (LAN-trust, default-drop) ───────────
 *
 * synnet_nft_ensure() above is egress: it drops connections *out* to flagged
 * IPs. This is the ingress side — the system firewall the box otherwise did not
 * have (nftables.service is disabled, nothing else filters input). A default-
 * drop input chain that accepts:
 *
 *   - loopback (covers ::1)
 *   - established/related — replies to anything we connected out to
 *   - ICMP / ICMPv6 — ping, path-MTU, and IPv6 ND (without which IPv6 breaks)
 *   - private-range sources: RFC1918, IPv6 ULA + link-local — so every service
 *     on the home LAN (Plex, Steam, the python:8077, …) stays reachable without
 *     enumerating their ports, and their dynamic UDP ports are not a problem
 *   - DHCP client replies, whose source may not be in a trusted range yet
 *
 * Everything else — unsolicited inbound from a public address, e.g. on the
 * café/hotel Wi-Fi the box can now roam onto — hits the drop policy. ollama's
 * own guard table still independently fences :11434; the two compose (a packet
 * traverses both input base chains, and any drop is final).
 *
 * Built in ONE `nft -f` load so it is atomic — there is never a window where the
 * chain exists half-populated — and idempotent across restarts via the
 * add/delete/re-add-as-base-chain idiom (redefining a chain in place would
 * otherwise stack duplicate rules every boot).
 *
 * Trust boundary caveat: "private ranges" means public Wi-Fi that also hands out
 * 192.168.x addresses is trusted too. That is the standard pragmatic tradeoff
 * for this model; per-subnet trust would be stricter but breaks on every roam.
 */
int synnet_nft_ensure_firewall(void) {
    static const char *script =
        "nft -f - <<'SYNNET_FW'\n"
        "add table inet " SYNNET_NFT_TABLE "\n"
        /* add-then-delete so the delete cannot fail on a first run where the
         * chain does not exist yet; then (re)create it as a fresh base chain. */
        "add chain inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT "\n"
        "delete chain inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT "\n"
        "add chain inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT
        " { type filter hook input priority 0 ; policy drop ; }\n"
        "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT " iif \"lo\" accept\n"
        "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT
        " ct state established,related accept\n"
        "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT " meta l4proto icmp accept\n"
        "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT " meta l4proto icmpv6 accept\n"
        "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT
        " ip saddr { 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16 } accept\n"
        "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT
        " ip6 saddr { fc00::/7, fe80::/10 } accept\n"
        /* DHCP client: offer/ack can arrive before we hold a trusted-range IP. */
        "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT " udp dport { 68, 546 } accept\n"
        "SYNNET_FW\n";
    int rc = run_nft("%s", script);
    firewall_publish(rc == 0);
    return rc;
}

/* Is our input base chain still there?
 *
 * `nft list chain` needs CAP_NET_ADMIN, so this answers 0 both for "the chain
 * is gone" and for "I am not allowed to look". The caller — the re-assert tick
 * — treats those the same on purpose: rebuilding a chain that is already
 * correct costs one atomic nft load and changes nothing, and the alternative is
 * a daemon that cannot tell it has been disarmed.
 */
int synnet_firewall_present(void) {
    return run_nft("nft list chain inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT
                   " >/dev/null 2>&1") == 0;
}

/* ── Persistent blocklist (one IPv4 per line) ─────────────── */
static int blocklist_contains(const char *ip) {
    FILE *f = fopen(SYNNET_STATE_FILE, "r");
    if (!f) return 0;
    char line[64];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, ip) == 0) { found = 1; break; }
    }
    fclose(f);
    return found;
}

static void blocklist_persist(const char *ip) {
    if (blocklist_contains(ip)) return;   /* idempotent */
    FILE *f = fopen(SYNNET_STATE_FILE, "a");
    if (!f) return;
    fprintf(f, "%s\n", ip);
    fclose(f);
}

/* Rewrite the file without `ip` (used by --allow / unblock). */
static void blocklist_remove(const char *ip) {
    FILE *f = fopen(SYNNET_STATE_FILE, "r");
    if (!f) return;

    char tmp[] = SYNNET_STATE_FILE ".tmp";
    FILE *o = fopen(tmp, "w");
    if (!o) { fclose(f); return; }

    char line[64];
    while (fgets(line, sizeof(line), f)) {
        char trimmed[64];
        snprintf(trimmed, sizeof(trimmed), "%s", line);
        trimmed[strcspn(trimmed, "\r\n")] = '\0';
        if (strcmp(trimmed, ip) != 0)
            fputs(line, o);
    }
    fclose(f);
    fclose(o);
    rename(tmp, SYNNET_STATE_FILE);
}

/* ── Per-run dedup set of destination IPs ─────────────────── */
static int seen_contains(synnet_state_t *s, uint32_t ip) {
    for (size_t i = 0; i < s->seen_count; i++)
        if (s->seen_dst[i] == ip) return 1;
    return 0;
}

static void seen_add(synnet_state_t *s, uint32_t ip) {
    if (seen_contains(s, ip)) return;
    if (s->seen_count == s->seen_cap) {
        size_t cap = s->seen_cap ? s->seen_cap * 2 : 64;
        uint32_t *p = realloc(s->seen_dst, cap * sizeof(*p));
        if (!p) return;   /* drop the dedup entry rather than crash */
        s->seen_dst = p;
        s->seen_cap = cap;
    }
    s->seen_dst[s->seen_count++] = ip;
}

/* Add an IP to the kernel block set + persist it. Honours dry-run. */
static void block_dst(synnet_state_t *s, const char *ip_str) {
    if (!is_valid_ipv4(ip_str)) return;

    if (s->dry_run) {
        syslog(LOG_WARNING, "synnet: [dry-run] WOULD-BLOCK %s", ip_str);
        return;
    }
    if (run_nft("nft add element inet " SYNNET_NFT_TABLE " " SYNNET_NFT_SET
                " { %s }", ip_str) == 0) {
        blocklist_persist(ip_str);
        s->events_blocked++;
        syslog(LOG_WARNING, "synnet: blocked %s (AI verdict)", ip_str);
    } else {
        syslog(LOG_WARNING, "synnet: nft add element failed for %s", ip_str);
    }
}

/* Reload persisted blocks into the kernel set and the dedup table at start,
 * so existing blocks are re-enforced and not re-queried after a restart. */
static void blocklist_reload(synnet_state_t *s) {
    FILE *f = fopen(SYNNET_STATE_FILE, "r");
    if (!f) return;

    char line[64];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!is_valid_ipv4(line)) continue;
        struct in_addr a;
        inet_pton(AF_INET, line, &a);
        seen_add(s, a.s_addr);
        if (!s->dry_run)
            run_nft("nft add element inet " SYNNET_NFT_TABLE " " SYNNET_NFT_SET
                    " { %s } 2>/dev/null", line);
        n++;
    }
    fclose(f);
    if (n) syslog(LOG_INFO, "synnet: reloaded %d persisted block(s)", n);
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

            if (!is_suspicious_port(dst_port)) continue;

            /* Dedup: a long-lived connection shows up on every poll. Only
             * query the AI / act on each destination once per run. */
            if (seen_contains(s, dst_ip)) continue;

            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &dst_ip, ip_str, sizeof(ip_str));
            syslog(LOG_WARNING, "synnet: suspicious connection to %s:%u",
                   ip_str, dst_port);

            if (s->synapd_fd < 0) {
                /* No AI available — flag once, but stay conservative and
                 * don't auto-block (avoid false positives breaking traffic).
                 * Mark seen so we don't log it every poll. */
                seen_add(s, dst_ip);
                syslog(LOG_WARNING,
                       "synnet: no AI verdict available for %s:%u — not blocking",
                       ip_str, dst_port);
                continue;
            }

            char prompt[512], response[1024];
            snprintf(prompt, sizeof(prompt),
                "A process is connecting to %s port %u which is a known malware/C2 port. "
                "Should this be blocked? Reply with just BLOCK or ALLOW and one sentence reason.",
                ip_str, dst_port);

            s->ai_queries++;
            if (synapd_query(s->synapd_fd, prompt, response, sizeof(response)) == 0) {
                /* Got a verdict: record it so we don't re-ask. */
                seen_add(s, dst_ip);
                syslog(LOG_WARNING, "synnet: AI verdict for %s:%u — %s",
                       ip_str, dst_port, response);
                if (strncmp(response, "BLOCK", 5) == 0)
                    block_dst(s, ip_str);
            }
            /* else: transient query failure — leave unseen so we retry next
             * poll. Reconnect for the next query. */
            close(s->synapd_fd);
            s->synapd_fd = synapd_connect();
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

    /* Set up the enforcement table/set/chain and re-apply persisted blocks
     * (unless dry-run, where we only watch). */
    if (!s->dry_run) {
        if (synnet_nft_ensure() != 0)
            syslog(LOG_WARNING, "synnet: nftables setup incomplete — "
                                "enforcement may not work (running as root?)");
        /* Bring up the base input firewall too. Rebuilt from scratch here
         * (atomic + idempotent) and re-checked once a minute in the monitor
         * loop — see the re-assert tick in synnet_run().
         *
         * ⚠ THE START-TIME BUILD ALONE IS NOT A SELF-HEAL, which is what the
         * comment here used to claim. `nft flush ruleset` — from a hand,
         * another firewall tool, or a container runtime — takes our chain with
         * it, and nothing rebuilt it until the next daemon restart. A box can
         * sit unfiltered for a week that way and its own log still says the
         * firewall came up, because it did, once. */
        if (synnet_nft_ensure_firewall() != 0)
            syslog(LOG_WARNING, "synnet: input firewall setup failed — "
                                "the box is not ingress-filtered");
        else
            syslog(LOG_INFO, "synnet: base input firewall active "
                             "(LAN-trust, default-drop)");
    } else {
        syslog(LOG_INFO, "synnet: dry-run — monitoring only, nftables untouched");
    }
    blocklist_reload(s);

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

        /* ── Is the firewall still there? ────────────────────
         *
         * Once a minute, not every tick: this forks `nft`, and the thing being
         * guarded against — something flushing the ruleset out from under us —
         * is rare rather than fast. A minute of exposure after a flush is the
         * price; the alternative was until the next reboot.
         *
         * Silent when nothing is wrong. A rebuild is logged at WARNING with a
         * running count, because "this happened once at boot" and "this has
         * happened forty times today" are different problems and only the
         * count tells them apart.
         *
         * Skipped under --dry-run, which promises not to touch nftables at all.
         */
        if (tick % 6 == 0 && !s->dry_run && !synnet_firewall_present()) {
            g_fw_reasserts++;
            if (synnet_nft_ensure_firewall() == 0)
                syslog(LOG_WARNING, "synnet: input firewall had gone — rebuilt "
                                    "(%lu time(s) this run). Something on this "
                                    "box is flushing nftables.", g_fw_reasserts);
            else
                syslog(LOG_ERR, "synnet: input firewall had gone and could NOT "
                                "be rebuilt — the box is not ingress-filtered");
        }

        if (tick % 6 == 0)
            syslog(LOG_INFO, "synnet: stats — seen=%lu blocked=%lu ai_queries=%lu",
                   s->events_seen, s->events_blocked, s->ai_queries);
    }
}

void synnet_shutdown(synnet_state_t *s) {
    s->running = 0;
    if (s->netlink_fd >= 0) close(s->netlink_fd);
    if (s->synapd_fd >= 0) close(s->synapd_fd);
    free(s->seen_dst);
    s->seen_dst = NULL;
    s->seen_count = s->seen_cap = 0;
    syslog(LOG_INFO, "synnet: shutdown complete");
}

/* CLI entry for `--block <ip>` / `--allow <ip>`. ALLOW means "unblock":
 * remove the IP from the drop set. */
int synnet_apply_rule(const char *ip, synnet_action_t action) {
    if (!is_valid_ipv4(ip)) {
        fprintf(stderr, "synnet: invalid IPv4 address: %s\n", ip);
        syslog(LOG_ERR, "synnet: invalid IP '%s'", ip ? ip : "(null)");
        return 1;
    }

    synnet_nft_ensure();

    int r;
    if (action == SYNNET_ACTION_ALLOW) {
        r = run_nft("nft delete element inet " SYNNET_NFT_TABLE " " SYNNET_NFT_SET
                    " { %s } 2>/dev/null", ip);
        blocklist_remove(ip);
        syslog(LOG_INFO, "synnet: unblocked %s", ip);
        r = 0;   /* deleting a non-present element is not an error for us */
    } else {
        r = run_nft("nft add element inet " SYNNET_NFT_TABLE " " SYNNET_NFT_SET
                    " { %s }", ip);
        if (r == 0) {
            blocklist_persist(ip);
            syslog(LOG_INFO, "synnet: blocked %s", ip);
        }
    }
    return r == 0 ? 0 : 1;
}

/* Print the current block set and synnet ruleset (for `--status`). */
/* One `key=value` out of the published state file, or "" when it is not there. */
static const char *fw_state_get(const char *key) {
    static char val[64];
    val[0] = '\0';

    FILE *f = fopen(synnet_fw_state_path(), "r");
    if (!f) return val;

    char line[128];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            snprintf(val, sizeof(val), "%s", line + klen + 1);
            break;
        }
    }
    fclose(f);
    return val;
}

/*
 * `--status`, and the whole reason it was rewritten.
 *
 * ⚠ IT USED TO SAY THE FIREWALL WAS NOT THERE WHEN IT WAS. The old version ran
 * `nft list set …` and, on any failure, printed "no synnet table loaded —
 * daemon has not run". Listing an nftables object needs CAP_NET_ADMIN, so as an
 * ordinary user that call ALWAYS fails: a fully firewalled box, with the daemon
 * running and "base input firewall active" in its own journal, answered its own
 * status command with "daemon has not run". It also never mentioned the input
 * firewall at all — only the egress block set — so even as root the one thing
 * somebody runs this to ask was missing from the answer.
 *
 * Three separate claims now, kept separate on purpose:
 *
 *   1. what synnet ASSERTED — from /run/synnet/firewall.state, readable by
 *      anyone, which is what makes an unprivileged answer possible at all;
 *   2. what the KERNEL holds — only when we can look, and it says plainly when
 *      it cannot rather than turning "not allowed" into "not there";
 *   3. the egress blocklist, as before.
 */
int synnet_status(void) {
    printf("synnet %s — nftables enforcement status\n\n", SYNNET_VERSION);

    /* ── 1. what synnet says it did ─────────────────────────── */
    const char *st = fw_state_get("state");
    printf("  Input firewall\n");
    if (st[0] == '\0') {
        printf("    not asserted — no %s.\n", synnet_fw_state_path());
        printf("    The daemon publishes that file at start; if synnet is\n");
        printf("    running and it is missing, the daemon predates this or\n");
        printf("    could not write to /run/synnet.\n");
    } else if (strcmp(st, "active") == 0) {
        const char *n = fw_state_get("reasserts");
        printf("    ACTIVE — default-drop input, LAN and established traffic\n");
        printf("    trusted. Asserted by synnet at boot and re-checked every minute.\n");
        if (n[0] && strcmp(n, "0") != 0)
            printf("    ⚠ rebuilt %s time(s) this run — something on this box\n"
                   "      is flushing nftables.\n", n);
    } else {
        printf("    FAILED to apply — this box is NOT ingress-filtered.\n");
        printf("    See `journalctl -u synnet` for what nft said.\n");
    }
    printf("\n");

    /* ── 2. what the kernel actually holds ──────────────────── */
    printf("  Live ruleset\n");
    fflush(stdout);
    if (geteuid() != 0) {
        /* Said BEFORE trying, not after failing. This is the exact spot the old
         * version turned a permission error into a factual claim about the
         * firewall. */
        printf("    (not shown — listing nftables needs root: sudo synnet --status)\n\n");
    } else {
        int r = run_nft("nft list chain inet " SYNNET_NFT_TABLE " "
                        SYNNET_NFT_INPUT " 2>/dev/null");
        if (r != 0)
            printf("    the input chain is NOT loaded in the kernel.\n\n");
        else
            printf("\n");
    }

    /* ── 3. the egress blocklist ────────────────────────────── */
    printf("  Blocked destinations (egress)\n");
    fflush(stdout);
    if (geteuid() != 0) {
        printf("    (not shown — needs root)\n");
        return 0;
    }
    int r = run_nft("nft list set inet " SYNNET_NFT_TABLE " " SYNNET_NFT_SET
                    " 2>/dev/null");
    if (r != 0)
        printf("    none — the synnet table is not loaded.\n");
    return 0;
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
