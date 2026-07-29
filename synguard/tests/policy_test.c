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

    ok("shipped policy carries NO deny rule",       n[VERDICT_DENY] == 0);
    ok("shipped policy carries NO quarantine rule", n[VERDICT_QUARANTINE] == 0);
    ok("shipped policy is detect-only under ENFORCE",
       rules_enforcement_reachable(&s) == 0);

    /* The .example template must stay inert: rules_load() takes only *.rules,
     * so the template's deny rules must not appear in the census above. */
    ok("the .rules.example template is NOT loaded",
       n[VERDICT_DENY] == 0 && n[VERDICT_QUARANTINE] == 0);
    rules_free(&s);

    printf(failures ? "policy: FAILED (%d)\n" : "policy: all passed\n",
           failures);
    return failures ? 1 : 0;
}
