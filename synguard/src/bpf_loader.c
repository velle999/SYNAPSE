/*
 * bpf_loader.c — load, arm and supervise synguard's BPF-LSM gate.
 *
 * Everything here exists to make a kernel-side denial safe to turn on:
 *
 *   sg_bpf_cmdline_disabled()  the recovery path. `synapse.bpf_enforce=0` on
 *                              the kernel cmdline and we never load at all --
 *                              checked here, in userspace, because a hook
 *                              cannot rescue a boot it is already breaking.
 *
 *   sg_bpf_init()              loads the object, seeds the control record
 *                              with the gate CLOSED (enforce=0, warmup
 *                              running, no heartbeat yet), attaches, then
 *                              starts the heartbeat. Arming is a separate,
 *                              explicit sg_bpf_set_enforce(1).
 *
 *   heartbeat thread           the dead-man. A bare bpf_link already covers
 *                              synguard *dying* (the link is refcounted by
 *                              our fd, so a crash detaches the hooks). It
 *                              does not cover synguard *hanging*, which is a
 *                              failure this project has actually shipped
 *                              before, so the kernel side stops denying when
 *                              the beat goes stale.
 *
 * No pinning, no bpffs path: nothing we attach can outlive this process.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>      /* bpf_map_{lookup,update}_elem, BPF_ANY */

#include "sg_bpf.h"
#include "sg_lower.h"
#include "sg_log.h"
#include "sg_lsm.skel.h"

/* Overridden by the failsafe test so the cmdline parser can be driven against
 * a temp file instead of the real boot cmdline. */
#ifndef SG_BPF_CMDLINE_PATH
#define SG_BPF_CMDLINE_PATH "/proc/cmdline"
#endif

static struct sg_lsm_bpf *g_skel;
static pthread_t          g_hb_thread;
static volatile int       g_hb_running;
static volatile int       g_hb_paused;
static int                g_attached;
static int                g_policy_rules;   /* rules currently in the maps */

/* CLOCK_MONOTONIC on both sides -- see the clock discipline note in sg_bpf.h. */
static __u64 mono_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (__u64)ts.tv_sec * 1000000000ULL + (__u64)ts.tv_nsec;
}

/* ── cmdline escape hatch ────────────────────────────────────────────── */
/*
 * Matches `synapse.bpf_enforce=0` as a whole token, so a future
 * `synapse.bpf_enforce_foo=0` cannot be mistaken for it. Any value other
 * than 0 means "leave enforcement available"; absence means the same.
 */
int sg_bpf_cmdline_disabled(void)
{
	int fd = open(SG_BPF_CMDLINE_PATH, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;

	char buf[4096];
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';

	const size_t klen = strlen(SG_BPF_CMDLINE_KEY);
	for (char *tok = strtok(buf, " \t\n"); tok; tok = strtok(NULL, " \t\n")) {
		if (strncmp(tok, SG_BPF_CMDLINE_KEY, klen) != 0)
			continue;
		if (tok[klen] != '=')
			continue;
		if (strcmp(tok + klen + 1, "0") == 0)
			return 1;
	}
	return 0;
}

/* ── control-record access ───────────────────────────────────────────── */

static int ctl_fd(void)
{
	if (!g_skel)
		return -1;
	return bpf_map__fd(g_skel->maps.sg_control);
}

int sg_bpf_read_control(struct sg_bpf_control *out)
{
	int fd = ctl_fd();
	__u32 zero = 0;

	if (fd < 0 || !out)
		return -1;
	return bpf_map_lookup_elem(fd, &zero, out);
}

static int ctl_write(const struct sg_bpf_control *c)
{
	int fd = ctl_fd();
	__u32 zero = 0;

	if (fd < 0)
		return -1;
	return bpf_map_update_elem(fd, &zero, c, BPF_ANY);
}

int sg_bpf_set_enforce(int on)
{
	struct sg_bpf_control c;

	if (sg_bpf_read_control(&c) != 0)
		return -1;

	c.enforce = on ? 1 : 0;

	/*
	 * Re-arming restarts the grace period. A policy that was just turned
	 * back on deserves the same benefit of the doubt as one that was just
	 * attached, and it costs nothing.
	 */
	if (on)
		c.warmup_until_ns = mono_ns() + SG_BPF_WARMUP_NS;

	if (ctl_write(&c) != 0)
		return -1;

	sg_log(LOG_WARNING, "bpf-lsm: enforcement %s", on ? "ARMED" : "disarmed");
	return 0;
}

int sg_bpf_refill_budget(unsigned int budget)
{
	struct sg_bpf_control c;

	if (sg_bpf_read_control(&c) != 0)
		return -1;

	int spent = (int)(SG_BPF_DENY_BUDGET - c.deny_budget);
	c.deny_budget = budget;
	if (ctl_write(&c) != 0)
		return -1;
	return spent;
}

int sg_bpf_set_canary_ino(unsigned long long ino)
{
	__u32 zero = 0;
	__u64 v = ino;

	if (!g_skel)
		return -1;
	return bpf_map_update_elem(bpf_map__fd(g_skel->maps.sg_canary_ino),
				   &zero, &v, BPF_ANY);
}

/* ── policy population ───────────────────────────────────────────────── */

/*
 * Push a lowered policy into the maps.
 *
 * Refuses, by rule name, anything this matcher cannot evaluate exactly. The
 * lowering pass is deliberately more general than the hook: sg_pat_classify()
 * correctly lowers "/etc/sha*" to a prefix, but a partial-component prefix
 * needs an LPM trie and the hook does plain hash lookups. Approximating it
 * would broaden a DENY rule beyond what its author wrote, so it is an error.
 *
 * Population is all-or-nothing. A partially applied policy is the worst of
 * both worlds: some rules enforcing, some not, and no way to tell which.
 */
int sg_bpf_load_policy(const sg_lowered_t *rules, int n, char *err, size_t errlen)
{
	if (err && errlen) err[0] = '\0';

	if (!g_skel) {
		if (err) snprintf(err, errlen, "bpf layer not loaded");
		return -1;
	}
	if (n > SG_BPF_MAX_RULES) {
		if (err) snprintf(err, errlen,
		                  "%d enforceable rules exceeds the %d-rule map", n,
		                  SG_BPF_MAX_RULES);
		return -1;
	}

	int fd_exact = bpf_map__fd(g_skel->maps.sg_path_exact);
	int fd_dir   = bpf_map__fd(g_skel->maps.sg_path_dir);
	int fd_rules = bpf_map__fd(g_skel->maps.sg_rules);

	/* Validate everything BEFORE writing anything. */
	for (int i = 0; i < n; i++) {
		const sg_lowered_t *l = &rules[i];

		if (l->path.kind == SG_PAT_ANY) {
			if (err) snprintf(err, errlen,
			    "rule '%s': a deny rule with no path cannot be enforced on "
			    "file_open — it would match every open on the system",
			    l->rule_name);
			return -1;
		}
		if (l->path.literal_len >= SG_BPF_PATH_MAX) {
			if (err) snprintf(err, errlen, "rule '%s': path too long",
			                  l->rule_name);
			return -1;
		}
		if (l->path.kind == SG_PAT_PREFIX_NOSLASH &&
		    (l->path.literal_len == 0 ||
		     l->path.literal[l->path.literal_len - 1] != '/')) {
			if (err) snprintf(err, errlen,
			    "rule '%s': path prefix \"%s*\" must end at '/' — this "
			    "matcher does exact and directory lookups only",
			    l->rule_name, l->path.literal);
			return -1;
		}
		if (l->comm.kind == SG_PAT_PREFIX &&
		    l->comm.literal_len >= SG_BPF_COMM_MAX) {
			if (err) snprintf(err, errlen,
			    "rule '%s': comm prefix longer than comm itself",
			    l->rule_name);
			return -1;
		}
	}

	/* Clear whatever was there, so a reload cannot leave stale rules armed. */
	struct sg_path_key { char p[SG_BPF_PATH_MAX]; } k, next;
	memset(&k, 0, sizeof(k));
	for (int fd = 0; fd < 2; fd++) {
		int m = fd ? fd_dir : fd_exact;
		while (bpf_map_get_next_key(m, NULL, &next) == 0)
			if (bpf_map_delete_elem(m, &next) != 0)
				break;
	}

	for (int i = 0; i < n; i++) {
		const sg_lowered_t *l = &rules[i];
		struct sg_bpf_rule r;
		__u32 idx = (__u32)i;

		memset(&r, 0, sizeof(r));
		r.path_kind   = l->path.kind == SG_PAT_EXACT ? SG_K_EXACT : SG_K_PREFIX;
		r.evt_mask    = l->evt_mask;
		r.access_mode = (__u8)l->access_mode;
		r.uid_match   = l->uid_match;
		r.verdict     = (__u32)l->verdict;

		switch (l->comm.kind) {
		case SG_PAT_ANY:    r.comm_kind = SG_K_ANY;    break;
		case SG_PAT_EXACT:  r.comm_kind = SG_K_EXACT;  break;
		default:            r.comm_kind = SG_K_PREFIX; break;
		}
		r.comm_len = (__u32)l->comm.literal_len;
		snprintf(r.comm, sizeof(r.comm), "%s", l->comm.literal);

		if (bpf_map_update_elem(fd_rules, &idx, &r, BPF_ANY) != 0) {
			if (err) snprintf(err, errlen, "rule '%s': map update failed",
			                  l->rule_name);
			return -1;
		}

		memset(&k, 0, sizeof(k));
		memcpy(k.p, l->path.literal, l->path.literal_len);

		int m = l->path.kind == SG_PAT_EXACT ? fd_exact : fd_dir;
		if (bpf_map_update_elem(m, &k, &idx, BPF_NOEXIST) != 0) {
			if (err) snprintf(err, errlen,
			    "rule '%s': duplicate path key \"%s\" — two deny rules on "
			    "the same path cannot both be enforced",
			    l->rule_name, l->path.literal);
			return -1;
		}
	}

	g_policy_rules = n;
	sg_log(LOG_INFO, "bpf-lsm: policy loaded — %d enforceable rule%s",
	       n, n == 1 ? "" : "s");
	return n;
}

/* ── reporting ───────────────────────────────────────────────────────── */

/*
 * Why the gate would decline to deny RIGHT NOW, evaluated in userspace with
 * the same precedence sg_may_deny() uses. The gate_opens counters say what has
 * happened; this says what is true at this instant, which is what someone
 * staring at a rule that "isn't working" actually needs.
 */
static enum sg_bpf_off_reason gate_reason_now(const struct sg_bpf_control *c)
{
	if (!c->enforce)
		return SG_OFF_MASTER_SWITCH;
	if (mono_ns() < c->warmup_until_ns)
		return SG_OFF_WARMUP;
	if (!c->heartbeat_ns ||
	    mono_ns() - c->heartbeat_ns > c->heartbeat_max_ns)
		return SG_OFF_HEARTBEAT;
	if (!c->deny_budget)
		return SG_OFF_BUDGET;
	return SG_OFF_NONE;
}

static const char *reason_text(enum sg_bpf_off_reason r)
{
	switch (r) {
	case SG_OFF_NONE:          return "armed";
	case SG_OFF_MASTER_SWITCH: return "not armed";
	case SG_OFF_WARMUP:        return "warming up";
	case SG_OFF_HEARTBEAT:     return "daemon heartbeat stale";
	case SG_OFF_BUDGET:        return "deny budget spent";
	}
	return "unknown";
}

const char *sg_bpf_status(char *buf, size_t len)
{
	struct sg_bpf_control c;

	if (!g_attached || sg_bpf_read_control(&c) != 0) {
		snprintf(buf, len, "not loaded — no kernel enforcement");
		return buf;
	}

	enum sg_bpf_off_reason r = gate_reason_now(&c);
	snprintf(buf, len, "attached · %d rule%s armed · gate %s (%s)",
	         g_policy_rules, g_policy_rules == 1 ? "" : "s",
	         r == SG_OFF_NONE ? "OPEN" : "CLOSED", reason_text(r));
	return buf;
}

const char *sg_bpf_counters(char *buf, size_t len)
{
	struct sg_bpf_control c;

	if (!g_attached || sg_bpf_read_control(&c) != 0) {
		snprintf(buf, len, "bpf-lsm: not loaded");
		return buf;
	}

	/*
	 * gate_opens is broken out per reason rather than summed. "The gate was
	 * open 4000 times" is not actionable; "4000 of them were a stale
	 * heartbeat" points straight at a wedged daemon, and "all of them were
	 * budget" points at a runaway rule.
	 */
	unsigned long long longpath = g_skel->bss ? g_skel->bss->sg_path_too_long : 0;

	snprintf(buf, len,
	         "bpf-lsm: denied=%llu budget=%u opens[switch=%llu warmup=%llu "
	         "heartbeat=%llu budget=%llu] longpath=%llu",
	         (unsigned long long)c.denies_total,
	         c.deny_budget,
	         (unsigned long long)c.gate_opens[SG_OFF_MASTER_SWITCH],
	         (unsigned long long)c.gate_opens[SG_OFF_WARMUP],
	         (unsigned long long)c.gate_opens[SG_OFF_HEARTBEAT],
	         (unsigned long long)c.gate_opens[SG_OFF_BUDGET],
	         longpath);
	return buf;
}

unsigned long long sg_bpf_denies_total(void)
{
	struct sg_bpf_control c;

	if (!g_attached || sg_bpf_read_control(&c) != 0)
		return 0;
	return (unsigned long long)c.denies_total;
}

int sg_bpf_enforcement_live(void)
{
	struct sg_bpf_control c;

	if (!g_attached || sg_bpf_read_control(&c) != 0)
		return 0;
	if (!c.enforce || !c.deny_budget)
		return 0;
	if (mono_ns() < c.warmup_until_ns)
		return 0;
	return 1;
}

/* ── dead-man heartbeat ──────────────────────────────────────────────── */

static void *heartbeat_thread(void *arg)
{
	(void)arg;
	int fd = ctl_fd();
	__u32 zero = 0;

	while (g_hb_running && fd >= 0) {
		struct sg_bpf_control c;

		/* Test seam: simulate a wedged daemon without tearing the
		 * attachment down, so the staleness gate is observable. */
		if (g_hb_paused)
			goto nap;

		/*
		 * Read-modify-write rather than a blind poke: the hooks own
		 * denies_total/deny_budget/gate_opens and we must not stamp
		 * on their accounting.
		 */
		if (bpf_map_lookup_elem(fd, &zero, &c) == 0) {
			c.heartbeat_ns = mono_ns();
			bpf_map_update_elem(fd, &zero, &c, BPF_ANY);
		}

nap:
		;
		struct timespec ts = {
			.tv_sec  =  SG_BPF_HEARTBEAT_MS / 1000,
			.tv_nsec = (SG_BPF_HEARTBEAT_MS % 1000) * 1000000L,
		};
		nanosleep(&ts, NULL);
	}
	return NULL;
}

/* ── lifecycle ───────────────────────────────────────────────────────── */

static int libbpf_quiet(enum libbpf_print_level lvl, const char *fmt, va_list ap)
{
	if (lvl == LIBBPF_DEBUG)
		return 0;
	return vfprintf(stderr, fmt, ap);
}

int sg_bpf_init(void)
{
	int err;

	if (g_skel)
		return 0;

	if (sg_bpf_cmdline_disabled()) {
		sg_log(LOG_WARNING,
		       "bpf-lsm: DISABLED by kernel cmdline (%s=0) — not loading",
		       SG_BPF_CMDLINE_KEY);
		return -1;
	}

	libbpf_set_print(libbpf_quiet);

	g_skel = sg_lsm_bpf__open();
	if (!g_skel) {
		sg_log(LOG_ERR, "bpf-lsm: open failed: %s", strerror(errno));
		return -1;
	}

	err = sg_lsm_bpf__load(g_skel);
	if (err) {
		sg_log(LOG_ERR, "bpf-lsm: load failed (%d) — is CONFIG_BPF_LSM=y "
		       "and 'bpf' in /sys/kernel/security/lsm?", err);
		sg_lsm_bpf__destroy(g_skel);
		g_skel = NULL;
		return -1;
	}

	/*
	 * Seed the gate CLOSED before a single hook can run: enforcement off,
	 * grace period already ticking, heartbeat still zero. Attaching must
	 * never be the same act as arming.
	 */
	struct sg_bpf_control c;
	memset(&c, 0, sizeof(c));
	c.enforce          = 0;
	c.heartbeat_ns     = 0;
	c.heartbeat_max_ns = SG_BPF_HEARTBEAT_MAX_NS;
	c.warmup_until_ns  = mono_ns() + SG_BPF_WARMUP_NS;
	c.deny_budget      = SG_BPF_DENY_BUDGET;

	if (ctl_write(&c) != 0) {
		sg_log(LOG_ERR, "bpf-lsm: could not seed control record");
		sg_lsm_bpf__destroy(g_skel);
		g_skel = NULL;
		return -1;
	}

	err = sg_lsm_bpf__attach(g_skel);
	if (err) {
		sg_log(LOG_ERR, "bpf-lsm: attach failed (%d)", err);
		sg_lsm_bpf__destroy(g_skel);
		g_skel = NULL;
		return -1;
	}
	g_attached = 1;

	g_hb_running = 1;
	if (pthread_create(&g_hb_thread, NULL, heartbeat_thread, NULL) != 0) {
		/*
		 * Without a heartbeat the gate can never open, so this is not
		 * merely degraded -- it is a permanently inert enforcement
		 * layer pretending to be armed. Tear it back down.
		 */
		sg_log(LOG_ERR, "bpf-lsm: heartbeat thread failed — detaching");
		g_hb_running = 0;
		sg_bpf_shutdown();
		return -1;
	}

	sg_log(LOG_INFO,
	       "bpf-lsm: attached, gate CLOSED (warmup %llus, heartbeat %dms)",
	       (unsigned long long)(SG_BPF_WARMUP_NS / 1000000000ULL),
	       SG_BPF_HEARTBEAT_MS);
	return 0;
}

int sg_bpf_test_write_control(const struct sg_bpf_control *c)
{
	return ctl_write(c);
}

void sg_bpf_test_pause_heartbeat(int on)
{
	g_hb_paused = on ? 1 : 0;
}

void sg_bpf_shutdown(void)
{
	if (g_hb_running) {
		g_hb_running = 0;
		pthread_join(g_hb_thread, NULL);
	}
	if (g_skel) {
		sg_lsm_bpf__destroy(g_skel);   /* detaches every link */
		g_skel = NULL;
	}
	g_attached = 0;
	g_policy_rules = 0;
}
