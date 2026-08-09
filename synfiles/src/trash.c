/* trash.c — the XDG trash specification.
 *
 * This is what the Delete key reaches. `synfiles delete` exists and is
 * permanent, but nothing in the GUI calls it without asking first, because
 * the difference between the two is the difference between an inconvenience
 * and a phone call.
 *
 * The spec is small but every part of it is load-bearing:
 *
 *   - A trashed file is TWO files: the data at $trash/files/NAME and a
 *     $trash/info/NAME.trashinfo recording where it came from. Without the
 *     second one the first is unrestorable, which makes it litter rather than
 *     trash.
 *   - The name is reserved by creating the .trashinfo with O_EXCL. That is
 *     what makes two processes trashing "notes.txt" at the same time end up
 *     with notes.txt and notes.txt.2 rather than one overwriting the other.
 *   - Path= is PERCENT-ENCODED, which is the same encoding this program's
 *     record format already uses — so a filename with a newline in it round
 *     trips through the trash and back without a special case.
 *   - Trashing is always a rename(), never a copy. That is why a file on
 *     another filesystem goes to that filesystem's own .Trash-$uid instead of
 *     the home trash: a "delete" that silently copied 40GB across a USB bus
 *     and then removed the original is not what the user asked for, and it is
 *     not recoverable if it fails halfway.
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
#include <time.h>
#include <unistd.h>

/* ── locating a trash directory ─────────────────────────────────────────── */

static char *home_trash(void)
{
	const char *env = getenv("SYNFILES_TRASH");
	if (env && *env)
		return xstrdup(env);
	char *data = xdg_data_home();
	char *p = xasprintf("%s/Trash", data);
	free(data);
	return p;
}

/* The mount point `path` lives on: walk up until the device number changes.
 * "/" is the terminating case and is its own topdir. */
static char *topdir_of(const char *path)
{
	struct stat st;
	if (lstat(path, &st) != 0)
		return NULL;
	dev_t dev = st.st_dev;

	char *cur = xstrdup(path);
	for (;;) {
		char *slash = strrchr(cur, '/');
		if (!slash || slash == cur) {
			free(cur);
			return xstrdup("/");
		}
		*slash = '\0';

		struct stat ps;
		if (lstat(cur, &ps) != 0 || ps.st_dev != dev) {
			/* The parent is on a different device, so what we just stepped
			 * out of was the mount point. Put the component back. */
			*slash = '/';
			return cur;
		}
	}
}

static int mkdir_p(const char *path, mode_t mode)
{
	if (mkdir(path, mode) == 0 || errno == EEXIST)
		return 0;
	if (errno != ENOENT)
		return -1;

	char *copy = xstrdup(path);
	char *slash = strrchr(copy, '/');
	if (!slash || slash == copy) {
		free(copy);
		return -1;
	}
	*slash = '\0';
	int rc = mkdir_p(copy, mode);
	free(copy);
	if (rc != 0)
		return -1;

	return (mkdir(path, mode) == 0 || errno == EEXIST) ? 0 : -1;
}

/* Which trash `path` belongs in, and — for a volume trash — the topdir its
 * Path= entries are recorded relative to. */
static char *trash_for(const char *path, char **topdir_out)
{
	*topdir_out = NULL;

	/* An explicit override wins outright and skips the volume logic below.
	 * Without this the device comparison still asks about the REAL home
	 * trash, so a test pointing SYNFILES_TRASH at a scratch directory on
	 * another filesystem would be routed to a volume trash instead — and a
	 * test suite that cannot be kept away from the user's actual trash is
	 * not one worth having. Same rule as every other SYNFILES_* override:
	 * set means used, not "used if it happens to fit". */
	const char *env = getenv("SYNFILES_TRASH");
	if (env && *env)
		return xstrdup(env);

	char *home = home_trash();
	struct stat fs, hs;

	/* The home trash may not exist yet; compare against its parent, which is
	 * on the same device by construction. */
	char *home_parent = xdg_data_home();
	if (lstat(path, &fs) == 0 && lstat(home_parent, &hs) == 0
	    && fs.st_dev == hs.st_dev) {
		free(home_parent);
		return home;
	}
	free(home_parent);
	free(home);

	/* A different filesystem. The spec allows a shared $topdir/.Trash with
	 * the sticky bit, but it must be validated (not a symlink, sticky set)
	 * and most implementations skip it; $topdir/.Trash-$uid is per-user,
	 * needs no validation, and is what gvfs and KIO actually create. */
	char *top = topdir_of(path);
	if (!top)
		return NULL;

	char *vol = xasprintf("%s/.Trash-%lu", top, (unsigned long)getuid());
	*topdir_out = top;
	return vol;
}

/* ── put ────────────────────────────────────────────────────────────────── */

static char *now_iso(void)
{
	time_t t = time(NULL);
	struct tm tm;
	/* Local time, no zone suffix — what the spec says and what every other
	 * implementation writes. */
	if (!localtime_r(&t, &tm))
		return xstrdup("1970-01-01T00:00:00");
	char buf[32];
	strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
	return xstrdup(buf);
}

static int trash_put_one(const char *path)
{
	char *real = sf_resolve(path);

	if (!strcmp(real, "/")) {
		warn("refusing to trash the root directory");
		free(real);
		return 1;
	}

	char *topdir = NULL;
	char *trash = trash_for(real, &topdir);
	if (!trash) {
		warn("cannot work out where to trash %s", real);
		free(real);
		return 1;
	}

	char *filesdir = xasprintf("%s/files", trash);
	char *infodir = xasprintf("%s/info", trash);
	if (mkdir_p(filesdir, 0700) != 0 || mkdir_p(infodir, 0700) != 0) {
		warn("cannot create the trash directory at %s: %s", trash, strerror(errno));
		free(filesdir); free(infodir); free(trash); free(topdir); free(real);
		return 1;
	}

	/* Per the spec, Path= is absolute for the home trash and RELATIVE to the
	 * volume's topdir for a volume trash — so a removable disk can be
	 * unplugged, remounted somewhere else, and its trash still restores. */
	const char *recorded = real;
	if (topdir) {
		size_t n = strlen(topdir);
		if (!strcmp(topdir, "/"))
			recorded = real + 1;
		else if (!strncmp(real, topdir, n) && real[n] == '/')
			recorded = real + n + 1;
	}

	const char *base = sf_basename(real);
	char *chosen = NULL;
	int infofd = -1;

	/* Reserve the name by creating the .trashinfo exclusively. Checking for
	 * a free name and then creating it would be a race that loses a file. */
	for (int i = 1; i < 10000 && infofd < 0; i++) {
		free(chosen);
		chosen = (i == 1) ? xstrdup(base) : xasprintf("%s.%d", base, i);
		char *ipath = xasprintf("%s/%s.trashinfo", infodir, chosen);
		infofd = open(ipath, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		free(ipath);
		if (infofd < 0 && errno != EEXIST) {
			warn("cannot write trash info: %s", strerror(errno));
			break;
		}
	}

	if (infofd < 0) {
		warn("cannot find a free name in the trash for %s", base);
		free(chosen); free(filesdir); free(infodir); free(trash);
		free(topdir); free(real);
		return 1;
	}

	char *enc = pct_encode(recorded, true);
	char *when = now_iso();
	char *body = xasprintf("[Trash Info]\nPath=%s\nDeletionDate=%s\n", enc, when);
	size_t blen = strlen(body);
	bool wrote = write(infofd, body, blen) == (ssize_t)blen;
	close(infofd);
	free(body); free(when); free(enc);

	int rc = 0;
	char *dest = xasprintf("%s/%s", filesdir, chosen);

	if (!wrote || rename(real, dest) != 0) {
		/* Roll the reservation back. A .trashinfo with no file beside it
		 * shows up in every trash viewer as a phantom entry that cannot be
		 * restored or removed. */
		if (!wrote)
			warn("cannot write trash info for %s", real);
		else
			warn("cannot move %s to the trash: %s", real, strerror(errno));
		char *ipath = xasprintf("%s/%s.trashinfo", infodir, chosen);
		unlink(ipath);
		free(ipath);
		rc = 1;
	} else if (g_out == OUT_REC) {
		char *e = pct_encode(real, true);
		rec_row(3, e, "done", chosen);
		free(e);
	} else {
		printf("trashed %s\n", real);
	}

	free(dest); free(chosen); free(filesdir); free(infodir);
	free(trash); free(topdir); free(real);
	return rc;
}

/* ── list ───────────────────────────────────────────────────────────────── */

/* One key out of a .trashinfo. Not a full INI parser: the file has one
 * section and two keys, and every implementation writes them in that order. */
static char *info_value(const char *text, const char *key)
{
	/* Anchored at a line start and followed by "=", so asking for "Path"
	 * cannot match the "Path" inside "DeletionDate" on some future key, and
	 * cannot match a value that happens to contain the word. */
	size_t klen = strlen(key);
	for (const char *p = text; (p = strstr(p, key)); p += klen) {
		if (p != text && p[-1] != '\n')
			continue;
		if (p[klen] != '=')
			continue;
		const char *v = p + klen + 1;
		const char *nl = strchr(v, '\n');
		return nl ? xstrndup(v, (size_t)(nl - v)) : xstrdup(v);
	}
	return NULL;
}

static int list_one_trash(const char *trash, const char *topdir, int *n)
{
	char *infodir = xasprintf("%s/info", trash);
	DIR *d = opendir(infodir);
	if (!d) {
		free(infodir);
		return 0;
	}

	struct dirent *e;
	while ((e = readdir(d))) {
		const char *suffix = strstr(e->d_name, ".trashinfo");
		if (!suffix || suffix[10] != '\0')
			continue;

		char *ipath = xasprintf("%s/%s", infodir, e->d_name);
		char *text = slurp(ipath);
		free(ipath);
		if (!text)
			continue;

		char *epath = info_value(text, "Path");
		char *when = info_value(text, "DeletionDate");
		char *name = xstrndup(e->d_name, (size_t)(suffix - e->d_name));

		/* A volume trash records Path= relative to its topdir; make it
		 * absolute so a caller never has to know which trash a row is in. */
		char *full;
		if (topdir && epath && epath[0] != '/') {
			char *pre = pct_encode(topdir, true);
			full = !strcmp(topdir, "/") ? xasprintf("/%s", epath)
			                            : xasprintf("%s/%s", pre, epath);
			free(pre);
		} else {
			full = xstrdup(epath ? epath : "");
		}

		/* `name` came from readdir and is therefore RAW bytes — it has never
		 * been encoded, so decoding it here would corrupt any trashed file
		 * whose name genuinely contains a '%'. It is encoded once, below,
		 * purely for the record stream.
		 *
		 * The data file may be gone — a trash emptied by another tool leaves
		 * the info behind. Say so rather than offering a restore that fails. */
		char *fpath = xasprintf("%s/files/%s", trash, name);
		bool present = faccessat(AT_FDCWD, fpath, F_OK, AT_SYMLINK_NOFOLLOW) == 0;
		free(fpath);

		char *ename = pct_encode(name, false);
		char *etrash = pct_encode(trash, true);

		if (g_out == OUT_REC) {
			rec_row(5, ename, full, when ? when : "", etrash,
			        present ? "1" : "0");
		} else {
			char *shown = pct_decode(full);
			printf("%s%-19s%s %s%s%s\n", C_DIM(), when ? when : "", C_RESET(),
			       present ? "" : C_WARN(), shown, C_RESET());
			free(shown);
		}
		(*n)++;

		free(ename); free(etrash); free(full);
		free(name); free(when); free(epath); free(text);
	}

	closedir(d);
	free(infodir);
	return 0;
}

/* Every trash this user has: the home one, plus a .Trash-$uid on any mounted
 * filesystem. Skipping the volume ones would make files trashed from an
 * external drive invisible and unrecoverable through this program. */
static void each_trash(void (*fn)(const char *trash, const char *topdir, void *),
                       void *ctx)
{
	char *home = home_trash();
	fn(home, NULL, ctx);
	free(home);

	char *mounts = slurp("/proc/self/mounts");
	if (!mounts)
		return;

	size_t nlines = 0;
	char **lines = split(mounts, '\n', &nlines);
	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i])
			continue;
		char *sp = strchr(lines[i], ' ');
		if (!sp)
			continue;
		char *mp = sp + 1;
		char *sp2 = strchr(mp, ' ');
		if (!sp2)
			continue;
		*sp2 = '\0';
		if (!strcmp(mp, "/"))
			continue;

		char *vol = xasprintf("%s/.Trash-%lu", mp, (unsigned long)getuid());
		struct stat st;
		if (lstat(vol, &st) == 0 && S_ISDIR(st.st_mode))
			fn(vol, mp, ctx);
		free(vol);
	}
	free(lines);
	free(mounts);
}

static void list_cb(const char *trash, const char *topdir, void *ctx)
{
	list_one_trash(trash, topdir, (int *)ctx);
}

static int trash_list(void)
{
	if (g_out == OUT_REC)
		rec_row(5, "name", "path", "deleted", "trash", "present");

	int n = 0;
	each_trash(list_cb, &n);

	if (g_out == OUT_HUMAN && n == 0)
		printf("%sthe trash is empty%s\n", C_DIM(), C_RESET());
	return n ? 0 : 100;
}

/* ── restore ────────────────────────────────────────────────────────────── */

typedef struct { const char *want; int rc; bool found; } restore_ctx;

static void restore_cb(const char *trash, const char *topdir, void *vctx)
{
	restore_ctx *ctx = vctx;
	if (ctx->found)
		return;

	char *ipath = xasprintf("%s/info/%s.trashinfo", trash, ctx->want);
	char *text = slurp(ipath);
	if (!text) {
		free(ipath);
		return;
	}
	ctx->found = true;

	char *epath = info_value(text, "Path");
	char *target = NULL;
	if (epath) {
		char *dec = pct_decode(epath);
		if (dec[0] == '/' || !topdir)
			target = xstrdup(dec);
		else
			target = !strcmp(topdir, "/") ? xasprintf("/%s", dec)
			                              : xasprintf("%s/%s", topdir, dec);
		free(dec);
	}

	char *from = xasprintf("%s/files/%s", trash, ctx->want);

	if (!target || !*target) {
		warn("%s has no Path in its trash info", ctx->want);
		ctx->rc = 1;
	} else if (faccessat(AT_FDCWD, target, F_OK, AT_SYMLINK_NOFOLLOW) == 0) {
		/* Never overwrite on restore. Something already occupies the place
		 * this came from, and clobbering it would destroy the newer file to
		 * recover the older one. */
		warn("%s already exists — not restoring over it", target);
		ctx->rc = 1;
	} else if (rename(from, target) != 0) {
		warn("cannot restore %s: %s", target, strerror(errno));
		ctx->rc = 1;
	} else {
		unlink(ipath);
		if (g_out == OUT_REC) {
			char *e = pct_encode(target, true);
			rec_row(3, e, "done", "restored");
			free(e);
		} else {
			printf("restored %s\n", target);
		}
	}

	free(from); free(target); free(epath); free(text); free(ipath);
}

static int trash_restore(const char *name)
{
	/* `trash list` emits the name PERCENT-ENCODED, like every other name this
	 * program prints, so that is the form callers hand back — decode it once
	 * here to get the bytes that are actually on disk.
	 *
	 * Without this, restoring anything whose name contains a tab, a newline
	 * or a space silently fails to find its own trashinfo: the encoded name
	 * from the listing never matches the raw name in the directory. A plain
	 * name survives the round trip unchanged, so the CLI still takes what a
	 * person would type. */
	char *raw = pct_decode(name);

	restore_ctx ctx = { raw, 0, false };
	if (g_out == OUT_REC)
		rec_row(3, "path", "status", "detail");

	each_trash(restore_cb, &ctx);

	if (!ctx.found) {
		warn("nothing called '%s' is in the trash", name);
		free(raw);
		return 1;
	}
	free(raw);
	return ctx.rc;
}

/* ── empty ──────────────────────────────────────────────────────────────── */

static void empty_cb(const char *trash, const char *topdir, void *vctx)
{
	(void)topdir;
	int *n = vctx;

	const char *subs[] = { "files", "info" };
	for (size_t s = 0; s < 2; s++) {
		char *dir = xasprintf("%s/%s", trash, subs[s]);
		DIR *d = opendir(dir);
		if (!d) {
			free(dir);
			continue;
		}
		int dfd = dirfd(d);
		struct dirent *e;
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				continue;
			if (sf_rm_rf(dfd, e->d_name) == 0 && s == 0)
				(*n)++;
		}
		closedir(d);
		free(dir);
	}
}

static int trash_empty(bool confirmed)
{
	if (!confirmed)
		die("emptying the trash is PERMANENT and cannot be undone.\n"
		    "  to see what would go:  synfiles trash list\n"
		    "  to empty it anyway:    synfiles trash empty --yes");

	int n = 0;
	each_trash(empty_cb, &n);

	if (g_out == OUT_REC) {
		char *c = xasprintf("%d", n);
		rec_row(2, "removed", c);
		free(c);
	} else {
		printf("removed %d item%s\n", n, n == 1 ? "" : "s");
	}
	return 0;
}

/* ── dispatch ───────────────────────────────────────────────────────────── */

int cmd_trash(int argc, char **argv)
{
	const char *sub = argc > 0 ? argv[0] : "list";

	if (!strcmp(sub, "list"))
		return trash_list();

	if (!strcmp(sub, "restore")) {
		if (argc < 2)
			die("trash restore: need the trashed name (see: synfiles trash list)");
		return trash_restore(argv[1]);
	}

	if (!strcmp(sub, "empty")) {
		bool yes = false;
		for (int i = 1; i < argc; i++)
			if (!strcmp(argv[i], "--yes"))
				yes = true;
		return trash_empty(yes);
	}

	/* Anything else is a path to trash — `synfiles trash <file>` is the
	 * shape a Delete key wants, without a verb in the middle. */
	if (g_out == OUT_REC)
		rec_row(3, "path", "status", "detail");

	int rc = 0;
	for (int i = 0; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1])
			die("trash: unknown option '%s'", argv[i]);
		if (trash_put_one(argv[i]) != 0)
			rc = 1;
	}
	return rc;
}
