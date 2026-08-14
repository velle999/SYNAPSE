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

bool st_pty_spawn(st_pty_t *p, char *const argv[], uint16_t cols, uint16_t rows)
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

void st_pty_resize(st_pty_t *p, uint16_t cols, uint16_t rows)
{
	struct winsize ws = {
		.ws_row = rows, .ws_col = cols, .ws_xpixel = 0, .ws_ypixel = 0
	};
	ioctl(p->fd, TIOCSWINSZ, &ws);
	/* SIGWINCH is the kernel's job on TIOCSWINSZ; sending it here as well is
	 * how a program gets two resizes and redraws twice. */
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
