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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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
	  false, NULL, false, false },
	{ "usercache", N_("Application cache"), N_("~/.cache, minus the rows below"),
	  false, NULL, false, false },
	{ "trash", N_("Trash"), N_("files you already deleted"),
	  false, NULL, false, false },
	{ "crash", N_("Crash reports"), N_("core dumps and crash logs"),
	  false, NULL, false, false },
	{ "browsercache", N_("Browser cache"),
	  N_("pages and images, re-downloaded as needed"),
	  false, "firefox chromium vivaldi-bin chrome brave", false, false },
	/* ⚠ SIGNS YOU OUT EVERYWHERE. Not grouped with the caches for that reason:
	 * a cache is invisible when it goes, and this is the one category whose
	 * effect the user will notice on every site they use. */
	{ "cookies", N_("Cookies"), N_("SIGNS YOU OUT of every site"),
	  false, "firefox chromium vivaldi-bin chrome brave", true, false },
	{ "tmp", N_("Temporary files"), N_("your own leftovers in /tmp and /var/tmp"),
	  false, NULL, false, false },
	/* ⛔ THE ONE THAT UNINSTALLS. Named by hand or not at all — see `uninstalls`
	 * in synclean.h. Everything else here grows back. */
	{ "orphans", N_("Orphaned packages"),
	  N_("installed as dependencies, needed by nothing"),
	  true, NULL, false, true },
	{ "pkgcache", N_("Package cache"), N_("downloaded packages already installed"),
	  true, NULL, false, false },
	{ "journal", N_("System logs"), N_("the journal, trimmed to the last week"),
	  true, NULL, false, false },
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
	/* ⛔ WHAT REFUSED TO GO. Counted so the caller can say so: a clean that
	 * hits EACCES on every file still walked every one of them, and a summary
	 * built from what it SAW rather than what it removed is the program
	 * telling somebody it freed 13 GB that is still on the disk. */
	unsigned long long failed;
	dev_t dev;
	bool  remove;
	/* ⚠ THE DIRECTORY ITSELF STAYS. /var/cache/pacman/pkg is pacman's to have,
	 * and ~/.local/share/Trash/files is where the desktop puts the next thing
	 * somebody deletes; emptying either is the job, removing it is a surprise
	 * for whatever goes looking next. */
	bool  keep_root;
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
			unsigned long long cost = (unsigned long long)st.st_blocks * 512ULL;

			/*
			 * ⛔ COUNTED ONLY ONCE IT IS ACTUALLY GONE. The count used to be
			 * added before the unlink was attempted, so a tree this user
			 * cannot write — root-owned directories sitting in their own
			 * trash, which is what a rootfs build leaves behind — was reported
			 * as "Freed 13.1 GB" with every byte of it still on the disk. The
			 * exit status said 1 and nothing else did.
			 *
			 * ⚠ MEASURING STILL COUNTS EVERYTHING. `scan` and --dry-run answer
			 * "what is there", which is a different question from "what went",
			 * and a scan that hid the files it doubts it can remove would be
			 * the same lie pointing the other way.
			 */
			if (w->remove && !g_dry) {
				if (unlinkat(dirfd(d), e->d_name, 0) != 0) {
					w->failed++;
					rc = -1;
					continue;
				}
			}
			w->bytes += cost;
			w->files++;
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
		int rc1 = 0;
		if (fstatat(pfd, base, &st, AT_SYMLINK_NOFOLLOW) == 0) {
			unsigned long long cost = (unsigned long long)st.st_blocks * 512ULL;
			bool gone = true;
			if (remove && !g_dry && unlinkat(pfd, base, 0) != 0) {
				w->failed++;
				rc1 = -1;
				gone = false;
			}
			if (gone) { w->bytes += cost; w->files++; }
		}
		close(pfd);
		free(dup);
		return rc1;                                /* absent is not a failure */
	}

	struct stat st;
	if (fstat(fd, &st) != 0) { close(fd); return 0; }
	w->dev = st.st_dev;

	int rc = walk_dir(fd, w, 0);
	/* toctou-ok: the tree under this root is gone by now; rmdir on the root
	 * itself is the last act and nothing reopens the name after it. */
	if (remove && !g_dry && !w->keep_root) rmdir(path);
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
		/* ⛔ SEAMED LIKE SYNCLEAN_HOME, AND NOW IT HAS TO BE. This root is
		 * emptied once the process has root, so a suite that reached the real
		 * one would take the package cache of the machine running it — the
		 * same way the `tmp` category swept the real /tmp before
		 * SYNCLEAN_TMPDIRS existed. */
		const char *pc = getenv("SYNCLEAN_PKGCACHE");
		v[i++] = xstrdup(pc && *pc ? pc : "/var/cache/pacman/pkg");
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

struct cookie_acc { unsigned long long bytes, files, failed; bool remove; };

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
	unsigned long long cost = (unsigned long long)st.st_blocks * 512ULL;
	close(fd);

	/* toctou-ok: the size above came from the descriptor, not from this name;
	 * the unlink is the last thing that touches it.
	 * ⛔ AND COUNTED ONLY IF IT WENT — the same rule as walk_dir(). A jar that
	 * refused is not space that was freed. */
	if (a->remove && !g_dry && unlink(path) != 0) { a->failed++; return; }
	a->bytes += cost;
	a->files++;
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
	/* ⚠ THE USER'S, NOT root's. Under sudo getuid() is 0, and this swept
	 * root's leftovers while leaving the ones it was typed for. */
	uid_t me = syn_target_uid();
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

/* ── The two that are not a directory to empty ──────────────────────────── */

/*
 * ⛔ THE JOURNAL IS TRIMMED, NEVER WALKED AWAY.
 *
 * This row says "the journal, trimmed to the last week", and the generic tree
 * walk would have made that sentence false twice over: it removes the file
 * journald is CURRENTLY WRITING TO, and then rmdir()s /var/log/journal itself —
 * and a machine with no /var/log/journal has no persistent logging at all. It
 * falls back to /run, silently, until somebody notices the boots are gone.
 *
 * So journald does it: `journalctl --vacuum-time` is the supported way and it
 * knows which files it may take. This code's job is the NUMBER either side of
 * it.
 *
 * ⚠ AND THE MEASUREMENT HAS TO ASK THE SAME QUESTION AS THE CLEAN. Walking the
 * whole directory reported 726 MB on a machine where a week's vacuum would
 * free a fraction of that, because most of it is the active journal — a row
 * offering space that no command it could run would ever release.
 */
#define JOURNAL_KEEP_DAYS 7

/*
 * ⛔ ARCHIVED FILES ONLY, WHICH IS WHAT THE '@' MEANS. journald names the file
 * it is writing `system.journal` and a rotated one `system@<seq>-<time>.journal`.
 * A vacuum never takes the former, so counting it is counting space that is not
 * on offer.
 */
static int journal_measure(unsigned long long *bytes, unsigned long long *files)
{
	*bytes = 0; *files = 0;

	int fd = open("/var/log/journal",
	              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return 0;
	DIR *d = fdopendir(fd);
	if (!d) { close(fd); return 0; }

	time_t cut = time(NULL) - (time_t)JOURNAL_KEEP_DAYS * 86400;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		/* One directory per machine-id, and a machine may have several. */
		int md = openat(dirfd(d), e->d_name,
		                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (md < 0) continue;
		DIR *m = fdopendir(md);
		if (!m) { close(md); continue; }

		struct dirent *f;
		while ((f = readdir(m))) {
			if (!strchr(f->d_name, '@')) continue;
			const char *dot = strrchr(f->d_name, '.');
			if (!dot || strcmp(dot, ".journal") != 0) continue;

			struct stat st;
			if (fstatat(dirfd(m), f->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
				continue;
			if (!S_ISREG(st.st_mode)) continue;
			if (st.st_mtime >= cut) continue;

			*bytes += (unsigned long long)st.st_blocks * 512ULL;
			(*files)++;
		}
		closedir(m);
	}
	closedir(d);
	return 0;
}

static int journal_clean(unsigned long long *bytes, unsigned long long *files)
{
	unsigned long long b0 = 0, f0 = 0;
	journal_measure(&b0, &f0);

	if (g_dry) { *bytes = b0; *files = f0; return 0; }

	/* ⚠ THE SAME NUMBER OF DAYS THE MEASUREMENT USED. Two spellings of one
	 * fact is a row that offers what the command does not take. */
	char cmd[128];
	snprintf(cmd, sizeof cmd,
	         "LC_ALL=C journalctl --vacuum-time=%dd 2>&1", JOURNAL_KEEP_DAYS);

	*bytes = 0; *files = 0;
	FILE *p = popen(cmd, "r");
	if (!p) {
		warn("%s", _("journalctl could not be run — the journal was left alone"));
		return 1;
	}
	char line[512];
	while (fgets(line, sizeof line, p)) { /* journald narrates; the sizes below
	                                       * are what this program reports. */ }
	int st = pclose(p);

	/* ⛔ THE DIFFERENCE, NOT THE ESTIMATE. What journald actually took is the
	 * only honest answer, and it is one more measurement away. */
	unsigned long long b1 = 0, f1 = 0;
	journal_measure(&b1, &f1);
	*bytes = b0 > b1 ? b0 - b1 : 0;
	*files = f0 > f1 ? f0 - f1 : 0;

	if (st != 0) {
		warn("%s", _("journalctl could not be run — the journal was left alone"));
		return 1;
	}
	return 0;
}

/*
 * ⛔ AND THE ORPHANS ARE UNINSTALLED, WHICH IS pacman's WORK.
 *
 * The row counted them and the clean did nothing at all: roots_for() has no
 * entry for this id, so `clean orphans` walked an empty list and printed
 * "Freed 0 B" over eight packages that were still installed.
 *
 * ⚠ BYTES STAY 0 HERE, deliberately, and the count is what the row is for. The
 * size of an installed package is a query per package to pacman; the number of
 * things nothing needs is the fact somebody is acting on.
 */
static bool pkgname_ok(const char *n)
{
	if (!*n) return false;
	for (const char *c = n; *c; c++)
		if (!isalnum((unsigned char)*c) && !strchr("@._+-", *c)) return false;
	return true;
}

static int orphans_clean(unsigned long long *bytes, unsigned long long *files)
{
	*bytes = 0; *files = 0;

	FILE *p = popen("LC_ALL=C pacman -Qtdq 2>/dev/null", "r");
	if (!p) {
		warn("%s", _("pacman could not be run — no packages were removed"));
		return 1;
	}

	/* ⛔ EVERY NAME IS CHECKED BEFORE IT REACHES A SHELL. These come from
	 * pacman and so are not an attacker's to choose today — but they are
	 * strings from another program on their way into a command line, and the
	 * cost of being sure is one loop. A name that fails means this program has
	 * misread the output, which is a reason to stop rather than to guess. */
	char list[8192] = "";
	size_t used = 0;
	unsigned long long n = 0;
	bool bad = false;
	char line[256];
	while (fgets(line, sizeof line, p)) {
		line[strcspn(line, "\n")] = '\0';
		if (!*line) continue;
		if (!pkgname_ok(line)) { bad = true; break; }
		int w = snprintf(list + used, sizeof list - used, " %s", line);
		if (w < 0 || (size_t)w >= sizeof list - used) { bad = true; break; }
		used += (size_t)w;
		n++;
	}
	pclose(p);

	/* ⚠ AND A NAME THAT FAILED THAT CHECK IS pacman NOT RUN, which is exactly
	 * what the message below says. There is no second sentence for it because
	 * there is no second outcome: nothing was removed either way. */
	if (bad) {
		warn("%s", _("pacman could not be run — no packages were removed"));
		return 1;
	}
	if (n == 0) return 0;

	*files = n;
	if (g_dry) {
		/* ⛔ AND THE DRY RUN SAYS WHAT IT WOULD DO. This row's bytes are 0 by
		 * design, so "Would free 0 B" is the whole of what the caller can
		 * print — which, in front of somebody checking what `clean orphans`
		 * is about to uninstall, reads as "nothing". */
		if (g_out != OUT_REC)
			printf(P_("Would remove %llu orphaned package.\n",
			          "Would remove %llu orphaned packages.\n", n), n);
		return 0;
	}

	/* ⚠ --noconfirm BECAUSE THIS PROGRAM ALREADY ASKED. cmd_clean puts the
	 * question in front of the user before anything gets here, and a second
	 * prompt from pacman would be one nothing is reading — the stream is a
	 * pipe. */
	char cmd[8600];
	snprintf(cmd, sizeof cmd, "LC_ALL=C pacman -Rns --noconfirm%s 2>&1", list);
	FILE *r = popen(cmd, "r");
	if (!r) {
		warn("%s", _("pacman could not be run — no packages were removed"));
		*files = 0;
		return 1;
	}
	while (fgets(line, sizeof line, r)) { /* pacman narrates its own removal */ }
	if (pclose(r) != 0) {
		warn("%s", _("pacman could not remove them — the package database may be in use"));
		*files = 0;
		return 1;
	}

	/* ⚠ NOT A `Freed %s` LINE. This category frees packages, not bytes, and
	 * the caller's total is in bytes — so the thing that happened is said
	 * here. ⛔ Never on the record stream, which carries values, not prose. */
	if (g_out != OUT_REC)
		printf(P_("Removed %llu orphaned package.\n",
		          "Removed %llu orphaned packages.\n", n), n);
	return 0;
}

/* ── measure / clean ────────────────────────────────────────────────────── */

static int do_category(const category_t *c, unsigned long long *bytes,
                       unsigned long long *files, bool remove)
{
	*bytes = 0; *files = 0;

	if (!strcmp(c->id, "tmp")) return tmp_sweep(bytes, files, remove);

	if (!strcmp(c->id, "cookies")) {
		struct cookie_acc a = { 0, 0, 0, remove };
		cookies_each(cookie_one, &a);
		*bytes = a.bytes; *files = a.files;
		if (a.failed > 0 && remove && !g_dry)
			warn("%s", _("Some files could not be removed — they belong to "
			             "another user. Try again with sudo."));
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
		if (remove) return orphans_clean(bytes, files);

		FILE *p = popen("LC_ALL=C pacman -Qtdq 2>/dev/null", "r");
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

	/* ⛔ AND THE JOURNAL IS NOT A DIRECTORY TO EMPTY EITHER — see the comment
	 * over journal_measure(). Both halves ask the same question so the row and
	 * the command agree about what is on offer. */
	if (!strcmp(c->id, "journal"))
		return remove ? journal_clean(bytes, files)
		              : journal_measure(bytes, files);

	int n = 0;
	char **v = roots_for(c, &n);
	int rc = 0;
	unsigned long long refused = 0;
	for (int i = 0; i < n; i++) {
		walk_t w = { 0 };

		/* ⚠ EMPTIED, NOT REMOVED. pacman owns its cache directory and the
		 * desktop owns ~/.local/share/Trash/files; both are where the next
		 * thing lands, and a cleaner that takes the directory has broken
		 * whatever goes looking for it. */
		w.keep_root = !strcmp(c->id, "pkgcache") || !strcmp(c->id, "trash");

		/*
		 * ⛔ THE RESTORE RECORDS GO ONLY IF THE FILES WENT. Trash/info holds
		 * one .trashinfo per item — the original path, which is the whole of
		 * what "restore" means — and it is the second root here. A clean that
		 * emptied it while Trash/files refused (root-owned trees land there
		 * from a rootfs build) left the space still used AND nothing
		 * restorable: the worst of both, and silent.
		 */
		if (!strcmp(c->id, "trash") && i == 1 && remove && !g_dry && rc != 0) {
			free(v[i]);
			continue;
		}
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
					/* ⚠ AND ITS REFUSALS, or a cache subdirectory this user
					 * cannot write is the one place the count goes quiet. */
					w.bytes += sw.bytes; w.files += sw.files;
					w.failed += sw.failed;
					free(sub);
				}
				closedir(d);
			}
		} else if (tree(v[i], &w, remove) != 0) {
			rc = -1;
		}
		*bytes += w.bytes; *files += w.files;
		refused += w.failed;
		free(v[i]);
	}
	free(v);

	/*
	 * ⛔ AND IT SAYS SO. The bytes above are now what actually went, so a run
	 * that removed nothing reports nothing — but "Freed 0 B" on its own is a
	 * program that looks broken rather than one that was refused. The two
	 * things a person can do about it are in the sentence.
	 */
	if (refused > 0 && remove && !g_dry)
		warn("%s", _("Some files could not be removed — they belong to another "
		             "user. Try again with sudo."));
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
	/*
	 * ⛔ ROOT IS A QUESTION ABOUT THIS PROCESS, NOT A PROPERTY OF THE ROW.
	 *
	 * This read the flag alone, and nothing in the program ever asked
	 * geteuid() — so `sudo syn-clean clean pkgcache` answered "needs root: run
	 * it with sudo" to somebody who had just done that. All three root
	 * categories were unreachable by any command line, while the scan went on
	 * offering their 27 GB in the total at the bottom. `clean --all` skipped
	 * them too, which is how it came to say it had freed 44 GB and freed none
	 * of it.
	 */
	if (c->needs_root && geteuid() != 0) {
		category_warn_needs_root(c);
		*freed = 0;
		return 1;
	}
	return do_category(c, freed, &files, true);
}

void category_warn_needs_root(const category_t *c)
{
	warn(_("'%s' needs root: run `syn-clean clean %s` with sudo"), c->id, c->id);
}
