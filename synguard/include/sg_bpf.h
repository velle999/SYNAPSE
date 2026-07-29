/*
 * sg_bpf.h — the contract between synguard's BPF-LSM programs and the daemon.
 *
 * Included from BOTH sides:
 *   - the BPF object, which defines SG_BPF_KERNEL before including (vmlinux.h
 *     has already supplied __u32/__u64 there);
 *   - the daemon, which gets the fixed-width types from <linux/types.h>.
 *
 * ── WHY THIS FILE EXISTS ─────────────────────────────────────────────────
 * A BPF-LSM hook returning -EPERM is the only part of synguard that can stop
 * a syscall rather than mourn it. That also makes it the only part that can
 * brick a boot. Every hook therefore routes its denial through a single gate,
 * sg_may_deny(), and that gate FAILS OPEN: if anything about our own state is
 * uncertain, we return 0 ("no opinion") and let the rest of the LSM chain
 * decide. A wedged synguard must never lock velle out of the machine.
 *
 * See [[reference_bpf_lsm_available_and_additive]] for the additive-only
 * invariant these programs must preserve: return 0 or -EPERM, and NOTHING
 * else, so we can tighten a user's AppArmor/SELinux policy but never weaken it.
 */

#ifndef SG_BPF_H
#define SG_BPF_H

#ifndef SG_BPF_KERNEL
#include <linux/types.h>
#endif

/* Kernel cmdline escape hatch. Present with value 0 => never load at all.
 * This is the "I armed a bad rule and cannot log in" recovery path, so it is
 * checked in userspace BEFORE the object is loaded, not inside a hook. */
#define SG_BPF_CMDLINE_KEY     "synapse.bpf_enforce"

/* Grace period after attach during which nothing is denied. If a freshly
 * loaded policy is catastrophically wrong, the box still finishes booting and
 * an admin still gets a session in which to turn it off. */
#define SG_BPF_WARMUP_NS       (30ULL * 1000000000ULL)   /* 30s */

/* Dead-man: the daemon bumps heartbeat_ns from a dedicated thread. If the
 * kernel side sees a stale heartbeat it stops denying. Covers the case a bare
 * bpf_link cannot -- synguard ALIVE but wedged (they have shipped a
 * main-thread block before; see the synui resume deadlock). Process *death*
 * is already covered for free: the link is refcounted by our fd. */
#define SG_BPF_HEARTBEAT_MS    500
#define SG_BPF_HEARTBEAT_MAX_NS (5ULL * 1000000000ULL)   /* 10 missed beats */

/*
 * The staleness threshold must be comfortably LONGER than the beat period.
 * Set it shorter and a perfectly healthy daemon reads as wedged for part of
 * every cycle, so enforcement flickers off and on at the beat rate -- which
 * looks exactly like a policy that "sometimes doesn't work". Caught in the
 * failsafe suite when a 300ms threshold was tried against a 500ms beat.
 * Four beats is the floor; the shipped value is ten.
 */
#if (SG_BPF_HEARTBEAT_MAX_NS) < (4ULL * (SG_BPF_HEARTBEAT_MS) * 1000000ULL)
#error "SG_BPF_HEARTBEAT_MAX_NS must be >= 4x SG_BPF_HEARTBEAT_MS"
#endif

/* Runaway-policy brake. A rule that matches far more than intended disables
 * itself instead of making the desktop unusable. Refilled by the daemon each
 * time it observes and reports the spend. */
#define SG_BPF_DENY_BUDGET     64

/* Why enforcement stopped. Surfaced in the banner and the journal so a
 * silently-open gate is never mistaken for a quiet system. */
enum sg_bpf_off_reason {
	SG_OFF_NONE          = 0,
	SG_OFF_MASTER_SWITCH = 1,  /* daemon set enforce = 0 */
	SG_OFF_WARMUP        = 2,  /* still inside the post-attach grace period */
	SG_OFF_HEARTBEAT     = 3,  /* daemon wedged: heartbeat went stale */
	SG_OFF_BUDGET        = 4,  /* runaway policy spent its deny budget */
};

/*
 * The single control record, an ARRAY map of one entry. The daemon writes it;
 * the hooks only read it (plus the deny accounting).
 *
 * CLOCK DISCIPLINE: heartbeat_ns and warmup_until_ns are CLOCK_MONOTONIC on
 * both sides -- bpf_ktime_get_ns() in the kernel, clock_gettime(CLOCK_MONOTONIC)
 * in the daemon. Both stop across suspend, so they stay comparable through a
 * resume. Do NOT switch either side to a boot-clock source: the kmod already
 * got burned comparing a MONOTONIC_RAW event stamp against a boot-clock
 * /proc starttime, and the same mismatch here would make every resume look
 * like a wedged daemon.
 */
struct sg_bpf_control {
	__u64 heartbeat_ns;       /* last daemon liveness stamp */
	__u64 heartbeat_max_ns;   /* staleness threshold */
	__u64 warmup_until_ns;    /* no denials before this */
	__u64 denies_total;       /* accounting, for the banner */
	__u64 gate_opens[8];      /* per-reason count of "we declined to deny" */
	__u32 enforce;            /* master switch: 0 = observe only */
	__u32 deny_budget;        /* remaining denials before self-disable */
};

/* ── The lowered policy, as the kernel sees it ───────────────────────────
 *
 * Path is the selective dimension, so it is the map key: the overwhelming
 * majority of opens match no rule at all and must cost one hash miss and
 * nothing else. file_open is on every open() the system makes.
 *
 * Two key maps, both exact-match hashes:
 *   sg_path_exact  full path, for a rule with no wildcard
 *   sg_path_dir    a directory prefix INCLUDING its trailing '/', for a
 *                  "/dir/*" rule. The hook truncates the path at its LAST
 *                  separator to build this key, which gives FNM_PATHNAME's
 *                  one-level semantics for free — the tail cannot contain a
 *                  '/' because that is where we cut.
 *
 * Both map to an index into sg_rules, which carries the rest of the rule's
 * conditions (comm, uid, event, access) so they can be checked after the
 * cheap lookup has already excluded almost everything.
 *
 * MATCHER LIMITATION, enforced at population time: a path prefix must end at
 * a '/'. sg_pat_classify() will happily lower "/etc/sha*" — that is a correct
 * lowering and the differential test covers it — but this matcher cannot
 * evaluate a partial-component prefix with plain hash lookups, and would need
 * an LPM trie. Rather than approximate, sg_bpf_load_policy() refuses it by
 * name. Lowering says what is expressible; population says what this code can
 * actually run.
 */
#define SG_BPF_MAX_RULES   64
#define SG_BPF_PATH_MAX    256
#define SG_BPF_COMM_MAX    16

/* Mirrors sg_pat_kind_t, narrowed to what the hook evaluates. */
enum {
	SG_K_ANY    = 0,   /* no constraint */
	SG_K_EXACT  = 1,
	SG_K_PREFIX = 2,
};

struct sg_bpf_rule {
	__u8  path_kind;               /* SG_K_* */
	__u8  comm_kind;               /* SG_K_* */
	__u8  evt_mask;                /* 0xFF = any */
	__u8  access_mode;             /* sg_access_t */
	__u32 uid_match;               /* UID_ANY = 0xFFFFFFFF */
	__u32 verdict;                 /* DENY or QUARANTINE */
	__u32 comm_len;                /* prefix length when comm_kind==PREFIX */
	char  comm[SG_BPF_COMM_MAX];
};

#ifndef SG_BPF_KERNEL
/* ── Daemon-side API (src/bpf_loader.c) ──────────────────────────────────
 * All of these are safe no-ops when the BPF layer never loaded, so callers
 * do not need to branch on availability. */

/* State is a file-static in bpf_loader.c, matching secfeed_init()/close(). */

/* 1 if the kernel cmdline disables us outright. Checked before loading. */
int  sg_bpf_cmdline_disabled(void);

/* Load + attach + start the heartbeat. Returns 0 on success, <0 on failure.
 * A failure is NON-FATAL by design: synguard keeps detecting without it. */
int  sg_bpf_init(void);

/* Detach and stop the heartbeat. Idempotent. */
void sg_bpf_shutdown(void);

/* Flip the master switch. Takes effect on the very next hook invocation --
 * no detach, no reload. This is what a panic path calls. */
int  sg_bpf_set_enforce(int on);

/* Read the control record back (for the banner / `synctl`). 0 on success. */
int  sg_bpf_read_control(struct sg_bpf_control *out);

/* 1 once programs are attached AND the gate can actually reach a denial. */
int  sg_bpf_enforcement_live(void);

/* Test seam: nominate the canary inode the shipped hook will deny (0 = none).
 * Exists so the failsafe suite can drive a real denial through the real gate
 * without any policy loaded. */
int  sg_bpf_set_canary_ino(unsigned long long ino);

/* Push a lowered policy into the maps. All-or-nothing: returns the number of
 * rules armed, or -1 with `err` naming the rule this matcher cannot evaluate.
 * Declared in sg_lower.h terms, so that header must be included first. */
struct sg_lowered;
int  sg_bpf_load_policy(const struct sg_lowered *rules, int n,
                        char *err, size_t errlen);

/* Refill the runaway brake; returns how much budget had been spent. */
int  sg_bpf_refill_budget(unsigned int budget);

/* Test seams. Not for daemon use: they exist so the failsafe suite can drive
 * each gate deterministically instead of waiting out real timeouts. */
int  sg_bpf_test_write_control(const struct sg_bpf_control *c);
void sg_bpf_test_pause_heartbeat(int on);
#endif /* !SG_BPF_KERNEL */

#endif /* SG_BPF_H */
