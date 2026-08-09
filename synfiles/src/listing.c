/* listing.c — reading a directory.
 *
 * Everything here goes through a DIRECTORY FILE DESCRIPTOR and the *at()
 * family rather than building "dir/name" strings and calling lstat on them.
 * Two reasons, both of which bite in a file manager specifically:
 *
 *   - TOCTOU. Between listing a directory and stat'ing an entry by full path,
 *     any component of that path can be replaced. With a dirfd the entries are
 *     resolved relative to the directory that was actually opened, so a
 *     renamed parent makes the call fail instead of silently answering about a
 *     different file.
 *   - Length. A path can exceed PATH_MAX by nesting even when every individual
 *     component is short, and the by-name form is the one that breaks.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	char  *name;      /* raw bytes, as the kernel gave them */
	char  *target;    /* symlink target, raw bytes; NULL if not a link */
	bool   is_dir;    /* AFTER following, so a link to a directory sorts as one */
	bool   is_link;
	bool   broken;    /* a symlink whose target does not resolve */
	const char *type;
	/* Resolved ONCE at scan time. mime_for() walks ~1500 globs, and calling it
	 * from inside the qsort comparator would pay that on every comparison —
	 * O(n log n) glob scans to sort a directory by type. */
	const char *mime;
	off_t  size;
	time_t mtime;
	mode_t mode;
} entry_t;

typedef enum { SORT_NAME, SORT_SIZE, SORT_MTIME, SORT_TYPE } sort_t;

static sort_t g_sort = SORT_NAME;
static bool   g_reverse;
static bool   g_dirs_first = true;

static const char *kind_of(mode_t m)
{
	if (S_ISDIR(m))  return "dir";
	if (S_ISREG(m))  return "file";
	if (S_ISFIFO(m)) return "fifo";
	if (S_ISSOCK(m)) return "sock";
	if (S_ISBLK(m))  return "blk";
	if (S_ISCHR(m))  return "chr";
	return "other";
}

/* Case-insensitive, but only as a TIE-BREAK on top of a byte comparison.
 * strcasecmp alone makes "README" and "readme" compare equal, and qsort with
 * an inconsistent comparator gives an unstable order that changes between
 * runs — a listing that reshuffles itself on refresh. */
static int name_cmp(const char *a, const char *b)
{
	int c = strcasecmp(a, b);
	return c ? c : strcmp(a, b);
}

static int entry_cmp(const void *va, const void *vb)
{
	const entry_t *a = va, *b = vb;

	/* Directories first is not a sort key the user picked — it survives
	 * every sort mode and every reversal, the way it does in Dolphin. A
	 * reversed listing that buries the folders at the bottom is not what
	 * "sort descending by size" was asking for. */
	if (g_dirs_first && a->is_dir != b->is_dir)
		return a->is_dir ? -1 : 1;

	int c = 0;
	switch (g_sort) {
	case SORT_SIZE:
		c = (a->size < b->size) ? -1 : (a->size > b->size) ? 1 : 0;
		break;
	case SORT_MTIME:
		c = (a->mtime < b->mtime) ? -1 : (a->mtime > b->mtime) ? 1 : 0;
		break;
	case SORT_TYPE:
		c = strcmp(a->mime, b->mime);
		break;
	case SORT_NAME:
	default:
		break;
	}

	if (c == 0)
		c = name_cmp(a->name, b->name);
	return g_reverse ? -c : c;
}

static void emit_header(void)
{
	rec_row(8, "name", "type", "size", "mtime", "mode", "link", "target", "mime");
}

static void emit_entry(const entry_t *e)
{
	const char *mime = e->mime;
	char *name = pct_encode(e->name, false);
	char *target = e->target ? pct_encode(e->target, true) : xstrdup("");

	if (g_out == OUT_REC) {
		char *size = xasprintf("%lld", (long long)e->size);
		char *mtime = xasprintf("%lld", (long long)e->mtime);
		char *mode = xasprintf("%04o", (unsigned)(e->mode & 07777));
		rec_row(8, name, e->type, size, mtime, mode,
		        e->is_link ? "1" : "0", target, mime);
		free(size);
		free(mtime);
		free(mode);
	} else {
		char *hs = human_size(e->size);
		char when[32] = "";
		struct tm tm;
		if (localtime_r(&e->mtime, &tm))
			strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm);

		printf("%s%-40s%s %s%10s%s  %s%s%s",
		       e->is_dir ? C_ACCENT() : "", e->name, C_RESET(),
		       C_DIM(), e->is_dir ? "" : hs, C_RESET(),
		       C_DIM(), when, C_RESET());
		if (e->is_link)
			printf("  %s-> %s%s", C_DIM(), e->target ? e->target : "?", C_RESET());
		if (e->broken)
			printf("  %s[broken]%s", C_WARN(), C_RESET());
		putchar('\n');
		free(hs);
	}

	free(name);
	free(target);
}

/* readlinkat into a malloc'd string. The size is discovered by growing rather
 * than trusting st_size: procfs and some network filesystems report 0 for a
 * link that has a perfectly good target. */
static char *read_link(int dirfd, const char *name)
{
	size_t cap = 256;
	for (;;) {
		char *buf = xmalloc(cap);
		ssize_t n = readlinkat(dirfd, name, buf, cap - 1);
		if (n < 0) {
			free(buf);
			return NULL;
		}
		if ((size_t)n < cap - 1) {
			buf[n] = '\0';
			return buf;
		}
		free(buf);
		cap *= 2;
		if (cap > (1u << 20))
			return NULL;
	}
}

int cmd_list(int argc, char **argv)
{
	bool all = false;
	const char *dir = NULL;

	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--all") || !strcmp(a, "-a"))
			all = true;
		else if (!strcmp(a, "--reverse") || !strcmp(a, "-r"))
			g_reverse = true;
		else if (!strcmp(a, "--no-dirs-first"))
			g_dirs_first = false;
		else if (!strncmp(a, "--sort=", 7)) {
			const char *s = a + 7;
			if      (!strcmp(s, "name"))  g_sort = SORT_NAME;
			else if (!strcmp(s, "size"))  g_sort = SORT_SIZE;
			else if (!strcmp(s, "mtime")) g_sort = SORT_MTIME;
			else if (!strcmp(s, "type"))  g_sort = SORT_TYPE;
			else die("list: unknown sort '%s' — try name, size, mtime, type", s);
		} else if (a[0] == '-' && a[1]) {
			die("list: unknown option '%s'", a);
		} else {
			dir = a;
		}
	}

	if (!dir)
		dir = ".";

	int dirfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0)
		die("cannot open %s: %s", dir, strerror(errno));

	/* fdopendir takes ownership of the fd, but dirfd() hands it back for the
	 * *at() calls below — closedir() closes it exactly once at the end. */
	DIR *d = fdopendir(dirfd);
	if (!d) {
		close(dirfd);
		die("cannot read %s: %s", dir, strerror(errno));
	}

	size_t cap = 128, n = 0;
	entry_t *ents = xmalloc(cap * sizeof *ents);

	struct dirent *de;
	while ((de = readdir(d))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (!all && de->d_name[0] == '.')
			continue;

		struct stat st;
		if (fstatat(dirfd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
			continue;   /* vanished between readdir and stat — not an error */

		entry_t e = { 0 };
		e.name = xstrdup(de->d_name);
		e.is_link = S_ISLNK(st.st_mode);

		if (e.is_link) {
			e.target = read_link(dirfd, de->d_name);

			/* A symlink is presented as the thing it points AT — that is what
			 * makes a link to a folder open like a folder and sort with the
			 * folders. The link's own stat is kept only for `broken`. */
			struct stat ts;
			if (fstatat(dirfd, de->d_name, &ts, 0) == 0) {
				e.is_dir = S_ISDIR(ts.st_mode);
				e.type   = kind_of(ts.st_mode);
				e.size   = ts.st_size;
				e.mtime  = ts.st_mtime;
				e.mode   = ts.st_mode;
			} else {
				e.broken = true;
				e.type   = "broken";
				e.size   = st.st_size;
				e.mtime  = st.st_mtime;
				e.mode   = st.st_mode;
			}
		} else {
			e.is_dir = S_ISDIR(st.st_mode);
			e.type   = kind_of(st.st_mode);
			e.size   = st.st_size;
			e.mtime  = st.st_mtime;
			e.mode   = st.st_mode;
		}

		e.mime = mime_for(e.name, e.is_dir);

		if (n == cap) {
			cap *= 2;
			ents = xrealloc(ents, cap * sizeof *ents);
		}
		ents[n++] = e;
	}

	qsort(ents, n, sizeof *ents, entry_cmp);

	if (g_out == OUT_REC)
		emit_header();
	for (size_t i = 0; i < n; i++)
		emit_entry(&ents[i]);

	if (g_out == OUT_HUMAN && n == 0)
		printf("%sempty%s\n", C_DIM(), C_RESET());

	for (size_t i = 0; i < n; i++) {
		free(ents[i].name);
		free(ents[i].target);
	}
	free(ents);
	closedir(d);

	return n ? 0 : 100;
}

/* ── info: what a properties pane needs ─────────────────────────────────── */

static void kv(const char *key, const char *value)
{
	if (g_out == OUT_REC)
		rec_row(2, key, value);
	else
		printf("%s%-12s%s %s\n", C_DIM(), key, C_RESET(), value);
}

int cmd_info(int argc, char **argv)
{
	if (argc < 1)
		die("info: need a path");
	const char *path = argv[0];

	struct stat st;
	if (lstat(path, &st) != 0)
		die("cannot stat %s: %s", path, strerror(errno));

	bool is_link = S_ISLNK(st.st_mode);
	struct stat tst = st;
	bool broken = false;
	if (is_link && stat(path, &tst) != 0) {
		broken = true;
		tst = st;
	}

	if (g_out == OUT_REC)
		rec_row(2, "key", "value");

	char *epath = pct_encode(path, true);
	kv("path", epath);
	free(epath);

	const char *base = strrchr(path, '/');
	char *ename = pct_encode(base ? base + 1 : path, false);
	kv("name", ename);
	free(ename);

	bool is_dir = S_ISDIR(tst.st_mode);
	kv("type", broken ? "broken" : kind_of(tst.st_mode));
	kv("mime", mime_for(base ? base + 1 : path, is_dir));
	kv("icon", icon_for(mime_for(base ? base + 1 : path, is_dir), is_dir));

	char *n;
	n = xasprintf("%lld", (long long)tst.st_size);          kv("size", n); free(n);
	n = xasprintf("%04o", (unsigned)(tst.st_mode & 07777)); kv("mode", n); free(n);
	n = xasprintf("%lld", (long long)tst.st_mtime);         kv("mtime", n); free(n);
	n = xasprintf("%lld", (long long)tst.st_atime);         kv("atime", n); free(n);
	n = xasprintf("%lld", (long long)tst.st_ctime);         kv("ctime", n); free(n);
	n = xasprintf("%lu", (unsigned long)tst.st_nlink);      kv("links", n); free(n);

	struct passwd *pw = getpwuid(tst.st_uid);
	struct group  *gr = getgrgid(tst.st_gid);
	if (pw) kv("owner", pw->pw_name);
	else { n = xasprintf("%lu", (unsigned long)tst.st_uid); kv("owner", n); free(n); }
	if (gr) kv("group", gr->gr_name);
	else { n = xasprintf("%lu", (unsigned long)tst.st_gid); kv("group", n); free(n); }

	kv("link", is_link ? "1" : "0");
	if (is_link) {
		char *t = read_link(AT_FDCWD, path);
		char *et = pct_encode(t ? t : "", true);
		kv("target", et);
		free(et);
		free(t);
	}

	return 0;
}
