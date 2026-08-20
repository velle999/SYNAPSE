/*
 * policy_test.c — verdict census and enforcement reachability
 *
 * "mode=ENFORCE" only means acting is permitted. It says nothing about whether
 * any loaded rule can ask for it, and on a stock SynapseOS install nothing can:
 * all 55 shipped rules are alert/escalate/allow/log, and the AI classifier is
 * clamped to advisory. The daemon reported ENFORCE anyway, which reads as an
 * armed system.
 *
 * rules_enforcement_reachable() is the predicate that fixed that, and it
 * encodes real policy: mode must permit acting AND something must be able to
 * ask. This test pins the whole matrix, because a wrong answer here is a
 * banner that lies in one of two directions — claiming enforcement that cannot
 * happen, or warning about its absence on a system that is in fact armed.
 *
 * Rules are written to a temp dir and loaded through the real rules_load(), so
 * the parser is in the loop rather than a hand-built list.
 *
 * The last case loads the ACTUAL shipped rules/ directory and asserts the
 * stock policy is still detect-only. If a deny rule is ever added to the
 * defaults, that assertion fails on purpose: shipping enforcement to every
 * install should not be something that can happen quietly.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

/* Write one .rules file into `dir`. */
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

/* Fresh state with rules loaded from `dir`. */
static void load(synguard_state_t *s, const char *dir, sg_mode_t mode,
                 int ai_enabled, int ai_enforce)
{
    memset(s, 0, sizeof(*s));
    s->config.mode        = mode;
    s->config.ai_enabled  = ai_enabled;
    s->config.ai_enforce  = ai_enforce;
    s->rules_count = rules_load(s, dir);
}

/* Is a rule with this NAME loaded?
 *
 * A local walk rather than a new entry point in rule_engine.c: the shipped
 * policy is the only thing that needs to be asked this, and it is asked here.
 */
static int has_rule(const synguard_state_t *s, const char *name)
{
	for (const sg_rule_t *r = s->rules_head; r; r = r->next)
		if (!strcmp(r->name, name)) return 1;
	return 0;
}

int main(void)
{
    char tmpl[] = "/tmp/synguard-policy-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 2; }
    printf("policy: verdict census + enforcement reachability (%s)\n", dir);

    synguard_state_t s;
    int n[VERDICT_QUARANTINE + 1];

    /* ── Census ─────────────────────────────────────────────── */
    /* The parser is line-oriented: `}` must be on its own line and each key
     * takes one line. Written the way the shipped rule files are, so the test
     * exercises the format that actually ships. */
    write_rules(dir, "10-mixed.rules",
        "rule a {\n    comm foo\n    verdict alert\n    priority 10\n}\n"
        "rule b {\n    comm bar\n    verdict alert\n    priority 11\n}\n"
        "rule c {\n    comm baz\n    verdict escalate\n    priority 12\n}\n"
        "rule d {\n    comm qux\n    verdict allow\n    priority 13\n}\n"
        "rule e {\n    comm quux\n    verdict log\n    priority 14\n}\n");

    load(&s, dir, MODE_ENFORCE, 1, 0);
    ok("all five rules parsed", s.rules_count == 5);

    rules_census(&s, n, sizeof(n) / sizeof(n[0]));
    ok("census counts alert",    n[VERDICT_ALERT]      == 2);
    ok("census counts escalate", n[VERDICT_ESCALATE]   == 1);
    ok("census counts allow",    n[VERDICT_ALLOW]      == 1);
    ok("census counts log",      n[VERDICT_LOG]        == 1);
    ok("census shows no deny",   n[VERDICT_DENY]       == 0);
    ok("census shows no quar.",  n[VERDICT_QUARANTINE] == 0);

    /* THE case a stock install is in: enforce mode, no acting rule, advisory
     * AI. Acting must be reported unreachable. */
    ok("ENFORCE + no deny + advisory AI is UNREACHABLE",
       rules_enforcement_reachable(&s) == 0);
    rules_free(&s);

    /* An escalate rule only reaches DENY if the AI verdict is allowed to
     * stand, which needs ai_enabled AND ai_enforce. */
    load(&s, dir, MODE_ENFORCE, 1, 1);
    ok("ENFORCE + escalate + --ai-enforce is REACHABLE",
       rules_enforcement_reachable(&s) == 1);
    rules_free(&s);

    load(&s, dir, MODE_ENFORCE, 0, 1);
    ok("ENFORCE + escalate + ai_enforce but AI OFF is UNREACHABLE",
       rules_enforcement_reachable(&s) == 0);
    rules_free(&s);

    /* ── A human-written deny rule ──────────────────────────── */
    write_rules(dir, "40-enforce.rules",
        "rule kill-it {\n    path /etc/ld.so.preload\n"
        "    verdict deny\n    priority 5\n}\n");

    load(&s, dir, MODE_ENFORCE, 1, 0);
    rules_census(&s, n, sizeof(n) / sizeof(n[0]));
    ok("census counts the deny rule", n[VERDICT_DENY] == 1);
    ok("ENFORCE + a deny rule is REACHABLE",
       rules_enforcement_reachable(&s) == 1);
    rules_free(&s);

    /* Mode still gates it: AUDIT and LEARNING observe and do not act. */
    load(&s, dir, MODE_AUDIT, 1, 0);
    ok("AUDIT + a deny rule is UNREACHABLE",
       rules_enforcement_reachable(&s) == 0);
    rules_free(&s);

    load(&s, dir, MODE_LEARNING, 1, 0);
    ok("LEARNING + a deny rule is UNREACHABLE",
       rules_enforcement_reachable(&s) == 0);
    rules_free(&s);

    load(&s, dir, MODE_LOCKDOWN, 1, 0);
    ok("LOCKDOWN + a deny rule is REACHABLE",
       rules_enforcement_reachable(&s) == 1);
    rules_free(&s);

    /* A disabled rule must not count as capability — `enabled 0` is how an
     * admin parks a rule without deleting it, and a parked deny rule that
     * still reported "armed" would be the same lie in reverse. */
    unlink_rules(dir, "40-enforce.rules");
    write_rules(dir, "40-enforce.rules",
        "rule parked {\n    path /etc/ld.so.preload\n"
        "    verdict deny\n    priority 5\n    enabled 0\n}\n");

    load(&s, dir, MODE_ENFORCE, 1, 0);
    rules_census(&s, n, sizeof(n) / sizeof(n[0]));
    ok("a disabled deny rule is not counted", n[VERDICT_DENY] == 0);
    ok("a disabled deny rule is UNREACHABLE",
       rules_enforcement_reachable(&s) == 0);
    rules_free(&s);

    /* QUARANTINE is an acting verdict too. */
    unlink_rules(dir, "40-enforce.rules");
    write_rules(dir, "40-enforce.rules",
        "rule freeze-it {\n    path /dev/*\n"
        "    verdict quarantine\n    priority 5\n}\n");

    load(&s, dir, MODE_ENFORCE, 1, 0);
    rules_census(&s, n, sizeof(n) / sizeof(n[0]));
    ok("census counts the quarantine rule", n[VERDICT_QUARANTINE] == 1);
    ok("ENFORCE + a quarantine rule is REACHABLE",
       rules_enforcement_reachable(&s) == 1);
    rules_free(&s);

    /* ── Can an acting open-rule ever be told about its path? ── */
    /*
     * Reaching a deny verdict is not enough: the kmod reports opens only for
     * the prefixes in synapse_sensitive_paths[], so a deny rule naming any
     * other path loads, counts as enforceable, and can never match. That is
     * how deny-bpf-canary — the positive control whose entire job is to prove
     * enforcement fires — sat armed and inert on 2026-07-29, and why the
     * banner's "REACHABLE" was true and still misleading.
     */
    char plist[512];
    snprintf(plist, sizeof(plist), "%s/sensitive_paths", dir);

    /* The kmod's list as it stood when the canary was written: no /var/lib. */
    write_rules(dir, "sensitive_paths",
        "/etc/passwd\n/etc/shadow\n/etc/ld.so.preload\n/dev/input/\n");

    unlink_rules(dir, "40-enforce.rules");
    write_rules(dir, "40-enforce.rules",
        "rule deny-ld-preload {\n    event open\n    path /etc/ld.so.preload\n"
        "    verdict deny\n    priority 0\n}\n");
    load(&s, dir, MODE_ENFORCE, 1, 0);
    ok("a deny rule on a watched path is reachable",
       rules_report_unreachable_paths_from(&s, plist) == 0);
    rules_free(&s);

    /* THE regression: the canary's path is not watched, so it can never fire
     * and the operator must be told so. */
    unlink_rules(dir, "40-enforce.rules");
    write_rules(dir, "40-enforce.rules",
        "rule deny-bpf-canary {\n    event open\n"
        "    path /var/lib/synguard/bpf-canary\n"
        "    verdict deny\n    priority 0\n}\n");
    load(&s, dir, MODE_ENFORCE, 1, 0);
    ok("the canary on an UNWATCHED path is reported unreachable",
       rules_report_unreachable_paths_from(&s, plist) == 1);
    ok("...while the verdict census still calls enforcement REACHABLE",
       rules_enforcement_reachable(&s) == 1);
    rules_free(&s);

    /* Same rule, once the kmod publishes the prefix that makes it work. */
    unlink_rules(dir, "sensitive_paths");
    write_rules(dir, "sensitive_paths",
        "/etc/passwd\n/etc/shadow\n/etc/ld.so.preload\n/dev/input/\n"
        "/var/lib/synguard/\n");
    load(&s, dir, MODE_ENFORCE, 1, 0);
    ok("the canary IS reachable once /var/lib/synguard/ is watched",
       rules_report_unreachable_paths_from(&s, plist) == 0);
    rules_free(&s);

    /* Overlap must work in both directions. */
    unlink_rules(dir, "40-enforce.rules");
    write_rules(dir, "40-enforce.rules",
        "rule inside {\n    event open\n    path /dev/input/event0\n"
        "    verdict deny\n    priority 0\n}\n"
        "rule broader {\n    event open\n    path /etc/*\n"
        "    verdict deny\n    priority 1\n}\n");
    load(&s, dir, MODE_ENFORCE, 1, 0);
    ok("a rule inside a watched subtree, and one broader than a prefix, "
       "are both reachable",
       rules_report_unreachable_paths_from(&s, plist) == 0);
    rules_free(&s);

    /* Only ACTING rules matter. An alert rule on an unwatched path is merely
     * quiet, not a false claim of enforcement, so it must not be reported. */
    unlink_rules(dir, "40-enforce.rules");
    write_rules(dir, "40-enforce.rules",
        "rule just-alert {\n    event open\n    path /nowhere/at/all\n"
        "    verdict alert\n    priority 0\n}\n");
    load(&s, dir, MODE_ENFORCE, 1, 0);
    ok("an ALERT rule on an unwatched path is not reported",
       rules_report_unreachable_paths_from(&s, plist) == 0);
    rules_free(&s);

    /* A rule covering more than open can still act through those other
     * events, so the open filter does not render it inert. */
    unlink_rules(dir, "40-enforce.rules");
    write_rules(dir, "40-enforce.rules",
        "rule any-event {\n    event any\n    path /nowhere/at/all\n"
        "    verdict deny\n    priority 0\n}\n");
    load(&s, dir, MODE_ENFORCE, 1, 0);
    ok("a deny rule covering all events is not reported as open-unreachable",
       rules_report_unreachable_paths_from(&s, plist) == 0);
    rules_free(&s);

    /* No kmod, no list: the check is blind and must not invent a verdict. */
    unlink_rules(dir, "40-enforce.rules");
    write_rules(dir, "40-enforce.rules",
        "rule deny-bpf-canary {\n    event open\n"
        "    path /var/lib/synguard/bpf-canary\n"
        "    verdict deny\n    priority 0\n}\n");
    load(&s, dir, MODE_ENFORCE, 1, 0);
    ok("a missing prefix list reports nothing rather than guessing",
       rules_report_unreachable_paths_from(&s, "/nonexistent/sensitive_paths")
           == 0);
    rules_free(&s);

    unlink_rules(dir, "sensitive_paths");
    unlink_rules(dir, "40-enforce.rules");
    unlink_rules(dir, "10-mixed.rules");
    rmdir(dir);

    /* ── The shipped policy ─────────────────────────────────── */
    /*
     * Loads rules/ from the source tree. If this fails because a deny rule was
     * added to the defaults, that is the test doing its job: decide that
     * deliberately, then update these numbers.
     */
    load(&s, SYNGUARD_SHIPPED_RULES_DIR, MODE_ENFORCE, 1, 0);
    ok("shipped rules load", s.rules_count > 0);
    rules_census(&s, n, sizeof(n) / sizeof(n[0]));
    printf("       shipped: %d alert · %d escalate · %d allow · %d log · "
           "%d deny · %d quarantine\n",
           n[VERDICT_ALERT], n[VERDICT_ESCALATE], n[VERDICT_ALLOW],
           n[VERDICT_LOG], n[VERDICT_DENY], n[VERDICT_QUARANTINE]);

    /*
     * ⚠ THESE NUMBERS ARE THE POLICY. They were 0 and 0 — the shipped rules
     * alerted and never acted — until 50-default-deny.rules armed three of
     * them. Pinned exactly rather than as "> 0", because the interesting
     * failure is not "the policy stopped acting", it is "the policy started
     * acting on something else": a fourth acting rule appearing here is a
     * change to what SynapseOS kills on every machine, and it should not be
     * possible to make it by editing a rules file alone.
     *
     * If this fails because you added one, that is the test doing its job.
     * Decide it deliberately, write down what breaks if it is wrong, then
     * change these numbers.
     */
    ok("shipped policy carries exactly 2 deny rules",       n[VERDICT_DENY] == 2);
    ok("shipped policy carries exactly 1 quarantine rule", n[VERDICT_QUARANTINE] == 1);
    ok("shipped policy CAN act under ENFORCE",
       rules_enforcement_reachable(&s) != 0);

    /*
     * And which ones. A count alone would pass if the ld.so.preload rule were
     * swapped for a deny on something a desktop touches — the count is the
     * cheap half of this check and the names are the half that means anything.
     */
    ok("…and they are the three we chose",
       has_rule(&s, "deny-ld-preload") &&
       has_rule(&s, "deny-bpf-canary") &&
       has_rule(&s, "quarantine-exec-from-dev"));

    /*
     * The .example template must STILL stay inert. rules_load() takes only
     * *.rules, so nothing named only in the template may appear — and the
     * check has to be by NAME now that the census is no longer zero.
     * deny-unexpected-module-load is the one that matters: DKMS and the NVIDIA
     * driver both trip it, so it arriving by accident would break a routine
     * `pacman -Syu` on every machine.
     */
    ok("the .rules.example template is NOT loaded",
       !has_rule(&s, "deny-unexpected-module-load") &&
       !has_rule(&s, "deny-shell-from-sshd") &&
       !has_rule(&s, "quarantine-exec-from-dev-subdir"));
    rules_free(&s);

    printf(failures ? "policy: FAILED (%d)\n" : "policy: all passed\n",
           failures);
    return failures ? 1 : 0;
}
