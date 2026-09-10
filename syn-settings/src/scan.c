/* syn-settings — the malware scanning pane.
 *
 * ⚠ IT OWNS NO PART OF THE SCANNING. syn-scan(1) owns the engines, the
 * schedule, the quarantine and the state files; this pane reads
 * `syn-scan status --rec` and `syn-scan engines --rec`, and its switches run
 * systemctl against the units syn-scan ships. The rule the AI, speech and
 * remote panes already follow: a second idea of "was this machine scanned" is
 * a second thing that can be wrong about it.
 *
 * ⛔ THE SWEEP AND YOUR OWN SCANS KEEP SEPARATE RECORDS, AND THE PANE SAYS SO.
 * syn-scan resolves its state directory at runtime — /var/lib/syn-scan as root,
 * $XDG_DATA_HOME/syn-scan otherwise — so the weekly timer (root) and a scan you
 * type yourself write to two different files. A pane that showed only the one
 * it could reach as the user would report "never scanned" on a machine that has
 * swept every week since it was installed. Both rows are here, each named for
 * whose scan it is.
 *
 * ⛔ AND clamav-clamonacc.service IS SHOWN WITHOUT A BUTTON. It is masked by
 * syn-scan's scriptlet on purpose — on-access scanning is a second owner of the
 * open() path synguard's BPF-LSM already holds — so the row exists to say the
 * state and the reason, and not to offer a way to undo a decision this window
 * cannot explain in a button.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Where the SCHEDULED sweep records what it did: syn-scan's own default for a
 * process running as root, see state_dir() in its src/util.c.
 *
 * ⚠ AND IT IS A SEAM, for the reason SYN_SETTINGS_LIBDIR is one in ai.c. What
 * this pane says about the sweep depends on a directory only root writes, so a
 * check that reads the record reads THIS machine — a build box that has never
 * swept and velle's desktop that sweeps weekly are two different answers from
 * the same code. tests/syn_settings_test.sh points this at a directory it
 * controls and gets the same rows on any machine.
 */
static const char *scan_system_home(void)
{
	const char *e = getenv("SYN_SETTINGS_SCAN_HOME");
	return (e && *e) ? e : "/var/lib/syn-scan";
}

/* enabled/disabled/masked/not installed, and active/inactive, for one unit.
 *
 * Same shape as ai.c's and power.c's, and separate for the same reason they
 * are separate from each other: systemctl exits non-zero for "disabled", for
 * "masked" and for "no such unit" alike, so the STATUS is not the answer — the
 * word it prints is, and an absent unit prints nothing at all.
 */
static void unit_state(const char *unit, char *en, size_t en_cap,
                       char *act, size_t act_cap)
{
	char out[128] = "";
	char *is_en[]  = { (char *)"systemctl", (char *)"is-enabled", (char *)unit, NULL };
	char *is_act[] = { (char *)"systemctl", (char *)"is-active",  (char *)unit, NULL };

	run_capture_quiet(is_en, out, sizeof out);
	out[strcspn(out, "\n")] = '\0';
	tsv_clean(out);
	/* ⛔ "not-found" IS SYSTEMD'S WORD, NOT A STATE THIS WINDOW DRAWS. It is
	 * the spelling that actually happens for a unit the machine does not have
	 * — the empty output the fallback below was written for comes from an
	 * older systemd — and it reaches the table as a value with no colour, no
	 * translation and no row style. Said in the record's own vocabulary
	 * instead, which is the word every other pane uses for the same fact. */
	if (!strcmp(out, "not-found")) out[0] = '\0';
	snprintf(en, en_cap, "%s", out[0] ? out : "not installed");

	out[0] = '\0';
	run_capture_quiet(is_act, out, sizeof out);
	out[strcspn(out, "\n")] = '\0';
	tsv_clean(out);
	snprintf(act, act_cap, "%s", out[0] ? out : "-");
}

/* Is this unit absent from the machine? Both spellings, because getting it
 * wrong offers Enable on a unit that is not there — which does nothing and
 * reports success at having done it. */
static int unit_absent(const char *en)
{
	return !strcmp(en, "not installed") || !strcmp(en, "not-found");
}

/* What one of syn-scan's --rec records says, asked for BY COLUMN NAME is not
 * possible here — that record is one row with a fixed shape — so this takes the
 * field index off the single `status` row. Returns 0 when there is no such row.
 *
 * `home` is the state directory to ask about: scan_system_home() for the
 * scheduled sweep, NULL for this account's own scans.
 *
 * ⚠ THE ENVIRONMENT IS PUT BACK IMMEDIATELY. $SYNSCAN_HOME is syn-scan's own
 * seam for pointing itself at a state directory (its test suite uses it), and
 * it is set only across this one call: left set, the very next syn-scan command
 * this pane runs — the quarantine count — would read root's 0700 directory and
 * report an error instead of a number.
 */
static int scan_status(const char *home, long long *finished,
                       unsigned long *findings)
{
	char rec[512] = "";
	char *a[] = { (char *)"syn-scan", (char *)"status", (char *)"--rec", NULL };
	int got = 0;

	*finished = 0;
	*findings = 0;

	if (home) setenv("SYNSCAN_HOME", home, 1);
	int rc = run_capture_quiet(a, rec, sizeof rec);
	if (home) unsetenv("SYNSCAN_HOME");
	if (rc != 0) return 0;

	for (char *line = strtok(rec, "\n"); line; line = strtok(NULL, "\n")) {
		if (strncmp(line, "status\t", 7) != 0) continue;
		char state[32] = "";
		long long fin = 0; unsigned long bad = 0;
		/* "status<TAB>ran<TAB><epoch><TAB><count>", or "status<TAB>never". */
		if (sscanf(line, "status\t%31[^\t]\t%lld\t%lu", state, &fin, &bad) >= 1
		    && !strcmp(state, "ran")) {
			*finished = fin;
			*findings = bad;
			got = 1;
		}
	}
	return got;
}

/* An epoch second as a date somebody can read, or "" for zero. */
static void when_str(long long t, char *out, size_t cap)
{
	out[0] = '\0';
	if (t <= 0) return;
	time_t tt = (time_t)t;
	struct tm tm;
	if (localtime_r(&tt, &tm))
		strftime(out, cap, "%Y-%m-%d %H:%M", &tm);
}

/* How many things are sitting in THIS ACCOUNT's quarantine. -1 when syn-scan
 * could not be asked. */
static int quarantine_count(void)
{
	char rec[8192] = "";
	char *a[] = { (char *)"syn-scan", (char *)"quarantine",
	              (char *)"list", (char *)"--rec", NULL };
	if (run_capture_quiet(a, rec, sizeof rec) != 0) return -1;

	int n = 0;
	for (char *line = strtok(rec, "\n"); line; line = strtok(NULL, "\n"))
		if (strncmp(line, "quarantine\t", 11) == 0) n++;
	return n;
}

/* ── The engines ─────────────────────────────────────────────────────────────
 *
 * ⛔ THE LABEL IS OURS, THE ENGINE ID IS syn-scan's. The names in that record —
 * "ClamAV", "Rootkit Hunter" — arrive from another program at runtime and can
 * never be in this package's catalog, so a pane that drew them as its labels
 * would have three cells no translator can reach. The id is the protocol
 * (synscan.h says so in as many words); the sentence beside it is ours and is
 * marked here.
 */
struct scan_engine {
	const char *id;
	const char *label;
	const char *what;
	const char *absent;
};

static const struct scan_engine engines[] = {
	{ "clamav", N_("Signature scanner"),
	  N_("ClamAV, which is what looks inside files \xc2\xb7 it is the engine that finds a Windows payload sitting in a Proton prefix"),
	  N_("without it there is nothing to match file contents against \xc2\xb7 synpkg install clamav") },
	{ "rkhunter", N_("Rootkit checks"),
	  N_("rkhunter, which checks the system itself rather than your files"),
	  N_("optional \xc2\xb7 synpkg install rkhunter adds it, and the scheduled sweep picks it up with no further setting") },
	{ "chkrootkit", N_("Second rootkit opinion"),
	  N_("chkrootkit, which looks for the same class of thing rkhunter does and disagrees with it usefully often"),
	  N_("optional, and it lives in [blackarch], which this machine may have declined at install time \xc2\xb7 syn arsenal --enable-repo") },
};

static const struct scan_engine *engine_by_id(const char *id)
{
	for (size_t i = 0; i < sizeof engines / sizeof engines[0]; i++)
		if (!strcmp(engines[i].id, id)) return &engines[i];
	return NULL;
}

static void engine_rows(void)
{
	char rec[4096] = "";
	char *a[] = { (char *)"syn-scan", (char *)"engines", (char *)"--rec", NULL };

	if (run_capture_quiet(a, rec, sizeof rec) != 0) {
		rec_row("engine\t%s\t%s\t-\t%s\t-",
		        N_("Engines"), N_("unknown"),
		        N_("syn-scan could not be asked which back ends are installed"));
		return;
	}

	for (char *line = strtok(rec, "\n"); line; line = strtok(NULL, "\n")) {
		if (strncmp(line, "engine\t", 7) != 0) continue;

		char id[64] = "", name[64] = "", path[PATH_CAP] = "";
		int present = 0, runnable = 0;
		if (sscanf(line, "engine\t%63[^\t]\t%63[^\t]\t%d\t%d\t%511[^\t\n]",
		           id, name, &present, &runnable, path) < 4)
			continue;
		tsv_clean(id); tsv_clean(name); tsv_clean(path);

		const struct scan_engine *e = engine_by_id(id);

		/* ⛔ PRESENT AND RUNNABLE ARE TWO FACTS. Arch ships rkhunter 0700
		 * root:root, so a normal user cannot execute a program that is
		 * installed and working — and the scheduled sweep, which runs as root,
		 * uses it perfectly well. Reporting that as "not installed" sends
		 * somebody to reinstall a package they already have. */
		const char *value = !present   ? N_("not installed")
		                  : !runnable  ? N_("needs root")
		                               : N_("ready");

		/* The sentence, plus what this particular state means. Composed, and
		 * each piece is a msgid of its own — the same shape kernel.c's detail
		 * uses, and what the drawn-label check's prefix rule is for. */
		char detail[1024];
		if (!present)
			snprintf(detail, sizeof detail, "%s \xc2\xb7 %s",
			         e ? e->what : N_("a scanning back end"),
			         e ? e->absent : N_("not on this machine"));
		else if (!runnable)
			snprintf(detail, sizeof detail, "%s \xc2\xb7 %s%s%s",
			         e ? e->what : N_("a scanning back end"),
			         N_("installed 0700 root, so the scheduled sweep can run it and a scan you start yourself cannot"),
			         path[0] ? " \xc2\xb7 " : "", path);
		else
			snprintf(detail, sizeof detail, "%s \xc2\xb7 %s",
			         e ? e->what : N_("a scanning back end"),
			         path[0] ? path : N_("installed"));

		rec_row("engine\t%s\t%s\t%s\t%s\t-",
		        e ? e->label : N_("Engine"), value,
		        id, detail);
	}
}

/* ── The units behind the switches ───────────────────────────────────────── */
struct scan_unit {
	const char *unit;
	const char *what;
	int offer;   /* 0 for the one that must not be turned on from here */
};

static const struct scan_unit scan_units[] = {
	{ "syn-scan.timer",
	  N_("the schedule \xc2\xb7 the switch at the top of this pane is what turns it on"), 1 },
	{ "syn-scan.service",
	  N_("the sweep itself \xc2\xb7 the timer starts it, and starting it here runs one now \xc2\xb7 inactive between runs is what a finished sweep looks like"), 1 },
	{ "clamav-freshclam.service",
	  N_("the signature updater \xc2\xb7 syn-scan enables this on install because a scanner with stale signatures is worse than no scanner"), 1 },
	{ "clamav-daemon.service",
	  N_("clamd \xc2\xb7 off by default on this system \xc2\xb7 while it is running syn-scan hands its files to clamdscan instead, which is the whole difference the switch above buys"), 1 },
	/* ⛔ NO BUTTON. Masked on purpose by syn-scan's scriptlet: on-access
	 * scanning is a second owner of the open() path synguard's BPF-LSM already
	 * holds, and the second owner is the one nobody is monitoring. */
	{ "clamav-clamonacc.service",
	  N_("on-access scanning, masked on purpose \xc2\xb7 synguard already watches every open in the kernel, and two owners of that path is one contended hook"), 0 },
};

int pane_scan(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	if (!have_cmd("syn-scan")) {
		rec_row("switch\t%s\tunavailable\t-\t%s\t-",
		        N_("Malware scanning"),
		        N_("needs syn-scan(1) \xc2\xb7 synpkg install syn-scan"));
		return 0;
	}

	int have_systemctl = have_cmd("systemctl");

	/* ── The switch ───────────────────────────────────────────────────── */
	/*
	 * ⚠ THE TIMER, NOT THE SERVICE. Enabling syn-scan.service would arm
	 * nothing: it is a oneshot the timer starts, and a person looking for
	 * "does this machine scan itself" is asking about the schedule.
	 */
	if (!have_systemctl) {
		rec_row("switch\t%s\t%s\t-\t%s\t-",
		        N_("Scheduled scan"), N_("unknown"),
		        N_("systemctl is not available, so the schedule cannot be read"));
	} else {
		char en[64], act[64];
		unit_state("syn-scan.timer", en, sizeof en, act, sizeof act);
		if (unit_absent(en))
			rec_row("switch\t%s\tunavailable\t-\t%s\t-",
			        N_("Scheduled scan"),
			        N_("syn-scan is installed and syn-scan.timer is not on this machine"));
		else {
			/* ⚠ THE WORD IS HOISTED OUT OF THE CALL, as ai.c does with the
			 * same comparison. Every literal in a rec_row argument is checked
			 * for unmarked prose, and "enabled" sitting there reads to that
			 * check exactly like a drawn English word — which is the right
			 * rule, kept by moving the token rather than by weakening it. */
			const char *on = !strcmp(en, "enabled") ? "on" : "off";
			rec_row("switch\t%s\t%s\t%s\t%s\ttoggle:malware-scan",
			        N_("Scheduled scan"), on, act,
			        N_("a weekly sweep of the home directories and the temporary ones, at idle priority \xc2\xb7 a machine that was asleep when it was due runs it on the next boot rather than skipping it"));
		}

		/* When it next runs. Only worth a row once something is armed:
		 * systemd leaves the property empty for a timer that is not running,
		 * and a row reading "-" says nothing a person wanted. */
		char next[128] = "";
		char *nx[] = { (char *)"systemctl", (char *)"show",
		               (char *)"syn-scan.timer",
		               (char *)"--property=NextElapseUSecRealtime",
		               (char *)"--value", NULL };
		run_capture_quiet(nx, next, sizeof next);
		next[strcspn(next, "\n")] = '\0';
		tsv_clean(next);
		if (next[0] && strcmp(next, "n/a") != 0)
			rec_row("value\t%s\t%s\t-\t%s\t-",
			        N_("Next sweep"), next,
			        N_("systemd spreads the start over two hours so every machine on a network does not begin at once"));
	}

	/* ── What the scheduled sweep found ───────────────────────────────── */
	/*
	 * ⛔ READ FROM ROOT'S STATE DIRECTORY, WHICH IS THE ONE THE TIMER WRITES.
	 * As the user, plain `syn-scan status` answers about YOUR scans — see the
	 * heading — so this pane would otherwise report a machine that sweeps
	 * weekly as never scanned.
	 *
	 * ⚠ AND UNREADABLE IS NOT THE SAME ANSWER AS NEVER. syn-scan prints
	 * "never" for both, because from inside it they look alike: there is no
	 * file it can open. Root's record is 0644 in the normal case, so this only
	 * fires on a machine where somebody tightened it — and "never scanned" on
	 * a machine that scans weekly is exactly the class of false state this
	 * whole app exists to stop. The name of the file is syn-scan's; it is
	 * tested for readability here and never parsed.
	 */
	{
		char sysfile[PATH_CAP];
		snprintf(sysfile, sizeof sysfile, "%s/last-scan", scan_system_home());
		int hidden = access(sysfile, F_OK) == 0 && access(sysfile, R_OK) != 0;

		long long fin = 0; unsigned long bad = 0;
		int ran = scan_status(scan_system_home(), &fin, &bad);
		char when[64];
		when_str(fin, when, sizeof when);

		if (hidden)
			rec_row("value\t%s\t%s\t-\t%s\t-",
			        N_("Last sweep"), N_("unknown"),
			        N_("the scheduled sweep's record is not readable by this account \xc2\xb7 sudo syn-scan status"));
		else if (!ran || !when[0])
			rec_row("value\t%s\t%s\t-\t%s\t-",
			        N_("Last sweep"), N_("never"),
			        N_("no scheduled sweep has finished yet \xc2\xb7 the first one runs within a week of the timer being switched on"));
		else
			rec_row("value\t%s\t%s\t-\t%s\t-",
			        N_("Last sweep"), when,
			        N_("what the timer last did \xc2\xb7 it walks the home directories, /srv and the temporary ones"));

		if (ran)
			rec_row("value\t%s\t%lu\t%s\t%s\t-",
			        N_("Outstanding findings"), bad,
			        bad ? N_("needs a look") : N_("clear"),
			        bad ? N_("the sweep flagged these and nothing has looked at them \xc2\xb7 sudo syn-scan status lists them, and nothing is ever deleted")
			            : N_("the last sweep finished with nothing to report"));
	}

	/* ── And this account's own scans ─────────────────────────────────── */
	{
		long long fin = 0; unsigned long bad = 0;
		int ran = scan_status(NULL, &fin, &bad);
		char when[64];
		when_str(fin, when, sizeof when);

		rec_row("value\t%s\t%s\t-\t%s\t-",
		        N_("Your last scan"),
		        (ran && when[0]) ? when : N_("never"),
		        N_("a scan you start yourself keeps its own record, under your account rather than root's \xc2\xb7 syn-scan scan ~/Downloads"));
	}

	/* ── Quarantine ───────────────────────────────────────────────────── */
	/*
	 * ⚠ YOURS, AND IT SAYS SO. The sweep's quarantine is 0700 root — that is
	 * the point of it — so this count can only ever be the one this account
	 * can see, and the sentence names the command that shows the other.
	 */
	{
		int n = quarantine_count();
		char count[32];
		snprintf(count, sizeof count, "%d", n < 0 ? 0 : n);
		rec_row("value\t%s\t%s\t-\t%s\t-",
		        N_("Your quarantine"), n < 0 ? N_("unknown") : count,
		        N_("nothing is ever deleted \xc2\xb7 syn-scan quarantine list shows what was set aside and restore puts a file back where it came from \xc2\xb7 the sweep's own quarantine belongs to root, so sudo lists that one"));
	}

	/* ── The engines ──────────────────────────────────────────────────── */
	engine_rows();

	/* ── Signatures, and the daemon that holds them ───────────────────── */
	if (have_systemctl) {
		char en[64], act[64];

		unit_state("clamav-freshclam.service", en, sizeof en, act, sizeof act);
		if (unit_absent(en))
			rec_row("switch\t%s\tunavailable\t-\t%s\t-",
			        N_("Signature updates"),
			        N_("needs clamav, which is what ships the updater"));
		else {
			const char *on = !strcmp(en, "enabled") ? "on" : "off";
			rec_row("switch\t%s\t%s\t%s\t%s\ttoggle:signature-updates",
			        N_("Signature updates"), on, act,
			        N_("freshclam fetches new signatures as they are published \xc2\xb7 switching this off leaves the scanner looking for last year's malware, and it is on by default for that reason"));
		}

		/*
		 * ⛔ A REAL GIGABYTE, AND THE ROW SAYS SO BEFORE ANYBODY PRESSES IT.
		 * clamd holds the whole signature set resident — measured at 0.95 GB —
		 * which is why syn-scan leaves it off and shells to clamscan instead.
		 * It is also a genuine choice: with clamd up, syn-scan uses clamdscan
		 * and a small scan takes 0.02 s instead of 5.91 s, because clamscan
		 * reloads every signature on every run.
		 *
		 * ⚠ AND THE FIRST-RUN TRAP IS NAMED. Arch's clamav ships no database
		 * at all, and clamd refuses to start without one — so switching this
		 * on before freshclam's first download has finished lands the unit in
		 * `failed`, which looks like a broken package rather than a race.
		 */
		unit_state("clamav-daemon.service", en, sizeof en, act, sizeof act);
		if (unit_absent(en))
			rec_row("switch\t%s\tunavailable\t-\t%s\t-",
			        N_("Keep signatures in memory"),
			        N_("needs clamav, which is what ships the daemon"));
		else {
			const char *on = !strcmp(en, "enabled") ? "on" : "off";
			rec_row("switch\t%s\t%s\t%s\t%s\ttoggle:scan-daemon",
			        N_("Keep signatures in memory"), on, act,
			        N_("clamd keeps the whole signature set loaded, which makes a scan start instantly instead of spending six seconds reading it back \xc2\xb7 it costs about a gigabyte of memory, which is why this system leaves it off unless you ask for it \xc2\xb7 it will not start until the signature download has finished"));
		}
	}

	/* ── The units themselves ─────────────────────────────────────────── */
	/*
	 * ⛔ A SWITCH AND THE THING IT SWITCHES ARE TWO SEPARATE FACTS. syn-speak
	 * shipped four releases with a switch reading On and no unit packaged
	 * behind it; these rows are what would have said so.
	 */
	if (have_systemctl) {
		for (size_t i = 0; i < sizeof scan_units / sizeof scan_units[0]; i++) {
			char en[64], act[64], action[128];
			unit_state(scan_units[i].unit, en, sizeof en, act, sizeof act);
			snprintf(action, sizeof action, "unit:%s", scan_units[i].unit);
			rec_row("unit\t%s\t%s\t%s\t%s\t%s",
			        scan_units[i].unit, en, act, scan_units[i].what,
			        (scan_units[i].offer && !unit_absent(en)) ? action : "-");
		}
	} else {
		rec_row("unit\t-\t%s\t-\t%s\t-",
		        N_("unknown"), N_("systemctl not available"));
	}

	return 0;
}
