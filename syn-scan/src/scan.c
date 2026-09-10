/* scan.c — running the engines that are actually here, and remembering it.
 *
 * ⚠ AN ABSENT ENGINE IS A NORMAL STATE, NOT AN ERROR. chkrootkit lives in
 * [blackarch], which syn-install offers as a choice (WANT_BLACKARCH) rather
 * than a guarantee, so a perfectly ordinary SynapseOS machine has two engines
 * and not three. It is reported as absent and skipped. A scanner that refuses
 * to run because one of its three back ends is missing is a scanner that never
 * runs.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synscan.h"
#include "i18n.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int scan_run(const scan_req_t *req, findings_t *out)
{
	size_t n; const engine_t *all = engines_all(&n);
	size_t ran = 0;

	for (size_t i = 0; i < n; i++) {
		const engine_t *e = &all[i];

		if (req->only && strcmp(req->only, e->id)) continue;

		/* A system engine has nothing to say about a path list, and a file
		 * engine has nothing to scan without one. */
		if (req->system && !(e->caps & ENGINE_SYSTEM)) continue;
		if (!req->system && !(e->caps & ENGINE_FILES))  continue;

		if (!engine_present(e)) {
			if (req->only)
				warn(_("%s is not installed"), _(e->name));
			else
				info(_("  %-12s not installed, skipped"), e->id);
			continue;
		}

		/* ⚠ INSTALLED BUT NOT OURS TO RUN. Arch's rkhunter is 0700 root:root.
		 * Saying "not installed" here would be false; saying nothing would
		 * report a partial scan as a whole one. */
		if (!engine_runnable(e)) {
			if (req->only)
				warn(_("%s needs root — try: sudo syn-scan ..."), _(e->name));
			else
				info(_("  %-12s installed, but needs root — skipped"), e->id);
			continue;
		}

		info(_("  %-12s running..."), e->id);
		if (g_dry) { ran++; continue; }

		if (e->run(e, req->paths, out) != 0)
			warn(_("%s could not be run"), _(e->name));
		else
			ran++;
	}

	if (ran == 0) {
		warn("%s", _("no engine ran — install clamav, or pass --only for the "
		             "one you meant"));
		return -1;
	}
	return 0;
}

/* ── what ran, and when ─────────────────────────────────────────────────────
 *
 * ⚠ A WEEKLY TIMER WHOSE RESULT LIVES ONLY IN THE JOURNAL IS A TIMER NOBODY
 * READS. This file is what `syn-scan status` reports and what the bar badge
 * asks, so the scheduled sweep can surface where the user already looks.
 */
static const char *state_file(void)
{
	static char *p = NULL;
	if (!p && asprintf(&p, "%s/last-scan", state_dir()) < 0) die("out of memory");
	return p;
}

void status_record(const findings_t *f, time_t started)
{
	size_t bad = 0;
	for (const finding_t *p = f->head; p; p = p->next)
		if (p->verdict != VERDICT_CLEAN) bad++;

	if (mkdir(state_dir(), 0750) != 0 && errno != EEXIST) {
		/* Not fatal: an unprivileged on-demand scan has nowhere to write and
		 * still has everything to report. */
		return;
	}

	FILE *w = fopen(state_file(), "w");
	if (!w) return;
	fprintf(w, "started=%lld\nfinished=%lld\nfindings=%zu\n",
	        (long long)started, (long long)time(NULL), bad);
	fclose(w);
}

int status_show(void)
{
	FILE *r = fopen(state_file(), "r");
	if (!r) {
		if (g_out == OUT_REC) puts("#status\tstate\nstatus\tnever");
		else info("%s", _("No scan has run yet."));
		return 0;
	}

	long long started = 0, finished = 0; unsigned long bad = 0;
	char line[128];
	while (fgets(line, sizeof line, r)) {
		sscanf(line, "started=%lld",  &started);
		sscanf(line, "finished=%lld", &finished);
		sscanf(line, "findings=%lu",  &bad);
	}
	fclose(r);

	if (g_out == OUT_REC) {
		puts("#status\tstate\tfinished\tfindings");
		printf("status\tran\t%lld\t%lu\n", finished, bad);
		return 0;
	}

	char when[64] = "";
	time_t t = (time_t)finished;
	struct tm tm;
	if (localtime_r(&t, &tm)) strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm);

	info(_("Last scan: %s"), when);
	if (bad == 0) info("%s", _("Nothing outstanding."));
	else          printf(P_("%lu finding needs a look. `syn-scan status --rec` for the list.\n",
	                        "%lu findings need a look. `syn-scan status --rec` for the list.\n",
	                        bad), bad);
	return bad ? 1 : 0;
}
