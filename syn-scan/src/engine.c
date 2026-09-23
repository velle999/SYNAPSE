/* engine.c — the three programs that actually detect things, given one voice.
 *
 * ⛔ NOTHING HERE REIMPLEMENTS A SCANNER. Each adapter runs the real engine and
 * turns its output into finding_t. If an adapter ever starts deciding what is
 * malicious on its own, it has become a fourth engine that nobody maintains.
 *
 * ⚠ AND NOTHING HERE GOES THROUGH A SHELL. Paths come from the user and from
 * disk; `~/Downloads/$(rm -rf ~).exe` is a legal filename. fork + execvp takes
 * an argv, so there is no string for a filename to break out of.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synscan.h"
#include "i18n.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t g_child = -1;

/* ⚠ LC_ALL=C IS THE WHOLE REASON THIS WRAPPER EXISTS. clamscan says "FOUND",
 * rkhunter says "Warning:", and both are translated — on a German desktop the
 * parsers below would match nothing and report a clean machine. Every engine is
 * read in C, always. */
FILE *engine_popen(const char *const argv[])
{
	int fd[2];
	if (pipe(fd) != 0) { warn("pipe: %s", strerror(errno)); return NULL; }

	pid_t pid = fork();
	if (pid < 0) {
		warn("fork: %s", strerror(errno));
		close(fd[0]); close(fd[1]);
		return NULL;
	}
	if (pid == 0) {
		close(fd[0]);
		dup2(fd[1], STDOUT_FILENO);
		/* Engines are chatty on stderr about things that are not findings
		 * (unreadable dirs, missing optional checks). Keep it off the parse
		 * path but let the user see it. */
		close(fd[1]);
		setenv("LC_ALL", "C", 1);
		setenv("LANG", "C", 1);
		unsetenv("LC_MESSAGES");
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	close(fd[1]);
	g_child = pid;
	return fdopen(fd[0], "r");
}

int engine_pclose(FILE *f)
{
	int status = -1;
	if (f) fclose(f);
	if (g_child > 0) {
		waitpid(g_child, &status, 0);
		g_child = -1;
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Find the binary by EXISTENCE, not by executability — see synscan.h. */
static char *engine_find(const engine_t *e, int mode)
{
	const char *p = getenv("PATH");
	if (!p || !*p) p = "/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin";
	char *dup = strdup(p);
	if (!dup) return NULL;

	char *save = NULL, *out = NULL;
	for (char *dir = strtok_r(dup, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
		if (!*dir) continue;
		char *cand = NULL;
		if (asprintf(&cand, "%s/%s", dir, e->binary) < 0) continue;
		if (access(cand, mode) == 0) { out = cand; break; }
		free(cand);
	}
	free(dup);
	return out;
}

char *engine_path(const engine_t *e)     { return engine_find(e, F_OK); }

bool engine_present(const engine_t *e)
{
	char *p = engine_find(e, F_OK);
	bool ok = p != NULL;
	free(p);
	return ok;
}

bool engine_runnable(const engine_t *e)
{
	char *p = engine_find(e, X_OK);
	bool ok = p != NULL;
	free(p);
	return ok;
}

/* Strip trailing newline. */
static void chomp(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = 0;
}

/* ── ClamAV ──────────────────────────────────────────────────────────────────
 *
 * clamscan prints one line per file:
 *
 *     /path/to/thing: Win.Trojan.Foo-1 FOUND
 *     /path/to/other: OK
 *     /path/to/big.zip: Heuristics.Limits.Exceeded ERROR
 *
 * ⚠ The signature name can contain spaces and the PATH can contain ": ", so
 * this parses from the RIGHT — the verdict is the last word, the signature is
 * what sits between the last ": " and that word.
 */
/* ── clamscan or clamdscan ───────────────────────────────────────────────────
 *
 * clamscan loads the whole signature set on every invocation. Measured on a
 * normal desktop: **5.91 s to scan one small file**, essentially all of it
 * load time. clamdscan asks the already-loaded daemon and answers in **0.02 s**.
 *
 * So when clamd is up we talk to it. That is what makes the daemon worth its
 * 0.95 GB to somebody who opted in — without this, clamd would be a resident
 * gigabyte that nothing on the system ever speaks to.
 *
 * ⚠ THE WEEKLY SWEEP BARELY CARES. It is ONE invocation over many paths, so
 * the 5.91 s is paid once and amortised. It is the interactive path — a person
 * clicking "Scan Downloads" — that goes from six seconds of nothing to instant.
 */
#ifndef SYNSCAN_CLAMD_SOCKET
#define SYNSCAN_CLAMD_SOCKET "/run/clamav/clamd.ctl"
#endif

static bool clamd_available(void)
{
	/* ⚠ $SYNSCAN_CLAMD_SOCKET EXISTS FOR THE SUITE, and it is not a nicety.
	 * Without it the tests inherit whatever the developer's machine is doing:
	 * a box with clamd running has a real socket and a real /usr/bin/clamdscan,
	 * which PATH cannot hide, so the stub engines are bypassed and the suite
	 * quietly scans the actual machine. It passed in CI and failed here — seven
	 * assertions at once — which is the good version of that bug. */
	const char *sock = getenv("SYNSCAN_CLAMD_SOCKET");
	if (!sock || !*sock) sock = SYNSCAN_CLAMD_SOCKET;

	if (access(sock, F_OK) != 0) return false;

	const engine_t probe = { "clamav", "", "clamdscan", 0, NULL };
	return engine_present(&probe);
}

/* ── the signature database clamscan is about to load ────────────────────────
 *
 * ⛔ clamscan SKIPS A SIGNATURE FILE IT CANNOT READ, AND SAYS NOTHING. No
 * error, exit 0, "OK" for every file. Found 2026-09-22: this package's own
 * freshclam drop-in set UMask=0027, every update wrote daily.cld 0640
 * clamav:clamav, and every scan a person ran loaded main.cvd and bytecode.cvd
 * only — `clamscan --debug` lists what it loaded, and daily.cld was not in it.
 * Every signature newer than main.cvd was missing, and nothing said so. The
 * weekly sweep runs as root and was whole.
 *
 * So each file in the database directory is checked for THIS account before
 * clamscan starts. One it cannot read is an "incomplete" row naming it: the
 * scan still runs, on the signatures that are there, and says it is not
 * whole. A directory with nothing readable in it is an engine that cannot
 * run — clamscan would refuse, print to stderr, and leave this parser an
 * empty stream to call "Nothing found."
 *
 * ⚠ clamscan only. clamd loads the database as its own user, and a client
 * talking to it never opens these files.
 *
 * ⚠ $SYNSCAN_CLAMAV_DBDIR is for the suite, for the same reason as
 * $SYNSCAN_CLAMD_SOCKET: without it the tests would check the real
 * /var/lib/clamav, and a box whose database is unreadable would fail them. */
#ifndef SYNSCAN_CLAMAV_DBDIR
#define SYNSCAN_CLAMAV_DBDIR "/var/lib/clamav"
#endif

static int clamav_db_check(findings_t *out)
{
	const char *dir = getenv("SYNSCAN_CLAMAV_DBDIR");
	if (!dir || !*dir) dir = SYNSCAN_CLAMAV_DBDIR;

	DIR *d = opendir(dir);
	if (!d) {
		int err = errno;
		warn(_("ClamAV has no signature database at %s (%s)"), dir, strerror(err));
		return -1;
	}

	/* Collected apart, so a directory with nothing readable adds no rows. */
	findings_t unread; findings_init(&unread);
	size_t readable = 0;
	struct dirent *de;
	while ((de = readdir(d))) {
		/* freshclam's own state, not signatures — clamscan never opens it. */
		if (de->d_name[0] == '.' || !strcmp(de->d_name, "freshclam.dat")
		    || !strcmp(de->d_name, "mirrors.dat"))
			continue;

		char *p = NULL;
		if (asprintf(&p, "%s/%s", dir, de->d_name) < 0) die("out of memory");
		struct stat st;
		if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
			if (access(p, R_OK) == 0) {
				readable++;
			} else {
				int err = errno;
				char *why = NULL;
				if (asprintf(&why, _("could not read %s (%s), so this scan ran "
				                     "without the signatures in it"),
				             p, strerror(err)) < 0)
					die("out of memory");
				findings_add(&unread, "clamav", VERDICT_INCOMPLETE, p, why);
				free(why);
			}
		}
		free(p);
	}
	closedir(d);

	if (readable == 0) {
		if (unread.n)
			warn(_("ClamAV's signature database at %s cannot be read by this "
			       "account"), dir);
		else
			warn(_("ClamAV has no signature database at %s yet — freshclam may "
			       "still be downloading it"), dir);
		findings_free(&unread);
		return -1;
	}
	for (const finding_t *u = unread.head; u; u = u->next)
		findings_add(out, u->engine, u->verdict, u->path, u->detail);
	findings_free(&unread);
	return 0;
}

static int run_clamav(const engine_t *e, char *const *paths, findings_t *out)
{
	size_t np = 0;
	while (paths && paths[np]) np++;
	if (np == 0) return 0;

	bool viad = clamd_available();
	if (!viad && clamav_db_check(out) != 0) return -1;

	const char **argv = calloc(np + 8, sizeof *argv);
	if (!argv) die("out of memory");
	size_t i = 0;

	if (viad) {
		argv[i++] = "clamdscan";
		/* ⛔ --fdpass IS NOT OPTIONAL. clamd runs as the clamav user and
		 * cannot open a file in somebody's home; without this every scan of a
		 * user's own files comes back
		 *     "File path check failure: Permission denied. ERROR"
		 * which this parser would faithfully record as an unreadable file,
		 * once per file, forever. Passing the descriptor is what lets the
		 * daemon read what the CALLER can read.
		 *
		 * ⚠ And there is no --scan-archive here: archive scanning is clamd.conf's
		 * ScanArchive (default yes), not a flag. Verified 2026-09-10 that EICAR
		 * inside modpack.zip is still found this way. */
		argv[i++] = "--fdpass";
		argv[i++] = "--no-summary";
	} else {
		argv[i++] = e->binary;
		argv[i++] = "--recursive";
		argv[i++] = "--no-summary";
		argv[i++] = "--stdout";
		/* Archives are the point on this machine — a mod pack is a zip. */
		argv[i++] = "--scan-archive=yes";
	}
	for (size_t k = 0; k < np; k++) argv[i++] = paths[k];
	argv[i] = NULL;

	FILE *f = engine_popen(argv);
	free(argv);
	if (!f) return -1;

	char *line = NULL; size_t cap = 0; ssize_t len;
	while ((len = getline(&line, &cap, f)) > 0) {
		chomp(line);
		if (!*line) continue;

		char *last = strrchr(line, ' ');
		if (!last) continue;
		const char *tail = last + 1;

		verdict_t v;
		if      (!strcmp(tail, "FOUND")) v = VERDICT_INFECTED;
		else if (!strcmp(tail, "ERROR")) v = VERDICT_ERROR;
		else continue;              /* OK, Empty file, Excluded — not findings */

		*last = 0;                  /* line is now "path: Signature" */
		char *sep = strrchr(line, ':');
		if (!sep) continue;
		*sep = 0;
		const char *sig = sep + 1;
		while (*sig == ' ') sig++;

		findings_add(out, e->id, v, line, sig);
	}
	free(line);
	engine_pclose(f);
	return 0;
}

/* ── rkhunter ────────────────────────────────────────────────────────────────
 *
 * --rwo is "report warnings only", but what arrives is not only warnings:
 *
 *   Warning: The file properties have changed:        ← a finding
 *            File: /usr/bin/foo                         ← more of that finding
 *   Logfile directory is not writable: /var/log/...    ← rkhunter's own trouble
 *
 * ⛔ Every line used to be a finding. The weekly sweep runs sandboxed, rkhunter
 * could not write /var/log/rkhunter.log there, and "Logfile directory is not
 * writable" was reported every week as a Suspicious finding — the only one,
 * and the one Settings counted as "1 outstanding". Now: a "Warning:" line is a
 * finding, an indented line belongs to the finding above it, and anything else
 * is INCOMPLETE — the engine's problem, listed, never counted.
 *
 * The log goes to our state directory, which the unit can write and root's
 * scans own; /var/log is outside the sandbox.
 *
 * ⚠ rkhunter exits 1 when it warns, which is success for our purposes; the
 * exit status is deliberately not consulted.
 */
static void append_detail(finding_t *f, const char *more)
{
	while (*more == ' ' || *more == '\t') more++;
	if (!*more) return;
	size_t a = strlen(f->detail), b = strlen(more);
	char *d = realloc(f->detail, a + b + 2);
	if (!d) die("out of memory");
	if (a) d[a++] = ' ';
	memcpy(d + a, more, b + 1);
	f->detail = d;
}

static int run_rkhunter(const engine_t *e, char *const *paths, findings_t *out)
{
	(void)paths;                    /* system-wide; a path list means nothing */
	char *logfile = NULL;
	mkdir(state_dir(), 0750);           /* EEXIST is the normal answer */
	if (asprintf(&logfile, "%s/rkhunter.log", state_dir()) < 0) die("out of memory");
	const char *argv[] = {
		e->binary, "--check", "--skip-keypress", "--nocolors", "--rwo",
		"--logfile", logfile, NULL
	};
	FILE *f = engine_popen(argv);
	if (!f) { free(logfile); return -1; }

	char *line = NULL; size_t cap = 0;
	finding_t *last = NULL;
	while (getline(&line, &cap, f) > 0) {
		chomp(line);
		if (!*line) continue;
		if (!strncmp(line, "Warning:", 8)) {
			const char *msg = line + 8;
			while (*msg == ' ') msg++;
			last = findings_add(out, e->id, VERDICT_SUSPECT, e->name, msg);
		} else if ((line[0] == ' ' || line[0] == '\t') && last) {
			append_detail(last, line);
		} else {
			findings_add(out, e->id, VERDICT_INCOMPLETE, e->name, line);
			last = NULL;
		}
	}
	free(line);
	free(logfile);
	engine_pclose(f);
	return 0;
}

/* ── chkrootkit ──────────────────────────────────────────────────────────────
 *
 * -q is quiet mode: it prints only what it considers positive. ⚠ It is famous
 * for false positives on a busy desktop, which is exactly why nothing here is
 * ever auto-quarantined on its word alone.
 */
static int run_chkrootkit(const engine_t *e, char *const *paths, findings_t *out)
{
	(void)paths;
	const char *argv[] = { e->binary, "-q", NULL };
	FILE *f = engine_popen(argv);
	if (!f) return -1;

	char *line = NULL; size_t cap = 0;
	while (getline(&line, &cap, f) > 0) {
		chomp(line);
		if (!*line) continue;
		findings_add(out, e->id, VERDICT_SUSPECT, e->name, line);
	}
	free(line);
	engine_pclose(f);
	return 0;
}

/* ⛔ ids are the protocol — `--only clamav` and the window both use them. */
static const engine_t k_engines[] = {
	{ "clamav",     N_("ClamAV"),        "clamscan",   ENGINE_FILES,  run_clamav },
	{ "rkhunter",   N_("Rootkit Hunter"), "rkhunter",  ENGINE_SYSTEM, run_rkhunter },
	{ "chkrootkit", N_("chkrootkit"),    "chkrootkit", ENGINE_SYSTEM, run_chkrootkit },
};

const engine_t *engines_all(size_t *n)
{
	*n = sizeof k_engines / sizeof k_engines[0];
	return k_engines;
}

const engine_t *engine_by_id(const char *id)
{
	size_t n; const engine_t *e = engines_all(&n);
	for (size_t i = 0; i < n; i++)
		if (!strcmp(e[i].id, id)) return &e[i];
	return NULL;
}
