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
 *   1. WHATEVER guard.c PROTECTS, asked as GUARD_DESTROY — because that is
 *      what this is. Nothing mounted (not the target, and not anything else on
 *      it: mkfs on a live filesystem corrupts it while the kernel still has it
 *      cached, and the damage surfaces minutes later as unrelated I/O errors),
 *      no live swap, nothing unlocked on top, nothing /etc/fstab expects, and
 *      not a device the kernel has marked read-only.
 *
 *      That rule is ASKED FOR and not copied. This file used to carry its own
 *      mount check, and the rules it did not carry were rules format did not
 *      have: a write-protected stick went through the dry run, the
 *      confirmation and polkit before mke2fs answered "Read-only file system
 *      while setting up superblock", which reads like a dead stick and means a
 *      switch on the side of one.
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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

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
	{ "ext4",  "mkfs.ext4",  "-L", "Linux, journalled",                    "ext4" },
	{ "btrfs", "mkfs.btrfs", "-L", "Linux, snapshots and compression",     "btrfs" },
	{ "xfs",   "mkfs.xfs",   "-L", "Linux, large files",                   "xfs" },
	{ "vfat",  "mkfs.vfat",  "-n", "reads everywhere; no files over 4GB",  "vfat" },
	{ "exfat", "mkfs.exfat", "-n", "reads nearly everywhere; large files", "exfat" },
	/* Two drivers read NTFS: ntfs3 (read-write, current) and the old
	 * read-only ntfs. Either one means the result will mount. */
	{ "ntfs",  "mkfs.ntfs",  "-L", "Windows",                              "ntfs3 ntfs" },
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

/* Is `word` one of the whitespace-separated tokens in `text`? Token-wise and
 * not strstr: "ntfs" is a substring of "ntfs3", so a substring match would
 * report the old read-only driver present on every machine that has the new
 * one, and — worse — the reverse on a machine that has neither. */
static bool has_token(const char *text, const char *word)
{
	if (!text || !word || !*word)
		return false;
	size_t n = strlen(word);
	for (const char *p = text; (p = strstr(p, word)); p += n) {
		bool left  = (p == text) || isspace((unsigned char)p[-1]);
		bool right = !p[n] || isspace((unsigned char)p[n]);
		if (left && right)
			return true;
	}
	return false;
}

/* /proc/filesystems lists what the kernel can mount RIGHT NOW — built in, or a
 * module already loaded. Lines are "nodev\text3" or "\text4", so the driver is
 * the last field; has_token over the whole file is enough and does not care
 * which. */
static bool fs_in_proc(const char *drivers)
{
	const char *path = getenv("SYN_DISKS_FILESYSTEMS");
	char *text = slurp(path && *path ? path : "/proc/filesystems");
	if (!text)
		return false;

	bool found = false;
	size_t n = 0;
	char *copy = xstrdup(drivers);
	char **drv = split(copy, ' ', &n);
	for (size_t i = 0; i < n && !found; i++)
		if (*drv[i])
			found = has_token(text, drv[i]);
	free(drv);
	free(copy);
	free(text);
	return found;
}

/* Not loaded yet is not the same as unavailable: the module may simply never
 * have been asked for. modules.dep lists every module the running kernel has,
 * one path per line, so it answers "could this be loaded" without loading it
 * — which needs root and would be a side effect of describing a plan. */
static bool fs_module_exists(const char *drivers, bool *tree_missing)
{
	*tree_missing = false;

	const char *dir = getenv("SYN_DISKS_MODULES");
	char *path;
	if (dir && *dir) {
		path = xasprintf("%s/modules.dep", dir);
	} else {
		struct utsname u;
		if (uname(&u) != 0)
			return false;
		path = xasprintf("/usr/lib/modules/%s/modules.dep", u.release);
	}

	char *text = slurp(path);
	free(path);
	if (!text) {
		/* The running kernel's module tree is GONE. This is what a kernel
		 * upgrade with no reboot leaves behind, and it is worth telling
		 * apart from "this kernel was built without the driver": one is
		 * fixed by rebooting and the other is not. */
		*tree_missing = true;
		return false;
	}

	bool found = false;
	size_t n = 0;
	char *copy = xstrdup(drivers);
	char **drv = split(copy, ' ', &n);
	for (size_t i = 0; i < n && !found; i++) {
		if (!*drv[i])
			continue;
		/* "/exfat.ko", so a driver cannot be matched by another whose name
		 * merely ends the same way (ntfs3 vs ntfs). The suffix varies with
		 * the kernel's compression — .ko, .ko.zst, .ko.xz — so it is left
		 * off and the leading slash carries the anchoring. */
		char *needle = xasprintf("/%s.ko", drv[i]);
		found = strstr(text, needle) != NULL;
		free(needle);
	}
	free(drv);
	free(copy);
	free(text);
	return found;
}

bool fs_kernel_can_mount(const fs_kind_t *fs, char **why)
{
	if (why)
		*why = NULL;
	if (!fs || !fs->kmod || !*fs->kmod)
		return true;

	if (fs_in_proc(fs->kmod))
		return true;

	bool tree_missing = false;
	if (fs_module_exists(fs->kmod, &tree_missing))
		return true;

	if (why) {
		if (tree_missing)
			*why = xasprintf("this kernel can no longer load modules — its "
			                 "module tree is gone, which is what a kernel "
			                 "upgrade with no reboot leaves behind. %s will "
			                 "be created correctly but will not mount here "
			                 "until you reboot", fs->name);
		else
			*why = xasprintf("this kernel cannot mount %s — it will be "
			                 "created correctly, but nothing here will read "
			                 "it", fs->name);
	}
	return false;
}

/* A tool's output with the line that says WHY moved to the front.
 *
 * mkfs tools narrate: a version banner, the geometry, the UUID, the superblock
 * backups, the group tables, and then — on the last line, where they died —
 * the reason. A front-end with one line to show puts the banner in it and
 * elides the rest, which is how "could not format /dev/sde — mke2fs 1.47.4
 * (6-Mar-2025) · Creating filesystem with 1792000 4k blocks…" came to be the
 * entire report of a write-protected stick. The banner is not a verdict, and
 * the verdict was the part that got cut.
 *
 * Nothing is dropped, because a tool that puts its reason somewhere else must
 * still be quoted in full: the last line leads and the rest follows in the
 * order it was printed. */
char *reason_first(const char *out)
{
	if (!out || !*out)
		return xstrdup("");

	const char *last = NULL;
	size_t last_len = 0;
	for (const char *p = out; *p; ) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		size_t trimmed = len;
		while (trimmed && (p[trimmed - 1] == ' ' || p[trimmed - 1] == '\t' ||
		                   p[trimmed - 1] == '\r'))
			trimmed--;
		const char *start = p;
		while (trimmed && (*start == ' ' || *start == '\t')) {
			start++;
			trimmed--;
		}
		if (trimmed) {
			last = start;
			last_len = trimmed;
		}
		if (!nl)
			break;
		p = nl + 1;
	}

	/* One line, or none worth naming: it is already in front of itself. */
	if (!last || (last == out && last_len == strlen(out)))
		return xstrdup(out);

	char *head = xstrndup(last, last_len);
	char *all = xasprintf("%s\n%s", head, out);
	free(head);
	return all;
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

/* ── who owns the new filesystem ─────────────────────────────────────────────
 *
 * mkfs runs as root, so the root directory of the filesystem it creates is
 * owned by root — and a stick formatted ext4 through this program then mounted
 * on the desktop is one the person who formatted it cannot write to. On a
 * removable drive that is never what was meant. udisks2 has an option for
 * exactly this (`take-ownership`), and syn-disks does not use udisks to format,
 * so it has to arrange the same thing itself.
 *
 * ⚠ It is arranged AT CREATION and not by a chown afterwards. A chown means a
 * second privileged step — mount the new filesystem somewhere, change the
 * owner, unmount — which is a second polkit prompt and a root helper that
 * takes a device path from an unprivileged caller. Every one of these three
 * filesystems can be told at mkfs time instead, so none of that is needed:
 *
 *   ext4    -E root_owner=UID:GID
 *   btrfs   --rootdir DIR    the root inode inherits DIR's owner
 *   xfs     -p PROTOFILE     whose first entry is the root directory
 *
 * vfat, exfat and ntfs store no ownership at all — the mount options decide,
 * and udisks already mounts them as the user. There is nothing to do for them
 * and nothing is done.
 */
uid_t fs_owner_uid(void)
{
	/* The person who asked, not the process that writes. Under pkexec the
	 * mkfs runs as root and this program does not; run directly as root there
	 * is nobody else to be, unless sudo says who it was. */
	uid_t me = getuid();
	if (me != 0)
		return me;
	const char *s = getenv("SUDO_UID");
	if (!s || !*s)
		s = getenv("PKEXEC_UID");
	if (s && *s) {
		long v = strtol(s, NULL, 10);
		if (v > 0 && v < 0x7FFFFFFF)
			return (uid_t)v;
	}
	return 0;
}

gid_t fs_owner_gid(void)
{
	uid_t me = getuid();
	if (me != 0)
		return getgid();
	const char *s = getenv("SUDO_GID");
	if (s && *s) {
		long v = strtol(s, NULL, 10);
		if (v > 0 && v < 0x7FFFFFFF)
			return (gid_t)v;
	}
	/* pkexec exports no GID. The user's primary group is the right answer and
	 * it is one lookup away; falling back to the uid would be a guess that is
	 * right on this distribution and wrong on others. */
	uid_t u = fs_owner_uid();
	if (u != 0) {
		struct passwd *pw = getpwuid(u);
		if (pw)
			return pw->pw_gid;
	}
	return 0;
}

/* The scratch files two of the three need. Owned by the caller, inside the
 * user's own runtime directory rather than /tmp: the path has to be known
 * BEFORE the command is built so that what --dry-run prints is exactly what
 * runs, and a predictable name in a world-writable directory is a symlink
 * waiting to happen. $XDG_RUNTIME_DIR is 0700 and nobody else can reach it.
 *
 * ⚠ The PID is in the name, and it is not decoration. Two sticks formatted at
 * once is two of these processes, and with one fixed name the first to finish
 * would delete the second's protofile out from under a running mkfs.xfs — a
 * format that fails for no reason the person who started it could ever see.
 * The same process builds the argv and does the write, so the dry run still
 * prints exactly the path the real run uses.
 *
 * COMPUTED ONCE AND KEPT. The result goes into an argv that outlives every
 * caller, so returning an allocation the caller must not free is a leak by
 * design — and a leak by design is indistinguishable, to LeakSanitizer, from
 * the other kind: the sanitiser build reported it, exited 1 instead of the
 * status the command meant, and eighteen assertions that had nothing to do
 * with ownership failed at once. Caching is the honest fix rather than a
 * suppression. There are two leaves and the answer never changes, so this
 * also guarantees the path in the argv and the path prepare() writes are the
 * same string and cannot drift. */
static char *owner_scratch(const char *leaf)
{
	static char *cache[2];
	int slot = !strcmp(leaf, "rootdir") ? 0 : 1;
	if (cache[slot])
		return cache[slot];

	const char *base = getenv("XDG_RUNTIME_DIR");
	if (!base || !*base)
		return NULL;
	cache[slot] = xasprintf("%s/syn-disks-%s.%ld", base, leaf, (long)getpid());
	return cache[slot];
}

/* Empty a directory we made, one level deep, and remove it.
 *
 * Deliberately NOT a recursive delete. Nothing puts anything in this directory
 * — it exists to be empty — so one level is all that can ever be there, and a
 * recursive unlink driven by a path from the environment is a much larger
 * thing to have in a program that also formats disks. A subdirectory that
 * somehow exists makes this fail, which is the safe direction: the caller
 * treats "could not empty it" as "do not hand it to mkfs". */
static bool scratch_dir_clear(const char *dir)
{
	DIR *d = opendir(dir);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				continue;
			char *p = xasprintf("%s/%s", dir, e->d_name);
			unlink(p);
			free(p);
		}
		closedir(d);
	}
	return rmdir(dir) == 0 || errno == ENOENT;
}

/* Put the scratch files where the argv already says they will be.
 *
 * Called only on the real run, never on a dry run: describing an operation
 * must not leave anything behind. The PATHS are decided when the command is
 * built, so the dry run still prints exactly what will execute — it is the
 * contents that arrive later. */
void fs_owner_prepare(const fs_kind_t *fs)
{
	uid_t owner = fs_owner_uid();
	if (owner == 0)
		return;

	if (!strcmp(fs->name, "btrfs")) {
		const char *dir = owner_scratch("rootdir");
		if (!dir)
			return;
		/* EMPTY, and made FRESH: --rootdir copies whatever is in the directory
		 * into the new filesystem, so a leftover file from a killed format
		 * would be written onto somebody's stick without a word.
		 *
		 * The PID in the name is what actually rules that out — the directory
		 * belongs to this run and no other. This is the second lock on the
		 * same door, for the one case the first does not cover: a PID that
		 * came round again onto the leavings of a format that was killed.
		 *
		 * It is `scratch_dir_clear` and not `rmdir`, which was the first
		 * version and was wrong: rmdir removes EMPTY directories only, so a
		 * stale file inside made it fail, the mkdir that followed returned
		 * EEXIST, and the directory reached mkfs exactly as it stood. Hence
		 * also the bare `!= 0` below — tolerating EEXIST here is what turned
		 * a failure to make the directory fresh into silence. */
		scratch_dir_clear(dir);
		if (mkdir(dir, 0755) != 0)
			fprintf(stderr, "syn-disks: cannot make %s fresh (%s) — the new "
			        "filesystem may be owned by root, and anything left in "
			        "that directory will be copied onto the device\n",
			        dir, strerror(errno));
	} else if (!strcmp(fs->name, "xfs")) {
		const char *path = owner_scratch("proto");
		if (!path)
			return;
		FILE *f = fopen(path, "w");
		if (f) {
			/* The xfs protofile: a boot-block name nobody uses, a size line,
			 * then the ROOT DIRECTORY's own mode, uid and gid, then the end
			 * marker. Three lines of it exist to carry the third. */
			fprintf(f, "dummy\n0 0\nd--755 %u %u\n$\n",
			        (unsigned)owner, (unsigned)fs_owner_gid());
			fclose(f);
		} else {
			fprintf(stderr, "syn-disks: cannot write %s — the new filesystem "
			        "will be owned by root\n", path);
		}
	}
}

void fs_owner_cleanup(const fs_kind_t *fs)
{
	if (!strcmp(fs->name, "btrfs")) {
		const char *dir = owner_scratch("rootdir");
		/* Emptied, not merely rmdir'd, for the same reason as above: a
		 * directory this leaves behind is one the NEXT format inherits. */
		if (dir) scratch_dir_clear(dir);
	} else if (!strcmp(fs->name, "xfs")) {
		const char *path = owner_scratch("proto");
		if (path) unlink(path);
	}
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

	/* The owner flags. Every string here outlives this function and none of
	 * them is the caller's to free — they are process-lifetime and computed
	 * once, exactly like the static table entries beside them, so `out` stays
	 * a plain borrowed argv with nothing to remember about it. */
	uid_t owner = fs_owner_uid();
	if (owner != 0) {
		if (!strcmp(fs->name, "ext4")) {
			static char *root_owner;
			if (!root_owner)
				root_owner = xasprintf("root_owner=%u:%u",
				                       (unsigned)owner,
				                       (unsigned)fs_owner_gid());
			out[n++] = (char *)"-E";
			out[n++] = root_owner;
		} else if (!strcmp(fs->name, "btrfs")) {
			char *dir = owner_scratch("rootdir");
			if (dir) {
				out[n++] = (char *)"--rootdir";
				out[n++] = dir;
			}
		} else if (!strcmp(fs->name, "xfs")) {
			char *proto = owner_scratch("proto");
			if (proto) {
				out[n++] = (char *)"-p";
				out[n++] = proto;
			}
		}
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
		char *why = xasprintf("it is on /dev/%s, the disk holding this "
		                      "running system", shared);
		if (g_out == OUT_REC) {
			guard_report_refusal(dev, why, "none");
		} else {
			fprintf(stderr, "%ssyn-disks: refusing to format %s — %s.%s\n",
			        C_BAD(), dev, why, C_RESET());
			fprintf(stderr,
			        "  There is no flag that overrides this. If you truly "
			        "mean to, use mkfs.%s directly.\n", fs);
		}
		free(why);
		free(shared);
		free(dev);
		free(k);
		return 1;
	}

	/* Everything else format refuses, it refuses because guard.c says so.
	 *
	 * This used to be a hand-written mount check, and the cost of the second
	 * implementation was exactly what the guard's own header warns about: the
	 * rules the copy did not have were rules format did not apply. A stick with
	 * its write-protect switch set sailed through the dry run, through the
	 * confirmation and through polkit, and mke2fs said "Read-only file system
	 * while setting up superblock" — a sentence that reads like a broken stick
	 * and means a switch on the side of it. The guard had known since the day
	 * it was written; nothing had asked.
	 *
	 * GUARD_DESTROY, because that is what formatting is: mounted, live swap, a
	 * volume unlocked on top, an fstab entry, and the read-only flag all stop
	 * it, each with the way out beside it. */
	if (guard_refuse(k, dev, "format", GUARD_DESTROY)) {
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
	char *kwhy = NULL;
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
			/* A WARNING and not `blocked`: making a stick for a camera or a
			 * Windows machine is a perfectly good reason to write a
			 * filesystem this kernel cannot read. It is here so the mount
			 * error that follows is not a surprise. */
			if (!fs_kernel_can_mount(kind, &kwhy)) {
				rec_row(2, "warn", kwhy);
				free(kwhy);
				kwhy = NULL;
			}
		} else {
			printf("would run: %s\n", line);
			printf("%sthis erases everything on %s%s\n", C_WARN(), dev, C_RESET());
			if (!fs_kernel_can_mount(kind, &kwhy)) {
				printf("%swarning: %s%s\n", C_WARN(), kwhy, C_RESET());
				free(kwhy);
				kwhy = NULL;
			}
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
	/* The scratch file two of the three ownership flags name. Only here, on
	 * the path that actually writes — the dry run above returned already. */
	fs_owner_prepare(kind);
	/* DETACHED: this is the write. Closing the window used to kill this
	 * process, and the mkfs it had started died of SIGPIPE part way through
	 * a filesystem. */
	char *out = run_capture_detached(cmd, NULL, &st);
	strip_trailing_newline(out);
	/* After the write and before anything is reported: run_capture_detached
	 * has waited, so mkfs has read whatever it was going to read. */
	fs_owner_cleanup(kind);

	/* A failure is an answer too, and this one had none: the guard cleared the
	 * device, mke2fs wrote nothing, and all the window could quote was the
	 * chatter of a tool that had already given up.
	 *
	 * So ask again, now that a write has been attempted. A stick that reported
	 * "Write Protect is off" at plug-in and refused every sector is not a
	 * mystery once the kernel has re-read it — it is a switch on the body, and
	 * that is the sentence, with the same `readonly` way out the guard would
	 * have given had the device been honest. */
	const char *fix = "none";
	char *late = st == 0 ? NULL : guard_write_protected_now(k, &fix);
	char *detail = NULL;
	if (st != 0) {
		char *ordered = reason_first(out);
		detail = late ? xasprintf("%s · %s", late, ordered) : xstrdup(ordered);
		free(ordered);
	}

	if (g_out == OUT_REC) {
		/* `fix` is a column of its own and not a sentence inside `detail`: the
		 * window keys its way out off the CODE, so a failed write can offer
		 * exactly what a refusal offers. */
		rec_row(4, "device", "status", "detail", "fix");
		rec_row(4, dev, st == 0 ? "ok" : "failed",
		        st == 0 ? out : detail, fix);
	} else if (st == 0) {
		printf("%s is now %s%s\n", dev, fs, label && *label ? " — labelled" : "");
		/* Said again after the write, not only in the plan: a scripted
		 * --yes never saw the dry run, and the mount failure it is about to
		 * hit looks like a bad format rather than a missing driver. */
		if (!fs_kernel_can_mount(kind, &kwhy)) {
			printf("%swarning: %s%s\n", C_WARN(), kwhy, C_RESET());
			free(kwhy);
			kwhy = NULL;
		}
	} else {
		/* The terminal has room, so the tool is quoted as it printed itself —
		 * the reordering above is for a one-line status bar, not for a screen
		 * that can hold all of it. What leads here is our sentence, because
		 * that is the one the tool could not say. */
		if (late) {
			/* Sentence then way out, exactly as a refusal reads — the two
			 * halves of our answer stay together and the tool's chatter goes
			 * after both, rather than between them. */
			fprintf(stderr, "%ssyn-disks: could not format %s — %s.%s\n",
			        C_BAD(), dev, late, C_RESET());
			guard_print_fix(dev, fix);
		}
		fprintf(stderr, "%s%s%s\n", C_BAD(), *out ? out : "mkfs refused", C_RESET());
	}

	free(late);
	free(detail);
	free(out);
	free(dev);
	free(k);
	return st == 0 ? 0 : 1;
}
