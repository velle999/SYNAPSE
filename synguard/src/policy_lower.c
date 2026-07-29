/*
 * policy_lower.c — turn fnmatch rules into something a BPF hook can evaluate.
 *
 * See sg_lower.h for why every failure here is a hard error rather than a
 * skipped rule.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sg_lower.h"

const char *sg_lower_strerror(sg_lower_err_t e)
{
	switch (e) {
	case SG_LOWER_OK:              return "ok";
	case SG_LOWER_ERR_PATH_PATTERN:return "path pattern cannot be expressed in-kernel";
	case SG_LOWER_ERR_COMM_PATTERN:return "comm pattern cannot be expressed in-kernel";
	case SG_LOWER_ERR_SHADOWED:    return "an earlier rule may pre-empt it";
	case SG_LOWER_ERR_CAPACITY:    return "too many enforceable rules";
	}
	return "unknown";
}

/* ── classification ──────────────────────────────────────────────────── */

static int has_wild(const char *s)
{
	return strpbrk(s, "*?[") != NULL;
}

/* Any wildcard in the first `n` bytes? Used to confirm a trailing `*` is the
 * only one, so "a*b*" and "a?b*" are refused rather than lowered to "a". */
static int has_wild_prefix(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (s[i] == '*' || s[i] == '?' || s[i] == '[')
			return 1;
	return 0;
}

void sg_pat_classify(const char *pattern, int pathname, sg_pat_t *out)
{
	memset(out, 0, sizeof(*out));

	if (!pattern || !pattern[0]) {
		out->kind = SG_PAT_ANY;
		return;
	}

	size_t len = strlen(pattern);
	if (len >= RULE_MAX_PATTERN) {
		out->kind = SG_PAT_UNLOWERABLE;
		return;
	}

	if (!has_wild(pattern)) {
		out->kind = SG_PAT_EXACT;
		memcpy(out->literal, pattern, len);
		out->literal_len = len;
		return;
	}

	/*
	 * The only glob shape we can express is a single trailing `*` with no
	 * other wildcard anywhere. "a*b", "a?b" and "a[0-9]" all need a real
	 * matcher, so they are refused rather than approximated.
	 */
	if (pattern[len - 1] == '*' && !has_wild_prefix(pattern, len - 1)) {
		out->kind = pathname ? SG_PAT_PREFIX_NOSLASH : SG_PAT_PREFIX;
		memcpy(out->literal, pattern, len - 1);
		out->literal_len = len - 1;
		return;
	}

	out->kind = SG_PAT_UNLOWERABLE;
}

/* ── matching ────────────────────────────────────────────────────────── */

int sg_pat_matches(const sg_pat_t *p, const char *s)
{
	if (!p || !s)
		return 0;

	switch (p->kind) {
	case SG_PAT_ANY:
		return 1;

	case SG_PAT_EXACT:
		return strcmp(p->literal, s) == 0;

	case SG_PAT_PREFIX:
		return strncmp(s, p->literal, p->literal_len) == 0;

	case SG_PAT_PREFIX_NOSLASH:
		/*
		 * FNM_PATHNAME stops `*` at a separator, so the tail after the
		 * prefix must contain none. Dropping this check is what would
		 * make "/tmp/*" also match "/tmp/a/b" — a deny rule matching
		 * strictly more than its author wrote.
		 */
		if (strncmp(s, p->literal, p->literal_len) != 0)
			return 0;
		return strchr(s + p->literal_len, '/') == NULL;

	case SG_PAT_UNLOWERABLE:
	default:
		return 0;
	}
}

/* ── disjointness, used for the shadowing check ──────────────────────── */

/*
 * Conservative: returns 1 only when the two patterns provably cannot match a
 * common string. Anything it cannot reason about returns 0 ("might overlap"),
 * which makes the caller refuse to lower. Being wrong in that direction costs
 * an unlowerable rule; being wrong the other way costs a policy divergence.
 */
static int pats_disjoint(const sg_pat_t *a, const sg_pat_t *b)
{
	if (a->kind == SG_PAT_ANY || b->kind == SG_PAT_ANY)
		return 0;
	if (a->kind == SG_PAT_UNLOWERABLE || b->kind == SG_PAT_UNLOWERABLE)
		return 0;

	if (a->kind == SG_PAT_EXACT && b->kind == SG_PAT_EXACT)
		return strcmp(a->literal, b->literal) != 0;

	if (a->kind == SG_PAT_EXACT)
		return !sg_pat_matches(b, a->literal);
	if (b->kind == SG_PAT_EXACT)
		return !sg_pat_matches(a, b->literal);

	/* Two prefixes share a string iff one prefix contains the other. */
	{
		size_t n = a->literal_len < b->literal_len
		         ? a->literal_len : b->literal_len;
		return strncmp(a->literal, b->literal, n) != 0;
	}
}

/* Could rule `e` match some input that `r` also matches? Conservative: 1
 * unless some field proves otherwise. */
static int rules_may_overlap(const sg_rule_t *e, const sg_lowered_t *r)
{
	if (e->evt_mask != 0xFF && r->evt_mask != 0xFF &&
	    (e->evt_mask & r->evt_mask) == 0)
		return 0;

	if (e->uid_match != UID_ANY && r->uid_match != UID_ANY &&
	    e->uid_match != r->uid_match)
		return 0;

	sg_pat_t ec, ep;
	sg_pat_classify(e->comm_pattern, 0, &ec);
	sg_pat_classify(e->path_pattern, 1, &ep);

	if (pats_disjoint(&ec, &r->comm))
		return 0;
	if (pats_disjoint(&ep, &r->path))
		return 0;

	return 1;
}

/* ── the lowering pass ───────────────────────────────────────────────── */

int sg_lower_policy(const sg_rule_t *head, sg_lowered_t *out, int max,
                    char *err, size_t errlen)
{
	int n = 0;

	if (err && errlen)
		err[0] = '\0';

	for (const sg_rule_t *r = head; r; r = r->next) {
		if (!r->enabled)
			continue;

		/*
		 * Only DENY is lowered.
		 *
		 * alert/log/escalate stay on the userspace path where the AI
		 * and the audit log live, and `allow` deliberately lowers to
		 * nothing: an affirmative allow in an LSM hook would shortcut
		 * the chain and could override a user's AppArmor/SELinux
		 * verdict.
		 *
		 * QUARANTINE is excluded for a sharper reason. An LSM hook can
		 * return -EPERM and nothing else — it cannot SIGSTOP a subtree
		 * or move it into a frozen cgroup, which is what quarantine
		 * MEANS. Lowering it would silently turn "freeze this and keep
		 * it for me to look at" into "refuse it", destroying the
		 * evidence the admin picked quarantine to preserve. It stays
		 * on the userspace path, where sg_freeze_tree() exists.
		 */
		if (r->verdict != VERDICT_DENY)
			continue;

		if (n >= max) {
			if (err) snprintf(err, errlen, "rule '%s': %s",
			                  r->name, sg_lower_strerror(SG_LOWER_ERR_CAPACITY));
			return -1;
		}

		sg_lowered_t *l = &out[n];
		memset(l, 0, sizeof(*l));
		snprintf(l->rule_name, sizeof(l->rule_name), "%s", r->name);
		l->evt_mask    = r->evt_mask;
		l->uid_match   = r->uid_match;
		l->access_mode = r->access_mode;
		l->verdict     = r->verdict;

		sg_pat_classify(r->path_pattern, 1, &l->path);
		if (l->path.kind == SG_PAT_UNLOWERABLE) {
			if (err) snprintf(err, errlen, "rule '%s': %s: \"%s\"",
			                  r->name,
			                  sg_lower_strerror(SG_LOWER_ERR_PATH_PATTERN),
			                  r->path_pattern);
			return -1;
		}

		sg_pat_classify(r->comm_pattern, 0, &l->comm);
		if (l->comm.kind == SG_PAT_UNLOWERABLE) {
			if (err) snprintf(err, errlen, "rule '%s': %s: \"%s\"",
			                  r->name,
			                  sg_lower_strerror(SG_LOWER_ERR_COMM_PATTERN),
			                  r->comm_pattern);
			return -1;
		}

		/*
		 * Ordering. rules_evaluate() returns the FIRST match, so any
		 * earlier enabled rule with a different verdict that could
		 * match the same input would win in userspace while the kernel,
		 * which was never told about it, denies. Refuse rather than
		 * ship two engines that disagree.
		 */
		for (const sg_rule_t *e = head; e && e != r; e = e->next) {
			if (!e->enabled)
				continue;
			if (e->verdict == r->verdict)
				continue;
			if (rules_may_overlap(e, l)) {
				if (err)
					snprintf(err, errlen,
					         "rule '%s': %s (rule '%s' comes first)",
					         r->name,
					         sg_lower_strerror(SG_LOWER_ERR_SHADOWED),
					         e->name);
				return -1;
			}
		}

		n++;
	}

	return n;
}
