/* util.c — printing, and the two ways this program talks.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synscan.h"
#include "i18n.h"

#include <locale.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Resolved once; every caller shares the answer so a scan and the quarantine
 * it writes can never disagree about where "here" is. */
static char *g_state = NULL;
static char *g_quar  = NULL;

const char *state_dir(void)
{
	if (g_state) return g_state;

	const char *env = getenv("SYNSCAN_HOME");
	if (env && *env) {
		g_state = strdup(env);
	} else if (geteuid() == 0) {
		g_state = strdup(SYNSCAN_STATEDIR);
	} else {
		const char *xdg = getenv("XDG_DATA_HOME");
		const char *home = getenv("HOME");
		if (xdg && *xdg) {
			if (asprintf(&g_state, "%s/syn-scan", xdg) < 0) g_state = NULL;
		} else if (home && *home) {
			if (asprintf(&g_state, "%s/.local/share/syn-scan", home) < 0) g_state = NULL;
		}
		/* ⚠ No HOME and not root — a unit with an empty environment. Fall
		 * back to the system path and let the write fail loudly there rather
		 * than composing a path from an empty string, which is "/syn-scan". */
		if (!g_state) g_state = strdup(SYNSCAN_STATEDIR);
	}
	if (!g_state) die("out of memory");
	return g_state;
}

const char *quarantine_dir(void)
{
	if (!g_quar && asprintf(&g_quar, "%s/quarantine", state_dir()) < 0)
		die("out of memory");
	return g_quar;
}

out_mode_t g_out   = OUT_HUMAN;
bool       g_yes   = false;
bool       g_dry   = false;
bool       g_quiet = false;
bool       g_will_quarantine = false;

void syn_scan_i18n_init(void)
{
	setlocale(LC_ALL, "");
	bindtextdomain(SYN_SCAN_GETTEXT_DOMAIN, SYNSCAN_LOCALEDIR);
	textdomain(SYN_SCAN_GETTEXT_DOMAIN);
}

/* ⚠ Diagnostics go to stderr even in --rec, so a front end reading stdout
 * gets records and only records. A warning on stdout is a parse error in the
 * window. */
void info(const char *fmt, ...)
{
	if (g_quiet || g_out == OUT_REC) return;
	va_list ap; va_start(ap, fmt);
	vfprintf(stdout, fmt, ap); fputc('\n', stdout);
	va_end(ap);
}

void warn(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	fputs("syn-scan: ", stderr);
	vfprintf(stderr, fmt, ap); fputc('\n', stderr);
	va_end(ap);
}

void die(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	fputs("syn-scan: ", stderr);
	vfprintf(stderr, fmt, ap); fputc('\n', stderr);
	va_end(ap);
	exit(2);
}
