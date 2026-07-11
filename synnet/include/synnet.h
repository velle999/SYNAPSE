#pragma once
#include <stdint.h>
#include <sys/types.h>

#define SYNNET_VERSION     "0.1.0-synapse"
#define SYNNET_SOCKET      "/run/synapd/synapd.sock"
/* Overridable at build time (packaging / tests) via -DSYNNET_STATE_FILE=... */
#ifndef SYNNET_STATE_FILE
#define SYNNET_STATE_FILE  "/var/lib/synnet/blocklist"   /* one IPv4 per line */
#endif
#define SYNNET_NFT_CONF    "/etc/nftables.d/synnet.nft"

/* nftables objects synnet manages (inet family). Enforcement is a single
 * rule "ip daddr @blocked drop" in an output-hook chain, so blocking an IP
 * is just adding it to the named set — idempotent and the correct direction
 * for outbound connections we flag. */
#define SYNNET_NFT_TABLE   "synnet"
#define SYNNET_NFT_SET     "blocked"
#define SYNNET_NFT_CHAIN   "output"

/* Base input firewall (synnet_nft_ensure_firewall). A default-drop input chain
 * that trusts loopback, established/related replies, ICMP, and private-range
 * (RFC1918 / IPv6 ULA + link-local) sources — so LAN services stay reachable
 * and dynamic-port apps (Plex, Steam) are not a maintenance burden — while the
 * box answers nothing unsolicited from a public network it roams onto. Lives in
 * the same `inet synnet` table as the egress set, in its own base chain. */
#define SYNNET_NFT_INPUT   "input"

typedef enum {
    SYNNET_ACTION_ALLOW  = 0,
    SYNNET_ACTION_BLOCK  = 1,
    SYNNET_ACTION_MONITOR = 2,
    SYNNET_ACTION_ASK    = 3,  /* ask synapd */
} synnet_action_t;

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  proto;     /* IPPROTO_TCP, IPPROTO_UDP */
    pid_t    pid;
    char     comm[16];
} synnet_event_t;

typedef struct {
    int      netlink_fd;
    int      synapd_fd;
    int      running;
    int      dry_run;          /* monitor + log only; never touch nftables */
    uint64_t events_seen;
    uint64_t events_blocked;
    uint64_t ai_queries;

    /* Destination IPs (network byte order) already handled this run, so a
     * still-open connection isn't re-queried/re-blocked on every poll. */
    uint32_t *seen_dst;
    size_t    seen_count;
    size_t    seen_cap;
} synnet_state_t;

/* core API */
int  synnet_init(synnet_state_t *s);
void synnet_run(synnet_state_t *s);
void synnet_shutdown(synnet_state_t *s);
int  synnet_apply_rule(const char *ip, synnet_action_t action);
int  synnet_query_ai(synnet_state_t *s, synnet_event_t *ev, char *out, size_t outlen);

/* nftables enforcement (monitor.c) */
int  synnet_nft_ensure(void);                 /* idempotent table/set/chain/rule */
int  synnet_nft_ensure_firewall(void);        /* base input firewall (atomic, idempotent) */
int  synnet_status(void);                      /* print nft set + ruleset, for --status */
