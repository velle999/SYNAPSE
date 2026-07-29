/*
 * bpf_policy_test.c — does a lowered rule actually deny the right file, and
 * only the right file?
 *
 * lower_test.c proves the lowering agrees with fnmatch in userspace. This
 * proves the KERNEL agrees too, by opening real files and checking errno. The
 * two together are what stop the written policy and the running policy from
 * drifting apart.
 *
 * The assertion that matters most is the negative one. A deny rule that
 * matches more than it should is the dangerous direction, so "/dir/*" denying
 * /dir/a while leaving /dir/sub/b alone is checked explicitly — that is
 * FNM_PATHNAME's one-level semantics, and getting it wrong would silently
 * broaden every directory rule an admin writes.
 *
 * Needs root and BPF-LSM; reports meson SKIP (77) otherwise rather than a
 * false pass.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "sg_lower.h"
#include "sg_bpf.h"

#define DIR   "/tmp/synguard-policy-test"
#define SUB   DIR "/sub"

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

static int denied(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd >= 0) { close(fd); return 0; }
	return errno == EPERM;
}

static void mkfile(const char *p)
{
	int fd = open(p, O_CREAT | O_RDWR, 0644);
	if (fd >= 0) close(fd);
}

/* Build a lowered rule directly, so this test exercises population + the
 * kernel matcher without depending on rule-file parsing. */
static sg_lowered_t mk(const char *name, const char *path, const char *comm)
{
	sg_lowered_t l;
	memset(&l, 0, sizeof(l));
	snprintf(l.rule_name, sizeof(l.rule_name), "%s", name);
	sg_pat_classify(path, 1, &l.path);
	sg_pat_classify(comm ? comm : "", 0, &l.comm);
	l.evt_mask  = 0xFF;
	l.uid_match = UID_ANY;
	l.access_mode = ACCESS_ANY;
	l.verdict   = VERDICT_DENY;
	return l;
}

/* Same, but scoped to one event type. */
static sg_lowered_t mk_evt(const char *name, const char *path, const char *comm,
                           uint8_t evt)
{
	sg_lowered_t l = mk(name, path, comm);
	l.evt_mask = evt;
	return l;
}

/* 1 if execve() was refused with EPERM. Runs in a child; the parent reads the
 * child's exit status, because a denied exec leaves the child alive in the
 * pre-exec state rather than killing it. */
static int exec_denied(const char *path)
{
	pid_t p = fork();
	if (p == 0) {
		execl(path, path, (char *)NULL);
		_exit(errno == EPERM ? 42 : 43);
	}
	int st;
	waitpid(p, &st, 0);
	return WIFEXITED(st) && WEXITSTATUS(st) == 42;
}

/* Arm past the warmup so denials can actually land. */
static int arm(void)
{
	struct sg_bpf_control c;
	if (sg_bpf_set_enforce(1) != 0) return -1;
	if (sg_bpf_read_control(&c) != 0) return -1;
	c.warmup_until_ns = 0;
	if (sg_bpf_test_write_control(&c) != 0) return -1;
	usleep(50000);
	return 0;
}

int main(void)
{
	char err[256];
	sg_lowered_t rules[4];
	int n;

	printf("synguard BPF policy matcher test\n\n");

	if (geteuid() != 0) {
		printf("SKIP: needs root\n");
		return 77;
	}

	mkdir(DIR, 0755);
	mkdir(SUB, 0755);
	mkfile(DIR "/target");
	mkfile(DIR "/other");
	mkfile(SUB "/deep");

	if (sg_bpf_init() != 0) {
		printf("SKIP: BPF-LSM unavailable\n");
		return 77;
	}

	/* ── population refuses what the matcher cannot run ──────────── */
	puts("population:");
	{
		sg_lowered_t r = mk("partial-prefix", "/etc/sha*", NULL);
		n = sg_bpf_load_policy(&r, 1, err, sizeof(err));
		ok(n < 0, "partial-component prefix refused");
		ok(strstr(err, "partial-prefix") != NULL,
		   "…error names the rule");
	}
	{
		/* A deny with no path would match every open on the system. */
		sg_lowered_t r = mk("no-path", "", NULL);
		n = sg_bpf_load_policy(&r, 1, err, sizeof(err));
		ok(n < 0, "path-less deny rule refused");
	}
	{
		sg_lowered_t r[2] = { mk("a", DIR "/target", NULL),
		                      mk("b", DIR "/target", NULL) };
		n = sg_bpf_load_policy(r, 2, err, sizeof(err));
		ok(n < 0, "duplicate path key refused");
	}

	/* ── exact path ──────────────────────────────────────────────── */
	puts("\nexact path rule:");
	rules[0] = mk("deny-target", DIR "/target", NULL);
	n = sg_bpf_load_policy(rules, 1, err, sizeof(err));
	ok(n == 1, "policy loaded");
	ok(!denied(DIR "/target"), "not denied before arming (gate closed)");

	ok(arm() == 0, "armed");
	ok(denied(DIR "/target"),  "target DENIED");
	ok(!denied(DIR "/other"),  "sibling NOT denied");
	ok(!denied(SUB "/deep"),   "file in subdir NOT denied");

	/* ── directory prefix, and the one-level rule ────────────────── */
	puts("\ndirectory prefix rule (\"" DIR "/*\"):");
	rules[0] = mk("deny-dir", DIR "/*", NULL);
	n = sg_bpf_load_policy(rules, 1, err, sizeof(err));
	ok(n == 1, "policy loaded");
	ok(arm() == 0, "armed");

	ok(denied(DIR "/target"), "direct child DENIED");
	ok(denied(DIR "/other"),  "other direct child DENIED");
	/*
	 * The whole point. FNM_PATHNAME stops `*` at a separator, so a file one
	 * level deeper is NOT covered. Getting this wrong broadens every
	 * directory rule an admin writes.
	 */
	ok(!denied(SUB "/deep"), "file one level DEEPER not denied (one-level rule)");

	/* ── comm condition ──────────────────────────────────────────── */
	puts("\ncomm condition:");
	rules[0] = mk("deny-othercomm", DIR "/target", "definitely-not-us");
	n = sg_bpf_load_policy(rules, 1, err, sizeof(err));
	ok(n == 1, "policy loaded");
	ok(arm() == 0, "armed");
	ok(!denied(DIR "/target"), "rule with a non-matching comm does NOT deny");

	{
		char self[32];
		int f = open("/proc/self/comm", O_RDONLY);
		int r = f >= 0 ? (int)read(f, self, sizeof(self) - 1) : 0;
		if (f >= 0) close(f);
		if (r > 0) {
			if (self[r - 1] == '\n') r--;
			self[r] = '\0';
			rules[0] = mk("deny-ourcomm", DIR "/target", self);
			n = sg_bpf_load_policy(rules, 1, err, sizeof(err));
			ok(n == 1, "policy loaded for our own comm");
			ok(arm() == 0, "armed");
			ok(denied(DIR "/target"), "rule matching our comm DOES deny");
		}
	}

	/* ── scratch reuse ───────────────────────────────────────────── */
	/*
	 * The per-CPU scratch buffer is a hash-map KEY and is reused across
	 * every open on the box. If the tail is not zero-filled, a short path
	 * inherits bytes from whatever long path came before it and the lookup
	 * misses — enforcement that works or not depending on open history,
	 * which would be maddening to debug and trivially exploitable.
	 */
	puts("\nscratch reuse (short path after long path):");
	{
		char deep[512];
		int off = snprintf(deep, sizeof(deep), "%s", SUB);
		for (int i = 0; i < 6 && off < (int)sizeof(deep) - 12; i++)
			off += snprintf(deep + off, sizeof(deep) - off, "/nesting%d", i);

		rules[0] = mk("deny-target", DIR "/target", NULL);
		n = sg_bpf_load_policy(rules, 1, err, sizeof(err));
		ok(n == 1, "policy loaded");
		ok(arm() == 0, "armed");

		/* Push a long path through the scratch buffer first. */
		int f = open(deep, O_RDONLY);
		if (f >= 0) close(f);

		ok(denied(DIR "/target"),
		   "short path still DENIED after a long one (tail zero-filled)");
	}

	/* ── the gate still governs policy denials ───────────────────── */
	puts("\ngate still applies to policy rules:");
	rules[0] = mk("deny-target", DIR "/target", NULL);
	sg_bpf_load_policy(rules, 1, err, sizeof(err));
	arm();
	ok(denied(DIR "/target"), "denied while armed");
	ok(sg_bpf_set_enforce(0) == 0, "disarmed");
	ok(!denied(DIR "/target"), "master switch off: policy rule FAILS OPEN");

	/* ── exec ────────────────────────────────────────────────────── */
	puts("\nexec (lsm/bprm_check_security):");
	{
		/* A real binary to run, and a second one that must be untouched. */
		if (system("cp /usr/bin/true " DIR "/prog 2>/dev/null") != 0 ||
		    system("cp /usr/bin/true " DIR "/prog2 2>/dev/null") != 0) {
			ok(0, "could not stage test binaries");
		} else {
			rules[0] = mk_evt("deny-exec", DIR "/prog", NULL, EVT_EXEC);
			n = sg_bpf_load_policy(rules, 1, err, sizeof(err));
			ok(n == 1, "exec rule loaded");
			ok(arm() == 0, "armed");

			ok(exec_denied(DIR "/prog"),  "exec of target DENIED");
			ok(!exec_denied(DIR "/prog2"), "exec of sibling NOT denied");

			/*
			 * Event discrimination. The two hooks share one path map,
			 * so a rule scoped to exec must not leak into open, or
			 * every exec rule would silently also block reading the
			 * binary — which would break far more than it protects.
			 */
			ok(!denied(DIR "/prog"),
			   "an EXEC rule does not deny OPEN of the same file");

			rules[0] = mk_evt("deny-open", DIR "/prog", NULL, EVT_OPEN);
			n = sg_bpf_load_policy(rules, 1, err, sizeof(err));
			ok(n == 1, "open rule loaded");
			ok(arm() == 0, "armed");
			ok(denied(DIR "/prog"), "…OPEN of it is denied");
			ok(!exec_denied(DIR "/prog"),
			   "an OPEN rule does not deny EXEC of the same file");

			/*
			 * comm is the CALLER at bprm_check_security time — the
			 * kernel installs the new comm later, in begin_new_exec().
			 * That is what makes "comm nginx + path /usr/bin/sh" mean
			 * "nginx ran a shell". Assert it rather than trust it.
			 */
			char self[32];
			int f = open("/proc/self/comm", O_RDONLY);
			int rr = f >= 0 ? (int)read(f, self, sizeof(self) - 1) : 0;
			if (f >= 0) close(f);
			if (rr > 0) {
				if (self[rr - 1] == '\n') rr--;
				self[rr] = '\0';

				rules[0] = mk_evt("deny-exec-bycaller", DIR "/prog",
				                  self, EVT_EXEC);
				n = sg_bpf_load_policy(rules, 1, err, sizeof(err));
				ok(n == 1, "caller-comm exec rule loaded");
				ok(arm() == 0, "armed");
				ok(exec_denied(DIR "/prog"),
				   "comm matches the CALLER, not the new binary");

				rules[0] = mk_evt("deny-exec-byprog", DIR "/prog",
				                  "prog", EVT_EXEC);
				n = sg_bpf_load_policy(rules, 1, err, sizeof(err));
				ok(n == 1, "target-name exec rule loaded");
				ok(arm() == 0, "armed");
				ok(!exec_denied(DIR "/prog"),
				   "…so a rule naming the TARGET does not match");
			}

			unlink(DIR "/prog"); unlink(DIR "/prog2");
		}
	}

	/* ── the status line must not lie ────────────────────────────── */
	/*
	 * This whole reporting surface exists because the gate fails open, and a
	 * silently open gate looks exactly like a quiet system. If the line says
	 * OPEN while nothing can be denied, it is worse than printing nothing.
	 */
	puts("\nstatus reporting:");
	{
		char st[256];

		rules[0] = mk("deny-target", DIR "/target", NULL);
		sg_bpf_load_policy(rules, 1, err, sizeof(err));

		sg_bpf_set_enforce(0);
		sg_bpf_status(st, sizeof(st));
		ok(strstr(st, "CLOSED") && strstr(st, "not armed"),
		   "disarmed -> CLOSED/not armed: %s", st);
		ok(strstr(st, "1 rule armed") != NULL, "…and reports the rule count");

		/* Armed but inside warmup: still closed, and it must say WHY. */
		sg_bpf_set_enforce(1);
		sg_bpf_status(st, sizeof(st));
		ok(strstr(st, "CLOSED") && strstr(st, "warming up"),
		   "armed in warmup -> CLOSED/warming up: %s", st);

		arm();
		sg_bpf_status(st, sizeof(st));
		ok(strstr(st, "OPEN") != NULL, "armed and warm -> OPEN: %s", st);
		ok(denied(DIR "/target"), "…and it really can deny");

		char cb[256];
		sg_bpf_counters(cb, sizeof(cb));
		ok(strstr(cb, "denied=") && strstr(cb, "heartbeat=") &&
		   strstr(cb, "longpath="),
		   "counters break opens out per reason: %s", cb);
	}

	sg_bpf_shutdown();
	ok(!denied(DIR "/target"), "after shutdown: no residue");
	{
		char st[256];
		sg_bpf_status(st, sizeof(st));
		ok(strstr(st, "not loaded") != NULL,
		   "after shutdown status says not loaded: %s", st);
	}

	unlink(DIR "/target"); unlink(DIR "/other"); unlink(SUB "/deep");
	rmdir(SUB); rmdir(DIR);

	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
