/* playlist.c — the queue mpv is holding, and the ones kept on disk.
 *
 * ⛔ MPV OWNS THE QUEUE. This file reads it and writes m3u8 files; it does not
 * keep a second copy. A frontend holding its own list would go out of step the
 * first time somebody dropped a file on the mpv window, hit `>` in mpv itself,
 * or let a playlist finish — and the copy that is wrong is always the one on
 * screen.
 *
 * ⚠ M3U8 BECAUSE MPV READS IT. `loadlist` takes it directly, so a playlist
 * saved here opens in mpv, in VLC, on a phone, and in this program next year
 * whatever happens to this code. A bespoke format would have been less work
 * today and a file nothing else can open for ever.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * ⛔ THE REPLY IS AS LONG AS THE LIBRARY, AND IT USED TO BE READ INTO 256 KB.
 *
 * A queue was a handful of rows for as long as a folder was ONE of them. Once
 * a directory expands into its files, `get_property playlist` is proportional
 * to how much music somebody owns — and the fixed buffer here, plus the fixed
 * one inside the socket reader, turned that into an EMPTY QUEUE beside a
 * player that was working perfectly. sp_cmd_full() and the array iterator are
 * both here for this one call.
 */
int sp_queue(int fd, sp_entry_t *out, int max)
{
	const char *reply = sp_cmd_full(fd, "\"get_property\",\"playlist\"");
	if (!reply) return -1;

	size_t len = 0;
	const char *arr = sp_json_raw(reply, "data", &len);
	if (!arr) return 0;

	const char *it;
	sp_json_iter(arr, &it);

	int n = 0;
	for (int i = 0; i < max; i++) {
		size_t elen = 0;
		const char *e = sp_json_next(&it, &elen);
		if (!e) break;

		/* Bounded copy of the element so the field readers see one object.
		 * A playlist entry is a filename and two flags; anything that does
		 * not fit in this is not an entry. */
		char item[PATH_MAX + 1024];
		if (elen >= sizeof item) continue;
		memcpy(item, e, elen);
		item[elen] = '\0';

		sp_entry_t *slot = &out[n];
		memset(slot, 0, sizeof *slot);
		if (!sp_json_str(item, "filename", slot->path, sizeof slot->path))
			continue;
		/* ⚠ mpv only carries `title` when the playlist named one — an m3u
		 * #EXTINF, mostly. Without it the filename is what a person has to
		 * read, so it is made readable rather than shown raw. */
		if (!sp_json_str(item, "title", slot->title, sizeof slot->title))
			sp_pretty_title(slot->path, slot->title, sizeof slot->title);
		sp_json_bool(item, "current", &slot->current);
		n++;
	}
	return n;
}

/*
 * ⛔ THE QUEUE CAN ALREADY HOLD FOLDERS, AND SETTING THE OPTION DOES NOT GO
 * BACK FOR THEM.
 *
 * sp_load() puts every LATER folder in as its files, but mpv does not revisit
 * entries it has already taken — so a queue built before this program was
 * upgraded, or by dropping a folder on the mpv window itself, still has a
 * folder sitting in it as one row, and no option set afterwards reaches it.
 * Shuffling that is the exact complaint this fixes, so shuffle re-asks for
 * those rows first: the directory is removed and handed back through sp_load(),
 * which loads it as the playlist it is.
 *
 * ⚠ SAFE ONLY BECAUSE SHUFFLE IS ABOUT TO REORDER EVERYTHING ANYWAY. This
 * appends the expansion at the end rather than in place, which would be a
 * visible reordering at any other moment — and `playlist-unshuffle` restores
 * the order files were ADDED in, so the added order is what this is spending.
 * Do not call it from anywhere that is not immediately shuffling.
 *
 * ⛔ THE PLAYING ROW IS LEFT ALONE. `playlist-remove` on the current entry
 * stops or advances playback, so a folder that mpv is in the middle of opening
 * would cut the music off to tidy up the queue. It is already being expanded by
 * the play that entered it; skipping it costs nothing.
 *
 * ⚠ BACKWARDS, because removing index i renumbers everything after it.
 */
int sp_expand_queue_dirs(int fd)
{
	if (fd < 0) return 0;

	/* ⛔ ASKED HERE TOO, not assumed. The transport verbs reach mpv through
	 * sp_connect(), which does not set this — so a `syn-play shuffle` would
	 * hand each folder straight back to a player still expanding one level
	 * at a time, and re-queue the same directory rows it just removed. */
	sp_dir_recursion(fd);

	static sp_entry_t q[4096];
	int n = sp_queue(fd, q, 4096);
	if (n <= 0) return 0;

	int done = 0;
	for (int i = n - 1; i >= 0; i--) {
		if (q[i].current) continue;

		struct stat st;
		if (stat(q[i].path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

		char args[64];
		snprintf(args, sizeof args, "\"playlist-remove\",%d", i);
		if (!sp_cmd(fd, args, NULL, 0)) continue;

		/* ⚠ `append`, NOT `append-play`. Tidying the queue must not start
		 * playing something; there may be nothing playing at all. */
		if (sp_load(fd, q[i].path, "append")) done++;
	}
	return done;
}

/*
 * ⚠ A PLAYLIST NAME BECOMES A FILENAME, so it is checked rather than trusted.
 * `../../.bashrc` is a name somebody can type, and a save that took it would
 * write outside the playlist directory. Letters, digits, space, dash,
 * underscore and dot — and never a leading dot, which would hide the file from
 * the very listing that is meant to show it.
 */
bool sp_playlist_path(const char *name, char *out, size_t cap)
{
	if (!name || !*name || name[0] == '.') return false;
	if (strlen(name) > 120) return false;
	for (const char *p = name; *p; p++) {
		if (isalnum((unsigned char)*p)) continue;
		if (strchr(" -_.", *p)) continue;
		return false;
	}
	snprintf(out, cap, "%s/%s.m3u8", sp_playlist_dir(), name);
	return true;
}

/*
 * ── The file operations, which neither print nor exit ───────────────────────
 *
 * ⛔ SEPARATE FROM THE VERBS BELOW BECAUSE `serve` CALLS THESE.
 *
 * A function that prints writes down the PROTOCOL PIPE — the window then reads
 * `Removed Saturday night` as a record and has no idea what it is. Worse, one
 * that die()s takes the engine with it, and the window follows the engine out
 * (`onExited: Qt.quit()`), so a click on the ✕ of a playlist that was already
 * gone would close the whole player.
 *
 * The CLI's verbs are thin wrappers that add the printing and the exiting.
 */

/* Rows written, or -1 with `why` set to a sentence. */
int sp_playlist_store(int fd, const char *name, const char **why)
{
	char path[PATH_MAX];
	if (!sp_playlist_path(name, path, sizeof path)) {
		if (why) *why = "not a name a playlist can have — letters, digits, "
		                "space, - _ .";
		return -1;
	}

	static sp_entry_t q[4096];
	int n = sp_queue(fd, q, 4096);
	if (n < 0) { if (why) *why = "nothing is playing"; return -1; }
	if (n == 0) { if (why) *why = "the queue is empty"; return -1; }

	/* Written to a temporary and renamed. A save interrupted half way
	 * through would otherwise leave a playlist that is neither the old one
	 * nor the new one, and the person finds out when they open it. */
	char tmp[PATH_MAX + 8];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) { if (why) *why = "cannot write there"; return -1; }

	fputs("#EXTM3U\n", f);
	for (int i = 0; i < n; i++) {
		/* ⚠ #EXTINF carries the title, so a saved playlist opens with the
		 * names that were on screen rather than a column of paths. -1 is
		 * "duration unknown", which is honest: this program does not
		 * measure files it has not played. */
		fprintf(f, "#EXTINF:-1,%s\n%s\n", q[i].title, q[i].path);
	}
	/* Written to a temporary and renamed. A save interrupted half way
	 * through would otherwise leave a playlist that is neither the old one
	 * nor the new one, and the person finds out when they open it. */
	if (fclose(f) != 0) { unlink(tmp); if (why) *why = "could not finish writing"; return -1; }
	if (rename(tmp, path) != 0) { unlink(tmp); if (why) *why = "could not save"; return -1; }
	return n;
}

/* True if it was there and is not any more. */
bool sp_playlist_unlink(const char *name)
{
	char path[PATH_MAX];
	if (!sp_playlist_path(name, path, sizeof path)) return false;
	return unlink(path) == 0;
}

/* Hand a saved playlist to mpv. False if there is no such playlist or mpv
 * refused it. */
bool sp_playlist_open(int fd, const char *name, bool append)
{
	char path[PATH_MAX];
	if (!sp_playlist_path(name, path, sizeof path)) return false;
	if (access(path, R_OK) != 0) return false;

	char quoted[PATH_MAX * 2], args[PATH_MAX * 2 + 64];
	sp_json_quote(path, quoted, sizeof quoted);
	snprintf(args, sizeof args, "\"loadlist\",%s,\"%s\"",
	         quoted, append ? "append" : "replace");
	return sp_cmd(fd, args, NULL, 0);
}

/* ── and the CLI's verbs, which are those plus words ─────────────────────── */

int sp_playlist_save(int fd, const char *name)
{
	const char *why = "could not save";
	int n = sp_playlist_store(fd, name, &why);
	if (n < 0) die("%s", why);

	if (g_out == OUT_REC) printf("saved\t%s\t%d\n", name, n);
	else printf("Saved %s — %d item%s\n", name, n, n == 1 ? "" : "s");
	return 0;
}

int sp_playlist_load(int fd, const char *name, bool append)
{
	char path[PATH_MAX];
	if (!sp_playlist_path(name, path, sizeof path))
		die("'%s' is not a playlist name", name);
	if (access(path, R_OK) != 0)
		die("no playlist called '%s' — syn-play playlist list", name);
	if (!sp_playlist_open(fd, name, append))
		die("mpv would not load %s", path);

	if (g_out == OUT_REC) printf("loaded\t%s\n", name);
	else printf("%s %s\n", append ? "Appended" : "Playing", name);
	return 0;
}

static int name_cmp(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

int sp_playlist_list(void)
{
	DIR *d = opendir(sp_playlist_dir());
	if (!d) return 0;

	char *names[1024];
	int n = 0;
	struct dirent *de;
	while ((de = readdir(d)) && n < 1024) {
		size_t len = strlen(de->d_name);
		if (len < 6 || strcmp(de->d_name + len - 5, ".m3u8")) continue;
		char *s = strdup(de->d_name);
		if (!s) break;
		s[len - 5] = '\0';
		names[n++] = s;
	}
	closedir(d);
	qsort(names, (size_t)n, sizeof names[0], name_cmp);

	for (int i = 0; i < n; i++) {
		char path[PATH_MAX];
		int items = 0;
		if (sp_playlist_path(names[i], path, sizeof path)) {
			FILE *f = fopen(path, "r");
			if (f) {
				char line[PATH_MAX + 512];
				while (sp_getline(f, line, sizeof line))
					if (line[0] && line[0] != '#') items++;
				fclose(f);
			}
		}
		if (g_out == OUT_REC) printf("playlist\t%s\t%d\n", names[i], items);
		else printf("  %-24s %d item%s\n", names[i], items,
		            items == 1 ? "" : "s");
		free(names[i]);
	}
	if (n == 0 && g_out == OUT_HUMAN)
		printf("  no playlists yet — syn-play playlist save <name>\n");
	return 0;
}

int sp_playlist_rm(const char *name)
{
	char path[PATH_MAX];
	if (!sp_playlist_path(name, path, sizeof path))
		die("'%s' is not a playlist name", name);
	if (!sp_playlist_unlink(name))
		die("no playlist called '%s'", name);
	if (g_out == OUT_REC) printf("removed\t%s\n", name);
	else printf("Removed %s\n", name);
	return 0;
}
