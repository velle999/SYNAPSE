/*
 * sg_lsm.bpf.c — synguard's BPF-LSM enforcement gate.
 *
 * This object deliberately contains NO security policy yet. It carries the
 * failsafe machinery and exactly one hook, and that hook denies only an inode
 * the daemon nominates at runtime (0 = nothing). Real hooks land on top of
 * this, and every one of them must route its denial through sg_may_deny().
 *
 * ── THE TWO RULES ────────────────────────────────────────────────────────
 * 1. Return 0 or -EPERM. Nothing else, ever. 0 means "no opinion" and lets
 *    AppArmor/SELinux/Landlock reach their own verdict; it does NOT mean
 *    allow. This is what makes synguard strictly additive to a policy the
 *    user configured elsewhere.
 *
 * 2. Never deny without sg_may_deny(). It fails OPEN on every uncertainty --
 *    master switch off, still warming up, daemon heartbeat stale, deny budget
 *    spent. A hook that denies directly is a bug, not an optimisation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

#define SG_BPF_KERNEL 1
#include "sg_bpf.h"

char LICENSE[] SEC("license") = "GPL";

/* Single control record, written by the daemon. */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct sg_bpf_control);
} sg_control SEC(".maps");

/* Runtime-nominated canary inode. Lets the test suite drive a real denial
 * through the real gate without shipping a policy that touches anything. */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} sg_canary_ino SEC(".maps");

static __always_inline struct sg_bpf_control *sg_ctl(void)
{
	__u32 zero = 0;
	return bpf_map_lookup_elem(&sg_control, &zero);
}

static __always_inline void sg_note_open(struct sg_bpf_control *c, __u32 reason)
{
	if (reason < 8)
		__sync_fetch_and_add(&c->gate_opens[reason], 1);
}

/*
 * The gate. Returns 1 only when denying is safe; 0 means the caller must
 * return 0 (no opinion). Every path that returns 0 records WHY, so a gate
 * that is quietly open is visible in the banner instead of looking like a
 * system with nothing to report.
 */
static __always_inline int sg_may_deny(void)
{
	struct sg_bpf_control *c = sg_ctl();

	/* No control record at all: we know nothing, so we say nothing. */
	if (!c)
		return 0;

	if (!c->enforce) {
		sg_note_open(c, SG_OFF_MASTER_SWITCH);
		return 0;
	}

	__u64 now = bpf_ktime_get_ns();

	/* Post-attach grace period. */
	if (now < c->warmup_until_ns) {
		sg_note_open(c, SG_OFF_WARMUP);
		return 0;
	}

	/*
	 * Dead-man. A zero heartbeat means the daemon never started beating,
	 * which is just as disqualifying as a stale one.
	 */
	if (!c->heartbeat_ns ||
	    (now > c->heartbeat_ns &&
	     now - c->heartbeat_ns > c->heartbeat_max_ns)) {
		sg_note_open(c, SG_OFF_HEARTBEAT);
		return 0;
	}

	/* Runaway-policy brake. */
	if (!c->deny_budget) {
		sg_note_open(c, SG_OFF_BUDGET);
		return 0;
	}

	__sync_fetch_and_add(&c->denies_total, 1);
	__sync_fetch_and_sub(&c->deny_budget, 1);
	return 1;
}

/* ── hooks ───────────────────────────────────────────────────────────── */

SEC("lsm/file_open")
int BPF_PROG(sg_file_open, struct file *file)
{
	__u32 zero = 0;
	__u64 *want = bpf_map_lookup_elem(&sg_canary_ino, &zero);

	if (!want || !*want)
		return 0;

	if (BPF_CORE_READ(file, f_inode, i_ino) != *want)
		return 0;

	if (!sg_may_deny())
		return 0;

	return -1;   /* -EPERM */
}
