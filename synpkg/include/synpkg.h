/* synpkg — SynapseOS package manager.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNPKG_H
#define SYNPKG_H

#include <alpm.h>
#include <alpm_list.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

/* ── Global output state ────────────────────────────────────────────────────
 * Every front-end reads the SAME code paths; only the renderer differs. TSV is
 * what the QML GUI parses (see data/synpkg.qml) — the reason it is TSV and not
 * JSON is in arsenal-query's original rationale: nothing in a base SynapseOS
 * install can parse JSON from shell, and hand-rolled JSON escaping in C is one
 * more place to get a package description containing a quote wrong.
 */
typedef enum { OUT_HUMAN, OUT_TSV } out_mode_t;

extern out_mode_t g_out;
extern bool g_color;      /* ANSI escapes are safe on stdout */
extern bool g_noconfirm;  /* never prompt */
extern bool g_verbose;


/* ── util.c ─────────────────────────────────────────────────────────────── */
void  *xmalloc(size_t n);
void  *xrealloc(void *p, size_t n);
char  *xstrdup(const char *s);
char  *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void   die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
void   warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void   info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Colour helpers return "" when colour is off, so call sites stay branch-free. */
const char *C_RESET(void);
const char *C_BOLD(void);
const char *C_DIM(void);
const char *C_ACCENT(void);
const char *C_WARN(void);
const char *C_ERR(void);
const char *C_OK(void);

/* Emit one TSV record. Tabs and newlines are stripped from every field before
 * emission: a stray tab in a package description would silently shift every
 * later column in the GUI, which is the kind of bug that looks like bad data. */
void tsv_row(int nfields, ...);

/* Run argv, return its exit status. `quiet` sends stdout+stderr to /dev/null. */
int   run(char *const argv[], bool quiet);
/* Run argv and capture stdout. Returns malloc'd NUL-terminated buffer (never
 * NULL); *status gets the exit status if non-NULL.
 *
 * `quiet_stderr` discards the child's stderr. Use it only where a non-zero exit
 * is itself the whole diagnosis — probing pacman-conf for a directive this
 * pacman may not have, say — because everywhere else the child's complaint is
 * the only explanation the user will get. */
char *run_capture(char *const argv[], int *status, bool quiet_stderr);

bool  confirm(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *human_size(off_t bytes);   /* malloc'd, e.g. "4.91 MiB" */
bool  have_cmd(const char *name);
void  strip_trailing_newline(char *s);

/* Split `text` on `sep` in place. Returns a malloc'd array of pointers INTO
 * text, *n set to the count. Caller frees the array only, not the strings. */
char **split(char *text, char sep, size_t *n);

/* ── pconf.c — pacman.conf, read through pacman-conf ────────────────────────
 * We shell out to pacman-conf rather than parsing /etc/pacman.conf ourselves.
 * Include globs, mirrorlist indirection and SigLevel inheritance are exactly
 * the parts a hand-rolled parser gets subtly wrong, and pacman-conf ships in
 * the pacman package, which is a hard dependency anyway. */
char  *pconf(const char *directive);                        /* malloc'd, may be "" */
char  *pconf_repo(const char *repo, const char *directive); /* malloc'd */
char **pconf_repo_list(size_t *n);                          /* malloc'd argv-ish */
void   pconf_free_list(char **list, size_t n);
int    pconf_siglevel(const char *repo);                    /* ALPM_SIG_* bitmask */

/* ── alpmctx.c ──────────────────────────────────────────────────────────── */
/* `for_write` installs the transaction callbacks and is what a mutation needs;
 * a query handle skips them so nothing prints during a --tsv listing. */
alpm_handle_t *sp_alpm_init(bool for_write);
void           sp_alpm_free(alpm_handle_t *h);
const char    *sp_alpm_err(alpm_handle_t *h);
/* Registered sync dbs, in pacman.conf order. */
alpm_list_t   *sp_syncdbs(alpm_handle_t *h);

/* ── query.c ────────────────────────────────────────────────────────────── */
int cmd_search(int argc, char **argv);
int cmd_info(int argc, char **argv);
int cmd_installed(int argc, char **argv);
int cmd_updates(int argc, char **argv);
int cmd_orphans(int argc, char **argv);
int cmd_status(int argc, char **argv);

/* Shared row shape for anything that lists packages:
 *   name \t installed(0|1) \t version \t repo \t size \t description */
void emit_pkg(const char *name, bool installed, const char *version,
              const char *repo, off_t size, const char *desc);
void emit_pkg_header(void);

/* ── trans.c ────────────────────────────────────────────────────────────── */
int cmd_install(int argc, char **argv);
int cmd_remove(int argc, char **argv);
int cmd_upgrade(int argc, char **argv);
int cmd_refresh(int argc, char **argv);
/* Re-exec this binary as root via pkexec to run exactly `verb argv...`, with
 * the global flags re-applied. Returns the child's exit status.
 *
 * It builds the command rather than replaying the process's own argv, and that
 * is load-bearing: the terminal browser reaches cmd_install() from a process
 * invoked as `synpkg tui`, so replaying argv would hand pkexec a request to
 * start a second, ROOT copy of the interactive browser. */
int escalate(const char *verb, int argc, char **argv);
bool is_root(void);

/* ── curated.c ──────────────────────────────────────────────────────────── */
int cmd_suggest(int argc, char **argv);

/* ── arsenal.c ──────────────────────────────────────────────────────────── */
int cmd_arsenal(int argc, char **argv);

/* ── ext.c — the non-ALPM sources ───────────────────────────────────────── */
int aur_search_term(const char *term);   /* also reached by `search --aur` */
int cmd_system(int argc, char **argv);   /* syn-update */
int cmd_flatpak(int argc, char **argv);
int cmd_aur(int argc, char **argv);

/* ── tui.c ──────────────────────────────────────────────────────────────── */
int cmd_tui(int argc, char **argv);

#endif /* SYNPKG_H */
