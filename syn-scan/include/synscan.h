/* synscan.h — scanning files at rest, on a machine that already watches itself.
 *
 * ── WHY THIS EXISTS WHEN synguard ALREADY RUNS ───────────────────────────────
 *
 * synguard watches ACTIVITY: two LSM hooks (file_open, bprm_check_security)
 * over a kmod that kprobes execve, openat, socket, connect, ptrace, setuid,
 * module load and mount. Its whole rule vocabulary — exec, open, write, setuid,
 * ptrace, mount, module — decides on WHO and WHERE. Not one rule in it decides
 * on WHAT IS INSIDE A FILE.
 *
 * So three things are invisible to it, and they are the three this program is
 * for:
 *
 *   1. A file at rest. Nothing has touched it, so there is no event, and no
 *      rule ever runs.
 *   2. Content. A malicious archive and a holiday photo are the same object to
 *      every rule synguard has.
 *   3. Windows payloads. To Linux a .exe is data. When Wine finally runs one,
 *      the LSM sees *wine* opening a file — which wine does thousands of times
 *      a session — and bprm_check_security never fires on a PE binary at all.
 *      On a distro with a Wine menu, Proton and an arcade front end, that is
 *      the likeliest way malware actually arrives.
 *
 * ── ⛔ THIS PROGRAM DOES NOT WATCH IN REAL TIME, AND MUST NOT LEARN TO ────────
 *
 * ClamAV ships clamonacc and it is the obvious next feature. It is also a
 * second owner of the same open() path synguard's BPF-LSM already owns, and the
 * second owner is the one nobody is monitoring. `clamav-clamonacc.service`
 * stays masked. The split is:
 *
 *     synguard   kernel, syscall path, behaviour, microseconds
 *     syn-scan   userspace, scheduled, content, minutes
 *
 * An LSM hook runs IN the syscall path — scanning content there means holding
 * every open() until a signature sweep finishes, which is precisely why
 * ClamAV's own on-access scanner is a userspace daemon and not an LSM.
 *
 * ── ⛔ THIS PROGRAM NEVER DELETES ────────────────────────────────────────────
 *
 * A finding moves to quarantine with a sidecar recording where it came from,
 * or it is reported and left alone. On a single-seat desktop a false positive
 * that eats somebody's save game is a worse outcome than the malware this will
 * realistically meet, and it is far more likely.
 *
 * ── The engines are not ours ─────────────────────────────────────────────────
 *
 * clamscan, rkhunter and chkrootkit do the detecting. This program gives them
 * one schedule, one quarantine, one output format and one window. It does not
 * reimplement them, and it does not pretend to be them: lynis matches malware
 * scanners BY BINARY FILENAME, so installing this as `chkrootkit` would pass
 * MALW-3275 with no rootkit detection behind it. See docs/MALWARE-SCANNER-DESIGN.md §2.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNSCAN_H
#define SYNSCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

/* ── output ─────────────────────────────────────────────────────────────── */

/* ⛔ OUT_REC IS A PROTOCOL, NOT A PRETTY-PRINT. data/syn-scan.qml keys off the
 * column names emitted by the #-header rows, and off the engine ids below.
 * Neither is ever translated — see i18n.h. */
typedef enum { OUT_HUMAN, OUT_REC } out_mode_t;
extern out_mode_t g_out;
extern bool g_yes;
extern bool g_dry;
extern bool g_quiet;
/* ⚠ findings_print() promises "nothing has been moved", which is a LIE if the
 * caller is about to quarantine. The printer has to know. */
extern bool g_will_quarantine;

void info(const char *fmt, ...);
void warn(const char *fmt, ...);
void die(const char *fmt, ...);

/* ── findings ───────────────────────────────────────────────────────────── */

/* What an engine concluded about one thing. Every engine's output is turned
 * into this, because the point of this program is that the user reads one
 * format instead of three. */
typedef enum {
	VERDICT_CLEAN = 0,
	VERDICT_INFECTED,   /* a signature matched */
	VERDICT_SUSPECT,    /* engine flagged it without naming a signature */
	VERDICT_ERROR,      /* engine could not read or decide */
} verdict_t;

/* ⛔ NEVER TRANSLATED — travels in records, matched by the window. */
const char *verdict_id(verdict_t v);
/* The human word, translated at the point it is drawn. */
const char *verdict_label(verdict_t v);

typedef struct finding {
	char     *engine;   /* engine id: "clamav", "rkhunter", "chkrootkit" */
	char     *path;     /* what was looked at; may be a check name, not a file */
	char     *detail;   /* signature name, or the engine's own words */
	verdict_t verdict;
	time_t    when;
	struct finding *next;
} finding_t;

typedef struct {
	finding_t *head, *tail;
	size_t     n;
} findings_t;

void       findings_init(findings_t *f);
finding_t *findings_add(findings_t *f, const char *engine, verdict_t v,
                        const char *path, const char *detail);
void       findings_free(findings_t *f);
void       findings_print(const findings_t *f);

/* ── engines ────────────────────────────────────────────────────────────── */

typedef enum {
	ENGINE_FILES = 1 << 0,  /* takes a path and scans content */
	ENGINE_SYSTEM = 1 << 1, /* checks the system, ignores the path list */
} engine_cap_t;

typedef struct engine {
	const char *id;      /* ⛔ never translated — the window matches on it */
	const char *name;    /* what a person calls it */
	const char *binary;  /* what to look for in PATH */
	unsigned    caps;
	/* Run over `paths` (NULL-terminated) and append what it concluded.
	 * Returns -1 only if the engine could not be run at all. */
	int (*run)(const struct engine *e, char *const *paths, findings_t *out);
} engine_t;

const engine_t *engines_all(size_t *n);
const engine_t *engine_by_id(const char *id);
/* ⚠ PRESENT AND RUNNABLE ARE TWO DIFFERENT FACTS, and conflating them tells a
 * user a lie about their own machine. Arch ships /usr/bin/rkhunter as 0700
 * root:root — an ordinary user cannot execute it, but it IS installed. A
 * scanner that answered "not installed" would send that user to reinstall a
 * package they already have, and lynis (which audits as root) would meanwhile
 * be finding it and awarding MALW-3276.
 *
 * engine_present()  — the file is on disk         (F_OK)
 * engine_runnable() — and THIS user may run it    (X_OK) */
bool            engine_present(const engine_t *e);
bool            engine_runnable(const engine_t *e);
char           *engine_path(const engine_t *e);   /* free() it */

/* ⚠ EVERY ENGINE IS PARSED UNDER LC_ALL=C. clamscan and rkhunter are
 * translated, and so is every strerror behind them; a German locale renames
 * the very words the parsers match on. */
FILE *engine_popen(const char *const argv[]);
int   engine_pclose(FILE *f);

/* ── where state lives ──────────────────────────────────────────────────────
 *
 * ⛔ NOT A COMPILE-TIME CONSTANT, for two reasons that are the same reason.
 *
 * A scan run by the timer is root and writes /var/lib/syn-scan. A scan run by
 * a person looking at their own Downloads folder is NOT root and cannot write
 * there at all — and a quarantine that silently fails for the user who
 * actually pressed the button is worse than no quarantine.
 *
 * ⚠ AND THE TEST SUITE COMPOSES EVERY PATH FROM SYNSCAN_HOME. A suite that can
 * reach the real /var/lib is one bad string away from moving somebody's files,
 * and this project has already had a test delete the real /tmp once.
 *
 *   $SYNSCAN_HOME  if set        — the tests, and anyone who wants it elsewhere
 *   /var/lib/syn-scan            — running as root
 *   $XDG_DATA_HOME/syn-scan      — otherwise
 */
const char *state_dir(void);
const char *quarantine_dir(void);

/* Move a file in, leaving a .meta sidecar next to it recording the original
 * path, mode and mtime so restore is exact. Never called without consent. */
int quarantine_take(const char *path, const char *engine, const char *detail);
int quarantine_list(void);
int quarantine_restore(const char *id);
int quarantine_purge(const char *id);   /* id or "--all" */

/* ── scanning ───────────────────────────────────────────────────────────── */

typedef struct {
	char *const *paths;      /* NULL-terminated, or NULL for --system */
	bool         system;     /* run the ENGINE_SYSTEM engines */
	const char  *only;       /* one engine id, or NULL for all present */
	bool         quarantine; /* move what is found */
} scan_req_t;

int scan_run(const scan_req_t *req, findings_t *out);

/* ── status ─────────────────────────────────────────────────────────────── */

#ifndef SYNSCAN_STATEDIR
#define SYNSCAN_STATEDIR "/var/lib/syn-scan"
#endif

int  status_show(void);
void status_record(const findings_t *f, time_t started);

#endif /* SYNSCAN_H */
