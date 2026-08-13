/* synfiles — the SynapseOS file browser.
 *
 * Same shape as synpkg, for the same reasons: a C core that does the work, a
 * line-oriented machine-readable output, and front-ends that are only ever
 * renderers. The GUI is quickshell parsing `synfiles --rec`; nothing in the
 * QML knows how to stat a file, and nothing in C knows what a row looks like.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNFILES_H
#define SYNFILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ── Output ─────────────────────────────────────────────────────────────────
 *
 * synpkg emits TSV and strips tabs and newlines out of every field. That is
 * correct for a package description and CATASTROPHIC for a filename.
 *
 * A POSIX filename is any sequence of bytes except '\0' and '/'. Tabs,
 * newlines, quotes, escape sequences and invalid UTF-8 are all legal, and a
 * file called "notes\treceipts" is a real file somebody can create by
 * accident. Stripping the tab out of it does not produce a broken row — it
 * produces a PLAUSIBLE row naming a different file, and the next thing the
 * GUI does with that name is hand it back to be renamed or deleted.
 *
 * So every field that carries a filename or a path is PERCENT-ENCODED on the
 * way out. That is the same encoding already used by file:// URIs, by the
 * XDG trash spec's Path= field, and by the XBEL bookmark files this program
 * reads, so it is the domain's own convention rather than a local invention.
 *
 * The rule that matters, and the one to check in any review of this code:
 *
 *   THE ENCODED FORM IS THE IDENTITY. The decoded form is for display only
 *   and must never be handed back to any function that touches the disk.
 *
 * A GUI that decodes a name to show it and then passes the decoded string to
 * `synfiles remove` has reintroduced the whole bug, silently, for exactly the
 * filenames that are hardest to test with.
 */
typedef enum { OUT_HUMAN, OUT_REC } out_mode_t;

extern out_mode_t g_out;
extern bool g_color;
extern bool g_verbose;

/* Percent-encode `s`. Everything outside the unreserved set (A-Z a-z 0-9 - . _
 * ~) becomes %XX, so the result is pure ASCII with no tab, no newline and no
 * byte a field separator could collide with. `keep_slash` leaves '/' literal,
 * which keeps a full path readable in the output of a command somebody is
 * debugging by eye; a bare filename never contains one. Result is malloc'd. */
char *pct_encode(const char *s, bool keep_slash);

/* Inverse. Invalid escapes are passed through verbatim rather than guessed at.
 * Result is malloc'd and may hold any byte except '\0'. */
char *pct_decode(const char *s);

/* Emit one record: fields joined by tab, terminated by newline.
 *
 * Control bytes are stripped from every field as a BACKSTOP, not as the
 * primary defence — a field carrying a name is expected to arrive already
 * percent-encoded, at which point the strip has nothing left to do. If this
 * ever starts mattering for a name field, the bug is upstream of here. */
void rec_row(int nfields, ...);

/* ── util.c ─────────────────────────────────────────────────────────────── */
void  *xmalloc(size_t n);
void  *xrealloc(void *p, size_t n);
char  *xstrdup(const char *s);
char  *xstrndup(const char *s, size_t n);
char  *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void   die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
void   warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

const char *C_RESET(void);
const char *C_BOLD(void);
const char *C_DIM(void);
const char *C_ACCENT(void);
const char *C_WARN(void);

char **split(char *text, char sep, size_t *n);
void   strip_trailing_newline(char *s);
char  *human_size(off_t bytes);
bool   have_cmd(const char *name);
char  *slurp(const char *path);           /* whole file, malloc'd, NULL on error */
char  *run_capture(char *const argv[], int *status, bool quiet_stderr);

/* $HOME, or die. Everything this program reads per-user hangs off it, and a
 * process with no HOME is not a state worth half-supporting. */
const char *home_dir(void);
/* $XDG_DATA_HOME, defaulting to ~/.local/share. malloc'd. */
char *xdg_data_home(void);

/* ── mime.c ─────────────────────────────────────────────────────────────────
 * Glob matching against /usr/share/mime/globs2, the database
 * shared-mime-info already ships and update-mime-database already maintains.
 * Deliberately NOT libmagic: content sniffing means opening every file in a
 * directory listing, and a listing that reads 4000 files to draw 4000 icons is
 * a listing that hangs on a network share. */
const char *mime_for(const char *name, bool is_dir);
/* The freedesktop icon NAME for a mime type — "text-x-csrc", "inode-directory".
 * Resolving it to a file on disk is the front-end's job: quickshell already
 * has the icon theme loaded and C would be re-implementing the lookup. */
const char *icon_for(const char *mime, bool is_dir);

/* ── listing.c ──────────────────────────────────────────────────────────── */

/* One directory entry, as the scan found it. Shared with the TUI so that both
 * front-ends see the same answers about symlinks, broken links and sort order;
 * a second walk would be a second set of answers. */
typedef struct {
	char  *name;      /* raw bytes, as the kernel gave them */
	char  *target;    /* symlink target, raw bytes; NULL if not a link */
	bool   is_dir;    /* AFTER following, so a link to a directory sorts as one */
	bool   is_link;
	bool   broken;    /* a symlink whose target does not resolve */
	const char *type;
	const char *mime;
	off_t  size;
	time_t mtime;
	mode_t mode;
	char  *icon;      /* only ever set for a .desktop launcher */
} sf_entry_t;

typedef enum { SF_SORT_NAME, SF_SORT_SIZE, SF_SORT_MTIME, SF_SORT_TYPE } sf_sort_t;

/* Read and sort a directory. NULL (with *count 0) when it cannot be read —
 * reported rather than fatal, because a browser has to survive a cd into
 * something unreadable. */
sf_entry_t  *sf_scan(const char *dir, bool all, size_t *count);
void      sf_entries_free(sf_entry_t *ents, size_t n);
void      sf_sort_set(sf_sort_t sort, bool reverse, bool dirs_first);
sf_sort_t sf_sort_get(void);

int cmd_list(int argc, char **argv);
int cmd_info(int argc, char **argv);
/* The recursive size of a folder — what `info`'s st_size is NOT. Streams a
 * running total (bytes, disk, files, dirs, done) because the walk takes as long
 * as it takes and a properties panel must not freeze for it. */
int cmd_du(int argc, char **argv);

/* ── tui.c ──────────────────────────────────────────────────────────────── */
int cmd_tui(int argc, char **argv);

/* ── resolution.c — pixel dimensions, for the properties pane ───────────────
 * The one place this program reads a file's CONTENT. Called by `info` only,
 * which is one file somebody asked about; a listing must never call it, for
 * the same reason mime.c matches globs instead of sniffing bytes.
 *
 * The format is decided by MAGIC, never by the extension, so a photo saved
 * as .txt still answers and a text file named .png is never mis-parsed. False
 * on anything without dimensions — which is most files, and not an error. */
bool resolution_for(const char *path, const char *mime, long *w, long *h);

/* ── find.c — recursive search ──────────────────────────────────────────────
 * Never follows a symlink while descending: a link to an ancestor is a loop,
 * and one pointing at / turns a search of a project folder into a search of
 * the whole machine. Bounded by --limit and --max-depth, both with defaults. */
int cmd_find(int argc, char **argv);

/* ── peek.c — what is inside a folder, for its icon ─────────────────────────
 * One pass over the subdirectories of one directory, reporting a few
 * previewable files in each. Called ONCE per listing, never once per folder:
 * a directory of 200 subfolders would otherwise fork 200 times to draw one
 * screen. Bounded, one level deep, and nothing is opened or read. */
int cmd_peek(int argc, char **argv);

/* ── places.c ───────────────────────────────────────────────────────────────
 * Pinned folders live in ~/.local/share/user-places.xbel — DOLPHIN'S OWN FILE,
 * on purpose. Reading the format that is already populated means a user's
 * existing sidebar is there the first time this program opens, and it means
 * running both file managers side by side during evaluation does not fork the
 * bookmark list into two diverging copies. */
int cmd_places(int argc, char **argv);

/* ── recent.c ───────────────────────────────────────────────────────────────
 * ~/.local/share/recently-used.xbel, the freedesktop recent-files spec. */
int cmd_recent(int argc, char **argv);

/* ── volumes.c ──────────────────────────────────────────────────────────── */
int cmd_volumes(int argc, char **argv);
/* Both delegate to udisksctl. Mounting is never reimplemented here: udisks2
 * owns the polkit rules that let a desktop user mount a disk unprivileged. */
int cmd_mount(int argc, char **argv);
int cmd_unmount(int argc, char **argv);

/* ── config.c — remembered settings ────────────────────────────────────────
 * Written through the BINARY, never by the GUI: quickshell's FileView silently
 * drops setText() on a path that does not exist yet, so a QML component owning
 * its own settings file loses everything until something else creates it.
 * Every key is declared with a type and a range; unknown keys are refused. */
int cmd_config(int argc, char **argv);

/* ── about.c ────────────────────────────────────────────────────────────── */
int cmd_about(int argc, char **argv);

/* ── actions.c — the right-click menu's borrowed half ───────────────────────
 * "Open With" comes from mimeinfo.cache; the Extract / Set as Wallpaper /
 * Mount ISO style entries come from $XDG_DATA_DIRS/kio/servicemenus, which is
 * where synui already installs its own — so synfiles inherits them rather than
 * reimplementing them, and a helper is written once for both file managers.
 *
 * The GUI never builds a command line: it lists actions, then names one to
 * run, so Exec parsing and %F/%f/%U substitution stay in one testable place. */
int cmd_actions(int argc, char **argv);
int cmd_action(int argc, char **argv);

/* Where "Support" in the About pane points. Named once here and mirrored in
 * synpkg so the suite's windows cannot end up pointing at different places. */
#define SYNAPSE_DONATE_URL "https://buymeacoffee.com/velle999"

/* ── fileops.c — the half that can destroy data ─────────────────────────────
 * Nothing here follows a symlink, nothing overwrites without being told to,
 * and a cross-filesystem move removes the source only after the copy has
 * fully succeeded. See the file header for the reasoning behind each. */
int cmd_copy(int argc, char **argv);
int cmd_move(int argc, char **argv);
int cmd_rename(int argc, char **argv);
int cmd_mkdir(int argc, char **argv);
int cmd_delete(int argc, char **argv);   /* PERMANENT — gated behind --yes */

/* realpath() that dies on failure; caller frees. */
char *sf_resolve(const char *path);
/* Same, for a path whose final component does not exist yet — realpath()
 * refuses those, which is every copy destination. NULL if the parent is bad. */
char *sf_resolve_parent(const char *path);
/* Is `child` at or inside `ancestor`? Both must already be resolved, or a
 * symlink walks straight past the check this exists to make. */
bool sf_is_descendant(const char *ancestor, const char *child);
const char *sf_basename(const char *path);
/* Depth-first remove. Never descends through a symlink — it unlinks the link
 * itself. Returns 0 on success, -1 with errno set. */
int sf_rm_rf(int dirfd, const char *name);

/* ── archive.c — creating archives ──────────────────────────────────────────
 * Extraction already works through synui's KIO service menu; this is the half
 * nothing provides. Compression is delegated to tar/7z/zip — every input must
 * share one parent, and the tool runs with that directory as its cwd, or the
 * archive records absolute paths and unpacking it scatters files. */
int cmd_compress(int argc, char **argv);

/* ── undo.c — the operation journal ─────────────────────────────────────────
 * Every mutation records how to reverse itself, in batches so that a move of
 * six files undoes as one thing. Undo VERIFIES before it acts and refuses
 * rather than guessing; `delete --yes` is deliberately not journalled, because
 * an undo entry that cannot undo would look like a safety net and not be one. */
int  cmd_undo(int argc, char **argv);
void sf_journal(const char *op, const char *a, const char *b);

/* ── trash.c — the XDG trash spec ───────────────────────────────────────────
 * What the Delete key reaches. Trashing is always a rename() and never a copy,
 * which is why a file on another filesystem goes to that volume's own
 * .Trash-$uid rather than the home trash. */
int cmd_trash(int argc, char **argv);

#endif /* SYNFILES_H */
