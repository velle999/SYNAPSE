#pragma once
#include <stdint.h>
#include <sys/types.h>

#define SYNNET_VERSION     "0.1.0-synapse"
#define SYNNET_SOCKET      "/run/synapd/synapd.sock"
#define SYNNET_STATE_FILE  "/var/lib/synnet/state.json"
#define SYNNET_NFT_CONF    "/etc/nftables.d/synnet.nft"

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
    uint64_t events_seen;
    uint64_t events_blocked;
    uint64_t ai_queries;
} synnet_state_t;

/* core API */
int  synnet_init(synnet_state_t *s);
void synnet_run(synnet_state_t *s);
void synnet_shutdown(synnet_state_t *s);
int  synnet_apply_rule(const char *ip, synnet_action_t action);
int  synnet_query_ai(synnet_state_t *s, synnet_event_t *ev, char *out, size_t outlen);
