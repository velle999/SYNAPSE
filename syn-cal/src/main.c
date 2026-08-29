/* main.c — the command line, which is the whole program.
 *
 * The GUI will be quickshell parsing `syn-cal --rec`, exactly as synfiles and
 * synpkg are: nothing in the QML knows what a CalDAV collection is, and nothing
 * here knows what a row looks like. Every verb therefore has to be usable by a
 * person at a terminal AND emit a clean record, because the second is the
 * front-end's only interface and the first is how it gets debugged.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "account.h"
#include "caldav.h"
#include "sync.h"

#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void usage(FILE *f)
{
	fprintf(f,
"syn-cal — the SynapseOS calendar and schedule planner\n"
"\n"
"  syn-cal accounts                     what is set up\n"
"  syn-cal account add <name> <url>     add a CalDAV account\n"
"  syn-cal account show <name>          its settings, and where its secret is\n"
"  syn-cal account remove <name>        forget it, and its password\n"
"  syn-cal login <name> [--user U]      set the password (prompted, never echoed)\n"
"  syn-cal logout <name>                forget just the password\n"
"  syn-cal discover <name>              ask the server which calendars exist\n"
"  syn-cal calendars <name>             list them, and which are switched on\n"
"  syn-cal enable <name> <calendar>     sync this one\n"
"  syn-cal disable <name> <calendar>    stop syncing it\n"
"  syn-cal sync [name]                  sync everything, or one account\n"
"  syn-cal events <name> <calendar>     what is in the local store\n"
"\n"
"  --rec        one record per line, for a front end\n"
"  --dry-run    with sync: decide everything, change nothing\n"
"  --conflict=keep-both|remote|local    default keep-both, which loses nothing\n"
"  --verbose    say what is happening\n"
"  --version    print the version\n"
"\n"
"Passwords and tokens are kept in the keyring, never in accounts.conf.\n"
"For an account with two-factor authentication, use an app password.\n");
}

/* ── a password, read without echoing it ────────────────────────────────── */

static char *prompt_secret(const char *label)
{
	if (!isatty(STDIN_FILENO)) {
		/* ⚠ STDIN IS A SUPPORTED WAY IN, so the settings pane can pipe a
		 * password rather than pass one in argv — /proc/<pid>/cmdline is
		 * world-readable and /proc/<pid>/environ is not. Same reasoning as
		 * syn-settings' assistant pane. */
		size_t cap = 0, len = 0;
		char *line = NULL;
		len = getline(&line, &cap, stdin) > 0 ? strlen(line) : 0;
		if (!line) return NULL;
		while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
		return line;
	}

	struct termios old, quiet;
	bool hushed = tcgetattr(STDIN_FILENO, &old) == 0;
	if (hushed) {
		quiet = old;
		quiet.c_lflag &= ~(tcflag_t)ECHO;
		hushed = tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet) == 0;
	}

	fprintf(stderr, "%s", label);
	fflush(stderr);
	char *line = NULL;
	size_t cap = 0;
	ssize_t n = getline(&line, &cap, stdin);

	if (hushed) tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
	fputc('\n', stderr);

	if (n <= 0) { free(line); return NULL; }
	while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
	return line;
}

/* ── auth for an account ────────────────────────────────────────────────── */

static bool auth_for(account_t *acc, http_auth_t *out, char **err)
{
	memset(out, 0, sizeof *out);
	out->timeout_s = 30;
	out->insecure = acc->insecure;

	if (acc->kind == ACC_CALDAV) {
		out->user = acc->user ? xstrdup(acc->user) : NULL;
		out->pass = secret_fetch(acc->name, "password", NULL);
		if (!out->pass) {
			if (err) *err = xasprintf("no password for '%s' — run: syn-cal login %s",
			                          acc->name, acc->name);
			return false;
		}
		return true;
	}

	out->bearer = secret_fetch(acc->name, "access_token", NULL);
	if (!out->bearer) {
		if (err) *err = xasprintf("'%s' is not signed in yet", acc->name);
		return false;
	}
	return true;
}

static void auth_free(http_auth_t *a)
{
	if (a->pass) { memset(a->pass, 0, strlen(a->pass)); free(a->pass); }
	if (a->bearer) { memset(a->bearer, 0, strlen(a->bearer)); free(a->bearer); }
	free(a->user);
	memset(a, 0, sizeof *a);
}

/* ── verbs ──────────────────────────────────────────────────────────────── */

static int cmd_accounts(void)
{
	accounts_t a;
	accounts_load(&a);

	if (g_out == OUT_REC) {
		rec_header("name\tkind\turl\tuser\tsecret\tcalendars\tenabled");
		for (size_t i = 0; i < a.n; i++) {
			account_t *e = &a.e[i];
			size_t on = 0;
			for (size_t c = 0; c < e->ncals; c++) if (e->cals[c].enabled) on++;
			char *n = pct_encode(e->name, false);
			char *u = pct_encode(e->url ? e->url : "", true);
			char *us = pct_encode(e->user ? e->user : "", false);
			rec_row("%s\t%s\t%s\t%s\t%s\t%zu\t%zu", n, acc_kind_name(e->kind), u, us,
			        secret_where(e->name, e->kind == ACC_CALDAV ? "password" : "access_token"),
			        e->ncals, on);
			free(n); free(u); free(us);
		}
	} else if (a.n == 0) {
		printf("No accounts yet.\n\n  syn-cal account add work https://caldav.fastmail.com/\n");
	} else {
		for (size_t i = 0; i < a.n; i++) {
			account_t *e = &a.e[i];
			size_t on = 0;
			for (size_t c = 0; c < e->ncals; c++) if (e->cals[c].enabled) on++;
			printf("%-14s %-9s %s\n", e->name, acc_kind_name(e->kind), e->url ? e->url : "");
			printf("               %zu of %zu calendars on, password %s\n", on, e->ncals,
			       secret_where(e->name, e->kind == ACC_CALDAV ? "password" : "access_token"));
		}
	}
	accounts_free(&a);
	return 0;
}

static int cmd_account_add(const char *name, const char *url, const char *user)
{
	accounts_t a;
	accounts_load(&a);
	if (accounts_find(&a, name)) { warn("there is already an account called '%s'", name); accounts_free(&a); return 1; }

	account_t *e = accounts_add(&a, name);
	e->kind = ACC_CALDAV;
	e->url = xstrdup(url);
	if (user) e->user = xstrdup(user);
	bool ok = accounts_save(&a);
	accounts_free(&a);
	if (!ok) { warn("could not write accounts.conf"); return 1; }

	if (g_out != OUT_REC)
		printf("Added '%s'.\n\n  syn-cal login %s\n  syn-cal discover %s\n", name, name, name);
	return 0;
}

static int cmd_account_remove(const char *name)
{
	accounts_t a;
	accounts_load(&a);
	if (!accounts_remove(&a, name)) { warn("no account called '%s'", name); accounts_free(&a); return 1; }
	bool ok = accounts_save(&a);
	accounts_free(&a);

	/* The password goes with it. Leaving a credential behind for an account
	 * that no longer exists is a secret nothing will ever clean up. */
	secret_forget(name, "password", NULL);
	secret_forget(name, "access_token", NULL);
	secret_forget(name, "refresh_token", NULL);

	if (g_out != OUT_REC) printf("Removed '%s', and its password.\n", name);
	return ok ? 0 : 1;
}

static int cmd_login(const char *name, const char *user)
{
	accounts_t a;
	accounts_load(&a);
	account_t *e = accounts_find(&a, name);
	if (!e) { warn("no account called '%s'", name); accounts_free(&a); return 1; }

	if (user) { free(e->user); e->user = xstrdup(user); accounts_save(&a); }
	if (!e->user) {
		warn("'%s' has no username — run: syn-cal login %s --user you@example.org", name, name);
		accounts_free(&a);
		return 1;
	}

	char *label = xasprintf("Password for %s at %s: ", e->user, e->url ? e->url : name);
	char *secret = prompt_secret(label);
	free(label);
	if (!secret || !*secret) { warn("nothing entered; no change"); free(secret); accounts_free(&a); return 1; }

	char *err = NULL;
	bool ok = secret_store(name, "password", secret, &err);
	memset(secret, 0, strlen(secret));
	free(secret);
	accounts_free(&a);

	if (!ok) { warn("%s", err ? err : "the password could not be stored"); free(err); return 1; }
	free(err);
	if (g_out != OUT_REC) printf("Saved. Where: %s\n", secret_where(name, "password"));
	return 0;
}

static int cmd_discover(const char *name)
{
	accounts_t a;
	accounts_load(&a);
	account_t *e = accounts_find(&a, name);
	if (!e) { warn("no account called '%s'", name); accounts_free(&a); return 1; }

	http_auth_t auth;
	char *err = NULL;
	if (!auth_for(e, &auth, &err)) { warn("%s", err); free(err); accounts_free(&a); return 1; }

	caldav_colls_t colls;
	bool ok = caldav_discover(e->url, &auth, &colls, &err);
	auth_free(&auth);
	if (!ok) {
		warn("%s", err ? err : "nothing found");
		free(err);
		accounts_free(&a);
		return 1;
	}

	/* ⚠ NEWLY FOUND CALENDARS ARE OFF. Discovery on a work account can turn up
	 * a dozen shared calendars; syncing them all because they exist is how a
	 * planner becomes unreadable on first use. A calendar already switched on
	 * stays on — re-running discovery must not undo a choice. */
	for (size_t i = 0; i < colls.n; i++) {
		acc_cal_t *known = acc_find_cal(e, colls.e[i].url);
		acc_set_cal(e, colls.e[i].url, colls.e[i].name, known ? known->enabled : false);
	}
	accounts_save(&a);

	if (g_out == OUT_REC) {
		rec_header("url\tname\tenabled\tevents\ttodos\tcolor");
		for (size_t i = 0; i < colls.n; i++) {
			acc_cal_t *c = acc_find_cal(e, colls.e[i].url);
			char *u = pct_encode(colls.e[i].url, true);
			char *n = pct_encode(colls.e[i].name ? colls.e[i].name : "", false);
			rec_row("%s\t%s\t%d\t%d\t%d\t%s", u, n, c && c->enabled,
			        colls.e[i].events, colls.e[i].todos,
			        colls.e[i].color ? colls.e[i].color : "");
			free(u); free(n);
		}
	} else {
		printf("Found %zu calendar%s on '%s':\n\n", colls.n, colls.n == 1 ? "" : "s", name);
		for (size_t i = 0; i < colls.n; i++) {
			acc_cal_t *c = acc_find_cal(e, colls.e[i].url);
			printf("  [%s] %s\n", (c && c->enabled) ? "on " : "off",
			       colls.e[i].name ? colls.e[i].name : colls.e[i].url);
		}
		printf("\nNew calendars start switched off.\n  syn-cal enable %s \"<name>\"\n", name);
	}

	caldav_colls_free(&colls);
	accounts_free(&a);
	return 0;
}

static int cmd_set_enabled(const char *name, const char *which, bool on)
{
	accounts_t a;
	accounts_load(&a);
	account_t *e = accounts_find(&a, name);
	if (!e) { warn("no account called '%s'", name); accounts_free(&a); return 1; }

	/* Matched by display name or by URL, because the display name is what the
	 * list shows and the URL is what a script has. */
	acc_cal_t *hit = NULL;
	for (size_t i = 0; i < e->ncals; i++)
		if ((e->cals[i].name && strcmp(e->cals[i].name, which) == 0) ||
		    strcmp(e->cals[i].url, which) == 0) { hit = &e->cals[i]; break; }

	if (!hit) {
		warn("'%s' has no calendar called '%s' — run: syn-cal calendars %s", name, which, name);
		accounts_free(&a);
		return 1;
	}
	hit->enabled = on;
	bool ok = accounts_save(&a);
	if (g_out != OUT_REC) printf("%s: %s\n", hit->name ? hit->name : hit->url, on ? "on" : "off");
	accounts_free(&a);
	return ok ? 0 : 1;
}

static int cmd_calendars(const char *name)
{
	accounts_t a;
	accounts_load(&a);
	account_t *e = accounts_find(&a, name);
	if (!e) { warn("no account called '%s'", name); accounts_free(&a); return 1; }

	if (g_out == OUT_REC) rec_header("url\tname\tenabled");
	for (size_t i = 0; i < e->ncals; i++) {
		if (g_out == OUT_REC) {
			char *u = pct_encode(e->cals[i].url, true);
			char *n = pct_encode(e->cals[i].name ? e->cals[i].name : "", false);
			rec_row("%s\t%s\t%d", u, n, e->cals[i].enabled);
			free(u); free(n);
		} else {
			printf("  [%s] %s\n", e->cals[i].enabled ? "on " : "off",
			       e->cals[i].name ? e->cals[i].name : e->cals[i].url);
		}
	}
	if (g_out != OUT_REC && e->ncals == 0)
		printf("Nothing yet — run: syn-cal discover %s\n", name);
	accounts_free(&a);
	return 0;
}

static int sync_account(account_t *e, conflict_t policy, bool dry, sync_stats_t *tot)
{
	http_auth_t auth;
	char *err = NULL;
	if (!auth_for(e, &auth, &err)) { warn("%s", err); free(err); return 1; }

	int bad = 0;
	for (size_t i = 0; i < e->ncals; i++) {
		if (!e->cals[i].enabled) continue;
		const char *label = e->cals[i].name ? e->cals[i].name : e->cals[i].url;

		remote_t *r = caldav_remote(e->cals[i].url, &auth);
		sync_opts_t o = { e->name, label, policy, dry };
		sync_stats_t st;
		char *serr = NULL;

		if (!sync_run(r, &o, &st, &serr)) {
			warn("%s / %s: %s", e->name, label, serr ? serr : "sync failed");
			free(serr);
			bad = 1;
		} else {
			tot->pulled_new += st.pulled_new; tot->pulled_changed += st.pulled_changed;
			tot->pulled_deleted += st.pulled_deleted; tot->pushed_new += st.pushed_new;
			tot->pushed_changed += st.pushed_changed; tot->pushed_deleted += st.pushed_deleted;
			tot->conflicts += st.conflicts; tot->errors += st.errors; tot->skipped += st.skipped;

			if (g_out == OUT_REC) {
				char *a1 = pct_encode(e->name, false), *c1 = pct_encode(label, false);
				rec_row("%s\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u", a1, c1,
				        st.pulled_new, st.pulled_changed, st.pulled_deleted,
				        st.pushed_new, st.pushed_changed, st.pushed_deleted,
				        st.conflicts, st.errors);
				free(a1); free(c1);
			} else if (g_verbose || st.pulled_new || st.pulled_changed || st.pulled_deleted ||
			           st.pushed_new || st.pushed_changed || st.pushed_deleted || st.conflicts) {
				printf("  %s / %s: %u down, %u up, %u removed here, %u removed there",
				       e->name, label,
				       st.pulled_new + st.pulled_changed,
				       st.pushed_new + st.pushed_changed,
				       st.pulled_deleted, st.pushed_deleted);
				if (st.conflicts) printf(", %u conflict%s kept", st.conflicts, st.conflicts == 1 ? "" : "s");
				putchar('\n');
			}
		}
		caldav_remote_free(r);
	}
	auth_free(&auth);
	return bad;
}

static int cmd_sync(const char *only, conflict_t policy, bool dry)
{
	accounts_t a;
	accounts_load(&a);
	if (a.n == 0) { warn("no accounts yet — run: syn-cal account add ..."); accounts_free(&a); return 1; }

	if (g_out == OUT_REC)
		rec_header("account\tcalendar\tdown_new\tdown_changed\tdown_deleted\t"
		           "up_new\tup_changed\tup_deleted\tconflicts\terrors");

	sync_stats_t tot;
	memset(&tot, 0, sizeof tot);
	int bad = 0;
	size_t ran = 0;

	for (size_t i = 0; i < a.n; i++) {
		if (only && strcmp(a.e[i].name, only) != 0) continue;
		ran++;
		bad |= sync_account(&a.e[i], policy, dry, &tot);
	}

	if (only && ran == 0) { warn("no account called '%s'", only); accounts_free(&a); return 1; }

	if (g_out != OUT_REC) {
		unsigned moved = tot.pulled_new + tot.pulled_changed + tot.pushed_new + tot.pushed_changed;
		if (dry) printf("Dry run: %u event%s would move.\n", moved, moved == 1 ? "" : "s");
		else if (!moved && !tot.conflicts && !tot.pulled_deleted && !tot.pushed_deleted)
			printf("Already up to date.\n");
		if (tot.conflicts)
			printf("%u conflict%s — both copies were kept. Look for events ending "
			       "'-syncal-local-'.\n", tot.conflicts, tot.conflicts == 1 ? "" : "s");
	}

	accounts_free(&a);
	return bad;
}

static int cmd_events(const char *name, const char *cal)
{
	local_list_t l;
	local_scan(name, cal, &l);

	if (g_out == OUT_REC) rec_header("uid\tkind\tstart\tsummary");
	for (size_t i = 0; i < l.n; i++) {
		size_t len = 0;
		char *data = local_read(name, cal, l.e[i].uid, &len);
		if (!data) continue;
		char *u = ics_unfold(data, len);
		char *sum = ics_prop(u, "SUMMARY");
		char *start = ics_prop(u, "DTSTART");
		char *kind = ics_kind(data, len);

		if (g_out == OUT_REC) {
			char *a1 = pct_encode(l.e[i].uid, false);
			char *a2 = pct_encode(sum ? sum : "", false);
			rec_row("%s\t%s\t%s\t%s", a1, kind ? kind : "", start ? start : "", a2);
			free(a1); free(a2);
		} else {
			printf("  %-18s %s\n", start ? start : "-", sum ? sum : "(no summary)");
		}
		free(sum); free(start); free(kind); free(u); free(data);
	}
	if (g_out != OUT_REC && l.n == 0) printf("Nothing in %s / %s yet.\n", name, cal);
	local_free(&l);
	return 0;
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
	const char *user = NULL;
	conflict_t policy = CONFLICT_KEEP_BOTH;
	bool dry = false;

	int n = 0;
	const char *pos[8];

	for (int i = 1; i < argc; i++) {
		const char *v = argv[i];
		if (!strcmp(v, "--rec")) { g_out = OUT_REC; g_color = false; continue; }
		if (!strcmp(v, "--verbose") || !strcmp(v, "-v")) { g_verbose = true; continue; }
		if (!strcmp(v, "--dry-run")) { dry = true; continue; }
		if (!strcmp(v, "--help") || !strcmp(v, "-h")) { usage(stdout); return 0; }
		if (!strcmp(v, "--version")) { printf("syn-cal %s\n", SYNCAL_VERSION); return 0; }
		if (!strncmp(v, "--user=", 7)) { user = v + 7; continue; }
		if (!strcmp(v, "--user") && i + 1 < argc) { user = argv[++i]; continue; }
		if (!strncmp(v, "--conflict=", 11)) {
			const char *c = v + 11;
			if (!strcmp(c, "keep-both")) policy = CONFLICT_KEEP_BOTH;
			else if (!strcmp(c, "remote")) policy = CONFLICT_REMOTE_WINS;
			else if (!strcmp(c, "local")) policy = CONFLICT_LOCAL_WINS;
			else { warn("unknown conflict policy '%s'", c); return 2; }
			continue;
		}
		if (v[0] == '-' && v[1] == '-') { warn("unknown option '%s'", v); usage(stderr); return 2; }
		if (n < 8) pos[n++] = v;
	}

	if (n == 0) { usage(stdout); return 0; }

	http_global_init();
	int rc = 2;
	const char *c = pos[0];

	if (!strcmp(c, "accounts"))                       rc = cmd_accounts();
	else if (!strcmp(c, "account") && n >= 2) {
		if (!strcmp(pos[1], "add") && n >= 4)         rc = cmd_account_add(pos[2], pos[3], user);
		else if (!strcmp(pos[1], "remove") && n >= 3) rc = cmd_account_remove(pos[2]);
		else if (!strcmp(pos[1], "show") && n >= 3)   rc = cmd_calendars(pos[2]);
		else { warn("usage: syn-cal account add|remove|show ..."); rc = 2; }
	}
	else if (!strcmp(c, "login") && n >= 2)           rc = cmd_login(pos[1], user);
	else if (!strcmp(c, "logout") && n >= 2)          rc = secret_forget(pos[1], "password", NULL) ? 0 : 1;
	else if (!strcmp(c, "discover") && n >= 2)        rc = cmd_discover(pos[1]);
	else if (!strcmp(c, "calendars") && n >= 2)       rc = cmd_calendars(pos[1]);
	else if (!strcmp(c, "enable") && n >= 3)          rc = cmd_set_enabled(pos[1], pos[2], true);
	else if (!strcmp(c, "disable") && n >= 3)         rc = cmd_set_enabled(pos[1], pos[2], false);
	else if (!strcmp(c, "sync"))                      rc = cmd_sync(n >= 2 ? pos[1] : NULL, policy, dry);
	else if (!strcmp(c, "events") && n >= 3)          rc = cmd_events(pos[1], pos[2]);
	else { warn("unknown command '%s'", c); usage(stderr); rc = 2; }

	http_global_cleanup();
	return rc;
}
