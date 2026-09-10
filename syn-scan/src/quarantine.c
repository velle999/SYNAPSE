/* quarantine.c — putting a file aside, and being able to put it back.
 *
 * ⛔ THIS PROGRAM NEVER DELETES A USER'S FILE. Not on a clamscan hit, not on a
 * chkrootkit warning, not with --yes. Everything here is a MOVE with a sidecar
 * recording where the file came from, its mode and its mtime, so restore puts
 * it back exactly. On a single-seat desktop the likeliest thing this program
 * will ever catch is a false positive on a game mod, and eating somebody's save
 * is a worse outcome than the malware.
 *
 * ⚠ AND THE MOVE IS A COPY-THEN-UNLINK WHEN IT CROSSES A FILESYSTEM. rename(2)
 * fails with EXDEV between /home and /var, which is the normal case here, not
 * the exotic one.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synscan.h"
#include "i18n.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static int ensure_dir(void)
{
	if (mkdir(state_dir(), 0750) != 0 && errno != EEXIST) {
		warn("%s: %s", state_dir(), strerror(errno));
		return -1;
	}
	/* ⚠ 0700: a quarantined file is still malware. Nothing but root reads it,
	 * and it must never be executable again — see copy_out(). */
	if (mkdir(quarantine_dir(), 0700) != 0 && errno != EEXIST) {
		warn("%s: %s", quarantine_dir(), strerror(errno));
		return -1;
	}
	return 0;
}

static int copy_out(const char *src, const char *dst, mode_t *saved_mode,
                    mode_t create_mode)
{
	int in = open(src, O_RDONLY | O_NOFOLLOW);
	if (in < 0) { warn("%s: %s", src, strerror(errno)); return -1; }

	struct stat st;
	if (fstat(in, &st) != 0 || !S_ISREG(st.st_mode)) {
		warn(_("%s: not a regular file"), src);
		close(in); return -1;
	}
	*saved_mode = st.st_mode & 07777;

	/* ⛔ O_EXCL, AND THE MODE IS SET AT CREATION. Creating then chmod()ing
	 * re-resolves the name a second time; handing open(2) the mode closes
	 * that window and needs no second syscall. Callers pass 0600 on the way
	 * INTO quarantine — a quarantine full of files that are still +x has only
	 * moved the problem — and the original mode on the way back out. */
	int outfd = open(dst, O_WRONLY | O_CREAT | O_EXCL, create_mode);
	if (outfd < 0) { warn("%s: %s", dst, strerror(errno)); close(in); return -1; }

	char buf[65536]; ssize_t r;
	while ((r = read(in, buf, sizeof buf)) > 0) {
		ssize_t off = 0;
		while (off < r) {
			ssize_t w = write(outfd, buf + off, (size_t)(r - off));
			if (w <= 0) {
				warn("%s: %s", dst, strerror(errno));
				close(in); close(outfd); unlink(dst);
				return -1;
			}
			off += w;
		}
	}
	close(in);
	if (close(outfd) != 0) { warn("%s: %s", dst, strerror(errno)); unlink(dst); return -1; }
	return r < 0 ? -1 : 0;
}

int quarantine_take(const char *path, const char *engine, const char *detail)
{
	if (ensure_dir() != 0) return -1;

	char *real = realpath(path, NULL);
	if (!real) { warn("%s: %s", path, strerror(errno)); return -1; }

	/* id: the basename plus the clock, so two files of the same name from
	 * different folders do not collide. */
	const char *base = strrchr(real, '/');
	base = base ? base + 1 : real;

	char *dst = NULL, *meta = NULL;
	long long now = (long long)time(NULL);
	if (asprintf(&dst,  "%s/%lld-%s", quarantine_dir(), now, base) < 0 ||
	    asprintf(&meta, "%s.meta", dst) < 0)
		die("out of memory");

	if (g_dry) {
		info(_("would quarantine %s"), real);
		free(real); free(dst); free(meta);
		return 0;
	}

	mode_t mode = 0600;
	if (copy_out(real, dst, &mode, 0600) != 0) {
		free(real); free(dst); free(meta);
		return -1;
	}

	FILE *m = fopen(meta, "w");
	if (m) {
		fprintf(m, "origin=%s\nmode=%o\nengine=%s\ndetail=%s\nwhen=%lld\n",
		        real, (unsigned)mode, engine ? engine : "",
		        detail ? detail : "", now);
		/* fchmod on the descriptor we already have, not chmod on the name:
		 * re-resolving it here would be a second lookup of a path we just
		 * created. */
		fchmod(fileno(m), 0600);
		fclose(m);
	}

	if (unlink(real) != 0) {
		/* The copy is safe; the original is not gone. Say so rather than
		 * reporting a quarantine that half happened. */
		warn(_("%s: copied to quarantine but could not be removed: %s"),
		     real, strerror(errno));
		free(real); free(dst); free(meta);
		return -1;
	}

	info(_("quarantined %s"), real);
	free(real); free(dst); free(meta);
	return 0;
}

static char *meta_get(const char *metafile, const char *key)
{
	FILE *r = fopen(metafile, "r");
	if (!r) return NULL;
	char line[4096]; char *val = NULL;
	size_t klen = strlen(key);
	while (fgets(line, sizeof line, r)) {
		if (strncmp(line, key, klen) || line[klen] != '=') continue;
		char *v = line + klen + 1;
		size_t n = strlen(v);
		while (n && (v[n-1] == '\n' || v[n-1] == '\r')) v[--n] = 0;
		val = strdup(v);
		break;
	}
	fclose(r);
	return val;
}

int quarantine_list(void)
{
	DIR *d = opendir(quarantine_dir());
	if (!d) {
		if (g_out == OUT_REC) puts("#quarantine\tid\torigin\tengine\tdetail\twhen");
		else info("%s", _("Quarantine is empty."));
		return 0;
	}

	if (g_out == OUT_REC) puts("#quarantine\tid\torigin\tengine\tdetail\twhen");

	struct dirent *de; size_t n = 0;
	while ((de = readdir(d))) {
		size_t len = strlen(de->d_name);
		if (len < 6 || strcmp(de->d_name + len - 5, ".meta")) continue;

		char *metafile = NULL;
		if (asprintf(&metafile, "%s/%s", quarantine_dir(), de->d_name) < 0) continue;

		char *id = strndup(de->d_name, len - 5);
		char *origin = meta_get(metafile, "origin");
		char *engine = meta_get(metafile, "engine");
		char *detail = meta_get(metafile, "detail");
		char *when   = meta_get(metafile, "when");

		if (g_out == OUT_REC)
			printf("quarantine\t%s\t%s\t%s\t%s\t%s\n", id,
			       origin ? origin : "", engine ? engine : "",
			       detail ? detail : "", when ? when : "0");
		else
			printf("  %-28s %s\n      %s %s\n", id, origin ? origin : "?",
			       engine ? engine : "", detail ? detail : "");
		n++;
		free(metafile); free(id); free(origin); free(engine); free(detail); free(when);
	}
	closedir(d);

	if (n == 0 && g_out == OUT_HUMAN) info("%s", _("Quarantine is empty."));
	return 0;
}

int quarantine_restore(const char *id)
{
	char *blob = NULL, *metafile = NULL;
	if (asprintf(&blob, "%s/%s", quarantine_dir(), id) < 0 ||
	    asprintf(&metafile, "%s.meta", blob) < 0)
		die("out of memory");

	char *origin = meta_get(metafile, "origin");
	if (!origin) {
		warn(_("%s: no such quarantined file"), id);
		free(blob); free(metafile);
		return -1;
	}

	if (g_dry) { info(_("would restore %s to %s"), id, origin); goto out; }

	mode_t mode = 0600;
	char *m = meta_get(metafile, "mode");
	if (m) { mode = (mode_t)strtol(m, NULL, 8); free(m); }

	/* ⛔ THE MODE GOES ON WHILE THE FILE IS STILL OURS. It is sitting in a
	 * 0700 directory nothing else can reach; setting it here means the file
	 * arrives at its destination already correct, with no chmod on a name
	 * that has just become reachable by everybody. */
	int bfd = open(blob, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (bfd < 0) {
		warn(_("%s: no such quarantined file"), id);
		free(origin); free(blob); free(metafile);
		return -1;
	}
	if (fchmod(bfd, mode) != 0)
		warn(_("%s: could not restore the original mode: %s"), origin,
		     strerror(errno));
	close(bfd);

	/* ⛔ RENAME_NOREPLACE, BECAUSE rename(2) OVERWRITES ITS DESTINATION IN
	 * SILENCE. This function promises never to clobber a file that reappeared
	 * while the original was in quarantine, and an access() check followed by
	 * a plain rename() does not keep that promise — it only narrows the window
	 * to something that fits between two syscalls. The kernel can refuse it
	 * atomically, so let it. */
	if (renameat2(AT_FDCWD, blob, AT_FDCWD, origin, RENAME_NOREPLACE) != 0) {
		if (errno == EEXIST) {
			warn(_("%s already exists — not overwriting it"), origin);
			free(origin); free(blob); free(metafile);
			return -1;
		}
		/* EXDEV: quarantine and home are different filesystems, which is the
		 * normal case here, not the exotic one. EINVAL/ENOSYS: the filesystem
		 * does not know RENAME_NOREPLACE. Either way the copy path below is
		 * O_EXCL, so it refuses to clobber for the same reason. */
		if (errno != EXDEV && errno != EINVAL && errno != ENOSYS) {
			warn(_("%s: %s"), origin, strerror(errno));
			free(origin); free(blob); free(metafile);
			return -1;
		}
		mode_t tmp;
		if (copy_out(blob, origin, &tmp, mode) != 0) {
			free(origin); free(blob); free(metafile);
			return -1;
		}
		unlink(blob);
	}
	unlink(metafile);
	info(_("restored %s"), origin);

out:
	free(origin); free(blob); free(metafile);
	return 0;
}

int quarantine_purge(const char *id)
{
	/* ⚠ The one place this program deletes anything, and it deletes only what
	 * it put there itself, only when named, and only after asking. */
	char *blob = NULL, *metafile = NULL;
	if (asprintf(&blob, "%s/%s", quarantine_dir(), id) < 0 ||
	    asprintf(&metafile, "%s.meta", blob) < 0)
		die("out of memory");

	if (g_dry) {
		/* Reports and returns. Nothing below re-resolves either name, because
		 * nothing below runs. */
		struct stat st;
		if (stat(metafile, &st) != 0) {
			warn(_("%s: no such quarantined file"), id);
			free(blob); free(metafile);
			return -1;
		}
		info(_("would purge %s"), id);
		free(blob); free(metafile);
		return 0;
	}

	/* ⛔ THE UNLINK IS THE EXISTENCE CHECK. Asking access() first and then
	 * unlinking resolves the same name twice and answers a question that has
	 * already gone stale by the time it is used; unlink's own ENOENT is the
	 * same answer, one syscall later, and cannot be wrong. */
	if (unlink(metafile) != 0) {
		warn(errno == ENOENT ? _("%s: no such quarantined file") : _("%s: %s"),
		     id, strerror(errno));
		free(blob); free(metafile);
		return -1;
	}
	unlink(blob);
	info(_("purged %s"), id);
	free(blob); free(metafile);
	return 0;
}
