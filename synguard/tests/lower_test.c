/*
 * lower_test.c — the lowered matcher must agree with fnmatch(3), always.
 *
 * policy_lower.c exists so a rule can run in two places. That is the whole
 * risk: the moment the kernel form and the userspace form disagree, the policy
 * an admin wrote is not the policy the machine runs, and nothing reports it.
 *
 * So the central test here is differential — every lowerable pattern is run
 * against a corpus of paths through BOTH sg_pat_matches() and the real
 * fnmatch() call rules_evaluate() uses, and any disagreement fails. The
 * classification tables below are secondary; they say what we intend, while
 * the differential test says whether we achieved it.
 *
 * Also guards the shipped policy: it must keep lowering to ZERO enforceable
 * rules, so arming the kernel path stays a deliberate act (same reasoning as
 * policy_test.c).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fnmatch.h>

#include "sg_lower.h"

static int failures;

static void ok(int cond, const char *fmt, ...)
{
	va_list ap;
	printf("  [%s] ", cond ? "PASS" : "FAIL");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	if (!cond)
		failures++;
}

/* ── classification ──────────────────────────────────────────────────── */

static const char *kind_name(sg_pat_kind_t k)
{
	switch (k) {
	case SG_PAT_ANY:            return "ANY";
	case SG_PAT_EXACT:          return "EXACT";
	case SG_PAT_PREFIX_NOSLASH: return "PREFIX_NOSLASH";
	case SG_PAT_PREFIX:         return "PREFIX";
	case SG_PAT_UNLOWERABLE:    return "UNLOWERABLE";
	}
	return "?";
}

static void test_classify(void)
{
	struct { const char *pat; int pathname; sg_pat_kind_t want; } cases[] = {
		/* paths (FNM_PATHNAME) */
		{ "",                    1, SG_PAT_ANY            },
		{ "/etc/shadow",         1, SG_PAT_EXACT          },
		{ "/etc/profile.d/*",    1, SG_PAT_PREFIX_NOSLASH },
		{ "/tmp/*",              1, SG_PAT_PREFIX_NOSLASH },
		{ "/etc/sha*",           1, SG_PAT_PREFIX_NOSLASH },
		/* refused: a `*` that is not the sole trailing wildcard */
		{ "/etc/*/shadow",       1, SG_PAT_UNLOWERABLE    },
		{ "/tmp/*.sh",           1, SG_PAT_UNLOWERABLE    },
		{ "/dev/input/event?",   1, SG_PAT_UNLOWERABLE    },
		{ "/etc/[sp]*",          1, SG_PAT_UNLOWERABLE    },
		{ "/a*/b*",              1, SG_PAT_UNLOWERABLE    },
		/* comms (no FNM_PATHNAME) */
		{ "",                    0, SG_PAT_ANY            },
		{ "sshd",                0, SG_PAT_EXACT          },
		{ "python*",             0, SG_PAT_PREFIX         },
		{ "a|b|c",               0, SG_PAT_EXACT          },  /* no alternation in fnmatch */
		{ "py*hon",              0, SG_PAT_UNLOWERABLE    },
	};

	puts("classification:");
	for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
		sg_pat_t p;
		sg_pat_classify(cases[i].pat, cases[i].pathname, &p);
		ok(p.kind == cases[i].want, "\"%s\" (%s) -> %s",
		   cases[i].pat, cases[i].pathname ? "path" : "comm",
		   kind_name(p.kind));
	}
}

/* ── the differential test ───────────────────────────────────────────── */

static const char *corpus[] = {
	"/etc/shadow", "/etc/shadowx", "/etc/shado", "/etc/passwd",
	"/etc/profile.d/", "/etc/profile.d/x.sh", "/etc/profile.d/a/b",
	"/etc/profile.dX", "/tmp/", "/tmp/x", "/tmp/x.sh", "/tmp/a/b",
	"/tmp", "/", "/boot/vmlinuz", "/dev/input/event0",
	"sshd", "ssh", "sshd2", "python", "python3", "py", "pyhon",
	"bash", "", "a|b|c",
};

static void diff_one(const char *pattern, int pathname)
{
	sg_pat_t p;
	sg_pat_classify(pattern, pathname, &p);

	/* Unlowerable patterns are never handed to the kernel, so there is
	 * nothing to agree about. */
	if (p.kind == SG_PAT_UNLOWERABLE)
		return;

	int flags = FNM_NOESCAPE | (pathname ? FNM_PATHNAME : 0);

	for (size_t i = 0; i < sizeof(corpus)/sizeof(corpus[0]); i++) {
		const char *s = corpus[i];
		int lowered = sg_pat_matches(&p, s);
		int real;

		if (!pattern[0])
			real = 1;              /* empty pattern = match any */
		else
			real = fnmatch(pattern, s, flags) == 0;

		if (lowered != real) {
			printf("  [FAIL] \"%s\" vs \"%s\": lowered=%d fnmatch=%d\n",
			       pattern, s, lowered, real);
			failures++;
		}
	}
}

static void test_differential(void)
{
	static const char *path_pats[] = {
		"", "/etc/shadow", "/etc/profile.d/*", "/tmp/*", "/etc/sha*",
		"/*", "/tmp", "/boot/*", "/dev/input/*",
	};
	static const char *comm_pats[] = {
		"", "sshd", "python*", "ssh*", "bash", "a|b|c", "p*",
	};

	puts("\ndifferential vs fnmatch(3):");
	int before = failures;

	for (size_t i = 0; i < sizeof(path_pats)/sizeof(path_pats[0]); i++)
		diff_one(path_pats[i], 1);
	for (size_t i = 0; i < sizeof(comm_pats)/sizeof(comm_pats[0]); i++)
		diff_one(comm_pats[i], 0);

	ok(failures == before,
	   "every lowered pattern agrees with fnmatch over %zu paths",
	   sizeof(corpus)/sizeof(corpus[0]));
}

/* ── lowering a rule list ────────────────────────────────────────────── */

static sg_rule_t *mkrule(const char *name, const char *comm, const char *path,
                         sg_verdict_t v, sg_rule_t *next)
{
	sg_rule_t *r = calloc(1, sizeof(*r));
	snprintf(r->name, sizeof(r->name), "%s", name);
	snprintf(r->comm_pattern, sizeof(r->comm_pattern), "%s", comm ? comm : "");
	snprintf(r->path_pattern, sizeof(r->path_pattern), "%s", path ? path : "");
	r->verdict  = v;
	r->evt_mask = 0xFF;
	r->uid_match = UID_ANY;
	r->access_mode = ACCESS_ANY;
	r->enabled  = 1;
	r->next     = next;
	return r;
}

static void free_rules(sg_rule_t *r)
{
	while (r) { sg_rule_t *n = r->next; free(r); r = n; }
}

static void test_lowering(void)
{
	sg_lowered_t out[16];
	char err[256];
	int n;

	puts("\nlowering:");

	/* Only denials lower. allow/alert/log/escalate must produce nothing —
	 * an affirmative allow in an LSM hook could override AppArmor/SELinux. */
	{
		sg_rule_t *r = mkrule("a", "synguard", NULL, VERDICT_ALLOW,
		              mkrule("b", NULL, "/etc/shadow", VERDICT_ALERT,
		              mkrule("c", NULL, NULL, VERDICT_LOG, NULL)));
		n = sg_lower_policy(r, out, 16, err, sizeof(err));
		ok(n == 0, "no deny rules -> 0 lowered (got %d)", n);
		free_rules(r);
	}

	/* A plain deny with an exact path lowers. */
	{
		sg_rule_t *r = mkrule("kill-shadow", NULL, "/etc/shadow",
		                      VERDICT_DENY, NULL);
		n = sg_lower_policy(r, out, 16, err, sizeof(err));
		ok(n == 1, "exact-path deny lowers (got %d)", n);
		ok(n == 1 && out[0].path.kind == SG_PAT_EXACT, "…as EXACT");
		free_rules(r);
	}

	/* An unlowerable glob is a HARD ERROR, not a skip. */
	{
		sg_rule_t *r = mkrule("bad", NULL, "/etc/*/shadow",
		                      VERDICT_DENY, NULL);
		n = sg_lower_policy(r, out, 16, err, sizeof(err));
		ok(n < 0, "unlowerable deny rule -> hard error");
		ok(strstr(err, "bad") != NULL, "error names the rule: %s", err);
	 	free_rules(r);
	}

	/* Ordering: an earlier allow that could pre-empt the deny must refuse. */
	{
		sg_rule_t *r = mkrule("allow-all-bash", "bash", NULL, VERDICT_ALLOW,
		              mkrule("deny-shadow", NULL, "/etc/shadow",
		                     VERDICT_DENY, NULL));
		n = sg_lower_policy(r, out, 16, err, sizeof(err));
		ok(n < 0, "deny shadowed by an earlier allow -> refused");
		ok(strstr(err, "allow-all-bash") != NULL,
		   "error names the shadowing rule: %s", err);
		free_rules(r);
	}

	/* …but a provably disjoint earlier rule must NOT block lowering, or
	 * the check would refuse everything and be useless. */
	{
		sg_rule_t *r = mkrule("allow-passwd", NULL, "/etc/passwd", VERDICT_ALLOW,
		              mkrule("deny-shadow", NULL, "/etc/shadow",
		                     VERDICT_DENY, NULL));
		n = sg_lower_policy(r, out, 16, err, sizeof(err));
		ok(n == 1, "disjoint earlier rule does not block lowering (got %d: %s)",
		   n, n < 0 ? err : "");
		free_rules(r);
	}
}

/* ── the shipped policy ──────────────────────────────────────────────── */

extern int rules_load(synguard_state_t *s, const char *dir);

static void test_shipped(void)
{
	synguard_state_t s;
	sg_lowered_t out[64];
	char err[256];

	puts("\nshipped policy:");

	memset(&s, 0, sizeof(s));
	pthread_rwlock_init(&s.rules_lock, NULL);

	int loaded = rules_load(&s, SYNGUARD_SHIPPED_RULES_DIR);
	ok(loaded > 0, "shipped rules load (%d)", loaded);

	int n = sg_lower_policy(s.rules_head, out, 64, err, sizeof(err));

	/*
	 * Zero, and not because lowering failed. A -1 here would also mean "no
	 * kernel enforcement", but for the opposite reason, and that difference
	 * is the whole point of the guard.
	 */
	ok(n == 0, "shipped policy lowers to ZERO enforceable rules (got %d%s%s)",
	   n, n < 0 ? ": " : "", n < 0 ? err : "");
}

int main(void)
{
	printf("synguard policy-lowering test\n\n");

	test_classify();
	test_differential();
	test_lowering();
	test_shipped();

	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
