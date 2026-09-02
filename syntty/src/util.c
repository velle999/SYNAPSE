/* util.c — allocation that cannot half-fail, and the one clock.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"
#include "i18n.h"
#include "config.h"

#include <locale.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* die() without the exit — the "syntty: " prefix, the newline, and back to
 * work. Everything this program prints with printf or fprintf is a RECORD that
 * tests/syntty_test.sh or somebody's script parses; everything a PERSON reads
 * goes through die() or warn().
 *
 * ⚠ THAT IS WHY THIS FUNCTION EXISTS AT ALL rather than six fprintf(stderr,
 * "syntty: …") calls spelling the prefix out. The rule "a `_()` appears only
 * inside die() or warn()" is checkable in one grep; "a `_()` appears only in
 * the fprintf calls that are for a person" is not checkable by anything.
 * tests/i18n_test.sh enforces it.  */
void warn(const char *fmt, ...)
{
	va_list ap;
	fputs("syntty: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void die(const char *fmt, ...)
{
	va_list ap;
	fputs("syntty: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

/* Out of memory is not a condition this program recovers from: every caller
 * would have to unwind a half-built grid, and the recovery path would be the
 * least tested code in the file. It dies, loudly, at the point of failure. */
void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p)
		die(_("out of memory (%zu bytes)"), n);
	return p;
}

void *xcalloc(size_t n, size_t size)
{
	void *p = calloc(n ? n : 1, size ? size : 1);
	if (!p)
		die(_("out of memory (%zu x %zu bytes)"), n, size);
	return p;
}

void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q)
		die(_("out of memory (%zu bytes)"), n);
	return q;
}

char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = xmalloc(n);
	memcpy(p, s, n);
	return p;
}

/* printf into a fresh allocation. Same contract as the rest of this file: it
 * either returns a string or it does not return. */
char *xasprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0)
		die(_("xasprintf: cannot format"));

	char *p = xmalloc((size_t)n + 1);
	va_start(ap, fmt);
	vsnprintf(p, (size_t)n + 1, fmt, ap);
	va_end(ap);
	return p;
}

uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/*
 * Bind the message catalog. Called once from main() before anything prints.
 *
 * ⛔ THE ENV OVERRIDE IS WHAT MAKES THIS TESTABLE. The compiled-in path is under
 * the install prefix, so an UNINSTALLED binary finds no catalog at all and
 * answers English in every locale — a test that runs it under two locales and
 * diffs would then pass on a real bug. synpkg shipped a release verified
 * exactly that way. Nothing changes for an installed syntty; the variable is
 * not set.
 */
void syntty_i18n_init(void)
{
	setlocale(LC_ALL, "");
	const char *dir = getenv("SYNTTY_LOCALEDIR");
	bindtextdomain(SYNTTY_GETTEXT_DOMAIN, dir && *dir ? dir : SYNTTY_LOCALEDIR);
	bind_textdomain_codeset(SYNTTY_GETTEXT_DOMAIN, "UTF-8");
	textdomain(SYNTTY_GETTEXT_DOMAIN);
}
