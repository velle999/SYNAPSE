/*
 * sg_lower.h — compiling synguard rules down to something a BPF hook can run.
 *
 * The userspace engine matches with fnmatch(3). A BPF program cannot: it gets
 * map lookups and bounded string work, nothing more. So a rule can only be
 * enforced in-kernel if its patterns lower to an exact string or a prefix.
 *
 * ── WHY THIS REFUSES INSTEAD OF SKIPPING ─────────────────────────────────
 * Two engines evaluating "the same" policy is how policies drift. Every
 * failure mode here is silent by nature: a rule that does not lower simply
 * never fires in the kernel, and the admin who wrote it has no way to tell.
 * So sg_lower_policy() treats anything it cannot express EXACTLY as a hard
 * error naming the rule, rather than lowering an approximation or dropping it.
 *
 * Two directions of drift, both refused:
 *
 *   too narrow   the lowered form matches less than fnmatch would. The rule
 *                quietly under-enforces.
 *   too broad    the lowered form matches MORE. A deny rule then kills things
 *                the written policy permits — the worse of the two, and the
 *                reason `/dir/*` cannot simply become "starts with /dir/".
 *                FNM_PATHNAME stops `*` at a slash, so the lowered form has
 *                to carry that restriction with it.
 *
 * Ordering matters just as much as patterns. rules_evaluate() takes the FIRST
 * match in priority order, so an earlier `allow` pre-empts a later `deny`. The
 * kernel side knows only about the rules it was given, so a deny rule is
 * lowerable only when no earlier rule could possibly match the same input.
 * Proving two globs disjoint is not always possible; when in doubt this
 * refuses.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SG_LOWER_H
#define SG_LOWER_H

#include "synguard.h"

/* How a pattern was expressed for the kernel. */
typedef enum {
	SG_PAT_ANY = 0,        /* empty pattern — matches everything */
	SG_PAT_EXACT,          /* no wildcards at all */
	SG_PAT_PREFIX_NOSLASH, /* trailing `*` under FNM_PATHNAME: the tail must
	                        * contain no '/'. Covers both "/etc/profile.d/*"
	                        * and "/etc/sha*". */
	SG_PAT_PREFIX,         /* trailing `*` without FNM_PATHNAME (comm): the
	                        * tail is unrestricted. */
	SG_PAT_UNLOWERABLE,    /* anything else: mid-pattern `*`, `?`, `[...]` */
} sg_pat_kind_t;

typedef struct {
	sg_pat_kind_t kind;
	char          literal[RULE_MAX_PATTERN];  /* exact string, or the prefix */
	size_t        literal_len;
} sg_pat_t;

/* One rule, in a form the kernel side can evaluate.
 * Tagged so sg_bpf.h can forward-declare it without pulling in this header. */
typedef struct sg_lowered {
	char         rule_name[RULE_MAX_NAME];
	sg_pat_t     path;
	sg_pat_t     comm;
	uint8_t      evt_mask;
	uint32_t     uid_match;
	sg_access_t  access_mode;
	sg_verdict_t verdict;      /* only DENY reaches here */
} sg_lowered_t;

/* Why a rule could not be lowered. Reported per-rule, by name. */
typedef enum {
	SG_LOWER_OK = 0,
	SG_LOWER_ERR_PATH_PATTERN,  /* path glob is not expressible */
	SG_LOWER_ERR_COMM_PATTERN,  /* comm glob is not expressible */
	SG_LOWER_ERR_SHADOWED,      /* an earlier rule may pre-empt it */
	SG_LOWER_ERR_CAPACITY,      /* more deny rules than the caller's array */
} sg_lower_err_t;

const char *sg_lower_strerror(sg_lower_err_t e);

/*
 * Classify one pattern. `pathname` selects FNM_PATHNAME semantics (true for
 * `path`, false for `comm`). Always fills *out; check out->kind.
 */
void sg_pat_classify(const char *pattern, int pathname, sg_pat_t *out);

/*
 * Evaluate a lowered pattern. MUST agree with fnmatch() on every input for
 * the pattern it came from — lower_test.c asserts exactly that against a
 * corpus, because this function existing at all is the drift risk.
 */
int sg_pat_matches(const sg_pat_t *p, const char *s);

/*
 * Lower every DENY rule in the list. QUARANTINE is NOT lowered: an LSM hook
 * can only return -EPERM, so enforcing it in-kernel would turn "freeze this
 * and keep it" into "refuse it" and destroy the evidence. Returns the number written to
 * `out`, or -1 on the first rule that cannot be lowered, with `err` filled in
 * (rule name + reason). Rules with other verdicts are skipped: alert, log and
 * escalate stay on the userspace path, and `allow` must NEVER become an
 * affirmative allow in the kernel — it lowers to nothing at all, so the LSM
 * chain keeps its say (see the additive-only invariant in sg_bpf.h).
 */
int sg_lower_policy(const sg_rule_t *head, sg_lowered_t *out, int max,
                    char *err, size_t errlen);

#endif /* SG_LOWER_H */
