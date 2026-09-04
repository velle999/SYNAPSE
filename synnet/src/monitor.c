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
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../include/synnet.h"
#include "../include/i18n.h"

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
    /* The input-firewall script is ~1KB of base rules, plus two rules for every
     * trusted interface AND one for every opened port, so this has to hold
     * SYNNET_MAX_IFACES and SYNNET_MAX_PORTS of them and not merely the fixed
     * part. Under-sizing it would trip the truncation guard below and leave the
     * box unfiltered rather than partly filtered — the safe failure, but a
     * needless one. */
    char cmd[32768];
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

/* Defined below, beside the state file it writes. Forward-declared because the
 * off switch publishes too — a firewall that was turned off and a firewall that
 * was never asserted are different answers, and --status has to be able to give
 * the first one. */
static void firewall_publish_state(const char *state);

const char *synnet_fw_pref_path(void) {
    const char *e = getenv("SYNNET_FW_PREF_FILE");
    return (e && *e) ? e : SYNNET_FW_PREF;
}

/*
 * Is the firewall wanted?
 *
 * ⚠ EVERY FAILURE READS AS ON. No file, unreadable, empty, garbage — all of
 * them mean "nobody has said otherwise", and the only safe reading of that for
 * a firewall is that it stays up. The one string that turns it off is the exact
 * word `off`; anything else, including a truncated write, leaves the box
 * filtered. A parser that fails open is a parser that disarms a machine because
 * a disk filled up.
 */
int synnet_firewall_enabled(void) {
    FILE *f = fopen(synnet_fw_pref_path(), "r");
    if (!f) return 1;

    char buf[32] = "";
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return 1; }
    fclose(f);

    buf[strcspn(buf, "\r\n")] = '\0';
    /* Leading and trailing space, because this file is meant to be editable by
     * hand and `echo off > file` is not the only way somebody will write it. */
    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;
    size_t n = strlen(p);
    while (n && (p[n-1] == ' ' || p[n-1] == '\t')) p[--n] = '\0';

    return strcmp(p, "off") != 0;
}

int synnet_firewall_set_enabled(int on) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", synnet_fw_pref_path());
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
    }

    /* Written whole and renamed: the daemon reads this every minute, and a
     * half-written file is a firewall decided by a race. */
    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", synnet_fw_pref_path())
            >= (int)sizeof(tmp))
        return -1;

    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", on ? "on" : "off");
    if (fclose(f) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, synnet_fw_pref_path()) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* ── Trusted container/VM links ──────────────────────────────
 *
 * See SYNNET_FW_IFACES in synnet.h for what this is for and, more importantly,
 * for what it deliberately is not.
 */
const char *synnet_fw_ifaces_path(void) {
    const char *e = getenv("SYNNET_FW_IFACES_FILE");
    return (e && *e) ? e : SYNNET_FW_IFACES;
}

/* How many links the last apply trusted, for the published state file. */
static unsigned g_fw_ifaces;

/*
 * Is this a legal interface name?
 *
 * ⚠ NOT COSMETIC VALIDATION. These names are interpolated into the nft script,
 * which is loaded in ONE atomic `nft -f`: a line containing a space, a quote or
 * a semicolon does not produce one bad rule, it produces a syntax error that
 * fails the whole load and takes the entire firewall with it. An unparseable
 * line in this file has to cost that line and nothing else, so anything that is
 * not a kernel-legal interface name is dropped here with a warning and the
 * firewall comes up without it.
 *
 * The kernel's own rules: 1..IFNAMSIZ-1 bytes, no '/' and no whitespace. This
 * is stricter — alphanumerics, '_', '.', '-' — because every interface name a
 * container runtime actually creates fits that, and the ones that would not are
 * exactly the ones worth refusing to paste into a ruleset.
 */
static int iface_name_valid(const char *n) {
    size_t len = strlen(n);
    if (len == 0 || len >= SYNNET_IFNAME_MAX) return 0;
    if (!isalnum((unsigned char)n[0]) && n[0] != '_') return 0;
    for (size_t i = 0; i < len; i++) {
        char c = n[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '.' && c != '-')
            return 0;
    }
    return 1;
}

/* Read the file into `out`, skipping comments, blanks, duplicates and anything
 * that is not a legal name. Returns how many were taken. A missing file is not
 * an error — it is the normal state of a box running no containers. */
static size_t trusted_ifaces_load(char out[][SYNNET_IFNAME_MAX], size_t max) {
    FILE *f = fopen(synnet_fw_ifaces_path(), "r");
    if (!f) return 0;

    size_t n = 0;
    char line[256];
    while (n < max && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = strchr(line, '#');
        if (p) *p = '\0';                       /* trailing comment */
        p = line;
        while (*p == ' ' || *p == '\t') p++;
        size_t l = strlen(p);
        while (l && (p[l-1] == ' ' || p[l-1] == '\t')) p[--l] = '\0';
        if (!*p) continue;

        if (!iface_name_valid(p)) {
            syslog(LOG_WARNING, "synnet: ignoring '%s' in %s — not a legal "
                                "interface name", p, synnet_fw_ifaces_path());
            continue;
        }
        int dup = 0;
        for (size_t i = 0; i < n; i++)
            if (strcmp(out[i], p) == 0) { dup = 1; break; }
        if (dup) continue;

        /* memcpy, not snprintf: iface_name_valid() has already established
         * strlen(p) < SYNNET_IFNAME_MAX, and snprintf here makes the compiler
         * warn about a truncation that cannot happen — a warning a reader then
         * has to go and disprove. */
        memcpy(out[n++], p, strlen(p) + 1);
    }
    fclose(f);
    return n;
}

/* ── Ports opened to a source the base chain would drop ───
 *
 * See SYNNET_FW_PORTS in the header for why this exists at all: `--allow` is an
 * unblock, `--trust-if` is DHCP+DNS, and neither of them can let a non-private
 * source reach a port.
 */
const char *synnet_fw_ports_path(void) {
    const char *e = getenv("SYNNET_FW_PORTS_FILE");
    return (e && *e) ? e : SYNNET_FW_PORTS;
}

/* How many port rules the last apply loaded, for the published state file. */
static unsigned g_fw_ports;

/*
 * Is this a legal source? A CIDR, or the word `any`.
 *
 * ⚠ THE PREFIX LENGTH IS CHECKED AGAINST THE FAMILY, not against 128 for both.
 * nft refuses `ip saddr 10.0.0.0/64` and refusing it HERE costs one rule;
 * letting it reach the atomic load costs the whole firewall.
 *
 * `family` is filled with AF_INET / AF_INET6, or AF_UNSPEC for `any`.
 */
static int src_valid(const char *src, int *family) {
    if (!src || !*src) return 0;
    if (strcmp(src, "any") == 0) { *family = AF_UNSPEC; return 1; }

    const char *slash = strchr(src, '/');
    if (!slash || !slash[1]) return 0;

    char addr[64];
    size_t alen = (size_t)(slash - src);
    if (alen == 0 || alen >= sizeof(addr)) return 0;
    memcpy(addr, src, alen);
    addr[alen] = '\0';

    /* ⛔ DIGITS ONLY, AND NOT VIA atoi. atoi("8bogus") is 8, so a prefix with a
     * tail would validate here and then be pasted into the ruleset intact. */
    for (const char *d = slash + 1; *d; d++)
        if (!isdigit((unsigned char)*d)) return 0;
    if (strlen(slash + 1) > 3) return 0;
    int bits = atoi(slash + 1);

    struct in_addr a4;
    struct in6_addr a6;
    if (inet_pton(AF_INET, addr, &a4) == 1) {
        if (bits < 0 || bits > 32) return 0;
        *family = AF_INET;
        return 1;
    }
    if (inet_pton(AF_INET6, addr, &a6) == 1) {
        if (bits < 0 || bits > 128) return 0;
        *family = AF_INET6;
        return 1;
    }
    return 0;
}

/*
 * Parse `<proto>/<port>` plus a source into the stored form.
 *
 * ⚠ EVERYTHING HERE IS INTERPOLATED INTO THE ATOMIC nft SCRIPT, so this is the
 * same rule iface_name_valid() follows: a value that is not exactly what it
 * claims to be does not make one bad rule, it makes a syntax error that fails
 * the load and takes the WHOLE firewall with it. Nothing reaches the script
 * that has not been through here.
 */
int synnet_port_rule_norm(const char *proto_port, const char *src,
                          char *out, size_t outsz) {
    if (!proto_port || !out) return -1;

    const char *slash = strchr(proto_port, '/');
    if (!slash || !slash[1]) return -1;

    char proto[8];
    size_t plen = (size_t)(slash - proto_port);
    if (plen == 0 || plen >= sizeof(proto)) return -1;
    memcpy(proto, proto_port, plen);
    proto[plen] = '\0';
    if (strcmp(proto, "tcp") != 0 && strcmp(proto, "udp") != 0) return -1;

    for (const char *d = slash + 1; *d; d++)
        if (!isdigit((unsigned char)*d)) return -1;
    if (strlen(slash + 1) > 5) return -1;
    long port = strtol(slash + 1, NULL, 10);
    if (port < 1 || port > 65535) return -1;

    const char *s = (src && *src) ? src : "any";
    int fam;
    if (!src_valid(s, &fam)) return -1;

    int n = snprintf(out, outsz, "%s/%ld %s", proto, port, s);
    if (n < 0 || (size_t)n >= outsz) return -1;
    return 0;
}

/* Split a stored line back into its parts. Returns 0 on success. */
static int port_rule_split(const char *line, char *proto, size_t protosz,
                           long *port, char *src, size_t srcsz, int *family) {
    char pp[SYNNET_PORTRULE_MAX], sr[SYNNET_PORTRULE_MAX];
    /* ⚠ Two fields, whitespace-separated, and the source is REQUIRED in the
     * stored form. A line missing it is a line whose meaning cannot be told
     * from a typo, so it is refused rather than defaulted — defaulting it to
     * `any` here would silently open a port to the internet. */
    if (sscanf(line, "%63s %63s", pp, sr) != 2) return -1;

    char norm[SYNNET_PORTRULE_MAX];
    if (synnet_port_rule_norm(pp, sr, norm, sizeof(norm)) != 0) return -1;

    const char *slash = strchr(pp, '/');
    size_t plen = (size_t)(slash - pp);
    if (plen >= protosz) return -1;
    memcpy(proto, pp, plen);
    proto[plen] = '\0';
    *port = strtol(slash + 1, NULL, 10);
    if (snprintf(src, srcsz, "%s", sr) < 0) return -1;
    return src_valid(sr, family) ? 0 : -1;
}

/* Read the file into `out`, skipping comments, blanks, duplicates and anything
 * that is not a legal rule. A missing file is the normal state of a box that
 * has opened nothing. */
static size_t open_ports_load(char out[][SYNNET_PORTRULE_MAX], size_t max) {
    FILE *f = fopen(synnet_fw_ports_path(), "r");
    if (!f) return 0;

    size_t n = 0;
    char line[256];
    while (n < max && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = strchr(line, '#');
        if (p) *p = '\0';
        p = line;
        while (*p == ' ' || *p == '\t') p++;
        size_t l = strlen(p);
        while (l && (p[l-1] == ' ' || p[l-1] == '\t')) p[--l] = '\0';
        if (!*p) continue;

        char pp[SYNNET_PORTRULE_MAX], sr[SYNNET_PORTRULE_MAX];
        char norm[SYNNET_PORTRULE_MAX];
        int got = sscanf(p, "%63s %63s", pp, sr);
        if (got < 1 ||
            synnet_port_rule_norm(pp, got == 2 ? sr : NULL,
                                  norm, sizeof(norm)) != 0) {
            syslog(LOG_WARNING, "synnet: ignoring '%s' in %s — not a legal "
                                "port rule", p, synnet_fw_ports_path());
            continue;
        }
        /* ⛔ A LINE WITH NO SOURCE IS REFUSED, NOT DEFAULTED. `tcp/5900` alone
         * reads as a note to oneself and would mean "open to the internet".
         * The CLI always writes the source, so a bare line is a hand edit that
         * has not said what it meant. */
        if (got != 2) {
            syslog(LOG_WARNING, "synnet: ignoring '%s' in %s — no source given; "
                                "write 'any' if that is what was meant",
                   p, synnet_fw_ports_path());
            continue;
        }
        int dup = 0;
        for (size_t i = 0; i < n; i++)
            if (strcmp(out[i], norm) == 0) { dup = 1; break; }
        if (dup) continue;

        memcpy(out[n++], norm, strlen(norm) + 1);
    }
    fclose(f);
    return n;
}

/* Add or remove one rule. Append-and-filter, the same shape (and for the same
 * reason) as synnet_trusted_iface_set: this is a `backup=` file that ships with
 * commented examples, and rewriting it from the parsed list would eat them. */
int synnet_open_port_set(const char *spec, int open) {
    char want[SYNNET_PORTRULE_MAX];
    {
        char pp[SYNNET_PORTRULE_MAX], sr[SYNNET_PORTRULE_MAX];
        int got = sscanf(spec ? spec : "", "%63s %63s", pp, sr);
        if (got < 1) return -2;
        if (synnet_port_rule_norm(pp, got == 2 ? sr : NULL,
                                  want, sizeof(want)) != 0) return -2;
    }

    const char *path = synnet_fw_ports_path();

    char rules[SYNNET_MAX_PORTS][SYNNET_PORTRULE_MAX];
    size_t n = open_ports_load(rules, SYNNET_MAX_PORTS);
    int present = 0;
    for (size_t i = 0; i < n; i++)
        if (strcmp(rules[i], want) == 0) { present = 1; break; }

    if (open) {
        if (present) return 0;                  /* idempotent */
        if (n >= SYNNET_MAX_PORTS) return -1;

        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) {
            *slash = '\0';
            if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
        }

        /* ⚠ A file whose last line has no newline would otherwise get the new
         * rule glued onto the end of it — the same trap the interface list
         * documents, and here it would produce a rule nobody wrote. */
        int need_nl = 0;
        FILE *r = fopen(path, "r");
        if (r) {
            if (fseek(r, -1, SEEK_END) == 0) {
                int c = fgetc(r);
                if (c != '\n' && c != EOF) need_nl = 1;
            }
            fclose(r);
        }
        FILE *f = fopen(path, "a");
        if (!f) return -1;
        if (need_nl) fputc('\n', f);
        fprintf(f, "%s\n", want);
        fclose(f);
        return 0;
    }

    if (!present) return 0;                     /* idempotent */

    /* Filter it out, keeping every comment and every other line byte for byte. */
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char tmp[PATH_MAX];
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.new", path) >= sizeof(tmp)) {
        fclose(f); return -1;
    }
    FILE *o = fopen(tmp, "w");
    if (!o) { fclose(f); return -1; }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char copy[256];
        snprintf(copy, sizeof(copy), "%s", line);
        copy[strcspn(copy, "\r\n")] = '\0';
        char *h = strchr(copy, '#');
        if (h) *h = '\0';
        char *p = copy;
        while (*p == ' ' || *p == '\t') p++;
        size_t l = strlen(p);
        while (l && (p[l-1] == ' ' || p[l-1] == '\t')) p[--l] = '\0';

        int drop = 0;
        if (*p) {
            char pp[SYNNET_PORTRULE_MAX], sr[SYNNET_PORTRULE_MAX];
            char norm[SYNNET_PORTRULE_MAX];
            if (sscanf(p, "%63s %63s", pp, sr) == 2 &&
                synnet_port_rule_norm(pp, sr, norm, sizeof(norm)) == 0 &&
                strcmp(norm, want) == 0)
                drop = 1;
        }
        if (!drop) fputs(line, o);
    }
    fclose(f);
    if (fclose(o) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* Add or remove one name.
 *
 * Append-and-filter rather than load-modify-rewrite, the same shape the
 * blocklist uses, and for a reason that matters more here: this is a pacman
 * `backup=` file that ships with commented examples in it and is meant to be
 * edited by hand. Rewriting it from the parsed list would silently eat every
 * comment the user (or the package) put there the first time anything touched
 * it through the CLI.
 */
int synnet_trusted_iface_set(const char *ifname, int trusted) {
    if (!ifname || !iface_name_valid(ifname)) return -2;

    const char *path = synnet_fw_ifaces_path();

    char ifaces[SYNNET_MAX_IFACES][SYNNET_IFNAME_MAX];
    size_t n = trusted_ifaces_load(ifaces, SYNNET_MAX_IFACES);
    int present = 0;
    for (size_t i = 0; i < n; i++)
        if (strcmp(ifaces[i], ifname) == 0) { present = 1; break; }

    if (trusted) {
        if (present) return 0;                  /* idempotent */
        if (n >= SYNNET_MAX_IFACES) return -1;

        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) {
            *slash = '\0';
            if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
        }

        /* ⚠ A file whose last line has no newline would otherwise get the new
         * name glued onto the end of it, turning `waydroid0` + `virbr0` into
         * one unparseable `waydroid0virbr0` that then fails validation and
         * takes the existing entry away with it. */
        int need_nl = 0;
        FILE *r = fopen(path, "rb");
        if (r) {
            if (fseek(r, -1, SEEK_END) == 0) {
                int last = fgetc(r);
                need_nl = (last != '\n' && last != EOF);
            }
            fclose(r);
        }

        FILE *f = fopen(path, "a");
        if (!f) return -1;
        fprintf(f, "%s%s\n", need_nl ? "\n" : "", ifname);
        if (fclose(f) != 0) return -1;
        return 0;
    }

    if (!present) return 0;                     /* nothing to take away */

    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        fclose(f);
        return -1;
    }
    FILE *o = fopen(tmp, "w");
    if (!o) { fclose(f); return -1; }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Compare the ENTRY, not the raw line: `  waydroid0   # android` is the
         * same entry as `waydroid0`, and an --untrust-if that only matched the
         * bare form would report success and leave the rule in place. */
        char work[256];
        snprintf(work, sizeof(work), "%s", line);
        work[strcspn(work, "\r\n")] = '\0';
        char *h = strchr(work, '#');
        if (h) *h = '\0';
        char *p = work;
        while (*p == ' ' || *p == '\t') p++;
        size_t l = strlen(p);
        while (l && (p[l-1] == ' ' || p[l-1] == '\t')) p[--l] = '\0';

        if (strcmp(p, ifname) == 0) continue;
        fputs(line, o);
    }
    fclose(f);
    if (fclose(o) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* Take the input chain away, and nothing else.
 *
 * ⚠ NOT `delete table` and emphatically not `flush ruleset`. The egress
 * `blocked` set lives in the same table and holds every address the AI has
 * flagged; turning off the INPUT firewall must not silently unblock all of
 * them. Deleting a chain that is not there is not an error worth reporting —
 * "off" is a state, not an operation that has to have found something to do.
 */
int synnet_nft_drop_firewall(void) {
    run_nft("nft delete chain inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT
            " 2>/dev/null");
    firewall_publish_state("off");
    return 0;
}

static void firewall_publish(int active) {
    firewall_publish_state(active ? "active" : "failed");
}

static void firewall_publish_state(const char *state) {
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
            "links=%u\n"
            /* ⚠ ADDITIVE, and it has to be: syn-settings reads `state`, `links`
             * and `reasserts` out of here by name, so a new key costs nothing
             * and a renamed one costs the network pane. */
            "ports=%u\n"
            "since=%lld\n"
            "reasserts=%lu\n",
            state, g_fw_ifaces, g_fw_ports, (long long)time(NULL),
            g_fw_reasserts);
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
    static const char *base =
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
        "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT " udp dport { 68, 546 } accept\n";

    char script[32768];
    int off = snprintf(script, sizeof(script), "%s", base);
    if (off < 0 || (size_t)off >= sizeof(script)) {
        syslog(LOG_ERR, "synnet: base firewall script does not fit — not applied");
        firewall_publish(0);
        return -1;
    }

    /* ── the container/VM links we are the gateway for ──────
     *
     * The mirror image of the DHCP rule above: that one is this box acting as a
     * DHCP CLIENT, and these are it acting as the SERVER for a bridge it owns.
     * A guest's first packet comes from 0.0.0.0 — no address yet, so the
     * LAN-trust rule cannot see it — and every packet after the lease is
     * already covered by that rule, which is why this stops at the gateway
     * services instead of trusting the interface wholesale. See
     * SYNNET_FW_IFACES.
     *
     * ⚠ `iifname`, never `iif`: these bridges appear when the container starts,
     * long after this chain was loaded at boot, and `iif` on an interface that
     * does not exist yet fails the atomic load — losing the whole firewall to
     * fix one link. */
    char ifaces[SYNNET_MAX_IFACES][SYNNET_IFNAME_MAX];
    size_t n = trusted_ifaces_load(ifaces, SYNNET_MAX_IFACES);
    size_t applied = 0;

    for (size_t i = 0; i < n; i++) {
        int w = snprintf(script + off, sizeof(script) - (size_t)off,
            "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT
            " iifname \"%s\" udp dport { 53, 67, 547 } accept "
            "comment \"synnet-gw\"\n"
            "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT
            " iifname \"%s\" tcp dport { 53, 67 } accept "
            "comment \"synnet-gw\"\n",
            ifaces[i], ifaces[i]);
        /* ⚠ Stop, do not truncate. A clipped rule is a syntax error that fails
         * the load and unfilters the box; dropping the tail of the list leaves
         * a working firewall missing one link, which is recoverable and says so
         * in the journal. */
        if (w < 0 || (size_t)w >= sizeof(script) - (size_t)off) {
            script[off] = '\0';
            syslog(LOG_WARNING, "synnet: firewall script full at %zu of %zu "
                                "trusted link(s) — '%s' and after are NOT "
                                "applied", applied, n, ifaces[i]);
            break;
        }
        off += w;
        applied++;
    }

    /* ── the ports opened to a source the base chain drops ──
     *
     * ⛔ THE ONLY RULE HERE THAT WIDENS INGRESS, which is why it is the only one
     * a person has to write down on purpose. Everything above accepts a source
     * that is already trusted (loopback, established, private ranges); these
     * accept a port from somewhere that is not.
     *
     * ⚠ Appended LAST, and that is safe because the chain has no drop RULE —
     * it has a drop POLICY, which is only reached when no rule has matched. So
     * order among the accepts cannot change the verdict, and putting these at
     * the end keeps the base set readable in `nft list chain`.
     *
     * Every value here has been through synnet_port_rule_norm(), so `proto` is
     * tcp or udp, `port` is 1-65535 and `src` is a CIDR of a known family or
     * `any`. Nothing else can reach the script. */
    char prules[SYNNET_MAX_PORTS][SYNNET_PORTRULE_MAX];
    size_t pn = open_ports_load(prules, SYNNET_MAX_PORTS);
    size_t papplied = 0;

    for (size_t i = 0; i < pn; i++) {
        char proto[8], src[SYNNET_PORTRULE_MAX];
        long port; int fam;
        if (port_rule_split(prules[i], proto, sizeof(proto), &port,
                            src, sizeof(src), &fam) != 0)
            continue;   /* load already validated; belt and braces */

        char match[128];
        if (fam == AF_INET)
            snprintf(match, sizeof(match), "ip saddr %s ", src);
        else if (fam == AF_INET6)
            snprintf(match, sizeof(match), "ip6 saddr %s ", src);
        else
            match[0] = '\0';    /* any */

        int w = snprintf(script + off, sizeof(script) - (size_t)off,
            "add rule inet " SYNNET_NFT_TABLE " " SYNNET_NFT_INPUT
            " %s%s dport %ld accept comment \"synnet-open\"\n",
            match, proto, port);
        /* ⚠ Stop, do not truncate — the same rule the interface loop follows.
         * A clipped rule is a syntax error that fails the load and unfilters
         * the box; dropping the tail leaves a working firewall missing one
         * opening, which is recoverable and says so in the journal. */
        if (w < 0 || (size_t)w >= sizeof(script) - (size_t)off) {
            script[off] = '\0';
            syslog(LOG_WARNING, "synnet: firewall script full at %zu of %zu "
                                "open port(s) — '%s' and after are NOT applied",
                   papplied, pn, prules[i]);
            break;
        }
        off += w;
        papplied++;
    }

    if (off + 12 >= (int)sizeof(script)) {   /* "SYNNET_FW\n" and slack */
        syslog(LOG_ERR, "synnet: no room to close the firewall script — not applied");
        firewall_publish(0);
        return -1;
    }
    snprintf(script + off, sizeof(script) - (size_t)off, "SYNNET_FW\n");

    int rc = run_nft("%s", script);
    /* Published even on failure: the count describes what this apply TRIED, and
     * a state file saying "failed, 1 link" is a better report than one that
     * quietly keeps the previous run's number. */
    g_fw_ifaces = (unsigned)applied;
    g_fw_ports  = (unsigned)papplied;
    firewall_publish(rc == 0);
    if (rc == 0 && applied)
        syslog(LOG_INFO, "synnet: %zu container/VM link(s) trusted for DHCP+DNS",
               applied);
    /* ⛔ NAMED IN THE JOURNAL, EVERY ONE. These are the only rules that let a
     * source the base chain would drop reach this machine, so "what is open"
     * must be answerable from the log alone, months later, by somebody who did
     * not open it. */
    for (size_t i = 0; rc == 0 && i < papplied; i++)
        syslog(LOG_NOTICE, "synnet: open %s", prules[i]);
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
        if (!synnet_firewall_enabled()) {
            /* Somebody turned it off and meant it. Say so at WARNING and every
             * start: an unfiltered box is worth a line in the journal each
             * boot, and this is the one setting here that makes the machine
             * less safe than the default. */
            syslog(LOG_WARNING, "synnet: input firewall is OFF by preference "
                                "(%s) — this box is not ingress-filtered",
                   synnet_fw_pref_path());
            synnet_nft_drop_firewall();
        } else if (synnet_nft_ensure_firewall() != 0) {
            syslog(LOG_WARNING, "synnet: input firewall setup failed — "
                                "the box is not ingress-filtered");
        } else {
            syslog(LOG_INFO, "synnet: base input firewall active "
                             "(LAN-trust, default-drop)");
        }
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
        /* ⚠ THE PREFERENCE IS RE-READ HERE, not cached at start. That is what
         * makes the settings pane's off switch work at all: it writes the file
         * and tears the chain down, and without this the very next tick would
         * put the chain back — the re-assert would be undoing the user rather
         * than the flush it exists for. */
        if (tick % 6 == 0 && !s->dry_run && !synnet_firewall_enabled()) {
            /* Nothing to do, and nothing to say: an off firewall staying off is
             * not news once a minute. The boot line above already said it. */
        } else if (tick % 6 == 0 && !s->dry_run && !synnet_firewall_present()) {
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
        fprintf(stderr, _("synnet: invalid IPv4 address: %s\n"), ip);
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
    printf(_("synnet %s — nftables enforcement status\n\n"), SYNNET_VERSION);

    /* ── 1. what synnet says it did ─────────────────────────── */
    const char *st = fw_state_get("state");
    fputs(_("  Input firewall\n"), stdout);
    if (!synnet_firewall_enabled()) {
        /* Checked BEFORE the published state, because "off" is a decision and
         * every other line here describes a machine that is trying to be
         * filtered. Reporting "not asserted" for a firewall somebody switched
         * off would send them looking for a fault. */
        printf(_("    OFF — switched off in %s.\n"), synnet_fw_pref_path());
        fputs(_("    Nothing inbound is filtered. Turn it back on with\n"
                "    `sudo synnet --firewall on`, or in Settings ▸ Network.\n"),
              stdout);
    } else if (st[0] == '\0') {
        printf(_("    not asserted — no %s.\n"), synnet_fw_state_path());
        fputs(_("    The daemon publishes that file at start; if synnet is\n"
                "    running and it is missing, the daemon predates this or\n"
                "    could not write to /run/synnet.\n"), stdout);
    } else if (strcmp(st, "active") == 0) {
        const char *n = fw_state_get("reasserts");
        fputs(_("    ACTIVE — default-drop input, LAN and established traffic\n"
                "    trusted. Asserted by synnet at boot and re-checked every "
                "minute.\n"), stdout);
        if (n[0] && strcmp(n, "0") != 0)
            printf(_("    ⚠ rebuilt %s time(s) this run — something on this box\n"
                     "      is flushing nftables.\n"), n);
    } else {
        fputs(_("    FAILED to apply — this box is NOT ingress-filtered.\n"
                "    See `journalctl -u synnet` for what nft said.\n"), stdout);
    }
    printf("\n");

    /* ── 1b. the links we serve DHCP and DNS on ─────────────
     *
     * Read from /etc/synnet/trusted-ifaces, not from the published state, so
     * this answers even before the daemon has applied anything — and so a name
     * that was added but never applied is visible as exactly that, rather than
     * as a firewall that mysteriously still drops the container's DHCP. */
    {
        char ifaces[SYNNET_MAX_IFACES][SYNNET_IFNAME_MAX];
        size_t n = trusted_ifaces_load(ifaces, SYNNET_MAX_IFACES);
        fputs(_("  Container / VM links (DHCP + DNS accepted on these)\n"), stdout);
        if (n == 0) {
            printf(_("    none — %s is empty or absent.\n"), synnet_fw_ifaces_path());
            fputs(_("    A container bridge needs one: its guest's first DHCP\n"
                    "    packet comes from 0.0.0.0 and the drop policy eats it.\n"
                    "    `sudo synnet --trust-if waydroid0` adds one.\n"), stdout);
        } else {
            const char *live = fw_state_get("links");
            for (size_t i = 0; i < n; i++) {
                char sys[PATH_MAX];
                snprintf(sys, sizeof(sys), "/sys/class/net/%s", ifaces[i]);
                /* Absent is normal, not a fault: the rule is matched by name
                 * and starts working the moment the container brings the
                 * bridge up. Said out loud so nobody goes looking for a typo. */
                /* ⛔ TWO WHOLE LINES. A parenthetical in a %s is a fragment no
                 * language can place, and "(up)" beside a German sentence is
                 * the half nobody translated. */
                if (access(sys, F_OK) == 0)
                    printf(_("    %-15s (up)\n"), ifaces[i]);
                else
                    printf(_("    %-15s (not present yet — matched by name)\n"),
                           ifaces[i]);
            }
            /* Only when the firewall is actually up and asserted: an "off" or
             * never-applied firewall not matching this list is not news, it is
             * the definition of those states. */
            if (strcmp(st, "active") == 0 && live[0] && atoi(live) != (int)n)
                printf(_("    ⚠ synnet last applied %s of these. Run\n"
                         "      `sudo synnet --firewall` to load the current "
                         "list.\n"), live);
        }
        printf("\n");
    }

    /* ── 2. what the kernel actually holds ──────────────────── */
    fputs(_("  Live ruleset\n"), stdout);
    fflush(stdout);
    if (geteuid() != 0) {
        /* Said BEFORE trying, not after failing. This is the exact spot the old
         * version turned a permission error into a factual claim about the
         * firewall. */
        fputs(_("    (not shown — listing nftables needs root: "
                "sudo synnet --status)\n\n"), stdout);
    } else if (system("command -v nft >/dev/null 2>&1") != 0) {
        /* ⚠ THE SAME LIE ONE LEVEL DOWN. `nft` failing does not mean the chain
         * is absent — it also means nft is not installed, or is not on this
         * process's PATH. Claiming "NOT loaded in the kernel" from that is the
         * exact reasoning error this whole function was rewritten to remove,
         * and it is the one a container hits first. */
        fputs(_("    (not shown — nft is not installed)\n\n"), stdout);
    } else {
        int r = run_nft("nft list chain inet " SYNNET_NFT_TABLE " "
                        SYNNET_NFT_INPUT " 2>/dev/null");
        if (r != 0)
            fputs(_("    the input chain is NOT loaded in the kernel.\n\n"), stdout);
        else
            printf("\n");
    }

    /* ── 1c. the ports opened to a source we would drop ─────
     *
     * ⛔ THE ONLY THING HERE THAT LETS THE OUTSIDE IN, so it is printed even
     * when the list is empty — an empty section says "nothing is open", and a
     * section that vanishes says nothing at all. Read from the file rather
     * than the published state for the same reason the links are: a rule that
     * was added but never applied has to be visible AS that.
     */
    {
        char prules[SYNNET_MAX_PORTS][SYNNET_PORTRULE_MAX];
        size_t n = open_ports_load(prules, SYNNET_MAX_PORTS);
        fputs(_("  Ports opened to sources this box would otherwise drop\n"),
              stdout);
        if (n == 0) {
            fputs(_("    none — only loopback, replies, ICMP and private-range\n"
                    "    sources reach this machine.\n"), stdout);
        } else {
            for (size_t i = 0; i < n; i++) {
                char proto[8], src[SYNNET_PORTRULE_MAX];
                long port; int fam;
                if (port_rule_split(prules[i], proto, sizeof(proto), &port,
                                    src, sizeof(src), &fam) != 0)
                    continue;
                /* ⛔ TWO WHOLE SENTENCES, not one with a word swapped in.
                 * "any" is the difference between a port open to one VPN and a
                 * port open to the internet, and that is not a distinction to
                 * carry in a %s beside a translated line. */
                if (fam == AF_UNSPEC)
                    printf(_("    %s/%ld  from ANYWHERE, including the internet\n"),
                           proto, port);
                else
                    printf(_("    %s/%ld  from %s\n"), proto, port, src);
            }
            const char *livep = fw_state_get("ports");
            if (strcmp(st, "active") == 0 && livep[0] && atoi(livep) != (int)n)
                printf(_("    ⚠ synnet last applied %s of these. Run\n"
                         "      `sudo synnet --firewall` to load the current "
                         "list.\n"), livep);
        }
        printf("\n");
    }

    /* ── 3. the egress blocklist ────────────────────────────── */
    fputs(_("  Blocked destinations (egress)\n"), stdout);
    fflush(stdout);
    if (geteuid() != 0) {
        fputs(_("    (not shown — needs root)\n"), stdout);
        return 0;
    }
    int r = run_nft("nft list set inet " SYNNET_NFT_TABLE " " SYNNET_NFT_SET
                    " 2>/dev/null");
    if (r != 0)
        fputs(_("    none — the synnet table is not loaded.\n"), stdout);
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
