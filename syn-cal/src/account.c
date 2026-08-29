/* account.c — the account list, and the keyring it deliberately does not hold.
 *
 * ── accounts.conf carries no secret ─────────────────────────────────────────
 *
 * ⛔ A URL AND A USERNAME, AND NOTHING ELSE. A config file that carries a
 * password or a bearer token is one `cat` into a bug report, one backup to a
 * shared drive, or one screen-share away from handing somebody a calendar —
 * which is a record of where you are, when, and with whom. Secrets go to the
 * keyring through libsecret.
 *
 * ── And a keyring that is not there says so ─────────────────────────────────
 *
 * ⛔ EVERY STORE IS READ BACK. On a machine with no keyring daemon the whole
 * stack — libsecret, secret-tool, D-Bus — reports success and stores nothing.
 * A credential store that says "saved" when it saved nothing is worse than one
 * that refuses, because the failure surfaces later as an authentication error
 * that looks like a wrong password.
 *
 * Where there is genuinely no keyring, the fallback is a 0600 file under the
 * store and the user is TOLD, in those words. A silent fallback would be the
 * same lie with an extra step.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "account.h"

#include <errno.h>
#include <libsecret/secret.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── kinds ──────────────────────────────────────────────────────────────── */

const char *acc_kind_name(acc_kind_t k)
{
	switch (k) {
	case ACC_GOOGLE:    return "google";
	case ACC_MICROSOFT: return "microsoft";
	default:            return "caldav";
	}
}

bool acc_kind_parse(const char *s, acc_kind_t *out)
{
	if (!s) return false;
	if (!strcmp(s, "caldav"))    { *out = ACC_CALDAV;    return true; }
	if (!strcmp(s, "google"))    { *out = ACC_GOOGLE;    return true; }
	if (!strcmp(s, "microsoft")) { *out = ACC_MICROSOFT; return true; }
	return false;
}

/* ── the list ───────────────────────────────────────────────────────────── */

void accounts_init(accounts_t *a) { a->e = NULL; a->n = a->cap = 0; }

static void account_clear(account_t *acc)
{
	free(acc->name); free(acc->url); free(acc->user);
	for (size_t i = 0; i < acc->ncals; i++) { free(acc->cals[i].url); free(acc->cals[i].name); }
	free(acc->cals);
	memset(acc, 0, sizeof *acc);
}

void accounts_free(accounts_t *a)
{
	for (size_t i = 0; i < a->n; i++) account_clear(&a->e[i]);
	free(a->e);
	accounts_init(a);
}

account_t *accounts_find(accounts_t *a, const char *name)
{
	for (size_t i = 0; i < a->n; i++)
		if (strcmp(a->e[i].name, name) == 0) return &a->e[i];
	return NULL;
}

account_t *accounts_add(accounts_t *a, const char *name)
{
	account_t *e = accounts_find(a, name);
	if (e) return e;
	if (a->n == a->cap) {
		a->cap = a->cap ? a->cap * 2 : 8;
		a->e = xrealloc(a->e, a->cap * sizeof *a->e);
	}
	e = &a->e[a->n++];
	memset(e, 0, sizeof *e);
	e->name = xstrdup(name);
	return e;
}

bool accounts_remove(accounts_t *a, const char *name)
{
	for (size_t i = 0; i < a->n; i++) {
		if (strcmp(a->e[i].name, name) != 0) continue;
		account_clear(&a->e[i]);
		memmove(&a->e[i], &a->e[i + 1], (a->n - i - 1) * sizeof *a->e);
		a->n--;
		return true;
	}
	return false;
}

acc_cal_t *acc_find_cal(account_t *acc, const char *url)
{
	for (size_t i = 0; i < acc->ncals; i++)
		if (strcmp(acc->cals[i].url, url) == 0) return &acc->cals[i];
	return NULL;
}

void acc_set_cal(account_t *acc, const char *url, const char *name, bool enabled)
{
	acc_cal_t *c = acc_find_cal(acc, url);
	if (!c) {
		acc->cals = xrealloc(acc->cals, (acc->ncals + 1) * sizeof *acc->cals);
		c = &acc->cals[acc->ncals++];
		memset(c, 0, sizeof *c);
		c->url = xstrdup(url);
	}
	if (name) { free(c->name); c->name = xstrdup(name); }
	c->enabled = enabled;
}

/* ── the file ───────────────────────────────────────────────────────────── */

/* One `key = value` per line under a `[name]` header — synui's own config
 * shape, so there is one syntax to learn on this system rather than two.
 * Values are percent-encoded: a calendar can legitimately be called
 * "Sam & Jo = holidays". */
bool accounts_load(accounts_t *a)
{
	accounts_init(a);
	char *path = store_path("accounts.conf");
	size_t len = 0;
	char *text = read_file(path, &len);
	free(path);
	if (!text) return true;

	account_t *cur = NULL;
	char *save = NULL;
	for (char *line = strtok_r(text, "\n", &save); line;
	     line = strtok_r(NULL, "\n", &save)) {
		while (*line == ' ' || *line == '\t') line++;
		if (!*line || *line == '#') continue;

		if (*line == '[') {
			char *end = strchr(line, ']');
			if (!end) continue;
			*end = '\0';
			char *nm = pct_decode(line + 1);
			cur = accounts_add(a, nm);
			free(nm);
			continue;
		}
		if (!cur) continue;

		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = line;
		char *val = eq + 1;
		while (*val == ' ') val++;
		for (char *e = key + strlen(key); e > key && (e[-1] == ' ' || e[-1] == '\t'); e--) e[-1] = '\0';

		char *v = pct_decode(val);
		if (!strcmp(key, "kind")) acc_kind_parse(v, &cur->kind);
		else if (!strcmp(key, "url")) { free(cur->url); cur->url = xstrdup(v); }
		else if (!strcmp(key, "user")) { free(cur->user); cur->user = xstrdup(v); }
		else if (!strcmp(key, "insecure")) cur->insecure = !strcmp(v, "yes");
		else if (!strcmp(key, "calendar")) {
			/* <on|off> <url> <name> — the name may contain spaces, the url
			 * may not, and both are already percent-encoded above. */
			char *sp1 = strchr(v, ' ');
			if (!sp1) { free(v); continue; }
			*sp1 = '\0';
			char *sp2 = strchr(sp1 + 1, ' ');
			bool on = !strcmp(v, "on");
			if (sp2) { *sp2 = '\0'; acc_set_cal(cur, sp1 + 1, sp2 + 1, on); }
			else acc_set_cal(cur, sp1 + 1, sp1 + 1, on);
		}
		free(v);
	}
	free(text);
	return true;
}

bool accounts_save(accounts_t *a)
{
	buf_t out;
	buf_init(&out);
	buf_addstr(&out,
	    "# syn-cal accounts.\n"
	    "#\n"
	    "# NO PASSWORDS AND NO TOKENS LIVE HERE — they are in the keyring.\n"
	    "# Values are percent-encoded so a calendar may be called anything.\n"
	    "# `syn-cal account` is the supported way to edit this.\n");

	for (size_t i = 0; i < a->n; i++) {
		account_t *e = &a->e[i];
		char *nm = pct_encode(e->name, false);
		buf_addf(&out, "\n[%s]\n", nm);
		free(nm);
		buf_addf(&out, "kind = %s\n", acc_kind_name(e->kind));
		if (e->url)  { char *v = pct_encode(e->url, true);  buf_addf(&out, "url = %s\n", v);  free(v); }
		if (e->user) { char *v = pct_encode(e->user, false); buf_addf(&out, "user = %s\n", v); free(v); }
		if (e->insecure) buf_addstr(&out, "insecure = yes\n");
		for (size_t c = 0; c < e->ncals; c++) {
			char *u = pct_encode(e->cals[c].url, true);
			char *n = pct_encode(e->cals[c].name ? e->cals[c].name : e->cals[c].url, false);
			buf_addf(&out, "calendar = %s %s %s\n", e->cals[c].enabled ? "on" : "off", u, n);
			free(u); free(n);
		}
	}

	char *root = store_root();
	bool ok = ensure_dir(root);
	free(root);
	char *path = store_path("accounts.conf");
	if (ok) ok = write_file_atomic(path, out.b, out.len, 0600);
	free(path);
	buf_free(&out);
	return ok;
}

/* ── secrets ────────────────────────────────────────────────────────────── */

/* ⚠ BUILT BY memset RATHER THAN BY AN INITIALISER LIST. SecretSchema ends in a
 * `reserved` field and seven reserved pointers, so a brace initialiser is a
 * -Wmissing-field-initializers warning that can only be silenced by writing out
 * eight members that exist to be ignored — and which libsecret is free to
 * rename. Zeroing the whole thing says the same and keeps saying it. */
static const SecretSchema *syncal_schema(void)
{
	static SecretSchema s;
	static bool built = false;
	if (!built) {
		memset(&s, 0, sizeof s);
		s.name = "org.synapseos.syn-cal";
		s.flags = SECRET_SCHEMA_NONE;
		s.attributes[0].name = "account";
		s.attributes[0].type = SECRET_SCHEMA_ATTRIBUTE_STRING;
		s.attributes[1].name = "what";
		s.attributes[1].type = SECRET_SCHEMA_ATTRIBUTE_STRING;
		built = true;
	}
	return &s;
}

/* The 0600 file the fallback uses. Under state/, never in the vdir, and never
 * written without telling the user it happened. */
static char *secret_file(const char *account, const char *what)
{
	char *a = pct_encode(account, false);
	char *w = pct_encode(what, false);
	char *p = store_path("state/secret.%s.%s", a, w);
	free(a); free(w);
	return p;
}

static bool keyring_store(const char *account, const char *what, const char *value)
{
	GError *e = NULL;
	char *label = g_strdup_printf("syn-cal: %s (%s)", account, what);
	gboolean ok = secret_password_store_sync(syncal_schema(), SECRET_COLLECTION_DEFAULT,
	                                         label, value, NULL, &e,
	                                         "account", account, "what", what, NULL);
	g_free(label);
	if (e) { g_error_free(e); return false; }
	return ok;
}

static char *keyring_fetch(const char *account, const char *what)
{
	GError *e = NULL;
	gchar *v = secret_password_lookup_sync(syncal_schema(), NULL, &e,
	                                       "account", account, "what", what, NULL);
	if (e) { g_error_free(e); return NULL; }
	if (!v) return NULL;
	char *out = xstrdup(v);
	secret_password_free(v);
	return out;
}

bool secret_store(const char *account, const char *what, const char *value, char **err)
{
	if (err) *err = NULL;

	if (keyring_store(account, what, value)) {
		/* ⛔ READ IT BACK. This is the whole point: the store above returns TRUE
		 * on a machine where D-Bus accepted the call and no keyring exists to
		 * carry it out. The only proof a secret was saved is finding it. */
		char *back = keyring_fetch(account, what);
		bool good = back && strcmp(back, value) == 0;
		if (back) { memset(back, 0, strlen(back)); free(back); }
		if (good) return true;
	}

	char *dir = store_path("state");
	bool ok = ensure_dir(dir);
	free(dir);
	char *path = secret_file(account, what);
	if (ok) ok = write_file_atomic(path, value, strlen(value), 0600);

	if (!ok) {
		if (err) *err = xasprintf("no keyring answered, and %s could not be written either", path);
		free(path);
		return false;
	}

	/* ⚠ SAID OUT LOUD, ALWAYS. A silent fallback to a file on disk is the same
	 * lie as a silent failure, one step later — the user believes the secret is
	 * in a keyring and it is in their backups. */
	warn("no keyring is running, so this secret went to %s (readable only by you).\n"
	     "         Start a keyring — gnome-keyring or kwallet — and set it again to move it.",
	     path);
	free(path);
	return true;
}

char *secret_fetch(const char *account, const char *what, char **err)
{
	if (err) *err = NULL;
	char *v = keyring_fetch(account, what);
	if (v) return v;

	char *path = secret_file(account, what);
	size_t len = 0;
	char *data = read_file(path, &len);
	free(path);
	if (!data) return NULL;
	/* A trailing newline is not part of a password, and one gets in whenever
	 * somebody writes the file by hand. */
	while (len && (data[len - 1] == '\n' || data[len - 1] == '\r')) data[--len] = '\0';
	return data;
}

bool secret_forget(const char *account, const char *what, char **err)
{
	if (err) *err = NULL;
	GError *e = NULL;
	secret_password_clear_sync(syncal_schema(), NULL, &e,
	                           "account", account, "what", what, NULL);
	if (e) g_error_free(e);

	char *path = secret_file(account, what);
	bool ok = (unlink(path) == 0 || errno == ENOENT);
	free(path);
	return ok;
}

const char *secret_where(const char *account, const char *what)
{
	char *v = keyring_fetch(account, what);
	if (v) { memset(v, 0, strlen(v)); free(v); return "keyring"; }

	char *path = secret_file(account, what);
	struct stat st;
	bool onfile = stat(path, &st) == 0;
	free(path);
	return onfile ? "file" : "not set";
}
