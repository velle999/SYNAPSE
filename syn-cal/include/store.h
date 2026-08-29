/* store.h — the vdir and the sync index. See store.c for why they are separate.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_STORE_H
#define SYNCAL_STORE_H

#include "syncal.h"

/* What the two sides agreed on at the end of the last sync. */
typedef struct {
	char *uid;      /* the identity, from inside the .ics */
	char *href;     /* where the server keeps it, relative to the collection */
	char *etag;     /* the server's version then */
	char *hash;     /* the local file's content hash then */
} idx_entry_t;

typedef struct { idx_entry_t *e; size_t n, cap; char *path; } index_t;

void idx_init(index_t *ix);
void idx_free(index_t *ix);
bool idx_load(index_t *ix, const char *account, const char *coll);
bool idx_save(index_t *ix);
idx_entry_t *idx_find(index_t *ix, const char *uid);
idx_entry_t *idx_find_href(index_t *ix, const char *href);
void idx_set(index_t *ix, const char *uid, const char *href,
             const char *etag, const char *hash);
void idx_remove(index_t *ix, const char *uid);

/* What is on disk right now. */
typedef struct { char *uid; char *hash; } local_item_t;
typedef struct { local_item_t *e; size_t n, cap; } local_list_t;

void local_init(local_list_t *l);
void local_free(local_list_t *l);
bool local_scan(const char *account, const char *coll, local_list_t *out);
local_item_t *local_find(local_list_t *l, const char *uid);

char *local_read(const char *account, const char *coll, const char *uid, size_t *len);
bool local_write(const char *account, const char *coll, const char *uid,
                 const void *data, size_t len);
bool local_delete(const char *account, const char *coll, const char *uid);

char *coll_dir(const char *account, const char *coll);
char *item_path(const char *account, const char *coll, const char *uid);

#endif /* SYNCAL_STORE_H */
