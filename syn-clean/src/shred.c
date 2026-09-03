/* shred.c — destroying a file on purpose, and saying how much that achieved.
 *
 * ⛔ READ THE HEADER COMMENT IN synclean.h BEFORE CHANGING ANYTHING HERE. The
 * hard part of this file is not the overwriting; it is refusing to claim more
 * than overwriting achieves on the filesystem it is running on.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synclean.h"
#include "i18n.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

/* Filesystem magics. From <linux/magic.h>, spelled out because that header
 * does not carry all of them and a wrong constant here would silently report
 * the wrong ground. */
#define MAGIC_BTRFS   0x9123683EUL
#define MAGIC_ZFS     0x2FC12FC1UL
#define MAGIC_EXT234  0xEF53UL
#define MAGIC_XFS     0x58465342UL
#define MAGIC_F2FS    0xF2F52010UL
#define MAGIC_TMPFS   0x01021994UL
#define MAGIC_OVERLAY 0x794C7630UL
#define MAGIC_NTFS3   0x7366746EUL
#define MAGIC_VFAT    0x4D44UL

void shred_ground(const char *path, shred_ground_t *g)
{
	memset(g, 0, sizeof *g);
	snprintf(g->fstype, sizeof g->fstype, "unknown");

	struct statfs sf;
	if (statfs(path, &sf) != 0) return;

	unsigned long t = (unsigned long)sf.f_type;
	switch (t) {
	case MAGIC_BTRFS:   snprintf(g->fstype, sizeof g->fstype, "btrfs");   g->cow = true;  break;
	case MAGIC_ZFS:     snprintf(g->fstype, sizeof g->fstype, "zfs");     g->cow = true;  break;
	case MAGIC_OVERLAY: snprintf(g->fstype, sizeof g->fstype, "overlay"); g->cow = true;  break;
	case MAGIC_EXT234:  snprintf(g->fstype, sizeof g->fstype, "ext4");    break;
	case MAGIC_XFS:     snprintf(g->fstype, sizeof g->fstype, "xfs");     break;
	case MAGIC_F2FS:    snprintf(g->fstype, sizeof g->fstype, "f2fs");    break;
	case MAGIC_TMPFS:   snprintf(g->fstype, sizeof g->fstype, "tmpfs");   break;
	case MAGIC_NTFS3:   snprintf(g->fstype, sizeof g->fstype, "ntfs");    break;
	case MAGIC_VFAT:    snprintf(g->fstype, sizeof g->fstype, "vfat");    break;
	default: break;
	}

	/* ⚠ SNAPSHOTS ARE THE HALF PEOPLE DO NOT THINK OF. SynapseOS installs btrfs
	 * with snapper, so /.snapshots holds read-only copies of $HOME as it was.
	 * Overwriting the live file cannot touch them, and it must not try: they are
	 * the backups. Naming them is the only useful thing to do. */
	if (g->cow) {
		struct stat st;
		if (stat("/.snapshots", &st) == 0 && S_ISDIR(st.st_mode))
			g->snapshots = true;
	}
}

/* ── Destroying one entry ────────────────────────────────────────────────────
 *
 * ⛔ EVERYTHING HERE IS RELATIVE TO A DIRECTORY DESCRIPTOR. A path resolved
 * twice — once to check it, once to act on it — is a path that can change
 * between the two, and this file's next act after checking is to OVERWRITE THE
 * CONTENTS. A symlink dropped into that window would send the writes at a file
 * the user never selected. openat/fstat/renameat/unlinkat against a directory
 * fd resolve the name once, and O_NOFOLLOW makes a swap fail loudly instead of
 * succeeding quietly.
 */

/* Overwrite an already-open file. The descriptor is the subject; no name is
 * consulted, so nothing can be substituted underneath it. */
static int overwrite_fd(int fd, off_t size, int passes)
{
	unsigned char buf[65536];
	int urnd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);

	for (int p = 0; p < passes; p++) {
		if (lseek(fd, 0, SEEK_SET) < 0) break;
		off_t left = size;
		while (left > 0) {
			size_t want = (size_t)(left > (off_t)sizeof buf ? (off_t)sizeof buf : left);
			if (urnd >= 0) {
				ssize_t r = read(urnd, buf, want);
				if (r <= 0) memset(buf, 0xA5, want);
			} else {
				memset(buf, 0xA5, want);
			}
			ssize_t w = write(fd, buf, want);
			if (w <= 0) break;
			left -= w;
		}
		/* ⛔ FLUSHED EVERY PASS. Without this the passes collapse in the page
		 * cache and the disk sees one write — which is not "fewer passes", it
		 * is none of the passes this program said it did. */
		if (fsync(fd) != 0) break;
	}

	/* A final zero pass, so what is left is not obviously a shredded file. */
	if (lseek(fd, 0, SEEK_SET) >= 0) {
		memset(buf, 0, sizeof buf);
		off_t left = size;
		while (left > 0) {
			size_t want = (size_t)(left > (off_t)sizeof buf ? (off_t)sizeof buf : left);
			ssize_t w = write(fd, buf, want);
			if (w <= 0) break;
			left -= w;
		}
		fsync(fd);
	}
	if (ftruncate(fd, 0) != 0) { /* best effort */ }
	fsync(fd);
	if (urnd >= 0) close(urnd);
	return 0;
}

static int shred_at(int dfd, const char *name, int passes, unsigned long long *bytes)
{
	int fd = openat(dfd, name, O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		/* A symlink, a device, a fifo, or something not writable. Each is a
		 * NAME to remove rather than data to overwrite — a symlink above all:
		 * following one destroys a file the user did not select. */
		if (g_dry) return 0;
		return unlinkat(dfd, name, 0) == 0 ? 0 : -1;
	}

	struct stat st;
	if (fstat(fd, &st) != 0) { close(fd); return -1; }
	if (!S_ISREG(st.st_mode)) {
		close(fd);
		if (g_dry) return 0;
		return unlinkat(dfd, name, 0) == 0 ? 0 : -1;
	}

	*bytes += (unsigned long long)st.st_size;
	if (g_dry) { close(fd); return 0; }

	overwrite_fd(fd, st.st_size, passes);
	close(fd);

	/* ⚠ THE RENAME IS NOT DECORATION. The directory entry holds the FILENAME,
	 * and the name is often the most telling part of a file
	 * ("resignation-letter.pdf"). Unlinking frees the entry but does not scrub
	 * it; renaming to a same-length run of junk first overwrites the entry in
	 * place on the filesystems where that works at all. */
	size_t blen = strlen(name);
	char *mask = malloc(blen + 1);
	if (mask) {
		for (size_t i = 0; i < blen; i++) mask[i] = '0';
		mask[blen] = '\0';
		if (renameat(dfd, name, dfd, mask) == 0) {
			int rc = unlinkat(dfd, mask, 0) == 0 ? 0 : -1;
			free(mask);
			return rc;
		}
		free(mask);
	}
	return unlinkat(dfd, name, 0) == 0 ? 0 : -1;
}

/* A directory, depth-first: entries first, then the directory itself.
 * ⛔ Never across a mount point and never through a symlink — with more at
 * stake than the same rule in scan.c, because none of this can be undone. */
static int shred_dir(int dfd, int passes, unsigned long long *bytes,
                     dev_t dev, int depth)
{
	DIR *d = fdopendir(dfd);          /* takes ownership: closedir closes it */
	if (!d) { close(dfd); return -1; }

	struct dirent *e;
	int rc = 0;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;

		struct stat st;
		if (fstatat(dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			rc = -1;
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			if (st.st_dev != dev) {
				warn(_("%s is on another filesystem — left alone"), e->d_name);
				continue;
			}
			if (depth > 64) { rc = -1; continue; }
			int sub = openat(dirfd(d), e->d_name,
			                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if (sub < 0) { rc = -1; continue; }
			if (shred_dir(sub, passes, bytes, dev, depth + 1) != 0) rc = -1;
			if (!g_dry && unlinkat(dirfd(d), e->d_name, AT_REMOVEDIR) != 0) rc = -1;
		} else if (shred_at(dirfd(d), e->d_name, passes, bytes) != 0) {
			rc = -1;
		}
	}
	closedir(d);
	return rc;
}

int shred_path(const char *path, int passes, unsigned long long *bytes)
{
	/* A directory opens with O_DIRECTORY; anything else fails it and is handled
	 * as a single entry relative to its parent. */
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd >= 0) {
		struct stat st;
		if (fstat(fd, &st) != 0) { close(fd); return -1; }
		int rc = shred_dir(fd, passes, bytes, st.st_dev, 0);
		/* toctou-ok: the tree under this name is gone by now; the rmdir is the
		 * last act and nothing reopens the name after it. */
		if (!g_dry && rmdir(path) != 0) rc = -1;
		return rc;
	}
	if (errno != ENOTDIR && errno != ELOOP) {
		warn("%s: %s", path, strerror(errno));
		return -1;
	}

	/* Split so the entry can be reached through its parent's descriptor. */
	char *dup = xstrdup(path);
	char *slash = strrchr(dup, '/');
	const char *base = slash ? slash + 1 : dup;
	const char *dir  = slash ? dup : ".";
	if (slash) { if (slash == dup) dir = "/"; else *slash = '\0'; }

	int pfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (pfd < 0) { warn("%s: %s", path, strerror(errno)); free(dup); return -1; }
	int rc = shred_at(pfd, base, passes, bytes);
	close(pfd);
	free(dup);
	return rc;
}

