/*
 * report_test.c — what the BPF-LSM hooks are told to report, how the two
 * witnesses of one open become one event, and the events synapse_kmod 30
 * added, against the shipped policy.
 *
 *  1. The watch list, derived from the SHIPPED rules. The property that
 *     matters is that it never MISSES: for every path in a corpus, if any open
 *     or exec rule's pattern matches it under fnmatch(FNM_PATHNAME) — what
 *     rules_evaluate() calls — the watch list, applied the way the hook
 *     applies it, must report it. And it must not report all of /home, or the
 *     whole filesystem, for opens.
 *  2. sg_dedup_check(): the second witness within the window is dropped, in
 *     either order; the same witness twice is two events.
 *  3. The shipped rules on a signal, a host bind mount and a setgid(0).
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synguard.h"
#include "sg_watch.h"

/* ── Stubs: event_processor.c references the rest of the daemon. ───────── */
void action_alert(synguard_state_t *s, const sg_alert_t *a) { (void)s; (void)a; }
void action_deny(synguard_state_t *s, const sg_event_t *e, const char *r)
{ (void)s; (void)e; (void)r; }
void action_quarantine(synguard_state_t *s, const sg_event_t *e) { (void)s; (void)e; }
int  secfeed_init(void) { return 0; }
void secfeed_close(void) { }

#ifndef SYNGUARD_SHIPPED_RULES_DIR
#define SYNGUARD_SHIPPED_RULES_DIR "rules"
#endif

static int failures, checks;
#define CHECK(cond, ...) do {                                   \
    checks++;                                                   \
    if (!(cond)) { failures++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* The hook's decision, in userspace: a whole-path prefix, or — for a path
 * under /home/<user>/ — a prefix of what follows the user. */
static int watch_reports(const sg_watch_t *w, int n, const char *path, uint32_t evt)
{
    const char *rest = NULL;
    if (strncmp(path, "/home/", 6) == 0) {
        const char *sep = strchr(path + 6, '/');
        if (sep && sep > path + 6 && sep[1]) rest = sep + 1;
    }
    for (int i = 0; i < n; i++) {
        if (!(w[i].evt_mask & evt)) continue;
        const char *subject = w[i].home ? rest : path;
        if (subject && strncmp(subject, w[i].prefix, strlen(w[i].prefix)) == 0)
            return 1;
    }
    return 0;
}

static void test_watch(synguard_state_t *s)
{
    printf("watch: the shipped rules, as prefixes the hooks can test\n");
    sg_watch_t w[128];
    char skipped[512];
    int n = sg_watch_derive(s->rules_head, w, 128, skipped, sizeof(skipped));
    CHECK(n > 0, "the shipped rules produce a watch list (n=%d)", n);

    for (int i = 0; i < n; i++) {
        if (!(w[i].evt_mask & EVT_OPEN)) continue;
        CHECK(!(w[i].home == 0 && (!strcmp(w[i].prefix, "/") ||
                                   !strcmp(w[i].prefix, "/home/"))),
              "no open watch on '%s' — it would report every open", w[i].prefix);
    }

    static const char *corpus[] = {
        "/etc/shadow", "/etc/passwd", "/etc/sudoers", "/etc/crontab",
        "/etc/cron.d/job", "/etc/cron.daily", "/etc/ld.so.preload",
        "/etc/systemd/system/evil.service", "/etc/profile.d/x.sh",
        "/etc/xdg/autostart/x.desktop", "/etc/rc.local",
        "/boot/vmlinuz-linux", "/boot/grub/grub.cfg", "/dev/input/event3",
        "/home/alice/.ssh/id_ed25519", "/home/bob/.ssh/id_rsa",
        "/home/alice/.bashrc", "/home/alice/.zshrc", "/home/alice/.profile",
        "/home/alice/.config/autostart/x.desktop",
        "/home/alice/.config/systemd/user.service",
        "/root/.ssh/authorized_keys", "/var/spool/cron/root",
        "/var/lib/synguard/bpf-canary",
        "/tmp/payload", "/var/tmp/payload", "/dev/shm/x", "/proc/self/exe",
        "/proc/1234/exe", "/usr/bin/bash",
        "/home/alice/.cache/", "/home/alice/Documents/report.pdf",
        "/usr/lib/libc.so.6", "/etc/hostname",
    };
    int missed = 0;
    for (size_t c = 0; c < sizeof(corpus) / sizeof(corpus[0]); c++) {
        for (const sg_rule_t *r = s->rules_head; r; r = r->next) {
            if (!r->path_pattern[0]) continue;
            uint32_t evts[2] = { EVT_OPEN, EVT_EXEC };
            for (int k = 0; k < 2; k++) {
                if (!(r->evt_mask & evts[k])) continue;
                if (fnmatch(r->path_pattern, corpus[c], FNM_PATHNAME) != 0) continue;
                if (!watch_reports(w, n, corpus[c], evts[k])) {
                    missed++;
                    printf("  FAIL: rule %s matches %s (%s) but it is not watched\n",
                           r->name, corpus[c], k ? "exec" : "open");
                }
            }
        }
    }
    checks++;
    if (missed) failures++;
    printf("  %s  every path a shipped open/exec rule matches is reported\n",
           missed ? "FAIL" : "ok  ");

    CHECK(!watch_reports(w, n, "/home/alice/Documents/report.pdf", EVT_OPEN),
          "an ordinary file in a home directory is not reported");
    CHECK(!watch_reports(w, n, "/usr/lib/libc.so.6", EVT_OPEN),
          "a library open is not reported");

    /* Rules that cannot be narrowed. */
    sg_rule_t wild = {0}, wildx = {0};
    snprintf(wild.name, sizeof(wild.name), "open-anything");
    wild.evt_mask = EVT_OPEN;
    snprintf(wild.path_pattern, sizeof(wild.path_pattern), "*");
    snprintf(wildx.name, sizeof(wildx.name), "exec-anything");
    wildx.evt_mask = EVT_EXEC;
    snprintf(wildx.path_pattern, sizeof(wildx.path_pattern), "/*");
    wild.next = &wildx;
    n = sg_watch_derive(&wild, w, 128, skipped, sizeof(skipped));
    CHECK(strstr(skipped, "open-anything") != NULL,
          "an open rule with no literal part is refused, by name ('%s')", skipped);
    CHECK(n == 1 && !strcmp(w[0].prefix, "/") && w[0].evt_mask == EVT_EXEC,
          "an exec rule on /-star- is watched at '/', for execs only");
}

static sg_event_t ev(uint8_t evt, uint32_t pid, const char *path, uint8_t src)
{
    sg_event_t e;
    memset(&e, 0, sizeof(e));
    e.evt_type = evt;
    e.pid = pid;
    snprintf(e.filename, sizeof(e.filename), "%s", path);
    e.source = src;
    return e;
}

static void test_dedup(void)
{
    printf("dedup: two witnesses, one event\n");
    const uint64_t S = 1000000000ULL;
    uint64_t t = 1000 * S;
    sg_event_t k = ev(EVT_OPEN, 42, "/etc/shadow", SG_SRC_KMOD);
    sg_event_t l = ev(EVT_OPEN, 42, "/etc/shadow", SG_SRC_LSM);

    CHECK(!sg_dedup_check(&k, t) && sg_dedup_check(&l, t + 1000),
          "kmod then LSM: the second is dropped");
    t += 10 * S;
    CHECK(!sg_dedup_check(&l, t) && sg_dedup_check(&k, t + 1000),
          "LSM then kmod: the second is dropped");
    t += 10 * S;
    CHECK(!sg_dedup_check(&k, t) && !sg_dedup_check(&k, t + 1000),
          "the same witness twice is two opens");
    t += 10 * S;
    sg_event_t other = ev(EVT_OPEN, 43, "/etc/shadow", SG_SRC_LSM);
    CHECK(!sg_dedup_check(&k, t) && !sg_dedup_check(&other, t + 1000),
          "another process's open is not a duplicate");
    t += 10 * S;
    CHECK(!sg_dedup_check(&k, t) && !sg_dedup_check(&l, t + 3 * S),
          "outside the window it is a new open");
    t += 10 * S;
    CHECK(!sg_dedup_check(&k, t) && sg_dedup_check(&l, t + 1) &&
          !sg_dedup_check(&k, t + 2),
          "a third report after a matched pair is a new open");
    t += 10 * S;
    sg_event_t x1 = ev(EVT_SETUID, 42, "", SG_SRC_KMOD);
    CHECK(!sg_dedup_check(&x1, t) && !sg_dedup_check(&x1, t + 1),
          "events other than open/exec are never deduplicated");
    sg_event_t e1 = ev(EVT_EXEC, 42, "/tmp/x", SG_SRC_KMOD);
    sg_event_t e2 = ev(EVT_EXEC, 42, "/tmp/x", SG_SRC_LSM);
    CHECK(!sg_dedup_check(&e1, t) && sg_dedup_check(&e2, t + 1),
          "an exec seen by both is one exec");
}

static void test_new_events(synguard_state_t *s)
{
    printf("events: the shipped policy on what synapse_kmod 30 added\n");
    const sg_rule_t *m = NULL;
    sg_event_t e;

    memset(&e, 0, sizeof(e));
    e.evt_type = EVT_SIGNAL; e.pid = 777; e.uid = 0; e.syscall_nr = 62;
    snprintf(e.comm, sizeof(e.comm), "bash");
    snprintf(e.filename, sizeof(e.filename), "synguard");
    e.arg0 = 19; e.has_arg0 = 1;
    sg_verdict_t v = rules_evaluate(s, &e, &m);
    CHECK(v == VERDICT_ALERT && m && !strcmp(m->name, "alert-signal-to-security-daemon"),
          "SIGSTOP to synguard alerts (%s)", m ? m->name : "no rule");

    memset(&e, 0, sizeof(e));
    e.evt_type = EVT_OPEN; e.pid = 780; e.uid = 1000; e.syscall_nr = 257;
    e.has_arg0 = 1; e.arg0 = 0; e.source = SG_SRC_LSM;
    snprintf(e.filename, sizeof(e.filename), "/home/alice/.ssh/id_ed25519");
    snprintf(e.comm, sizeof(e.comm), "ssh");
    m = NULL;
    v = rules_evaluate(s, &e, &m);
    CHECK(v == VERDICT_ALLOW, "ssh reading its own key does not alert (%s)",
          m ? m->name : "no rule");
    snprintf(e.comm, sizeof(e.comm), "python3");
    m = NULL;
    v = rules_evaluate(s, &e, &m);
    CHECK(v == VERDICT_ALERT && m && !strcmp(m->name, "alert-ssh-key-user"),
          "anything else reading a private key alerts (%s)", m ? m->name : "no rule");

    memset(&e, 0, sizeof(e));
    e.evt_type = EVT_MOUNT; e.pid = 778; e.uid = 0; e.syscall_nr = 165;
    snprintf(e.comm, sizeof(e.comm), "mount");
    snprintf(e.filename, sizeof(e.filename), "/usr/bin");
    e.arg0 = 4096; e.has_arg0 = 1;
    m = NULL;
    v = rules_evaluate(s, &e, &m);
    CHECK(v == VERDICT_ESCALATE && m && !strcmp(m->name, "escalate-bind-mount"),
          "a host bind mount escalates (%s)", m ? m->name : "no rule");

    memset(&e, 0, sizeof(e));
    e.evt_type = EVT_SETUID; e.pid = 779; e.uid = 1000; e.syscall_nr = 106;
    snprintf(e.comm, sizeof(e.comm), "exploit");
    e.arg0 = 0; e.has_arg0 = 1;
    m = NULL;
    v = rules_evaluate(s, &e, &m);
    CHECK(v == VERDICT_ESCALATE && m && !strcmp(m->name, "escalate-setuid-to-root"),
          "setgid(0) escalates like setuid(0) (%s)", m ? m->name : "no rule");
}

int main(void)
{
    static synguard_state_t s;
    memset(&s, 0, sizeof(s));
    s.config.mode = MODE_ENFORCE;
    pthread_rwlock_init(&s.rules_lock, NULL);
    s.rules_count = rules_load(&s, SYNGUARD_SHIPPED_RULES_DIR);
    if (s.rules_count <= 0) {
        printf("could not load the shipped rules from %s\n", SYNGUARD_SHIPPED_RULES_DIR);
        return 1;
    }

    test_watch(&s);
    test_dedup();
    test_new_events(&s);

    printf("\n%d check(s), %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
