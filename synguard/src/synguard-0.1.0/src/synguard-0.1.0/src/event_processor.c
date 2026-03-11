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
 * https://synapseos.dev
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
    char comm[32] = {0};
    char filename[256] = {0};
    memset(out, 0, sizeof(*out));

    int n = sscanf(line, "%llu %u %u %u %31s %255s",
                   (unsigned long long *)&out->timestamp_ns,
                   &out->pid, &out->uid, &out->syscall_nr,
                   comm, filename);

    if (n < 5) return -1;

    strncpy(out->comm, comm, sizeof(out->comm) - 1);

    if (n >= 6 && strcmp(filename, "-") != 0)
        strncpy(out->filename, filename, sizeof(out->filename) - 1);

    out->evt_type = syscall_to_evt(out->syscall_nr);
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
        [EVT_SETUID] = "setuid_to_root",
    };

    const char *ename = (e->evt_type < 0x80 && evt_names[e->evt_type])
                        ? evt_names[e->evt_type] : "unknown";

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

/* ── Full decision pipeline ───────────────────────────────── */
void synguard_process_event(synguard_state_t *s, const sg_event_t *e)
{
    s->stats.events_processed++;

    /* Skip our own events to prevent infinite loops */
    if (strcmp(e->comm, "synguard") == 0 ||
        strcmp(e->comm, "synapd")   == 0)
        return;

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
            /* AI verdict overrides rule verdict for ESCALATE */
            if (verdict == VERDICT_ESCALATE)
                verdict = ai_result.verdict;
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
            usleep(s->config.poll_interval_ms * 1000);
            continue;
        }
        buf[n] = '\0';

        /* Parse each line */
        char *line = strtok(buf, "\n");
        while (line) {
            if (*line) {
                sg_event_t evt;
                if (kmod_parse_event(line, &evt) == 0)
                    synguard_process_event(s, &evt);
            }
            line = strtok(NULL, "\n");
        }

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
