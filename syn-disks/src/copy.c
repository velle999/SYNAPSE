/* copy.c — copying one partition onto another, byte for byte.
 *
 * ── What this is, and what it deliberately is not ──────────────────────────
 *
 * copypart takes TWO PARTITIONS THAT ALREADY EXIST and writes the contents of
 * the first over the second. It does not create the destination, and that is a
 * decision rather than an omission:
 *
 *   - creating and copying in one command is two writes with one confirmation.
 *     If the copy fails halfway — a cable, a bad block, a full destination —
 *     the drive is left holding a new partition full of half a filesystem,
 *     which is a worse state than either of the two the user chose between.
 *   - `mkpart` already makes a partition of any size in any gap, and says what
 *     it will do first. Two commands that each describe themselves beat one
 *     that describes an outcome it may only half reach.
 *
 * Whole drives are refused at both ends. Cloning a DRIVE means copying its
 * partition table as well, and a table copied byte-for-byte carries the disk
 * GUID with it — two disks claiming one identity, which is a genuinely
 * different operation and not one to arrive at by pointing this at sda.
 *
 * ── Why the source has to be quiet ─────────────────────────────────────────
 *
 * A block-level copy of a MOUNTED filesystem is a copy of a filesystem
 * mid-write: the metadata on the destination describes a state the source was
 * in at no single moment. It mounts, it usually even passes a cursory look,
 * and it is corrupt. So the source is asked about with GUARD_READ, which
 * refuses anything mounted, anything holding "/", live swap and anything with
 * a volume unlocked on top — the same four rules the destructive modes use,
 * because the reason is the same: something else is writing to it.
 *
 * The destination is asked about with GUARD_DESTROY, because that is what
 * happens to it.
 *
 * ── The UUID the copy carries ──────────────────────────────────────────────
 *
 * A byte-for-byte copy of a filesystem has the filesystem's UUID in it. With
 * both attached, anything resolving a device by UUID — /etc/fstab, the
 * initramfs, a bootloader entry — may pick either one, and which one it picks
 * is not stable across boots. That is warned about every time and blocks
 * nothing: copying a partition and then wiping the original is exactly what
 * somebody replacing a disk is doing.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>

/* Overridable so the suite can put a recorder on PATH and assert the argv
 * rather than copying a hundred gigabytes across the machine running it. */
static const char *dd_tool(void)
{
	const char *env = getenv("SYN_DISKS_DD");
	return (env && *env) ? env : "dd";
}

/* A partition, or a sentence saying why the argument is not one. The two ends
 * of this command have identical requirements, so they are checked by the same
 * code and cannot end up with different ideas of what a partition is. */
static char *resolve_part(const char *arg, const char *role)
{
	char *k = sd_kernel_name(arg);
	if (!k)
		die(_("copypart: %s is not a block device"), arg);

	if (!sd_is_partition(k)) {
		fprintf(stderr, _("%ssyn-disks: %s is a whole drive, and %s must be a "
		        "partition%s\n"), C_BAD(), arg, role, C_RESET());
		fprintf(stderr, _("  Copying a drive means copying its partition table "
		        "too, which would give two disks\n"
		        "  the same identity. Copy the partitions one at a time: "
		        "syn-disks table %s\n"), arg);
		free(k);
		return NULL;
	}
	return k;
}

int cmd_copypart(int argc, char **argv)
{
	const char *src_arg = NULL, *dst_arg = NULL;
	bool yes = false, dry = false;

	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--yes"))                              yes = true;
		else if (!strcmp(a, "--dry-run") || !strcmp(a, "-n")) dry = true;
		else if (a[0] == '-') die(_("copypart: unknown option '%s'"), a);
		else if (!src_arg)    src_arg = a;
		else if (!dst_arg)    dst_arg = a;
		else die(_("copypart: two partitions — the source and the destination "
		         "(got '%s' as well)"), a);
	}

	if (!src_arg || !dst_arg)
		die(_("copypart: need a source and a destination partition "
		    "(see: syn-disks table <disk>)"));

	char *sk = resolve_part(src_arg, "the source");
	if (!sk)
		return 1;
	char *dk = resolve_part(dst_arg, "the destination");
	if (!dk) {
		free(sk);
		return 1;
	}

	/* Kernel names, so two spellings of one device compare equal: /dev/sdb1,
	 * /dev/disk/by-uuid/… and /dev/disk/by-partuuid/… all resolve through
	 * st_rdev to the same name. Comparing the strings the user typed would let
	 * a partition be copied over itself, which zeroes it. */
	if (!strcmp(sk, dk)) {
		fprintf(stderr, _("%ssyn-disks: %s and %s are the same partition%s\n"),
		        C_BAD(), src_arg, dst_arg, C_RESET());
		free(dk);
		free(sk);
		return 1;
	}

	char *sdev = xasprintf("/dev/%s", sk);
	char *ddev = xasprintf("/dev/%s", dk);

	/* GUARD_READ on the source: its extent is not touched, but its BYTES are,
	 * and a filesystem being written to while it is read is copied in a state
	 * it was never in. */
	if (guard_refuse(sk, sdev, N_("copy"), GUARD_READ)) {
		free(ddev);
		free(sdev);
		free(dk);
		free(sk);
		return 1;
	}
	/* GUARD_DESTROY on the destination, because that is precisely what this
	 * does to it — everything on it is gone, exactly as with rmpart. */
	if (guard_refuse(dk, ddev, N_("overwrite"), GUARD_DESTROY)) {
		free(ddev);
		free(sdev);
		free(dk);
		free(sk);
		return 1;
	}

	unsigned long long sbytes = sd_size_bytes(sk);
	unsigned long long dbytes = sd_size_bytes(dk);

	if (sbytes == 0) {
		fprintf(stderr, _("%ssyn-disks: %s reports no size%s\n"),
		        C_BAD(), sdev, C_RESET());
		free(ddev);
		free(sdev);
		free(dk);
		free(sk);
		return 1;
	}

	/* Refused BEFORE anything is offered, rather than discovered by dd running
	 * out of destination halfway through. A truncated copy is not a small copy:
	 * it is a filesystem whose superblock describes blocks that are not there,
	 * and it will mount. */
	if (dbytes < sbytes) {
		char *hs = human_size(sbytes);
		char *hd = human_size(dbytes);
		char *why = xasprintf(_("%s holds %s and %s is only %s"),
		                      sdev, hs, ddev, hd);
		if (g_out == OUT_REC) {
			guard_report_refusal(ddev, why, "none");
		} else {
			fprintf(stderr, _("%ssyn-disks: refusing to copy %s onto %s — %s.%s\n"),
			        C_BAD(), sdev, ddev, why, C_RESET());
			fprintf(stderr, _("  A copy that stopped short would be a filesystem "
			        "describing blocks that are not there.\n"
			        "  Grow it first: syn-disks resize %s --size=%llu --yes\n"),
			        ddev, sbytes);
		}
		free(why);
		free(hd);
		free(hs);
		free(ddev);
		free(sdev);
		free(dk);
		free(sk);
		return 1;
	}

	/* The UUID warning is unconditional, and the size one is added to it when
	 * it applies. One `warn` field, because a front-end that had to render an
	 * arbitrary number of them would render none. */
	char *warn = xasprintf(_("the copy carries %s's filesystem UUID and label; "
	                       "with both attached, anything that finds a "
	                       "filesystem by UUID may pick either"), sdev);
	if (dbytes > sbytes) {
		char *hs = human_size(sbytes);
		char *hd = human_size(dbytes);
		char *both = xasprintf(_("%s · %s is %s and the copy is %s, so the "
		                       "filesystem inside it will still end where %s's "
		                       "does — grow it with the tool that belongs to it"),
		                       warn, ddev, hd, hs, sdev);
		free(warn);
		warn = both;
		free(hd);
		free(hs);
	}

	/* bs=4M because the default of 512 bytes makes a hundred-gigabyte copy
	 * two hundred million write syscalls, and conv=fsync because dd otherwise
	 * returns while the tail of the copy is still in the page cache — "done"
	 * arriving before the data is on the disk is how a drive gets unplugged
	 * one second too early. */
	const char *priv = priv_prefix();
	char *sif = xasprintf("if=%s", sdev);
	char *sof = xasprintf("of=%s", ddev);

	char *cmd[8];
	int c = 0;
	if (priv)
		cmd[c++] = (char *)priv;
	cmd[c++] = (char *)dd_tool();
	cmd[c++] = sif;
	cmd[c++] = sof;
	cmd[c++] = (char *)"bs=4M";
	cmd[c++] = (char *)"conv=fsync";
	cmd[c] = NULL;

	char *hs = human_size(sbytes);
	char *what = xasprintf(_("destroys everything on %s and replaces it with the "
	                       "%s on %s"), ddev, hs, sdev);

	/* No script: dd is driven entirely by its arguments, and the empty stdin
	 * it gets is what stops it reading the terminal if one of them is ever
	 * mistyped into an `if=` that does not exist. */
	int rc = pt_plan_do(cmd, NULL, ddev, what, dd_tool(), warn, yes, dry);

	if (rc == 0 && yes && !dry && g_out != OUT_REC)
		printf("%s%s%s\n", C_WARN(), warn, C_RESET());

	free(what);
	free(hs);
	free(sof);
	free(sif);
	free(warn);
	free(ddev);
	free(sdev);
	free(dk);
	free(sk);
	return rc;
}
