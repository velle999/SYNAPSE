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

/* Where the daemon publishes what it actually asserted, for `--status` to read.
 *
 * ⚠ THIS EXISTS BECAUSE THE FIREWALL WAS INVISIBLE. Reading the live ruleset
 * needs CAP_NET_ADMIN, so `nft list` as an ordinary user fails — and
 * synnet_status() used to take that failure as "no table" and print
 * "daemon has not run". The box was fully firewalled and its own status
 * command said it was not, which is a worse answer than saying nothing.
 *
 * The daemon writes this after every assert, so an unprivileged `--status` can
 * report the firewall from the horse's mouth without needing to look at the
 * kernel. The live ruleset is still consulted when we CAN look; the two are
 * reported separately, because "what synnet believes" and "what the kernel
 * holds" are different claims and conflating them is how the last bug happened.
 *
 * /run, so it cannot outlive a boot and claim a firewall that a reboot removed.
 * The unit's RuntimeDirectory= creates the directory. */
#define SYNNET_FW_STATE    "/run/synnet/firewall.state"
/* …and where to look for it, honouring $SYNNET_FW_STATE_FILE.
 *
 * A RUNTIME override rather than the build-time one SYNNET_STATE_FILE uses,
 * because this file is what `--status` reports and a test that has to rebuild
 * the daemon to check a status string is a test nobody runs. It redirects
 * where the STATUS is written and read, never what the firewall does — and
 * anyone able to set this daemon's environment already controls its startup. */
const char *synnet_fw_state_path(void);

/* The user's PREFERENCE, which is a different thing from the state above:
 * `/etc/synnet/firewall` holds "on" or "off", and off means the daemon does not
 * assert the input chain and does not rebuild it in the re-assert tick.
 *
 * ⚠ WITHOUT A PERSISTED PREFERENCE, "OFF" LASTS AT MOST A MINUTE. The daemon
 * re-checks the chain once a minute and puts it back, so a switch that only
 * deleted the chain would be undone by the thing that exists to undo exactly
 * that. Anything offering an off switch — the settings pane does — has to write
 * here, not just tear the chain down.
 *
 * Absent means ON. A box that has never expressed a preference is filtered;
 * that is the whole point of shipping a firewall, and a missing file must never
 * be the thing that disarms it. */
#define SYNNET_FW_PREF     "/etc/synnet/firewall"
const char *synnet_fw_pref_path(void);   /* honours $SYNNET_FW_PREF_FILE */
int  synnet_firewall_enabled(void);      /* 1 unless the preference says off */
int  synnet_firewall_set_enabled(int on);/* write the preference; 0 on success */
/* Tear the input chain down. Leaves the table and the egress `blocked` set
 * alone — those are a different policy and dropping them with this would
 * silently unblock every IP the AI has flagged. */
int  synnet_nft_drop_firewall(void);

/* ── Container / VM links this box is the GATEWAY for ────────
 *
 * `/etc/synnet/trusted-ifaces`, one interface name per line, `#` comments and
 * blank lines ignored. For each one the input chain gains DHCP-server and
 * DNS-server accepts.
 *
 * ⚠ THIS EXISTS FOR THE PACKETS THAT ARRIVE BEFORE THE PEER HAS AN ADDRESS.
 * Waydroid, libvirt and the rest hang a bridge off the host, run dnsmasq on it,
 * and hand the guest an RFC1918 lease. Everything the guest sends AFTER that is
 * already accepted — its source is 192.168.x, which the LAN-trust rule takes.
 * The one packet that is not is the first one: a DHCPDISCOVER is sent from
 * 0.0.0.0 to 255.255.255.255:67, matching no accept in the chain, so the
 * default-drop policy eats it and the guest never gets a lease at all. The
 * symptom is "the container has no network", and nothing in the log says
 * firewall.
 *
 * ⚠ AND IT IS NOT `allow in on <iface>`. ufw's advice for these bridges is to
 * trust the interface wholesale; doing that here would widen the trust boundary
 * to every port on the host for whatever the guest is running — an arbitrary
 * Android APK, in Waydroid's case — and would fix nothing extra, because
 * everything an addressed guest sends is already accepted by the LAN-trust
 * rule. The gateway services are the entire delta, so they are the entire rule.
 *
 * ⚠ MATCHED BY NAME (`iifname`), NEVER BY INDEX (`iif`). `iif` resolves the
 * interface to an ifindex when the rule is LOADED, and fails if it does not
 * exist — and these bridges are created when the container starts, hours after
 * the firewall came up at boot. Since the whole chain is one atomic `nft -f`
 * load, one `iif` naming an absent interface does not lose that rule, it loses
 * THE FIREWALL. `iifname` is a string match and is happy to name something that
 * does not exist yet. */
#define SYNNET_FW_IFACES   "/etc/synnet/trusted-ifaces"
#define SYNNET_IFNAME_MAX  16    /* IFNAMSIZ, without dragging in <net/if.h> */
#define SYNNET_MAX_IFACES  32    /* far past plausible; bounds the nft script */
const char *synnet_fw_ifaces_path(void); /* honours $SYNNET_FW_IFACES_FILE */

/* Add or remove one interface. 0 on success, -1 if the file could not be
 * written, -2 if the name is not a legal interface name.
 *
 * ⚠ THE CALLER HAS TO RE-APPLY THE FIREWALL. The daemon's re-assert tick only
 * rebuilds a chain that has GONE; a chain that is present but out of date looks
 * healthy to it, so editing this file changes nothing in the kernel until
 * something calls synnet_nft_ensure_firewall(). `--trust-if` does. */
int  synnet_trusted_iface_set(const char *ifname, int trusted);

/*
 * ── Ports opened to a source that is not on the LAN ──────────────────────────
 *
 * ⛔ THE ONE THING `--allow` DOES NOT DO. `synnet --allow <ip>` reads like it
 * opens the firewall and does not: it removes an address from the DROP SET,
 * which only ever undoes a previous `--block`. The base chain trusts RFC1918
 * sources and drops everything else, and until this there was NO supported way
 * to let a non-private source reach a port — so a VPN on 100.64.0.0/10
 * (Tailscale and friends) established outbound and then had every packet
 * inside the tunnel eaten by the input policy, with nothing anywhere saying so.
 *
 * A rule is `<proto>/<port> <source>`, e.g. `tcp/5900 100.64.0.0/10`, and the
 * source is a CIDR or the word `any`.
 *
 * ⚠ THE SOURCE IS NEVER OPTIONAL IN THE STORED FORM. A line that omitted it
 * would read as "closed to everyone but me" and mean "open to the internet",
 * and the two are one missing word apart. The CLI writes `any` in full rather
 * than leaving the field empty, so the file always states what it did.
 */
#define SYNNET_FW_PORTS     "/etc/synnet/open-ports"
#define SYNNET_PORTRULE_MAX 64   /* "tcp/65535 " + an IPv6 CIDR, with room */
#define SYNNET_MAX_PORTS    32   /* far past plausible; bounds the nft script */
const char *synnet_fw_ports_path(void);  /* honours $SYNNET_FW_PORTS_FILE */

/* Add or remove one rule. 0 on success, -1 if the file could not be written,
 * -2 if the spec is not a legal rule.
 *
 * ⚠ THE CALLER HAS TO RE-APPLY THE FIREWALL, for the same reason --trust-if
 * does: the daemon's re-assert tick rebuilds a chain that has GONE, and one
 * that is merely out of date looks healthy to it.
 */
int  synnet_open_port_set(const char *spec, int open);

/* Normalise a rule to its stored form. Returns 0 and fills `out` (at least
 * SYNNET_PORTRULE_MAX bytes) or -1 if the spec is not legal. `src` may be NULL
 * or empty, which means `any`. */
int  synnet_port_rule_norm(const char *proto_port, const char *src,
                           char *out, size_t outsz);

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
/* Is our input base chain still in the kernel? Root-only (it lists a chain);
 * answers 0 for "no, or cannot tell". Used by the re-assert tick, which treats
 * both the same way — rebuilding a chain that is already correct is harmless
 * and rebuilding one that vanished is the point. */
int  synnet_firewall_present(void);
int  synnet_status(void);                      /* print nft set + ruleset, for --status */
