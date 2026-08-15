/* pty.c — the pseudo-terminal, and nothing else.
 *
 * There is no shell anywhere in this file. The argv the caller passes is exec'd
 * directly, so a command with a space, a quote or a semicolon in it is a
 * command with those characters in it and not three commands. This is the same
 * rule the rest of the suite follows for the same reason, and a terminal is the
 * one program where getting it wrong is hardest to notice: it spends its life
 * handing arbitrary bytes to a child.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

bool st_pty_spawn_env(st_pty_t *p, char *const argv[], uint16_t cols,
                      uint16_t rows, const char *const env[])
{
	struct winsize ws = {
		.ws_row = rows, .ws_col = cols, .ws_xpixel = 0, .ws_ypixel = 0
	};

	int fd = -1;
	pid_t pid = forkpty(&fd, NULL, NULL, &ws);
	if (pid < 0)
		return false;

	if (pid == 0) {
		/* A fresh session with the pty as controlling terminal is what makes
		 * job control work in the child; without it every shell started here
		 * prints "no job control" and Ctrl-C goes to the wrong process. */
		setenv("TERM", "xterm-256color", 1);

		/* ⚠ WITHOUT THIS, A PROGRAM QUANTISES ITS COLOURS. The parser has
		 * understood `ESC[38;2;r;g;b` since stage 1, but a child has no way to
		 * find that out: TERM says 256 and the terminfo entry for
		 * xterm-256color has no truecolor capability, so every library that
		 * cares (chalk, ansi-styles, rich, tcell) falls back to the 6x6x6 cube
		 * and picks a NEIGHBOUR of the colour it meant. Measured: Claude Code
		 * sends `38;5;153` without it and `38;2;177;185;249` with it — the
		 * second is the colour it actually chose. COLORTERM is the de-facto
		 * flag every one of those libraries reads. */
		setenv("COLORTERM", "truecolor", 1);

		/* NAME=VALUE, split here rather than handed to putenv — putenv keeps
		 * the caller's pointer in the environment, and the callers here build
		 * these on the stack. The exec is immediate, so it would work; it would
		 * work by luck. */
		for (int i = 0; env && env[i]; i++) {
			const char *eq = strchr(env[i], '=');
			if (!eq)
				continue;
			char nm[64];
			size_t n = (size_t)(eq - env[i]);
			if (n == 0 || n >= sizeof nm)
				continue;
			memcpy(nm, env[i], n);
			nm[n] = 0;
			setenv(nm, eq + 1, 1);
		}

		unsetenv("COLUMNS");
		unsetenv("LINES");
		signal(SIGPIPE, SIG_DFL);
		execvp(argv[0], argv);
		_exit(127);
	}

	p->fd = fd;
	p->pid = pid;
	return true;
}

bool st_pty_spawn(st_pty_t *p, char *const argv[], uint16_t cols, uint16_t rows)
{
	return st_pty_spawn_env(p, argv, cols, rows, NULL);
}

/* ⚠ Explicit, and NOT the default, because the two readers want opposite
 * things. st_pty_pump below blocks until the child is done and reads to EIO;
 * making the fd non-blocking there would turn the first empty read into a
 * spurious end-of-session. The window's loop is the other way round: it polls
 * two descriptors and must drain this one until it is empty without ever
 * blocking, because blocking means not answering the compositor. */
void st_pty_set_nonblocking(st_pty_t *p)
{
	int fl = fcntl(p->fd, F_GETFL, 0);
	if (fl >= 0)
		fcntl(p->fd, F_SETFL, fl | O_NONBLOCK);
}

void st_pty_resize(st_pty_t *p, uint16_t cols, uint16_t rows)
{
	struct winsize ws = {
		.ws_row = rows, .ws_col = cols, .ws_xpixel = 0, .ws_ypixel = 0
	};
	ioctl(p->fd, TIOCSWINSZ, &ws);
	/* SIGWINCH is the kernel's job on TIOCSWINSZ; sending it here as well is
	 * how a program gets two resizes and redraws twice. */
}

/* Hang the child up and collect its status.
 *
 * Closing the master end is what sends SIGHUP down the pty, which is what
 * closing a terminal window is SUPPOSED to do — the child gets the same signal
 * it would get from any other terminal going away, rather than being killed
 * with something it cannot handle.
 *
 * ⚠ The status is the WINDOW's status. A terminal that always exits 0 breaks
 * every script that runs one, and the failure is silent: the script carries on
 * as though the command it wrapped had succeeded. */
int st_pty_reap(st_pty_t *p)
{
	if (p->fd >= 0) {
		close(p->fd);
		p->fd = -1;
	}
	int status = 0;
	while (waitpid(p->pid, &status, 0) < 0) {
		if (errno == EINTR)
			continue;
		return 0;      /* already reaped, or never ours */
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int st_pty_pump(st_pty_t *p, st_vt_t *vt)
{
	/* 256 KB, which is the read size the design argues for: the parser's fast
	 * path only pays off on long runs, and a 4 KB read hands it eight lines at
	 * a time. The buffer is on the heap because 256 KB of stack in a program
	 * that may one day run a thread per window is a stack overflow waiting for
	 * a reason. */
	enum { BUFSZ = 256 * 1024 };
	uint8_t *buf = xmalloc(BUFSZ);

	for (;;) {
		ssize_t n = read(p->fd, buf, BUFSZ);
		if (n > 0) {
			st_vt_feed(vt, buf, (size_t)n);
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		/* EIO is how a pty reports that the child closed the other end. It
		 * is the normal end of a session, not an error, and treating it as
		 * one prints a spurious failure at the end of every run. */
		break;
	}
	free(buf);

	int status = 0;
	while (waitpid(p->pid, &status, 0) < 0 && errno == EINTR)
		;
	close(p->fd);
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
