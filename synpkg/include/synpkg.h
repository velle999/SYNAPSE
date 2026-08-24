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

/* ── The super search: one list, every source ────────────────────────────────
 *
 * `synpkg search --all` asks the repositories, the AUR and Flathub the same
 * question and prints the answers as ONE table. That cannot be done by running
 * the three commands and concatenating them, because each has its own TSV
 * shape: the repositories emit six columns, the AUR seven (its seventh is
 * `votes` and its sixth `flag`), Flathub seven with a `title`. Three headers
 * would arrive interleaved with three sets of rows, and a reader would have to
 * know which was which.
 *
 * So there is one shape, and it is the UNION rather than a new invention:
 *
 *   name  installed  version  repo  size  description  title  votes  flag
 *
 * Every column already existed in one of the three; nothing had to be renamed,
 * and `repo` was already the source label the GUI badges rows with ("core",
 * "extra", "blackarch", "aur", "flathub"). A source fills what it has and
 * leaves the rest empty — which is what those columns already mean everywhere
 * else in this program.
 *
 * ⚠ WHEN THIS IS SET, NOTHING ELSE MAY PRINT A HEADER. cmd_search() emits it
 * once, before the first source runs; the per-source emitters have to know to
 * stay quiet, or a header lands in the middle of the data. That is the whole
 * reason this is a global rather than a parameter — it has to reach
 * aur_render() and flatpak_row(), which are statics three call levels down in
 * ext.c and have no route to an argument.
 */
extern bool g_super;
/* The unified header, once. A no-op in human mode and after the first call. */
void sp_super_header(void);
/* One row in that shape. Any of the trailing strings may be NULL or "". */
void sp_super_row(const char *name, bool installed, const char *version,
                  const char *repo, off_t size, const char *desc,
                  const char *title, const char *votes, const char *flag);
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
bool  confirm_possible(void);
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

/* ── progress.c — pacman's progress bar, ILoveCandy and all ─────────────────
 * `key` is what the bar is FOR (a package name, a filename): it resets the
 * chomp's animation state, which matters because alpm interleaves parallel
 * downloads. Both are silent in TSV mode and whenever stderr is not a terminal,
 * so no caller needs to check either. */
void progress_draw(const char *key, const char *label, int percent);
void progress_end(void);   /* close the line, if one is open. Idempotent. */

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
int cmd_groups(int argc, char **argv);

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

/* ── news.c — the Arch Linux news gate ──────────────────────────────────────
 *
 * Arch occasionally requires a manual step before an upgrade will succeed, and
 * archlinux.org/news is the only place it is announced. `pacman -Syu` has never
 * mentioned this; a graphical Update button makes that gap universal. */
int cmd_news(int argc, char **argv);
/* Show what has been published since this machine's last full system upgrade
 * and ask whether to continue. FALSE means the user said no.
 *
 * TRUE when the feed cannot be reached: this is an advisory, and one that can
 * wedge the updater when a network is down is worse than none. */
bool sp_news_gate(void);

/* ── kernel.c — "installed" is not "running" ────────────────────────────────
 *
 * A kernel upgrade DELETES /usr/lib/modules/<running release>/ while the kernel
 * itself keeps running from memory, so every module not already loaded silently
 * stops being loadable. Take a snapshot before a transaction, hand it back
 * after, and the diff says whether a reboot is owed. */
typedef struct sp_kernel sp_kernel;
size_t sp_kernel_snapshot(sp_kernel **out);
void   sp_kernel_free(sp_kernel *k, size_t n);
void   sp_kernel_reboot_check(const sp_kernel *before, size_t n_before);
/* The same facts as a report, for `status`: which kernels are installed, which
 * one is running, and whether those disagree. */
void   sp_kernel_status(void);
/* The THIRD fact, and the one that strands a machine: what the bootloader will
 * actually load. Bootloaders that stage their own copy of the kernel (limine)
 * can be left pinned to a release whose modules the last upgrade deleted, with
 * every hash and filename still looking correct. Reads the release out of each
 * boot image itself. */
void   sp_boot_status(void);

/* ── curated.c ──────────────────────────────────────────────────────────── */
int cmd_suggest(int argc, char **argv);
/* Where the catalogue was resolved from, and how many entries it holds — the
 * About pane reports both, because "0 suggestions" and "the file is missing"
 * look identical in a list. Path is malloc'd. */
char  *sp_curated_path(void);
size_t sp_curated_count(void);

/* ── arsenal.c ──────────────────────────────────────────────────────────── */
int cmd_arsenal(int argc, char **argv);
int cmd_cachyos(int argc, char **argv);
/* Is [cachyos] in pacman.conf? The Kernel pane's rows depend on it. */
bool cachyos_repo_enabled(void);

/* ── ext.c — the non-ALPM sources ───────────────────────────────────────── */
int aur_search_term(const char *term);   /* also reached by `search --aur` */

/* ── AUR, reached from the repo commands ────────────────────────────────────
 *
 * `install` falls back here when a name is in no repository, and `upgrade`
 * comes here for the foreign packages a sysupgrade structurally cannot see.
 *
 * ALL THREE MUST RUN UNPRIVILEGED. makepkg refuses to run as root because it
 * executes a PKGBUILD fetched off the internet, so the AUR half of a command
 * happens in the pass BEFORE (or AFTER) escalate(), never inside the pkexec'd
 * child. See the note above aur_install() in ext.c.
 */

/* Which of `names` actually exist in the AUR. One RPC call, not n. Returns the
 * count and fills `*out` with malloc'd names (caller frees both). */
size_t aur_filter_existing(char **names, size_t n, char ***out);

/* Installed foreign packages the AUR has a NEWER version of. */
size_t aur_outdated_names(char ***out);

/* Clone/pull and makepkg -si each name. Refuses if called as root. */
int aur_build_install(char **names, size_t n);

/* Did synpkg install this from the AUR? `upgrade` rebuilds only what it owns —
 * "foreign" also covers every locally built SynapseOS package, and names do
 * collide with the AUR. See the note above the definition in ext.c. */
bool aur_have_checkout(const char *name);
int cmd_system(int argc, char **argv);   /* syn-update */
/* Flathub's half of the super search, and `synpkg flatpak search`'s whole
 * implementation — one function, so the two cannot disagree about what a
 * Flathub result looks like. */
int flatpak_search(const char *term);
int cmd_flatpak(int argc, char **argv);
int cmd_aur(int argc, char **argv);

/* The Flathub remote, named once. The URL is the .flatpakrepo and NOT the bare
 * repository: the repo file carries Flathub's GPG key, and a remote added
 * without it verifies no signatures at all. */
/* Where "Support" in the About pane points. Named once so the suite's windows
 * cannot end up pointing at different places. */
#define SYNAPSE_DONATE_URL "https://buymeacoffee.com/velle999"

#define SYNPKG_FLATHUB_NAME "flathub"
#define SYNPKG_FLATHUB_URL  "https://dl.flathub.org/repo/flathub.flatpakrepo"

/* Is flatpak installed, and is Flathub among its remotes? Both are runtime
 * facts on every machine, so every caller has to ask rather than assume. */
bool sp_flatpak_present(void);
bool sp_flathub_enabled(void);
/* Flatpak's own hold: every masked pattern, system and user installation both.
 * NOT IgnorePkg — a flatpak is not a pacman package and pacman.conf has no
 * opinion about one. Caller frees with pconf_free_list(). */
size_t sp_flatpak_masks(char ***out);

/* ── appstream.c — Flathub's category index ─────────────────────────────────
 * The flatpak CLI can only search by term, so browsing Flathub by category
 * means reading the AppStream catalogue flatpak has already downloaded. Every
 * string field is a valid C string and never NULL; "" means the catalogue did
 * not carry one, which is common and is not a fault. */
typedef struct {
	char *id;        /* the installable flatpak ref, e.g. org.mozilla.firefox */
	char *name;      /* human name; "" when the catalogue omits it */
	char *summary;   /* one-line description; may be "" */
	char *version;   /* newest release; may be "" */
	char *cats;      /* "\nGame\nActionGame\n" — match whole lines, not substrings */
} sp_as_app;

/* NULL means this machine has NO index (a remote added but never
 * `flatpak update --appstream`), which callers must report differently from an
 * index that simply holds nothing. *count is 0 in both cases. */
sp_as_app *sp_appstream_load(size_t *count);
/* Is there an index at all — answered from the filesystem, without reading it.
 * "Remote configured, index never fetched" is a distinct state that makes every
 * search and every category come back empty with no error. */
bool       sp_appstream_present(void);
void       sp_appstream_free(sp_as_app *apps, size_t count);
bool       sp_appstream_in(const sp_as_app *a, const char *category);

/* ── settings.c ─────────────────────────────────────────────────────────── */
/* The stored value for a known key, or its default — never NULL for a key the
 * table knows. Caller frees. */
char *sp_setting(const char *key);
/* yes/true/1 is true; anything else is false. Unknown key -> true, because
 * every setting today is an opt-OUT and a typo must not silently disable a
 * pass of the upgrade. */
bool  sp_setting_bool(const char *key);
int   cmd_config(int argc, char **argv);

/* ── ignore.c — holding a package back ──────────────────────────────────────
 *
 * The list is pacman.conf's IgnorePkg, not a synpkg file. An ignore only this
 * program honoured would be a lie on a machine that also has pacman on it, and
 * synpkg has READ IgnorePkg since the beginning (see alpmctx.c) — this only
 * adds the ability to write what was already being obeyed. Changing it needs
 * root, so both commands escalate. */
/* Everything pacman considers ignored, resolved across every line and every
 * Include. Caller frees with pconf_free_list(). */
size_t sp_ignore_list(char ***out);
bool   sp_ignore_has(const char *name);
int    cmd_ignore(int argc, char **argv);     /* no args: list */
int    cmd_unignore(int argc, char **argv);

/* ── about.c ────────────────────────────────────────────────────────────── */
int cmd_about(int argc, char **argv);

/* ── tui.c ──────────────────────────────────────────────────────────────── */
int cmd_tui(int argc, char **argv);

#endif /* SYNPKG_H */
