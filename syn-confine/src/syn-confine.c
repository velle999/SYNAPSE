/* syn-confine — run a command inside a kernel-enforced allowlist.
 *
 * This exists because of one line in vibe/tools.py:
 *
 *     subprocess.run(command, shell=True, ...)
 *
 * — a command proposed by a language model, run as the user, with the user's
 * whole authority. What stood in front of it was a five-branch regular
 * expression looking for `rm -rf /` and a fork bomb. A denylist over shell
 * strings is not a boundary: a shell has unlimited ways to spell a command,
 * and that particular denylist said nothing at all about READING, which is
 * the half that matters when the process can reach ~/.ssh and the network.
 *
 * The threat is not a malicious model. It is prompt injection: any file, diff
 * or web page the agent reads can carry instructions, and the agent has the
 * user's keys in reach. So the boundary has to hold regardless of what the
 * command says, which means it has to be in the kernel.
 *
 * ── Why Landlock ───────────────────────────────────────────────────────────
 *
 * Landlock is applied by a process TO ITSELF, needs no privilege, is inherited
 * across execve and cannot be dropped. That shape is exactly right here: this
 * program builds a ruleset, restricts itself, and then execs the command. The
 * command inherits a confinement it cannot see, cannot argue with and cannot
 * remove, and nothing in the string it was spelled with changes that.
 *
 * It is also strictly additive — it can only ever remove access the process
 * already had, so this can never hand out a permission.
 *
 * ── What this does NOT do ──────────────────────────────────────────────────
 *
 * Stated here rather than discovered later, because a sandbox believed to be
 * tighter than it is, is worse than none:
 *
 *   - Landlock's network support covers TCP bind and connect ONLY. UDP is not
 *     covered, so DNS still resolves and DNS is an exfiltration channel.
 *     `--isolate-net` adds a network namespace for that; plain --tcp/--net
 *     does not.
 *   - It is path-based. It confines WHERE, never WHAT. There is no way to say
 *     "not files matching *.key".
 *   - /proc is allowed read-only because virtually every tool needs
 *     /proc/self, and Landlock cannot express "this pid but not that one".
 *     Reading another process's environ is gated by yama, not by us.
 *   - A newer kernel may add access rights this binary does not know about.
 *     Unknown rights are UNHANDLED, which means unrestricted. See abi_mask().
 *
 * ── The one invariant ──────────────────────────────────────────────────────
 *
 * FAIL CLOSED. Every path out of this file that could not establish the
 * sandbox exits non-zero without running the command. A confinement tool that
 * degrades quietly to running things unconfined is worse than no tool at all,
 * because everything downstream goes on believing the boundary is there.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdarg.h>
#include <locale.h>
#include "../include/i18n.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <linux/landlock.h>

#ifndef SYNCONFINE_VERSION
#define SYNCONFINE_VERSION "0.1.0"
#endif

/* glibc has no wrappers for these. */
static long ll_create(const struct landlock_ruleset_attr *attr, size_t size,
                      unsigned int flags)
{
	return syscall(__NR_landlock_create_ruleset, attr, size, flags);
}
static long ll_add(int fd, enum landlock_rule_type type, const void *attr,
                   unsigned int flags)
{
	return syscall(__NR_landlock_add_rule, fd, type, attr, flags);
}
static long ll_restrict(int fd, unsigned int flags)
{
	return syscall(__NR_landlock_restrict_self, fd, flags);
}

static const char *g_prog = "syn-confine";

/*
 * ⛔ LC_NUMERIC STAYS AT C. Nothing here prints a float today, but the ports
 * and the ABI number in `--print` are %u and %d and the next number somebody
 * adds may not be. A separator that moved with the desktop is a policy summary
 * that reads differently from the policy.
 *
 * ⚠ AND setlocale() HERE AFFECTS THE CONFINED CHILD NOT AT ALL. It inherits
 * the environment, not this process's locale state, and phrases its own errors
 * from its own libc — which is the thing tests/syn_confine_test.sh pins LC_ALL
 * for, two execs away.
 */
static void i18n_init(void)
{
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");

	const char *dir = getenv("SYN_CONFINE_LOCALEDIR");
	bindtextdomain(SYN_CONFINE_GETTEXT_DOMAIN,
	               dir && *dir ? dir : SYNCONFINE_LOCALEDIR);
	bind_textdomain_codeset(SYN_CONFINE_GETTEXT_DOMAIN, "UTF-8");
	textdomain(SYN_CONFINE_GETTEXT_DOMAIN);
}

static void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s: ", g_prog);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	/* 78 = EX_CONFIG. Distinct from the command's own status, so a caller can
	 * tell "the sandbox could not be built" from "the command failed", and
	 * never mistake the first for the second. */
	exit(78);
}

/* ── access-right sets ───────────────────────────────────────────────────── */

#define A_READ  (LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR)
#define A_EXEC  (LANDLOCK_ACCESS_FS_EXECUTE)

/* Everything a normal tool does to a file it owns. MAKE_CHAR and MAKE_BLOCK
 * are deliberately absent: nothing legitimate here creates device nodes, and
 * they stay in the handled set so they are denied everywhere.
 *
 * TRUNCATE is not optional — without it `echo x > existing` fails, which reads
 * as "the sandbox randomly breaks redirection". REFER is what allows rename
 * and link between two directories inside the workspace. */
#define A_WRITE (LANDLOCK_ACCESS_FS_WRITE_FILE   | LANDLOCK_ACCESS_FS_MAKE_REG  \
               | LANDLOCK_ACCESS_FS_MAKE_DIR     | LANDLOCK_ACCESS_FS_REMOVE_FILE \
               | LANDLOCK_ACCESS_FS_REMOVE_DIR   | LANDLOCK_ACCESS_FS_MAKE_SYM  \
               | LANDLOCK_ACCESS_FS_MAKE_FIFO    | LANDLOCK_ACCESS_FS_MAKE_SOCK \
               | LANDLOCK_ACCESS_FS_TRUNCATE     | LANDLOCK_ACCESS_FS_REFER)

/* Handled = the set this program has an opinion about. Anything handled and
 * not granted on a path is DENIED there; anything not handled is unrestricted,
 * which is why this list wants to be as wide as the kernel allows. */
#define A_ALL   (A_READ | A_EXEC | A_WRITE \
               | LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_BLOCK \
               | LANDLOCK_ACCESS_FS_IOCTL_DEV)

/* The only rights meaningful on a NON-directory.
 *
 * Landlock rejects the whole rule with EINVAL — not silently, but not
 * obviously either — if a directory-only right such as MAKE_REG or REFER is
 * granted on a regular file or a device node. The first build failed exactly
 * here, on /dev/null, and because one bad rule fails landlock_add_rule the
 * effect was that NOTHING ran at all. Better that than the alternative, but
 * the fix is to grant a file only what a file can have. */
#define A_FILE_ONLY (LANDLOCK_ACCESS_FS_EXECUTE    | LANDLOCK_ACCESS_FS_WRITE_FILE \
                   | LANDLOCK_ACCESS_FS_READ_FILE  | LANDLOCK_ACCESS_FS_TRUNCATE  \
                   | LANDLOCK_ACCESS_FS_IOCTL_DEV)

/* Rights that arrived after Landlock ABI 1. Asking for one the running kernel
 * does not know is EINVAL from landlock_create_ruleset, so the set is trimmed
 * to the ABI rather than assumed — this binary has to keep working on an older
 * kernel than the one it was built on, including a rescue boot. */
static uint64_t abi_mask(int abi, uint64_t rights)
{
	if (abi < 5) rights &= ~(uint64_t)LANDLOCK_ACCESS_FS_IOCTL_DEV;
	if (abi < 3) rights &= ~(uint64_t)LANDLOCK_ACCESS_FS_TRUNCATE;
	if (abi < 2) rights &= ~(uint64_t)LANDLOCK_ACCESS_FS_REFER;
	return rights;
}

/* ── the policy being assembled ──────────────────────────────────────────── */

typedef struct {
	const char *path;
	uint64_t    rights;
	bool        required;   /* an explicit flag; a missing one is an error */
} rule_t;

#define MAX_RULES 128
static rule_t g_rules[MAX_RULES];
static size_t g_nrules;

static void add_rule(const char *path, uint64_t rights, bool required)
{
	if (g_nrules >= MAX_RULES)
		die(_("too many paths (limit %d)"), MAX_RULES);
	g_rules[g_nrules++] = (rule_t){ path, rights, required };
}

#define MAX_PORTS 32
static uint16_t g_ports[MAX_PORTS];
static size_t   g_nports;

/* The system a command needs in order to BE a command: the interpreter, the
 * shared libraries, the tools on PATH. Read and execute, never write — so a
 * confined process cannot rewrite the binaries the next one will run.
 *
 * /etc is readable because resolv.conf, passwd and ssl/certs are needed to do
 * anything at all, and it is NOT writable: /etc/ld.so.preload and
 * /etc/sudoers.d are the two files most worth putting out of reach.
 *
 * These are optional — /lib64 does not exist on every layout, and a base entry
 * that is simply absent is not an error. An explicit --ro/--rw/--rx that is
 * absent IS one, because it is far more likely to be a typo, and a typo that
 * silently widened nothing would leave someone believing a path was allowed. */
static void add_base_profile(void)
{
	static const char *rx[] = {
		"/usr", "/bin", "/sbin", "/lib", "/lib64", "/opt", NULL
	};
	for (int i = 0; rx[i]; i++)
		add_rule(rx[i], A_READ | A_EXEC, false);

	static const char *ro[] = { "/etc", "/proc", "/sys", NULL };
	for (int i = 0; ro[i]; i++)
		add_rule(ro[i], A_READ, false);

	/* Named individually rather than granting /dev, which holds every disk on
	 * the machine. A rule on /dev would put /dev/nvme0n1 inside the sandbox.
	 * IOCTL_DEV goes with them because isatty() is an ioctl and a tool that
	 * cannot ask "am I a terminal" tends to fail in a way nobody connects
	 * back to the sandbox. */
	static const char *devs[] = {
		"/dev/null", "/dev/zero", "/dev/full",
		"/dev/random", "/dev/urandom", NULL
	};
	for (int i = 0; devs[i]; i++)
		add_rule(devs[i], A_READ | A_WRITE | LANDLOCK_ACCESS_FS_IOCTL_DEV,
		         false);
}

/* ── network namespace, for the half Landlock does not cover ─────────────── */

static void write_file(const char *path, const char *data)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		die(_("--isolate-net: open %s: %s"), path, strerror(errno));
	if (write(fd, data, strlen(data)) < 0)
		die(_("--isolate-net: write %s: %s"), path, strerror(errno));
	close(fd);
}

/* Landlock's network rules are TCP-only, so "no network" needs a namespace.
 *
 * An unprivileged user namespace is what makes CLONE_NEWNET reachable without
 * root. The uid/gid maps are written straight back to the real ids, so the
 * command runs as the same user it would have anyway — the namespace is here
 * for the empty network stack, not to change who anything runs as.
 *
 * Loopback is left DOWN. Bringing it up needs netlink, and a sandbox where
 * 127.0.0.1 answers is one where anything already listening on this machine —
 * including synapd's own socket — is still reachable. */
static void isolate_net(void)
{
	uid_t uid = getuid();
	gid_t gid = getgid();

	if (unshare(CLONE_NEWUSER | CLONE_NEWNET) != 0)
		die(_("--isolate-net: unshare: %s\n"
		      "  This needs unprivileged user namespaces "
		      "(/proc/sys/user/max_user_namespaces must be > 0)."),
		    strerror(errno));

	/* Must deny setgroups before gid_map may be written unprivileged. */
	write_file("/proc/self/setgroups", "deny");

	char buf[64];
	snprintf(buf, sizeof buf, "%u %u 1", (unsigned)uid, (unsigned)uid);
	write_file("/proc/self/uid_map", buf);
	snprintf(buf, sizeof buf, "%u %u 1", (unsigned)gid, (unsigned)gid);
	write_file("/proc/self/gid_map", buf);
}

/* ── usage ───────────────────────────────────────────────────────────────── */

static void usage(FILE *f)
{
	fputs(
"syn-confine " SYNCONFINE_VERSION " — run a command inside a kernel-enforced allowlist\n"
"\n"
"Usage: syn-confine [options] -- COMMAND [ARG...]\n"
"\n"
"Paths (repeatable; each covers everything beneath it)\n"
"  --rw PATH       read, write, create and delete\n"
"  --ro PATH       read only\n"
"  --rx PATH       read and execute\n"
"\n"
"Network\n"
"  (default)       no outbound TCP at all\n"
"  --tcp PORT      allow outbound TCP to PORT only (repeatable)\n"
"  --net           allow outbound TCP without restriction\n"
"  --isolate-net   an empty network namespace: no TCP, no UDP, no DNS,\n"
"                  no loopback. The only option that stops DNS exfiltration\n"
"\n"
"Other\n"
"  --no-base       omit the base system profile (/usr, /etc, /proc, ...)\n"
"  --print         print the resolved policy and exit without running\n"
"  -h, --help      this text\n"
"\n"
"Everything not granted is denied. Landlock is inherited across execve and\n"
"cannot be dropped, so the confinement holds however the command is spelled.\n"
"\n"
"⚠ --tcp and --net cover TCP ONLY. UDP is not covered, so DNS still resolves\n"
"and DNS is an exfiltration channel. Use --isolate-net if that matters.\n"
"\n"
"Exit status: 78 if the sandbox could not be built (the command did NOT run);\n"
"otherwise the command's own status.\n", f);
}

int main(int argc, char **argv)
{
	i18n_init();

	bool want_base = true, print_only = false;
	bool net_unrestricted = false, isolate = false;
	int i = 1;

	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--")) { i++; break; }
		else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout); return 0; }
		else if (!strcmp(a, "--no-base"))     want_base = false;
		else if (!strcmp(a, "--print"))       print_only = true;
		else if (!strcmp(a, "--net"))         net_unrestricted = true;
		else if (!strcmp(a, "--isolate-net")) isolate = true;
		else if (!strcmp(a, "--rw") || !strcmp(a, "--ro") || !strcmp(a, "--rx")) {
			if (i + 1 >= argc)
				die(_("%s needs a path"), a);
			uint64_t r = A_READ;
			if (a[3] == 'w') r |= A_WRITE | A_EXEC;
			if (a[3] == 'x') r |= A_EXEC;
			add_rule(argv[++i], r, true);
		} else if (!strcmp(a, "--tcp")) {
			if (i + 1 >= argc)
				die("%s", _("--tcp needs a port"));
			char *end = NULL;
			long p = strtol(argv[++i], &end, 10);
			if (!end || *end || p < 1 || p > 65535)
				die(_("--tcp: '%s' is not a port"), argv[i]);
			if (g_nports >= MAX_PORTS)
				die(_("too many --tcp ports (limit %d)"), MAX_PORTS);
			g_ports[g_nports++] = (uint16_t)p;
		} else {
			die(_("unknown option '%s' (use -- before the command)"), a);
		}
	}

	if (i >= argc && !print_only) {
		usage(stderr);
		return 78;
	}

	if (net_unrestricted && g_nports)
		die("%s", _("--net and --tcp contradict each other"));
	if (net_unrestricted && isolate)
		die("%s", _("--net and --isolate-net contradict each other"));

	if (want_base)
		add_base_profile();

	int abi = (int)ll_create(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
	if (abi < 1)
		die(_("this kernel has no Landlock support (%s) — refusing to run "
		      "the command unconfined"), strerror(errno));

	uint64_t handled_fs = abi_mask(abi, A_ALL);

	struct landlock_ruleset_attr attr = { .handled_access_fs = handled_fs };

	/* Network handling is only switched on when it is going to be enforced.
	 * Handling it and then granting every port would be a longer way of
	 * writing --net, and a much easier thing to get subtly wrong. */
	bool enforce_net = !net_unrestricted && abi >= 4;
	if (enforce_net)
		attr.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP
		                        | LANDLOCK_ACCESS_NET_CONNECT_TCP;
	if (!net_unrestricted && abi < 4 && !isolate)
		die(_("this kernel's Landlock (ABI %d) cannot restrict the network, "
		      "and --isolate-net was not given — refusing rather than "
		      "pretending the network is closed"), abi);

	/* Signals and abstract unix sockets scoped to the sandbox: the confined
	 * process cannot signal the compositor, and cannot reach an abstract
	 * socket belonging to anything outside. Both are ways out of a purely
	 * filesystem sandbox. */
	if (abi >= 6)
		attr.scoped = LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET
		            | LANDLOCK_SCOPE_SIGNAL;

	if (print_only) {
		/*
		 * ⚠ THE LABEL AND ITS PADDING ARE INSIDE THE MSGID, deliberately.
		 * This is a two-column report spaced by hand, and the alternative —
		 * translating the word and padding it here — needs a DISPLAY WIDTH,
		 * because "ネットワーク" is 18 bytes, 6 code points and 12 columns.
		 * Handing the translator the whole line instead lets them line their
		 * own column up, which is what a .po is for; all thirteen were
		 * checked against the English width.
		 */
		printf(_("landlock abi   %d\n"), abi);
		/*
		 * ⛔ FOUR WHOLE SENTENCES, NOT A WORD IN A SLOT. This is the line
		 * somebody reads to decide whether a sandbox is tight enough —
		 * "(UDP NOT covered)" is the whole point of it — and a clause
		 * dropped into a %s reaches every reader in English however well
		 * the rest is translated.
		 */
		printf("%s", isolate
		       ? _("network        isolated (namespace: no TCP, UDP, DNS or loopback)\n")
		       : net_unrestricted
		       ? _("network        unrestricted TCP\n")
		       : g_nports
		       ? _("network        TCP to listed ports only (UDP NOT covered)\n")
		       : _("network        no TCP (UDP NOT covered)\n"));
		for (size_t k = 0; k < g_nports; k++)
			printf(_("  tcp port     %u\n"), g_ports[k]);
		for (size_t k = 0; k < g_nrules; k++) {
			/* ⚠ `rw`/`rx`/`ro` ARE THE FLAG SPELLINGS — what you typed —
			 * so they stay as they are while the note beside them moves. */
			const char *kind = (g_rules[k].rights & A_WRITE) ? "rw"
			                 : (g_rules[k].rights & A_EXEC)   ? "rx" : "ro";
			bool exists = access(g_rules[k].path, F_OK) == 0;
			if (exists)
				printf("  %-2s %s\n", kind, g_rules[k].path);
			else
				printf(_("  %-2s %-28s (absent, skipped)\n"),
				       kind, g_rules[k].path);
		}
		return 0;
	}

	/* The namespace goes up BEFORE the ruleset, because writing the uid maps
	 * needs /proc/self writable and the ruleset is about to make /proc
	 * read-only. */
	if (isolate)
		isolate_net();

	int rfd = (int)ll_create(&attr, sizeof attr, 0);
	if (rfd < 0)
		die(_("landlock_create_ruleset: %s"), strerror(errno));

	for (size_t k = 0; k < g_nrules; k++) {
		int pfd = open(g_rules[k].path, O_PATH | O_CLOEXEC);
		if (pfd < 0) {
			if (g_rules[k].required)
				die("%s: %s", g_rules[k].path, strerror(errno));
			continue;     /* an absent base path is not an error */
		}

		uint64_t rights = abi_mask(abi, g_rules[k].rights);

		/* Trim to what the target can actually hold. A directory-only right
		 * on a file is EINVAL for the whole rule, and one rejected rule
		 * aborts the entire sandbox. */
		struct stat st;
		if (fstat(pfd, &st) == 0 && !S_ISDIR(st.st_mode))
			rights &= A_FILE_ONLY;

		struct landlock_path_beneath_attr pb = {
			.allowed_access = rights,
			.parent_fd = pfd,
		};
		if (ll_add(rfd, LANDLOCK_RULE_PATH_BENEATH, &pb, 0))
			die(_("landlock_add_rule %s: %s"), g_rules[k].path, strerror(errno));
		close(pfd);
	}

	for (size_t k = 0; k < g_nports; k++) {
		struct landlock_net_port_attr np = {
			.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP,
			.port = g_ports[k],
		};
		if (ll_add(rfd, LANDLOCK_RULE_NET_PORT, &np, 0))
			die(_("landlock_add_rule tcp/%u: %s"), g_ports[k], strerror(errno));
	}

	/* Landlock requires no_new_privs, which also means no setuid binary can
	 * raise privilege from in here — sudo and pkexec stop working inside the
	 * sandbox. That is not a side effect to work around; it is half the
	 * point. */
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
		die(_("prctl(NO_NEW_PRIVS): %s"), strerror(errno));
	if (ll_restrict(rfd, 0))
		die(_("landlock_restrict_self: %s"), strerror(errno));
	close(rfd);

	execvp(argv[i], &argv[i]);
	/* Reached only if exec failed. Not die(): 78 means "no sandbox", and by
	 * here the sandbox is very much on. */
	fprintf(stderr, "%s: %s: %s\n", g_prog, argv[i], strerror(errno));
	return 127;
}
