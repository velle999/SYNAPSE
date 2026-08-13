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

/* sf_entry_t now lives in synfiles.h — the TUI reads the same rows. */

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
	const sf_entry_t *a = va, *b = vb;

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
	rec_row(9, "name", "type", "size", "mtime", "mode", "link", "target", "mime",
	        "icon");
}

static void emit_entry(const sf_entry_t *e)
{
	const char *mime = e->mime;
	char *name = pct_encode(e->name, false);
	char *target = e->target ? pct_encode(e->target, true) : xstrdup("");

	if (g_out == OUT_REC) {
		char *size = xasprintf("%lld", (long long)e->size);
		char *mtime = xasprintf("%lld", (long long)e->mtime);
		char *mode = xasprintf("%04o", (unsigned)(e->mode & 07777));
		char *icon = e->icon ? pct_encode(e->icon, true) : xstrdup("");
		rec_row(9, name, e->type, size, mtime, mode,
		        e->is_link ? "1" : "0", target, mime, icon);
		free(icon);
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
/* The Icon= a .desktop launcher names.
 *
 * This is the ONE place a listing opens a file, and the exception is narrow on
 * purpose: only for application/x-desktop, only under DESKTOP_MAX bytes, and
 * the whole point of the file type is to name an icon. Without it ~/Desktop is
 * two hundred identical grey sheets — the generic application-x-desktop icon —
 * where the user put two hundred DIFFERENT games. Dolphin reads the same key
 * for the same reason.
 *
 * Deliberately NOT a .desktop parser: the first Icon= inside [Desktop Entry],
 * stopping at the next group header, because an [Desktop Action] block further
 * down carries its own Icon= and that is not the launcher's.
 */
#define DESKTOP_MAX 65536

static char *desktop_icon(int dirfd, const char *name)
{
	int fd = openat(dirfd, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return NULL;

	char buf[DESKTOP_MAX];
	ssize_t got = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (got <= 0)
		return NULL;
	buf[got] = '\0';

	char *icon = NULL;
	bool in_entry = false;
	for (char *line = buf, *next; line && *line; line = next) {
		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';

		while (*line == ' ' || *line == '\t')
			line++;
		if (line[0] == '[') {
			if (in_entry)
				break;            /* left [Desktop Entry] without finding one */
			in_entry = !strncmp(line, "[Desktop Entry]", 15);
			continue;
		}
		if (!in_entry || strncmp(line, "Icon", 4))
			continue;

		char *eq = line + 4;
		while (*eq == ' ' || *eq == '\t')
			eq++;
		if (*eq != '=')
			continue;             /* "IconName=" is not "Icon=" */
		eq++;
		while (*eq == ' ' || *eq == '\t')
			eq++;

		size_t len = strlen(eq);
		while (len && (eq[len - 1] == '\r' || eq[len - 1] == ' '))
			len--;
		if (len)
			icon = xstrndup(eq, len);
		break;
	}
	return icon;
}

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

/* ── the scan, on its own ────────────────────────────────────────────────────
 *
 * Pulled out of cmd_list so the TUI reads a directory through the SAME code
 * that `list` prints. The alternative — a second walk inside tui.c — is a
 * second set of answers about symlinks, broken links, .desktop icons and sort
 * order, and the two would drift on the first bug fixed in one of them.
 *
 * Sort options travel through the file-static g_sort/g_reverse/g_dirs_first,
 * which is what entry_cmp already reads: qsort takes no context argument, and
 * threading one through qsort_r to avoid three statics would be a bigger change
 * than the problem deserves in a single-threaded program.
 *
 * Returns NULL and sets *n to 0 when the directory cannot be read — a TUI must
 * be able to survive a cd into something unreadable, so this reports rather
 * than dying the way cmd_list may.
 */
sf_entry_t *sf_scan(const char *dir, bool all, size_t *count)
{
	*count = 0;

	int dirfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0)
		return NULL;

	DIR *d = fdopendir(dirfd);
	if (!d) {
		close(dirfd);
		return NULL;
	}

	size_t cap = 128, n = 0;
	sf_entry_t *ents = xmalloc(cap * sizeof *ents);

	struct dirent *de;
	while ((de = readdir(d))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (!all && de->d_name[0] == '.')
			continue;

		struct stat st;
		if (fstatat(dirfd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
			continue;   /* vanished between readdir and stat — not an error */

		sf_entry_t e = { 0 };
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
		if (!e.is_dir && e.mime && !strcmp(e.mime, "application/x-desktop")
		    && e.size > 0 && e.size < DESKTOP_MAX)
			e.icon = desktop_icon(dirfd, de->d_name);

		if (n == cap) {
			cap *= 2;
			ents = xrealloc(ents, cap * sizeof *ents);
		}
		ents[n++] = e;
	}
	closedir(d);

	qsort(ents, n, sizeof *ents, entry_cmp);
	*count = n;
	return ents;
}

void sf_entries_free(sf_entry_t *ents, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		free(ents[i].name);
		free(ents[i].target);
		free(ents[i].icon);
	}
	free(ents);
}

void sf_sort_set(sf_sort_t sort, bool reverse, bool dirs_first)
{
	g_sort = (sort_t)sort;
	g_reverse = reverse;
	g_dirs_first = dirs_first;
}

sf_sort_t sf_sort_get(void) { return (sf_sort_t)g_sort; }

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

	size_t n = 0;
	sf_entry_t *ents = sf_scan(dir, all, &n);
	if (!ents)
		die("cannot read %s: %s", dir, strerror(errno));

	if (g_out == OUT_REC)
		emit_header();
	for (size_t i = 0; i < n; i++)
		emit_entry(&ents[i]);

	if (g_out == OUT_HUMAN && n == 0)
		printf("%sempty%s\n", C_DIM(), C_RESET());

	sf_entries_free(ents, n);
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
	const char *mime = mime_for(base ? base + 1 : path, is_dir);
	kv("type", broken ? "broken" : kind_of(tst.st_mode));
	kv("mime", mime);
	kv("icon", icon_for(mime, is_dir));

	char *n;
	n = xasprintf("%lld", (long long)tst.st_size);          kv("size", n); free(n);

	/* How big the picture is — the one question a properties pane is asked
	 * that stat() cannot answer. Emitted only when the file actually has
	 * dimensions: a row reading "resolution  unknown" on every text file is
	 * noise, and an empty one looks like a bug in the reader. WIDTHxHEIGHT so
	 * the field stays one token a script can split; the GUI is what turns it
	 * into a × for reading. */
	if (!is_dir && !broken) {
		long rw = 0, rh = 0;
		if (resolution_for(path, mime, &rw, &rh)) {
			n = xasprintf("%ldx%ld", rw, rh);
			kv("resolution", n);
			free(n);
		}
	}

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

/* ── du — what a folder actually holds ───────────────────────────────────────
 *
 * `info` reports st_size, and for a directory that is the size of the DIRECTORY
 * ENTRY — 890 bytes for a tree holding an ISO. It is the correct answer to the
 * question stat() was asked and the wrong answer to the one a properties pane
 * is asking, which is "how much is in here".
 *
 * WHY THIS IS A SEPARATE COMMAND, AND WHY IT STREAMS
 *
 * Walking a large tree takes seconds to minutes, and `info` is what opens the
 * properties panel — folding the walk into it would freeze the panel on
 * exactly the folders anyone would ask about. So this is its own command, the
 * panel starts it after `info` has already drawn, and it prints a running
 * total that the window renders as it arrives. A number that climbs is also
 * the only honest progress indicator available here: there is no cheap way to
 * know the total ahead of time, which is the whole reason this is slow.
 *
 * TWO NUMBERS, because they answer different questions and disagree a lot:
 *   bytes — apparent size, the sum of st_size. What "this folder is 7.2 GB"
 *           means to a person, and what has to fit when it is copied.
 *   disk  — st_blocks * 512. What it costs on THIS filesystem, so a tree of
 *           tiny files reads larger and a sparse file or a btrfs-compressed
 *           one reads smaller.
 *
 * Symlinks are counted as links (lstat, never followed): following them
 * double-counts anything reachable twice and hangs forever on a cycle.
 *
 * Hard links are counted ONCE. A tree with a 4 GB file linked into it twice
 * does not hold 8 GB, and pacman's cache and any backup tree are full of them.
 * The seen-set is (dev, ino) and only files with st_nlink > 1 go into it —
 * putting every inode in it would cost more memory than the walk saves.
 */

struct du_seen {
	dev_t dev;
	ino_t ino;
	struct du_seen *next;
};

#define DU_BUCKETS 1024

static bool du_seen_add(struct du_seen **tab, dev_t dev, ino_t ino)
{
	size_t h = (size_t)(ino ^ (dev << 8)) % DU_BUCKETS;
	for (struct du_seen *s = tab[h]; s; s = s->next)
		if (s->ino == ino && s->dev == dev)
			return true;                     /* already counted */
	struct du_seen *n = xmalloc(sizeof *n);
	n->dev = dev; n->ino = ino; n->next = tab[h];
	tab[h] = n;
	return false;
}

static void du_seen_free(struct du_seen **tab)
{
	for (size_t i = 0; i < DU_BUCKETS; i++) {
		struct du_seen *s = tab[i];
		while (s) {
			struct du_seen *n = s->next;
			free(s);
			s = n;
		}
	}
}

struct du_acc {
	long long bytes, disk, files, dirs;
	struct du_seen **seen;
	time_t last_report;
	bool done;
};

static void du_report(const struct du_acc *a)
{
	char *b = xasprintf("%lld", a->bytes);
	char *d = xasprintf("%lld", a->disk);
	char *f = xasprintf("%lld", a->files);
	char *r = xasprintf("%lld", a->dirs);

	if (g_out == OUT_REC) {
		/* One record per report, five fields, the last saying whether this is
		 * the final one. A reader that only wants the answer waits for
		 * done=1; a reader that wants progress draws every row. */
		rec_row(5, b, d, f, r, a->done ? "1" : "0");
	} else if (a->done) {
		printf("%s%-12s%s %lld\n", C_DIM(), "bytes", C_RESET(), a->bytes);
		printf("%s%-12s%s %lld\n", C_DIM(), "disk",  C_RESET(), a->disk);
		printf("%s%-12s%s %lld\n", C_DIM(), "files", C_RESET(), a->files);
		printf("%s%-12s%s %lld\n", C_DIM(), "dirs",  C_RESET(), a->dirs);
	}
	fflush(stdout);
	free(b); free(d); free(f); free(r);
}

/* Depth-first with an open fd per level, so the walk is immune to a rename
 * happening underneath it and never builds a path longer than PATH_MAX. */
static void du_walk(int dirfd, struct du_acc *a)
{
	DIR *d = fdopendir(dirfd);
	if (!d) {
		close(dirfd);
		return;
	}

	struct dirent *e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;

		struct stat st;
		if (fstatat(dirfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
			continue;     /* vanished mid-walk, or unreadable: not fatal */

		if (S_ISDIR(st.st_mode)) {
			a->dirs++;
			/* The directory's OWN st_size is not added to `bytes`. That
			 * number is the size of the directory entry — the 890-byte
			 * answer this command exists to replace — and adding it back
			 * would be the same mistake, just spread thinner. `disk` does
			 * count its blocks, because they really are occupied. Both
			 * choices match what `du -sb` and `du -s` report. */
			a->disk  += (long long)st.st_blocks * 512;
			int fd = openat(dirfd, e->d_name,
			                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if (fd >= 0)
				du_walk(fd, a);       /* takes ownership of fd */
			continue;
		}

		/* Counted once, however many names it has. */
		if (st.st_nlink > 1 && du_seen_add(a->seen, st.st_dev, st.st_ino))
			continue;

		a->files++;
		a->bytes += st.st_size;
		a->disk  += (long long)st.st_blocks * 512;

		/* Progress, at most once a second. Reporting per file turns a walk of
		 * a kernel tree into hundreds of thousands of writes and makes the
		 * reader the bottleneck. */
		time_t now = time(NULL);
		if (now != a->last_report) {
			a->last_report = now;
			du_report(a);
		}
	}
	closedir(d);      /* closes dirfd */
}

int cmd_du(int argc, char **argv)
{
	if (argc < 1)
		die("du: need a path");
	const char *path = argv[0];

	struct stat st;
	if (lstat(path, &st) != 0)
		die("cannot stat %s: %s", path, strerror(errno));

	if (g_out == OUT_REC)
		rec_row(5, "bytes", "disk", "files", "dirs", "done");

	struct du_seen **seen = xmalloc(DU_BUCKETS * sizeof *seen);
	for (size_t i = 0; i < DU_BUCKETS; i++)
		seen[i] = NULL;

	struct du_acc a = { 0, 0, 0, 0, seen, time(NULL), false };

	if (S_ISDIR(st.st_mode)) {
		a.disk  += (long long)st.st_blocks * 512;
		int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (fd < 0)
			die("cannot read %s: %s", path, strerror(errno));
		du_walk(fd, &a);
	} else {
		/* A plain file is a legitimate argument and answers in one line, so
		 * the caller does not need to know which it has before asking. */
		a.files = 1;
		a.bytes = st.st_size;
		a.disk  = (long long)st.st_blocks * 512;
	}

	a.done = true;
	du_report(&a);

	du_seen_free(seen);
	free(seen);
	return 0;
}
