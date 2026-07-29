/*
 * syscall_ret_test.c — the syscall return value, and not acting on a failure
 *
 * The kmod's probes fire on syscall ENTRY, so for years an event said "this
 * process opened this file" when what had actually happened was "this process
 * asked, and the kernel said no". Nothing downstream could tell the two apart,
 * and the deny path treated both as an access. It killed a process tree twice
 * on a live desktop for opens that had ALREADY been refused:
 *
 *   ENOENT   ld.so probing an /etc/ld.so.preload that did not exist
 *   EACCES   a read of a root-owned 0600 canary by an unprivileged process
 *
 * Neither process ever saw a byte of either file. openat now reports at syscall
 * exit and carries the return value, and this test pins both halves of using
 * it: that the wire field survives the trip, and that the deny path stands down
 * when the access did not happen — WITHOUT standing down in the cases where
 * enforcement is the only thing there is.
 *
 * The degradation direction is the point of half these cases. A detector that
 * silently stops enforcing is a worse failure than one that enforces too
 * eagerly, so "outcome unknown" must fall through to the kill, and every way of
 * failing to parse the field must produce "unknown" rather than "succeeded".
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "synguard.h"

/* ── Stubs ─────────────────────────────────────────────────────────────────
 * kmod_parse_event() and sg_deny_suppression_reason() are pure, but they live
 * in event_processor.c alongside the pipeline, which references the rest of the
 * daemon. Nothing below is reached from these tests — and action_deny() in
 * particular MUST NOT be, since the real one SIGKILLs a process tree. */
void action_alert(synguard_state_t *s, const sg_alert_t *a) { (void)s; (void)a; }
void action_deny(synguard_state_t *s, const sg_event_t *e, const char *r)
{ (void)s; (void)e; (void)r; }
void action_quarantine(synguard_state_t *s, const sg_event_t *e) { (void)s; (void)e; }
sg_verdict_t rules_evaluate(synguard_state_t *s, const sg_event_t *e,
                            const sg_rule_t **m)
{ (void)s; (void)e; (void)m; return VERDICT_ALLOW; }
int synguard_ai_classify(synguard_state_t *s, const sg_event_t *e,
                         const char *context, sg_ai_result_t *r)
{ (void)s; (void)e; (void)context; (void)r; return -1; }
int  rules_load(synguard_state_t *s, const char *dir) { (void)s; (void)dir; return 0; }
void rules_free(synguard_state_t *s) { (void)s; }
int  secfeed_init(void) { return 0; }
void secfeed_close(void) { }

static int failures;

#define CHECK(cond, ...) do {                                       \
    if (!(cond)) {                                                  \
        printf("  FAIL: ");  printf(__VA_ARGS__);  printf("\n");    \
        failures++;                                                 \
    }                                                               \
} while (0)

/* ── 1. The wire field ────────────────────────────────────── */
/*
 * Line format:
 *   "<ts> <pid> <uid> <nr> <comm> <filename|-> <flags:hex> <arg0> <ret|->"
 */
static void test_wire_parsing(void)
{
    sg_event_t e;

    printf("wire: ret field\n");

    /* A real failure: EACCES on the canary. */
    CHECK(kmod_parse_event(
              "1000 2211 1000 257 claude /var/lib/synguard/bpf-canary 02 0 -13",
              &e) == 0,
          "a line with a negative ret must parse");
    CHECK(e.has_ret == 1, "has_ret must be set when the field is a number");
    CHECK(e.ret == -13, "ret must be -13 (EACCES), got %d", e.ret);
    CHECK(SG_EVENT_FAILED(&e), "-13 must count as a failed syscall");

    /* A success: openat returning fd 7. */
    CHECK(kmod_parse_event(
              "1000 2211 1000 257 cat /etc/shadow 02 0 7", &e) == 0,
          "a line with a positive ret must parse");
    CHECK(e.has_ret == 1 && e.ret == 7, "ret must be 7, got %d", e.ret);
    CHECK(!SG_EVENT_FAILED(&e), "fd 7 must not count as a failure");

    /* fd 0 is a legitimate success, and it is the value a naive
     * atoi()-of-garbage would also produce. That collision is exactly why the
     * parser validates rather than converts. */
    CHECK(kmod_parse_event("1000 5 0 257 sh /etc/shadow 02 0 0", &e) == 0,
          "ret=0 must parse");
    CHECK(e.has_ret == 1 && e.ret == 0, "ret must be 0");
    CHECK(!SG_EVENT_FAILED(&e), "ret=0 is fd 0 — a success, not a failure");

    /* "-" is the kmod saying the outcome is unknown: reported at entry. */
    CHECK(kmod_parse_event("1000 5 0 59 sh /usr/bin/x 01 0 -", &e) == 0,
          "a line with ret=\"-\" must parse");
    CHECK(e.has_ret == 0, "\"-\" must leave has_ret clear");
    CHECK(!SG_EVENT_FAILED(&e), "unknown must not read as failed");

    /* An older kmod stops after arg0. Absence is unknown, not success. */
    CHECK(kmod_parse_event("1000 5 0 257 sh /etc/shadow 02 0", &e) == 0,
          "an 8-field line from an older kmod must still parse");
    CHECK(e.has_ret == 0, "a missing field must leave has_ret clear");
    CHECK(e.has_arg0 == 1, "the older fields must still be read");

    /* Garbage must degrade to unknown. strtol() would return 0 here, and 0 is
     * "opened fd 0" — the one misreading that turns a refused access back into
     * an enforceable one. */
    CHECK(kmod_parse_event("1000 5 0 257 sh /etc/shadow 02 0 wat", &e) == 0,
          "a non-numeric ret must not fail the whole line");
    CHECK(e.has_ret == 0, "a non-numeric ret must be unknown, NOT 0");
    CHECK(kmod_parse_event("1000 5 0 257 sh /etc/shadow 02 0 12x", &e) == 0,
          "a trailing-garbage ret must not fail the line");
    CHECK(e.has_ret == 0, "\"12x\" must be unknown, not 12");

    /* The new field must not have disturbed the escaped ones: a comm with a
     * space is why escaping exists, and it sits before ret on the wire. */
    CHECK(kmod_parse_event(
              "1000 9 0 257 Socket\\x20Thread /etc/shadow 02 0 -2", &e) == 0,
          "an escaped comm plus a ret must parse");
    CHECK(strcmp(e.comm, "Socket Thread") == 0,
          "comm must unescape to \"Socket Thread\", got \"%s\"", e.comm);
    CHECK(strcmp(e.filename, "/etc/shadow") == 0,
          "filename must survive, got \"%s\"", e.filename);
    CHECK(e.has_ret == 1 && e.ret == -2, "ret must be -2 (ENOENT)");
}

/* ── 2. The suppression predicate ─────────────────────────── */

static int gate_is_live(void)     { return 1; }
static int gate_is_not_live(void) { return 0; }

static sg_event_t mk_event(int has_ret, int ret)
{
    sg_event_t e;
    memset(&e, 0, sizeof(e));
    e.pid      = 2211;
    e.uid      = 1000;
    e.evt_type = EVT_OPEN;
    snprintf(e.comm, sizeof(e.comm), "claude");
    snprintf(e.filename, sizeof(e.filename), "/var/lib/synguard/bpf-canary");
    e.has_ret  = (uint8_t)has_ret;
    e.ret      = ret;
    return e;
}

static void test_failed_syscall_suppresses(void)
{
    synguard_state_t s;
    memset(&s, 0, sizeof(s));
    sg_deny_set_gate_probe_for_test(gate_is_not_live);

    printf("deny: a failed syscall\n");

    /* The exact shape of the incident: EACCES, no kernel gate involved. */
    sg_event_t eacces = mk_event(1, -13);
    const char *r = sg_deny_suppression_reason(&s, &eacces, "deny-bpf-canary");
    CHECK(r != NULL, "EACCES must suppress the kill");
    CHECK(r && strstr(r, "syscall failed"),
          "the reason must name the failed syscall, got \"%s\"", r ? r : "(null)");

    /* And the other one: ENOENT on a preload probe. */
    sg_event_t enoent = mk_event(1, -2);
    CHECK(sg_deny_suppression_reason(&s, &enoent, "deny-ld-preload") != NULL,
          "ENOENT must suppress the kill");

    /* A success must still be enforced. This is the case the whole feature
     * exists to preserve. */
    sg_event_t ok = mk_event(1, 3);
    CHECK(sg_deny_suppression_reason(&s, &ok, "deny-shadow-read") == NULL,
          "a successful open MUST still be denied");

    /* Unknown outcome still enforces — an older kmod must not silently
     * disarm enforcement for every event it produces. */
    sg_event_t unknown = mk_event(0, 0);
    CHECK(sg_deny_suppression_reason(&s, &unknown, "deny-shadow-read") == NULL,
          "an unknown outcome MUST still be denied");

    sg_deny_set_gate_probe_for_test(NULL);
}

static void test_kernel_enforced_suppresses(void)
{
    synguard_state_t s;
    memset(&s, 0, sizeof(s));
    snprintf(s.bpf_enforced_rules[0], RULE_MAX_NAME, "deny-bpf-canary");
    s.bpf_enforced_count = 1;

    /* A successful-looking open, so only the gate half can suppress it. */
    sg_event_t e = mk_event(1, 3);

    printf("deny: the kernel gate owns the rule\n");

    sg_deny_set_gate_probe_for_test(gate_is_live);
    const char *r = sg_deny_suppression_reason(&s, &e, "deny-bpf-canary");
    CHECK(r != NULL, "an armed, live gate must suppress the userspace kill");
    CHECK(r && strstr(r, "in-kernel"),
          "the reason must say the kernel handled it, got \"%s\"", r ? r : "(null)");

    /* A rule the kernel was never given must still be enforced here, even
     * with the gate live — the gate cannot refuse what it does not know. */
    CHECK(sg_deny_suppression_reason(&s, &e, "deny-some-other-rule") == NULL,
          "a rule that was never lowered MUST still be denied in userspace");

    /*
     * The safety case. Loaded-but-not-live covers warmup, a spent deny budget
     * and a gate that never attached. In each the kernel refuses nothing, so
     * the post-hoc kill is the only enforcement that exists and must not be
     * suppressed.
     */
    sg_deny_set_gate_probe_for_test(gate_is_not_live);
    CHECK(sg_deny_suppression_reason(&s, &e, "deny-bpf-canary") == NULL,
          "an armed but NOT live gate must leave the userspace kill in place");

    /* No policy loaded at all: nothing suppresses. */
    synguard_state_t empty;
    memset(&empty, 0, sizeof(empty));
    sg_deny_set_gate_probe_for_test(gate_is_live);
    CHECK(sg_deny_suppression_reason(&empty, &e, "deny-bpf-canary") == NULL,
          "with no lowered policy the kill must stand");

    sg_deny_set_gate_probe_for_test(NULL);
}

int main(void)
{
    printf("=== syscall return value / enforcement suppression ===\n");

    test_wire_parsing();
    test_failed_syscall_suppresses();
    test_kernel_enforced_suppresses();

    if (failures) {
        printf("\n%d check%s FAILED\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
