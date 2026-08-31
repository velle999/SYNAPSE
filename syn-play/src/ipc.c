/* ipc.c — talking to mpv over its JSON IPC socket.
 *
 * ── Why the player is a separate process ────────────────────────────────────
 *
 * The window is quickshell, and quickshell cannot host libmpv: there is no way
 * to hand a QML scene a video surface it renders into. So mpv keeps its own
 * window and this program is a control surface beside it — which is not a
 * compromise so much as the only shape available, and it has a real advantage:
 * `syn-play next` from a terminal, a keybind, the TUI and the window are all
 * the same one-line command to the same socket, and none of them has to be
 * running for the others to work.
 *
 * ── The protocol ────────────────────────────────────────────────────────────
 *
 * One JSON object per line in, one per line out. Replies carry back the
 * `request_id` they were sent with; EVENTS arrive on the same socket, unasked,
 * in between. So a reader that takes the next line as its answer will sooner or
 * later take a property change instead — reliably, under exactly the load that
 * makes it hardest to notice. Every read here matches on request_id and
 * discards anything else.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Defined with the rest of the reader, below. Called here because a fresh
 * connection can be handed the FD NUMBER of the one just closed — anything
 * still buffered from the old socket has to go when the new one is made. */
static void rd_discard(void);

static int connect_once(void)
{
	const char *path = sp_socket_path();
	struct sockaddr_un a;
	if (strlen(path) >= sizeof a.sun_path) return -1;

	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;

	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
	if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
		close(fd);
		return -1;
	}
	rd_discard();
	return fd;
}

int sp_connect(void)
{
	return connect_once();
}

bool sp_session_live(void)
{
	int fd = connect_once();
	if (fd < 0) return false;
	close(fd);
	return true;
}

/*
 * ⚠ A STALE SOCKET FILE IS NOT A RUNNING PLAYER. mpv does not always unlink it
 * — a SIGKILL never does — so `access()` on the path answers yes for a session
 * that ended some time last week. Only a successful connect() counts, and the
 * leftover is removed before a new mpv is asked to bind it, because mpv will
 * not bind over a file that is already there.
 */
bool sp_start_mpv(void)
{
	const char *sock = sp_socket_path();

	if (sp_session_live()) return true;
	unlink(sock);

	char arg_ipc[PATH_MAX + 32];
	snprintf(arg_ipc, sizeof arg_ipc, "--input-ipc-server=%s", sock);

	pid_t pid = fork();
	if (pid < 0) return false;
	if (pid == 0) {
		/* ⛔ DOUBLE FORK AND setsid, so mpv outlives the shell that started
		 * it and never becomes this process's zombie. A player that dies
		 * when the terminal closes is a player nobody can start from one. */
		if (setsid() == -1) _exit(127);
		pid_t second = fork();
		if (second < 0) _exit(127);
		if (second > 0) _exit(0);

		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDIN_FILENO);
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			if (devnull > STDERR_FILENO) close(devnull);
		}

		char *argv[] = {
			(char *)"mpv",
			arg_ipc,
			/* Stays up with an empty playlist, so `add`, `next` and the
			 * window's transport work between files rather than only
			 * while something happens to be playing. */
			(char *)"--idle=yes",
			/* ⚠ mpv's own status line would fight the TUI for the
			 * terminal, and its key bindings would read the same
			 * keystrokes this program is reading. */
			(char *)"--no-terminal",
			/* ⛔ RESUME IS MPV'S, NOT OURS. --save-position-on-quit plus
			 * the watch-later files it already writes is the whole
			 * feature; a position this program remembered separately
			 * would be a second answer disagreeing with the one mpv
			 * actually seeks to. */
			(char *)"--save-position-on-quit",
			NULL
		};
		execvp("mpv", argv);
		_exit(127);
	}

	int st;
	waitpid(pid, &st, 0);           /* the intermediate child, immediately */

	/* mpv binds the socket a moment after exec. Waited for rather than
	 * assumed: the first command otherwise races it and reports "no session"
	 * for a player that is starting perfectly well. */
	for (int i = 0; i < 100; i++) {
		if (sp_session_live()) return true;
		struct timespec ts = { 0, 50 * 1000 * 1000 };
		nanosleep(&ts, NULL);
	}
	return false;
}

/*
 * ⛔ THE HISTORY WRITER IS STARTED WITH THE PLAYER, not with the window.
 *
 * History has to be kept whether or not anybody is looking at it — a queue left
 * running while the window is closed is exactly the case where "what did I
 * watch last night" is asked later. So `syn-play watch` is spawned here, once
 * per mpv, and exits when mpv does. See watch.c for why it is the ONLY writer.
 *
 * ⚠ /proc/self/exe FIRST. A syn-play run out of a build tree must start ITS
 * watcher, not the installed one — otherwise a test, or a developer, silently
 * exercises the version in /usr and reports it as the one they just changed.
 */
static void start_watcher(void)
{
	pid_t pid = fork();
	if (pid < 0) return;
	if (pid == 0) {
		if (setsid() == -1) _exit(127);
		pid_t second = fork();
		if (second < 0) _exit(127);
		if (second > 0) _exit(0);

		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDIN_FILENO);
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			if (devnull > STDERR_FILENO) close(devnull);
		}

		char self[PATH_MAX];
		ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
		if (n > 0) {
			self[n] = '\0';
			char *argv[] = { self, (char *)"watch", NULL };
			execv(self, argv);
		}
		char *argv[] = { (char *)"syn-play", (char *)"watch", NULL };
		execvp("syn-play", argv);
		_exit(127);
	}
	int st;
	waitpid(pid, &st, 0);
}

/*
 * ⛔ A FOLDER IS ONE PLAYLIST ENTRY UNTIL MPV IS TOLD OTHERWISE, AND SHUFFLE
 * COUNTS IT ONCE.
 *
 * mpv does expand a directory handed to `loadfile` — but `--directory-mode`
 * defaults to `auto`, which its manual defines as `recursive` only with
 * `--shuffle`, and `lazy` otherwise. Lazy stops at the first level: the albums
 * inside an added music folder stay in the playlist as ONE entry each until
 * playback reaches them and opens them. `playlist-shuffle` therefore shuffles a
 * handful of FOLDERS, plays whichever it lands on in track order, and never
 * reaches most of the files. That reads as a broken shuffle rather than as a
 * directory setting, which is why it went unnoticed: every file does eventually
 * play, just never out of order.
 *
 * Asked of mpv rather than walked here, per the rule at the top of synplay.h.
 * A directory walk in this program would be a second idea of what is in a
 * folder, disagreeing with the one that has to open the files anyway — and it
 * would have to re-derive `--directory-filter-types` to boot.
 *
 * ⛔ SET AS A PROPERTY, NEVER AS A COMMAND-LINE FLAG. Both of these options are
 * newer than the oldest mpv this frontend can talk to, and mpv EXITS on an
 * option it does not know — so passing them in `sp_start_mpv()`'s argv would
 * turn an older player into "syn-play cannot start mpv" with nothing on screen
 * to say why. An unknown PROPERTY is a failed reply on a live socket, which is
 * ignored here and costs nothing. It also reaches an mpv that was already
 * running before this program was upgraded.
 *
 * ⚠ `image` COMES OUT OF THE FILTER, and only recursion made that matter. mpv's
 * default types include images, so recursing into an album folder queues the
 * cover.jpg sitting beside the tracks and a shuffled queue stops on artwork.
 * This program's own media list (MEDIA_EXT in open.c) has never contained an
 * image format; the filter is set to agree with it rather than to invent a
 * policy.
 */
void sp_dir_recursion(int fd)
{
	if (fd < 0) return;
	sp_cmd(fd, "\"set_property\",\"directory-mode\",\"recursive\"", NULL, 0);
	sp_cmd(fd, "\"set_property\",\"directory-filter-types\","
	           "\"video,audio,archive,playlist\"", NULL, 0);
}

/*
 * ⛔ A DIRECTORY GOES IN WITH `loadlist`, NOT `loadfile`, AND THE DIFFERENCE IS
 * WHEN IT EXPANDS.
 *
 * Measured on mpv 0.41: `loadfile <dir> replace` expands the folder into
 * entries straight away — because that call is opening it — but `loadfile <dir>
 * append` and `append-play` leave it in the playlist as ONE row until playback
 * arrives there and opens it. So a folder that is QUEUED rather than played
 * stays a single row, and `playlist-shuffle` gives an album of forty tracks one
 * ticket in the draw. `loadlist` expands a directory at the moment it is asked,
 * in every mode, and answers with how many entries it added.
 *
 * ⚠ THE SAME COMMAND THAT LOADS AN m3u8, which is not a trick: mpv treats a
 * directory as a playlist, so this is a folder being read as what it already
 * is. Both spellings take the same mode words, so no caller has to branch.
 *
 * ⚠ AND IT STILL NEEDS sp_dir_recursion(). `loadlist` honours
 * `--directory-mode` — with the default it expands one level and leaves the
 * album folders inside as rows of their own.
 */
bool sp_load(int fd, const char *path, const char *mode)
{
	struct stat st;
	bool dir = (stat(path, &st) == 0 && S_ISDIR(st.st_mode));

	char quoted[PATH_MAX * 2], args[PATH_MAX * 2 + 64];
	sp_json_quote(path, quoted, sizeof quoted);
	snprintf(args, sizeof args, "\"%s\",%s,\"%s\"",
	         dir ? "loadlist" : "loadfile", quoted, mode);
	return sp_cmd(fd, args, NULL, 0);
}

int sp_connect_or_start(void)
{
	int fd = connect_once();
	if (fd >= 0) { sp_dir_recursion(fd); return fd; }
	if (!sp_start_mpv()) return -1;
	start_watcher();
	fd = connect_once();
	sp_dir_recursion(fd);
	return fd;
}

/* ── one command, one reply ─────────────────────────────────────────────── */

static bool write_all(int fd, const char *buf, size_t n)
{
	while (n) {
		ssize_t w = write(fd, buf, n);
		if (w < 0) {
			if (errno == EINTR) continue;
			return false;
		}
		buf += w;
		n -= (size_t)w;
	}
	return true;
}

/* Reads one \n-terminated line. Returns false at EOF or on error. */
/*
 * ⛔ A REPLY IS AS BIG AS THE PLAYLIST, AND A FIXED BUFFER TRUNCATED IT
 * *AND* DESYNCHRONISED THE SOCKET.
 *
 * This read one byte at a time into a 64 KB caller buffer and, on overflow,
 * returned the truncated line AS IF IT WERE COMPLETE — leaving the rest of it
 * unread in the socket, where the next read picked it up as a fresh message.
 * mpv puts `request_id` at the END of a reply, so a truncated one matches
 * nothing, and sp_cmd() then spun through fragments until it blocked on a line
 * that was never coming.
 *
 * ⚠ It never fired while a folder was ONE queue row. Recursive expansion made
 * `get_property playlist` proportional to a music library, and the symptom was
 * "the queue is empty but it is playing" plus a command that hung — the queue
 * read failing while every short reply carried on working perfectly.
 *
 * So: the line grows to whatever arrives, and a partial read is never handed
 * back as a whole one.
 *
 * ⚠ AND BUFFERED. One `read()` per BYTE is 90,000 syscalls for a 600-track
 * folder and millions for a library; the readahead below makes it one per
 * 64 KB. It is discarded on connect rather than tracked per-fd — a new
 * connection can be handed the same fd number as the one just closed, so
 * anything left from the old socket has to go at the moment the new one is
 * made, not the next time the number changes.
 */
static char   rd_buf[65536];
static size_t rd_len, rd_off;
static char  *rd_line;
static size_t rd_cap;

static void rd_discard(void) { rd_len = rd_off = 0; }

static int rd_getc(int fd)
{
	if (rd_off >= rd_len) {
		ssize_t r;
		do { r = read(fd, rd_buf, sizeof rd_buf); } while (r < 0 && errno == EINTR);
		if (r <= 0) return -1;
		rd_len = (size_t)r;
		rd_off = 0;
	}
	return (unsigned char)rd_buf[rd_off++];
}

/* The whole of the next line, however long. Points at a buffer owned here and
 * valid until the next call. False at end of stream. */
static bool read_line(int fd, char **out)
{
	size_t o = 0;
	bool   oom = false;

	for (;;) {
		int c = rd_getc(fd);
		if (c < 0) return false;
		if (c == '\n') break;

		/* ⚠ ON A FAILED GROW, KEEP DRAINING. Returning here would leave
		 * the tail of this line in the socket for the next read to take
		 * as a message — the exact failure this replaced. */
		if (!oom && o + 1 >= rd_cap) {
			size_t want = rd_cap ? rd_cap * 2 : 16384;
			char  *p    = realloc(rd_line, want);
			if (p) { rd_line = p; rd_cap = want; }
			else   { oom = true; }
		}
		if (!oom) rd_line[o++] = (char)c;
	}
	if (oom) return false;

	rd_line[o] = '\0';
	*out = rd_line;
	return true;
}

static bool cmd_core(int fd, const char *json_args, char *reply, size_t cap,
                     char **full)
{
	static int next_id = 1;
	int id = next_id++;

	char out[8192];
	int n = snprintf(out, sizeof out,
	                 "{\"command\":[%s],\"request_id\":%d}\n", json_args, id);
	if (n <= 0 || (size_t)n >= sizeof out) return false;
	if (!write_all(fd, out, (size_t)n)) return false;

	/*
	 * ⛔ MATCHED ON request_id, NOT "the next line". Property changes and
	 * playback events arrive on this socket unasked and in between — so
	 * taking the next line as the answer works right up until something is
	 * actually playing, which is every time it matters.
	 */
	for (int guard = 0; guard < 512; guard++) {
		char *line;
		if (!read_line(fd, &line)) return false;
		double got;
		if (!sp_json_num(line, "request_id", &got)) continue;   /* an event */
		if ((int)got != id) continue;                            /* not ours */

		char err[64] = "";
		if (sp_json_str(line, "error", err, sizeof err) && strcmp(err, "success"))
			return false;
		if (full)  *full = line;
		if (reply) snprintf(reply, cap, "%s", line);
		return true;
	}
	return false;
}

bool sp_cmd(int fd, const char *json_args, char *reply, size_t cap)
{
	return cmd_core(fd, json_args, reply, cap, NULL);
}

/*
 * ⛔ FOR THE ONE REPLY THAT HAS NO SENSIBLE FIXED SIZE. sp_cmd() copies into
 * the caller's buffer, which is right for `path`, `volume` and the rest — they
 * are bounded and a caller that names 512 bytes means it. The PLAYLIST is
 * bounded only by how much music somebody owns, and the last buffer named for
 * it (256 KB) was a guess that a library walks straight past.
 *
 * ⚠ Valid until the next command on this connection.
 */
const char *sp_cmd_full(int fd, const char *json_args)
{
	char *line = NULL;
	if (!cmd_core(fd, json_args, NULL, 0, &line)) return NULL;
	return line;
}

/* ── properties ─────────────────────────────────────────────────────────── */

bool sp_get_str(int fd, const char *prop, char *out, size_t cap)
{
	char args[256], reply[65536];
	snprintf(args, sizeof args, "\"get_property\",\"%s\"", prop);
	if (!sp_cmd(fd, args, reply, sizeof reply)) return false;
	return sp_json_str(reply, "data", out, cap);
}

bool sp_get_num(int fd, const char *prop, double *out)
{
	char args[256], reply[4096];
	snprintf(args, sizeof args, "\"get_property\",\"%s\"", prop);
	if (!sp_cmd(fd, args, reply, sizeof reply)) return false;
	return sp_json_num(reply, "data", out);
}

bool sp_get_bool(int fd, const char *prop, bool *out)
{
	char args[256], reply[4096];
	snprintf(args, sizeof args, "\"get_property\",\"%s\"", prop);
	if (!sp_cmd(fd, args, reply, sizeof reply)) return false;
	return sp_json_bool(reply, "data", out);
}

bool sp_set_bool(int fd, const char *prop, bool v)
{
	char args[256];
	snprintf(args, sizeof args, "\"set_property\",\"%s\",%s",
	         prop, v ? "true" : "false");
	return sp_cmd(fd, args, NULL, 0);
}

void sp_now_title(int fd, const char *path, char *out, size_t cap)
{
	char meta[512] = "";
	bool has = sp_get_str(fd, "media-title", meta, sizeof meta) && meta[0];

	if (has && path && *path) {
		/* mpv's fallback is the BASENAME, so that is what it is compared
		 * against — comparing the whole path would never match and the
		 * fallback would always be taken for a real title. */
		const char *base = strrchr(path, '/');
		base = base ? base + 1 : path;
		if (!strcmp(meta, base)) has = false;
	}

	if (has) snprintf(out, cap, "%s", meta);
	else     sp_pretty_title(path ? path : "", out, cap);
}
