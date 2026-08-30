/* main.c — syn-play's command line, and the launcher for its other two faces.
 *
 * ── What this program is ────────────────────────────────────────────────────
 *
 * A front end for mpv: a queue you can see, playlists you can keep, a shuffle,
 * a quick-open that does not need a path, and a history of what has been
 * played. mpv does the playing, the shuffling, the resuming and the m3u
 * reading; every one of those was checked before anything here was written,
 * because a second implementation of a playlist is a second playlist to
 * disagree with.
 *
 * ── Three faces, one implementation ─────────────────────────────────────────
 *
 *   syn-play …        the CLI, and the source of truth
 *   syn-play tui      the same thing with arrow keys
 *   syn-play gui      a quickshell window driven by `syn-play serve`
 *
 * None of them holds state. Every one of them asks the socket.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"
#include "config.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *f)
{
	fputs(
"syn-play — playlists, shuffle, quick open and history, on top of mpv\n"
"\n"
"  syn-play <file|url> ...        play these now\n"
"  syn-play add <file|url> ...    add them to the end of the queue\n"
"  syn-play open <words>          find the best match and play it\n"
"  syn-play find <words>          list what those words match\n"
"  syn-play resume                the last thing played, where it stopped\n"
"\n"
"  syn-play status                what is playing\n"
"  syn-play queue                 the queue, current line marked\n"
"  syn-play next | prev | jump N\n"
"  syn-play pause | resume-play | toggle\n"
"  syn-play seek <+N|-N|MM:SS>    relative, or absolute with a colon\n"
"  syn-play volume <N|+N|-N>\n"
"  syn-play shuffle | unshuffle\n"
"  syn-play loop off|file|playlist\n"
"  syn-play remove N | clear | stop\n"
"\n"
"  syn-play playlist list\n"
"  syn-play playlist save|load|append|rm <name>\n"
"  syn-play history [N] | history clear\n"
"\n"
"  syn-play tui | gui\n"
"\n"
"  --rec        one record per line, tab separated, for another program\n"
"  --version    print the version\n"
, f);
}

/* ── small shared helpers ───────────────────────────────────────────────── */

static void fmt_time(double secs, char *out, size_t cap)
{
	if (secs < 0 || secs != secs) { snprintf(out, cap, "--:--"); return; }
	long t = (long)(secs + 0.5);
	long h = t / 3600, m = (t % 3600) / 60, s = t % 60;
	if (h) snprintf(out, cap, "%ld:%02ld:%02ld", h, m, s);
	else   snprintf(out, cap, "%ld:%02ld", m, s);
}

/* "90", "1:30" and "-30" are all things people type at a player. */
static bool parse_time(const char *s, double *out, bool *absolute)
{
	if (!s || !*s) return false;
	*absolute = strchr(s, ':') != NULL;
	if (*absolute) {
		double parts[3] = { 0, 0, 0 };
		int n = 0;
		const char *p = s;
		while (n < 3 && *p) {
			char *end = NULL;
			parts[n++] = strtod(p, &end);
			if (end == p) return false;
			p = (*end == ':') ? end + 1 : end;
			if (*end != ':') break;
		}
		double v = 0;
		for (int i = 0; i < n; i++) v = v * 60 + parts[i];
		*out = v;
		return true;
	}
	char *end = NULL;
	*out = strtod(s, &end);
	return end != s;
}

/*
 * ⛔ A BARE NAME IS STILL A PATH, and it has to become an absolute one before
 * mpv sees it. mpv's working directory is wherever it was started — which for a
 * player launched from the dock months ago is `/`, and for one started by this
 * program is whatever shell did it. Handing over "episode1.mkv" and hoping is
 * how "file not found" happens for a file that is plainly right there.
 *
 * ⚠ A URL IS LEFT ALONE. realpath() on "https://…" fails, and falling back to
 * the raw string is exactly right for one and exactly wrong for a typo'd path,
 * so the two cases are told apart up front rather than by accident.
 */
static bool resolve_target(const char *in, char *out, size_t cap)
{
	if (strstr(in, "://")) { snprintf(out, cap, "%s", in); return true; }

	char buf[PATH_MAX];
	if (!realpath(in, buf)) return false;
	snprintf(out, cap, "%s", buf);
	return true;
}

/* loadfile for each, the first replacing and the rest appending — so
 * `syn-play a b c` plays a and queues b and c, which is what handing a player
 * three files means everywhere else. */
static int load_targets(int fd, char **paths, int n, bool append_all)
{
	int loaded = 0;
	for (int i = 0; i < n; i++) {
		char abs[PATH_MAX];
		if (!resolve_target(paths[i], abs, sizeof abs)) {
			warn("no such file: %s", paths[i]);
			continue;
		}

		char quoted[PATH_MAX * 2], args[PATH_MAX * 2 + 64];
		sp_json_quote(abs, quoted, sizeof quoted);
		bool first = (loaded == 0 && !append_all);
		snprintf(args, sizeof args, "\"loadfile\",%s,\"%s\"",
		         quoted, first ? "replace" : "append-play");
		if (!sp_cmd(fd, args, NULL, 0)) {
			warn("mpv would not take %s", abs);
			continue;
		}

		char title[512];
		sp_pretty_title(abs, title, sizeof title);
		/* ⚠ NOTED HERE, not only by `watch`. The watcher is what keeps the
		 * position up to date, but it may not be running — and a file that
		 * was played once, briefly, still belongs in the history. */
		sp_history_note(abs, title, 0, 0);
		loaded++;

		if (g_out == OUT_REC) printf("load\t%s\t%s\n", first ? "play" : "queue", abs);
	}
	if (!loaded) return 1;
	if (g_out == OUT_HUMAN) {
		char title[512];
		char abs[PATH_MAX];
		if (resolve_target(paths[0], abs, sizeof abs)) {
			sp_pretty_title(abs, title, sizeof title);
			printf("%s %s%s\n", append_all ? "Queued" : "Playing", title,
			       loaded > 1 ? " (+ more)" : "");
		}
	}
	return 0;
}

/* Every transport verb needs a live session and none of them should start one:
 * `syn-play next` with nothing playing is a question, not an instruction to
 * open a player. */
static int need_fd(void)
{
	int fd = sp_connect();
	if (fd < 0) {
		if (g_out == OUT_REC) printf("state\tstopped\n");
		else fprintf(stderr, "syn-play: nothing is playing\n");
		exit(3);
	}
	return fd;
}

/* ── the verbs ──────────────────────────────────────────────────────────── */

static int cmd_status(void)
{
	int fd = sp_connect();
	if (fd < 0) {
		if (g_out == OUT_REC) printf("state\tstopped\n");
		else printf("Nothing is playing.\n");
		return 3;
	}

	char path[PATH_MAX] = "", title[512] = "";
	double pos = 0, dur = 0, vol = 0, idx = -1, count = 0;
	bool paused = false;

	sp_get_str(fd, "path", path, sizeof path);
	sp_now_title(fd, path, title, sizeof title);
	sp_get_num(fd, "time-pos", &pos);
	sp_get_num(fd, "duration", &dur);
	sp_get_num(fd, "volume", &vol);
	sp_get_num(fd, "playlist-pos", &idx);
	sp_get_num(fd, "playlist-count", &count);
	sp_get_bool(fd, "pause", &paused);

	if (g_out == OUT_REC) {
		char p[PATH_MAX * 3], t[1536];
		sp_enc(path, p, sizeof p);
		sp_enc(title, t, sizeof t);
		printf("state\t%s\n", path[0] ? (paused ? "paused" : "playing") : "idle");
		printf("path\t%s\n", p);
		printf("title\t%s\n", t);
		printf("pos\t%.3f\n", pos);
		printf("duration\t%.3f\n", dur);
		printf("volume\t%.0f\n", vol);
		printf("index\t%d\n", (int)idx);
		printf("count\t%d\n", (int)count);
	} else if (!path[0]) {
		printf("Idle — a player is running with nothing queued.\n");
	} else {
		char a[16], b[16];
		fmt_time(pos, a, sizeof a);
		fmt_time(dur, b, sizeof b);
		printf("%s %s\n", paused ? "Paused" : "Playing", title);
		printf("  %s / %s   volume %.0f   %d of %d\n",
		       a, b, vol, (int)idx + 1, (int)count);
	}
	close(fd);
	return 0;
}

static int cmd_queue(void)
{
	int fd = need_fd();
	static sp_entry_t q[4096];
	int n = sp_queue(fd, q, 4096);
	close(fd);
	if (n < 0) die("mpv would not say what is queued");

	for (int i = 0; i < n; i++) {
		if (g_out == OUT_REC) {
			char p[PATH_MAX * 3], t[1536];
			sp_enc(q[i].path, p, sizeof p);
			sp_enc(q[i].title, t, sizeof t);
			printf("item\t%d\t%s\t%s\t%s\n", i, q[i].current ? "yes" : "no", t, p);
		} else {
			printf("%s%3d  %s\n", q[i].current ? "> " : "  ", i + 1, q[i].title);
		}
	}
	if (n == 0 && g_out == OUT_HUMAN) printf("The queue is empty.\n");
	return 0;
}

static int cmd_history(int argc, char **argv)
{
	if (argc && !strcmp(argv[0], "clear")) {
		sp_history_clear();
		if (g_out == OUT_HUMAN) printf("History cleared.\n");
		return 0;
	}

	int want = argc ? atoi(argv[0]) : 25;
	if (want <= 0) want = 25;

	static sp_hist_t h[2000];
	int n = sp_history_read(h, 2000);
	if (n > want) n = want;

	for (int i = 0; i < n; i++) {
		if (g_out == OUT_REC) {
			char p[PATH_MAX * 3], t[1536];
			sp_enc(h[i].path, p, sizeof p);
			sp_enc(h[i].title, t, sizeof t);
			printf("hist\t%lld\t%.3f\t%.3f\t%s\t%s\n",
			       (long long)h[i].when, h[i].pos, h[i].dur, t, p);
		} else {
			char at[16];
			fmt_time(h[i].pos, at, sizeof at);
			/* ⚠ The position is only worth printing when it means
			 * something. "0:00 in" beside every row is noise that
			 * hides the two rows where it is the point. */
			if (h[i].pos > 30 && (h[i].dur <= 0 || h[i].pos < h[i].dur - 30))
				printf("  %-52s  %s in\n", h[i].title, at);
			else
				printf("  %s\n", h[i].title);
		}
	}
	if (n == 0 && g_out == OUT_HUMAN)
		printf("  nothing played yet\n");
	return 0;
}

static int cmd_find(const char *query, bool play)
{
	static sp_entry_t hits[200];
	int n = sp_find(query, hits, play ? 1 : 40);
	if (n == 0) {
		if (g_out == OUT_REC) printf("none\t%s\n", query);
		else fprintf(stderr, "syn-play: nothing matches '%s'\n", query);
		return 4;
	}

	if (!play) {
		for (int i = 0; i < n; i++) {
			if (g_out == OUT_REC) {
				char p[PATH_MAX * 3], t[1536];
				sp_enc(hits[i].path, p, sizeof p);
				sp_enc(hits[i].title, t, sizeof t);
				printf("hit\t%s\t%s\n", t, p);
			} else {
				printf("  %s\n", hits[i].title);
			}
		}
		return 0;
	}

	int fd = sp_connect_or_start();
	if (fd < 0) die("could not start mpv");
	char *one = hits[0].path;
	int rc = load_targets(fd, &one, 1, false);
	close(fd);
	return rc;
}

static int cmd_resume(void)
{
	static sp_hist_t h[1];
	if (sp_history_read(h, 1) < 1) {
		fprintf(stderr, "syn-play: nothing has been played yet\n");
		return 4;
	}
	int fd = sp_connect_or_start();
	if (fd < 0) die("could not start mpv");
	char *one = h[0].path;
	/*
	 * ⛔ THE SEEK IS MPV'S. --save-position-on-quit wrote a watch-later file
	 * when this stopped, and loadfile alone resumes from it. Seeking to the
	 * position in our own history on top of that would be a second, staler
	 * answer fighting the correct one — visibly, as a jump a second after
	 * playback starts.
	 */
	int rc = load_targets(fd, &one, 1, false);
	close(fd);
	return rc;
}

static int cmd_gui(void)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY"))
		die("no display — syn-play gui needs a graphical session");
	if (access("/usr/bin/quickshell", X_OK) != 0 &&
	    access("/usr/local/bin/quickshell", X_OK) != 0)
		die("quickshell is not installed — synpkg install quickshell");

	/* The window's own Wayland identity, so the dock resolves its .desktop
	 * and it does not inherit the app_id of whatever launched it. */
	setenv("QS_APP_ID", "syn-play", 1);

	const char *qml = SYNPLAY_DATADIR "/syn-play.qml";
	if (access(qml, R_OK) != 0 && access("data/syn-play.qml", R_OK) == 0)
		qml = "data/syn-play.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die("could not start quickshell");
	return 1;
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
	char *pos[256];
	int n = 0;

	for (int i = 1; i < argc && n < 256; i++) {
		char *v = argv[i];
		if (!strcmp(v, "--rec"))     { g_out = OUT_REC; continue; }
		if (!strcmp(v, "--help") || !strcmp(v, "-h")) { usage(stdout); return 0; }
		if (!strcmp(v, "--version")) { printf("syn-play %s\n", SYNPLAY_VERSION); return 0; }
		/*
		 * ⚠ `--` ENDS THE OPTIONS, and after it everything is a path. A
		 * media file called "--rec.mkv" exists as surely as any other, and
		 * without this there is no way to name it.
		 */
		if (!strcmp(v, "--")) {
			for (int j = i + 1; j < argc && n < 256; j++) pos[n++] = argv[j];
			break;
		}
		if (v[0] == '-' && v[1] == '-') {
			warn("unknown option '%s'", v);
			usage(stderr);
			return 2;
		}
		pos[n++] = v;
	}

	if (n == 0) { usage(stdout); return 0; }

	const char *c = pos[0];
	int rest = n - 1;
	char **args = pos + 1;

	/* ── the ones that need no session ─────────────────────────────────── */
	if (!strcmp(c, "gui"))     return cmd_gui();
	if (!strcmp(c, "tui"))     return sp_tui();
	if (!strcmp(c, "serve"))   return sp_serve();
	if (!strcmp(c, "watch"))   return sp_watch();
	if (!strcmp(c, "status"))  return cmd_status();
	if (!strcmp(c, "history")) return cmd_history(rest, args);
	if (!strcmp(c, "resume"))  return cmd_resume();

	if (!strcmp(c, "find")) {
		if (!rest) die("find what? — syn-play find <words>");
		char q[512] = "";
		for (int i = 0; i < rest; i++)
			snprintf(q + strlen(q), sizeof q - strlen(q), "%s%s",
			         i ? " " : "", args[i]);
		return cmd_find(q, false);
	}
	if (!strcmp(c, "open")) {
		if (!rest) die("open what? — syn-play open <words>");
		char q[512] = "";
		for (int i = 0; i < rest; i++)
			snprintf(q + strlen(q), sizeof q - strlen(q), "%s%s",
			         i ? " " : "", args[i]);
		return cmd_find(q, true);
	}

	if (!strcmp(c, "playlist")) {
		if (!rest || !strcmp(args[0], "list")) return sp_playlist_list();
		if (rest < 2) die("syn-play playlist %s <name>", args[0]);
		if (!strcmp(args[0], "rm") || !strcmp(args[0], "remove"))
			return sp_playlist_rm(args[1]);
		if (!strcmp(args[0], "save")) {
			int fd = need_fd();
			int rc = sp_playlist_save(fd, args[1]);
			close(fd);
			return rc;
		}
		if (!strcmp(args[0], "load") || !strcmp(args[0], "append")) {
			int fd = sp_connect_or_start();
			if (fd < 0) die("could not start mpv");
			int rc = sp_playlist_load(fd, args[1], !strcmp(args[0], "append"));
			close(fd);
			return rc;
		}
		die("syn-play playlist list|save|load|append|rm");
	}

	/* ── the ones that start a player ──────────────────────────────────── */
	if (!strcmp(c, "play") || !strcmp(c, "add")) {
		if (!rest) die("syn-play %s <file|url> ...", c);
		int fd = sp_connect_or_start();
		if (fd < 0) die("could not start mpv");
		int rc = load_targets(fd, args, rest, !strcmp(c, "add"));
		close(fd);
		return rc;
	}

	/* ── the ones that need one ────────────────────────────────────────── */
	if (!strcmp(c, "queue"))  return cmd_queue();

	if (!strcmp(c, "next") || !strcmp(c, "prev") || !strcmp(c, "previous")) {
		int fd = need_fd();
		bool ok = sp_cmd(fd, c[0] == 'n' ? "\"playlist-next\",\"force\""
		                                 : "\"playlist-prev\",\"force\"", NULL, 0);
		close(fd);
		if (!ok) { fprintf(stderr, "syn-play: no %s track\n", c); return 3; }
		return g_out == OUT_HUMAN ? cmd_status() : 0;
	}

	if (!strcmp(c, "toggle") || !strcmp(c, "pause") || !strcmp(c, "resume-play")) {
		int fd = need_fd();
		bool ok;
		if (!strcmp(c, "toggle")) ok = sp_cmd(fd, "\"cycle\",\"pause\"", NULL, 0);
		else ok = sp_set_bool(fd, "pause", !strcmp(c, "pause"));
		close(fd);
		if (!ok) die("mpv would not take that");
		return g_out == OUT_HUMAN ? cmd_status() : 0;
	}

	if (!strcmp(c, "seek")) {
		if (!rest) die("seek where? — +30, -10, or 1:23");
		double v;
		bool absolute;
		if (!parse_time(args[0], &v, &absolute)) die("'%s' is not a time", args[0]);
		int fd = need_fd();
		char a[128];
		snprintf(a, sizeof a, "\"seek\",%.3f,\"%s\"", v,
		         absolute ? "absolute" : "relative");
		bool ok = sp_cmd(fd, a, NULL, 0);
		close(fd);
		if (!ok) die("mpv would not seek there");
		return g_out == OUT_HUMAN ? cmd_status() : 0;
	}

	if (!strcmp(c, "volume")) {
		if (!rest) { int fd = need_fd(); double v = 0;
		             sp_get_num(fd, "volume", &v); close(fd);
		             printf(g_out == OUT_REC ? "volume\t%.0f\n" : "%.0f\n", v);
		             return 0; }
		int fd = need_fd();
		char a[128];
		if (args[0][0] == '+' || args[0][0] == '-')
			snprintf(a, sizeof a, "\"add\",\"volume\",%.0f", atof(args[0]));
		else
			snprintf(a, sizeof a, "\"set_property\",\"volume\",%.0f", atof(args[0]));
		bool ok = sp_cmd(fd, a, NULL, 0);
		close(fd);
		if (!ok) die("mpv would not take that volume");
		return 0;
	}

	if (!strcmp(c, "shuffle") || !strcmp(c, "unshuffle")) {
		/*
		 * ⛔ MPV'S OWN SHUFFLE, AND ITS OWN UNDO. `playlist-unshuffle`
		 * restores the ORDER THE FILES WERE ADDED IN — mpv keeps that,
		 * and nothing here could reconstruct it after the fact. Shuffling
		 * a copied list would have thrown that away for good.
		 */
		int fd = need_fd();
		bool ok = sp_cmd(fd, c[0] == 's' ? "\"playlist-shuffle\""
		                                 : "\"playlist-unshuffle\"", NULL, 0);
		close(fd);
		if (!ok) die("mpv would not %s", c);
		if (g_out == OUT_HUMAN) printf("%s.\n", c[0] == 's' ? "Shuffled" : "Unshuffled");
		return 0;
	}

	if (!strcmp(c, "loop")) {
		const char *how = rest ? args[0] : "playlist";
		int fd = need_fd();
		char a[128];
		bool ok;
		if (!strcmp(how, "off"))
			ok = sp_cmd(fd, "\"set_property\",\"loop-playlist\",\"no\"", NULL, 0) &&
			     sp_cmd(fd, "\"set_property\",\"loop-file\",\"no\"", NULL, 0);
		else if (!strcmp(how, "file"))
			ok = sp_cmd(fd, "\"set_property\",\"loop-file\",\"inf\"", NULL, 0);
		else if (!strcmp(how, "playlist"))
			ok = sp_cmd(fd, "\"set_property\",\"loop-playlist\",\"inf\"", NULL, 0);
		else { close(fd); die("syn-play loop off|file|playlist"); }
		(void)a;
		close(fd);
		if (!ok) die("mpv would not set the loop");
		if (g_out == OUT_HUMAN) printf("Loop: %s\n", how);
		return 0;
	}

	if (!strcmp(c, "jump")) {
		if (!rest) die("jump to which line? — syn-play queue");
		int idx = atoi(args[0]) - 1;         /* the queue prints from 1 */
		if (idx < 0) die("the queue starts at 1");
		int fd = need_fd();
		char a[64];
		snprintf(a, sizeof a, "\"playlist-play-index\",%d", idx);
		bool ok = sp_cmd(fd, a, NULL, 0);
		close(fd);
		if (!ok) die("there is no line %s in the queue", args[0]);
		return g_out == OUT_HUMAN ? cmd_status() : 0;
	}

	if (!strcmp(c, "remove")) {
		if (!rest) die("remove which line? — syn-play queue");
		int idx = atoi(args[0]) - 1;
		if (idx < 0) die("the queue starts at 1");
		int fd = need_fd();
		char a[64];
		snprintf(a, sizeof a, "\"playlist-remove\",%d", idx);
		bool ok = sp_cmd(fd, a, NULL, 0);
		close(fd);
		if (!ok) die("there is no line %s in the queue", args[0]);
		return 0;
	}

	if (!strcmp(c, "clear")) {
		int fd = need_fd();
		/* ⚠ `playlist-clear` leaves the CURRENT file playing — that is
		 * mpv's meaning of it, and it is the useful one: clearing what is
		 * queued up is not the same as stopping the music. */
		bool ok = sp_cmd(fd, "\"playlist-clear\"", NULL, 0);
		close(fd);
		if (!ok) die("mpv would not clear the queue");
		if (g_out == OUT_HUMAN) printf("Queue cleared — what is playing continues.\n");
		return 0;
	}

	if (!strcmp(c, "stop") || !strcmp(c, "quit")) {
		int fd = sp_connect();
		if (fd < 0) { if (g_out == OUT_HUMAN) printf("Nothing is playing.\n"); return 0; }
		sp_cmd(fd, "\"quit\"", NULL, 0);
		close(fd);
		if (g_out == OUT_HUMAN) printf("Stopped.\n");
		return 0;
	}

	/*
	 * ⛔ A BARE ARGUMENT IS A FILE, and this is the last branch on purpose.
	 * `syn-play film.mkv` has to work, which means anything not recognised
	 * above is a path — so a MISTYPED VERB would be treated as a filename and
	 * reported as a missing file, which is a confusing way to say "no such
	 * command". It is only taken as a file when it actually is one.
	 */
	{
		char abs[PATH_MAX];
		if (!resolve_target(c, abs, sizeof abs)) {
			warn("unknown command '%s'", c);
			usage(stderr);
			return 2;
		}
		int fd = sp_connect_or_start();
		if (fd < 0) die("could not start mpv");
		int rc = load_targets(fd, pos, n, false);
		close(fd);
		return rc;
	}
}
