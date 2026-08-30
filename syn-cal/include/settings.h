/* settings.h — the handful of preferences that are syn-cal's own.
 *
 * ⛔ ONE READER, ONE WRITER, AND THE WRITER KEEPS WHAT IT DID NOT WRITE. The
 * first version of this stored week_start by rewriting settings.conf with that
 * single line in it, which was correct for exactly as long as there was one
 * setting. The second one would have been erased every time somebody ran
 * `syn-cal weekstart`, silently, and only noticed later.
 *
 * ⚠ NOT accounts.conf. That file is a list of accounts and every reader of it
 * walks sections; a preference that belongs to no account has no section to
 * live in, and inventing one would mean an account could be called [settings].
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_SETTINGS_H
#define SYNCAL_SETTINGS_H

#include "syncal.h"

/* The value for `key`, or NULL when it is not set. Caller frees. */
char *settings_get(const char *key);

/* Set or replace one key, leaving every other line of the file as it was.
 * Passing NULL removes it. */
bool settings_set(const char *key, const char *value, char **err);

#endif /* SYNCAL_SETTINGS_H */
