/* fileops.c — copy, move, rename, mkdir, and permanent delete.
 *
 * This is the file in this program that can destroy data, and it is written on
 * that assumption throughout:
 *
 *   - Nothing follows a symlink. Every descent uses openat with O_NOFOLLOW and
 *     every stat is AT_SYMLINK_NOFOLLOW. A recursive copy that followed a link
 *     to / would walk the whole filesystem; a recursive DELETE that followed
 *     one would remove it.
 *   - Nothing overwrites by default. The conflict policy defaults to erroring
 *     out and naming the collision, because the caller — a person, or the GUI
 *     asking one — is the only thing that knows whether an overwrite is
 *     wanted. Guessing here is unrecoverable.
 *   - A cross-filesystem move deletes the source only after the copy has
 *     fully succeeded, byte for byte and with its metadata applied.
 *   - Special files (fifos, sockets, devices) are skipped rather than copied.
 *     Opening a fifo blocks until somebody writes to it, which turns a copy
 *     into a hang with no diagnosis.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── shared helpers ─────────────────────────────────────────────────────── */

char *sf_resolve(const char *path)
{
	char *real = realpath(path, NULL);
	if (!real)
		die("cannot resolve %s: %s", path, strerror(errno));
	return real;
}

/* Resolve a path that does not exist yet by resolving its PARENT and
 * re-attaching the basename. realpath() fails outright on a missing final
 * component, which is exactly the case a copy destination is in. */
char *sf_resolve_parent(const char *path)
{
	char *copy = xstrdup(path);
	char *slash = strrchr(copy, '/');

	char *dir, *base;
	if (!slash) {
		dir = xstrdup(".");
		base = xstrdup(copy);
	} else if (slash == copy) {
		dir = xstrdup("/");
		base = xstrdup(slash + 1);
	} else {
		*slash = '\0';
		dir = xstrdup(copy);
		base = xstrdup(slash + 1);
	}
	free(copy);

	char *rdir = realpath(dir, NULL);
	free(dir);
	if (!rdir) {
		free(base);
		return NULL;
	}

	char *out = !strcmp(rdir, "/") ? xasprintf("/%s", base)
	                               : xasprintf("%s/%s", rdir, base);
	free(rdir);
	free(base);
	return out;
}

/* Is `child` inside `ancestor`? The "/" boundary is load-bearing: a plain
 * prefix test says /home/velle-backup is inside /home/velle, and the operation
 * that follows would be refused for no reason — or, with the comparison the
 * other way round, allowed when it should not be. */
bool sf_is_descendant(const char *ancestor, const char *child)
{
	size_t n = strlen(ancestor);
	if (strncmp(child, ancestor, n))
		return false;
	if (child[n] == '\0')
		return true;                    /* the same directory */
	if (n == 1 && ancestor[0] == '/')
		return true;                    /* everything is inside / */
	return child[n] == '/';
}

const char *sf_basename(const char *path)
{
	const char *slash = strrchr(path, '/');
	return (slash && slash[1]) ? slash + 1 : path;
}

/* ── recursive delete ───────────────────────────────────────────────────── */

/* Removes `name` under `dirfd`. Directories are emptied depth-first and then
 * rmdir'd. Never descends through a symlink: a link is unlinked as itself. */
int sf_rm_rf(int dirfd, const char *name)
{
	struct stat st;
	if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
		return errno == ENOENT ? 0 : -1;

	if (!S_ISDIR(st.st_mode))
		return unlinkat(dirfd, name, 0);

	int fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0)
		return -1;

	DIR *d = fdopendir(fd);
	if (!d) {
		close(fd);
		return -1;
	}

	int rc = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if (sf_rm_rf(fd, e->d_name) != 0)
			rc = -1;
	}
	closedir(d);

	if (rc == 0)
		rc = unlinkat(dirfd, name, AT_REMOVEDIR);
	return rc;
}

/* ── conflict policy ────────────────────────────────────────────────────── */

typedef enum {
	CONFLICT_ERROR,     /* default: refuse and name the collision */
	CONFLICT_SKIP,
	CONFLICT_RENAME,
	CONFLICT_OVERWRITE
} conflict_t;

static conflict_t parse_conflict(const char *s)
{
	if (!strcmp(s, "error"))     return CONFLICT_ERROR;
	if (!strcmp(s, "skip"))      return CONFLICT_SKIP;
	if (!strcmp(s, "rename"))    return CONFLICT_RENAME;
	if (!strcmp(s, "overwrite")) return CONFLICT_OVERWRITE;
	die("unknown conflict policy '%s' — try error, skip, rename, overwrite", s);
}

/* "report.txt" -> "report (copy).txt" -> "report (copy 2).txt". The suffix goes
 * before the extension because that is where a person expects to still be able
 * to read it, and because the extension is what decides how it opens. */
static char *unique_name(int dfd, const char *base)
{
	if (faccessat(dfd, base, F_OK, AT_SYMLINK_NOFOLLOW) != 0)
		return xstrdup(base);

	const char *dot = strrchr(base, '.');
	/* A leading dot is the whole name of a hidden file, not an extension. */
	if (dot == base)
		dot = NULL;

	char *stem = dot ? xstrndup(base, (size_t)(dot - base)) : xstrdup(base);
	const char *ext = dot ? dot : "";

	for (int i = 1; i < 10000; i++) {
		char *cand = (i == 1) ? xasprintf("%s (copy)%s", stem, ext)
		                      : xasprintf("%s (copy %d)%s", stem, i, ext);
		if (faccessat(dfd, cand, F_OK, AT_SYMLINK_NOFOLLOW) != 0) {
			free(stem);
			return cand;
		}
		free(cand);
	}

	free(stem);
	return NULL;
}

/* ── reporting ──────────────────────────────────────────────────────────── */

typedef struct {
	int done, skipped, failed;
} tally_t;

static void report(const char *path, const char *status, const char *detail)
{
	if (g_out == OUT_REC) {
		char *enc = pct_encode(path, true);
		rec_row(3, enc, status, detail ? detail : "");
		free(enc);
	} else if (strcmp(status, "done")) {
		printf("%s%-9s%s %s%s%s\n", C_WARN(), status, C_RESET(), path,
		       detail && *detail ? "  — " : "", detail ? detail : "");
	} else if (g_verbose) {
		printf("%s%-9s%s %s\n", C_DIM(), status, C_RESET(), path);
	}
}

/* ── copying ────────────────────────────────────────────────────────────── */

static int copy_one(int sfd, const char *sname, int dfd, const char *dname,
                    const struct stat *st)
{
	int in = openat(sfd, sname, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (in < 0)
		return -1;

	/* O_EXCL: the caller has already applied the conflict policy, so a
	 * collision reaching this point is a race, and losing that race must not
	 * silently truncate somebody's file. */
	int out = openat(dfd, dname, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
	                 st->st_mode & 07777);
	if (out < 0) {
		close(in);
		return -1;
	}

	int rc = 0;
	off_t remaining = st->st_size;

	/* copy_file_range lets the kernel do the work — and on btrfs, which is
	 * what SynapseOS installs by default, it reflinks instead of copying
	 * bytes at all. It can refuse for filesystems that do not support it, so
	 * the read/write loop below is not dead code. */
	while (remaining > 0) {
		ssize_t n = copy_file_range(in, NULL, out, NULL, (size_t)remaining, 0);
		if (n <= 0)
			break;
		remaining -= n;
	}

	if (remaining > 0) {
		if (lseek(in, 0, SEEK_SET) == (off_t)-1
		    || lseek(out, 0, SEEK_SET) == (off_t)-1) {
			rc = -1;
		} else {
			char buf[128 * 1024];
			for (;;) {
				ssize_t n = read(in, buf, sizeof buf);
				if (n == 0)
					break;
				if (n < 0) { rc = -1; break; }
				for (ssize_t off = 0; off < n; ) {
					ssize_t w = write(out, buf + off, (size_t)(n - off));
					if (w <= 0) { rc = -1; break; }
					off += w;
				}
				if (rc != 0)
					break;
			}
		}
	}

	/* Timestamps last: writing to the file updates mtime, so setting it
	 * before the final write would be silently undone. */
	if (rc == 0) {
		struct timespec times[2] = { st->st_atim, st->st_mtim };
		futimens(out, times);
		fchmod(out, st->st_mode & 07777);
	}

	/* fsync before declaring success. A cross-filesystem MOVE deletes the
	 * source on the strength of this return value, and a copy still sitting
	 * in page cache is not a copy that survived the power going out. */
	if (rc == 0 && fsync(out) != 0)
		rc = -1;

	close(in);
	close(out);

	if (rc != 0)
		unlinkat(dfd, dname, 0);   /* never leave a truncated file behind */
	return rc;
}

static int copy_tree(int sfd, const char *sname, int dfd, const char *dname,
                     conflict_t pol, tally_t *t, const char *display)
{
	struct stat st;
	if (fstatat(sfd, sname, &st, AT_SYMLINK_NOFOLLOW) != 0) {
		report(display, "failed", strerror(errno));
		t->failed++;
		return -1;
	}

	/* Apply the conflict policy before touching anything.
	 *
	 * TWO THINGS THIS GOT WRONG, both of them silent.
	 *
	 * A DIRECTORY whose name already existed skipped the policy ENTIRELY and
	 * merged, whatever the caller had asked for. So pasting a folder into the
	 * folder that contains it produced no copy at all: it walked the source
	 * into itself, left `f (copy).txt` scattered through the ORIGINAL, and
	 * reported "4 done, 0 skipped, 0 failed". From the GUI that reads as a
	 * paste that did nothing — which is how it was reported. Merging is now
	 * what --conflict=overwrite MEANS for two directories, and nothing else
	 * does it.
	 *
	 * And the destination entry can BE the source. `copy --conflict=overwrite
	 * f.txt .` removed f.txt and then copied from the file it had just
	 * deleted: "0 done, 1 failed", and the only copy of the data gone. That
	 * was unreachable from the GUI only for as long as the GUI never offered
	 * an overwrite, which is a promise about a caller — not a guarantee from
	 * the code doing the deleting.
	 *
	 * Same dev+ino, not same path: a hardlink, a bind mount or a symlinked
	 * parent reaches the same bytes under a name that compares differently.
	 */
	char *target = xstrdup(dname);
	struct stat dst;
	bool exists = fstatat(dfd, target, &dst, AT_SYMLINK_NOFOLLOW) == 0;
	bool same   = exists && dst.st_dev == st.st_dev && dst.st_ino == st.st_ino;
	bool merge  = false;

	if (exists && same && pol == CONFLICT_OVERWRITE) {
		/* Refused rather than "overwritten": there is nothing to copy from
		 * once the destination has been removed, and the destination is the
		 * source. */
		report(display, "failed", "source and destination are the same");
		t->failed++;
		free(target);
		return -1;
	}

	if (exists && !same && pol == CONFLICT_OVERWRITE &&
	    S_ISDIR(st.st_mode) && S_ISDIR(dst.st_mode)) {
		/* Merge: cp -r's behaviour, kept because it is genuinely what someone
		 * pasting a folder over another folder usually means — but only when
		 * they have said overwrite. Files inside collide individually and get
		 * this same policy each. */
		merge = true;
	}

	if (exists && !merge) {
		switch (pol) {
		case CONFLICT_ERROR:
			report(display, "conflict", "already exists");
			t->failed++;
			free(target);
			return -1;
		case CONFLICT_SKIP:
			report(display, "skipped", "already exists");
			t->skipped++;
			free(target);
			return 0;
		case CONFLICT_RENAME: {
			char *u = unique_name(dfd, target);
			free(target);
			if (!u) {
				report(display, "failed", "no free name");
				t->failed++;
				return -1;
			}
			target = u;
			break;
		}
		case CONFLICT_OVERWRITE:
			if (sf_rm_rf(dfd, target) != 0) {
				report(display, "failed", "cannot replace");
				t->failed++;
				free(target);
				return -1;
			}
			break;
		}
	}

	int rc = 0;

	if (S_ISLNK(st.st_mode)) {
		/* Recreated as a link, not chased. Copying what a link points at
		 * turns one 40-byte symlink into a second copy of a 4GB file. */
		size_t cap = 4096;
		char *buf = xmalloc(cap);
		ssize_t n = readlinkat(sfd, sname, buf, cap - 1);
		if (n < 0 || symlinkat((buf[n] = '\0', buf), dfd, target) != 0) {
			report(display, "failed", strerror(errno));
			t->failed++;
			rc = -1;
		} else {
			report(display, "done", "symlink");
			t->done++;
		}
		free(buf);

	} else if (S_ISDIR(st.st_mode)) {
		if (mkdirat(dfd, target, st.st_mode & 07777) != 0 && errno != EEXIST) {
			report(display, "failed", strerror(errno));
			t->failed++;
			free(target);
			return -1;
		}

		int s2 = openat(sfd, sname, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		int d2 = openat(dfd, target, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (s2 < 0 || d2 < 0) {
			if (s2 >= 0) close(s2);
			if (d2 >= 0) close(d2);
			report(display, "failed", strerror(errno));
			t->failed++;
			free(target);
			return -1;
		}

		DIR *d = fdopendir(s2);
		if (!d) {
			close(s2);
			close(d2);
			report(display, "failed", strerror(errno));
			t->failed++;
			free(target);
			return -1;
		}

		struct dirent *e;
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				continue;
			char *sub = xasprintf("%s/%s", display, e->d_name);
			if (copy_tree(s2, e->d_name, d2, e->d_name, pol, t, sub) != 0)
				rc = -1;
			free(sub);
		}
		closedir(d);
		close(d2);
		t->done++;

	} else if (S_ISREG(st.st_mode)) {
		if (copy_one(sfd, sname, dfd, target, &st) != 0) {
			report(display, "failed", strerror(errno));
			t->failed++;
			rc = -1;
		} else {
			report(display, "done", NULL);
			t->done++;
		}

	} else {
		/* Opening a fifo blocks until a writer appears; a device node copied
		 * as data is meaningless anyway. */
		report(display, "skipped", "not a regular file");
		t->skipped++;
	}

	free(target);
	return rc;
}

/* ── the commands ───────────────────────────────────────────────────────── */

typedef struct {
	conflict_t pol;
	char **srcs;
	int nsrc;
	const char *dest;
} args_t;

static args_t parse_args(int argc, char **argv, const char *verb)
{
	args_t a = { CONFLICT_ERROR, NULL, 0, NULL };
	a.srcs = xmalloc((size_t)(argc + 1) * sizeof *a.srcs);

	for (int i = 0; i < argc; i++) {
		if (!strncmp(argv[i], "--conflict=", 11))
			a.pol = parse_conflict(argv[i] + 11);
		else if (argv[i][0] == '-' && argv[i][1])
			die("%s: unknown option '%s'", verb, argv[i]);
		else
			a.srcs[a.nsrc++] = argv[i];
	}

	if (a.nsrc < 2)
		die("%s: need one or more sources and a destination directory", verb);

	a.dest = a.srcs[--a.nsrc];   /* last positional is the destination */
	return a;
}

static int finish(tally_t *t)
{
	if (g_out == OUT_HUMAN && (t->done || t->skipped || t->failed))
		printf("%s%d done, %d skipped, %d failed%s\n", C_DIM(),
		       t->done, t->skipped, t->failed, C_RESET());
	return t->failed ? 1 : 0;
}

/* Which of these sources already exist at the destination?
 *
 * ASKED BEFORE PASTING, and it exists because the alternative is worse. The
 * GUI cannot offer "overwrite?" without knowing there is something to
 * overwrite, and the two ways to find out without this are both wrong: read
 * the destination's own listing, which is filtered (a collision with a hidden
 * file is invisible) and stale; or paste with --conflict=error and ask
 * afterwards, which has already copied everything that did not collide.
 *
 * A STAT PER SOURCE, no traversal — the question is about the top-level names,
 * which is exactly the level the person doing the pasting is looking at.
 *
 * Exit 0 whether or not anything collides: this ANSWERS a question, and a
 * non-zero exit would make "yes, two of them" indistinguishable from "the
 * destination does not exist".
 */
int cmd_collisions(int argc, char **argv)
{
	args_t a = parse_args(argc, argv, "collisions");

	char *dest = sf_resolve(a.dest);
	int dfd = open(dest, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dfd < 0) {
		if (errno == ENOTDIR)
			die("collisions: %s is not a directory", a.dest);
		die("collisions: cannot open %s: %s", dest, strerror(errno));
	}

	if (g_out == OUT_REC)
		rec_row(4, "path", "name", "kind", "same");

	for (int i = 0; i < a.nsrc; i++) {
		char *src = sf_resolve(a.srcs[i]);
		const char *base = sf_basename(src);

		struct stat dst, s;
		if (fstatat(dfd, base, &dst, AT_SYMLINK_NOFOLLOW) == 0) {
			/* "same" means this source IS the entry it collides with — the
			 * ordinary duplicate-in-place paste. Overwriting there deletes
			 * the source, so the GUI must not offer it. */
			bool same = lstat(src, &s) == 0 &&
			            s.st_dev == dst.st_dev && s.st_ino == dst.st_ino;

			if (g_out == OUT_REC) {
				char *enc = pct_encode(src, true);
				char *encname = pct_encode(base, false);
				rec_row(4, enc, encname,
				        S_ISDIR(dst.st_mode) ? "dir" : "file",
				        same ? "yes" : "no");
				free(encname);
				free(enc);
			} else {
				printf("%s\t%s%s\n", base,
				       S_ISDIR(dst.st_mode) ? "dir" : "file",
				       same ? "\tsame" : "");
			}
		}
		free(src);
	}

	close(dfd);
	free(dest);
	return 0;
}

int cmd_copy(int argc, char **argv)
{
	args_t a = parse_args(argc, argv, "copy");

	char *dest = sf_resolve(a.dest);

	/* O_DIRECTORY IS the "is it a directory" test — it fails with ENOTDIR on
	 * anything else. Asking stat() first and opening afterwards resolves the
	 * name twice and answers about two possibly different files; every
	 * operation below then runs against this ONE descriptor. */
	int dfd = open(dest, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dfd < 0) {
		if (errno == ENOTDIR)
			die("copy: %s is not a directory", a.dest);
		die("copy: cannot open %s: %s", dest, strerror(errno));
	}

	if (g_out == OUT_REC)
		rec_row(3, "path", "status", "detail");

	tally_t t = { 0, 0, 0 };
	for (int i = 0; i < a.nsrc; i++) {
		char *src = sf_resolve(a.srcs[i]);

		/* Copying a directory into itself or into its own subtree recurses
		 * until the disk fills. The check has to be on RESOLVED paths, or
		 * a symlink or a "../" in either argument walks straight past it. */
		if (sf_is_descendant(src, dest)) {
			report(src, "failed", "destination is inside the source");
			t.failed++;
			free(src);
			continue;
		}

		int sfd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		char *parent = xstrdup(src);
		char *slash = strrchr(parent, '/');
		if (slash && slash != parent) {
			*slash = '\0';
			close(sfd);
			sfd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		} else if (slash) {
			close(sfd);
			sfd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		}

		if (sfd < 0) {
			report(src, "failed", "cannot open the source directory");
			t.failed++;
		} else {
			const char *base = sf_basename(src);
			int before = t.failed;
			copy_tree(sfd, base, dfd, base, a.pol, &t, src);
			close(sfd);

			/* Only the top-level thing created is journalled, not every file
			 * inside a copied tree — undo trashes the tree as one item, which
			 * is what "undo the copy" means. */
			if (t.failed == before) {
				char *made = xasprintf("%s/%s", dest, base);
				if (faccessat(AT_FDCWD, made, F_OK, AT_SYMLINK_NOFOLLOW) == 0)
					sf_journal("copy", made, "");
				free(made);
			}
		}

		free(parent);
		free(src);
	}

	close(dfd);
	free(dest);
	free(a.srcs);
	return finish(&t);
}

int cmd_move(int argc, char **argv)
{
	args_t a = parse_args(argc, argv, "move");

	char *dest = sf_resolve(a.dest);

	/* Opened once, up front, for the same reason as in copy: O_DIRECTORY is
	 * the directory test, and the descriptor is what the loop below works
	 * against instead of re-resolving the name per source. */
	int destfd = open(dest, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (destfd < 0) {
		if (errno == ENOTDIR)
			die("move: %s is not a directory", a.dest);
		die("move: cannot open %s: %s", dest, strerror(errno));
	}

	if (g_out == OUT_REC)
		rec_row(3, "path", "status", "detail");

	tally_t t = { 0, 0, 0 };
	for (int i = 0; i < a.nsrc; i++) {
		char *src = sf_resolve(a.srcs[i]);

		if (sf_is_descendant(src, dest)) {
			report(src, "failed", "destination is inside the source");
			t.failed++;
			free(src);
			continue;
		}

		char *target = xasprintf("%s/%s", dest, sf_basename(src));

		/* Asked THROUGH the destination descriptor rather than by path: the
		 * answer and the action that follows it then refer to the same
		 * directory even if `dest` is replaced underneath us. */
		struct stat dstat, sstat;
		bool exists = fstatat(destfd, sf_basename(src), &dstat,
		                      AT_SYMLINK_NOFOLLOW) == 0;

		/* THE DESTINATION ENTRY CAN BE THE SOURCE — moving something into the
		 * folder it is already in. The overwrite branch below removes the
		 * destination and then renames the source onto it, so on the same
		 * inode it deleted the file and then had nothing to move: "0 done, 1
		 * failed", and the data gone for good. Worse here than in copy, where
		 * at least the removal happens before a copy that then fails; a move
		 * has no second copy anywhere. */
		if (exists && a.pol == CONFLICT_OVERWRITE &&
		    lstat(src, &sstat) == 0 &&
		    sstat.st_dev == dstat.st_dev && sstat.st_ino == dstat.st_ino) {
			report(src, "failed", "source and destination are the same");
			t.failed++;
			free(target);
			free(src);
			continue;
		}

		if (exists) {
			const int dfd = destfd;
			bool handled = false;
			switch (a.pol) {
			case CONFLICT_ERROR:
				report(src, "conflict", "already exists");
				t.failed++;
				handled = true;
				break;
			case CONFLICT_SKIP:
				report(src, "skipped", "already exists");
				t.skipped++;
				handled = true;
				break;
			case CONFLICT_RENAME: {
				char *u = unique_name(dfd, sf_basename(src));
				free(target);
				target = xasprintf("%s/%s", dest, u);
				free(u);
				break;
			}
			case CONFLICT_OVERWRITE:
				sf_rm_rf(dfd, sf_basename(src));
				break;
			}
			if (handled) {
				free(target);
				free(src);
				continue;
			}
		}

		/* The fast path, and the only one that is atomic. */
		if (rename(src, target) == 0) {
			/* target -> src is the inverse. Recorded AFTER success, so a
			 * failed move never leaves an undo entry that would move a file
			 * that was never moved. */
			sf_journal("move", target, src);
			report(src, "done", "moved");
			t.done++;
			free(target);
			free(src);
			continue;
		}

		if (errno != EXDEV) {
			report(src, "failed", strerror(errno));
			t.failed++;
			free(target);
			free(src);
			continue;
		}

		/* Different filesystem: copy, verify, and only then remove. The
		 * source is not touched until the copy has returned success for
		 * every entry — a partial copy followed by a delete is the worst
		 * outcome this program could produce. */
		tally_t sub = { 0, 0, 0 };
		copy_tree(AT_FDCWD, src, destfd, sf_basename(target), a.pol, &sub, src);

		if (sub.failed == 0) {
			if (sf_rm_rf(AT_FDCWD, src) == 0) {
				sf_journal("move", target, src);
				report(src, "done", "copied across filesystems");
				t.done++;
			} else {
				report(src, "failed",
				       "copied, but the original could not be removed");
				t.failed++;
			}
		} else {
			report(src, "failed",
			       "copy incomplete — the original was left alone");
			t.failed++;
		}

		free(target);
		free(src);
	}

	close(destfd);
	free(dest);
	free(a.srcs);
	return finish(&t);
}

int cmd_rename(int argc, char **argv)
{
	if (argc < 2)
		die("rename: need a path and a new name");

	const char *path = argv[0];
	const char *newname = argv[1];

	/* A new name is a NAME. Allowing a "/" would make rename a silent move,
	 * and "../.." a silent move somewhere surprising. Moving is `move`. */
	if (strchr(newname, '/'))
		die("rename: '%s' is a path, not a name — use `synfiles move`", newname);
	if (!*newname || !strcmp(newname, ".") || !strcmp(newname, ".."))
		die("rename: '%s' is not a usable name", newname);

	char *src = sf_resolve(path);
	char *parent = xstrdup(src);
	char *slash = strrchr(parent, '/');
	if (slash && slash != parent)
		*slash = '\0';
	else if (slash)
		parent[1] = '\0';

	char *target = !strcmp(parent, "/") ? xasprintf("/%s", newname)
	                                    : xasprintf("%s/%s", parent, newname);

	int rc = 0;
	if (faccessat(AT_FDCWD, target, F_OK, AT_SYMLINK_NOFOLLOW) == 0) {
		warn("%s already exists", target);
		rc = 1;
	} else if (rename(src, target) != 0) {
		warn("cannot rename %s: %s", src, strerror(errno));
		rc = 1;
	} else if ((sf_journal("rename", target, src)), g_out == OUT_REC) {
		char *e = pct_encode(target, true);
		rec_row(3, "path", "status", "detail");
		rec_row(3, e, "done", "renamed");
		free(e);
	} else {
		printf("renamed to %s\n", newname);
	}

	free(target);
	free(parent);
	free(src);
	return rc;
}

int cmd_mkdir(int argc, char **argv)
{
	if (argc < 1)
		die("mkdir: need a path");

	int rc = 0;
	for (int i = 0; i < argc; i++) {
		if (mkdir(argv[i], 0755) != 0) {
			warn("cannot create %s: %s", argv[i], strerror(errno));
			rc = 1;
		} else {
			char *real = sf_resolve_parent(argv[i]);
			if (real) {
				sf_journal("mkdir", real, "");
				free(real);
			}
		}
	}
	return rc;
}

int cmd_delete(int argc, char **argv)
{
	bool confirmed = false;
	int n = 0;
	char **paths = xmalloc((size_t)(argc + 1) * sizeof *paths);

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--yes"))
			confirmed = true;
		else if (argv[i][0] == '-' && argv[i][1])
			die("delete: unknown option '%s'", argv[i]);
		else
			paths[n++] = argv[i];
	}

	if (n == 0)
		die("delete: need a path");

	/* Permanent deletion is gated behind an explicit flag, and the GUI never
	 * passes it without asking. `synfiles trash` is what a Delete key should
	 * reach — this is the one that cannot be undone. */
	if (!confirmed) {
		free(paths);
		die("delete removes files PERMANENTLY and cannot be undone.\n"
		    "  to move them to the trash instead:  synfiles trash <path>\n"
		    "  to delete them anyway:              synfiles delete --yes <path>");
	}

	if (g_out == OUT_REC)
		rec_row(3, "path", "status", "detail");

	tally_t t = { 0, 0, 0 };
	for (int i = 0; i < n; i++) {
		char *real = sf_resolve(paths[i]);

		/* "/" is not a thing anybody meant to type. */
		if (!strcmp(real, "/")) {
			report(real, "failed", "refusing to delete the root directory");
			t.failed++;
			free(real);
			continue;
		}

		if (sf_rm_rf(AT_FDCWD, real) != 0) {
			report(real, "failed", strerror(errno));
			t.failed++;
		} else {
			report(real, "done", "deleted permanently");
			t.done++;
		}
		free(real);
	}

	free(paths);
	return finish(&t);
}
