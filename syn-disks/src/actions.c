/* actions.c — the half that changes something.
 *
 * Three of these are safe and one can destroy a disk, so they are held to
 * different standards and the difference is stated here rather than left to be
 * inferred from the code.
 *
 * ── mount / unmount / eject ────────────────────────────────────────────────
 *
 * Delegated to udisksctl, never reimplemented. udisks2 owns the polkit rules
 * that let a desktop user mount a disk without being root; a disk utility that
 * called mount(2) itself would have to run privileged to do the same job
 * worse, and would then be a privileged process taking a device path from a
 * GUI. "Eject" is `udisksctl power-off`, which is the safe-removal path:
 * flush, unmount every filesystem on the drive, then tell the hardware to
 * spin down. Yanking a stick that has only been unmounted is how the last
 * write gets lost.
 *
 * ── format ─────────────────────────────────────────────────────────────────
 *
 * This one writes a new filesystem over whatever was there. It is gated by
 * four rules, and the first two have NO OVERRIDE — no --force, no environment
 * variable, no flag combination:
 *
 *   1. NOTHING MOUNTED. Not the target, and not anything else on it. mkfs on a
 *      live filesystem corrupts it while the kernel still has it cached, which
 *      means the damage surfaces minutes later as unrelated I/O errors.
 *
 *   2. NOTHING ON THE SYSTEM DISK. If the target shares a physical disk with
 *      "/", it is refused outright. This is the rule that matters most and the
 *      one most worth stating: the check walks the full stack — on this
 *      machine "/" is /dev/mapper/cryptroot over dm-0 over nvme1n1p2 over
 *      nvme1n1 — so asking to format /dev/nvme1n1p2, the encrypted container
 *      holding the running system, is refused even though nothing reports that
 *      partition itself as mounted.
 *
 *      A disk utility that can erase the disk it is running from on a
 *      mis-click has no business existing. Somebody who genuinely needs to do
 *      that has mkfs, and will have had to think about it first.
 *
 *   3. --yes IS REQUIRED. The GUI's confirmation is not the boundary; this is.
 *      A confirmation that lives only in a front-end is one that anything else
 *      calling this binary skips for free.
 *
 *   4. THE FILESYSTEM IS FROM A FIXED LIST, and the label is checked before it
 *      is passed on. Every argument here reaches an external tool through
 *      execvp with no shell anywhere in the path, but a label is still a
 *      user-supplied string arriving at a program that will write it to disk.
 *
 * mkfs needs root and there is no udisks2 call for it, so the actual write
 * goes through pkexec — which puts the authorisation in polkit's hands, where
 * it belongs, and means this program never holds privilege itself.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"

#include <stdlib.h>
#include <string.h>

/* The udisks2 tool, overridable so the test suite can put a fake on PATH and
 * assert the argv rather than mounting something. */
static const char *udisks_tool(void)
{
	const char *env = getenv("SYN_DISKS_UDISKSCTL");
	return (env && *env) ? env : "udisksctl";
}

/* A device path, never a bare name, and never one this program invented. The
 * caller's string is resolved to a kernel name first — which proves it is a
 * block device that exists — and the canonical /dev/<kname> is what is handed
 * on. Passing the user's spelling through would mean a helper receiving a
 * string that was never validated as naming what we just checked. */
static char *canonical_dev(const char *arg, char **kname_out)
{
	char *k = sd_kernel_name(arg);
	if (!k)
		die("%s: not a block device", arg);
	if (kname_out)
		*kname_out = k;
	char *dev = xasprintf("/dev/%s", k);
	if (!kname_out)
		free(k);
	return dev;
}

/* Takes a KERNEL NAME that has already been resolved, not the caller's string.
 *
 * It used to take the argument and resolve it itself, which meant eject —
 * which resolves a partition to its disk before acting — had to hand back a
 * "/dev/sdz" it had built from a name, for this function to stat and resolve
 * all over again. That is the same shape as re-deriving a row from the
 * selection instead of passing it in: it works only as long as the round trip
 * through a string happens to be lossless, and it is not. In a fixture with no
 * device nodes the second resolution simply failed, and on a real machine it
 * would have silently depended on the /dev name matching the kernel name.
 *
 * Resolve once, pass the value. */
static int udisks_op(const char *kname, const char *verb, const char *flag)
{
	const char *tool = udisks_tool();
	if (!have_cmd(tool))
		die("%s is not installed — install udisks2 to mount from here", tool);

	char *dev = xasprintf("/dev/%s", kname);

	char *argv[] = { (char *)tool, (char *)verb, (char *)flag, dev, NULL };
	int st = 0;
	char *out = run_capture(argv, &st, false);
	strip_trailing_newline(out);

	if (g_out == OUT_REC) {
		rec_row(3, "device", "status", "detail");
		rec_row(3, dev, st == 0 ? "ok" : "failed", out);
	} else if (st == 0) {
		printf("%s\n", *out ? out : "done");
	} else {
		fprintf(stderr, "%s%s%s\n", C_BAD(), *out ? out : "refused", C_RESET());
	}

	free(out);
	free(dev);
	return st == 0 ? 0 : 1;
}

/* Resolve the caller's spelling once, act, free. */
static int udisks_cmd(int argc, char **argv, const char *verb, const char *what)
{
	if (argc < 1)
		die("%s: need a %s", verb, what);
	char *k = NULL;
	char *dev = canonical_dev(argv[0], &k);
	free(dev);
	int rc = udisks_op(k, verb, "-b");
	free(k);
	return rc;
}

int cmd_mount(int argc, char **argv)
{
	return udisks_cmd(argc, argv, "mount", "device (see: syn-disks parts <disk>)");
}

int cmd_unmount(int argc, char **argv)
{
	return udisks_cmd(argc, argv, "unmount", "device (see: syn-disks parts <disk>)");
}

int cmd_eject(int argc, char **argv)
{
	if (argc < 1)
		die("eject: need a disk (see: syn-disks list)");

	char *k = sd_kernel_name(argv[0]);
	if (!k)
		die("%s: not a block device", argv[0]);

	/* power-off takes the DRIVE, not one of its partitions. Handed a
	 * partition, udisks fails with a message about the wrong object, which
	 * reads as "eject is broken" rather than "you named the wrong thing" —
	 * and ejecting the stick is plainly what was meant by clicking eject
	 * beside one of its filesystems. */
	if (sd_is_partition(k)) {
		char *parent = sd_parent_disk(k);
		if (parent) {
			free(k);
			k = parent;
		}
	}

	int rc = udisks_op(k, "power-off", "-b");
	free(k);
	return rc;
}

/* ── format ─────────────────────────────────────────────────────────────── */

/* The filesystems offered, each with the tool that makes it and the flag that
 * sets a label. A fixed table rather than "mkfs.$fs": that would turn the --fs
 * argument into a choice of which program to execute.
 *
 * Shared with partition.c through fs_all/fs_find rather than copied into it.
 * `mkpart --fs=` offers what `format --fs=` offers because it is reading the
 * same array — two lists would agree on the day they were written and drift on
 * the day somebody added a filesystem to one of them. */
static const fs_kind_t FS[] = {
	{ "ext4",  "mkfs.ext4",  "-L", "Linux, journalled" },
	{ "btrfs", "mkfs.btrfs", "-L", "Linux, snapshots and compression" },
	{ "xfs",   "mkfs.xfs",   "-L", "Linux, large files" },
	{ "vfat",  "mkfs.vfat",  "-n", "reads everywhere; no files over 4GB" },
	{ "exfat", "mkfs.exfat", "-n", "reads nearly everywhere; large files" },
	{ "ntfs",  "mkfs.ntfs",  "-L", "Windows" },
};
static const size_t NFS = sizeof FS / sizeof *FS;

const fs_kind_t *fs_all(size_t *n)
{
	*n = NFS;
	return FS;
}

const fs_kind_t *fs_find(const char *name)
{
	if (!name)
		return NULL;
	for (size_t i = 0; i < NFS; i++)
		if (!strcmp(FS[i].name, name))
			return &FS[i];
	return NULL;
}

/* A label goes to a tool that writes it into a filesystem superblock, so it is
 * checked rather than trusted. Conservative on purpose: the set below is what
 * every one of these filesystems accepts, and a rejected label costs somebody
 * a retype while a smuggled one costs them a disk. */
bool fs_label_ok(const char *s)
{
	if (!s)
		return true;
	if (strlen(s) > 32)
		return false;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		bool fine = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
		         || (*p >= '0' && *p <= '9')
		         || *p == ' ' || *p == '-' || *p == '_' || *p == '.';
		if (!fine)
			return false;
	}
	return true;
}

const char *priv_prefix(void)
{
	/* SYN_DISKS_NO_PKEXEC exists for the test suite and for a root shell,
	 * where pkexec has nothing to ask. */
	if (getenv("SYN_DISKS_NO_PKEXEC"))
		return NULL;
	const char *pkexec = getenv("SYN_DISKS_PKEXEC");
	return (pkexec && *pkexec) ? pkexec : "pkexec";
}

int fs_mkfs_argv(const fs_kind_t *fs, const char *label, const char *dev,
                 char **out)
{
	int n = 0;
	const char *priv = priv_prefix();
	if (priv)
		out[n++] = (char *)priv;
	out[n++] = (char *)fs->tool;
	if (label && *label) {
		out[n++] = (char *)fs->label_flag;
		out[n++] = (char *)label;
	}
	/* mkfs.ntfs on a large disk spends an hour zeroing unless told not to,
	 * and mkfs.vfat refuses a whole device that has no partition table unless
	 * told the target really is the device. Neither is a preference: without
	 * them the command this program has just described, and had confirmed,
	 * fails at the last step for a reason the user cannot act on. */
	if (!strcmp(fs->name, "ntfs"))
		out[n++] = (char *)"--quick";
	else if (!strcmp(fs->name, "vfat"))
		out[n++] = (char *)"-I";
	out[n++] = (char *)dev;
	out[n] = NULL;
	return n;
}

int cmd_format(int argc, char **argv)
{
	const char *target = NULL, *fs = NULL, *label = NULL;
	bool yes = false, dry = false;

	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if (!strncmp(a, "--fs=", 5))          fs = a + 5;
		else if (!strncmp(a, "--label=", 8))  label = a + 8;
		else if (!strcmp(a, "--yes"))         yes = true;
		else if (!strcmp(a, "--dry-run") || !strcmp(a, "-n")) dry = true;
		else if (a[0] == '-')                 die("format: unknown option '%s'", a);
		else if (!target)                     target = a;
		else die("format: one device at a time (got '%s' as well)", a);
	}

	if (!target)
		die("format: need a device (see: syn-disks parts <disk>)");
	if (!fs)
		die("format: need --fs=TYPE (one of: ext4 btrfs xfs vfat exfat ntfs)");

	const fs_kind_t *kind = fs_find(fs);
	if (!kind)
		die("format: '%s' is not a filesystem this offers", fs);

	if (!fs_label_ok(label))
		die("format: a label may hold up to 32 letters, digits, spaces, "
		    "'-', '_' and '.' — '%s' does not", label);

	char *k = NULL;
	char *dev = canonical_dev(target, &k);

	/* Rule 2 first, because it is the one whose failure is unrecoverable and
	 * because it is true regardless of what is mounted right now.
	 *
	 * This is deliberately WIDER than the rule partitioning uses. guard.c
	 * refuses the partitions that matter and allows the free space around
	 * them, because a partition editor that will not touch the only drive in
	 * the machine is no use to anybody. Formatting has no such excuse: it
	 * writes over a whole device, so it refuses the whole drive. */
	char *shared = guard_shares_root_disk(k);
	if (shared) {
		fprintf(stderr,
		        "%ssyn-disks: refusing to format %s — it is on /dev/%s, "
		        "the disk holding this running system.%s\n",
		        C_BAD(), dev, shared, C_RESET());
		fprintf(stderr,
		        "  There is no flag that overrides this. If you truly mean "
		        "to, use mkfs.%s directly.\n", fs);
		free(shared);
		free(dev);
		free(k);
		return 1;
	}

	char *busy = guard_mounted_under(k);
	if (busy) {
		fprintf(stderr,
		        "%ssyn-disks: refusing to format %s — something on it is "
		        "mounted at %s.%s\n", C_BAD(), dev, busy, C_RESET());
		fprintf(stderr, "  Unmount it first: syn-disks unmount %s\n", dev);
		free(busy);
		free(dev);
		free(k);
		return 1;
	}

	/* Checked, but not fatal to a dry run: "what would this do" is a question
	 * worth answering on a machine that cannot yet do it, and a GUI asking
	 * for confirmation needs the description before it can offer the button.
	 * Only an actual write is refused. */
	bool have_tool = have_cmd(kind->tool);
	if (!have_tool && yes && !dry) {
		fprintf(stderr, "syn-disks: %s is not installed — cannot make an %s "
		        "filesystem\n", kind->tool, fs);
		free(dev);
		free(k);
		return 3;
	}

	/* The command is BUILT ONCE and either printed or run, so what --dry-run
	 * shows and what --yes executes cannot drift apart. syn-settings takes
	 * the same approach for changing the bootloader, for the same reason. */
	char *cmd[10];
	fs_mkfs_argv(kind, label, dev, cmd);

	if (dry || !yes) {
		char *line = cmd_display(cmd);
		if (g_out == OUT_REC) {
			rec_row(2, "field", "value");
			rec_row(2, "device", dev);
			rec_row(2, "filesystem", fs);
			rec_row(2, "label", label ? label : "");
			rec_row(2, "command", line);
			rec_row(2, "destroys", "everything currently on this device");
			/* The GUI needs this to disable its own confirm button rather
			 * than offering an action that will fail at the last step. */
			if (!have_tool) {
				char *why = xasprintf("%s is not installed", kind->tool);
				rec_row(2, "blocked", why);
				free(why);
			}
		} else {
			printf("would run: %s\n", line);
			printf("%sthis erases everything on %s%s\n", C_WARN(), dev, C_RESET());
		}
		free(line);
		free(dev);
		free(k);
		/* Not an error when it was asked for; exit 2 when --yes was simply
		 * missing, so a caller cannot mistake "I described it" for "I did
		 * it". */
		return dry ? 0 : 2;
	}

	int st = 0;
	char *out = run_capture(cmd, &st, false);
	strip_trailing_newline(out);

	if (g_out == OUT_REC) {
		rec_row(3, "device", "status", "detail");
		rec_row(3, dev, st == 0 ? "ok" : "failed", out);
	} else if (st == 0) {
		printf("%s is now %s%s\n", dev, fs, label && *label ? " — labelled" : "");
	} else {
		fprintf(stderr, "%s%s%s\n", C_BAD(), *out ? out : "mkfs refused", C_RESET());
	}

	free(out);
	free(dev);
	free(k);
	return st == 0 ? 0 : 1;
}
