/* main.c — syn-scan's command line, and the window it can open.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synscan.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SYNSCAN_DATADIR
#define SYNSCAN_DATADIR "/usr/share/syn-scan"
#endif

static void usage(FILE *f)
{
	fprintf(f,
"syn-scan — scan files at rest for malware, and put what turns up aside\n"
"\n"
"  syn-scan scan <path...>        look inside these files\n"
"  syn-scan scan --system         the rootkit and system checks (root)\n"
"  syn-scan status                what ran, when, and what is outstanding\n"
"  syn-scan engines               which back ends are installed\n"
"  syn-scan quarantine list       what has been put aside\n"
"  syn-scan quarantine take <path>\n"
"  syn-scan quarantine restore <id>\n"
"  syn-scan quarantine purge <id>\n"
"  syn-scan gui                   the window\n"
"\n"
"  --only <engine>   clamav, rkhunter or chkrootkit — one of them\n"
"  --dry-run         say what would happen, change nothing\n"
"  --yes             do not ask\n"
"  --quiet           findings only\n"
"  --rec             one record per line, for a front end\n"
"  --version         print the version\n"
"\n"
"WHAT THIS DOES NOT DO. It does not watch the system in real time — synguard\n"
"already owns that path, in the kernel, and a second watcher on the same hooks\n"
"is one nobody is monitoring. syn-scan looks at files sitting on disk, which is\n"
"the half synguard cannot see: it decides on behaviour, never on what is inside\n"
"a file. A Windows .exe in a Proton prefix is data to Linux and invisible to it.\n"
"\n"
"AND IT NEVER DELETES ANYTHING. A finding is reported, or moved to quarantine\n"
"with a record of where it came from so it can be put back. chkrootkit in\n"
"particular warns about ordinary desktops; a false positive that ate a save\n"
"game would be worse than what it was looking for.\n");
}

static bool confirm(const char *what)
{
	if (g_yes || g_dry) return true;
	/* ⚠ A front end pipes; a pipe has nobody to answer, so --rec without --yes
	 * is refused rather than silently taken as consent. */
	if (!isatty(STDIN_FILENO)) {
		warn("%s", _("nothing to ask on a pipe — pass --yes if you mean it"));
		return false;
	}
	/* ⚠ THE [y/N] AND THE LETTER READ HERE STAY ENGLISH. A translated prompt
	 * whose accepted key was not translated with it is a prompt nobody can
	 * say yes to. */
	printf("%s [y/N] ", what);
	fflush(stdout);
	int c = getchar();
	return c == 'y' || c == 'Y';
}

static int cmd_engines(void)
{
	size_t n; const engine_t *all = engines_all(&n);

	if (g_out == OUT_REC) puts("#engine\tid\tname\tpresent\trunnable\tpath");

	for (size_t i = 0; i < n; i++) {
		char *p = engine_path(&all[i]);
		bool runnable = p && engine_runnable(&all[i]);

		if (g_out == OUT_REC) {
			printf("engine\t%s\t%s\t%d\t%d\t%s\n", all[i].id, all[i].name,
			       p ? 1 : 0, runnable ? 1 : 0, p ? p : "");
		} else {
			const char *state = !p       ? _("not installed")
			                  : !runnable ? _("installed — needs root")
			                              : p;
			printf("  %-12s %-18s %s\n", all[i].id, _(all[i].name), state);
		}
		free(p);
	}
	return 0;
}

static int cmd_gui(void)
{
	/* ⚠ The window is quickshell reading data/syn-scan.qml, which shells back
	 * to this same binary with --rec. One implementation, three faces. */
	execlp("quickshell", "quickshell", "-p", SYNSCAN_DATADIR "/syn-scan.qml", (char *)NULL);
	die(_("quickshell is not installed — the window needs it"));
	return 2;
}

int main(int argc, char **argv)
{
	syn_scan_i18n_init();

	const char *only = NULL;
	bool want_system = false, want_quarantine = false;

	/* Options first so they can appear anywhere. */
	int w = 1;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if      (!strcmp(a, "--rec"))       g_out = OUT_REC;
		else if (!strcmp(a, "--dry-run"))   g_dry = true;
		else if (!strcmp(a, "--yes"))       g_yes = true;
		else if (!strcmp(a, "--quiet"))     g_quiet = true;
		else if (!strcmp(a, "--system"))    want_system = true;
		else if (!strcmp(a, "--quarantine")) want_quarantine = g_will_quarantine = true;
		else if (!strcmp(a, "--only")) {
			if (++i >= argc) die(_("--only needs an engine name"));
			only = argv[i];
			if (!engine_by_id(only)) die(_("no engine called '%s'"), only);
		}
		else if (!strcmp(a, "--version")) { puts(SYNSCAN_VERSION); return 0; }
		else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(stdout); return 0; }
		else argv[w++] = argv[i];
	}
	argv[w] = NULL;
	argc = w;

	if (argc < 2) { usage(stderr); return 2; }

	const char *cmd = argv[1];

	if (!strcmp(cmd, "engines"))  return cmd_engines();
	if (!strcmp(cmd, "status"))   return status_show();
	if (!strcmp(cmd, "gui"))      return cmd_gui();

	if (!strcmp(cmd, "quarantine")) {
		if (argc < 3) { usage(stderr); return 2; }
		const char *sub = argv[2];
		if (!strcmp(sub, "list"))    return quarantine_list();
		if (argc < 4) die(_("`quarantine %s` needs something to act on"), sub);
		if (!strcmp(sub, "take"))    return quarantine_take(argv[3], "manual", "") == 0 ? 0 : 1;
		if (!strcmp(sub, "restore")) return quarantine_restore(argv[3]) == 0 ? 0 : 1;
		if (!strcmp(sub, "purge")) {
			if (!confirm(_("Permanently delete this quarantined file?"))) return 1;
			return quarantine_purge(argv[3]) == 0 ? 0 : 1;
		}
		die(_("no such quarantine command: %s"), sub);
	}

	if (!strcmp(cmd, "scan")) {
		findings_t f; findings_init(&f);
		time_t started = time(NULL);

		scan_req_t req = {
			.paths      = want_system ? NULL : (char *const *)&argv[2],
			.system     = want_system,
			.only       = only,
			.quarantine = want_quarantine,
		};

		if (!want_system && argc < 3)
			die(_("nothing to scan — give a path, or --system for the "
			      "rootkit checks"));

		/* ⚠ The system engines read files only root can read. Say so plainly
		 * rather than reporting a clean machine that was never looked at. */
		if (want_system && geteuid() != 0)
			warn("%s", _("--system without root: the checks will be partial"));

		int rc = scan_run(&req, &f);

		/* ⛔ A SCAN THAT RAN NOTHING IS NOT A CLEAN SCAN. Printing the
		 * findings here would say "Nothing found." to somebody whose machine
		 * was never looked at, and status_record() would persist that as a
		 * clean result for `syn-scan status` and the bar badge to repeat back
		 * for a week. Both are worse than the error. */
		if (rc != 0) {
			findings_free(&f);
			return 2;
		}

		findings_print(&f);
		status_record(&f, started);

		size_t bad = 0;
		for (const finding_t *p = f.head; p; p = p->next)
			if (p->verdict != VERDICT_CLEAN) bad++;

		if (want_quarantine && bad) {
			for (const finding_t *p = f.head; p; p = p->next) {
				/* ⛔ Only ClamAV's verdict moves a file. rkhunter and
				 * chkrootkit report system CHECKS, not files, and their
				 * "path" is the engine name — quarantining on that would
				 * mean moving something nobody identified. */
				if (p->verdict != VERDICT_INFECTED) continue;
				if (strcmp(p->engine, "clamav")) continue;
				if (!confirm(p->path)) continue;
				quarantine_take(p->path, p->engine, p->detail);
			}
		}

		findings_free(&f);
		return bad ? 1 : 0;
	}

	usage(stderr);
	return 2;
}
