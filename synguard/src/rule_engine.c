/*
 * rule_engine.c — synguard rule engine
 *
 * Rules are loaded from /etc/synguard/rules.d/*.rules
 * Each file contains ordered rules in a simple declarative syntax.
 *
 * Rule file format:
 * ─────────────────
 *   # comment
 *   rule <name> {
 *       event   exec|open|socket|ptrace|module|mount|setuid|any
 *       uid     <uid>|any
 *       comm    <pattern>      # fnmatch: bash, python*, vim
 *       path    <pattern>      # fnmatch: /etc/shadow, /boot/*
 *       verdict allow|log|alert|escalate|deny|quarantine
 *       priority <number>      # lower = higher priority (default 50)
 *   }
 *
 * Examples:
 *   rule allow-synguard-self {
 *       comm    synguard
 *       verdict allow
 *       priority 1
 *   }
 *
 *   rule deny-shadow-read {
 *       event   open
 *       path    /etc/shadow
 *       uid     any
 *       verdict deny
 *       priority 10
 *   }
 *
 *   rule alert-module-load {
 *       event   module
 *       verdict alert
 *       priority 20
 *   }
 *
 *   rule escalate-unknown-exec {
 *       event   exec
 *       path    /tmp/*
 *       verdict escalate
 *       priority 30
 *   }
 *
 * Rules are evaluated in priority order (lowest number first).
 * First match wins. If no rule matches: default verdict is LOG.
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#include "synguard.h"
#include "sg_log.h"

/* ── Verdict parsing ──────────────────────────────────────── */
static sg_verdict_t parse_verdict(const char *s)
{
    if (strcmp(s, "allow")      == 0) return VERDICT_ALLOW;
    if (strcmp(s, "log")        == 0) return VERDICT_LOG;
    if (strcmp(s, "alert")      == 0) return VERDICT_ALERT;
    if (strcmp(s, "escalate")   == 0) return VERDICT_ESCALATE;
    if (strcmp(s, "deny")       == 0) return VERDICT_DENY;
    if (strcmp(s, "quarantine") == 0) return VERDICT_QUARANTINE;
    return VERDICT_LOG;  /* safe default */
}

/* ── Event mask parsing ───────────────────────────────────── */
static uint8_t parse_event(const char *s)
{
    if (strcmp(s, "exec")   == 0) return EVT_EXEC;
    if (strcmp(s, "open")   == 0) return EVT_OPEN;
    if (strcmp(s, "socket") == 0) return EVT_SOCKET;
    if (strcmp(s, "ptrace") == 0) return EVT_PTRACE;
    if (strcmp(s, "module") == 0) return EVT_MODULE;
    if (strcmp(s, "mount")  == 0) return EVT_MOUNT;
    if (strcmp(s, "setuid") == 0) return EVT_SETUID;
    if (strcmp(s, "any")    == 0) return 0xFF;
    return 0;
}

/* ── Rule allocation ──────────────────────────────────────── */
static sg_rule_t *rule_alloc(void)
{
    sg_rule_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->uid_match = UID_ANY;
    r->priority  = 50;
    r->enabled   = 1;
    r->verdict   = VERDICT_LOG;
    r->evt_mask  = 0xFF;   /* match all events by default */
    return r;
}

/* ── Insert rule in sorted order (by priority) ────────────── */
static void rule_insert(synguard_state_t *s, sg_rule_t *r)
{
    pthread_rwlock_wrlock(&s->rules_lock);

    if (!s->rules_head || r->priority < s->rules_head->priority) {
        r->next = s->rules_head;
        s->rules_head = r;
    } else {
        sg_rule_t *cur = s->rules_head;
        while (cur->next && cur->next->priority <= r->priority)
            cur = cur->next;
        r->next = cur->next;
        cur->next = r;
    }
    s->rules_count++;
    pthread_rwlock_unlock(&s->rules_lock);
}

/* ── Parse one rule file ──────────────────────────────────── */
static int parse_rule_file(synguard_state_t *s, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        sg_log(LOG_WARNING, "rules: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    char line[512];
    sg_rule_t *current = NULL;
    int loaded = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing whitespace */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' ||
                            line[len-1] == ' ' || line[len-1] == '\t'))
            line[--len] = '\0';

        /* Skip empty lines and comments */
        if (!len || line[0] == '#') continue;

        /* Trim leading whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        /* "rule <name> {" — start a new rule */
        if (strncmp(p, "rule ", 5) == 0) {
            if (current) {
                /* Unterminated previous rule */
                sg_log(LOG_WARNING, "rules: unterminated rule in %s", path);
                free(current);
            }
            current = rule_alloc();
            if (!current) break;

            char *name = p + 5;
            /* Strip " {" suffix if present */
            char *brace = strchr(name, '{');
            if (brace) *brace = '\0';
            /* Trim trailing space */
            len = strlen(name);
            while (len > 0 && name[len-1] == ' ') name[--len] = '\0';
            strncpy(current->name, name, sizeof(current->name) - 1);
            continue;
        }

        /* "}" — close rule */
        if (strcmp(p, "}") == 0) {
            if (current) {
                rule_insert(s, current);
                loaded++;
                sg_log(LOG_DEBUG, "rules: loaded '%s' (priority=%d verdict=%d)",
                       current->name, current->priority, (int)current->verdict);
                current = NULL;
            }
            continue;
        }

        if (!current) continue;

        /* Key-value pairs inside a rule block */
        char key[64] = {0}, val[256] = {0};
        if (sscanf(p, "%63s %255[^\n]", key, val) != 2) continue;

        if (strcmp(key, "event")    == 0) current->evt_mask = parse_event(val);
        else if (strcmp(key, "uid") == 0) {
            if (strcmp(val, "any") == 0) current->uid_match = UID_ANY;
            else current->uid_match = (uint32_t)atoi(val);
        }
        else if (strcmp(key, "comm")     == 0) strncpy(current->comm_pattern, val, RULE_MAX_PATTERN - 1);
        else if (strcmp(key, "path")     == 0) strncpy(current->path_pattern, val, RULE_MAX_PATTERN - 1);
        else if (strcmp(key, "verdict")  == 0) current->verdict  = parse_verdict(val);
        else if (strcmp(key, "priority") == 0) current->priority = atoi(val);
        else if (strcmp(key, "enabled")  == 0) current->enabled  = atoi(val);
        else
            sg_log(LOG_DEBUG, "rules: unknown key '%s' in %s", key, path);
    }

    if (current) {
        sg_log(LOG_WARNING, "rules: unterminated rule '%s' in %s",
               current->name, path);
        free(current);
    }

    fclose(f);
    return loaded;
}

/* ── Load all rules from directory ───────────────────────── */
int rules_load(synguard_state_t *s, const char *dir)
{
    if (!dir) dir = SYNGUARD_RULES_DIR;

    DIR *d = opendir(dir);
    if (!d) {
        sg_log(LOG_WARNING, "rules: cannot open rules dir %s: %s",
               dir, strerror(errno));
        return 0;
    }

    struct dirent *ent;
    int total = 0;

    while ((ent = readdir(d)) != NULL) {
        /* Only load .rules files */
        size_t nlen = strlen(ent->d_name);
        if (nlen < 7 || strcmp(ent->d_name + nlen - 6, ".rules") != 0)
            continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        int n = parse_rule_file(s, path);
        if (n > 0) {
            sg_log(LOG_INFO, "rules: loaded %d rules from %s", n, ent->d_name);
            total += n;
        }
    }

    closedir(d);
    sg_log(LOG_INFO, "rules: %d total rules loaded", total);
    return total;
}

/* ── Evaluate rules against an event ─────────────────────── */
sg_verdict_t rules_evaluate(synguard_state_t *s, const sg_event_t *e,
                             const sg_rule_t **matched_out)
{
    if (matched_out) *matched_out = NULL;

    pthread_rwlock_rdlock(&s->rules_lock);

    sg_verdict_t verdict = VERDICT_LOG;  /* default */

    for (sg_rule_t *r = s->rules_head; r; r = r->next) {
        if (!r->enabled) continue;

        /* Event type match */
        if (r->evt_mask != 0xFF && !(r->evt_mask & e->evt_type))
            continue;

        /* UID match */
        if (r->uid_match != UID_ANY && r->uid_match != e->uid)
            continue;

        /* comm pattern match */
        if (r->comm_pattern[0]) {
            if (fnmatch(r->comm_pattern, e->comm, FNM_NOESCAPE) != 0)
                continue;
        }

        /* path pattern match */
        if (r->path_pattern[0]) {
            if (!e->filename[0]) continue;
            if (fnmatch(r->path_pattern, e->filename, FNM_NOESCAPE | FNM_PATHNAME) != 0)
                continue;
        }

        /* Match found */
        verdict = r->verdict;
        if (matched_out) *matched_out = r;
        break;
    }

    pthread_rwlock_unlock(&s->rules_lock);
    return verdict;
}

/* ── Free all rules ───────────────────────────────────────── */
void rules_free(synguard_state_t *s)
{
    pthread_rwlock_wrlock(&s->rules_lock);
    sg_rule_t *r = s->rules_head;
    while (r) {
        sg_rule_t *next = r->next;
        free(r);
        r = next;
    }
    s->rules_head  = NULL;
    s->rules_count = 0;
    pthread_rwlock_unlock(&s->rules_lock);
}
