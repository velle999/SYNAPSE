/*
 * baseline.c — Process behavioral baseline
 *
 * Maintains an in-memory table of what events each process
 * (by comm name) normally generates. Used in LEARNING mode
 * to flag anomalous behavior without a pre-written rule.
 *
 * A process is anomalous if it generates an event type it
 * has never generated before AND the event type is sensitive.
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include "synguard.h"
#include "sg_log.h"

#define BASELINE_MAX_ENTRIES  1024
#define SENSITIVE_EVT_MASK    (EVT_EXEC | EVT_PTRACE | EVT_MODULE | EVT_SETUID)

int baseline_load(synguard_state_t *s)
{
    s->baseline = calloc(BASELINE_MAX_ENTRIES, sizeof(sg_baseline_entry_t));
    if (!s->baseline) return -1;
    s->baseline_count = 0;
    pthread_mutex_init(&s->baseline_lock, NULL);

    FILE *f = fopen(SYNGUARD_BASELINE, "r");
    if (!f) return 0;  /* fresh start */

    char line[512];
    while (fgets(line, sizeof(line), f) && s->baseline_count < BASELINE_MAX_ENTRIES) {
        sg_baseline_entry_t *e = &s->baseline[s->baseline_count];
        if (sscanf(line, "%15s %u %u %ld",
                   e->comm, &e->typical_evt_mask,
                   &e->seen_count, &e->first_seen) == 4) {
            e->last_seen = e->first_seen;
            s->baseline_count++;
        }
    }
    fclose(f);
    sg_log(LOG_INFO, "baseline: loaded %d entries", s->baseline_count);
    return 0;
}

void baseline_save(synguard_state_t *s)
{
    FILE *f = fopen(SYNGUARD_BASELINE, "w");
    if (!f) return;

    pthread_mutex_lock(&s->baseline_lock);
    for (int i = 0; i < s->baseline_count; i++) {
        sg_baseline_entry_t *e = &s->baseline[i];
        fprintf(f, "%s %u %u %ld\n",
                e->comm, e->typical_evt_mask,
                e->seen_count, e->first_seen);
    }
    pthread_mutex_unlock(&s->baseline_lock);
    fclose(f);
}

void baseline_update(synguard_state_t *s, const sg_event_t *evt)
{
    pthread_mutex_lock(&s->baseline_lock);

    /* Find entry for this comm */
    sg_baseline_entry_t *entry = NULL;
    for (int i = 0; i < s->baseline_count; i++) {
        if (strcmp(s->baseline[i].comm, evt->comm) == 0) {
            entry = &s->baseline[i];
            break;
        }
    }

    if (!entry && s->baseline_count < BASELINE_MAX_ENTRIES) {
        entry = &s->baseline[s->baseline_count++];
        strncpy(entry->comm, evt->comm, sizeof(entry->comm) - 1);
        entry->first_seen = time(NULL);
    }

    if (entry) {
        entry->typical_evt_mask |= evt->evt_type;
        entry->seen_count++;
        entry->last_seen = time(NULL);
    }

    pthread_mutex_unlock(&s->baseline_lock);
}

int baseline_is_anomalous(synguard_state_t *s, const sg_event_t *evt)
{
    if (!(evt->evt_type & SENSITIVE_EVT_MASK)) return 0;

    pthread_mutex_lock(&s->baseline_lock);

    for (int i = 0; i < s->baseline_count; i++) {
        if (strcmp(s->baseline[i].comm, evt->comm) == 0) {
            int known = (s->baseline[i].typical_evt_mask & evt->evt_type) != 0;
            /* Only flag if we have enough data (seen 10+ times) */
            int enough_data = s->baseline[i].seen_count >= 10;
            pthread_mutex_unlock(&s->baseline_lock);
            return enough_data && !known;
        }
    }

    /* Unknown process — flag as anomalous only for module loads */
    pthread_mutex_unlock(&s->baseline_lock);
    return (evt->evt_type & EVT_MODULE) != 0;
}


/*
 * audit.c — Append-only audit log
 *
 * Writes a structured log line for each security event.
 * Format:
 *   TIMESTAMP|VERDICT|THREAT|PID|UID|COMM|EVT|FILE|REASON|ACTION
 */

int audit_init(synguard_state_t *s)
{
    if (!s->config.audit_enabled) return 0;

    const char *path = s->config.audit_log_path
                       ? s->config.audit_log_path
                       : SYNGUARD_AUDIT_LOG;

    s->audit_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
    if (s->audit_fd < 0) {
        sg_log(LOG_WARNING, "audit: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    pthread_mutex_init(&s->audit_lock, NULL);

    /* Write header if file is new */
    struct stat st;
    fstat(s->audit_fd, &st);
    if (st.st_size == 0) {
        const char *hdr =
            "# synguard audit log\n"
            "# FORMAT: TIMESTAMP|VERDICT|THREAT|PID|UID|COMM|EVT|FILE|REASON|ACTION\n";
        write(s->audit_fd, hdr, strlen(hdr));
    }

    return 0;
}

void audit_write(synguard_state_t *s, const sg_alert_t *alert)
{
    if (!s->config.audit_enabled || s->audit_fd < 0) return;

    const sg_event_t *e = &alert->event;

    static const char *vnames[] = {
        "ALLOW", "LOG", "ALERT", "ESCALATE", "DENY", "QUARANTINE"
    };
    static const char *tnames[] = {
        "NONE", "LOW", "MEDIUM", "HIGH", "CRITICAL"
    };

    const char *vn = alert->verdict < 6 ? vnames[alert->verdict] : "?";
    const char *tn = alert->threat  < 5 ? tnames[alert->threat]  : "?";

    char line[1024];
    int n = snprintf(line, sizeof(line),
        "%llu|%s|%s|%u|%u|%s|%02x|%s|%s|%s\n",
        (unsigned long long)((uint64_t)alert->timestamp * 1000000000ULL),
        vn, tn,
        e->pid, e->uid, e->comm,
        e->evt_type,
        e->filename[0] ? e->filename : "-",
        alert->reason[0] ? alert->reason : "-",
        alert->action_taken[0] ? alert->action_taken : "-"
    );

    pthread_mutex_lock(&s->audit_lock);
    write(s->audit_fd, line, n);
    pthread_mutex_unlock(&s->audit_lock);
}

void audit_close(synguard_state_t *s)
{
    if (s->audit_fd >= 0) {
        close(s->audit_fd);
        s->audit_fd = -1;
    }
}


/*
 * init.c — synguard daemon initialization
 */

int synguard_init(synguard_state_t *s)
{
    /* Set defaults */
    if (!s->config.rules_dir)      s->config.rules_dir    = SYNGUARD_RULES_DIR;
    if (!s->config.audit_log_path) s->config.audit_log_path = SYNGUARD_AUDIT_LOG;
    if (!s->config.ai_timeout_ms)  s->config.ai_timeout_ms = 3000;
    if (!s->config.poll_interval_ms) s->config.poll_interval_ms = 100;
    if (!s->config.ai_threshold)   s->config.ai_threshold = 0.3f;

    /* Default: AI enabled, audit enabled */
    if (s->config.ai_enabled == 0 && !s->debug)
        s->config.ai_enabled  = 1;
    if (s->config.audit_enabled == 0 && !s->debug)
        s->config.audit_enabled = 1;

    s->running    = 1;
    s->synapd_fd  = -1;
    s->kmod_fd    = -1;
    s->audit_fd   = -1;
    s->stats.start_time = time(NULL);

    pthread_rwlock_init(&s->rules_lock, NULL);

    /* Load rules */
    int n = rules_load(s, s->config.rules_dir);
    if (n == 0)
        sg_log(LOG_WARNING, "synguard: no rules loaded — all events will use defaults");

    /* Load baseline */
    baseline_load(s);

    /* Open audit log */
    audit_init(s);

    return 0;
}

void synguard_destroy(synguard_state_t *s)
{
    s->running = 0;
    pthread_join(s->reader_thread, NULL);
    rules_free(s);
    free(s->baseline);
    pthread_rwlock_destroy(&s->rules_lock);
    pthread_mutex_destroy(&s->baseline_lock);
    pthread_mutex_destroy(&s->audit_lock);
}
