/* find.c — searching a tree.
 *
 * Same discipline as listing.c: dirfds all the way down, and symlinks are
 * never followed. That second rule is not a preference here, it is what stops
 * the search hanging — a link pointing at its own ancestor is a loop, and a
 * search that walked it would recurse until it ran out of file descriptors.
 * Links are still REPORTED, they are just never descended through.
 *
 * Two kinds of match, because they cost wildly different amounts:
 *
 *   - by name, which is a glob against each entry and costs nothing;
 *   - by content, which opens and reads every candidate file, and is therefore
 *     bounded by a size cap and skipped entirely for anything that looks
 *     binary.
 *
 * Everything is bounded. A search that walks / with no limit is not a feature,
 * it is a way to make the window stop responding, so there is a result limit
 * and a depth limit and both have defaults.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"
#include "i18n.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Reading a 4GB video looking for the word "invoice" is not a search, it is a
 * stall. Anything larger is matched by name only. */
#define CONTENT_MAX_BYTES (8 * 1024 * 1024)

typedef struct {
	const char *name_pat;   /* already wrapped in wildcards if it needed them */
	const char *content;
	long limit;
	int max_depth;
	bool all;               /* descend into and report dotfiles */
	long found;
	bool truncated;
} search_t;

/* The classic heuristic, and the one grep uses: a NUL byte in the first block
 * means binary. Extension-based guessing would miss a Makefile and wrongly
 * skip a .dat that happens to be text. */
static bool looks_binary(const char *buf, size_t n)
{
	size_t probe = n < 4096 ? n : 4096;
	return memchr(buf, '\0', probe) != NULL;
}

static bool content_matches(int dirfd, const char *name, const char *needle,
                            off_t size)
{
	if (size <= 0 || size > CONTENT_MAX_BYTES)
		return false;

	int fd = openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0)
		return false;

	char *buf = xmalloc((size_t)size + 1);
	size_t len = 0;
	while (len < (size_t)size) {
		ssize_t n = read(fd, buf + len, (size_t)size - len);
		if (n <= 0)
			break;
		len += (size_t)n;
	}
	close(fd);

	bool hit = false;
	if (!looks_binary(buf, len))
		hit = memmem(buf, len, needle, strlen(needle)) != NULL;

	free(buf);
	return hit;
}

static void emit_hit(const char *name, const char *reldir, const struct stat *st,
                     bool is_link, bool broken, const char *target)
{
	bool is_dir = S_ISDIR(st->st_mode);
	const char *type = broken      ? "broken"
	                 : is_dir      ? "dir"
	                 : S_ISREG(st->st_mode) ? "file"
	                 : S_ISFIFO(st->st_mode) ? "fifo"
	                 : S_ISSOCK(st->st_mode) ? "sock"
	                 : S_ISBLK(st->st_mode)  ? "blk"
	                 : S_ISCHR(st->st_mode)  ? "chr" : "other";

	char *ename = pct_encode(name, false);
	char *edir = pct_encode(reldir ? reldir : "", true);
	char *etarget = pct_encode(target ? target : "", true);

	if (g_out == OUT_REC) {
		char *size = xasprintf("%lld", (long long)st->st_size);
		char *mtime = xasprintf("%lld", (long long)st->st_mtime);
		char *mode = xasprintf("%04o", (unsigned)(st->st_mode & 07777));
		rec_row(9, ename, type, size, mtime, mode, is_link ? "1" : "0",
		        etarget, mime_for(name, is_dir), edir);
		free(size);
		free(mtime);
		free(mode);
	} else {
		/* One line per hit, path first. A result list where the location is
		 * on its own line above the name is unreadable and ungreppable —
		 * "config.json" appears eleven times in a source tree and the only
		 * useful thing about a hit is which one it is. */
		if (reldir && *reldir)
			printf("%s%s/%s", C_DIM(), reldir, C_RESET());
		printf("%s%s%s\n", is_dir ? C_ACCENT() : "", name, C_RESET());
	}

	free(ename);
	free(edir);
	free(etarget);
}

/* `reldir` is the directory being scanned, relative to the search root, and is
 * what makes a result say WHERE it was found. Results from a recursive search
 * are useless without it — "config.json" appears eleven times in a source
 * tree. */
static void walk(int dirfd, const char *reldir, int depth, search_t *s)
{
	if (s->found >= s->limit || depth > s->max_depth)
		return;

	DIR *d = fdopendir(dirfd);
	if (!d) {
		close(dirfd);
		return;
	}

	struct dirent *e;
	while ((e = readdir(d))) {
		if (s->found >= s->limit) {
			s->truncated = true;
			break;
		}
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if (!s->all && e->d_name[0] == '.')
			continue;

		struct stat st;
		if (fstatat(dirfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
			continue;

		bool is_link = S_ISLNK(st.st_mode);
		bool broken = false;
		char *target = NULL;
		struct stat resolved = st;

		if (is_link) {
			char buf[4096];
			ssize_t n = readlinkat(dirfd, e->d_name, buf, sizeof buf - 1);
			if (n >= 0) {
				buf[n] = '\0';
				target = xstrdup(buf);
			}
			if (fstatat(dirfd, e->d_name, &resolved, 0) != 0) {
				broken = true;
				resolved = st;
			}
		}

		bool is_dir = !broken && S_ISDIR(resolved.st_mode);

		/* Match. A name pattern and a content pattern given together mean
		 * BOTH must hold — that is what makes "*.c containing malloc" a
		 * useful question rather than two unrelated ones. */
		bool ok = true;
		if (s->name_pat)
			ok = fnmatch(s->name_pat, e->d_name, FNM_CASEFOLD) == 0;
		if (ok && s->content) {
			ok = !is_dir && !is_link
			     && content_matches(dirfd, e->d_name, s->content,
			                        resolved.st_size);
		}

		if (ok) {
			emit_hit(e->d_name, reldir, &resolved, is_link, broken, target);
			s->found++;
		}
		free(target);

		/* Descend. NOT through a symlink: a link to an ancestor is a loop,
		 * and one pointing at / turns a search of a project folder into a
		 * search of the whole machine. */
		if (is_dir && !is_link) {
			int sub = openat(dirfd, e->d_name,
			                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if (sub >= 0) {
				char *nextrel = (reldir && *reldir)
				                ? xasprintf("%s/%s", reldir, e->d_name)
				                : xstrdup(e->d_name);
				walk(sub, nextrel, depth + 1, s);
				free(nextrel);
			}
		}
	}

	closedir(d);
}

int cmd_find(int argc, char **argv)
{
	const char *name = NULL, *content = NULL, *root = NULL;
	search_t s = { NULL, NULL, 1000, 16, false, 0, false };

	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if (!strncmp(a, "--name=", 7))
			name = a + 7;
		else if (!strncmp(a, "--content=", 10))
			content = a + 10;
		else if (!strncmp(a, "--limit=", 8))
			s.limit = strtol(a + 8, NULL, 10);
		else if (!strncmp(a, "--max-depth=", 12))
			s.max_depth = (int)strtol(a + 12, NULL, 10);
		else if (!strcmp(a, "--all") || !strcmp(a, "-a"))
			s.all = true;
		else if (a[0] == '-' && a[1])
			die(_("find: unknown option '%s'"), a);
		else
			root = a;
	}

	if (!name && !content)
		die(_("find: need --name=GLOB or --content=TEXT (or both)"));
	if (s.limit <= 0)
		s.limit = 1000;
	if (s.max_depth <= 0)
		s.max_depth = 16;
	if (!root)
		root = ".";

	/* A pattern with no glob characters means "contains", which is what
	 * somebody typing "invoice" into a search box is asking for. Requiring
	 * them to type *invoice* would be a quiz about fnmatch. */
	char *pattern = NULL;
	if (name) {
		pattern = strpbrk(name, "*?[") ? xstrdup(name)
		                               : xasprintf("*%s*", name);
		s.name_pat = pattern;
	}
	s.content = content;

	int fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		free(pattern);
		die(_("cannot open %s: %s"), root, strerror(errno));
	}

	if (g_out == OUT_REC)
		rec_row(9, "name", "type", "size", "mtime", "mode", "link", "target",
		        "mime", "dir");

	walk(fd, "", 0, &s);
	free(pattern);

	if (g_out == OUT_HUMAN) {
		if (s.found == 0)
			printf("%s%s%s\n", C_DIM(), _("no matches"), C_RESET());
		else if (s.truncated)
			printf("%s", C_DIM());
			printf(_("%ld matches (stopped at the limit)"), s.found);
			printf("%s\n", C_RESET());
	}

	return s.found ? 0 : 100;
}
