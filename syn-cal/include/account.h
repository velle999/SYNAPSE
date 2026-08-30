/* account.h — what an account is, and where its secret is not.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_ACCOUNT_H
#define SYNCAL_ACCOUNT_H

#include "syncal.h"

typedef enum { ACC_CALDAV, ACC_GOOGLE, ACC_MICROSOFT } acc_kind_t;

typedef struct {
	char *url;      /* the collection, absolute */
	char *name;     /* what to call it here — also the store directory */
	bool enabled;
} acc_cal_t;

typedef struct {
	char *name;             /* the short id the user chose; the store directory */
	acc_kind_t kind;
	char *url;              /* server or collection URL, as entered */
	char *user;
	/* ⚠ NOT A SECRET, DESPITE THE NAME. A desktop application is a public
	 * client: whatever client id it is issued ships in the binary on every
	 * machine that installs it. Google and Microsoft both say so, which is why
	 * PKCE exists. It lives in accounts.conf with the URL. */
	char *client_id;
	/* ⚠ ALSO NOT A SECRET, AND GOOGLE SAYS SO ITSELF. Google issues its
	 * Desktop-app clients a "client secret", requires it at the token endpoint,
	 * and documents that it is not treated as confidential for this client type
	 * — it ships in the binary beside the id. Without it the exchange fails
	 * with `invalid_request — client_secret is missing` AFTER the user has
	 * already consented, which is the most confusing place to fail. NULL for
	 * providers that refuse one; Microsoft public clients must not be sent it. */
	char *client_secret;
	/* When the access token stops working, as unix time. Also not a secret, and
	 * keeping it out of the keyring means the expiry can be read without
	 * unlocking anything. 0 means "never had one". */
	long token_expiry;
	acc_cal_t *cals;
	size_t ncals;
	bool insecure;          /* a lab server only; never written by the GUI */
} account_t;

typedef struct { account_t *e; size_t n, cap; } accounts_t;

const char *acc_kind_name(acc_kind_t k);
bool acc_kind_parse(const char *s, acc_kind_t *out);

void accounts_init(accounts_t *a);
void accounts_free(accounts_t *a);
bool accounts_load(accounts_t *a);
bool accounts_save(accounts_t *a);
account_t *accounts_find(accounts_t *a, const char *name);
account_t *accounts_add(accounts_t *a, const char *name);
bool accounts_remove(accounts_t *a, const char *name);

void acc_set_cal(account_t *acc, const char *url, const char *name, bool enabled);
acc_cal_t *acc_find_cal(account_t *acc, const char *url);

/* ── secrets ────────────────────────────────────────────────────────────────
 *
 * ⛔ EVERY STORE IS READ BACK BEFORE IT IS CALLED A SUCCESS. On a machine with
 * no keyring daemon the whole stack reports success and stores nothing — see
 * [[reference_secret_tool_exits_zero_with_no_keyring]]. For a credential store
 * that is the worst failure available: telling somebody their password is saved
 * when there is no password at all. secret_store() returns false in that case
 * and says why.
 *
 * `what` is "password" or "refresh_token": one account can hold both.
 */
bool secret_store(const char *account, const char *what, const char *value, char **err);
char *secret_fetch(const char *account, const char *what, char **err);
bool secret_forget(const char *account, const char *what, char **err);
/* Where this account's secret actually lives, in one word, for the CLI and the
 * settings pane to show: "keyring", "file" or "not set". Never the value. */
const char *secret_where(const char *account, const char *what);

#endif /* SYNCAL_ACCOUNT_H */
