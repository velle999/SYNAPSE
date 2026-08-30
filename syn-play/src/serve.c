/* serve.c — the long-lived process the window talks to.
 *
 * The same split syn-edit and vibe use, for the same reason: the window is a
 * renderer and owns no state. It writes one command per line on stdin and draws
 * the records that come back on stdout. Everything it can do, the CLI can do,
 * because it is the same code underneath.
 *
 * ── The protocol ────────────────────────────────────────────────────────────
 *
 * Tab-separated, one record per line, first field a key. ⚠ EVERY field that
 * came from a filesystem is percent-encoded — a filename may contain a tab and
 * a title may contain a newline, and both are separators here.
 *
 *   s <key> <value>        one fact: state, path, title, pos, duration, …
 *   q <i> <cur> <t> <p>    one queue row, between q-begin and q-end
 *   h <when> <pos> <dur> <t> <p>   one history row, between h-begin and h-end
 *   l <name> <count>       one saved playlist, between l-begin and l-end
 *   f <t> <p>              one quick-open hit, between f-begin and f-end
 *   e                      end of a burst; the window redraws on this
 *
 * ⚠ THE LISTS ARE SENT ONLY WHEN THEY CHANGE. Scalars go out four times a
 * second because a seek bar has to move; re-sending a thousand-row queue at
 * that rate would be a megabyte a second down a pipe for a list nobody touched.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

/*
 * ⛔ poll() ON A FILE DESCRIPTOR AND fgets() ON THE SAME STREAM DO NOT MIX.
 *
 * This read commands with `sp_getline(stdin, …)`, which is fgets, which is
 * BUFFERED. When the window sends two lines in one tick — which it does every
 * time somebody drops two files, or opens three from the chooser — both arrive
 * in a single write. fgets pulls the whole lot into stdio's buffer and hands
 * back the first line; the second is then sitting in libc, where poll() on the
 * FD cannot see it. poll times out, nothing more is read, and the second
 * command is lost in silence. Dropping two files queued one, every time.
 *
 * ⚠ AND IT IS INVISIBLE FROM THE OTHER END. Driving `syn-play serve` from a
 * shell works fine — two `printf`s are two write() calls, so the reader usually
 * sees them separately — which is exactly why this survived being tested by
 * hand and only fell out of driving the real window.
 *
 * So the buffer is ours: read what is there, then drain EVERY complete line out
 * of it before polling again. This also handles the other half of the same
 * problem, a line split across two reads.
 */
static char  g_in[PATH_MAX * 4];
static size_t g_in_len;

static bool in_fill(void)
{
	if (g_in_len >= sizeof g_in - 1) {
		/* A line longer than this is not a command. Dropped rather than
		 * left to wedge the reader for ever. */
		g_in_len = 0;
		return true;
	}
	ssize_t r = read(STDIN_FILENO, g_in + g_in_len, sizeof g_in - 1 - g_in_len);
	if (r < 0) return errno == EINTR;
	if (r == 0) return false;             /* the window is gone */
	g_in_len += (size_t)r;
	return true;
}

static bool in_take(char *line, size_t cap)
{
	char *nl = memchr(g_in, '\n', g_in_len);
	if (!nl) return false;

	size_t n = (size_t)(nl - g_in);
	size_t copy = (n < cap - 1) ? n : cap - 1;
	memcpy(line, g_in, copy);
	line[copy] = '\0';
	while (copy && (line[copy - 1] == '\r')) line[--copy] = '\0';

	memmove(g_in, nl + 1, g_in_len - n - 1);
	g_in_len -= n + 1;
	return true;
}

static void rec_str(const char *key, const char *val)
{
	char enc[PATH_MAX * 3];
	sp_enc(val, enc, sizeof enc);
	printf("s\t%s\t%s\n", key, enc);
}

static void rec_num(const char *key, double v)
{
	printf("s\t%s\t%.3f\n", key, v);
}

static void send_queue(int fd)
{
	static sp_entry_t q[4096];
	int n = sp_queue(fd, q, 4096);
	if (n < 0) n = 0;
	printf("q-begin\n");
	for (int i = 0; i < n; i++) {
		char t[1536], p[PATH_MAX * 3];
		sp_enc(q[i].title, t, sizeof t);
		sp_enc(q[i].path, p, sizeof p);
		printf("q\t%d\t%s\t%s\t%s\n", i, q[i].current ? "1" : "0", t, p);
	}
	printf("q-end\n");
}

static void send_history(void)
{
	static sp_hist_t h[400];
	int n = sp_history_read(h, 400);
	printf("h-begin\n");
	for (int i = 0; i < n; i++) {
		char t[1536], p[PATH_MAX * 3];
		sp_enc(h[i].title, t, sizeof t);
		sp_enc(h[i].path, p, sizeof p);
		printf("h\t%lld\t%.3f\t%.3f\t%s\t%s\n",
		       (long long)h[i].when, h[i].pos, h[i].dur, t, p);
	}
	printf("h-end\n");
}

static void send_playlists(void)
{
	printf("l-begin\n");
	sp_out_t save = g_out;
	g_out = OUT_REC;                  /* prints `playlist\t<name>\t<n>` */
	sp_playlist_list();
	g_out = save;
	printf("l-end\n");
}

static void send_find(const char *query)
{
	static sp_entry_t hits[60];
	int n = sp_find(query, hits, 60);
	printf("f-begin\n");
	for (int i = 0; i < n; i++) {
		char t[1536], p[PATH_MAX * 3];
		sp_enc(hits[i].title, t, sizeof t);
		sp_enc(hits[i].path, p, sizeof p);
		printf("f\t%s\t%s\n", t, p);
	}
	printf("f-end\n");
}

/* One argument, already percent-decoded. */
static void handle(char *line, int *fd, bool *want_queue, bool *want_hist,
                   bool *want_pl)
{
	char *sp = strchr(line, ' ');
	char *arg = "";
	if (sp) { *sp = '\0'; arg = sp + 1; }

	char decoded[PATH_MAX * 2];
	sp_dec(arg, decoded, sizeof decoded);

	if (!strcmp(line, "quit")) { g_stop = 1; return; }
	if (!strcmp(line, "refresh")) {
		*want_queue = *want_hist = *want_pl = true;
		return;
	}
	if (!strcmp(line, "find")) { send_find(decoded); printf("e\n"); fflush(stdout); return; }
	if (!strcmp(line, "history-clear")) { sp_history_clear(); *want_hist = true; return; }

	/*
	 * ⛔ DELETING A PLAYLIST DOES NOT NEED A PLAYER, AND USED TO DEMAND ONE.
	 *
	 * velle, 2026-08-30: "delete playlist x button isn't working". `plrm`
	 * sat below the `if (*fd < 0) return;` that guards the transport verbs,
	 * so the ✕ on a playlist did nothing at all unless something happened to
	 * be playing — which, on a window just opened to tidy up playlists, it
	 * never is. A playlist is a file; mpv has no opinion about it.
	 *
	 * ⚠ AND IT GOES THROUGH THE SILENT CORE. `sp_playlist_rm()` prints and
	 * die()s: the print would land in this pipe as a record the window
	 * cannot parse, and the exit would take the engine down — and the window
	 * follows the engine out — over a playlist that was already gone.
	 */
	if (!strcmp(line, "plrm")) {
		sp_playlist_unlink(decoded);
		*want_pl = true;
		return;
	}

	/* Everything past here needs a player, and these are the two verbs that
	 * are allowed to start one. */
	if (!strcmp(line, "play") || !strcmp(line, "add")) {
		if (*fd < 0) *fd = sp_connect_or_start();
		if (*fd < 0) return;
		/* ⚠ EITHER ZONE CAN BE HANDED A FOLDER — a file manager drags one
		 * as readily as a file. sp_load() puts its contents in the queue
		 * rather than the folder itself. */
		if (sp_load(*fd, decoded, line[0] == 'p' ? "replace" : "append-play")) {
			char title[512];
			sp_pretty_title(decoded, title, sizeof title);
			sp_history_note(decoded, title, 0, 0);
			*want_queue = *want_hist = true;
		}
		return;
	}
	if (!strcmp(line, "plload") || !strcmp(line, "plappend")) {
		if (*fd < 0) *fd = sp_connect_or_start();
		if (*fd < 0) return;
		/* ⚠ THE SILENT ONE. This used to call the CLI's verb with stdout
		 * temporarily reopened onto /dev/null — which worked, and left the
		 * engine's one output stream being juggled underneath it for the
		 * length of a call that can talk to mpv. */
		sp_playlist_open(*fd, decoded, line[2] == 'a');
		*want_queue = true;
		return;
	}

	if (*fd < 0) *fd = sp_connect();
	if (*fd < 0) return;

	char args[256];
	if (!strcmp(line, "next"))       sp_cmd(*fd, "\"playlist-next\",\"force\"", NULL, 0);
	else if (!strcmp(line, "prev"))  sp_cmd(*fd, "\"playlist-prev\",\"force\"", NULL, 0);
	else if (!strcmp(line, "toggle")) sp_cmd(*fd, "\"cycle\",\"pause\"", NULL, 0);
	else if (!strcmp(line, "stop"))  { sp_cmd(*fd, "\"quit\"", NULL, 0);
	                                   close(*fd); *fd = -1; }
	else if (!strcmp(line, "shuffle"))   { sp_expand_queue_dirs(*fd);
	                                       sp_cmd(*fd, "\"playlist-shuffle\"", NULL, 0);
	                                       *want_queue = true; }
	else if (!strcmp(line, "unshuffle")) { sp_cmd(*fd, "\"playlist-unshuffle\"", NULL, 0);
	                                       *want_queue = true; }
	else if (!strcmp(line, "clear"))     { sp_cmd(*fd, "\"playlist-clear\"", NULL, 0);
	                                       *want_queue = true; }
	else if (!strcmp(line, "seek")) {
		snprintf(args, sizeof args, "\"seek\",%.3f,\"relative\"", atof(decoded));
		sp_cmd(*fd, args, NULL, 0);
	} else if (!strcmp(line, "seek-abs")) {
		snprintf(args, sizeof args, "\"seek\",%.3f,\"absolute\"", atof(decoded));
		sp_cmd(*fd, args, NULL, 0);
	} else if (!strcmp(line, "volume")) {
		snprintf(args, sizeof args, "\"set_property\",\"volume\",%.0f", atof(decoded));
		sp_cmd(*fd, args, NULL, 0);
	} else if (!strcmp(line, "jump")) {
		snprintf(args, sizeof args, "\"playlist-play-index\",%d", atoi(decoded));
		sp_cmd(*fd, args, NULL, 0);
		*want_queue = true;
	} else if (!strcmp(line, "remove")) {
		snprintf(args, sizeof args, "\"playlist-remove\",%d", atoi(decoded));
		sp_cmd(*fd, args, NULL, 0);
		*want_queue = true;
	} else if (!strcmp(line, "plsave")) {
		/* ⚠ SILENT, AND IT CANNOT EXIT. The CLI's verb die()s on an empty
		 * queue, which is right at a prompt and is the engine vanishing
		 * under a window that is merely showing a button. */
		sp_playlist_store(*fd, decoded, NULL);
		*want_pl = true;
	}
}

int sp_serve(void)
{
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	setvbuf(stdout, NULL, _IOLBF, 0);

	int fd = sp_connect();       /* may be -1: no player yet is normal */
	bool want_queue = true, want_hist = true, want_pl = true;
	int last_count = -1, last_index = -2;
	char last_path[PATH_MAX] = "\x01";

	while (!g_stop) {
		/* ── commands ──────────────────────────────────────────────── */
		struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
		int ready = poll(&pfd, 1, 250);
		if (ready > 0 && (pfd.revents & POLLIN)) {
			if (!in_fill()) break;                        /* window gone */
			/* ⛔ EVERY complete line, not one per wake — see in_fill(). */
			char line[PATH_MAX * 2];
			while (in_take(line, sizeof line))
				if (line[0]) handle(line, &fd, &want_queue, &want_hist, &want_pl);
		} else if (ready > 0) {
			break;                                             /* HUP */
		}

		/* ── facts ─────────────────────────────────────────────────── */
		if (fd < 0) fd = sp_connect();
		if (fd < 0) {
			/*
			 * ⛔ THE HISTORY AND THE PLAYLISTS ARE FILES, NOT MPV.
			 *
			 * velle, 2026-08-30: "the playlist isn't showing after
			 * creating closing and reopening but if i add to queue it
			 * then appears". This branch used to `continue`, which
			 * skipped the three list sends at the bottom of the loop —
			 * so a window opened with nothing playing drew an empty
			 * Playlists tab AND an empty History, and both filled in
			 * the moment something started playing and the loop began
			 * reaching the end. The flags stayed true the whole time;
			 * nothing ever got to them.
			 *
			 * Only the QUEUE belongs to mpv. With no mpv there is no
			 * queue, which is said once rather than left as whatever
			 * the last session put on screen.
			 */
			printf("s\tstate\tstopped\n");
			if (want_queue) {
				printf("q-begin\nq-end\n");
				want_queue = false;
			}
			if (want_hist) { send_history();   want_hist = false; }
			if (want_pl)   { send_playlists(); want_pl   = false; }
			printf("e\n");
			fflush(stdout);
			continue;
		}

		char path[PATH_MAX] = "", title[512] = "";
		double pos = 0, dur = 0, vol = 0, index = -1, count = 0;
		bool paused = false;

		if (!sp_get_str(fd, "path", path, sizeof path)) {
			/* Either nothing is loaded or mpv went away. One reconnect
			 * tells them apart without treating a gap between files as
			 * a dead player. */
			close(fd);
			fd = sp_connect();
			if (fd < 0) continue;
		}
		if (path[0]) sp_now_title(fd, path, title, sizeof title);
		sp_get_num(fd, "time-pos", &pos);
		sp_get_num(fd, "duration", &dur);
		sp_get_num(fd, "volume", &vol);
		sp_get_num(fd, "playlist-pos", &index);
		sp_get_num(fd, "playlist-count", &count);
		sp_get_bool(fd, "pause", &paused);

		rec_str("state", path[0] ? (paused ? "paused" : "playing") : "idle");
		rec_str("path", path);
		rec_str("title", title);
		rec_num("pos", pos);
		rec_num("duration", dur);
		rec_num("volume", vol);
		rec_num("index", index);
		rec_num("count", count);

		/* ⚠ The queue is re-read when its LENGTH or the current row moves,
		 * not on a timer. A shuffle changes neither, which is why every
		 * command that reorders it asks for a re-read itself. */
		if ((int)count != last_count || (int)index != last_index ||
		    strcmp(path, last_path)) {
			want_queue = true;
			last_count = (int)count;
			last_index = (int)index;
			snprintf(last_path, sizeof last_path, "%s", path);
			want_hist = true;
		}

		if (want_queue) { send_queue(fd);   want_queue = false; }
		if (want_hist)  { send_history();   want_hist  = false; }
		if (want_pl)    { send_playlists(); want_pl    = false; }

		printf("e\n");
		fflush(stdout);
	}

	if (fd >= 0) close(fd);
	return 0;
}
