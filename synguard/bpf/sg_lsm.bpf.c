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

/* Paths too deep for the buffer: bpf_d_path failed, so we could not evaluate
 * policy for them and said nothing. Visible under-enforcement, not silent. */
__u64 sg_path_too_long = 0;

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

/* ── the lowered policy ──────────────────────────────────────────────── */

struct sg_path_key { char p[SG_BPF_PATH_MAX]; };

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, SG_BPF_MAX_RULES);
	__type(key, struct sg_path_key);
	__type(value, __u32);
} sg_path_exact SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, SG_BPF_MAX_RULES);
	__type(key, struct sg_path_key);
	__type(value, __u32);
} sg_path_dir SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, SG_BPF_MAX_RULES);
	__type(key, __u32);
	__type(value, struct sg_bpf_rule);
} sg_rules SEC(".maps");

/* 256-byte buffers do not fit the 512-byte BPF stack twice over, so scratch
 * lives in a per-CPU map. Per-CPU means no locking and no cross-CPU reuse. */
struct sg_scratch {
	struct sg_path_key path;
	struct sg_path_key key;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct sg_scratch);
} sg_scratch_map SEC(".maps");

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

/* ── policy matching ─────────────────────────────────────────────────── */

/*
 * Check the conditions that are NOT the map key. Returns 1 if this rule still
 * applies. Kept separate from the lookup so the hot path — a path that matches
 * no rule at all — never reaches any of it.
 */
static __always_inline int rule_conditions_hold(const struct sg_bpf_rule *r,
						__u8 evt)
{
	if (r->evt_mask != 0xFF && !(r->evt_mask & evt))
		return 0;

	if (r->uid_match != 0xFFFFFFFFu) {
		__u32 uid = (__u32)bpf_get_current_uid_gid();
		if (uid != r->uid_match)
			return 0;
	}

	if (r->comm_kind != SG_K_ANY) {
		char comm[SG_BPF_COMM_MAX] = {};
		bpf_get_current_comm(&comm, sizeof(comm));

		__u32 n = r->comm_kind == SG_K_PREFIX ? r->comm_len
						     : SG_BPF_COMM_MAX;
		if (n > SG_BPF_COMM_MAX)
			n = SG_BPF_COMM_MAX;

		for (__u32 i = 0; i < SG_BPF_COMM_MAX; i++) {
			if (i >= n)
				break;
			if (comm[i] != r->comm[i])
				return 0;
			/* EXACT: equal NULs mean we are done and equal. */
			if (r->comm_kind == SG_K_EXACT && comm[i] == '\0')
				break;
		}
	}

	return 1;
}

/*
 * Byte loops over a 128-byte buffer are what killed the first version of this:
 * open-coded, they explode the verifier's state space (>1,000,000 insns
 * processed against a 719-instruction program). bpf_loop() runs the iteration
 * in the kernel, so the verifier checks each callback exactly once.
 */
struct scan_ctx {
	struct sg_scratch *sc;
	__u32 len;      /* strlen, NOT counting the NUL */
	int   cut;      /* index of the last '/', or -1 */
};

/*
 * One pass that both zero-fills the tail and records the last separator.
 *
 * The zero-fill is not cosmetic: these buffers are hash-map KEYS, compared
 * over their whole width, and the scratch map is reused across calls. Leave
 * the tail alone and a short path inherits the previous path's bytes, so
 * lookups miss at random — enforcement that works or not depending on what
 * was opened before it.
 */
static int scan_cb(__u32 i, void *ctx)
{
	struct scan_ctx *c = ctx;

	if (i >= SG_BPF_PATH_MAX)
		return 1;

	if (i >= c->len) {
		c->sc->path.p[i] = '\0';
		return 0;
	}
	if (c->sc->path.p[i] == '/')
		c->cut = (int)i;
	return 0;
}

/* Copy [0..cut] into the dir key and zero the rest. */
static int dirkey_cb(__u32 i, void *ctx)
{
	struct scan_ctx *c = ctx;

	if (i >= SG_BPF_PATH_MAX)
		return 1;

	c->sc->key.p[i] = (c->cut >= 0 && i <= (__u32)c->cut)
	                ? c->sc->path.p[i] : '\0';
	return 0;
}

/*
 * Resolve a path to a rule index, or -1. Two exact-hash lookups: the whole
 * path, then the directory it sits in. Cutting at the LAST '/' is what makes
 * the second lookup mean "/dir/*" and not "/dir/**" — the tail is by
 * construction separator-free, which is exactly FNM_PATHNAME's rule.
 */
static __always_inline int path_to_rule(struct sg_scratch *sc, __u32 len)
{
	struct scan_ctx c = { .sc = sc, .len = len, .cut = -1 };

	bpf_loop(SG_BPF_PATH_MAX, scan_cb, &c, 0);

	__u32 *idx = bpf_map_lookup_elem(&sg_path_exact, &sc->path);
	if (idx)
		return (int)*idx;

	if (c.cut < 0)
		return -1;

	bpf_loop(SG_BPF_PATH_MAX, dirkey_cb, &c, 0);

	idx = bpf_map_lookup_elem(&sg_path_dir, &sc->key);
	if (idx)
		return (int)*idx;

	return -1;
}

/* ── hooks ───────────────────────────────────────────────────────────── */

#define SG_EVT_OPEN 0x02   /* mirrors EVT_OPEN */

SEC("lsm/file_open")
int BPF_PROG(sg_file_open, struct file *file)
{
	__u32 zero = 0;

	/* Canary path: a test seam, kept because it is the only way to drive a
	 * denial with NO policy loaded. Checked first and cheaply. */
	__u64 *want = bpf_map_lookup_elem(&sg_canary_ino, &zero);
	if (want && *want &&
	    BPF_CORE_READ(file, f_inode, i_ino) == *want) {
		if (!sg_may_deny())
			return 0;
		return -1;
	}

	/* Policy path. */
	struct sg_scratch *sc = bpf_map_lookup_elem(&sg_scratch_map, &zero);
	if (!sc)
		return 0;

	/*
	 * No memset here — scan_cb zero-fills the tail as it goes. bpf_d_path
	 * returns the length INCLUDING the NUL, and it fails with -ENAMETOOLONG
	 * for anything longer than the buffer. That is a silent under-enforce
	 * for very deep paths, so it is counted rather than ignored: fail open,
	 * but visibly.
	 */
	long n = bpf_d_path(&file->f_path, sc->path.p, SG_BPF_PATH_MAX);
	if (n <= 0) {
		__sync_fetch_and_add(&sg_path_too_long, 1);
		return 0;		/* unresolvable path: no opinion */
	}

	int idx = path_to_rule(sc, (__u32)(n - 1));
	if (idx < 0)
		return 0;		/* the overwhelmingly common case */

	__u32 uidx = (__u32)idx;
	struct sg_bpf_rule *r = bpf_map_lookup_elem(&sg_rules, &uidx);
	if (!r)
		return 0;

	if (!rule_conditions_hold(r, SG_EVT_OPEN))
		return 0;

	/*
	 * A rule matched. Everything up to here decided WHETHER policy says to
	 * deny; sg_may_deny() decides whether we are in any state to act on it,
	 * and fails open if not.
	 */
	if (!sg_may_deny())
		return 0;

	return -1;   /* -EPERM */
}
