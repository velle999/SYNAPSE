/* sync.h — the three-way engine. See sync.c for the rules it follows.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_SYNC_H
#define SYNCAL_SYNC_H

#include "remote.h"
#include "store.h"

/* What to do when both sides changed the same event since the last sync.
 *
 * KEEP_BOTH is the default and the only one that cannot lose an edit: the
 * remote version keeps the UID, and the local one is re-filed under a new UID
 * so it is still there to look at. "Last writer wins" is data loss with a
 * reassuring name — the writer who lost is not told. */
typedef enum { CONFLICT_KEEP_BOTH, CONFLICT_REMOTE_WINS, CONFLICT_LOCAL_WINS } conflict_t;

typedef struct {
	unsigned pulled_new, pulled_changed, pulled_deleted;
	unsigned pushed_new, pushed_changed, pushed_deleted;
	unsigned conflicts, skipped, errors;
} sync_stats_t;

typedef struct {
	const char *account;
	const char *collection;
	conflict_t on_conflict;
	bool dry_run;          /* decide everything, change nothing, report it all */
} sync_opts_t;

bool sync_run(remote_t *r, const sync_opts_t *o, sync_stats_t *st, char **err);

#endif /* SYNCAL_SYNC_H */
