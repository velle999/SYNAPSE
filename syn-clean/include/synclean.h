/* synclean.h — reclaiming disk space, and destroying files on purpose.
 *
 * ── Two jobs, and they are not the same job ──────────────────────────────────
 *
 * `scan`/`clean` free space: caches, thumbnails, trash, orphaned packages. The
 * files are junk, nobody wants them back, and ordinary unlink is right.
 *
 * `shred` destroys a file somebody chose. That is a different promise, and this
 * program is careful about how much of it it makes — see below.
 *
 * ── ⛔ SECURE DELETE ON A COPY-ON-WRITE FILESYSTEM IS NOT WHAT IT SOUNDS LIKE ─
 *
 * Overwriting a file's bytes only destroys the old contents if the new bytes
 * land on the same physical blocks. On ext4 they usually do. On btrfs — which
 * is what SynapseOS installs on, and what / and /home are here — they DO NOT:
 * copy-on-write allocates new blocks for the write and leaves the originals
 * intact until they are reused, which may be never. The same is true of zfs.
 *
 * It gets worse with snapshots. A btrfs subvolume under snapper (SynapseOS sets
 * this up: /.snapshots) has read-only copies of the file that this program
 * cannot touch, must not touch, and which contain the data in full.
 *
 * And underneath all of it, an SSD's controller does its own remapping. A block
 * the filesystem overwrote is not necessarily the block the flash wrote to;
 * wear levelling and over-provisioning keep old copies out of reach of any
 * software, which is why `shred(1)`'s own man page carries this caveat.
 *
 * ⇒ SO THIS PROGRAM SAYS WHAT IT DID, NOT WHAT IT ACHIEVED. It overwrites, it
 * renames, it unlinks, and it TELLS THE USER when the filesystem it just did
 * that on cannot honour it — including naming the snapshots that still hold a
 * copy. A "securely deleted" message on btrfs would be a lie, and the person
 * who believes it is the person who most needed the truth.
 *
 * The honest advice, which the program gives: full-disk encryption is what
 * makes a deleted file unreadable. Destroying one file after the fact is a
 * best-effort.
 *
 * ── What it will not touch ───────────────────────────────────────────────────
 *
 * ⛔ NOTHING OUTSIDE $HOME IS EVER REMOVED WITHOUT root_ok BEING ASKED FOR, and
 * the categories that need root say so rather than half-working. A cleaner that
 * silently skips what it listed is a cleaner that reports freeing space it did
 * not free.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCLEAN_H
#define SYNCLEAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

/* ── output ─────────────────────────────────────────────────────────────── */

typedef enum { OUT_HUMAN, OUT_REC } out_mode_t;
extern out_mode_t g_out;

/* Set by --dry-run. Everything that would remove something checks it. */
extern bool g_dry;
/* Set by --yes. Without it, anything destructive asks. */
extern bool g_yes;

void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void die(const char *fmt, ...)  __attribute__((format(printf, 1, 2), noreturn));
void rec_header(const char *fields);
void rec_row(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *pct_encode(const char *s);
char *home_path(const char *rel);          /* $SYNCLEAN_HOME or $HOME + rel */
void  human_size(unsigned long long b, char *out, size_t n);

/* ── what there is to clean ─────────────────────────────────────────────── */

typedef struct {
	const char *id;         /* stable, what the CLI and the GUI both name */
	const char *label;
	const char *what;       /* one line, for a person */
	bool        needs_root;
	/* ⚠ THE PROGRAMS THAT MUST NOT BE RUNNING, as a space-separated list.
	 * Cookie and cache databases are sqlite; removing one under a live browser
	 * leaves its -wal and -shm behind pointing at a database that is gone, and
	 * the next start is a profile the browser cannot read. One name is not
	 * enough: this category covers Firefox AND the Chromium family, so the
	 * check has to name every browser whose files it would take. */
	const char *conflicts;
	bool        loses_logins;
} category_t;

extern const category_t g_categories[];
extern const size_t     g_ncategories;

const category_t *category_find(const char *id);

/* The conflicting process that is running, or NULL. Caller must not free. */
const char *category_blocked_by(const category_t *c);

/* Bytes this category would free, and how many files. Never removes. */
int category_measure(const category_t *c, unsigned long long *bytes,
                     unsigned long long *files);

/* Removes it. Honours g_dry. */
int category_clean(const category_t *c, unsigned long long *freed);

int cmd_scan(int nsel, char **sel);
int cmd_clean(int nsel, char **sel);
int cmd_gui(void);

/* ── destroying one file on purpose ─────────────────────────────────────── */

/* What the filesystem under `path` can actually honour. */
typedef struct {
	char fstype[32];
	bool cow;               /* btrfs, zfs — overwrite-in-place is a no-op */
	bool snapshots;         /* and read-only copies exist */
} shred_ground_t;

void shred_ground(const char *path, shred_ground_t *g);

/* Overwrite, rename, unlink. `passes` is how many overwrites.
 * ⚠ Returns 0 when it did what it could, which is NOT the same as "the data is
 * gone" — see the header comment. The caller reports the difference. */
int shred_path(const char *path, int passes, unsigned long long *bytes);

int cmd_shred(int npaths, char **paths, int passes);

#endif /* SYNCLEAN_H */
