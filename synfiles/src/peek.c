/* peek.c — what is inside a folder, for drawing a preview on its icon.
 *
 * Dolphin puts a few of a folder's own images on the front of its icon, and
 * that is genuinely useful: a Pictures directory of twenty subfolders is
 * twenty identical shapes until something distinguishes them.
 *
 * The shape of this command is the whole design decision. The obvious way — a
 * "what is in here" call per folder, made by the GUI as each row scrolls into
 * view — is one process per folder, and a directory of 200 subfolders would
 * fork 200 times to draw one screen. So the front-end asks ONCE per listing:
 * peek <dir> reports candidates for every subdirectory of <dir> in a single
 * pass, and the GUI groups the rows it gets back.
 *
 * Everything is bounded, for the same reason `find` is. This runs on every
 * navigation, including into a directory holding a thousand others on a
 * network mount:
 *
 *   - at most PEEK_DIRS subdirectories are opened at all;
 *   - at most PEEK_SCAN entries are examined inside each one;
 *   - at most --limit candidates are reported per folder (default 4);
 *   - ONE level. It never descends: a folder whose media all live two levels
 *     down draws a plain folder, which is the right trade against walking a
 *     tree of unknown size to decorate an icon.
 *
 * Nothing is opened or read. A candidate is decided from its NAME through
 * mime_for(), exactly like the icon in a listing — see mime.c for why content
 * sniffing is not done in a path that runs per directory entry.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"
#include "i18n.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PEEK_DIRS 400    /* subdirectories opened per call */
#define PEEK_SCAN 600    /* entries examined inside each one */

/* Images and video only. A folder of PDFs has thumbnails in the shared cache
 * too, but a page of white paper shrunk to 20 pixels is a grey square, and
 * four grey squares on a folder icon read as damage rather than as contents. */
static bool previewable(const char *name)
{
	const char *mime = mime_for(name, false);
	if (!mime)
		return false;
	return !strncmp(mime, "image/", 6) || !strncmp(mime, "video/", 6);
}

/* One subdirectory: report up to `limit` previewable files, in readdir order.
 *
 * readdir order is arbitrary rather than sorted, and that is deliberate — the
 * alternative is reading the WHOLE directory and sorting it to pick four
 * names, which is the expensive half of a listing done for decoration. What
 * matters is that the answer is stable between two calls on an unchanged
 * directory, and it is.
 */
static int peek_one(int parentfd, const char *dirname, const char *dirpath,
                    long limit)
{
	int fd = openat(parentfd, dirname,
	                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return 0;   /* unreadable, or a symlink — not an error worth a row */

	DIR *d = fdopendir(fd);
	if (!d) {
		close(fd);
		return 0;
	}

	int found = 0;
	long scanned = 0;
	struct dirent *e;
	while ((e = readdir(d)) && found < limit && scanned < PEEK_SCAN) {
		if (e->d_name[0] == '.')
			continue;      /* including . and .. */
		scanned++;
		if (e->d_type == DT_DIR)
			continue;
		if (!previewable(e->d_name))
			continue;

		/* The size travels with the row because the front-end needs it to
		 * decide whether to draw the file ITSELF when the shared cache has
		 * no thumbnail yet — and a 40MB photo read to fill a 20-pixel tile
		 * is exactly what that cap exists to prevent. One fstatat per
		 * REPORTED file, never per entry scanned. */
		struct stat st;
		if (fstatat(dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
			continue;
		if (!S_ISREG(st.st_mode))
			continue;

		char *full = xasprintf("%s/%s", dirpath, e->d_name);
		if (g_out == OUT_REC) {
			char *ed = pct_encode(dirpath, true);
			char *ef = pct_encode(full, true);
			char *sz = xasprintf("%lld", (long long)st.st_size);
			rec_row(3, ed, ef, sz);
			free(ed);
			free(ef);
			free(sz);
		} else {
			printf("%s\n", full);
		}
		free(full);
		found++;
	}

	closedir(d);
	return found;
}

int cmd_peek(int argc, char **argv)
{
	const char *root = NULL;
	long limit = 4;

	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if (!strncmp(a, "--limit=", 8))
			limit = strtol(a + 8, NULL, 10);
		else if (a[0] == '-' && a[1])
			die(_("peek: unknown option '%s'"), a);
		else
			root = a;
	}
	if (limit <= 0 || limit > 16)
		limit = 4;
	if (!root)
		root = ".";

	int fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		die(_("%s: cannot read"), root);

	DIR *d = fdopendir(fd);
	if (!d) {
		close(fd);
		die(_("%s: cannot read"), root);
	}

	if (g_out == OUT_REC)
		rec_row(3, "dir", "file", "size");

	/* Trailing slash trimmed once, so "/home/velle/" does not produce
	 * "/home/velle//Pictures" — a different string for the same directory,
	 * and the front-end matches these against paths it already holds. */
	size_t rootlen = strlen(root);
	char *base = (rootlen > 1 && root[rootlen - 1] == '/')
	             ? xstrndup(root, rootlen - 1) : xstrdup(root);
	/* "/" is the one directory whose name already ends in the separator, so
	 * joining it the normal way gives "//usr". The front-end compares these
	 * against paths it built itself, and "//usr" matches nothing. */
	if (!strcmp(base, "/")) {
		free(base);
		base = xstrdup("");
	}

	long dirs = 0;
	struct dirent *e;
	while ((e = readdir(d)) && dirs < PEEK_DIRS) {
		if (e->d_name[0] == '.')
			continue;
		/* DT_UNKNOWN is real on some filesystems; openat with O_DIRECTORY
		 * settles it without a stat, and fails harmlessly on a file. */
		if (e->d_type != DT_DIR && e->d_type != DT_UNKNOWN)
			continue;

		char *sub = xasprintf("%s/%s", base, e->d_name);
		peek_one(dirfd(d), e->d_name, sub, limit);
		free(sub);
		dirs++;
	}

	closedir(d);
	free(base);
	return 0;
}
