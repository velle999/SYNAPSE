/* util.c — output, allocation and the two string shapes everything else uses.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synclean.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

out_mode_t g_out = OUT_HUMAN;
bool g_dry = false;
bool g_yes = false;

/* ⚠ THE "syn-clean: " PREFIX IS FOR A TERMINAL, so --rec drops it. It says
 * which program in a pipeline spoke; a front end already knows what it ran and
 * puts this text straight in front of somebody. The same rule syn-vault
 * follows, for the same reason. */
static void say_who(void)
{
	if (g_out != OUT_REC) fputs("syn-clean: ", stderr);
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
	char *p = NULL;
	va_start(ap, fmt);
	if (vasprintf(&p, fmt, ap) < 0) p = NULL;
	va_end(ap);
	if (!p) die("out of memory");
	return p;
}

/* ⛔ EVERY FIELD IN A --rec ROW IS PERCENT-ENCODED, because the fields are
 * tab-separated and a FILENAME MAY CONTAIN A TAB. It may also contain a
 * newline, which would end the record early and hand the front end a row it
 * reads as two. Encoding is not decoration here; it is what makes the format
 * parseable at all. */
char *pct_encode(const char *s)
{
	static const char *hex = "0123456789ABCDEF";
	size_t n = strlen(s);
	char *out = malloc(n * 3 + 1);
	if (!out) die("out of memory");
	char *o = out;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9') || strchr("-_.~/ ", *p)) {
			*o++ = (char)*p;
		} else {
			*o++ = '%'; *o++ = hex[*p >> 4]; *o++ = hex[*p & 15];
		}
	}
	*o = '\0';
	return out;
}

/* ⚠ SYNCLEAN_HOME EXISTS FOR THE TESTS AND FOR NOTHING ELSE. A suite that
 * cleaned the caches of whoever ran it would be a suite nobody could run
 * twice — and one bad path in this program deletes a home directory. */
char *home_path(const char *rel)
{
	const char *h = getenv("SYNCLEAN_HOME");
	if (!h || !*h) h = getenv("HOME");
	if (!h || !*h) die("no HOME set");
	if (!rel || !*rel) return xstrdup(h);
	return xasprintf("%s/%s", h, rel);
}

void human_size(unsigned long long b, char *out, size_t n)
{
	static const char *u[] = { "B", "KB", "MB", "GB", "TB" };
	int i = 0;
	double v = (double)b;
	while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
	if (i == 0) snprintf(out, n, "%llu B", b);
	else        snprintf(out, n, "%.1f %s", v, u[i]);
}
