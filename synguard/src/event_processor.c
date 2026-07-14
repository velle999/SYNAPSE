/*
 * event_processor.c — Event reading and processing pipeline
 *
 * Reads the syscall event stream from synapse_kmod via
 * /sys/kernel/synapse/syscall_log and runs each event through:
 *
 *   1. Baseline anomaly check (fast, in-memory)
 *   2. Rule engine evaluation  (fast, O(rules) matching)
 *   3. AI classification       (slow, only on ESCALATE)
 *   4. Action dispatch         (log / alert / deny / quarantine)
 *
 * The reader runs in a dedicated thread, polling at a configurable
 * interval. Events are parsed from the text format emitted by the
 * kmod ring buffer.
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
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#include "synguard.h"
#include "sg_log.h"

/* ── kmod event log format ────────────────────────────────── */
/*
 * Each line from /sys/kernel/synapse/syscall_log:
 *   "<timestamp_ns> <pid> <uid> <syscall_nr> <comm> <filename>\n"
 *
 * filename may be "-" if not applicable.
 */

/* Syscall nr → event type mapping */
static evt_type_t syscall_to_evt(uint32_t nr)
{
    /* Linux x86_64 syscall numbers */
    switch (nr) {
    case 59:  /* execve    */
    case 322: /* execveat  */  return EVT_EXEC;
    case 257: /* openat    */
    case 2:   /* open      */  return EVT_OPEN;
    case 41:  /* socket    */
    case 42:  /* connect   */  return EVT_SOCKET;
    case 101: /* ptrace    */  return EVT_PTRACE;
    case 175: /* init_module */
    case 313: /* finit_module*/ return EVT_MODULE;
    case 165: /* mount     */  return EVT_MOUNT;
    case 105: /* setuid    */
    case 117: /* setresuid */
    case 126: /* setgroups */  return EVT_SETUID;
    default:                   return EVT_UNKNOWN;
    }
}

int kmod_parse_event(const char *line, sg_event_t *out)
{
    /* comm and filename arrive ESCAPED (\xHH for whitespace/backslash), so each
     * is one token no matter what it contains — see synguard.h. The buffers are
     * sized for a fully-escaped field (4x) so a hostile comm can't be truncated
     * mid-escape. */
    char comm_esc[SG_ESC_MAX_COMM] = {0};
    char fname_esc[SG_ESC_MAX_FILENAME] = {0};
    unsigned int wire_flags = 0;
    unsigned long long arg0 = 0;
    memset(out, 0, sizeof(*out));

    /* The kmod appends "flags arg0" after the filename (newer log format);
     * older kmods stop at the filename, so those fields are optional. */
    int n = sscanf(line, "%llu %u %u %u %79s %639s %x %llu",
                   (unsigned long long *)&out->timestamp_ns,
                   &out->pid, &out->uid, &out->syscall_nr,
                   comm_esc, fname_esc, &wire_flags, &arg0);

    if (n < 5) return -1;

    sg_str_unescape(out->comm, sizeof(out->comm), comm_esc);

    if (n >= 6 && strcmp(fname_esc, "-") != 0)
        sg_str_unescape(out->filename, sizeof(out->filename), fname_esc);

    if (n >= 8) {
        out->evt_type = (uint8_t)wire_flags;
        out->arg0     = arg0;
        out->has_arg0 = 1;
    } else {
        out->evt_type = syscall_to_evt(out->syscall_nr);
    }
    return 0;
}

/* ── Verdict name (for logging) ───────────────────────────── */
static const char *verdict_name(sg_verdict_t v)
{
    switch (v) {
    case VERDICT_ALLOW:      return "ALLOW";
    case VERDICT_LOG:        return "LOG";
    case VERDICT_ALERT:      return "ALERT";
    case VERDICT_ESCALATE:   return "ESCALATE";
    case VERDICT_DENY:       return "DENY";
    case VERDICT_QUARANTINE: return "QUARANTINE";
    default:                 return "UNKNOWN";
    }
}

/* ── Build AI context string for an event ─────────────────── */
static void build_ai_context(const sg_event_t *e, char *out, size_t out_len)
{
    static const char *evt_names[] = {
        [0]          = "unknown",
        [EVT_EXEC]   = "execve",
        [EVT_OPEN]   = "open_sensitive_file",
        [EVT_SOCKET] = "create_socket",
        [EVT_PTRACE] = "ptrace_attach",
        [EVT_MODULE] = "load_kernel_module",
        [EVT_MOUNT]  = "mount_filesystem",
        [EVT_SETUID] = "setuid_change",
    };

    const char *ename = (e->evt_type < 0x80 && evt_names[e->evt_type])
                        ? evt_names[e->evt_type] : "unknown";

    /* Describe setuid by its target, not a fixed label: telling the model
     * "setuid_to_root" for a root→user privilege drop poisons the verdict. */
    if (e->evt_type == EVT_SETUID && e->has_arg0)
        ename = (e->arg0 == 0) ? "setuid_to_root"
                               : "setuid_drop_to_unprivileged_uid";

    snprintf(out, out_len,
        "syscall_event: %s\n"
        "process: %s (pid=%u uid=%u)\n"
        "%s%s%s"
        "timestamp: %llu ns",
        ename,
        e->comm, e->pid, e->uid,
        e->filename[0] ? "file: "     : "",
        e->filename[0] ? e->filename  : "",
        e->filename[0] ? "\n"         : "",
        (unsigned long long)e->timestamp_ns
    );
}

/* ── Active canary: detect probe blinding ─────────────────────
 *
 * The in-kernel self-integrity check can see a targeted disable_kprobe() but
 * NOT the global `echo 0 > /sys/kernel/debug/kprobes/enabled` switch, which
 * disarms every kprobe without setting per-probe flags (the flag it does set,
 * kprobes_all_disarmed, is not exported to modules).
 *
 * We close that gap from userspace: synguard periodically open()s a sentinel
 * path that IS on the kmod's sensitive list, then confirms the resulting event
 * comes back through the syscall feed. If our own opens stop showing up while
 * event capture is supposed to be on, the probes are disarmed — however it
 * happened — and we raise a CRITICAL alert.
 */
#define CANARY_PATH        "/sys/kernel/synapse/version"  /* matches "/sys/kernel/" */
#define CANARY_INTERVAL_S  20
#define CANARY_GRACE_S      6      /* must return within this after firing */
#define CANARY_MISS_ALERT   2      /* consecutive misses before alerting */

static time_t canary_last_fire = 0;
static time_t canary_fired_at  = 0;
static int    canary_pending   = 0;
static int    canary_misses    = 0;

/* True if the kmod says event capture is currently enabled. If an admin
 * deliberately turned it off (config events_enabled=0), a missing canary is
 * expected and already logged loudly by the kmod — don't cry wolf. */
static int kmod_events_enabled(void)
{
    int fd = open("/sys/kernel/synapse/config", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 1;   /* can't tell → assume on */
    char buf[128] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 1;
    return strstr(buf, "events_enabled=0") ? 0 : 1;
}

/* Recognise our own canary event coming back through the feed. */
static int is_canary_event(const sg_event_t *e)
{
    return e->evt_type == EVT_OPEN &&
           strcmp(e->comm, "synguard") == 0 &&
           strstr(e->filename, "synapse/version") != NULL;
}

/* Called each reader iteration: fire a new canary on schedule and alert if a
 * previously fired one never came back. */
static void canary_tick(synguard_state_t *s)
{
    if (!s->kmod_present) return;

    time_t now = time(NULL);

    /* A pending canary that blew its grace window is a miss. */
    if (canary_pending && (now - canary_fired_at) > CANARY_GRACE_S) {
        canary_pending = 0;
        canary_misses++;
        sg_log(LOG_WARNING,
               "canary: kmod did not report our sentinel open (miss %d)",
               canary_misses);
        if (canary_misses == CANARY_MISS_ALERT) {
            sg_alert_t a = {
                .timestamp = now,
                .verdict   = VERDICT_ALERT,
                .threat    = THREAT_CRITICAL,
            };
            strncpy(a.event.comm, "synapse_kmod", sizeof(a.event.comm) - 1);
            a.event.evt_type = EVT_UNKNOWN;
            snprintf(a.reason, sizeof(a.reason),
                     "canary: syscall probes are not reporting — monitoring "
                     "may be blinded (kprobes disabled / tampered)");
            snprintf(a.action_taken, sizeof(a.action_taken), "alert");
            s->stats.alerts++;
            action_alert(s, &a);
            if (s->config.audit_enabled)
                audit_write(s, &a);
        }
    }

    /* Fire a fresh canary on schedule (only while capture should be on). */
    if (!canary_pending && (now - canary_last_fire) >= CANARY_INTERVAL_S) {
        if (!kmod_events_enabled()) { canary_last_fire = now; return; }
        int fd = open(CANARY_PATH, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) close(fd);
        canary_last_fire = now;
        canary_fired_at  = now;
        canary_pending   = 1;
    }
}

/* ── Worm / C2 egress detector (netwatch) ─────────────────────
 *
 * The kmod reports connect() with the destination ("A.B.C.D:port" or
 * "[v6]:port") in filename, syscall_nr 42. Per process, over a short sliding
 * window, we look for the shapes that actually separate a worm from a busy
 * browser:
 *
 *   sweep   many distinct hosts inside ONE /24 — lateral movement and worm
 *           spreading walk a subnet host by host; a browser never does.
 *   scan    many distinct hosts on non-web ports — a scanner hunting 22/445/
 *           3389 fans out wide; a browser only ever speaks 80/443/8080/8443.
 *   flood   a large number of connects to one or two hosts — a retry storm, or
 *           a beacon hammering its C2.
 *
 * Raw counts are NOT signatures, which is the trap the first version fell into:
 * "20 distinct hosts OR 80 connects in 10s" is an ordinary news page (~80
 * connections to ~30 CDN hosts), so it called Firefox a worm. Every rule here
 * has to stay silent for that and still fire on a subnet sweep.
 *
 * Loopback is skipped outright — connecting to 127.0.0.1 is not egress, and the
 * local AI daemons chat constantly.
 *
 * Purely additive: the event still flows through the normal rule pipeline.
 */
#define NW_TABLE       256         /* direct-mapped by pid (heuristic) */
#define NW_WINDOW_S     10         /* sliding window, seconds */
#define NW_DESTS        64         /* distinct hosts tracked per window */
#define NW_SUBNETS      32         /* distinct /24s tracked per window */
#define NW_SWEEP        20         /* distinct hosts in ONE /24 → sweep */
#define NW_SCAN         20         /* distinct non-web hosts → port scan */
#define NW_FLOOD       300         /* connects in the window ...          */
#define NW_FLOOD_HOSTS   2         /* ... spread over at most this many hosts */

struct nw_entry {
    uint32_t pid;
    time_t   win_start;
    int      conn_count;
    int      distinct;              /* distinct hosts this window */
    int      nonweb;                /* of those, how many on non-web ports */
    int      alerted;               /* one alert per window */
    uint32_t dest_hash[NW_DESTS];
    uint32_t sub_key[NW_SUBNETS];   /* /24 network address */
    uint16_t sub_hosts[NW_SUBNETS]; /* distinct hosts seen inside that /24 */
    int      nsubs;
    int      max_sub;               /* busiest /24's host count */
    char     max_sub_name[20];      /* "192.168.1.0/24" */
};
static struct nw_entry nw_table[NW_TABLE];
static pthread_mutex_t nw_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t nw_fnv1a(const char *s, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n && s[i]; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h ? h : 1;   /* reserve 0 for "empty slot" */
}

static int nw_is_web_port(unsigned port)
{
    return port == 80 || port == 443 || port == 8080 || port == 8443;
}

/* "A.B.C.D:port" / "[v6]:port" → host, port. IPv6 keeps its brackets. */
static int nw_split_dest(const char *dest, char *host, size_t hlen, unsigned *port)
{
    const char *colon = strrchr(dest, ':');
    if (!colon || colon == dest) return -1;

    size_t n = (size_t)(colon - dest);
    if (n >= hlen) return -1;

    memcpy(host, dest, n);
    host[n] = '\0';
    *port = (unsigned)strtoul(colon + 1, NULL, 10);
    return 0;
}

/* The /24 a host sits in. IPv4 only: there is no cheap IPv6 equivalent worth
 * the false positives, so v6 destinations simply never trip the sweep rule. */
static int nw_subnet24(const char *host, uint32_t *key, char *name, size_t nlen)
{
    unsigned a, b, c, d;
    char trailing;

    /* The %c catches trailing junk: a clean dotted quad matches exactly 4. */
    if (sscanf(host, "%u.%u.%u.%u%c", &a, &b, &c, &d, &trailing) != 4)
        return -1;
    if (a > 255 || b > 255 || c > 255 || d > 255)
        return -1;

    *key = (a << 24) | (b << 16) | (c << 8);
    snprintf(name, nlen, "%u.%u.%u.0/24", a, b, c);
    return 0;
}

/*
 * Record a connect and decide whether it trips a sweep/scan/flood alert.
 * Returns the threat level (THREAT_NONE when nothing tripped) and fills
 * `reason`, once per window.
 */
sg_threat_t netwatch_connect(const sg_event_t *e, char *reason, size_t rlen)
{
    char host[64];
    unsigned port = 0;

    if (e->filename[0] == '\0') return THREAT_NONE;
    if (nw_split_dest(e->filename, host, sizeof(host), &port) != 0)
        return THREAT_NONE;

    uint32_t subkey = 0;
    char subname[20] = {0};
    int have_sub = (nw_subnet24(host, &subkey, subname, sizeof(subname)) == 0);

    if (have_sub && (subkey >> 24) == 127)      /* loopback is not egress */
        return THREAT_NONE;

    uint32_t dh = nw_fnv1a(host, strlen(host));
    time_t now = time(NULL);
    sg_threat_t threat = THREAT_NONE;

    pthread_mutex_lock(&nw_lock);
    struct nw_entry *ent = &nw_table[e->pid % NW_TABLE];

    /* New pid in this slot, or window expired → start a fresh window. */
    if (ent->pid != e->pid || (now - ent->win_start) > NW_WINDOW_S) {
        memset(ent, 0, sizeof(*ent));
        ent->pid       = e->pid;
        ent->win_start = now;
    }

    ent->conn_count++;

    /* First sighting of this host in the window? Then it also counts towards
     * its /24 and, if the port isn't a web port, the scan tally. Repeat
     * connections to a host already seen only move conn_count. */
    int seen = 0;
    for (int i = 0; i < ent->distinct && i < NW_DESTS; i++)
        if (ent->dest_hash[i] == dh) { seen = 1; break; }

    if (!seen && ent->distinct < NW_DESTS) {
        ent->dest_hash[ent->distinct++] = dh;

        if (!nw_is_web_port(port))
            ent->nonweb++;

        if (have_sub) {
            int b;
            for (b = 0; b < ent->nsubs; b++)
                if (ent->sub_key[b] == subkey) break;

            if (b == ent->nsubs && ent->nsubs < NW_SUBNETS) {
                ent->sub_key[b]   = subkey;
                ent->sub_hosts[b] = 0;
                ent->nsubs++;
            }
            if (b < ent->nsubs) {
                ent->sub_hosts[b]++;
                if (ent->sub_hosts[b] > ent->max_sub) {
                    ent->max_sub = ent->sub_hosts[b];
                    snprintf(ent->max_sub_name, sizeof(ent->max_sub_name),
                             "%s", subname);
                }
            }
        }
    }

    /* The secfeed truncates a reason to 111 chars, so lead with the counts and
     * the destination — they are what a human acts on. */
    if (!ent->alerted) {
        if (ent->max_sub >= NW_SWEEP) {
            threat = THREAT_HIGH;
            snprintf(reason, rlen,
                     "netwatch: subnet sweep — %d hosts in %s in <%ds (last=%s)",
                     ent->max_sub, ent->max_sub_name, NW_WINDOW_S, e->filename);
        } else if (ent->nonweb >= NW_SCAN) {
            threat = THREAT_HIGH;
            snprintf(reason, rlen,
                     "netwatch: port scan — %d hosts on non-web ports in <%ds "
                     "(last=%s)", ent->nonweb, NW_WINDOW_S, e->filename);
        } else if (ent->conn_count >= NW_FLOOD &&
                   ent->distinct <= NW_FLOOD_HOSTS) {
            threat = THREAT_MEDIUM;
            snprintf(reason, rlen,
                     "netwatch: connect flood — %d conns to %d host(s) in <%ds "
                     "(last=%s)", ent->conn_count, ent->distinct,
                     NW_WINDOW_S, e->filename);
        }
        if (threat != THREAT_NONE)
            ent->alerted = 1;
    }
    pthread_mutex_unlock(&nw_lock);
    return threat;
}

/* ── Full decision pipeline ───────────────────────────────── */
void synguard_process_event(synguard_state_t *s, const sg_event_t *e)
{
    s->stats.events_processed++;

    /* Skip our own events to prevent infinite loops */
    if (strcmp(e->comm, "synguard") == 0 ||
        strcmp(e->comm, "synapd")   == 0)
        return;

    /* ── Step 0: Worm/C2 egress fan-out check (connect only) ── */
    /* syscall_nr 42 == connect; the kmod put the dest IP:port in filename. */
    if (e->evt_type == EVT_SOCKET && e->syscall_nr == 42 && e->filename[0]) {
        char nwreason[200];
        sg_threat_t nwthreat = netwatch_connect(e, nwreason, sizeof(nwreason));

        if (nwthreat != THREAT_NONE) {
            sg_alert_t nwalert = {
                .timestamp = time(NULL),
                .event     = *e,
                .verdict   = VERDICT_ALERT,
                .threat    = nwthreat,
            };
            strncpy(nwalert.reason, nwreason, sizeof(nwalert.reason) - 1);
            snprintf(nwalert.action_taken, sizeof(nwalert.action_taken), "alert");
            s->stats.alerts++;
            action_alert(s, &nwalert);
            if (s->config.audit_enabled)
                audit_write(s, &nwalert);
        }
    }

    /* ── Step 1: Baseline anomaly check ────────────────────── */
    int anomalous = 0;
    if (s->config.mode == MODE_LEARNING || s->config.mode == MODE_ENFORCE)
        anomalous = baseline_is_anomalous(s, e);

    /* ── Step 2: Rule engine ────────────────────────────────── */
    const sg_rule_t *matched_rule = NULL;
    sg_verdict_t verdict = rules_evaluate(s, e, &matched_rule);
    if (matched_rule) s->stats.rules_matched++;

    sg_log(LOG_DEBUG, "event: %s pid=%u evt=%02x → rule=%s verdict=%s anomalous=%d",
           e->comm, e->pid, e->evt_type,
           matched_rule ? matched_rule->name : "(default)",
           verdict_name(verdict),
           anomalous);

    /* ── Step 3: AI classification on ESCALATE ──────────────── */
    sg_ai_result_t ai_result = {
        .threat_level = THREAT_NONE,
        .verdict      = verdict,
        .confidence   = 0.0f,
    };

    if (verdict == VERDICT_ESCALATE ||
        (anomalous && s->config.ai_enabled && verdict >= VERDICT_LOG)) {

        char ctx[512];
        build_ai_context(e, ctx, sizeof(ctx));

        if (synguard_ai_classify(s, e, ctx, &ai_result) == 0) {
            sg_log(LOG_DEBUG, "AI: threat=%d verdict=%s confidence=%.2f reason=%.80s",
                   (int)ai_result.threat_level,
                   verdict_name(ai_result.verdict),
                   ai_result.confidence,
                   ai_result.reason);
            /* AI verdict overrides rule verdict for ESCALATE — but the
             * classifier is advisory unless ai_enforce is set: a model
             * hallucination must never be able to SIGKILL a process tree.
             * Only rules written by a human may carry a DENY. */
            if (verdict == VERDICT_ESCALATE) {
                if (ai_result.verdict >= VERDICT_DENY && !s->config.ai_enforce) {
                    sg_log(LOG_WARNING,
                           "AI recommended %s for pid=%u (%s) — clamped to "
                           "alert (--ai-enforce not set): %.120s",
                           verdict_name(ai_result.verdict),
                           e->pid, e->comm, ai_result.reason);
                    verdict = VERDICT_ALERT;
                } else {
                    verdict = ai_result.verdict;
                }
            }
        } else {
            sg_log(LOG_DEBUG, "AI classification failed — keeping rule verdict");
        }
    }

    /* Baseline update (after classification, not before) */
    baseline_update(s, e);

    /* ── Step 4: Action dispatch ─────────────────────────────── */
    sg_alert_t alert = {
        .timestamp   = time(NULL),
        .event       = *e,
        .verdict     = verdict,
        .threat      = ai_result.threat_level,
    };
    snprintf(alert.reason, sizeof(alert.reason), "%s%s%s",
             matched_rule ? matched_rule->name : "default",
             ai_result.reason[0] ? " / AI: " : "",
             ai_result.reason);

    switch (verdict) {
    case VERDICT_ALLOW:
        /* Nothing to do */
        return;

    case VERDICT_LOG:
        sg_log(LOG_DEBUG, "LOG: %s pid=%u %s",
               e->comm, e->pid, e->filename[0] ? e->filename : "");
        if (s->config.audit_enabled)
            audit_write(s, &alert);
        return;

    case VERDICT_ALERT:
        s->stats.alerts++;
        snprintf(alert.action_taken, sizeof(alert.action_taken), "alert");
        action_alert(s, &alert);
        if (s->config.audit_enabled)
            audit_write(s, &alert);
        return;

    case VERDICT_ESCALATE:
        /* Still ESCALATE after AI? Treat as ALERT */
        s->stats.alerts++;
        snprintf(alert.action_taken, sizeof(alert.action_taken), "alert (escalated)");
        action_alert(s, &alert);
        if (s->config.audit_enabled)
            audit_write(s, &alert);
        return;

    case VERDICT_DENY:
        s->stats.denials++;
        snprintf(alert.action_taken, sizeof(alert.action_taken),
                 s->config.mode == MODE_ENFORCE ? "SIGKILL" : "alert(audit-mode)");

        if (s->config.mode == MODE_ENFORCE || s->config.mode == MODE_LOCKDOWN) {
            action_deny(s, e, alert.reason);
        } else {
            /* In AUDIT/LEARNING mode, log but don't actually kill */
            sg_log(LOG_WARNING, "WOULD-DENY: %s pid=%u reason=%s",
                   e->comm, e->pid, alert.reason);
        }
        action_alert(s, &alert);
        if (s->config.audit_enabled)
            audit_write(s, &alert);
        return;

    case VERDICT_QUARANTINE:
        s->stats.quarantines++;
        snprintf(alert.action_taken, sizeof(alert.action_taken), "quarantine");
        action_quarantine(s, e);
        action_alert(s, &alert);
        if (s->config.audit_enabled)
            audit_write(s, &alert);
        return;
    }
}

/* ── kmod reader thread ───────────────────────────────────── */
static void *reader_thread_fn(void *arg)
{
    synguard_state_t *s = (synguard_state_t *)arg;

    sg_log(LOG_INFO, "kmod_reader: started, polling %s every %dms",
           KMOD_SYSCALL_LOG, s->config.poll_interval_ms);

    char buf[4096];

    while (s->running) {
        int fd = open(KMOD_SYSCALL_LOG, O_RDONLY);
        if (fd < 0) {
            /* kmod not loaded — degrade gracefully */
            if (!s->kmod_present) {
                sg_log(LOG_DEBUG, "kmod_reader: %s not available",
                       KMOD_SYSCALL_LOG);
            }
            usleep(s->config.poll_interval_ms * 1000);
            continue;
        }

        s->kmod_present = 1;
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);

        if (n <= 0) {
            /* Quiet system: still run the canary so blinding is caught even
             * when no other events are flowing. */
            canary_tick(s);
            usleep(s->config.poll_interval_ms * 1000);
            continue;
        }
        buf[n] = '\0';

        /* Parse each line */
        char *line = strtok(buf, "\n");
        while (line) {
            if (*line) {
                sg_event_t evt;
                if (kmod_parse_event(line, &evt) == 0) {
                    /* Our own canary open coming back = probes are live.
                     * Check before process_event, which skips synguard's
                     * own events. */
                    if (is_canary_event(&evt)) {
                        canary_pending = 0;
                        canary_misses  = 0;
                    }
                    synguard_process_event(s, &evt);
                }
            }
            line = strtok(NULL, "\n");
        }

        canary_tick(s);
        usleep(s->config.poll_interval_ms * 1000);
    }

    sg_log(LOG_INFO, "kmod_reader: stopped");
    return NULL;
}

int kmod_reader_start(synguard_state_t *s)
{
    /* Check if kmod is present */
    struct stat st;
    s->kmod_present = (stat(KMOD_SYSCALL_LOG, &st) == 0);

    if (!s->kmod_present)
        sg_log(LOG_WARNING, "synguard: synapse_kmod not loaded — "
                            "operating in userspace-only mode");

    if (pthread_create(&s->reader_thread, NULL, reader_thread_fn, s) != 0) {
        sg_log(LOG_ERR, "synguard: failed to start reader thread: %s",
               strerror(errno));
        return -1;
    }
    return 0;
}
