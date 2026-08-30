/* util.c — output, allocation, and the password prompt.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synvault.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

out_mode_t g_out = OUT_HUMAN;

/*
 * ⚠ THE "syn-vault: " PREFIX IS FOR A TERMINAL, so --rec drops it. That prefix
 * exists to say which program in a pipeline spoke; a front end already knows
 * what it ran and puts this text straight in front of somebody, where the
 * program's own name is noise in a sentence about their password.
 */
static void say_who(void)
{
	if (g_out != OUT_REC) fputs("syn-vault: ", stderr);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	say_who();
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void die(const char *fmt, ...)
{
	va_list ap;
	say_who();
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void rec_header(const char *fields)
{
	if (g_out == OUT_REC) printf("%s\n", fields);
}

void rec_row(const char *fmt, ...)
{
	va_list ap;
	if (g_out != OUT_REC) return;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

char *xstrdup(const char *s)
{
	char *p = strdup(s ? s : "");
	if (!p) die("out of memory");
	return p;
}

char *xasprintf(const char *fmt, ...)
{
	va_list ap;
	char *out = NULL;
	va_start(ap, fmt);
	int n = vasprintf(&out, fmt, ap);
	va_end(ap);
	if (n < 0 || !out) die("out of memory");
	return out;
}

/* Percent-encoding for the record format, so a vault called "Tax 2024" is one
 * field rather than two. */
char *pct_encode(const char *s)
{
	static const char *hex = "0123456789ABCDEF";
	size_t n = strlen(s ? s : "");
	char *out = malloc(n * 3 + 1);
	if (!out) die("out of memory");
	char *w = out;
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
		    *p == '.' || *p == '~' || *p == '/') {
			*w++ = (char)*p;
		} else {
			*w++ = '%';
			*w++ = hex[*p >> 4];
			*w++ = hex[*p & 0xF];
		}
	}
	*w = '\0';
	return out;
}

/* ── the password ───────────────────────────────────────────────────────── */

/*
 * ⛔ NEVER FROM argv. /proc/<pid>/cmdline is world-readable, so a password on a
 * command line is visible to every process on the machine for as long as this
 * one runs — and to anything reading the shell history afterwards. There is no
 * --password option here and there must not be one.
 *
 * ⚠ AND NOT FROM THE ENVIRONMENT EITHER, for a vault. /proc/<pid>/environ is
 * only readable by the same user, which is enough for a password that protects
 * that user's own service account — but a vault's password protects files from
 * somebody who has the user's session, which is exactly the reader that can
 * see it there.
 *
 * So: the terminal with echo off, or stdin when stdin is a pipe. The window
 * uses the pipe.
 */
char *password_read(const char *prompt)
{
	if (!isatty(STDIN_FILENO)) {
		/* A pipe. One line, and the newline is not part of it. */
		char *line = NULL;
		size_t cap = 0;
		ssize_t n = getline(&line, &cap, stdin);
		if (n <= 0) { free(line); return NULL; }
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
		return line;
	}

	struct termios old, quiet;
	if (tcgetattr(STDIN_FILENO, &old) != 0) return NULL;
	quiet = old;
	quiet.c_lflag &= (tcflag_t)~ECHO;

	fputs(prompt, stderr);
	fflush(stderr);

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet) != 0) return NULL;

	char *line = NULL;
	size_t cap = 0;
	ssize_t n = getline(&line, &cap, stdin);

	/* ⛔ RESTORED BEFORE ANYTHING ELSE CAN FAIL. A program that exits with echo
	 * off leaves the person typing into a terminal that shows nothing, and the
	 * fix (`reset`) is not obvious to somebody who has just lost their prompt. */
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
	fputc('\n', stderr);

	if (n <= 0) { free(line); return NULL; }
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
	return line;
}
