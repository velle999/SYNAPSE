/*
 * access_test.c — the `access read|write|any` rule filter
 *
 * The kmod reports the persistence surfaces (/etc/profile.d, /etc/systemd/
 * system, /etc/xdg/autostart, /boot, cron) on ANY access, because they sit in
 * its sensitive-path list. The system reads those paths constantly: one boot
 * produced 4510 `alert-systemd-unit-write` lines of which 4485 were systemd
 * itself reading units, and 255 `alert-profiled-write` lines that were nothing
 * but login shells sourcing /etc/profile.d. Every rule named "*-write" was
 * firing on reads, because the rule language had no way to say otherwise — the
 * kmod shipped the O_* flags as arg0 and the engine never looked at them.
 *
 * `access` is that filter. This test pins three things:
 *
 *   1. The semantics: write matches a write, read matches a read, neither
 *      matches the other, and the filter is inert on non-open events (where
 *      arg0 means something else entirely — the target uid for setuid).
 *
 *   2. The degradation rule. If the wire carries no arg0, a narrowed rule
 *      matches ANYWAY. This codebase has twice shipped a detector that
 *      silently stopped matching (the pt_regs arg bug left every path rule
 *      dead at alerts=0; the persistence rules named paths the kmod never
 *      emitted). A rule that goes quiet is a worse failure than a rule that is
 *      noisy, so an unknown access mode must fail toward alerting.
 *
 *   3. The real regression, against the SHIPPED rules/ tree: a read of
 *      /etc/profile.d must not alert, and a write to it must.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "synguard.h"

#ifndef SYNGUARD_SHIPPED_RULES_DIR
#define SYNGUARD_SHIPPED_RULES_DIR "rules"
#endif

static int failures;

static void ok(const char *name, int cond)
{
    printf("  %s - %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

static void write_rules(const char *dir, const char *name, const char *body)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) { perror("fopen"); exit(2); }
    fputs(body, f);
    fclose(f);
}

static void unlink_rules(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    unlink(path);
}

static void load(synguard_state_t *s, const char *dir)
{
    memset(s, 0, sizeof(*s));
    s->config.mode = MODE_ENFORCE;
    s->rules_count = rules_load(s, dir);
}

/* An open event with the given path and O_* flags. */
static sg_event_t open_evt(const char *path, unsigned long flags)
{
    sg_event_t e;
    memset(&e, 0, sizeof(e));
    e.pid      = 4242;
    e.uid      = 1000;
    e.evt_type = EVT_OPEN;
    e.has_arg0 = 1;
    e.arg0     = flags;
    snprintf(e.comm, sizeof(e.comm), "bash");
    snprintf(e.filename, sizeof(e.filename), "%s", path);
    return e;
}

int main(void)
{
    char tmpl[] = "/tmp/synguard-access-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 2; }
    printf("access: open-flag rule filter (%s)\n", dir);

    synguard_state_t s;
    sg_event_t e;

    /* ── Semantics ──────────────────────────────────────────── */
    write_rules(dir, "10-access.rules",
        "rule w {\n    event open\n    path /tmp/target\n"
        "    access write\n    verdict alert\n    priority 10\n}\n"
        "rule r {\n    event open\n    path /tmp/readable\n"
        "    access read\n    verdict alert\n    priority 10\n}\n"
        "rule a {\n    event open\n    path /tmp/either\n"
        "    verdict alert\n    priority 10\n}\n");

    load(&s, dir);
    ok("three rules parsed", s.rules_count == 3);

    e = open_evt("/tmp/target", O_WRONLY);
    ok("access write matches O_WRONLY", rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);

    e = open_evt("/tmp/target", O_RDONLY | O_CREAT);
    ok("access write matches O_CREAT", rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);

    e = open_evt("/tmp/target", O_RDWR);
    ok("access write matches O_RDWR", rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);

    /* The live values seen in the ring: /etc/passwd opened 0x80000, sudoers
     * 0x800. Both are reads and neither may trip a write rule. */
    e = open_evt("/tmp/target", O_RDONLY | O_CLOEXEC);
    ok("access write does NOT match O_RDONLY|O_CLOEXEC (0x80000)",
       rules_evaluate(&s, &e, NULL) == VERDICT_LOG);

    e = open_evt("/tmp/target", O_RDONLY | O_NONBLOCK);
    ok("access write does NOT match O_RDONLY|O_NONBLOCK (0x800)",
       rules_evaluate(&s, &e, NULL) == VERDICT_LOG);

    e = open_evt("/tmp/readable", O_RDONLY);
    ok("access read matches a read", rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);

    e = open_evt("/tmp/readable", O_WRONLY);
    ok("access read does NOT match a write",
       rules_evaluate(&s, &e, NULL) == VERDICT_LOG);

    e = open_evt("/tmp/either", O_RDONLY);
    ok("a rule with no access clause still matches a read",
       rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);

    /* ── Degradation: no arg0 on the wire ───────────────────── */
    e = open_evt("/tmp/target", 0);
    e.has_arg0 = 0;
    e.arg0     = 0;
    ok("without arg0 a write rule matches ANYWAY (never silently dark)",
       rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);

    /* ── Inert on non-open events ───────────────────────────── */
    /* For setuid, arg0 is the target uid — uid 0 would look like O_RDONLY and
     * uid 1 like O_WRONLY. A narrowed rule must not read it as open flags. */
    e = open_evt("/tmp/target", 1);
    e.evt_type = EVT_SETUID;
    ok("an access rule does not match a setuid event",
       rules_evaluate(&s, &e, NULL) == VERDICT_LOG);

    /* ── An unparseable access value must not narrow ────────── */
    unlink_rules(dir, "10-access.rules");
    write_rules(dir, "10-access.rules",
        "rule typo {\n    event open\n    path /tmp/target\n"
        "    access wrtie\n    verdict alert\n    priority 10\n}\n");
    rules_free(&s);
    load(&s, dir);
    e = open_evt("/tmp/target", O_RDONLY);
    ok("a misspelled access value falls back to any, not to silence",
       rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);
    rules_free(&s);

    unlink_rules(dir, "10-access.rules");
    rmdir(dir);

    /* ── The shipped policy ─────────────────────────────────── */
    /*
     * The actual regression. These paths are what flooded the log, and each
     * pair is "the system doing its job" vs "something installing
     * persistence".
     */
    load(&s, SYNGUARD_SHIPPED_RULES_DIR);
    ok("shipped rules load", s.rules_count > 0);

    static const struct { const char *path, *what; } quiet[] = {
        { "/etc/profile.d/locale.sh",           "a login shell sourcing profile.d" },
        { "/etc/systemd/system/foo.service",    "systemd reading a unit" },
        { "/etc/xdg/autostart/foo.desktop",     "a session reading autostart" },
        { "/home/velle/.bashrc",                "bash reading its own rc" },
        { "/boot/vmlinuz-linux",                "something reading the kernel" },
    };
    for (size_t i = 0; i < sizeof(quiet) / sizeof(quiet[0]); i++) {
        char name[256];
        e = open_evt(quiet[i].path, O_RDONLY | O_CLOEXEC);
        snprintf(name, sizeof(name), "READ is quiet: %s", quiet[i].what);
        ok(name, rules_evaluate(&s, &e, NULL) != VERDICT_ALERT);
    }

    static const struct { const char *path, *what; } loud[] = {
        { "/etc/profile.d/evil.sh",             "a dropper writing profile.d" },
        { "/etc/systemd/system/evil.service",   "a unit being installed" },
        { "/etc/xdg/autostart/evil.desktop",    "autostart persistence" },
        { "/home/velle/.bashrc",                "an rc file being rewritten" },
        { "/boot/vmlinuz-linux",                "something writing /boot" },
    };
    for (size_t i = 0; i < sizeof(loud) / sizeof(loud[0]); i++) {
        char name[256];
        e = open_evt(loud[i].path, O_WRONLY | O_CREAT | O_TRUNC);
        snprintf(name, sizeof(name), "WRITE still alerts: %s", loud[i].what);
        ok(name, rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);
    }

    /*
     * Credential and device paths must NOT have been narrowed. Reading
     * /etc/shadow or /dev/input IS the attack — a keylogger only ever reads.
     * If someone "cleans up the noise" by putting access write on these, the
     * detector stops seeing the thing it exists for.
     */
    e = open_evt("/etc/shadow", O_RDONLY);
    ok("reading /etc/shadow still alerts",
       rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);

    e = open_evt("/dev/input/event0", O_RDONLY);
    snprintf(e.comm, sizeof(e.comm), "keylogger");
    ok("reading /dev/input still alerts (keylogger)",
       rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);

    e = open_evt("/etc/sudoers", O_RDONLY);
    ok("reading /etc/sudoers still alerts",
       rules_evaluate(&s, &e, NULL) == VERDICT_ALERT);

    rules_free(&s);

    printf(failures ? "access: FAILED (%d)\n" : "access: all passed\n",
           failures);
    return failures ? 1 : 0;
}
