/* syn-cal — the SynapseOS calendar and schedule planner.
 *
 * Same shape as synfiles and synpkg: a C core that does the work, a
 * line-oriented machine-readable output, and front-ends that are only ever
 * renderers. The GUI is quickshell parsing `syn-cal --rec`; nothing in the QML
 * knows what a CalDAV collection is, and nothing in C knows what a row looks
 * like.
 *
 * ── The store is a vdir, on purpose ─────────────────────────────────────────
 *
 * ⛔ ONE .ics PER EVENT IN A DIRECTORY PER CALENDAR — the vdir layout, which is
 * what vdirsyncer, khal and pimutils write. A private database would be less
 * code and would make this the only program on the machine that can read your
 * calendar. Anything that can read a folder of .ics files can read this one,
 * including a backup, a grep, and you.
 *
 *   ~/.local/share/syn-cal/
 *     accounts.conf                  the account list. NO SECRETS — see below.
 *     <account>/<collection>/ *.ics  the vdir. This is the data.
 *     state/<account>.<collection>   the sync index. Rebuildable, never data.
 *
 * ⛔ AND CREDENTIALS ARE NOT IN IT. Passwords and OAuth refresh tokens live in
 * the keyring through libsecret; accounts.conf holds a username and a URL. A
 * config file that carries a bearer token is one `syn-cal export`, one paste
 * into a bug report, or one backup to a shared drive away from handing somebody
 * a calendar. See [[reference_secret_tool_exits_zero_with_no_keyring]] — a store
 * that goes nowhere reads as success, so every write is read back.
 *
 * ── Sync never silently loses an edit ───────────────────────────────────────
 *
 * The engine is three-way: the server's ETag, the local file's hash, and what
 * both were at the end of the last sync. Anything else cannot tell "they
 * changed it" from "I changed it", and a two-way sync that guesses wrong
 * deletes an appointment.
 *
 * When both sides changed the same event, the default is to KEEP BOTH — the
 * remote version wins the UID and the local one is re-filed under a new UID
 * with its summary marked. Losing an edit quietly is the one failure a calendar
 * must not have, and "last writer wins" is that failure with a name.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNCAL_H
#define SYNCAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* ── output ─────────────────────────────────────────────────────────────── */

typedef enum { OUT_HUMAN, OUT_REC } out_mode_t;
extern out_mode_t g_out;
extern bool g_color;
extern bool g_verbose;

/* Percent-encode everything outside the unreserved set. Every field that can
 * carry arbitrary text — a summary, a location, an error message — goes through
 * this on the way into a --rec row, for the reason synfiles gives at length: a
 * tab inside a value does not break the row, it produces a PLAUSIBLE row
 * describing something else. Result is malloc'd. */
char *pct_encode(const char *s, bool keep_slash);
char *pct_decode(const char *s);

void rec_header(const char *fields);
void rec_row(const char *fmt, ...);

/* ── util ───────────────────────────────────────────────────────────────── */

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...);
void die(const char *fmt, ...);
void warn(const char *fmt, ...);
void info(const char *fmt, ...);          /* only when g_verbose */

/* A growable byte buffer. Used for HTTP bodies and for building .ics text. */
typedef struct { char *b; size_t len, cap; } buf_t;
void buf_init(buf_t *s);
void buf_add(buf_t *s, const void *p, size_t n);
void buf_addstr(buf_t *s, const char *p);
void buf_addf(buf_t *s, const char *fmt, ...);
void buf_free(buf_t *s);

/* Paths under the store. Result is malloc'd; the directory is created. */
char *store_root(void);
char *store_path(const char *fmt, ...);
bool ensure_dir(const char *path);

/* Read a whole file. Returns NULL and sets *len to 0 when it is not there. */
char *read_file(const char *path, size_t *len);
/* Write via a temporary in the same directory and rename over the target.
 *
 * ⚠ mkstemp, NOT a name built from the pid — see the note in synfiles'
 * thumb.c: a guessable temp beside a known target is a symlink away from being
 * written somewhere else. */
bool write_file_atomic(const char *path, const void *data, size_t len, int mode);

/* FNV-1a over the file's bytes, hex. The sync index stores it to answer "did
 * the local copy change since the last sync" without keeping a second copy. */
char *content_hash(const void *data, size_t len);

/* ── iCalendar, at the line level only (ics.c) ──────────────────────────────
 *
 * See the header of ics.c: this is deliberately not a parser. Sync moves opaque
 * blobs and needs an identity; everything that has to UNDERSTAND an event lives
 * in event.c behind libical.
 */
char *ics_unfold(const char *data, size_t len);
char *ics_prop(const char *unfolded, const char *name);
char *ics_uid(const char *data, size_t len);
char *ics_kind(const char *data, size_t len);
char *ics_replace_uid(const char *data, size_t len, const char *uid, size_t *out_len);

#endif /* SYNCAL_H */
