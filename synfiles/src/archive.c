/* archive.c — creating archives.
 *
 * Extraction is already handled: synui ships a KIO service menu for it, and
 * synfiles inherits that through actions.c. This is the other half, which no
 * service menu provides.
 *
 * Compression itself is NOT implemented here. tar, 7z and zip already exist,
 * are already correct about permissions and sparse files and symlinks, and
 * shipping a fourth implementation of deflate inside a file manager would be
 * absurd. The work here is choosing the right tool, giving it a sane cwd, and
 * refusing the cases that would produce a surprising archive.
 *
 * Two rules that shape the whole file:
 *
 *   - Every input must share ONE parent directory, and the tool runs with that
 *     directory as its cwd. Otherwise the paths stored inside the archive are
 *     absolute or full of "../", and unpacking it somewhere else strews files
 *     across the filesystem. This is the tarbomb problem, from the other end.
 *   - Nothing is overwritten. An archive named the same as an existing one is
 *     refused rather than replaced, because "compress" is not a command anybody
 *     expects to destroy something.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct {
	const char *id;     /* what the caller asks for */
	const char *ext;    /* what the file is called */
} format_t;

static const format_t formats[] = {
	{ "zip",      ".zip" },
	{ "tar.gz",   ".tar.gz" },
	{ "tar.xz",   ".tar.xz" },
	{ "tar.zst",  ".tar.zst" },
	{ "7z",       ".7z" },
};

static const format_t *find_format(const char *id)
{
	for (size_t i = 0; i < sizeof formats / sizeof *formats; i++)
		if (!strcmp(formats[i].id, id))
			return &formats[i];
	return NULL;
}

/* The directory every input lives in, or NULL if they do not agree. */
static char *common_parent(char **paths, int n)
{
	char *first = NULL;

	for (int i = 0; i < n; i++) {
		char *real = sf_resolve(paths[i]);
		char *copy = xstrdup(real);
		char *slash = strrchr(copy, '/');
		if (!slash) {
			free(copy);
			free(real);
			free(first);
			return NULL;
		}
		if (slash == copy)
			copy[1] = '\0';
		else
			*slash = '\0';

		if (!first) {
			first = copy;
		} else {
			if (strcmp(first, copy)) {
				free(copy);
				free(real);
				free(first);
				return NULL;
			}
			free(copy);
		}
		free(real);
	}

	return first;
}

/* Run `argv` with `dir` as the working directory, so the archive records
 * relative paths. chdir happens in the CHILD only — changing the parent's cwd
 * would quietly break every relative path the rest of the process holds. */
static int run_in(const char *dir, char *const argv[])
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		if (chdir(dir) != 0)
			_exit(126);
		/* The tools are chatty and their progress is not this program's
		 * output; failures still surface through the exit status. */
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			if (!g_verbose)
				dup2(devnull, STDERR_FILENO);
			if (devnull > STDERR_FILENO)
				close(devnull);
		}
		execvp(argv[0], argv);
		_exit(127);
	}

	int st = 0;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int cmd_compress(int argc, char **argv)
{
	const char *fmt_id = "tar.gz";
	const char *want_name = NULL;
	int n = 0;
	char **paths = xmalloc((size_t)(argc + 1) * sizeof *paths);

	for (int i = 0; i < argc; i++) {
		if (!strncmp(argv[i], "--format=", 9))
			fmt_id = argv[i] + 9;
		else if (!strncmp(argv[i], "--name=", 7))
			want_name = argv[i] + 7;
		else if (argv[i][0] == '-' && argv[i][1])
			die("compress: unknown option '%s'", argv[i]);
		else
			paths[n++] = argv[i];
	}

	if (n == 0) {
		free(paths);
		die("compress: need at least one path");
	}

	const format_t *fmt = find_format(fmt_id);
	if (!fmt) {
		free(paths);
		die("compress: unknown format '%s' — try zip, tar.gz, tar.xz, tar.zst, 7z",
		    fmt_id);
	}

	char *dir = common_parent(paths, n);
	if (!dir) {
		free(paths);
		die("compress: everything must be in the same folder\n"
		    "  otherwise the archive records absolute paths and unpacking it "
		    "scatters files");
	}

	/* A name is only accepted as a NAME. A path here would put the archive
	 * somewhere the user did not choose while the tool still ran in `dir`. */
	if (want_name && strchr(want_name, '/')) {
		free(dir);
		free(paths);
		die("compress: --name takes a name, not a path");
	}

	char *base;
	if (want_name) {
		/* Only add the extension if it is not already there, so
		 * --name=backup.tar.gz does not become backup.tar.gz.tar.gz. */
		size_t wl = strlen(want_name), el = strlen(fmt->ext);
		base = (wl >= el && !strcmp(want_name + wl - el, fmt->ext))
		       ? xstrdup(want_name)
		       : xasprintf("%s%s", want_name, fmt->ext);
	} else if (n == 1) {
		char *real = sf_resolve(paths[0]);
		base = xasprintf("%s%s", sf_basename(real), fmt->ext);
		free(real);
	} else {
		/* Named after the folder they are in, which is the only thing a
		 * mixed selection has in common. */
		base = xasprintf("%s%s", sf_basename(dir), fmt->ext);
	}

	char *out = xasprintf("%s/%s", dir, base);
	if (faccessat(AT_FDCWD, out, F_OK, AT_SYMLINK_NOFOLLOW) == 0) {
		warn("%s already exists", out);
		free(out); free(base); free(dir); free(paths);
		return 1;
	}

	/* Build the tool's argv: the command, its flags, the archive name, then
	 * one BASENAME per input — they are relative to the cwd the child takes. */
	size_t cap = (size_t)n + 8;
	char **child = xmalloc(cap * sizeof *child);
	size_t k = 0;

	if (!strcmp(fmt->id, "zip")) {
		/* zip is not installed everywhere; 7z writes the same format and is.
		 * Falling back beats telling somebody to install a second tool for a
		 * format the one they already have can produce. */
		if (have_cmd("zip")) {
			child[k++] = (char *)"zip";
			child[k++] = (char *)"-r";
			child[k++] = (char *)"-q";
			child[k++] = base;
		} else if (have_cmd("7z")) {
			child[k++] = (char *)"7z";
			child[k++] = (char *)"a";
			child[k++] = (char *)"-tzip";
			child[k++] = base;
		} else {
			free(child); free(out); free(base); free(dir); free(paths);
			die("compress: neither zip nor 7z is installed");
		}
	} else if (!strcmp(fmt->id, "7z")) {
		if (!have_cmd("7z")) {
			free(child); free(out); free(base); free(dir); free(paths);
			die("compress: 7z is not installed — install p7zip");
		}
		child[k++] = (char *)"7z";
		child[k++] = (char *)"a";
		child[k++] = base;
	} else {
		if (!have_cmd("tar")) {
			free(child); free(out); free(base); free(dir); free(paths);
			die("compress: tar is not installed");
		}
		child[k++] = (char *)"tar";
		if (!strcmp(fmt->id, "tar.gz"))       child[k++] = (char *)"-czf";
		else if (!strcmp(fmt->id, "tar.xz"))  child[k++] = (char *)"-cJf";
		else {
			child[k++] = (char *)"--zstd";
			child[k++] = (char *)"-cf";
		}
		child[k++] = base;
	}

	for (int i = 0; i < n; i++) {
		char *real = sf_resolve(paths[i]);
		child[k++] = xstrdup(sf_basename(real));
		free(real);
	}
	child[k] = NULL;

	if (g_out == OUT_REC)
		rec_row(3, "path", "status", "detail");

	int rc = run_in(dir, child);

	if (rc != 0) {
		/* A half-written archive is worse than none: it looks like a backup. */
		unlink(out);
		if (g_out == OUT_REC) {
			char *e = pct_encode(out, true);
			rec_row(3, e, "failed", "the archive tool reported an error");
			free(e);
		} else {
			warn("could not create %s", base);
		}
	} else {
		sf_journal("compress", out, "");
		if (g_out == OUT_REC) {
			char *e = pct_encode(out, true);
			rec_row(3, e, "done", fmt->id);
			free(e);
		} else {
			printf("created %s\n", base);
		}
	}

	for (size_t i = k - (size_t)n; i < k; i++)
		free(child[i]);
	free(child);
	free(out);
	free(base);
	free(dir);
	free(paths);
	return rc == 0 ? 0 : 1;
}
