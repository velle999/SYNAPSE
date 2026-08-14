/* util.c — allocation that cannot half-fail, and the one clock.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
		die("out of memory (%zu bytes)", n);
	return p;
}

void *xcalloc(size_t n, size_t size)
{
	void *p = calloc(n ? n : 1, size ? size : 1);
	if (!p)
		die("out of memory (%zu x %zu bytes)", n, size);
	return p;
}

void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q)
		die("out of memory (%zu bytes)", n);
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
		die("xasprintf: cannot format");

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
