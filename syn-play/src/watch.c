/* watch.c — the one process that writes history.
 *
 * ⛔ ONE WRITER, DELIBERATELY. The window, the TUI and every CLI invocation can
 * all be running at once; if each of them wrote history there would be four
 * processes rewriting the same file with four ideas of what the current
 * position is, and the last one to finish would win. So `watch` writes and
 * everything else reads. It is started with the session and lives exactly as
 * long as mpv does.
 *
 * ⚠ IT POLLS RATHER THAN OBSERVING. mpv's `observe_property` events arrive on
 * the same socket as command replies, so a process doing both has to match
 * every reply by request_id and route everything else — which this had, and
 * which bought nothing: history needs to be right within a few seconds, not
 * within a frame. Asking twice a second for two numbers on a unix socket costs
 * less than the machinery to avoid asking.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"

#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

int sp_watch(void)
{
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);
	/* ⚠ mpv going away mid-write is an EPIPE, not a reason to be killed. */
	signal(SIGPIPE, SIG_IGN);

	int fd = sp_connect();
	if (fd < 0) return 3;

	char cur[PATH_MAX] = "", cur_title[512] = "";
	double cur_pos = 0, cur_dur = 0;
	int since_flush = 0;

	while (!g_stop) {
		char path[PATH_MAX] = "", title[512] = "";
		double pos = 0, dur = 0;

		if (!sp_get_str(fd, "path", path, sizeof path)) {
			/*
			 * A failed read is either mpv gone or nothing loaded. Told
			 * apart by trying once more on a fresh connection — because
			 * treating "between files" as "mpv exited" would stop the
			 * history writer in the middle of a playlist.
			 */
			close(fd);
			fd = sp_connect();
			if (fd < 0) break;
			path[0] = '\0';
		} else {
			sp_now_title(fd, path, title, sizeof title);
			sp_get_num(fd, "time-pos", &pos);
			sp_get_num(fd, "duration", &dur);
		}

		if (strcmp(path, cur)) {
			/* ⛔ THE OUTGOING FILE IS WRITTEN DOWN BEFORE THE NEW ONE.
			 * Its last known position is the only record that it was
			 * watched to that point — the next poll is about a different
			 * file and there is no second chance at it. */
			if (cur[0]) sp_history_note(cur, cur_title, cur_pos, cur_dur);
			snprintf(cur, sizeof cur, "%s", path);
			snprintf(cur_title, sizeof cur_title, "%s", title);
			cur_pos = pos;
			cur_dur = dur;
			if (cur[0]) sp_history_note(cur, cur_title, cur_pos, cur_dur);
			since_flush = 0;
		} else if (path[0]) {
			cur_pos = pos;
			if (dur > 0.5) cur_dur = dur;
			/* Every ~10s, not every poll: this rewrites a file, and a
			 * disk write twice a second for a number nobody is reading
			 * is a laptop that never idles. */
			if (++since_flush >= 20) {
				sp_history_note(cur, cur_title, cur_pos, cur_dur);
				since_flush = 0;
			}
		}

		struct timespec ts = { 0, 500 * 1000 * 1000 };
		nanosleep(&ts, NULL);
	}

	/* ⚠ AND ON THE WAY OUT. Ctrl+C, a logout or mpv quitting all land here,
	 * and the position at that moment is the one worth keeping — it is the
	 * one the person stopped at. */
	if (cur[0]) sp_history_note(cur, cur_title, cur_pos, cur_dur);
	if (fd >= 0) close(fd);
	return 0;
}
