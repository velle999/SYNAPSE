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

/* What the last scan of each kind found, in the `finding` record format.
 * Two files because the weekly sweep is two runs — the system checks, then
 * the files — and one list would hold only whichever ran second. */
static char *findings_file(bool system)
{
	char *p = NULL;
	if (asprintf(&p, "%s/findings-%s", state_dir(), system ? "system" : "files") < 0)
		die("out of memory");
	return p;
}

/* How many rows in a saved list carry one of `verdicts` (a NULL-terminated
 * list of verdict ids). */
static size_t saved_count(bool system, const char *const *verdicts)
{
	char *path = findings_file(system);
	FILE *r = fopen(path, "r");
	free(path);
	if (!r) return 0;
	size_t n = 0;
	char *line = NULL; size_t cap = 0;
	while (getline(&line, &cap, r) > 0) {
		char *t1 = strchr(line, '\t');                 /* after "finding" */
		char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;   /* after the engine */
		if (!t2) continue;
		for (const char *const *v = verdicts; *v; v++) {
			size_t len = strlen(*v);
			if (!strncmp(t2 + 1, *v, len) && t2[1 + len] == '\t') { n++; break; }
		}
	}
	free(line);
	fclose(r);
	return n;
}

/* The rows that need a person: infected or suspect, the same rule as
 * findings_outstanding(). */
static size_t saved_outstanding(bool system)
{
	static const char *const counted[] = { "infected", "suspect", NULL };
	return saved_count(system, counted);
}

static size_t saved_unread(bool system)
{
	static const char *const error[] = { "error", NULL };
	return saved_count(system, error);
}

static bool saved_list_exists(void)
{
	bool any = false;
	for (int sys = 0; sys < 2 && !any; sys++) {
		char *path = findings_file(sys);
		any = access(path, F_OK) == 0;
		free(path);
	}
	return any;
}

void status_record(const findings_t *f, time_t started, bool system)
{
	if (mkdir(state_dir(), 0750) != 0 && errno != EEXIST) {
		/* Not fatal: an unprivileged on-demand scan has nowhere to write and
		 * still has everything to report. */
		return;
	}

	/* The list first, so last-scan's count is read from what was just
	 * written. World-readable like last-scan: Settings and the window read
	 * the weekly sweep's record as the user. */
	char *path = findings_file(system);
	char *tmp = NULL;
	if (asprintf(&tmp, "%s.tmp", path) < 0) die("out of memory");
	FILE *w = fopen(tmp, "w");
	if (w) {
		fchmod(fileno(w), 0644);
		findings_write_rec(w, f);
		if (fclose(w) == 0) rename(tmp, path);
		else unlink(tmp);
	}
	free(tmp);
	free(path);

	size_t bad = saved_outstanding(true) + saved_outstanding(false);
	w = fopen(state_file(), "w");
	if (!w) return;
	fprintf(w, "started=%lld\nfinished=%lld\nfindings=%zu\n",
	        (long long)started, (long long)time(NULL), bad);
	fclose(w);
}

/* The saved rows, straight through (they are already records). */
static void cat_findings(bool system, FILE *out)
{
	char *path = findings_file(system);
	FILE *r = fopen(path, "r");
	free(path);
	if (!r) return;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, r)) > 0) fwrite(buf, 1, n, out);
	fclose(r);
}

/* Undo put_field's escaping, in place: \t, \n, \\ . */
static void unescape(char *s)
{
	char *w = s;
	for (char *p = s; *p; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			*w++ = *p == 't' ? '\t' : *p == 'n' ? '\n' : *p;
		} else {
			*w++ = *p;
		}
	}
	*w = '\0';
}

/* The saved findings, read back for a person. `unread` false: the rows that
 * need a look, and a line for each engine that did not finish. `unread` true:
 * the files an engine could not read, which are listed apart and not counted.
 * Returns how many rows were shown. */
static size_t show_findings(bool system, bool unread, size_t *incomplete)
{
	char *path = findings_file(system);
	FILE *r = fopen(path, "r");
	free(path);
	if (!r) return 0;
	size_t shown = 0;
	char *line = NULL; size_t cap = 0;
	while (getline(&line, &cap, r) > 0) {
		line[strcspn(line, "\n")] = '\0';
		char *f[6] = { 0 };
		int nf = 0;
		for (char *save = NULL, *t = strtok_r(line, "\t", &save); t && nf < 6;
		     t = strtok_r(NULL, "\t", &save))
			f[nf++] = t;
		if (nf < 5) continue;
		for (int i = 1; i < 5; i++) unescape(f[i]);
		if (!strcmp(f[2], "clean")) continue;
		if (!strcmp(f[2], "incomplete")) {
			if (!unread) {
				warn(_("%s did not finish: %s"), f[1], f[4]);
				(*incomplete)++;
			}
			continue;
		}
		verdict_t v = !strcmp(f[2], "infected") ? VERDICT_INFECTED
		            : !strcmp(f[2], "suspect")  ? VERDICT_SUSPECT : VERDICT_ERROR;
		if ((v == VERDICT_ERROR) != unread) continue;
		printf("  %-10s %-11s %s\n", f[1], _(verdict_label(v)), f[3]);
		if (*f[4]) printf("  %-10s %-11s   %s\n", "", "", f[4]);
		shown++;
	}
	free(line);
	fclose(r);
	return shown;
}

int status_show(bool weekly)
{
	if (weekly) state_dir_use_system();

	FILE *r = fopen(state_file(), "r");
	if (!r) {
		if (g_out == OUT_REC) puts("#status\tstate\nstatus\tnever");
		else info("%s", weekly ? _("The weekly scan has not run yet.")
		                       : _("No scan has run yet."));
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

	/* ⚠ COUNTED FROM THE LIST WHEN THERE IS ONE, by today's rule. A record
	 * written before 0.1.0-6 counted what an engine could not read, and
	 * would go on saying so — here and in Settings — until the next sweep. */
	if (saved_list_exists())
		bad = saved_outstanding(true) + saved_outstanding(false);

	if (g_out == OUT_REC) {
		puts("#status\tstate\tfinished\tfindings");
		printf("status\tran\t%lld\t%lu\n", finished, bad);
		/* ⛔ Column names are the protocol — the same as a scan's rows. */
		puts("#finding\tengine\tverdict\tpath\tdetail\twhen");
		cat_findings(true, stdout);
		cat_findings(false, stdout);
		return 0;
	}

	char when[64] = "";
	time_t t = (time_t)finished;
	struct tm tm;
	if (localtime_r(&t, &tm)) strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm);

	info(weekly ? _("Weekly scan: %s") : _("Last scan: %s"), when);
	size_t incomplete = 0;
	size_t shown = show_findings(true, false, &incomplete)
	             + show_findings(false, false, &incomplete);
	size_t unread = saved_unread(true) + saved_unread(false);
	if (unread) {
		printf(P_("\n%zu file could not be scanned:\n",
		          "\n%zu files could not be scanned:\n", unread), unread);
		show_findings(true, true, &incomplete);
		show_findings(false, true, &incomplete);
	}
	if (bad == 0) info("%s", _("Nothing outstanding."));
	else if (shown == 0)
		/* A record from before the list was kept: the count, and no way to
		 * say what it was. */
		printf(P_("%lu finding needs a look, but this record does not say what — "
		          "run the scan again to see it.\n",
		          "%lu findings need a look, but this record does not say what — "
		          "run the scan again to see them.\n", bad), bad);
	else
		printf(P_("\n%lu thing needs a look.\n",
		          "\n%lu things need a look.\n", bad), bad);
	return bad ? 1 : 0;
}
