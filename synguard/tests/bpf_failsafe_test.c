/*
 * bpf_failsafe_test.c — the gate must fail OPEN on every uncertainty.
 *
 * These are the checks that have to pass before any real deny hook ships. The
 * interesting assertion is never "the deny worked" — it is "the deny stopped
 * working the moment we had any reason to doubt ourselves", because that is
 * what keeps a bad rule from costing velle a login.
 *
 * The cmdline half runs anywhere. The gate half needs root and a kernel with
 * BPF-LSM, and reports meson's SKIP (77) rather than a false pass otherwise —
 * an enforcement failsafe that silently "passes" because it never ran is
 * exactly the shape of bug this file exists to prevent.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>

#include "sg_bpf.h"

#define CANARY "/tmp/synguard-failsafe-canary"

static int failures;

static void ok(int cond, const char *what)
{
	printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
	if (!cond)
		failures++;
}

/* ── cmdline parser ──────────────────────────────────────────────────── */

static void write_cmdline(const char *s)
{
	int fd = open(SG_BPF_CMDLINE_PATH_TEST, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0) { perror("write_cmdline"); exit(1); }
	if (write(fd, s, strlen(s)) < 0) { perror("write"); exit(1); }
	close(fd);
}

static void test_cmdline(void)
{
	puts("cmdline escape hatch:");

	write_cmdline("root=/dev/sda2 rw quiet\n");
	ok(sg_bpf_cmdline_disabled() == 0, "absent -> enabled");

	write_cmdline("root=/dev/sda2 synapse.bpf_enforce=0 quiet\n");
	ok(sg_bpf_cmdline_disabled() == 1, "synapse.bpf_enforce=0 -> DISABLED");

	write_cmdline("synapse.bpf_enforce=1\n");
	ok(sg_bpf_cmdline_disabled() == 0, "=1 -> enabled");

	/* A prefix match would be a very bad way to lose enforcement. */
	write_cmdline("synapse.bpf_enforce_extra=0\n");
	ok(sg_bpf_cmdline_disabled() == 0, "prefix-alike key is NOT a match");

	write_cmdline("synapse.bpf_enforce\n");
	ok(sg_bpf_cmdline_disabled() == 0, "bare key without =0 -> enabled");

	write_cmdline("a=1 synapse.bpf_enforce=0\n");
	ok(sg_bpf_cmdline_disabled() == 1, "last token still matches");

	unlink(SG_BPF_CMDLINE_PATH_TEST);
}

/* ── gate behaviour ──────────────────────────────────────────────────── */

static ino_t canary_ino;

/* 1 if the canary open was denied with EPERM. */
static int canary_denied(void)
{
	int fd = open(CANARY, O_RDONLY);
	if (fd >= 0) { close(fd); return 0; }
	return errno == EPERM;
}

/* The heartbeat thread does a read-modify-write every SG_BPF_HEARTBEAT_MS, so
 * a control write can lose a race with it. Verify and retry rather than
 * assuming; a flaky failsafe test teaches people to ignore failsafe tests. */
static int put_control(struct sg_bpf_control *c)
{
	for (int i = 0; i < 20; i++) {
		struct sg_bpf_control back;
		if (sg_bpf_test_write_control(c) != 0)
			return -1;
		if (sg_bpf_read_control(&back) != 0)
			return -1;
		if (back.enforce == c->enforce &&
		    back.deny_budget == c->deny_budget &&
		    back.warmup_until_ns == c->warmup_until_ns &&
		    back.heartbeat_max_ns == c->heartbeat_max_ns)
			return 0;
		usleep(20000);
	}
	return -1;
}

static void msleep(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

/*
 * Assert the canary was allowed AND that it was allowed for the reason we
 * meant to test. Checking only "the open succeeded" once let a drained-budget
 * case pass while the gate was actually open on a stale heartbeat — the test
 * was green and the budget brake was never exercised at all. A failsafe suite
 * that cannot tell which failsafe fired is not testing failsafes.
 */
static void expect_open(int reason, const char *label)
{
	struct sg_bpf_control before, after;

	if (sg_bpf_read_control(&before) != 0) { ok(0, label); return; }
	int denied = canary_denied();
	if (sg_bpf_read_control(&after) != 0)  { ok(0, label); return; }

	if (denied) {
		printf("  [FAIL] %s (denied — gate did not open)\n", label);
		failures++;
		return;
	}
	if (after.gate_opens[reason] <= before.gate_opens[reason]) {
		printf("  [FAIL] %s (opened, but NOT for reason %d)\n", label, reason);
		failures++;
		return;
	}
	printf("  [PASS] %s\n", label);
}

static void test_gate(void)
{
	struct sg_bpf_control c;

	puts("\ngate:");

	ok(sg_bpf_set_canary_ino(canary_ino) == 0, "canary inode nominated");

	/*
	 * Staleness threshold for the test. Must stay well ABOVE the 500ms beat
	 * period or a healthy heartbeat reads as stale for part of every cycle
	 * (see the compile-time guard in sg_bpf.h). 1.2s = 2.4 beats: quick
	 * enough to keep the suite short, slack enough to be stable.
	 */
	const __u64 test_max_ns = 1200000000ULL;

	/* 1. Attached but never armed. Attaching must not enforce. */
	expect_open(SG_OFF_MASTER_SWITCH, "fresh attach: gate CLOSED (enforce=0)");
	ok(!sg_bpf_enforcement_live(), "enforcement reported NOT live");

	/* 2. Armed, but inside the post-attach grace period. */
	ok(sg_bpf_set_enforce(1) == 0, "armed");
	expect_open(SG_OFF_WARMUP, "warmup: still no denial");

	/* 3. Grace period over -> the deny finally lands. This is the only
	 *    assertion here that proves the gate can close at all; without it
	 *    every "failed open" result below would be vacuous. */
	if (sg_bpf_read_control(&c) != 0) { failures++; return; }
	c.warmup_until_ns  = 0;
	c.heartbeat_max_ns = test_max_ns;
	ok(put_control(&c) == 0, "warmup cleared");
	msleep(50);
	ok(canary_denied(), "ARMED + warm: canary DENIED (gate can close)");
	ok(sg_bpf_enforcement_live(), "enforcement reported live");

	/* 4. Dead-man: a wedged daemon must stop enforcing. The last beat may
	 *    be up to one period old already, so wait max + a full period. */
	sg_bpf_test_pause_heartbeat(1);
	msleep(1600);
	expect_open(SG_OFF_HEARTBEAT, "stale heartbeat: FAILS OPEN");

	sg_bpf_test_pause_heartbeat(0);
	msleep(1000);
	ok(canary_denied(), "heartbeat resumed: enforcing again");

	/* 5. Runaway brake. */
	if (sg_bpf_read_control(&c) != 0) { failures++; return; }
	c.deny_budget = 0;
	ok(put_control(&c) == 0, "deny budget drained");
	expect_open(SG_OFF_BUDGET, "spent budget: FAILS OPEN");

	int spent = sg_bpf_refill_budget(SG_BPF_DENY_BUDGET);
	ok(spent >= 0, "budget refilled, spend reported");
	msleep(50);
	ok(canary_denied(), "after refill: enforcing again");

	/* 6. Master switch, with no detach or reload. Re-arming restarts the
	 *    grace period, so clear it again to isolate the switch itself. */
	ok(sg_bpf_set_enforce(0) == 0, "disarmed");
	expect_open(SG_OFF_MASTER_SWITCH, "master switch off: FAILS OPEN");

	/* 7. Real denials were counted too, not just the opens. */
	if (sg_bpf_read_control(&c) == 0)
		ok(c.denies_total > 0, "counted real denials");
	else
		failures++;

	/* 8. Detach must leave nothing behind. */
	sg_bpf_shutdown();
	ok(!canary_denied(), "after shutdown: no residue");
}

int main(void)
{
	printf("synguard BPF-LSM failsafe test\n\n");

	test_cmdline();

	if (geteuid() != 0) {
		printf("\nSKIP: gate tests need root\n");
		return failures ? 1 : 77;
	}

	int fd = open(CANARY, O_CREAT | O_RDWR, 0644);
	if (fd < 0) { perror("canary"); return 1; }

	/* The inode comes from THIS descriptor, not from a second look at the
	 * name. canary_ino is the identity every verdict below is judged against,
	 * so a stat() of the name — resolved again, after the close, in a
	 * world-writable /tmp — could hand the whole test the inode of a file it
	 * did not create, and it would then pass or fail about that one. */
	struct stat st;
	if (fstat(fd, &st) != 0) { perror("fstat"); close(fd); return 1; }
	canary_ino = st.st_ino;
	close(fd);

	if (sg_bpf_init() != 0) {
		printf("\nSKIP: BPF-LSM unavailable here\n");
		unlink(CANARY);
		return failures ? 1 : 77;
	}

	test_gate();

	sg_bpf_shutdown();
	unlink(CANARY);

	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
