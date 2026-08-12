/* mounts.c — what is mounted, from /proc/self/mounts and nowhere else.
 *
 * ⚠ THIS IS DELIBERATELY NOT lsblk. On the machine this was written on:
 *
 *     NAME="cryptroot" PATH="/dev/mapper/cryptroot" FSTYPE="btrfs"
 *     MOUNTPOINT="/var/log"
 *
 * That device carries FIVE mounts — /, /home, /.snapshots, /var/cache/pacman/pkg
 * and /var/log — because it is a btrfs with subvolumes, and lsblk's MOUNTPOINT
 * column has room for one. The one it prints is not the one that matters.
 *
 * The format guard in actions.c asks "is the root filesystem on this disk?".
 * Built on lsblk, that question would have been answered "no" for the disk
 * holding this machine's root filesystem, and the guard would have waved
 * through a request to reformat it. /proc/self/mounts lists every mount of
 * every device, so it is the only correct source for this.
 *
 * SYN_DISKS_MOUNTS overrides the file, so the test suite can describe mount
 * tables this machine does not have — including the one above.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"

#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

typedef struct {
	char *source;   /* the device column, verbatim */
	char *point;
	char *fstype;
} mount_t;

static mount_t *g_mounts;
static size_t   g_nmounts;
static char    *g_backing;
static bool     g_loaded;

/* /proc/self/mounts is space-separated, so it escapes space, tab, newline and
 * backslash in a path as octal. A mount point like "/mnt/My Disk" is otherwise
 * truncated at the space — and a truncated mount point is not a missing
 * answer, it is a WRONG one that happens to name a real directory. */
static void unescape_mount(char *s)
{
	char *w = s;
	for (const char *r = s; *r; ) {
		if (r[0] == '\\' && r[1] >= '0' && r[1] <= '7'
		    && r[2] >= '0' && r[2] <= '7' && r[3] >= '0' && r[3] <= '7') {
			*w++ = (char)((r[1] - '0') * 64 + (r[2] - '0') * 8 + (r[3] - '0'));
			r += 4;
		} else {
			*w++ = *r++;
		}
	}
	*w = '\0';
}

static void load(void)
{
	if (g_loaded)
		return;
	g_loaded = true;

	const char *env = getenv("SYN_DISKS_MOUNTS");
	g_backing = slurp((env && *env) ? env : "/proc/self/mounts");
	if (!g_backing) {
		g_backing = xstrdup("");
		return;
	}

	size_t nlines = 0;
	char **lines = split(g_backing, '\n', &nlines);
	g_mounts = xmalloc((nlines ? nlines : 1) * sizeof *g_mounts);

	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i])
			continue;
		size_t nf = 0;
		char **f = split(lines[i], ' ', &nf);
		if (nf >= 3) {
			unescape_mount(f[0]);
			unescape_mount(f[1]);
			g_mounts[g_nmounts++] = (mount_t){ f[0], f[1], f[2] };
		}
		free(f);
	}
	free(lines);
}

/* The mount table's device column is whatever the mounter wrote there —
 * "/dev/mapper/cryptroot" for one filesystem, "/dev/sda1" for another, and
 * "systemd-1" for an autofs trigger. Comparing it to a kernel name as a string
 * would match none of them, so every source is resolved to its kernel name the
 * same way every other device path in this program is: by device number. */
static const char *source_kname(size_t i)
{
	static char **cache;
	static size_t ncache;

	if (ncache <= i) {
		cache = xrealloc(cache, (i + 1) * sizeof *cache);
		for (size_t k = ncache; k <= i; k++)
			cache[k] = NULL;
		ncache = i + 1;
	}
	if (!cache[i]) {
		char *k = sd_kernel_name(g_mounts[i].source);
		cache[i] = k ? k : xstrdup("");
	}
	return cache[i];
}

char **mt_points_of(const char *kname, size_t *n)
{
	load();
	*n = 0;
	char **out = NULL;
	if (!kname || !*kname)
		return NULL;

	for (size_t i = 0; i < g_nmounts; i++) {
		/* autofs rows name "systemd-1" as their device and never a real
		 * one; skipping them here is what stops an automount trigger being
		 * attributed to whatever device happens to resolve to nothing. */
		if (!strcmp(g_mounts[i].fstype, "autofs"))
			continue;
		if (strcmp(source_kname(i), kname))
			continue;
		out = xrealloc(out, (*n + 1) * sizeof *out);
		out[(*n)++] = xstrdup(g_mounts[i].point);
	}
	return out;
}

bool mt_is_mounted(const char *kname)
{
	size_t n = 0;
	char **p = mt_points_of(kname, &n);
	sd_free_list(p, n);
	return n > 0;
}

char *mt_root_device(void)
{
	load();
	for (size_t i = 0; i < g_nmounts; i++)
		if (!strcmp(g_mounts[i].point, "/")) {
			const char *k = source_kname(i);
			return *k ? xstrdup(k) : NULL;
		}
	return NULL;
}

const char *mt_fstype_at(const char *point)
{
	load();
	for (size_t i = 0; i < g_nmounts; i++)
		if (!strcmp(g_mounts[i].point, point))
			return g_mounts[i].fstype;
	return NULL;
}

bool mt_is_autofs(const char *point)
{
	load();
	if (!point || !*point)
		return false;
	for (size_t i = 0; i < g_nmounts; i++)
		if (!strcmp(g_mounts[i].point, point)
		    && !strcmp(g_mounts[i].fstype, "autofs"))
			return true;
	return false;
}

void mt_usage(const char *point, unsigned long long *used,
              unsigned long long *total)
{
	*used = 0;
	*total = 0;
	if (!point || !*point)
		return;

	/* Asking an autofs trigger how full it is TRIGGERS THE MOUNT. On this
	 * machine that spins up a sleeping 3TB disk to draw a progress bar
	 * nobody asked for. Unknown is the correct answer here, and callers are
	 * required to render it as unknown rather than as empty. */
	if (mt_is_autofs(point))
		return;

	struct statvfs vfs;
	if (statvfs(point, &vfs) != 0)
		return;

	unsigned long long unit = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
	*total = (unsigned long long)vfs.f_blocks * unit;
	/* f_bavail, not f_bfree. The difference is the reserved blocks only root
	 * may use; counting them as free is how a full disk reports space to
	 * somebody who cannot write a single byte to it. */
	*used = (unsigned long long)(vfs.f_blocks - vfs.f_bavail) * unit;
}
