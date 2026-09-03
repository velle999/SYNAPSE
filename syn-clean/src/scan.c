/* scan.c — what there is to clean, how big it is, and removing it.
 *
 * ⛔ EVERY PATH HERE IS BUILT FROM home_path(), NEVER FROM A CATEGORY STRING
 * PASTED INTO A COMMAND. This file deletes directory trees; the one bug class
 * that matters is a path that escapes where it was meant to be, so the roots
 * are composed in C and the walker refuses to cross a mount point or follow a
 * symlink out of the tree it was given.
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
#include <time.h>
#include <unistd.h>

/* ── The table ──────────────────────────────────────────────────────────────
 *
 * ⚠ `conflicts` IS NOT CAUTION, IT IS THE DIFFERENCE BETWEEN CLEANING A
 * PROFILE AND CORRUPTING ONE. Cookies and browser caches are sqlite databases
 * with write-ahead logs; removing one while the browser has it open leaves the
 * -wal and -shm files behind pointing at a database that is gone, and the
 * browser's next start is a profile it cannot read. Every category that names a
 * process refuses while that process is running.
 */
/*
 * ⛔ THE FIRST FIELD IS THE COMMAND AND THE SECOND IS THE SENTENCE, one struct
 * field apart. `syn-clean clean browsercache` takes that exact id back and the
 * window sends it, so it stays English in every language; the label and the
 * "what" are marked with N_() and translated where they are DRAWN, which
 * leaves the record carrying the English word for the window to look up.
 *
 * ⛔ AND `conflicts` IS MATCHED AGAINST /proc/<pid>/comm. It is a list of
 * process names, not a list of words.
 */
const category_t g_categories[] = {
	{ "thumbnails", N_("Thumbnails"), N_("image previews, rebuilt on demand"),
	  false, NULL, false },
	{ "usercache", N_("Application cache"), N_("~/.cache, minus the rows below"),
	  false, NULL, false },
	{ "trash", N_("Trash"), N_("files you already deleted"),
	  false, NULL, false },
	{ "crash", N_("Crash reports"), N_("core dumps and crash logs"),
	  false, NULL, false },
	{ "browsercache", N_("Browser cache"),
	  N_("pages and images, re-downloaded as needed"),
	  false, "firefox chromium vivaldi-bin chrome brave", false },
	/* ⚠ SIGNS YOU OUT EVERYWHERE. Not grouped with the caches for that reason:
	 * a cache is invisible when it goes, and this is the one category whose
	 * effect the user will notice on every site they use. */
	{ "cookies", N_("Cookies"), N_("SIGNS YOU OUT of every site"),
	  false, "firefox chromium vivaldi-bin chrome brave", true },
	{ "tmp", N_("Temporary files"), N_("your own leftovers in /tmp and /var/tmp"),
	  false, NULL, false },
	{ "orphans", N_("Orphaned packages"),
	  N_("installed as dependencies, needed by nothing"),
	  true, NULL, false },
	{ "pkgcache", N_("Package cache"), N_("downloaded packages already installed"),
	  true, NULL, false },
	{ "journal", N_("System logs"), N_("the journal, trimmed to the last week"),
	  true, NULL, false },
};
const size_t g_ncategories = sizeof g_categories / sizeof g_categories[0];

const category_t *category_find(const char *id)
{
	for (size_t i = 0; i < g_ncategories; i++)
		if (!strcmp(g_categories[i].id, id)) return &g_categories[i];
	return NULL;
}

/* ── Is something holding it open ───────────────────────────────────────── */

/* ⚠ MATCHED ON THE COMM NAME, not on a path: a browser may be /usr/lib/firefox
 * /firefox, a flatpak wrapper, or a snap, and all three are "firefox" in
 * /proc/<pid>/comm. Reading /proc directly rather than shelling out to pgrep
 * keeps this working on a machine where procps is not installed. */
static bool process_running(const char *comm)
{
	DIR *d = opendir("/proc");
	if (!d) return false;
	bool found = false;
	struct dirent *e;
	while (!found && (e = readdir(d))) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
		char p[286];
		snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
		FILE *f = fopen(p, "r");
		if (!f) continue;
		char line[128];
		if (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\n")] = '\0';
			if (!strcmp(line, comm)) found = true;
		}
		fclose(f);
	}
	closedir(d);
	return found;
}

/* ⚠ Returns a pointer into a static buffer, so the caller gets a name it can
 * print without owning it — and only ONE name, because "close Firefox and
 * Chromium and Vivaldi" is a worse sentence than naming the one that is
 * actually in the way. */
const char *category_blocked_by(const category_t *c)
{
	static char found[64];
	if (!c->conflicts) return NULL;
	char *list = xstrdup(c->conflicts);
	char *save = NULL, *tok = strtok_r(list, " ", &save);
	const char *hit = NULL;
	while (tok) {
		if (process_running(tok)) {
			snprintf(found, sizeof found, "%s", tok);
			hit = found;
			break;
		}
		tok = strtok_r(NULL, " ", &save);
	}
	free(list);
	return hit;
}

/* ⛔ SUBDIRECTORIES OF ~/.cache THAT ANOTHER ROW ALREADY COUNTS. Without this
 * list `usercache` walks the browser caches and the thumbnails as well as its
 * own, and the scan reports the same bytes on two rows — so the total at the
 * bottom is a number that was never true. It was 63 GB on the first real run,
 * of which 2.5 GB was counted twice. Every name here MUST match the leaf of a
 * root in roots_for() for its own category. */
static const char *g_cache_owned_elsewhere[] = {
	"thumbnails",                                    /* thumbnails */
	"mozilla", "chromium", "vivaldi",
	"google-chrome", "BraveSoftware",                /* browsercache */
	"crash",                                         /* crash */
	NULL
};

static bool cache_owned_elsewhere(const char *name)
{
	for (const char **p = g_cache_owned_elsewhere; *p; p++)
		if (!strcmp(*p, name)) return true;
	return false;
}

/* ── Walking a tree ─────────────────────────────────────────────────────── */

/*
 * ⛔ NEVER ACROSS A MOUNT POINT, AND NEVER THROUGH A SYMLINK. `~/.cache` may
 * contain a bind mount or a link somebody made to another disk; a cleaner that
 * followed either would remove files nowhere near the directory it was asked
 * about. The device number of the root is remembered and every directory is
 * checked against it, and directories are identified by lstat so a symlink is
 * a file to unlink, never a tree to descend.
 */
typedef struct {
	unsigned long long bytes, files;
	dev_t dev;
	bool  remove;
} walk_t;

/*
 * ⛔ THE TREE IS WALKED BY DESCRIPTOR, NOT BY NAME. Every entry is stat'd with
 * fstatat() and removed with unlinkat(), both relative to the fd of the
 * directory it was read from — so the thing measured and the thing deleted are
 * the same thing. Re-resolving the path between the check and the unlink is a
 * window in which a directory component can be swapped for a symlink, and this
 * program's whole job is deleting what it finds.
 *
 * ⚠ fdopendir() TAKES OWNERSHIP of the descriptor: closedir() closes it, so it
 * must not be closed again here.
 */
static int walk_dir(int dfd, walk_t *w, int depth)
{
	DIR *d = fdopendir(dfd);
	if (!d) { close(dfd); return 0; }

	struct dirent *e;
	int rc = 0;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;

		struct stat st;
		if (fstatat(dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;

		if (S_ISDIR(st.st_mode)) {
			if (st.st_dev != w->dev) continue;   /* another filesystem */
			if (depth > 64) continue;            /* a loop somebody made */
			int sub = openat(dirfd(d), e->d_name,
			                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if (sub < 0) continue;
			if (walk_dir(sub, w, depth + 1) != 0) rc = -1;
			/* Last, and only if it emptied. A refusal inside has already been
			 * counted, so a failing rmdir is not news. */
			if (w->remove && !g_dry)
				unlinkat(dirfd(d), e->d_name, AT_REMOVEDIR);
		} else {
			/* st_blocks is what the file COSTS, which is the number somebody
			 * looking to free space cares about; st_size counts holes in a
			 * sparse file that were never on the disk. */
			w->bytes += (unsigned long long)st.st_blocks * 512ULL;
			w->files++;
			if (w->remove && !g_dry && unlinkat(dirfd(d), e->d_name, 0) != 0)
				rc = -1;
		}
	}
	closedir(d);
	return rc;
}

static int tree(const char *path, walk_t *w, bool remove)
{
	w->remove = remove;

	int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		/* Not a directory, or a symlink where one was expected — so it is a
		 * single entry, reached through its PARENT's descriptor for the same
		 * reason the walk below is: the name that is measured and the name that
		 * is unlinked have to be the one name, resolved once. */
		char *dup = xstrdup(path);
		char *slash = strrchr(dup, '/');
		const char *base = slash ? slash + 1 : dup;
		const char *dir  = slash ? dup : ".";
		if (slash) { if (slash == dup) dir = "/"; else *slash = '\0'; }

		int pfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (pfd < 0) { free(dup); return 0; }

		struct stat st;
		if (fstatat(pfd, base, &st, AT_SYMLINK_NOFOLLOW) == 0) {
			w->bytes += (unsigned long long)st.st_blocks * 512ULL;
			w->files++;
			if (remove && !g_dry) unlinkat(pfd, base, 0);
		}
		close(pfd);
		free(dup);
		return 0;                                  /* absent is not a failure */
	}

	struct stat st;
	if (fstat(fd, &st) != 0) { close(fd); return 0; }
	w->dev = st.st_dev;

	int rc = walk_dir(fd, w, 0);
	/* toctou-ok: the tree under this root is gone by now; rmdir on the root
	 * itself is the last act and nothing reopens the name after it. */
	if (remove && !g_dry) rmdir(path);
	return rc;
}

/* ── The roots each category owns ───────────────────────────────────────── */

/* Returns a NULL-terminated array the caller frees (paths and array both). */
static char **roots_for(const category_t *c, int *n)
{
	char **v = calloc(24, sizeof *v);
	if (!v) die("%s", _("out of memory"));
	int i = 0;

	if (!strcmp(c->id, "thumbnails")) {
		v[i++] = home_path(".cache/thumbnails");
	} else if (!strcmp(c->id, "usercache")) {
		v[i++] = home_path(".cache");
	} else if (!strcmp(c->id, "pkgcache")) {
		v[i++] = xstrdup("/var/cache/pacman/pkg");
	} else if (!strcmp(c->id, "journal")) {
		v[i++] = xstrdup("/var/log/journal");
	} else if (!strcmp(c->id, "trash")) {
		v[i++] = home_path(".local/share/Trash/files");
		v[i++] = home_path(".local/share/Trash/info");
	} else if (!strcmp(c->id, "crash")) {
		v[i++] = home_path(".cache/crash");
		v[i++] = home_path(".local/share/apport");
	} else if (!strcmp(c->id, "browsercache")) {
		v[i++] = home_path(".cache/mozilla");
		v[i++] = home_path(".cache/chromium");
		v[i++] = home_path(".cache/vivaldi");
		v[i++] = home_path(".cache/google-chrome");
		v[i++] = home_path(".cache/BraveSoftware");
	} else if (!strcmp(c->id, "cookies")) {
		/* ⚠ NAMED FILES, NOT A TREE. Everything else here is a directory that
		 * exists to be disposable; a cookie jar sits INSIDE the live profile,
		 * beside the bookmarks and the saved passwords, and a walker pointed at
		 * that directory would take all of it. */
		v[i++] = home_path(".mozilla/firefox");   /* resolved per-profile below */
	}
	*n = i;
	return v;
}

/* Firefox keeps one cookie jar per profile and the profile directory name is
 * random, so the jars are found rather than composed. */
static void cookies_each(void (*fn)(const char *, void *), void *ctx)
{
	char *base = home_path(".mozilla/firefox");
	DIR *d = opendir(base);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.') continue;
			for (const char **f = (const char *[]){ "cookies.sqlite",
			         "cookies.sqlite-wal", "cookies.sqlite-shm", NULL }; *f; f++) {
				char *p = xasprintf("%s/%s/%s", base, e->d_name, *f);
				fn(p, ctx);
				free(p);
			}
		}
		closedir(d);
	}
	free(base);

	/* Chromium-family: <config>/<Browser>/<Profile>/Cookies */
	const char *chrom[] = { ".config/chromium", ".config/vivaldi",
	                        ".config/google-chrome", ".config/BraveSoftware/Brave-Browser",
	                        NULL };
	for (const char **c = chrom; *c; c++) {
		char *cb = home_path(*c);
		DIR *cd = opendir(cb);
		if (cd) {
			struct dirent *e;
			while ((e = readdir(cd))) {
				if (e->d_name[0] == '.') continue;
				char *p = xasprintf("%s/%s/Cookies", cb, e->d_name);
				fn(p, ctx);
				free(p);
			}
			closedir(cd);
		}
		free(cb);
	}
}

struct cookie_acc { unsigned long long bytes, files; bool remove; };

static void cookie_one(const char *path, void *ctx)
{
	struct cookie_acc *a = ctx;

	/* Opened first and measured through the DESCRIPTOR, so the file counted is
	 * the file that existed — O_NOFOLLOW means a symlink dropped in place of a
	 * cookie jar is not followed to whatever it points at. */
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return; }
	a->bytes += (unsigned long long)st.st_blocks * 512ULL;
	a->files++;
	close(fd);
	/* toctou-ok: the size above came from the descriptor, not from this name;
	 * the unlink is the last thing that touches it. */
	if (a->remove && !g_dry) unlink(path);
}

/* ── /tmp, but only what is ours ────────────────────────────────────────── */
/*
 * ⛔ ONLY FILES THIS USER OWNS, AND ONLY ONES NOTHING IS USING. /tmp is shared:
 * it holds other users' files, the compositor's sockets, and the Wayland
 * display this session is talking to. Removing by age alone is how a cleaner
 * kills the desktop it is running on. Owned-by-us AND untouched for a day AND
 * not a socket is the narrow rule that leaves a live session alone.
 */
static int tmp_sweep(unsigned long long *bytes, unsigned long long *files, bool remove)
{
	/*
	 * ⛔ THE ONE PAIR OF ROOTS SYNCLEAN_HOME DOES NOT COVER, AND THE SUITE WAS
	 * DELETING THROUGH THEM.
	 *
	 * clean_test.sh opens by saying every path this program touches is composed
	 * from SYNCLEAN_HOME, "because a suite that could reach the real $HOME is
	 * one bad category string away from deleting the caches of whoever ran it".
	 * These two were hard-coded, so `clean --all` — which the suite runs — swept
	 * the REAL /tmp and /var/tmp of whoever typed `meson test`, removing
	 * anything of theirs older than a day. It ate this session's own scratch
	 * files while the translation work was going on, which is how it was found.
	 *
	 * ⚠ AND IT MADE tests/i18n_test.sh FLAKY, for the same reason: `--rec scan`
	 * reports a byte count and a file count for this row, and both move while
	 * the test is running. Two locale runs seconds apart disagreed about a
	 * number that had nothing to do with language.
	 *
	 * A colon-separated override, defaulting to the real pair. Same shape and
	 * same purpose as SYNCLEAN_HOME.
	 */
	const char *env = getenv("SYNCLEAN_TMPDIRS");
	const char *dirs[8] = { "/tmp", "/var/tmp", NULL };
	char buf[1024];
	if (env && *env) {
		snprintf(buf, sizeof buf, "%s", env);
		size_t n = 0;
		for (char *t = strtok(buf, ":"); t && n < 7; t = strtok(NULL, ":"))
			dirs[n++] = t;
		dirs[n] = NULL;
	}
	uid_t me = getuid();
	time_t now = time(NULL);
	for (const char **dp = dirs; *dp; dp++) {
		DIR *d = opendir(*dp);
		if (!d) continue;
		struct dirent *e;
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			char *p = xasprintf("%s/%s", *dp, e->d_name);
			struct stat st;
			if (lstat(p, &st) == 0 && st.st_uid == me &&
			    !S_ISSOCK(st.st_mode) && !S_ISFIFO(st.st_mode) &&
			    now - st.st_mtime > 86400) {
				walk_t w = { 0 };
				tree(p, &w, remove);
				*bytes += w.bytes;
				*files += w.files;
			}
			free(p);
		}
		closedir(d);
	}
	return 0;
}

/* ── measure / clean ────────────────────────────────────────────────────── */

static int do_category(const category_t *c, unsigned long long *bytes,
                       unsigned long long *files, bool remove)
{
	*bytes = 0; *files = 0;

	if (!strcmp(c->id, "tmp")) return tmp_sweep(bytes, files, remove);

	if (!strcmp(c->id, "cookies")) {
		struct cookie_acc a = { 0, 0, remove };
		cookies_each(cookie_one, &a);
		*bytes = a.bytes; *files = a.files;
		return 0;
	}

	/* ⚠ MEASURED WITHOUT ROOT, CLEANED ONLY WITH IT. /var/cache/pacman/pkg and
	 * /var/log/journal are world-readable, so the size is knowable to anybody —
	 * and a row that reported "0 B" because this program cannot DELETE it would
	 * be telling the user there is nothing there. Reporting the real number and
	 * saying it needs sudo is the honest pair.
	 *
	 * ⛔ Orphans are counted by asking pacman, never by walking: which packages
	 * nothing depends on is a question about the dependency graph, and a
	 * directory size is not an answer to it. */
	if (!strcmp(c->id, "orphans")) {
		FILE *p = popen("pacman -Qtdq 2>/dev/null", "r");
		if (!p) return 0;
		char line[256];
		while (fgets(line, sizeof line, p)) if (line[0] != '\n') (*files)++;
		pclose(p);
		if (*files > 0) {
			/* The bytes are pacman's to report and cost a query per package;
			 * the count is what makes the row worth reading. */
			*bytes = 0;
		}
		return 0;
	}

	int n = 0;
	char **v = roots_for(c, &n);
	int rc = 0;
	for (int i = 0; i < n; i++) {
		walk_t w = { 0 };
		/* usercache owns ~/.cache but thumbnails is its own row, so a scan that
		 * counted both would report the same bytes twice and a clean that
		 * removed both would be fine but the NUMBER would have been a lie. */
		if (!strcmp(c->id, "usercache")) {
			DIR *d = opendir(v[i]);
			if (d) {
				struct dirent *e;
				while ((e = readdir(d))) {
					if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
					if (cache_owned_elsewhere(e->d_name)) continue;
					char *sub = xasprintf("%s/%s", v[i], e->d_name);
					walk_t sw = { 0 };
					if (tree(sub, &sw, remove) != 0) rc = -1;
					w.bytes += sw.bytes; w.files += sw.files;
					free(sub);
				}
				closedir(d);
			}
		} else if (tree(v[i], &w, remove) != 0) {
			rc = -1;
		}
		*bytes += w.bytes; *files += w.files;
		free(v[i]);
	}
	free(v);
	return rc;
}

int category_measure(const category_t *c, unsigned long long *bytes,
                     unsigned long long *files)
{
	return do_category(c, bytes, files, false);
}

int category_clean(const category_t *c, unsigned long long *freed)
{
	unsigned long long files = 0;

	const char *live = category_blocked_by(c);
	if (live) {
		warn(_("%s is running — close it first, or its profile is what gets cleaned"),
		     live);
		*freed = 0;
		return 1;
	}
	if (c->needs_root) {
		warn(_("'%s' needs root: run `syn-clean clean %s` with sudo"), c->id, c->id);
		*freed = 0;
		return 1;
	}
	return do_category(c, freed, &files, true);
}
