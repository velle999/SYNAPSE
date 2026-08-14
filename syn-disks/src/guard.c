/* guard.c — the rules that stop this program destroying the machine.
 *
 * Everything destructive in syn-disks asks this file first, and this file is
 * the ONLY place the rules are written down. That matters more than it looks.
 * The command line, the `table` output and the GUI all need to know whether a
 * partition may be deleted; if each worked it out for itself there would be
 * three implementations of one rule, and the day they disagreed the button
 * would be enabled for something the binary then refused — or, far worse, the
 * button would be enabled for something the binary agreed to.
 *
 * So: one function, guard_why_protected(), returning a SENTENCE rather than a
 * bool. The sentence is what the terminal prints as its refusal, what the GUI
 * prints beside the greyed-out button, and what `table` puts in its
 * `protected` column. Nothing downstream re-derives anything.
 *
 * ── Why there are two tiers ────────────────────────────────────────────────
 *
 * format refuses ANY device sharing a physical disk with "/". That is wider
 * than this file's rule and it is staying that way: writing a filesystem is
 * total, and on a mis-click the cost is the machine.
 *
 * Partitioning cannot use the same rule and still be useful. On a laptop with
 * one drive, "refuse the disk holding /" means the feature can never do
 * anything at all — and a partition editor that only works on USB sticks is
 * not a partition editor. So the rule here protects the PARTITIONS rather than
 * the drive:
 *
 *   1. Anything "/" rests on. Not just the root filesystem's own device — the
 *      whole stack under it. On this machine "/" is a btrfs inside
 *      /dev/mapper/cryptroot, over dm-0, over nvme1n1p2, over nvme1n1, and
 *      deleting nvme1n1p2 destroys the running system just as surely as
 *      formatting it does. Every rung is protected, which also means the DISK
 *      is protected, which is what makes `mktable` on the system drive
 *      impossible without a rule of its own.
 *
 *   2. Anything mounted, or with anything mounted under it. A LUKS container
 *      reports nothing mounted while the volume inside it holds five
 *      filesystems.
 *
 *   3. Anything with live swap on it. Swap is NOT in /proc/self/mounts. A
 *      guard built on mounts alone calls the swap partition idle, and the
 *      machine it was deleted from dies at the next page-out — minutes later,
 *      looking like unrelated hardware failure.
 *
 *   4. Anything with a volume unlocked or assembled on top of it. An open LUKS
 *      mapping or an LVM physical volume in a live group may have nothing
 *      mounted this second and still be very much in use.
 *
 *   5. Anything /etc/fstab expects at the next boot, matched by UUID as well
 *      as by path — a modern fstab is nothing but UUIDs, so a guard comparing
 *      paths matches nothing on it. /boot on a running machine is very often
 *      not mounted, and deleting it is not less destructive for that.
 *
 * Rule 5 is skipped for a RESIZE, and only for a resize: growing a partition
 * leaves its UUID alone, so the fstab entry naming it still resolves
 * afterwards. Rules 1 to 4 apply to everything that touches something which
 * already exists.
 *
 * MAKING a partition touches nothing that exists — it writes into space
 * nothing is using — so GUARD_ADD skips all five and checks only whether the
 * device can be written at all. Running the in-use rules over it anyway would
 * read well and destroy the feature: "/" is on the system disk, so the disk is
 * protected, so the free space at the end of the only drive in the machine
 * could never be partitioned. That is the case this whole two-tier design
 * exists to allow.
 *
 * ── No override ────────────────────────────────────────────────────────────
 *
 * There is no --force here either. Somebody who genuinely needs to delete the
 * partition their system is running from has sfdisk, and will have had to
 * think about it first.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"

#include <stdlib.h>
#include <string.h>

/* ── the stack under a device ───────────────────────────────────────────── */

static void stack_add(char ***v, size_t *n, const char *s)
{
	for (size_t i = 0; i < *n; i++)
		if (!strcmp((*v)[i], s))
			return;
	*v = xrealloc(*v, (*n + 1) * sizeof **v);
	(*v)[(*n)++] = xstrdup(s);
}

static void stack_walk(const char *kname, char ***out, size_t *n, int depth)
{
	if (depth > 16 || !kname || !*kname)
		return;
	/* A device reached twice is one device, and the guard against revisiting
	 * is also the guard against a cycle in a malformed slaves/ tree. */
	for (size_t i = 0; i < *n; i++)
		if (!strcmp((*out)[i], kname))
			return;

	stack_add(out, n, kname);

	/* Both directions down, and not one or the other.
	 *
	 * sd_base_disks returns early on slaves/ and never looks at the partition
	 * link, because it only wants the hardware at the bottom. This wants every
	 * rung, so it follows slaves/ AND the partition link — dm-0 has
	 * nvme1n1p2 as a slave, and nvme1n1p2 has nvme1n1 as its parent disk. Stop
	 * at the first and nvme1n1 is missing; stop at the second and nvme1n1p2
	 * is. */
	size_t nsl = 0;
	char **sl = sd_slaves(kname, &nsl);
	for (size_t i = 0; i < nsl; i++)
		stack_walk(sl[i], out, n, depth + 1);
	sd_free_list(sl, nsl);

	char *parent = sd_parent_disk(kname);
	if (parent) {
		stack_walk(parent, out, n, depth + 1);
		free(parent);
	}
}

char **sd_stack_under(const char *kname, size_t *n)
{
	char **out = NULL;
	*n = 0;
	stack_walk(kname, &out, n, 0);
	return out;
}

/* ── the individual rules ───────────────────────────────────────────────── */

bool guard_holds_root(const char *kname)
{
	char *rootdev = mt_root_device();
	if (!rootdev)
		return false;

	size_t n = 0;
	char **stack = sd_stack_under(rootdev, &n);
	bool hit = false;
	for (size_t i = 0; i < n && !hit; i++)
		hit = !strcmp(stack[i], kname);

	sd_free_list(stack, n);
	free(rootdev);
	return hit;
}

char *guard_shares_root_disk(const char *kname)
{
	char *rootdev = mt_root_device();
	if (!rootdev)
		return NULL;

	size_t nt = 0, nr = 0;
	char **target = sd_base_disks(kname, &nt);
	char **root = sd_base_disks(rootdev, &nr);

	char *hit = NULL;
	for (size_t i = 0; i < nt && !hit; i++)
		for (size_t j = 0; j < nr && !hit; j++)
			if (!strcmp(target[i], root[j]))
				hit = xstrdup(target[i]);

	sd_free_list(target, nt);
	sd_free_list(root, nr);
	free(rootdev);
	return hit;
}

static char *mounted_under(const char *kname, int depth)
{
	if (depth > 8)
		return NULL;

	size_t n = 0;
	char **pts = mt_points_of(kname, &n);
	if (n > 0) {
		char *first = xstrdup(pts[0]);
		sd_free_list(pts, n);
		return first;
	}
	sd_free_list(pts, n);

	size_t nh = 0;
	char **holders = sd_holders(kname, &nh);
	char *hit = NULL;
	for (size_t i = 0; i < nh && !hit; i++)
		hit = mounted_under(holders[i], depth + 1);
	sd_free_list(holders, nh);

	size_t np = 0;
	char **parts = sd_partitions(kname, &np);
	for (size_t i = 0; i < np && !hit; i++)
		hit = mounted_under(parts[i], depth + 1);
	sd_free_list(parts, np);

	return hit;
}

char *guard_mounted_under(const char *kname)
{
	return mounted_under(kname, 0);
}

/* Is `needle` this device, something built on it, or one of its partitions?
 * The same downward sweep the mount check does, reused so that swap on the
 * logical volume inside a container is found when the container is asked
 * about. */
static bool under(const char *kname, const char *needle, int depth)
{
	if (depth > 8)
		return false;
	if (!strcmp(kname, needle))
		return true;

	bool hit = false;

	size_t nh = 0;
	char **holders = sd_holders(kname, &nh);
	for (size_t i = 0; i < nh && !hit; i++)
		hit = under(holders[i], needle, depth + 1);
	sd_free_list(holders, nh);

	size_t np = 0;
	char **parts = sd_partitions(kname, &np);
	for (size_t i = 0; i < np && !hit; i++)
		hit = under(parts[i], needle, depth + 1);
	sd_free_list(parts, np);

	return hit;
}

char *guard_swap_under(const char *kname)
{
	size_t n = 0;
	char **sw = mt_swap_devices(&n);

	char *hit = NULL;
	for (size_t i = 0; i < n && !hit; i++)
		if (under(kname, sw[i], 0))
			hit = xasprintf("/dev/%s", sw[i]);

	sd_free_list(sw, n);
	return hit;
}

char *guard_holder_of(const char *kname)
{
	size_t nh = 0;
	char **holders = sd_holders(kname, &nh);
	char *out = NULL;
	if (nh > 0) {
		/* The dm name if it has one — "cryptroot" is what somebody will
		 * recognise, and "dm-0" is what they will have to go and look up. */
		char *dm = sd_attr(holders[0], "dm/name");
		out = (dm && *dm) ? xasprintf("/dev/mapper/%s", dm)
		                  : xasprintf("/dev/%s", holders[0]);
		free(dm);
	}
	sd_free_list(holders, nh);
	return out;
}

/* fstab, over the whole subtree, for the same reason as the mount check: a
 * container is not in fstab but the volume inside it is. */
static char *fstab_under(const char *kname, int depth)
{
	if (depth > 8)
		return NULL;

	char *point = mt_fstab_point(kname);
	if (point)
		return point;

	size_t nh = 0;
	char **holders = sd_holders(kname, &nh);
	char *hit = NULL;
	for (size_t i = 0; i < nh && !hit; i++)
		hit = fstab_under(holders[i], depth + 1);
	sd_free_list(holders, nh);

	size_t np = 0;
	char **parts = sd_partitions(kname, &np);
	for (size_t i = 0; i < np && !hit; i++)
		hit = fstab_under(parts[i], depth + 1);
	sd_free_list(parts, np);

	return hit;
}

/* ── the one answer ─────────────────────────────────────────────────────── */

char *guard_why_protected(const char *kname, guard_mode_t mode,
                          const char **fix)
{
	/* Set once here so no return below can forget it; the branches that have
	 * a way out overwrite it. */
	if (fix)
		*fix = "none";

	if (!kname || !*kname)
		return xstrdup("there is no such device");

	/* GUARD_ADD skips every one of these, and that is the whole reason it
	 * exists. A new partition is written into space nothing is using, so
	 * "something on this disk is mounted" is not an objection to it — and
	 * treating it as one would refuse the free space on the system drive,
	 * which on a laptop with one disk is all the free space there is. */
	if (mode != GUARD_ADD) {
		/* Order is the order of severity, because only the first is shown.
		 * Being told "it is mounted at /boot" about the partition holding the
		 * running system would be true, fixable-looking, and utterly
		 * misleading. */
		if (guard_holds_root(kname))
			return xstrdup("the running system is on it");

		char *point = guard_mounted_under(kname);
		if (point) {
			char *why = xasprintf("it is mounted at %s", point);
			free(point);
			if (fix)
				*fix = "unmount";
			return why;
		}

		char *swap = guard_swap_under(kname);
		if (swap) {
			char *why = xasprintf("%s is in use as swap", swap);
			free(swap);
			if (fix)
				*fix = "swapoff";
			return why;
		}

		char *holder = guard_holder_of(kname);
		if (holder) {
			char *why = xasprintf("%s is unlocked on top of it", holder);
			free(holder);
			if (fix)
				*fix = "lock";
			return why;
		}

		if (mode == GUARD_DESTROY) {
			char *fst = fstab_under(kname, 0);
			if (fst) {
				char *why = xasprintf("/etc/fstab expects it at %s", fst);
				free(fst);
				if (fix)
					*fix = "fstab";
				return why;
			}
		}
	}

	/* A drive the kernel has marked read-only cannot be written whatever the
	 * rules say, and finding that out from sfdisk's error after confirming a
	 * destructive dialogue is a worse way to learn it.
	 *
	 * GUARD_READ is exempt, and not as a special case: it writes nothing. A
	 * read-only device is a perfectly good source to copy FROM, and refusing it
	 * here would refuse precisely the disk somebody has write-protected in
	 * order to get the data off it safely. */
	if (mode == GUARD_READ)
		return NULL;

	char *ro = sd_attr(kname, "ro");
	bool readonly = ro && !strcmp(ro, "1");
	free(ro);
	if (readonly)
		return xstrdup("the kernel has it marked read-only");

	return NULL;
}

void guard_report_refusal(const char *dev, const char *why, const char *fix)
{
	/* A REFUSAL IS AN ANSWER, and in --rec mode it has to arrive as records.
	 *
	 * Printing it only on stderr is what left the format window with an empty
	 * dry run and a greyed-out button: the plan parser reads stdout, found
	 * nothing there, and had no idea why. The reason existed the whole time,
	 * on a stream nothing was reading. */
	if (g_out == OUT_REC) {
		rec_row(2, "field", "value");
		rec_row(2, "device", dev);
		rec_row(2, "refused", why);
		rec_row(2, "fix", fix);
		return;
	}

	fprintf(stderr, "%ssyn-disks: refusing — %s.%s\n",
	        C_BAD(), why, C_RESET());
	guard_print_fix(dev, fix);
}

void guard_print_fix(const char *dev, const char *fix)
{
	/* The way out, where there is one, keyed off the CODE and not the prose.
	 * "It is mounted" is somebody's next step; "the running system is on it"
	 * is not, and offering a fixed-with-one-command tone for that would be an
	 * invitation. */
	if (!strcmp(fix, "unmount"))
		fprintf(stderr, "  Unmount it first: syn-disks unmount %s\n", dev);
	else if (!strcmp(fix, "swapoff"))
		fprintf(stderr, "  Turn it off first: swapoff %s\n", dev);
	else if (!strcmp(fix, "lock"))
		fprintf(stderr, "  Lock it first: udisksctl lock -b %s\n", dev);
	else if (!strcmp(fix, "fstab"))
		fprintf(stderr, "  Remove its line from /etc/fstab first, or this "
		        "machine will not boot.\n");
	else
		fprintf(stderr, "  There is no flag that overrides this.\n");
}

bool guard_refuse(const char *kname, const char *dev, const char *verb,
                  guard_mode_t mode)
{
	const char *fix = "none";
	char *why = guard_why_protected(kname, mode, &fix);
	if (!why)
		return false;

	if (g_out == OUT_REC) {
		guard_report_refusal(dev, why, fix);
	} else {
		fprintf(stderr, "%ssyn-disks: refusing to %s %s — %s.%s\n",
		        C_BAD(), verb, dev, why, C_RESET());
		guard_print_fix(dev, fix);
	}

	free(why);
	return true;
}
