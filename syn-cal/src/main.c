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
#include "config.h"
#include "event.h"
#include "graph.h"
#include "month.h"
#include "oauth.h"
#include "sync.h"

#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static void usage(FILE *f)
{
	fprintf(f,
"syn-cal — the SynapseOS calendar and schedule planner\n"
"\n"
"  syn-cal accounts                     what is set up\n"
"  syn-cal account add <name> <url>     add a CalDAV account\n"
"  syn-cal account add-google <name>    add a Google account\n"
"  syn-cal account add-microsoft <name> add a Microsoft 365 account\n"
"  syn-cal account show <name>          its settings, and where its secret is\n"
"  syn-cal account remove <name>        forget it, and its password\n"
"  syn-cal login <name> [--user U]      sign in: a password prompt, or the\n"
"                                       browser for Google and Microsoft\n"
"  syn-cal logout <name>                forget just the password\n"
"  syn-cal discover <name>              ask the server which calendars exist\n"
"  syn-cal calendars <name>             list them, and which are switched on\n"
"  syn-cal enable <name> <calendar>     sync this one\n"
"  syn-cal disable <name> <calendar>    stop syncing it\n"
"  syn-cal sync [name]                  sync everything, or one account\n"
"  syn-cal gui                          the window\n"
"  syn-cal tui                          the month, in this terminal\n"
"  syn-cal agenda [--days N] [--from YYYY-MM-DD]\n"
"                                       what is on, across every calendar\n"
"  syn-cal month [--from YYYY-MM]       the month, as a grid\n"
"  syn-cal today                        just today\n"
"  syn-cal week                         the next seven days\n"
"  syn-cal events <name> <calendar>     the raw store, one calendar\n"
"\n"
"  --client-id ID   with account add-google/add-microsoft: use your own\n"
"                   OAuth project instead of the one this build ships\n"
"  --client-secret S  with account add-google: Google requires one even for a\n"
"                   desktop client, and documents it as not confidential\n"
"  --browser        with login: open the browser, whether or not this was\n"
"                   run from a terminal. --no-browser prints the URL instead.\n"
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

/* ── OAuth ──────────────────────────────────────────────────────────────── */

static const oauth_provider_t *provider_for(acc_kind_t k)
{
	if (k == ACC_GOOGLE) return oauth_google();
	if (k == ACC_MICROSOFT) return oauth_microsoft();
	return NULL;
}

static void store_tokens(accounts_t *a, account_t *e, oauth_tokens_t *t)
{
	char *err = NULL;
	if (t->refresh_token && !secret_store(e->name, "refresh_token", t->refresh_token, &err))
		warn("%s", err ? err : "the refresh token could not be stored");
	free(err); err = NULL;
	if (!secret_store(e->name, "access_token", t->access_token, &err))
		warn("%s", err ? err : "the access token could not be stored");
	free(err);

	/* ⚠ SIXTY SECONDS EARLY. A token that expires while a sync is halfway
	 * through fails partway, and the clocks involved are not the same clock. */
	e->token_expiry = (long)time(NULL) + (t->expires_in > 60 ? t->expires_in - 60 : 0);
	accounts_save(a);
}

static const char *secret_for(const account_t *e);

/* Swap the refresh token for a fresh access token, and write both down. */
static bool renew(accounts_t *a, account_t *e, char **err)
{
	const oauth_provider_t *p = provider_for(e->kind);
	if (!p) { if (err) *err = xstrdup("that account kind does not use OAuth"); return false; }
	if (!e->client_id) {
		if (err) *err = xasprintf("'%s' has no OAuth client id — see: syn-cal account add-%s --help",
		                          e->name, acc_kind_name(e->kind));
		return false;
	}

	char *refresh = secret_fetch(e->name, "refresh_token", NULL);
	if (!refresh) {
		if (err) *err = xasprintf("'%s' is not signed in — run: syn-cal login %s", e->name, e->name);
		return false;
	}

	oauth_tokens_t t;
	memset(&t, 0, sizeof t);
	bool ok = oauth_refresh(p, e->client_id, secret_for(e), refresh, &t, err);
	memset(refresh, 0, strlen(refresh));
	free(refresh);
	if (!ok) return false;

	store_tokens(a, e, &t);
	oauth_tokens_free(&t);
	return true;
}

/* ── auth for an account ────────────────────────────────────────────────── */

static bool auth_for(accounts_t *a, account_t *acc, http_auth_t *out, char **err)
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

	/* ⚠ RENEWED BEFORE IT IS USED, not after a request has already failed. An
	 * expired bearer answers 401 on every call of a sync, and a sync that
	 * half-fails is harder to reason about than one that never started. */
	if (acc->token_expiry && (long)time(NULL) >= acc->token_expiry) {
		info("the access token for '%s' has expired; renewing", acc->name);
		if (!renew(a, acc, err)) return false;
	}

	out->bearer = secret_fetch(acc->name, "access_token", NULL);
	if (!out->bearer) {
		if (err) *err = xasprintf("'%s' is not signed in — run: syn-cal login %s",
		                          acc->name, acc->name);
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
			bool oauth = e->kind != ACC_CALDAV;
			printf("               %zu of %zu calendars on, %s %s\n", on, e->ncals,
			       oauth ? "signed in:" : "password",
			       secret_where(e->name, oauth ? "access_token" : "password"));
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

/* Google's CalDAV root. Discovery walks from here with the bearer token, so
 * the account never has to be told its own email address. */
#define GOOGLE_CALDAV "https://apidata.googleusercontent.com/caldav/v2/"

/* The client id this build ships for a provider, or NULL if it ships none.
 *
 * ⚠ A PUBLIC CLIENT ID IS NOT A CREDENTIAL. Asking every person who installs a
 * calendar to open a cloud console and register an application is not a
 * security measure — it is the setup step that stops them using the calendar.
 * Google and Microsoft both class a desktop application as a public client
 * precisely so one id can ship to everyone, and PKCE is what makes an
 * intercepted redirect worthless without the verifier. --client-id stays, for
 * anyone who would rather their calendar traffic went through their own
 * project than through SynapseOS's. */
static const char *builtin_client_id(acc_kind_t kind)
{
	const char *id = kind == ACC_GOOGLE ? SYNCAL_GOOGLE_CLIENT_ID
	                                    : SYNCAL_MICROSOFT_CLIENT_ID;
	return id && *id ? id : NULL;
}

/* Google only — see the note in account.h. Microsoft public clients are refused
 * when they send one, so there is deliberately no Microsoft equivalent. */
static const char *builtin_client_secret(acc_kind_t kind)
{
	if (kind != ACC_GOOGLE) return NULL;
	const char *s = SYNCAL_GOOGLE_CLIENT_SECRET;
	return s && *s ? s : NULL;
}

/* The secret this account should send, which is not always the one written down.
 *
 * ⚠ A SHIPPED CREDENTIAL BELONGS TO THE BUILD, NOT TO THE ACCOUNT. It is copied
 * into accounts.conf when the account is added, so an account added by a build
 * that shipped no secret goes on sending none for the rest of its life — and
 * Google refuses the exchange AFTER consent, which does not read as a missing
 * field. It reads as a sign-in that hangs. An account still using the id this
 * build ships gets this build's secret, so no existing install has to be
 * removed and added again to be repaired.
 *
 * ⛔ ONLY WHEN THE ID MATCHES. Sending SynapseOS's secret alongside somebody
 * else's client id sends a credential to a project that is not ours, and fails
 * in a way nobody could read. */
static const char *secret_for(const account_t *e)
{
	if (e->client_secret && *e->client_secret) return e->client_secret;
	const char *id = builtin_client_id(e->kind);
	if (id && e->client_id && !strcmp(id, e->client_id))
		return builtin_client_secret(e->kind);
	return NULL;
}

/* Whether the sign-in should open a browser: -1 decide from the terminal, 0 no,
 * 1 yes.
 *
 * ⚠ isatty IS THE WRONG QUESTION FOR A WINDOW. It answers "was I run from a
 * terminal", and a GUI that spawns syn-cal is not — so the browser flow degrades
 * to printing a URL nobody will ever read, and the sign-in hangs until it times
 * out looking like a failure. The caller that knows a person is watching says
 * so; the terminal keeps deciding for itself. */
static int g_browser = -1;

static int cmd_account_add_oauth(const char *name, acc_kind_t kind,
                                 const char *client_id, const char *client_secret)
{
	/* ⚠ THE PAIR TRAVELS TOGETHER. Taking a caller's id while quietly keeping
	 * the shipped secret would send SynapseOS's secret to somebody else's
	 * project, which fails in a way nobody could read. */
	bool own = client_id && *client_id;
	if (!own) {
		client_id = builtin_client_id(kind);
		if (!client_secret || !*client_secret)
			client_secret = builtin_client_secret(kind);
	}
	if (!client_id || !*client_id) {
		/* Only a rebuild that compiled no id in reaches this. It is still the
		 * right answer for that build — but it is no longer what a person who
		 * installed the distribution is asked to do. */
		warn("this build ships no OAuth client id, so it needs one of yours.\n"
		     "\n"
		     "  %s: register an application of type 'Desktop app'.\n"
		     "  Google also issues a client secret for that type and its token endpoint\n"
		     "  requires it — pass it with --client-secret. Google documents that it is\n"
		     "  not confidential for an installed application; PKCE is what protects the\n"
		     "  flow. Microsoft wants no secret from a public client.\n"
		     "\n"
		     "  syn-cal account add-%s %s --client-id <the id>",
		     kind == ACC_GOOGLE
		       /* ⛔ THE CalDAV API, NOT THE CALENDAR API. They are two products
		        * in the console and this client speaks CalDAV; a project with
		        * only the Calendar API enabled signs in perfectly and then
		        * answers 403 accessNotConfigured to everything, naming the one
		        * that is off. */
		       ? "https://console.cloud.google.com/apis/api/caldav.googleapis.com — enable the CalDAV API"
		       : "https://portal.azure.com — App registrations, and grant Calendars.ReadWrite",
		     acc_kind_name(kind), name);
		return 2;
	}

	accounts_t a;
	accounts_load(&a);
	if (accounts_find(&a, name)) { warn("there is already an account called '%s'", name); accounts_free(&a); return 1; }

	account_t *e = accounts_add(&a, name);
	e->kind = kind;
	e->client_id = xstrdup(client_id);
	if (client_secret && *client_secret) e->client_secret = xstrdup(client_secret);
	if (kind == ACC_GOOGLE) e->url = xstrdup(GOOGLE_CALDAV);
	bool ok = accounts_save(&a);
	accounts_free(&a);
	if (!ok) { warn("could not write accounts.conf"); return 1; }

	if (g_out != OUT_REC)
		printf("Added '%s'.\n\n  syn-cal login %s\n", name, name);
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

	const oauth_provider_t *p = provider_for(e->kind);
	if (p) {
		/* ⚠ THE BROWSER, NOT A PASSWORD PROMPT. Google and Microsoft both
		 * refuse a password from a program, and asking for one anyway is how
		 * an application teaches people to type their password into things. */
		oauth_tokens_t t;
		char *err = NULL;
		bool open_browser = g_browser >= 0 ? g_browser != 0 : isatty(STDERR_FILENO);
		bool ok = oauth_authorise(p, e->client_id, secret_for(e),
		                          open_browser, 0, &t, &err);
		if (!ok) {
			warn("%s", err ? err : "sign-in did not complete");
			free(err);
			accounts_free(&a);
			return 1;
		}
		if (!t.refresh_token)
			warn("the provider returned no refresh token, so this will need signing in "
			     "again when it expires");
		store_tokens(&a, e, &t);
		oauth_tokens_free(&t);
		if (g_out != OUT_REC)
			printf("Signed in. Token: %s\n", secret_where(name, "access_token"));
		accounts_free(&a);
		return 0;
	}

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
	if (!auth_for(&a, e, &auth, &err)) { warn("%s", err); free(err); accounts_free(&a); return 1; }

	caldav_colls_t colls;
	/* One kind of answer either way — see graph_discover. The account's kind is
	 * what decides, not the URL, because a Microsoft account has no URL of its
	 * own to look at. */
	bool ok = (e->kind == ACC_MICROSOFT)
	        ? graph_discover(&auth, &colls, &err)
	        : caldav_discover(e->url, &auth, &colls, &err);
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

static int sync_account(accounts_t *a, account_t *e, conflict_t policy, bool dry, sync_stats_t *tot)
{
	http_auth_t auth;
	char *err = NULL;
	if (!auth_for(a, e, &auth, &err)) { warn("%s", err); free(err); return 1; }

	int bad = 0;
	for (size_t i = 0; i < e->ncals; i++) {
		if (!e->cals[i].enabled) continue;
		const char *label = e->cals[i].name ? e->cals[i].name : e->cals[i].url;

		/* ⛔ THE BACKEND FOLLOWS THE ACCOUNT, NOT THE URL. Microsoft removed
		 * CalDAV; a Graph calendar URL handed to the CalDAV client answers 404
		 * to PROPFIND, which reads as "my account is broken". */
		remote_t *r = (e->kind == ACC_MICROSOFT)
		            ? graph_remote(e->cals[i].url, &auth)
		            : caldav_remote(e->cals[i].url, &auth);
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
		if (e->kind == ACC_MICROSOFT) graph_remote_free(r);
		else caldav_remote_free(r);
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
		bad |= sync_account(&a, &a.e[i], policy, dry, &tot);
	}

	if (only && ran == 0) { warn("no account called '%s'", only); accounts_free(&a); return 1; }

	if (g_out != OUT_REC) {
		unsigned moved = tot.pulled_new + tot.pulled_changed + tot.pushed_new + tot.pushed_changed;
		if (dry) printf("Dry run: %u event%s would move.\n", moved, moved == 1 ? "" : "s");
		/* ⛔ NOT AFTER A FAILURE. "Already up to date" is the most reassuring
		 * possible way to say nothing happened, and saying it when an account
		 * could not be reached is how a calendar quietly stops updating. */
		else if (bad) printf("Some calendars did not sync — see the messages above.\n");
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
		char *data = read_file(l.e[i].path, &len);
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

/* ── the agenda ─────────────────────────────────────────────────────────── */

static int cmd_agenda(int days, const char *from_date)
{
	/* From the start of a day in LOCAL time, not from this instant: somebody
	 * asking at 4pm what is on wants the whole day, including the 9am they
	 * missed. */
	time_t now = time(NULL);
	struct tm lt;
	localtime_r(&now, &lt);

	if (from_date) {
		/* ⚠ THE FIELDS NOT PARSED ARE LEFT AS TODAY'S, then overwritten. tm has
		 * members mktime reads that scanf never sets — tm_isdst above all, and
		 * getting that wrong moves the whole range by an hour across a clock
		 * change. Starting from a localtime_r of now means every one of them is
		 * already a coherent value for this machine. */
		int y = 0, m = 0, d = 0;
		if (sscanf(from_date, "%d-%d-%d", &y, &m, &d) != 3 ||
		    m < 1 || m > 12 || d < 1 || d > 31) {
			warn("--from wants a date like 2026-09-01");
			return 2;
		}
		lt.tm_year = y - 1900;
		lt.tm_mon = m - 1;
		lt.tm_mday = d;
		lt.tm_isdst = -1;          /* let mktime work out the offset for THAT day */
	}

	lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
	time_t from = mktime(&lt);
	if (from == (time_t)-1) { warn("--from: that is not a date this machine can represent"); return 2; }
	time_t to = from + (time_t)days * 86400;

	events_t l;
	char *err = NULL;
	if (!agenda_range(from, to, &l, &err)) {
		warn("%s", err ? err : "could not read the calendars");
		free(err);
		return 1;
	}

	if (g_out == OUT_REC) {
		rec_header("start\tend\tall_day\trecurring\taccount\tcalendar\tsummary\tlocation\tuid");
		for (size_t i = 0; i < l.n; i++) {
			event_t *e = &l.e[i];
			char *sum = pct_encode(e->summary ? e->summary : "", false);
			char *loc = pct_encode(e->location ? e->location : "", false);
			char *acc = pct_encode(e->account ? e->account : "", false);
			char *cal = pct_encode(e->calendar ? e->calendar : "", false);
			char *uid = pct_encode(e->uid ? e->uid : "", false);
			rec_row("%ld\t%ld\t%d\t%d\t%s\t%s\t%s\t%s\t%s",
			        (long)e->start, (long)e->end, e->all_day, e->recurring,
			        acc, cal, sum, loc, uid);
			free(sum); free(loc); free(acc); free(cal); free(uid);
		}
		events_free(&l);
		return 0;
	}

	if (l.n == 0) {
		printf("Nothing in the next %d day%s.\n", days, days == 1 ? "" : "s");
		events_free(&l);
		return 0;
	}

	char lastday[16] = "";
	for (size_t i = 0; i < l.n; i++) {
		event_t *e = &l.e[i];
		struct tm st;
		localtime_r(&e->start, &st);

		char day[16];
		strftime(day, sizeof day, "%Y-%m-%d", &st);
		if (strcmp(day, lastday) != 0) {
			char head[64];
			strftime(head, sizeof head, "%A %e %B", &st);
			printf("%s%s\n", i ? "\n" : "", head);
			snprintf(lastday, sizeof lastday, "%s", day);
		}

		if (e->all_day) {
			printf("  all day   %s", e->summary ? e->summary : "(no summary)");
		} else {
			char t[8];
			strftime(t, sizeof t, "%H:%M", &st);
			printf("  %s     %s", t, e->summary ? e->summary : "(no summary)");
		}
		if (e->location && *e->location) printf("  — %s", e->location);
		if (e->cancelled) printf("  (cancelled)");
		putchar('\n');
	}

	events_free(&l);
	return 0;
}

/* ── the window ─────────────────────────────────────────────────────────── */

/* quickshell rendering data/syn-cal.qml in a separate process, for the same
 * reason synfiles, synpkg and syn-disks do it that way: the binary stays usable
 * over SSH, and the window consumes exactly the records any other consumer
 * would. */
static int cmd_gui(void)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY"))
		die("no display — syn-cal gui needs a graphical session");

	/* ⚠ CHECKED BEFORE exec, so the message names what is missing. execvp
	 * failing leaves "could not start quickshell", which is true and useless. */
	if (access("/usr/bin/quickshell", X_OK) != 0 &&
	    access("/usr/local/bin/quickshell", X_OK) != 0)
		die("quickshell is not installed — synpkg install quickshell");

	/* ⛔ THE WINDOW'S WAYLAND app_id, AND OVERWRITTEN RATHER THAN MERELY SET.
	 * Without it quickshell names every one of its windows "org.quickshell",
	 * which is the generic icon in the dock and the reason the dock cannot find
	 * a .desktop for the window. An INHERITED value is the real accident: these
	 * apps hand their whole environment to what they spawn, so a calendar
	 * opened from another quickshell app would otherwise take that app's
	 * identity and have no dock entry of its own. */
	setenv("QS_APP_ID", "syn-cal", 1);

	const char *qml = SYNCAL_DATADIR "/syn-cal.qml";
	if (access(qml, R_OK) != 0 && access("data/syn-cal.qml", R_OK) == 0)
		qml = "data/syn-cal.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die("could not start quickshell");
	return 1;
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
	const char *user = NULL;
	const char *client_id = NULL;
	const char *client_secret = NULL;
	int days = 7;
	const char *from_date = NULL;
	conflict_t policy = CONFLICT_KEEP_BOTH;
	bool dry = false;

	/* ⚠ COLOUR IS A PROPERTY OF THE OUTPUT, NOT OF THE BUILD, and it is decided
	 * once here so every command that prints inherits the same answer. Piped
	 * into a file or a pager there are no escapes to strip, and NO_COLOR is
	 * honoured because somebody who sets it means it. --rec turns it off again
	 * below whatever this says. */
	g_color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");

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
		if (!strncmp(v, "--days=", 7)) { days = atoi(v + 7); if (days < 1) days = 1; continue; }
		if (!strncmp(v, "--from=", 7)) { from_date = v + 7; continue; }
		if (!strcmp(v, "--browser"))    { g_browser = 1; continue; }
		if (!strcmp(v, "--no-browser")) { g_browser = 0; continue; }
		if (!strncmp(v, "--client-id=", 12)) { client_id = v + 12; continue; }
		if (!strcmp(v, "--client-id") && i + 1 < argc) { client_id = argv[++i]; continue; }
		if (!strncmp(v, "--client-secret=", 16)) { client_secret = v + 16; continue; }
		if (!strcmp(v, "--client-secret") && i + 1 < argc) { client_secret = argv[++i]; continue; }
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
		else if (!strcmp(pos[1], "add-google") && n >= 3)
			rc = cmd_account_add_oauth(pos[2], ACC_GOOGLE, client_id, client_secret);
		else if (!strcmp(pos[1], "add-microsoft") && n >= 3)
			rc = cmd_account_add_oauth(pos[2], ACC_MICROSOFT, client_id, client_secret);
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
	else if (!strcmp(c, "gui"))                       rc = cmd_gui();
	else if (!strcmp(c, "tui"))                       rc = cmd_tui();
	else if (!strcmp(c, "agenda"))                    rc = cmd_agenda(days, from_date);
	else if (!strcmp(c, "month"))                     rc = cmd_month(from_date);
	else if (!strcmp(c, "today"))                     rc = cmd_agenda(1, NULL);
	else if (!strcmp(c, "week"))                      rc = cmd_agenda(7, NULL);
	else { warn("unknown command '%s'", c); usage(stderr); rc = 2; }

	http_global_cleanup();
	return rc;
}
