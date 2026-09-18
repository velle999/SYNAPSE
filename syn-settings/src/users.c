/*
 * users.c — the accounts on this machine: adding one, its password, whether it
 * can administer the machine, its fingerprints, and removing it.
 *
 * ⛔ EVERY WRITE IS DONE TWICE OVER: ONCE TO ASK, ONCE AS ROOT. The window runs
 * as you; making an account needs root. So `syn-settings user <op> …` checks
 * what it was given and re-runs THIS BINARY under pkexec with `--as-root`, and
 * the root half checks all of it again from scratch — it is a different
 * process, started with arguments anybody can type, and "the caller already
 * validated this" is exactly the assumption a privileged helper cannot make.
 *
 * ⛔ A PASSWORD NEVER TOUCHES argv. The window puts it in the environment
 * (runSecretWrite), which pkexec wipes — so the unprivileged half reads it and
 * writes it down a pipe into the root half's stdin, which hands it to chpasswd
 * the same way. `ps` shows argv to every account on the machine.
 *
 * ⚠ ADMINISTRATOR MEANS THE `wheel` GROUP, because that is what both deciders
 * read on this OS: sudo (/etc/sudoers.d/wheel, written by syn-install) and
 * polkit (Arch's default rules name unix-group:wheel as the admin identity).
 *
 * ⚠ THREE THINGS THIS REFUSES, whatever authorisation it is given: removing or
 * demoting the account that asked (a settings window that locks its own user
 * out of sudo is a machine nobody can repair from the desktop), demoting or
 * removing the LAST administrator, and removing an account that is signed in.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "i18n.h"

#include <ctype.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define UID_HUMAN_MIN 1000
#define UID_HUMAN_MAX 60000
#define ADMIN_GROUP   "wheel"
#define PW_MAX        512

static int refuse(const char *msg)
{
	fprintf(stderr, "syn-settings: %s\n", msg);
	return 2;
}

/* ── who is who ─────────────────────────────────────────────────────────── */

/*
 * useradd's own default NAME_REGEX on Arch, minus the trailing `$` it allows
 * for machine accounts: lower case, a letter or underscore first, 32 at most.
 * It is also the whole of what reaches useradd's argv, so it is the gate.
 */
static bool name_ok(const char *n)
{
	size_t l = strlen(n);
	if (l < 1 || l > 32) return false;
	if (!(islower((unsigned char)n[0]) || n[0] == '_')) return false;
	for (const char *p = n; *p; p++)
		if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '_' || *p == '-'))
			return false;
	return true;
}

/* A person's account: in the login range, with a shell that lets them in. */
static bool human(const struct passwd *pw)
{
	if (!pw || pw->pw_uid < UID_HUMAN_MIN || pw->pw_uid >= UID_HUMAN_MAX) return false;
	if (!pw->pw_name || !name_ok(pw->pw_name)) return false;
	const char *sh = pw->pw_shell ? pw->pw_shell : "";
	size_t l = strlen(sh);
	if (l >= 7 && !strcmp(sh + l - 7, "nologin")) return false;
	if (l >= 5 && !strcmp(sh + l - 5, "false")) return false;
	return true;
}

static bool in_group(const struct passwd *pw, const char *group)
{
	struct group *g = getgrnam(group);
	if (!g) return false;
	if (pw->pw_gid == g->gr_gid) return true;
	for (char **m = g->gr_mem; m && *m; m++)
		if (!strcmp(*m, pw->pw_name)) return true;
	return false;
}

static int admin_count(void)
{
	int n = 0;
	setpwent();
	for (struct passwd *pw; (pw = getpwent()); )
		if (human(pw) && in_group(pw, ADMIN_GROUP)) n++;
	endpwent();
	return n;
}

/*
 * Signed in: logind's per-user state file says `active` or `online`.
 * `lingering` is not signed in — it is services left running on purpose.
 */
static bool signed_in(uid_t uid)
{
	char path[PATH_CAP];
	snprintf(path, sizeof path, "%s/%u",
	         env_or("SYN_SETTINGS_LOGIND_USERS", "/run/systemd/users"), (unsigned)uid);
	char *t = slurp(path);
	if (!t) return false;
	bool on = strstr(t, "\nSTATE=active") || strstr(t, "\nSTATE=online") ||
	          !strncmp(t, "STATE=active", 12) || !strncmp(t, "STATE=online", 12);
	free(t);
	return on;
}

/*
 * The account that ASKED. Under pkexec that is PKEXEC_UID — getuid() is 0 by
 * then — and it is what "you cannot remove yourself" has to mean.
 */
static uid_t caller_uid(void)
{
	const char *e = getenv("PKEXEC_UID");
	if (e && *e) {
		char *end;
		unsigned long v = strtoul(e, &end, 10);
		if (!*end) return (uid_t)v;
	}
	return getuid();
}

/* ── the pane ───────────────────────────────────────────────────────────── */

struct acct { char name[33]; uid_t uid; bool admin; };

static int cmp_uid(const void *a, const void *b)
{
	const struct acct *x = a, *y = b;
	return x->uid < y->uid ? -1 : x->uid > y->uid;
}

int pane_users(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	struct acct a[128];
	int n = 0;
	setpwent();
	for (struct passwd *pw; (pw = getpwent()) && n < 128; ) {
		if (!human(pw)) continue;
		snprintf(a[n].name, sizeof a[n].name, "%s", pw->pw_name);
		a[n].uid = pw->pw_uid;
		a[n].admin = in_group(pw, ADMIN_GROUP);
		n++;
	}
	endpwent();
	qsort(a, (size_t)n, sizeof *a, cmp_uid);

	int admins = 0;
	for (int i = 0; i < n; i++) admins += a[i].admin;
	bool fp = fprint_reader_present();
	uid_t me = getuid();

	for (int i = 0; i < n; i++) {
		const char *u = a[i].name;
		bool self = a[i].uid == me;

		/* The role, and the one control that changes it. */
		if (self)
			rec_row("account\t%s\t%s\tok\t%s\t-", u,
			        a[i].admin ? N_("administrator") : N_("standard"),
			        N_("this is you — the account this window runs as"));
		else if (a[i].admin && admins <= 1)
			rec_row("account\t%s\t%s\t-\t%s\t-", u, N_("administrator"),
			        N_("the only administrator — make another one before this one can be made standard"));
		else if (a[i].admin)
			rec_row("account\t%s\t%s\t-\t%s\tdemote:%s", u, N_("administrator"),
			        N_("an administrator: it can use sudo and change the system"), u);
		else
			rec_row("account\t%s\t%s\t-\t%s\tpromote:%s", u, N_("standard"),
			        N_("a standard account: it cannot use sudo or change the system"), u);

		rec_row("password\t%s\t-\t-\t%s\tpassword:%s", u,
		        N_("type a new password twice — the old one is not asked for, so an administrator can reset a forgotten one"),
		        u);

		if (fp) {
			rec_row("fingerprint\t%s\t-\t-\t%s\tchoice:finger/%s", u,
			        N_("pick a finger, then lift and rest it on the reader each time it asks"), u);
			rec_row("fingerprints\t%s\t-\t-\t%s\tfforget:%s", u,
			        N_("forget every fingerprint on file for this account"), u);
		}

		if (self) continue;
		if (signed_in(a[i].uid))
			rec_row("remove\t%s\t-\twarn\t%s\t-", u,
			        N_("signed in right now — it has to sign out before it can be removed"));
		else if (a[i].admin && admins <= 1)
			rec_row("remove\t%s\t-\t-\t%s\t-", u,
			        N_("the only administrator cannot be removed"));
		else
			rec_row("remove\t%s\t-\t-\t%s\tdeluser:%s", u,
			        N_("removes the account; its files in /home can go with it or stay"), u);
	}

	rec_row("add\t%s\t\t-\t%s\tadduser:new", N_("Add a user"),
	        N_("another account with its own files and desktop — at the login screen, Tab moves to the name field"));
	return 0;
}

/* ── running things ─────────────────────────────────────────────────────── */

/* This binary, for the pkexec re-exec — see boot.c's self_path() for why
 * /proc/self/exe and not an installed path. */
static const char *self_exe(void)
{
	static char buf[512];
	if (!buf[0]) {
		ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
		if (n > 0) buf[n] = '\0';
		else snprintf(buf, sizeof buf, "/usr/bin/syn-settings");
	}
	return buf;
}

/*
 * argv with `data` on its stdin, the child's own output left where it was.
 * Under --dry-run it only says so — and says the password is on stdin, never
 * what it is.
 */
static int run_with_stdin(char *const argv[], const char *data)
{
	if (g_dry_run) {
		fputs("would run:", stdout);
		for (int i = 0; argv[i]; i++) printf(" %s", argv[i]);
		puts("  (the password on its stdin)");
		return 0;
	}
	int fds[2];
	if (pipe(fds) != 0) return 1;
	pid_t pid = fork();
	if (pid < 0) { close(fds[0]); close(fds[1]); return 1; }
	if (pid == 0) {
		dup2(fds[0], 0);
		close(fds[0]); close(fds[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fds[0]);
	/* A child that exits early must not kill us with SIGPIPE mid-write. */
	void (*old)(int) = signal(SIGPIPE, SIG_IGN);
	size_t len = strlen(data), off = 0;
	while (off < len) {
		ssize_t w = write(fds[1], data + off, len - off);
		if (w <= 0) break;
		off += (size_t)w;
	}
	close(fds[1]);
	signal(SIGPIPE, old);
	int st = 0;
	if (waitpid(pid, &st, 0) < 0) return 1;
	int rc = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
	if (rc == 127) fprintf(stderr, "syn-settings: could not run %s\n", argv[0]);
	return rc;
}

/*
 * The password: from the window's environment on the way in, from stdin in
 * the root half. Unset as soon as it is read, so nothing this process starts
 * inherits it.
 */
static bool read_secret(bool from_stdin, char *out, size_t cap)
{
	out[0] = '\0';
	if (!from_stdin) {
		const char *e = getenv("SYN_SETTINGS_SECRET");
		if (e) {
			snprintf(out, cap, "%s", e);
			unsetenv("SYN_SETTINGS_SECRET");
			return true;
		}
		if (isatty(0)) return false;
	}
	size_t n = 0;
	for (;;) {
		ssize_t r = read(0, out + n, cap - 1 - n);
		if (r <= 0) break;
		n += (size_t)r;
		if (n >= cap - 1) break;
	}
	out[n] = '\0';
	if (n && out[n - 1] == '\n') out[--n] = '\0';
	return true;
}

static bool password_ok(const char *pw)
{
	if (!*pw || strlen(pw) >= PW_MAX - 1) return false;
	for (const char *p = pw; *p; p++)
		if (*p == '\n' || *p == '\r') return false;
	return true;
}

/* The layout the person at the keyboard is using, for a new account's first
 * login: syn-install writes it into synuirc too, and a desktop that comes up
 * on a US layout for somebody typing in German is the first thing they meet. */
static void caller_xkb(char *out, size_t cap)
{
	out[0] = '\0';
	char path[PATH_CAP];
	synuirc_path(path, sizeof path);
	char *t = slurp(path);
	if (!t) return;
	for (char *l = t; l && *l; ) {
		char *eol = strchr(l, '\n');
		if (eol) *eol = '\0';
		while (*l == ' ' || *l == '\t') l++;
		if (!strncmp(l, "xkb_layout", 10)) {
			char *v = strchr(l, '=');
			if (v) {
				v++;
				while (*v == ' ' || *v == '\t') v++;
				size_t k = 0;
				while (v[k] && (isalnum((unsigned char)v[k]) || strchr(",_-+", v[k])) && k + 1 < cap)
					{ out[k] = v[k]; k++; }
				out[k] = '\0';
			}
		}
		l = eol ? eol + 1 : NULL;
	}
	free(t);
}

static bool xkb_ok(const char *x)
{
	if (!*x || strlen(x) > 64) return false;
	for (const char *p = x; *p; p++)
		if (!isalnum((unsigned char)*p) && !strchr(",_-+", *p)) return false;
	return true;
}

/* ── the root half ──────────────────────────────────────────────────────── */

/* chpasswd with "name:password" on stdin, then proof it landed: a PAM module
 * that failed can leave the account locked while chpasswd's own complaint
 * scrolls past (syn-install learned this and checks the same way). */
static int set_password_root(const char *name, const char *pw)
{
	char line[PW_MAX + 64];
	snprintf(line, sizeof line, "%s:%s\n", name, pw);
	char *a[] = { (char *)"chpasswd", NULL };
	int rc = run_with_stdin(a, line);
	explicit_bzero(line, sizeof line);
	if (rc) return rc;
	if (g_dry_run) return 0;
	struct spwd *sp = getspnam(name);
	if (!sp || !sp->sp_pwdp || sp->sp_pwdp[0] != '$')
		return refuse("chpasswd reported success but the password is not in /etc/shadow");
	return 0;
}

static int root_add(const char *name, bool admin, const char *xkb, const char *pw)
{
	if (getpwnam(name)) return refuse("an account by that name already exists");

	/* The groups syn-install gives the first account, where this machine has
	 * them — a plain Arch box has no `synapse` or `seat` group. */
	static const char *want[] = { "audio", "video", "input", "synapse", "seat" };
	char groups[128] = "";
	for (size_t i = 0; i < sizeof want / sizeof *want; i++)
		if (getgrnam(want[i]))
			snprintf(groups + strlen(groups), sizeof groups - strlen(groups),
			         "%s%s", *groups ? "," : "", want[i]);
	if (admin && getgrnam(ADMIN_GROUP))
		snprintf(groups + strlen(groups), sizeof groups - strlen(groups),
		         "%s%s", *groups ? "," : "", ADMIN_GROUP);

	/* -m copies /etc/skel, which is where a new account's desktop comes
	 * from: synui ships the house synuirc there. */
	char *a[12];
	int k = 0;
	a[k++] = (char *)"useradd"; a[k++] = (char *)"-m";
	a[k++] = (char *)"-s"; a[k++] = (char *)"/bin/bash";
	if (*groups) { a[k++] = (char *)"-G"; a[k++] = groups; }
	a[k++] = (char *)name;
	a[k] = NULL;
	int rc = run_or_show_progress(a);
	if (rc) return rc;

	rc = set_password_root(name, pw);
	if (rc) {
		/* Not left behind half-made: an account with no password is one
		 * nobody can sign in to, listed as though it were ready. */
		char *d[] = { (char *)"userdel", (char *)"-r", (char *)name, NULL };
		run_or_show(d);
		return rc;
	}

	if (xkb && *xkb) {
		if (g_dry_run) {
			printf("would append to /home/%s/.config/synui/synuirc: xkb_layout = %s\n", name, xkb);
		} else {
			struct passwd *pw2 = getpwnam(name);
			if (pw2 && pw2->pw_dir) {
				char path[PATH_CAP];
				snprintf(path, sizeof path, "%.400s/.config/synui/synuirc", pw2->pw_dir);
				/* Only into the file skel put there, and never through a
				 * link: this runs as root inside somebody else's home. */
				int fd = open(path, O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC);
				if (fd >= 0) {
					dprintf(fd, "xkb_layout = %s\n", xkb);
					close(fd);
				}
			}
		}
	}
	return 0;
}

static int root_op(const char *op, const char *name, int argc, char **argv)
{
	if (!g_dry_run && geteuid() != 0)
		return refuse("--as-root is what pkexec runs; it does nothing without root");
	if (!name_ok(name) || !strcmp(name, "root"))
		return refuse("that is not an account name this will touch");

	bool admin = false, files = false;
	const char *xkb = NULL;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--admin")) admin = true;
		else if (!strcmp(argv[i], "--files")) files = true;
		else if (!strcmp(argv[i], "--xkb") && i + 1 < argc) {
			xkb = argv[++i];
			if (!xkb_ok(xkb)) return refuse("that keyboard layout name is malformed");
		}
		else if (!strcmp(argv[i], "--as-root")) ;
		else return refuse("unknown option");
	}

	if (!strcmp(op, "add")) {
		char pw[PW_MAX];
		read_secret(true, pw, sizeof pw);
		if (!password_ok(pw)) { explicit_bzero(pw, sizeof pw); return refuse("a new account needs a password, one line long"); }
		int rc = root_add(name, admin, xkb, pw);
		explicit_bzero(pw, sizeof pw);
		return rc;
	}

	/* Everything else is about an account that exists and is a person's. */
	struct passwd *pw = getpwnam(name);
	if (!pw || !human(pw)) return refuse("no such account among the people who use this machine");
	uid_t target = pw->pw_uid;
	bool is_admin = in_group(pw, ADMIN_GROUP);

	if (!strcmp(op, "password")) {
		char pass[PW_MAX];
		read_secret(true, pass, sizeof pass);
		if (!password_ok(pass)) { explicit_bzero(pass, sizeof pass); return refuse("the password is empty or more than one line"); }
		int rc = set_password_root(name, pass);
		explicit_bzero(pass, sizeof pass);
		return rc;
	}
	if (!strcmp(op, "promote")) {
		char *a[] = { (char *)"gpasswd", (char *)"-a", (char *)name, (char *)ADMIN_GROUP, NULL };
		return run_or_show_progress(a);
	}
	if (!strcmp(op, "demote")) {
		if (target == caller_uid())
			return refuse("an administrator cannot take away their own rights here — another administrator can");
		if (is_admin && admin_count() <= 1)
			return refuse("that is the only administrator on this machine");
		char *a[] = { (char *)"gpasswd", (char *)"-d", (char *)name, (char *)ADMIN_GROUP, NULL };
		return run_or_show_progress(a);
	}
	if (!strcmp(op, "remove")) {
		if (target == caller_uid()) return refuse("the account that asked cannot remove itself");
		if (is_admin && admin_count() <= 1) return refuse("that is the only administrator on this machine");
		if (signed_in(target)) return refuse("that account is signed in; it has to sign out first");
		/* Its prints go first — fprintd keeps them by name, and a new account
		 * that one day takes the name would inherit them. */
		if (have_cmd("fprintd-delete")) {
			char *f[] = { (char *)"fprintd-delete", (char *)name, NULL };
			if (g_dry_run) run_or_show(f); else run_quiet(f);
		}
		char *a[4];
		int k = 0;
		a[k++] = (char *)"userdel";
		if (files) a[k++] = (char *)"-r";
		a[k++] = (char *)name;
		a[k] = NULL;
		return run_or_show_progress(a);
	}
	if (!strcmp(op, "forget-prints")) {
		if (!have_cmd("fprintd-delete")) return refuse("fprintd is not installed");
		char *a[] = { (char *)"fprintd-delete", (char *)name, NULL };
		return run_or_show_progress(a);
	}
	return refuse("unknown user operation");
}

/* ── the half that asks ─────────────────────────────────────────────────── */

/* pkexec <self> user <op> <name> <extra…> --as-root, with `secret` on stdin
 * when there is one. */
static int escalate(const char *op, const char *name, const char *const *extra, const char *secret)
{
	char *a[16];
	int k = 0;
	a[k++] = (char *)"pkexec";
	a[k++] = (char *)self_exe();
	a[k++] = (char *)"user";
	a[k++] = (char *)op;
	a[k++] = (char *)name;
	for (int i = 0; extra && extra[i] && k < 14; i++) a[k++] = (char *)extra[i];
	a[k++] = (char *)"--as-root";
	a[k] = NULL;
	if (secret) return run_with_stdin(a, secret);
	return run_or_show_progress(a);
}

int do_user(int argc, char **argv)
{
	if (argc < 2) return refuse("user needs an operation and an account name");
	const char *op = argv[0], *name = argv[1];

	bool as_root = false;
	for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "--as-root")) as_root = true;

	/* Fingerprints for an account: `user enroll <name> <finger>`. Its own
	 * shape, because the finger is a positional token from the allowlist. */
	if (!strcmp(op, "enroll")) {
		const char *finger = argc > 2 ? argv[2] : "";
		if (!fprint_known_finger(finger)) return refuse("that is not a finger fprintd knows");
		if (!name_ok(name)) return refuse("that is not an account name");
		if (!as_root) {
			struct passwd *me = getpwuid(getuid());
			/* Your own: no root needed, the Fingerprint pane's own path. */
			if (me && !strcmp(me->pw_name, name)) return cmd_enroll(finger);
			const char *extra[] = { finger, NULL };
			return escalate("enroll", name, extra, NULL);
		}
		if (!g_dry_run && geteuid() != 0) return refuse("--as-root needs root");
		struct passwd *pw = getpwnam(name);
		if (!pw || !human(pw)) return refuse("no such account");
		if (!have_cmd("fprintd-enroll")) return refuse("fprintd is not installed");
		char *a[] = { (char *)"fprintd-enroll", (char *)"-f", (char *)finger, (char *)name, NULL };
		return run_or_show_progress(a);
	}

	if (as_root) return root_op(op, name, argc - 2, argv + 2);

	/* The asking half: the same checks the root half will make, so a refusal
	 * arrives before an authorisation dialogue rather than after it. */
	if (!name_ok(name) || !strcmp(name, "root"))
		return refuse("an account name is lower-case letters, digits, _ and -, starting with a letter, at most 32");

	if (!strcmp(op, "add")) {
		if (getpwnam(name)) return refuse("an account by that name already exists");
		bool admin = false;
		for (int i = 2; i < argc; i++) {
			if (!strcmp(argv[i], "--admin")) admin = true;
			else return refuse("add takes only --admin");
		}
		char pw[PW_MAX];
		if (!read_secret(false, pw, sizeof pw) || !password_ok(pw)) {
			explicit_bzero(pw, sizeof pw);
			return refuse("a new account needs a password, one line long");
		}
		char xkb[80];
		caller_xkb(xkb, sizeof xkb);
		const char *extra[4];
		int e = 0;
		if (admin) extra[e++] = "--admin";
		if (xkb_ok(xkb)) { extra[e++] = "--xkb"; extra[e++] = xkb; }
		extra[e] = NULL;
		char line[PW_MAX + 2];
		snprintf(line, sizeof line, "%s\n", pw);
		int rc = escalate("add", name, extra, line);
		explicit_bzero(pw, sizeof pw);
		explicit_bzero(line, sizeof line);
		return rc;
	}

	struct passwd *pw = getpwnam(name);
	if (!pw || !human(pw)) return refuse("no such account among the people who use this machine");

	if (!strcmp(op, "password")) {
		char pass[PW_MAX];
		if (!read_secret(false, pass, sizeof pass) || !password_ok(pass)) {
			explicit_bzero(pass, sizeof pass);
			return refuse("the password is empty or more than one line");
		}
		char line[PW_MAX + 2];
		snprintf(line, sizeof line, "%s\n", pass);
		int rc = escalate("password", name, NULL, line);
		explicit_bzero(pass, sizeof pass);
		explicit_bzero(line, sizeof line);
		return rc;
	}
	if (!strcmp(op, "promote")) return escalate("promote", name, NULL, NULL);
	if (!strcmp(op, "demote")) {
		if (pw->pw_uid == getuid())
			return refuse("an administrator cannot take away their own rights here — another administrator can");
		return escalate("demote", name, NULL, NULL);
	}
	if (!strcmp(op, "remove")) {
		if (pw->pw_uid == getuid()) return refuse("the account that asked cannot remove itself");
		bool files = false;
		for (int i = 2; i < argc; i++) {
			if (!strcmp(argv[i], "--files")) files = true;
			else return refuse("remove takes only --files");
		}
		const char *extra[] = { files ? "--files" : NULL, NULL };
		return escalate("remove", name, extra, NULL);
	}
	if (!strcmp(op, "forget-prints")) {
		/* Your own: fprintd lets an account delete its own prints. */
		if (pw->pw_uid == getuid()) return cmd_forget("all");
		return escalate("forget-prints", name, NULL, NULL);
	}
	return refuse("user takes add, password, promote, demote, remove, forget-prints or enroll");
}

/* `set finger/<user> <finger>` — the finger picker's buttons (a `choice:`). */
int users_set(const char *key, const char *val)
{
	if (strncmp(key, "finger/", 7)) return -1;
	char *argv[] = { (char *)"enroll", (char *)(key + 7), (char *)val, NULL };
	return do_user(3, argv);
}
